// SPDX-License-Identifier: GPL-2.0-only
/*
 * OPTEE Secure Graphics DRM TA Wire Protocol 
 *
 */

#ifndef __OPTEE_SG_ABI_H__
#define __OPTEE_SG_ABI_H__

#include <drm/drm_secure_graphics.h>

#define SG_ABI_VERSION		1


#define SG_CMD_VALIDATE_TRANSACTION		0x01

enum sg_decision {
	SG_DECISION_ALLOW = 0,
	SG_DECISION_DENY = 1,
	SG_DECISION_ERROR = 2,
};

enum sg_reason {
	SG_REASON_POLICY_ALLOWED = 0,
	SG_REASON_POLICY_DENIED = 1,
	SG_REASON_INVALID_METADATA = 2,
	SG_REASON_UNSUPPORTED_VERSION = 3,
};

struct sg_request_header {
	uint32_t version;
	uint32_t command;
	uint32_t size;
	uint32_t response_offset;
	uint32_t response_size;
};

struct sg_validate_request {
	struct sg_request_header header;
	struct sg_transaction_metadata metadata;
};

struct sg_validate_response {
	uint32_t version;
	uint32_t decision;
	uint32_t reason;
};

void sg_serialize_transaction(u8 *buf, size_t *offset,
	const struct sg_transaction_metadata *metadata);

void sg_serialize_header(u8 *buf, size_t *offset,
	const struct sg_request_header *header);


void print_sg_response(struct sg_validate_response *resp);
void print_sg_request_header(struct sg_request_header *header);
void print_serialized_bytes(u8 *buf, u32 req_size, u32 offset);
void generate_sg_request_header(struct sg_request_header *header, u32 size);


/* APIs exposed to TEE Client Driver to invoke serialization sequence */
int sg_serialize_request(u8 *buffer, size_t size,
		struct sg_validate_request *request);

int sg_deserialize_response(u8 *buffer, size_t offset, size_t size, 
		struct sg_validate_response *response);

#endif

