// SPDX-License-Identifier: GPL-2.0-only
/*
 * OPTEE Secure Graphics DRM TA Wire Transport/Protocol/{De}Serialization
 *
 */


#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/byteorder/little_endian.h>

#include "optee-sg-abi.h"

/* Deserialization Helper */
static void sg_read_u32(u8 *buf, size_t *offset, size_t size, u32 *value)
{
    // TODO: add size related checks here please post being passed from sg_deserialize_response
    if (size - *offset < sizeof(u32))
	return;

    u32 v;
    memcpy(&v, buf + *offset, sizeof(v));
    *offset += sizeof(v);

    *value = le32_to_cpu(v);
}



/* Serialization Helpers */
static void sg_write_u32(u8 *buf, size_t *offset, u32 value)
{
    pr_info("sg_write_u32: buf=%px offset_ptr=%px offset=%zu value=%u\n",
            buf, offset, offset ? *offset : 0, value); 
    u32 v = cpu_to_le32(value);
    memcpy(buf + *offset, &v, sizeof(v));
    *offset += sizeof(v);
}

static void sg_write_u64(u8 *buf, size_t *offset, uint64_t value)
{
    uint64_t v = cpu_to_le64(value);
    memcpy(buf + *offset, &v, sizeof(v));
    *offset += sizeof(v);
}

static void sg_write_s32(u8 *buf, size_t *offset, s32 value)
{
    s32 v = cpu_to_le32(value);
    memcpy(buf + *offset, &v, sizeof(v));
    *offset += sizeof(v);
}

static void sg_serialize_crtc(u8 *buf, size_t *offset,
	const struct sg_crtc_metadata *crtc)
{
    pr_info("entered sg_serialize_crtc\n");
    sg_write_u32(buf, offset, crtc->crtc_id);
    sg_write_u32(buf, offset, crtc->enable);
    sg_write_u32(buf, offset, crtc->active);
    sg_write_u32(buf, offset, crtc->mode_width);
    sg_write_u32(buf, offset, crtc->mode_height);
    sg_write_u32(buf, offset, crtc->refresh_millihz);
}

static void sg_serialize_connector(u8 *buf, size_t *offset,
	const struct sg_connector_metadata *connector)
{
    pr_info("entered sg_serialize_connector\n");
    sg_write_u32(buf, offset, connector->connector_id);
    sg_write_u32(buf, offset, connector->crtc_id);
}

static void sg_serialize_buffer(u8 *buf, size_t *offset,
	const struct sg_buffer_metadata *buffer)
{
    pr_info("entered sg_serialize_buffer\n");
    sg_write_u32(buf, offset, buffer->security_class);
}

static void sg_serialize_plane(u8 *buf, size_t *offset,
	const struct sg_plane_metadata *plane)
{
    pr_info("entered sg_serialize_plane\n");
    sg_write_u32(buf, offset, plane->plane_id);
    sg_write_u32(buf, offset, plane->crtc_id);
    sg_write_u32(buf, offset, plane->fb_id);
    sg_write_u32(buf, offset, plane->fb_width);
    sg_write_u32(buf, offset, plane->fb_height);
    sg_write_u32(buf, offset, plane->format);

    sg_write_u64(buf, offset, plane->modifier);

    sg_write_u32(buf, offset, plane->src_x);
    sg_write_u32(buf, offset, plane->src_y);
    sg_write_u32(buf, offset, plane->src_width);
    sg_write_u32(buf, offset, plane->src_height);

    sg_write_s32(buf, offset, plane->dst_x);
    sg_write_s32(buf, offset, plane->dst_y);

    sg_write_u32(buf, offset, plane->dst_width);
    sg_write_u32(buf, offset, plane->dst_height);
    sg_write_u32(buf, offset, plane->num_fb_planes);

    for (u32 i = 0; i < plane->num_fb_planes; i++) {
	sg_serialize_buffer(buf, offset, &plane->buffers[i]);
    }

}

void sg_serialize_header(u8 *buf, size_t *offset,
	const struct sg_request_header *header)
{
    pr_info("entered sg_serialize_header\n");
    sg_write_u32(buf, offset, header->version);
    sg_write_u32(buf, offset, header->command);
    sg_write_u32(buf, offset, header->size);
    sg_write_u32(buf, offset, header->response_offset);
    sg_write_u32(buf, offset, header->response_size);
}

void sg_serialize_transaction(u8 *buf, size_t *offset,
	const struct sg_transaction_metadata *metadata)
{
    pr_info("entered sg_serialize_transaction\n");
    pr_info("sg_serialize_transaction: buf=%px offset=%zu metadata=%px\n",buf, *offset, metadata);
    sg_write_u32(buf, offset, metadata->version);


    sg_write_u32(buf, offset, metadata->num_crtcs);
    sg_write_u32(buf, offset, metadata->num_connectors);
    sg_write_u32(buf, offset, metadata->num_planes);

    for (u32 i = 0; i < metadata->num_crtcs; i++) {
	sg_serialize_crtc(buf, offset, &metadata->crtcs[i]);
    }

    for (u32 i = 0; i < metadata->num_connectors; i++) {
	sg_serialize_connector(buf, offset, &metadata->connectors[i]);
    }

    for (u32 i = 0; i < metadata->num_planes; i++) {
	sg_serialize_plane(buf, offset, &metadata->planes[i]);
    }

}

int sg_serialize_request(u8 *buffer, size_t size,
		struct sg_validate_request *request)
{
	pr_info("entered sg_serialize_request: buffer=%px \n",buffer);

	size_t offset = sizeof(struct sg_request_header);
	pr_info("proposed reserving offset: %zu\n", offset);

	sg_serialize_transaction(buffer, &offset, &request->metadata);
	pr_info("post sg_serialize_transaction offset: %lu\n", offset);
	request->header.size = offset;
	request->header.response_offset = offset;

	offset = 0;

	sg_serialize_header(buffer, &offset, &request->header);

	return 0;
}

int sg_deserialize_response(u8 *buffer, size_t offset, size_t size, 
		struct sg_validate_response *response)
{
    sg_read_u32(buffer, &offset, size, &response->version);
    sg_read_u32(buffer, &offset, size, &response->decision);
    sg_read_u32(buffer, &offset, size, &response->reason);

    return 0;
}

void print_serialized_bytes(u8 *buf, u32 req_size, u32 offset)
{
    pr_info("serialized request size: %u\n", req_size);
    pr_info("response offset: %u\n", offset);

    pr_info("hex dump:");

    print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE,
                   8, 1, buf, req_size, false);}

void generate_sg_request_header(struct sg_request_header *header, u32 size)
{
	header->version = SG_ABI_VERSION;
	header->command = SG_CMD_VALIDATE_TRANSACTION;
	header->size = size;
	header->response_offset = size;
	header->response_size = sizeof(struct sg_validate_response);
}

void print_sg_request_header(struct sg_request_header *header)
{
	pr_info("struct sg_request_header:\n");
	pr_info("SG_ABI_VERSION: %u\n", header->version);
	pr_info("COMMAND: %u\n", header->command);
	pr_info("SIZE: %u\n", header->size);
	pr_info("RESPONSE_OFFSET: %u\n", header->response_offset);
	pr_info("RESPONSE_SIZE: %u\n", header->response_size);
}

void print_sg_response(struct sg_validate_response *resp)
{
	pr_info("struct sg_validate_response:\n");
	pr_info("VERSION: %u\n",resp->version);
	pr_info("DECISION: %u\n",resp->decision);
	pr_info("REASON: %u\n",resp->reason);
}
