// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic.h>

#define SG_METADATA_VERSION		1
#define SG_MAX_CRTCS			8
#define SG_MAX_CONNECTORS		8
#define SG_MAX_PLANES			32
#define SG_MAX_FB_PLANES		4

enum sg_buffer_security_class {
	SG_BUFFER_NORMAL = 0,
	SG_BUFFER_SECURE = 1,
	SG_BUFFER_HARDWARE_SECURE = 2,
};

/*
 * Security/Provenance Metadata
 * */
struct sg_buffer_metadata {
	uint32_t security_class;
};


/*
 * Display Pipeline Metadata
 * */
struct sg_crtc_metadata {
	uint32_t crtc_id;
	
	uint32_t enable;
	uint32_t active;

	uint32_t mode_width;
	uint32_t mode_height;
	uint32_t refresh_millihz;
};


/*
 * Plane/Framebuffer Metadata
 * */
struct sg_plane_metadata {
	uint32_t plane_id;
	uint32_t crtc_id;

	uint32_t fb_id;

	uint32_t fb_width;
	uint32_t fb_height;

	uint32_t format;
	uint64_t modifier;

	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_width;
	uint32_t src_height;

	int32_t dst_x;
	int32_t dst_y;
	uint32_t dst_width;
	uint32_t dst_height;

	uint32_t num_fb_planes;
	struct sg_buffer_metadata buffers[SG_MAX_FB_PLANES];
};

/*
 * Connector Metadata
 * */
struct sg_connector_metadata {
	uint32_t connector_id;
	uint32_t crtc_id;
};


/*
 * Transaction Metadata
 * */
struct sg_transaction_metadata {
	uint32_t version;

	uint32_t num_crtcs;
	uint32_t num_connectors;
	uint32_t num_planes;

	struct sg_crtc_metadata crtcs[SG_MAX_CRTCS];

	struct sg_connector_metadata connectors[SG_MAX_CONNECTORS];

	struct sg_plane_metadata planes[SG_MAX_PLANES];
};


/* 
 * Metadata builder functions 
 * */
int drm_secure_graphics_build_metadata(
		struct drm_atomic_commit *state,
		struct sg_transaction_metadata *metadata);



