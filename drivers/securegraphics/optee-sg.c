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
#include "sg-tee.h"

#define DRIVER_NAME	"optee-sg"

// TODO: replace this with either dynamic or other method but not static defined size at all
// use of single static buffer serializes the DRM validation requests, meaning one by one.
#define SG_STATIC_SHM_SIZE	4096

// TODO: not sure if the shm must be a permanent single global buffer, so work on this later

/*
 * struct sg_tee_data - SG TEE Client Driver-owned Object for persistent connection with TA 
 * @dev:	struct device of this TEE Client Driver
 * @ctx:	Persistent connection to TEE subsystem
 * @session_id:	Persistent session to our SG Trusted App
 * @shm:	Shared Memory tee_shm object
 * @shm_va:	Shared Memory kernel VA
 * @shm_size:	Shared Memory Size
 * @lock:	Mutex to protect transport and session operations
 * @provider:	Registered Service Instance connecting the generic sg-tee 
 *		interface to the private driver state
 * */
struct sg_tee_data {
	struct device *dev;

	struct tee_context *ctx;
	u32 session_id;

	struct tee_shm *shm;
	void *shm_va;
	size_t shm_size;

	struct mutex lock;
	bool ready;

	/* registration mechanism */
	struct sg_tee_provider provider;
};

/* TODO: data->lock currently serializes the complete validation transaction as all requests uses the same 
 * SHM buffer and TEE session
 *
 * Change this, if later validation or the communication with TEE TA becomes parallel or per-request SHM/State or Shared Buffer Pool 
 * */

static int sg_tee_validate_impl(void *priv,
		const struct sg_transaction_metadata *metadata,
		struct sg_validate_result *result)
{
	struct sg_tee_data *data = priv;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	int ret;

	mutex_lock(&data->lock);

	// init case when first qemu DRM placeholder image boots but the driver aint ready hence no shm_va available
	if (!data->ready || !data->shm_va || !data->shm_size) {
		dev_dbg(data->dev, "SG TEE Driver not ready, skipping validation\n");

		result->decision = SG_VALIDATE_ALLOW;
		result->reason = SG_REASON_TEE_NOT_READY;

		ret = 0;
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
	

	// serialize the request
	ret = sg_serialize_request(data->shm_va, data->shm_size, &request);
	if (ret)
		goto out;

	print_serialized_bytes(data->shm_va, request.header.size, request.header.response_offset);

	// prepare the invocation arguments
	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(param, 0, sizeof(param));

	inv_arg.session = data->session_id;
	inv_arg.func = SG_CMD_VALIDATE_TRANSACTION;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = data->shm;
	param[0].u.memref.shm_offs = 0;
	param[0].u.memref.size = SG_STATIC_SHM_SIZE;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA invocation failed: ret=%d, ta_ret=%#x\n",
			ret, inv_arg.ret);
		ret = ret < 0 ? ret : -EINVAL;
		goto out;
	}
	
	// now the TA has responded, and the shared buffer must have been modified
	pr_debug("POST TA RESPONSE\nrequest.header.size: %u request.header.response_size: %u\n", 
			request.header.size, request.header.response_size);

	print_serialized_bytes(data->shm_va, request.header.size + request.header.response_size, 0);

	//TODO: Resume here: Deserialize the TA returned buffer to get the sg_validate_response
	struct sg_validate_response response;
	sg_deserialize_response(data->shm_va, request.header.response_offset, request.header.response_size, &response);
	print_sg_response(&response);

	result->decision = response.decision;
	result->reason = response.reason;

out:
	mutex_unlock(&data->lock);
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
	struct sg_tee_data *data;
	struct tee_shm *shm;
	char *buffer;
	int ret = 0;
	struct tee_ioctl_open_session_arg sess_arg;

	dev_info(dev, "optee_sg driver probed\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	mutex_init(&data->lock);

	dev_set_drvdata(dev, data);

	// open context with TEE driver
	data->ctx = tee_client_open_context(NULL,
			optee_ctx_match, NULL, NULL);

	if (IS_ERR(data->ctx)) {
		ret = PTR_ERR(data->ctx);
		data->ctx = NULL;
		dev_err(dev, "tee_client_open_context failed: %d\n", ret);
		return ret;
	}

	// open session with our SG TA
	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &sg_tee_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(data->ctx, &sess_arg, NULL);
	if (ret < 0 || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed: ret: %d ta_ret: %x\n", ret, sess_arg.ret);
		ret = ret < 0 ? ret : -EINVAL;
		goto out_ctx;
	}
	data->session_id = sess_arg.session;

	
	// allocate shm
	data->shm = tee_shm_alloc_kernel_buf(data->ctx, SG_STATIC_SHM_SIZE);
	if (IS_ERR(data->shm)) {
		ret = PTR_ERR(data->shm);
		data->shm = NULL;
		dev_err(dev, "failed to allocate kernel buffer: %d\n", ret);
		goto out_session;
	}

	// 3B. get the kernel virtual address of the shared buffer
	buffer = tee_shm_get_va(data->shm, 0);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		dev_err(dev, "failed to get shared memory VA: %d", ret);
		goto out_shm;
	}

	data->shm_va = buffer;
	data->shm_size = SG_STATIC_SHM_SIZE;
	data->ready = true;

	dev_info(dev, "TEE Context/Session/SHM initialized successfully\n");

	// set up the public SG-TEE provider
	data->provider.validate = sg_tee_validate_impl;
	data->provider.priv = data;

	ret = sg_tee_register_provider(&data->provider);
	if (ret) {
		dev_err(dev, "SG-TEE Provider registration failed\n");
		goto out_shm;
	}

	dev_info(dev, "SG-TEE Provider registered\n");

	return 0;

out_shm:
	tee_shm_free(shm);
out_session:
	tee_client_close_session(data->ctx, data->session_id);

out_ctx:
	tee_client_close_context(data->ctx);
	data->ctx = NULL;

	return ret;
}


static void optee_sg_remove(struct tee_client_device *sg_tee_device)
{
	struct sg_tee_data *data = dev_get_drvdata(&sg_tee_device->dev);

	// TODO: make sure that unregister_provider guarantees that all in-progess validation requests callbacks
	// being serviced by TA is completed before we destroy the @data and all the OP-TEE resources
	// rn, this is made sure by the global registry lock as that is being held all across a single validation
	// transaction request to TA but that might change hence be on lookout for this issue
	sg_tee_unregister_provider(&data->provider);
	data->ready = false;
	
	//TODO: add fallbacks by checking if TEE's session, context and SHM are available, and if so, close them
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
