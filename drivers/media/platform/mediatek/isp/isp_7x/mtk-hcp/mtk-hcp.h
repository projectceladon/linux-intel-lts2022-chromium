/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef MTK_HCP_H
#define MTK_HCP_H

#include <linux/fdtable.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>

#include <uapi/linux/dma-heap.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/dma-heap.h>
#include <linux/scatterlist.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>

#include "../imgsys/mtk-img-ipi.h"

/*
 * HCP (Hetero Control Processor ) is a tiny processor controlling
 * the methodology of register programming. If the module support
 * to run on CM4 then it will send data to CM4 to program register.
 * Or it will send the data to user library and let RED to program
 * register.
 */

typedef void (*hcp_handler_t) (void *data, unsigned int len, void *priv);

/**
 * hcp ID definition
 */
enum hcp_id {
	HCP_INIT_ID = 0,
	HCP_ISP_CMD_ID,
	HCP_ISP_FRAME_ID,
	HCP_DIP_INIT_ID,
	HCP_IMGSYS_INIT_ID = HCP_DIP_INIT_ID,
	HCP_DIP_FRAME_ID,
	HCP_IMGSYS_FRAME_ID = HCP_DIP_FRAME_ID,
	HCP_DIP_HW_TIMEOUT_ID,
	HCP_IMGSYS_HW_TIMEOUT_ID = HCP_DIP_HW_TIMEOUT_ID,
	HCP_IMGSYS_SW_TIMEOUT_ID,
	HCP_DIP_DEQUE_DUMP_ID,
	HCP_IMGSYS_DEQUE_DONE_ID,
	HCP_IMGSYS_DEINIT_ID,
	HCP_IMGSYS_IOVA_FDS_ADD_ID,
	HCP_IMGSYS_IOVA_FDS_DEL_ID,
	HCP_IMGSYS_UVA_FDS_ADD_ID,
	HCP_IMGSYS_UVA_FDS_DEL_ID,
	HCP_IMGSYS_SET_CONTROL_ID,
	HCP_IMGSYS_GET_CONTROL_ID,
	HCP_IMGSYS_CLEAR_HWTOKEN_ID,
	HCP_FD_CMD_ID,
	HCP_FD_FRAME_ID,
	HCP_RSC_INIT_ID,
	HCP_RSC_FRAME_ID,
	HCP_MAX_ID,
};

/**
 * mtk_hcp_register - register an hcp function
 *
 * @pdev: HCP platform device
 * @id: HCP ID
 * @handler: HCP handler
 * @name: HCP name
 * @priv: private data for HCP handler
 *
 * Return: Return 0 if hcp registers successfully, otherwise it is failed.
 */
int mtk_hcp_register(struct platform_device *pdev, enum hcp_id id,
		     hcp_handler_t handler, const char *name, void *priv);

/**
 * mtk_hcp_unregister - unregister an hcp function
 *
 * @pdev: HCP platform device
 * @id: HCP ID
 *
 * Return: Return 0 if hcp unregisters successfully, otherwise it is failed.
 */
int mtk_hcp_unregister(struct platform_device *pdev, enum hcp_id id);

/**
 * mtk_hcp_send - send data from camera kernel driver to HCP and wait the
 *      command send to demand.
 *
 * @pdev:   HCP platform device
 * @id:     HCP ID
 * @buf:    the data buffer
 * @len:    the data buffer length
 * @frame_no: frame number
 *
 * This function is thread-safe. When this function returns,
 * HCP has received the data and save the data in the workqueue.
 * After that it will schedule work to dequeue to send data to CM4 or
 * RED for programming register.
 * Return: Return 0 if sending data successfully, otherwise it is failed.
 */
int mtk_hcp_send(struct platform_device *pdev,
		 enum hcp_id id, void *buf,
		 unsigned int len, int req_fd);

/**
 * mtk_hcp_send_async - send data from camera kernel driver to HCP without
 *      waiting demand receives the command.
 *
 * @pdev:   HCP platform device
 * @id:     HCP ID
 * @buf:    the data buffer
 * @len:    the data buffer length
 * @frame_no: frame number
 *
 * This function is thread-safe. When this function returns,
 * HCP has received the data and save the data in the workqueue.
 * After that it will schedule work to dequeue to send data to CM4 or
 * RED for programming register.
 * Return: Return 0 if sending data successfully, otherwise it is failed.
 */
