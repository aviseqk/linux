// SPDX-License-Identifier: GPL-2.0

#ifndef __DRM_SECURE_GRAPHICS_H__
#define __DRM_SECURE_GRAPHICS_H__
/*
 * Secure Graphics Validator Function 
 *
 * This is the entrypoint into Secure Graphics
 * Validation logic using transaction commit 
 * from the core drm_atomic_commit call
 * */

struct drm_atomic_commit;

int drm_secure_graphics_validate(
		struct drm_atomic_commit *state);

#endif


