// SPDX-License-Identifier: GPL-2.0-only
/*
 * SG-TEE Interface Implementation
 */

#include <linux/mutex.h>

#include "sg-tee.h"
#include "optee-sg.h"


static DEFINE_MUTEX(sg_tee_registry_lock);

/* Global Pointer to the registry's currently registered provider to the driver */
static struct sg_tee_provider *registered_provider;


int sg_tee_register_provider(struct sg_tee_provider *provider)
{
	int ret = 0;

	mutex_lock(&sg_tee_registry_lock);

	if (registered_provider) {
		ret = -EBUSY;
		goto out;
	}

	registered_provider = provider;
out:
	mutex_unlock(&sg_tee_registry_lock);

	return ret;
}

void sg_tee_unregister_provider(struct sg_tee_provider *provider)
{
	mutex_lock(&sg_tee_registry_lock);

	if (registered_provider == provider)
		registered_provider = NULL;

	mutex_unlock(&sg_tee_registry_lock);
}


/*TODO: 1. change the registry lock lifetime and 2. also inspect lock scope of callback's data->lock
 * 1. right now, the registry lock is held for entire OP-TEE function invocation lifetime.
 * This lock prevents the unregister/removal path from racing with active validation callback but also 
 * it holds the mutex lock while it calls the provider callback as well including when tee_client_invoke_func is called
 *
 * for current work, this is okay, but can become a performance issue later on because of frequency of drm atomic calls
 * Change it to a lifetime or a reference counting based mechanism so that provider can be safely acquired under
 * registry lock, the lock then can be released, and the callback can execute without holding the registry lock
 *
 * 2. data->lock in the callback function holds the lock for shared TEE state, the memory buffer as it currently uses
 * a single shared SHM buffer for validation transaction with TEE, but later if validation via TEE TA is to become
 * parallel or per-request SHM/state or even buffer pool, the lock strategy must be revisited
 */
int sg_tee_validate(const struct sg_transaction_metadata *metadata, struct sg_validate_result *result)
{
	struct sg_tee_provider *provider;
	int ret;

	mutex_lock(&sg_tee_registry_lock);

	provider = registered_provider;

	if (!provider) {
		mutex_unlock(&sg_tee_registry_lock);

		result->decision = SG_VALIDATE_ALLOW;
		result->reason = SG_REASON_TEE_NOT_READY;
		return 0;
	}

	ret = provider->validate(provider->priv,
			metadata, result);

	mutex_unlock(&sg_tee_registry_lock);

	return ret;
}
