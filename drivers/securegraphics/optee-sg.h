// SPDX-License-Identifier: GPL-2.0-only
/*
 * OPTEE Secure Graphics DRM Path TA Client
 *
 */

#ifndef __OPTEE_SG_H__
#define __OPTEE_SG_H__

#include <drm/drm_secure_graphics.h>
#include "optee-sg-abi.h"

enum sg_validate_decision {
	SG_VALIDATE_ALLOW,
	SG_VALIDATE_DENY,
};

struct sg_validate_result {
	enum sg_validate_decision decision;
	int reason;
};

#define SG_REASON_TEE_NOT_READY	404

/* C API exposed to DRM for communication with the TEE layer */
/*
int sg_tee_validate(const struct sg_transaction_metadata *metadata, 
		struct sg_validate_result *result);*/


#endif
