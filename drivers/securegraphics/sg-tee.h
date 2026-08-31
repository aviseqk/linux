// SPDX-License-Identifier: GPL-2.0-only
/*
 * SG-TEE Interface Header File
 */

#ifndef __SG_TEE_H__
#define __SG_TEE_H__

#include <drm/drm_secure_graphics.h>
#include "optee-sg.h"

/*
 * struct sg_tee_data - represents the implementation of the SG validation service 
 * @validate:	Callback registered to be called for SG-TEE validation
 * @priv:	Private context associated to the callback, hence when callback is invoked,
 *		the private struct can be recovered like `struct sg_tee_data *data = priv`
 *
 * */
struct sg_tee_provider {
	int (*validate)(void *priv,
			const struct sg_transaction_metadata *metadata,
			struct sg_validate_result *result);

	void *priv;
};

/* Registration API of the SG-TEE infrastructure, making the current provider persistent in the driver file */
int sg_tee_register_provider(struct sg_tee_provider *provider);

/* Unregistration API of the SG-TEE infrastructure */
void sg_tee_unregister_provider(struct sg_tee_provider *provider);

/* 
 * DRM-exposed public API as entrypoint to the SG framework, and this function acts a dispatcher,
 * it finds out the registered sg-tee provider in the current driver and calls its validate callback 
 * */
int sg_tee_validate(const struct sg_transaction_metadata *metadata,
		struct sg_validate_result *result);

#endif
