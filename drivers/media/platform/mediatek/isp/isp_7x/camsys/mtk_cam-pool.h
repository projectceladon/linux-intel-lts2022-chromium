/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 MediaTek Inc.
 */

#ifndef __MTK_CAM_POOL_H
#define __MTK_CAM_POOL_H

struct mtk_cam_ctx;

int mtk_cam_working_buf_pool_init(struct mtk_cam_ctx *ctx, struct device *dev);
void mtk_cam_working_buf_pool_release(struct mtk_cam_ctx *ctx, struct device *dev);
void
mtk_cam_working_buf_put(struct mtk_cam_working_buf_entry *buf_entry);
struct mtk_cam_working_buf_entry*
mtk_cam_working_buf_get(struct mtk_cam_ctx *ctx);

int mtk_cam_img_working_buf_pool_init(struct mtk_cam_ctx *ctx, int buf_num, struct device *dev);
void mtk_cam_img_working_buf_pool_release(struct mtk_cam_ctx *ctx, struct device *dev);
void
mtk_cam_img_working_buf_put(struct mtk_cam_img_working_buf_entry *buf_entry);
struct mtk_cam_img_working_buf_entry*
mtk_cam_img_working_buf_get(struct mtk_cam_ctx *ctx);

#endif
