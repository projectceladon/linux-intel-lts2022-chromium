/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CAM_ICP_CONTEXT_H_
#define _CAM_ICP_CONTEXT_H_

#include "cam_context.h"
#include "cam_icp_hw_mgr_intf.h"

/**
 * struct cam_icp_context - icp context
 * @base: icp context object
 * @state_machine: state machine for ICP context
 * @req_base: common request structure
 * @state: icp context state
 * @ctxt_to_hw_map: context to FW handle mapping
 */
struct cam_icp_context {
	struct cam_context *base;
	struct cam_ctx_ops *state_machine;
	struct cam_ctx_request req_base[CAM_ICP_CTX_REQ_MAX];
	uint32_t state;
	void *ctxt_to_hw_map;
};

/**
 * cam_icp_context_init() - ICP context init
 * @ctx: Pointer to context
 * @hw_intf: Pointer to ICP hardware interface
 * @ctx_id: ID for this context
 */
int cam_icp_context_init(struct cam_icp_context *ctx,
	struct cam_hw_mgr_intf *hw_intf, uint32_t ctx_id);

/**
 * cam_icp_context_deinit() - ICP context deinit
 * @ctx: Pointer to context
 */
int cam_icp_context_deinit(struct cam_icp_context *ctx);

const char *cam_icp_dev_name(void);

#endif /* _CAM_ICP_CONTEXT_H_ */
