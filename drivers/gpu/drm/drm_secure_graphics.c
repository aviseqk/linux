// SPDX-License-Identifier: GPL-2.0

#include <linux/string.h>

#include <drm/drm_atomic.h>
#include <drm/drm_print.h>
#include <drm/drm_crtc.h>
#include <drm/drm_connector.h>
#include <drm/drm_plane.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>

#include <drm/drm_secure_graphics.h>

#include "drm_secure_graphics_internal.h"

/*
 * Security Provenance Classification
 *
 * This function checks and classifies the fb backing object
 * and its security classification
 *
 * GEM Object -> Trusted Provenance -> Security Class
 *
 * TODO: Currently every classification makes the fb GEM Object
 * to be classified as SG_BUFFER_NORMAL but this function is isolated
 * and temporarily placeholder-ily implemented.
 * TODO: Post decision of actual security classification strategy, 
 * trusted secure-memory allocation, and registration mechanism 
 * this will be implemented properly
 *
 * */
static enum sg_buffer_security_class 
drm_sg_get_buffer_security_class(struct drm_gem_object *obj)
{
	(void)obj;


	/* TODO: actual security classification will be added later */
	return SG_BUFFER_NORMAL;
}

static int drm_sg_build_crtc_metadata(
		struct drm_atomic_commit *state,
		struct sg_transaction_metadata *metadata)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct sg_crtc_metadata *out;

		if (metadata->num_crtcs > SG_MAX_CRTCS)
			return -E2BIG;

		out = &metadata->crtcs[metadata->num_crtcs];

		out->crtc_id = crtc->base.id;
		out->enable = new_crtc_state->enable;
		out->active = new_crtc_state->active;

		out->mode_width = new_crtc_state->mode.hdisplay;
		out->mode_height = new_crtc_state->mode.vdisplay;

		out->refresh_millihz = drm_mode_vrefresh(&new_crtc_state->mode) * 1000;

		metadata->num_crtcs++;

		drm_dbg_atomic(state->dev,
				"SG: CRTC[%u] enable=%u active=%u "
			       "mode=%ux%u refresh=%umHz\n",
			       out->crtc_id,
			       out->enable,
			       out->active,
			       out->mode_width,
			       out->mode_height,
			       out->refresh_millihz);
	}
	return 0;
}

// TODO: SG Metadata resolution must eventually resolve resulting display state
// using both the atomic commit transaction's new state and existing DRM state too.
//
// ISSUE: if the crtc is alrady connected to the connector, then that connector
// is not at all being recorded in the sg_metadata, and this could happen with other
// objects too. The bigger issue is hand is the "incomplete display pipeline config
// being derived from the `delta-oriented` commit data. 
// Hence, the resolution to be implemented later is to somehow old state, the existing
// DRM state and the new atomic commit transaction data to ultimately derive a canonical
// security-relavant complete display config and pipeline data for the TEE to work with"
//
// ARCHITECTURE POINT: not just use the atomic commit new state, but old DRM state too.

static int drm_sg_build_connector_metadata(
		struct drm_atomic_commit *state,
		struct sg_transaction_metadata *metadata)
{
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	int i;

	drm_dbg_atomic(state->dev, "check_num_connector %d\n", state->num_connector);

	drm_dbg_atomic(state->dev, "before each_new_connector loop\n");
	for_each_new_connector_in_state(state, conn, conn_state, i) {
		struct sg_connector_metadata *out;
		drm_dbg_atomic(state->dev, "entered each_new_connector loop\n");

		if (metadata->num_connectors > SG_MAX_CONNECTORS)
			return -E2BIG;

		out = &metadata->connectors[metadata->num_connectors];

		out->connector_id = conn->base.id;

		if (conn_state->crtc) {
			out->crtc_id = conn_state->crtc->base.id;
		} else {
			out->crtc_id = 0;
		}

		metadata->num_connectors++;
		drm_dbg_atomic(state->dev,
				"SG: Connector[%u] CRTC=%u\n",
				out->connector_id, out->crtc_id);
	}
	drm_dbg_atomic(state->dev, "exited each_new_connector loop\n");
	return 0;
}

static int drm_sg_build_plane_metadata(
		struct drm_atomic_commit *state,
		struct sg_transaction_metadata *metadata)
{
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	int i;

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		struct sg_plane_metadata *out;
		struct drm_framebuffer *fb;
		unsigned int j;

