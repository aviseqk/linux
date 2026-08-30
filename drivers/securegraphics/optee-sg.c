// SPDX-License-Identifier: GPL-2.0-only
/*
 * OPTEE Secure Graphics DRM Path TA Client Driver
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include "optee-sg.h"
#include "optee-sg-abi.h"

#define DRIVER_NAME	"optee-sg"

// TODO: replace this with either dynamic or other method but not static defined size at all
// use of single static buffer serializes the DRM validation requests, meaning one by one.
#define SG_STATIC_SHM_SIZE	4096

// TODO: not sure if the shm must be a permanent single global buffer, so work on this later

/*
 * struct sg_tee_data - SG TEE Client Object for persistent connection with TA 
 * @ctx:	Persistent connection to TEE subsystem
 * @session_id:	Persistent session to our SG Trusted App
 * @shm:	Shared Memory tee_shm object
 * @shm_va:	Shared Memory kernel VA
 * @shm_size:	Shared Memory Size
 * @lock:	Mutex to protect transport and session operations
 *
 * */
struct sg_tee_data {
	struct tee_context *ctx;
	u32 session_id;

	struct tee_shm *shm;
	void *shm_va;
	size_t shm_size;

	struct mutex lock;
	bool ready;
};

//TODO: make the sg_tee_data to belong to TEE client device as drvdata rather than a global static singleton object
static struct sg_tee_data pvt_data;

int sg_tee_validate(const struct sg_transaction_metadata *metadata, 
		struct sg_validate_result *result)
{
	int ret = 0;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	
	mutex_lock(&pvt_data.lock);

	// init case when first qemu DRM placeholder image boots but the driver aint ready hence no shm_va available
	if (!pvt_data.ready || !pvt_data.shm_va || !pvt_data.shm_size) {
		pr_debug("SG TEE Driver not ready, skipping validation\n");
		result->decision = SG_VALIDATE_ALLOW;
		result->reason = 404; // make it SG_REASON_TEE_NOT_READY
		goto out;
	}

	struct sg_validate_request request = {
		.header = {
			.version = SG_ABI_VERSION,
			.command = SG_CMD_VALIDATE_TRANSACTION,
			.size = 0,
			.response_offset = 0,
			.response_size = sizeof(struct sg_validate_response),
		},
		.metadata = *metadata,
	};

	ret = sg_serialize_request(pvt_data.shm_va, pvt_data.shm_size, &request);
	if (ret)
		goto out;


	print_serialized_bytes(pvt_data.shm_va, request.header.size, request.header.response_offset);

	// prepare the invocation arguments
	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(param, 0, sizeof(param));

	inv_arg.session = pvt_data.session_id;
	inv_arg.func = SG_CMD_VALIDATE_TRANSACTION;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = pvt_data.shm;
	param[0].u.memref.shm_offs = 0;
	param[0].u.memref.size = SG_STATIC_SHM_SIZE;
	
	//pr_info("ctx=%px session=%u shm=%px shm_va=%px\n", pvt_data.ctx, pvt_data.session_id, pvt_data.shm, pvt_data.shm_va);
	//struct tee_context *ctx = pvt_data.ctx;
	//pr_info("ctx       = %px\n", ctx);
	//pr_info("teedev    = %px\n", ctx->teedev);

	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		pr_err("TA invocation failed: ret=%d, ta_ret=%#x\n",
			ret, inv_arg.ret);
		ret = ret < 0 ? ret : -EINVAL;
		goto out;
	}

	// now the TA has responded, and the shared buffer must have been modified
	pr_debug("POST TA RESPONSE\n");
	pr_debug("request.header.size: %u request.header.response_size: %u\n",
			request.header.size, request.header.response_size);
	print_serialized_bytes(pvt_data.shm_va, request.header.size + request.header.response_size, 0);

	//TODO: Resume here: Deserialize the TA returned buffer to get the sg_validate_response
	struct sg_validate_response response;
	sg_deserialize_response(pvt_data.shm_va, request.header.response_offset, request.header.response_size, &response);
	print_sg_response(&response);

	result->decision = response.decision;
	result->reason = response.reason;

	// TODO: fake ptr update, just to check the path being called properly, REMOVE LATER
	//result->decision = SG_VALIDATE_ALLOW;
	//result->reason = 2;

out:
	mutex_unlock(&pvt_data.lock);
	return ret;
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver,
			   const void *data)
{
	return ver->impl_id == TEE_IMPL_ID_OPTEE;
}

static int optee_sg_probe(struct tee_client_device *sg_tee_device)
{
	struct device *dev = &sg_tee_device->dev;
	struct tee_shm *shm;
	char *buffer;
	int ret = 0;
	struct tee_ioctl_open_session_arg sess_arg;

	memset(&pvt_data, 0, sizeof(pvt_data));
	mutex_init(&pvt_data.lock);

	dev_info(dev, "optee_sg driver probed\n");

	// open context with TEE driver
	pvt_data.ctx = tee_client_open_context(NULL,
			optee_ctx_match, NULL, NULL);

	if (IS_ERR(pvt_data.ctx)) {
		ret = PTR_ERR(pvt_data.ctx);
		pvt_data.ctx = NULL;
		dev_err(dev, "tee_client_open_context failed: %d\n", ret);
		return ret;
	}

	// open session with our SG TA
	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &sg_tee_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if (ret < 0 || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed: ret: %d ta_ret: %x\n", ret, sess_arg.ret);
		ret = ret < 0 ? ret : -EINVAL;
		goto out_ctx;
	}
	pvt_data.session_id = sess_arg.session;

	
	// allocate shm
	shm = tee_shm_alloc_kernel_buf(pvt_data.ctx, SG_STATIC_SHM_SIZE);
	if (IS_ERR(shm)) {
		ret = PTR_ERR(shm);
		dev_err(dev, "failed to allocate kernel buffer: %d\n", ret);
		goto out_session;
	}

	// 3B. get the kernel virtual address of the shared buffer
	buffer = tee_shm_get_va(shm, 0);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		dev_err(dev, "failed to get shared memory VA: %d", ret);
		goto out_shm;
	}
	pvt_data.shm = shm;
	pvt_data.shm_va = buffer;
	pvt_data.shm_size = SG_STATIC_SHM_SIZE;
	pvt_data.ready = true;

	dev_info(dev, "TEE Context/Session/SHM initialized successfully\n");

	//dev_info(dev, "optee_sg driver: TEST CASE: fake DRM call with no actual TA call\n");
	return 0;

out_shm:
	tee_shm_free(shm);
out_session:
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);

out_ctx:
	tee_client_close_context(pvt_data.ctx);
	pvt_data.ctx = NULL;

	return ret;
}


static void optee_sg_remove(struct tee_client_device *sg_tee_device)
{
	dev_info(&sg_tee_device->dev, "optee_sg driver successfully removed\n");
}

static const struct tee_client_device_id optee_sg_client_id_table[] = {
	{UUID_INIT(0xa6a197ec, 0xd5cb, 0x4e04,
			0xb7, 0x2c, 0x73, 0x32, 0xec, 0x01, 0xd4, 0x27)},
	{}
};


MODULE_DEVICE_TABLE(tee, optee_sg_client_id_table);

static struct tee_client_driver optee_sg_driver = {
	.probe = optee_sg_probe,
	.remove = optee_sg_remove,
	.id_table = optee_sg_client_id_table,
	.driver = {
		.name = DRIVER_NAME,
	},
};

module_tee_client_driver(optee_sg_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Abhishek Kumar");
MODULE_DESCRIPTION("OPTEE Client Layer Driver for Secure Graphics DRM Requests Policy Validation");