int mtk_hcp_send_async(struct platform_device *pdev,
		       enum hcp_id id, void *buf,
		       unsigned int len, int frame_no);

/**
 * mtk_hcp_get_plat_device - get HCP's platform device
 *
 * @pdev: the platform device of the module requesting HCP platform
 *    device for using HCP API.
 *
 * Return: Return NULL if it is failed.
 * otherwise it is HCP's platform device
 */
struct platform_device *mtk_hcp_get_plat_device(struct platform_device *pdev);

/**
 * mtk_hcp_allocate_working_buffer - allocate driver working buffer.
 *
 * @pdev: HCP platform device
 *
 * This function allocate working buffers and store the information
 * in mtk_hcp_reserve_mblock.
 */
int mtk_hcp_allocate_working_buffer(struct platform_device *pdev, unsigned int mode);

/**
 * mtk_hcp_release_working_buffer - release driver working buffer.
 *
 * @pdev: HCP platform device
 *
 * This function release working buffers
 */
int mtk_hcp_release_working_buffer(struct platform_device *pdev);

void *mtk_hcp_get_gce_mem_virt(struct platform_device *pdev);
phys_addr_t mtk_hcp_get_gce_mem_size(struct platform_device *pdev);
int mtk_hcp_get_init_info(struct platform_device *pdev, struct img_init_info *info);

void *mtk_hcp_get_reserve_mem_virt(struct platform_device *pdev, unsigned int id);

/**
 * struct hcp_desc - hcp descriptor
 *
 * @handler:      IPI handler
 * @name:         the name of IPI handler
 * @priv:         the private data of IPI handler
 */
struct hcp_desc {
	hcp_handler_t handler;
	const char *name;
	void *priv;
};

struct object_id {
	union {
		struct send {
			u32 hcp: 5;
			u32 ack: 1;
			u32 req: 10;
			u32 seq: 16;
		} __packed send;

		u32 cmd;
	};
} __packed;

#define HCP_SHARE_BUF_SIZE      576

/**
 * struct share_buf - DTCM (Data Tightly-Coupled Memory) buffer shared with
 *                    RED and HCP
 *
 * @id:             hcp id
 * @len:            share buffer length
 * @share_buf:      share buffer data
 */
struct share_buf {
	u32 id;
	u32 len;
	u8 share_data[HCP_SHARE_BUF_SIZE];
	struct object_id info;
};

struct mtk_hcp {
	struct hcp_desc hcp_desc_table[HCP_MAX_ID];
	struct device *dev;
	const struct mtk_hcp_data *data;
	struct mtk_scp *scp;
	struct device *scp_dev;
	phandle rproc_phandle;
	struct rproc *rproc_handle;
};

struct mtk_hcp_data {
	struct mtk_hcp_reserve_mblock *mblock;
	unsigned int block_num;
	int (*allocate)(struct mtk_hcp *hcp_dev, unsigned int smvr);
	int (*release)(struct mtk_hcp *hcp_dev);
	int (*get_init_info)(struct img_init_info *info);
	void* (*get_gce_virt)(void);
	int (*get_gce)(void);
	int (*put_gce)(void);
	phys_addr_t (*get_gce_mem_size)(void);
	void* (*get_reserve_mem_virt)(unsigned int id);
};

#define HCP_RESERVED_MEM  (1)

#if HCP_RESERVED_MEM
/**
 * struct mtk_hcp_reserve_mblock - info about memory buffer allocated in kernel
 *
 * @num:        vb2_dc_buf
 * @start_phys:     starting addr(phy/iova) about allocated buffer
 * @start_virt:     starting addr(kva) about allocated buffer
 * @start_dma:      starting addr(iova) about allocated buffer
 * @size:       allocated buffer size
 * @is_dma_buf:     attribute: is_dma_buf or not
 * @mmap_cnt:     counter about mmap times
 * @mem_priv:     vb2_dc_buf
 * @d_buf:        dma_buf
 * @fd:         buffer fd
 */
struct mtk_hcp_reserve_mblock {
	const char *name;
	unsigned int num;
	phys_addr_t start_phys;
	void *start_virt;
	phys_addr_t start_dma;
	phys_addr_t size;
	u8 is_dma_buf;
	/*new add*/
	int mmap_cnt;
	void *mem_priv;
	struct dma_buf *d_buf;
	int fd;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct kref kref;
	struct iosys_map map;
};

#endif

#endif /* _MTK_HCP_H */