		if (metadata->num_planes > SG_MAX_PLANES)
			return -E2BIG;

		out = &metadata->planes[metadata->num_planes];

		out->plane_id = plane->base.id;
		
		if (new_plane_state->crtc) {
			out->crtc_id = new_plane_state->crtc->base.id;
		} else {
			out->crtc_id = 0;
		}

		fb = new_plane_state->fb;

		/* plane without a framebuffer is a disabled plane */
		if (!fb) {
			metadata->num_planes++;

			drm_dbg_atomic(state->dev,
					"SG: PLANE[%u] CRTC=%u - NO FRAMEBUFFER\n",
					out->plane_id, out->crtc_id);

			continue;
		}

		out->fb_id = fb->base.id;
		out->fb_width = fb->width;
		out->fb_height = fb->height;

		out->modifier = fb->modifier;
		out->format = fb->format->format;

		out->src_x = new_plane_state->src_x >> 16;
		out->src_y = new_plane_state->src_y >> 16;
		out->src_width = new_plane_state->src_w >> 16;
		out->src_height = new_plane_state->src_h >> 16;

		out->dst_x = new_plane_state->crtc_x;
		out->dst_y = new_plane_state->crtc_y;
		out->dst_width = new_plane_state->crtc_w;
		out->dst_height = new_plane_state->crtc_h;

		out->num_fb_planes = fb->format->num_planes;

		if (out->num_fb_planes > SG_MAX_FB_PLANES)
			return -E2BIG;


		/* get security class for each fb backing object */
		for (j = 0; j < out->num_fb_planes; j++) {
			struct drm_gem_object *obj = fb->obj[j];

			out->buffers[j].security_class = drm_sg_get_buffer_security_class(obj);

			drm_dbg_atomic(state->dev,
					"SG: PLANE[%u] CRTC[%u] FB[%u] buffer[%u] security=%u\n",
					out->plane_id, out->crtc_id, out->fb_id, j, out->buffers[j].security_class);
		}

		metadata->num_planes++;

		drm_dbg_atomic(state->dev, 
				"SG: PLANE[%u] -> CRTC[%u] "
				"FB[%u] %ux%u format=%08x modifier=%016llx "
				"src=%u,%u %ux%u "
				"dst=%u,%u %ux%u "
				"fb_planes=%u\n",
				out->plane_id, out->crtc_id,
				out->fb_id, out->fb_width, out->fb_height,
				out->format, out->modifier,
				out->src_x, out->src_y, out->src_width, out->src_height,
				out->dst_x, out->dst_y, out->dst_width, out->dst_height,
				out->num_fb_planes);
	}

	return 0;
}


int drm_secure_graphics_build_metadata(
		struct drm_atomic_commit *state,
		struct sg_transaction_metadata *metadata)
{
	drm_dbg_atomic(state->dev,
			"entered drm_secure_graphics_build_metadata fn\n");
	int ret;

	if (!state || !metadata)
		return -EINVAL;
	
	memset(metadata, 0, sizeof(*metadata));

	metadata->version = SG_METADATA_VERSION;

	ret = drm_sg_build_crtc_metadata(state, metadata);
	if (ret)
		return ret;

	ret = drm_sg_build_connector_metadata(state, metadata);
	if (ret)
		return ret;

	ret = drm_sg_build_plane_metadata(state, metadata);
	if (ret)
		return ret;

	drm_dbg_atomic(state->dev,
			"SG: metadata built successfully: "
			"version=%u num_crtcs=%u connectors=%u planes=%u\n",
			metadata->version, metadata->num_crtcs,
			metadata->num_connectors, metadata->num_planes);
	
	return 0;
}

int drm_secure_graphics_validate(
		struct drm_atomic_commit *state)
{
	drm_dbg_atomic(state->dev,
			"entered drm_secure_graphics_validate fn\n");

	/* TODO: currently metadata is put on stack, because we are just building and discarding,
	 * later when we need the metadata to live across TEE SMC Call, we will need to store the metadata
	 * so we will build it somewhere else and store it. */
	struct sg_transaction_metadata sg_metadata;
	int ret;

	/* Metadata Build Step */
	ret = drm_secure_graphics_build_metadata(state, &sg_metadata);
	if (ret)
		return ret;

	/* Validation Step */

	return 0;
}
