// SPDX-License-Identifier: GPL-2.0-only
/*
 * OPTEE Secure Graphics TA Client Test Driver
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#define DRIVER_NAME	"optee-sg-test"

#define SG_TEST_SHM_SIZE	256

#define SG_TEST_SAMPLE_MESSAGE "Hello from Linux Kernel via Shared Memory"

/*
 * TA_SG_TEE_TEST_SAY_MESSAGE - Say Hi and receive Hi back from SG TEE TA
 *
 * param[0] - MEMREF_INOUT - Shared memory buffer with the message as a string
 * param[1...3] - NONE
 *
 * Result:
 * TEE_SUCCESS
 * TEE_BAD_PARAMETERS
 * */
#define TA_SG_TEE_TEST_SAY_MESSAGE	0x0


static int optee_ctx_match(struct tee_ioctl_version_data *ver,
			   const void *data)
{
	return ver->impl_id == TEE_IMPL_ID_OPTEE;
}


/* 
 * since this is a dumb driver just sending and receiving 
 * messages from the OPTEE TA using the shared memory buffer pointer
 * we include all the steps in the probe() functionality deliberately
 *
 * The steps are:
 * 1. open TEE Context
 * 2. open session to our TA
 * 3. allocate kernel SHM
 * 4. write a message from linux kernel
 * 5. construct MEMREF_INOUT 
 * 6. invoke our command
 * 7. read modified buffer returned from the TA
 * 8. free SHM 
 * 9. close session
 * 10. close context
 *
 * */
static int optee_sg_test_probe(struct tee_client_device *sg_test_device)
{
	struct tee_context *ctx;
	struct tee_shm *shm;
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	char *buffer;
	int ret;

	struct device *dev = &sg_test_device->dev;
	dev_info(dev, "optee_sg_test driver probed\n");

	
	//1. Open context with the OP-TEE driver 
	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx)) {
		dev_err(dev, "failed to open a TEE Context\n");
		return PTR_ERR(ctx);
	}

	// 2. Open a session with our TA
	memset(&sess_arg, 0, sizeof(sess_arg));

	export_uuid(sess_arg.uuid, &sg_test_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		dev_err(dev, "failed to open TA session: ret-%d ta_ret-%d\n",
				ret, sess_arg.ret);
		ret = ret < 0 ? ret : -EINVAL;
		goto err_close_ctx;
	}

	// 3A. Allocate shared memory
	shm = tee_shm_alloc_kernel_buf(ctx, SG_TEST_SHM_SIZE);
	if (IS_ERR(shm)) {
		ret = PTR_ERR(shm);
		dev_err(dev, "failed to allocate shared memory: %d\n", ret);
		goto err_close_session;
	}

	// 3B. get the kernel virtual address of the shared buffer
	buffer = tee_shm_get_va(shm, 0);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		dev_err(dev, "failed to get shared memory VA: %d", ret);
		goto err_free_shm;
	}

	// 4. write a message from linux kernel in to the shared buffer
	snprintf(buffer, SG_TEST_SHM_SIZE, SG_TEST_SAMPLE_MESSAGE);
	dev_info(dev, "sending to TA: \"%s\"\n", buffer);

	// 5A. prepare the invoke arguments, the MEMREF_INOUT .. TODO: make this a different static function
	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(param, 0, sizeof(param));

	inv_arg.func = TA_SG_TEE_TEST_SAY_MESSAGE;
	inv_arg.session = sess_arg.session;
	inv_arg.num_params = 4;

	// 5B. preparing the parameters param[0]->MEMREF_INOUT param[1..3]->NONE
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = shm;
	param[0].u.memref.shm_offs = 0;
	param[0].u.memref.size = SG_TEST_SHM_SIZE;

	// 6. Invoke our TA with the command
	ret = tee_client_invoke_func(ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA invocation failed: ret=%d, ta_ret=%#x\n",
			ret, inv_arg.ret);
		ret = ret < 0 ? ret : -EINVAL;
		goto err_free_shm;	
	}

	// 7. TA has modified the same buffer, READ the modification
	dev_info(dev, "received from TA: \"%s\"\n", buffer);

	// 8..9..10 Cleanup the buffer, the session and the context
	tee_shm_free(shm);
	tee_client_close_session(ctx, sess_arg.session);
	tee_client_close_context(ctx);

	return 0;

err_free_shm:
	tee_shm_free(shm);

err_close_session:
	tee_client_close_session(ctx, sess_arg.session);

err_close_ctx:
	tee_client_close_context(ctx);

	return ret;
}

static void optee_sg_test_remove(struct tee_client_device *sg_test_device)
{
	dev_info(&sg_test_device->dev, "optee_sg_test driver successfully removed\n");
}

static const struct tee_client_device_id optee_sg_test_client_id_table[] = {
	{UUID_INIT(0xcc6c3285, 0xe725, 0x4249,
			0xbf, 0xc4, 0x3a, 0xd5, 0xa5, 0x58, 0xe0, 0x51)},
	{}
};

MODULE_DEVICE_TABLE(tee, optee_sg_test_client_id_table);

static struct tee_client_driver optee_sg_test_driver = {
	.probe = optee_sg_test_probe,
	.remove = optee_sg_test_remove,
	.id_table =  optee_sg_test_client_id_table,
	.driver = {
		.name = DRIVER_NAME,
	},
};

module_tee_client_driver(optee_sg_test_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Abhishek Kumar");
MODULE_DESCRIPTION("OPTEE based shared-memory bi-directional messaging test driver");
