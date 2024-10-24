// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/iopoll.h>
#include <linux/rpmsg.h>
#include "mdw_cmn.h"
#include "mdw_rv.h"
#include "mdw_rv_msg.h"
#define CREATE_TRACE_POINTS
#include "mdw_rv_events.h"
#include "mdw_mem_rsc.h"

#define MDW_CMD_IPI_TIMEOUT (10*1000) //ms


static void mdw_rv_dev_msg_insert(struct mdw_rv_dev *mrdev,
	struct mdw_ipi_msg_sync *s_msg)
{
	list_add_tail(&s_msg->ud_item, &mrdev->s_list);
}

static void mdw_rv_dev_msg_remove(struct mdw_rv_dev *mrdev,
	struct mdw_ipi_msg_sync *s_msg)
{
	list_del(&s_msg->ud_item);
}

static struct mdw_ipi_msg_sync *mdw_rv_dev_msg_find(struct mdw_rv_dev *mrdev,
	uint64_t sync_id)
{
	struct mdw_ipi_msg_sync *s_msg = NULL, *tmp = NULL;

	mdw_drv_debug("get msg(0x%llx)\n", sync_id);

	list_for_each_entry_safe(s_msg, tmp, &mrdev->s_list, ud_item) {
		if (s_msg->msg.sync_id == sync_id)
			return s_msg;
	}
	return NULL;
}

static int mdw_rv_dev_send_msg(struct mdw_rv_dev *mrdev, struct mdw_ipi_msg_sync *s_msg)
{
	int ret = 0;
	uint32_t cnt = 100, i = 0;

	s_msg->msg.sync_id = (uint64_t)s_msg;
	mdw_drv_debug("sync id(0x%llx) (0x%llx/%u)\n",
		s_msg->msg.sync_id, s_msg->msg.c.iova, s_msg->msg.c.size);

	/* insert to msg list */
	mutex_lock(&mrdev->msg_mtx);
	mdw_rv_dev_msg_insert(mrdev, s_msg);
	mutex_unlock(&mrdev->msg_mtx);

	/* send & retry */
	for (i = 0; i < cnt; i++) {
		ret = rpmsg_send(mrdev->ept, &s_msg->msg, sizeof(s_msg->msg));

		/* send busy, retry */
		if (ret == -EBUSY || ret == -EAGAIN) {
			if (!(i % 10))
				dev_info(mrdev->mdev->dev, "re-send ipi(%u/%u)\n", i, cnt);
			if (ret == -EAGAIN && i < 10)
				usleep_range(200, 500);
			else if (ret == -EAGAIN && i < 50)
				usleep_range(1000, 2000);
			else
				usleep_range(10000, 11000);
			continue;
		}
		break;
	}

	/* send ipi fail, remove msg from list */
	if (ret) {
		dev_err(mrdev->mdev->dev, "[error] send ipi msg(0x%llx) fail(%d)\n",
				s_msg->msg.sync_id, ret);
		mutex_lock(&mrdev->msg_mtx);
		if (mdw_rv_dev_msg_find(mrdev, s_msg->msg.sync_id) == s_msg) {
			dev_info(mrdev->mdev->dev, "[warn] remove ipi msg(0x%llx)\n",
					s_msg->msg.sync_id);
			mdw_rv_dev_msg_remove(mrdev, s_msg);
		} else {
			dev_err(mrdev->mdev->dev, "[error] can't find ipi msg(0x%llx)\n",
					s_msg->msg.sync_id);
		}
		mutex_unlock(&mrdev->msg_mtx);
	}

	return ret;
}

static void mdw_rv_ipi_cmplt_sync(struct mdw_ipi_msg_sync *s_msg)
{
	mdw_flw_debug("\n");
	complete(&s_msg->cmplt);
}

static int mdw_rv_dev_send_sync(struct mdw_rv_dev *mrdev, struct mdw_ipi_msg *msg)
{
	int ret = 0;
	struct mdw_ipi_msg_sync *s_msg = NULL;
	unsigned long timeout = msecs_to_jiffies(MDW_CMD_IPI_TIMEOUT);

	s_msg = kzalloc(sizeof(*s_msg), GFP_KERNEL);
	if (!s_msg)
		return -ENOMEM;

	memcpy(&s_msg->msg, msg, sizeof(*msg));
	init_completion(&s_msg->cmplt);
	s_msg->complete = mdw_rv_ipi_cmplt_sync;

	mutex_lock(&mrdev->mtx);
	/* send */
	ret = mdw_rv_dev_send_msg(mrdev, s_msg);
	if (ret) {
		dev_err(mrdev->mdev->dev, "[error] send msg fail\n");
		goto fail_send_sync;
	}
	mutex_unlock(&mrdev->mtx);

	/* wait for response */
	if (!wait_for_completion_timeout(&s_msg->cmplt, timeout)) {
		dev_err(mrdev->mdev->dev, "[error] ipi no response\n");
		mutex_lock(&mrdev->msg_mtx);
		if (mdw_rv_dev_msg_find(mrdev, s_msg->msg.sync_id) == s_msg) {
			dev_info(mrdev->mdev->dev, "[warn] remove ipi msg(0x%llx)\n",
					s_msg->msg.sync_id);
			mdw_rv_dev_msg_remove(mrdev, s_msg);
		} else {
			dev_err(mrdev->mdev->dev, "[error] can't find ipi msg(0x%llx)\n",
					s_msg->msg.sync_id);
		}
		mutex_unlock(&mrdev->msg_mtx);
		ret = -ETIME;
	} else {
		memcpy(msg, &s_msg->msg, sizeof(*msg));
		ret = msg->ret;
		if (ret)
			dev_info(mrdev->mdev->dev, "[warn] up return fail(%d)\n", ret);
	}

	goto out;

fail_send_sync:
	mutex_unlock(&mrdev->mtx);
out:
	kfree(s_msg);

	return ret;
}

static void mdw_rv_ipi_cmplt_cmd(struct mdw_ipi_msg_sync *s_msg)
{
	int ret = 0;
	struct mdw_rv_cmd *rc =
		container_of(s_msg, struct mdw_rv_cmd, s_msg);
	struct mdw_cmd *c = rc->c;
	struct mdw_rv_dev *mrdev = (struct mdw_rv_dev *)c->mpriv->mdev->dev_specific;

	switch (s_msg->msg.ret) {
	case MDW_IPI_MSG_STATUS_BUSY:
		ret = -EBUSY;
		break;

	case MDW_IPI_MSG_STATUS_ERR:
		ret = -EREMOTEIO;
		break;

	case MDW_IPI_MSG_STATUS_TIMEOUT:
		ret = -ETIME;
		break;

	default:
		break;
	}

	if (ret)
		pr_err("[error] %s: cmd(%p/0x%llx) ret(%d/0x%llx) time(%llu) pid(%d/%d)\n",
				__func__, c->mpriv, c->kid, ret, c->einfos->c.sc_rets,
				c->einfos->c.total_us, c->pid, c->tgid);
	mrdev->cmd_funcs->done(rc, ret);
}

static int mdw_rv_dev_send_cmd(struct mdw_rv_dev *mrdev, struct mdw_rv_cmd *rc)
{
	int ret = 0;

	mdw_drv_debug("pid(%d) run cmd(0x%llx/0x%llx) dva(0x%llx) size(%u)\n",
		task_pid_nr(current), rc->c->kid, rc->c->rvid,
		rc->cb->device_va, rc->cb->size);

	rc->s_msg.msg.id = MDW_IPI_APU_CMD;
	rc->s_msg.msg.c.iova = rc->cb->device_va;
	rc->s_msg.msg.c.size = rc->cb->size;
	rc->s_msg.msg.c.start_ts_ns = rc->start_ts_ns;
	rc->s_msg.complete = mdw_rv_ipi_cmplt_cmd;

	/* send */
	ret = mdw_rv_dev_send_msg(mrdev, &rc->s_msg);
	if (ret) {
		dev_err(mrdev->mdev->dev, "[error] pid(%d) send msg fail\n", task_pid_nr(current));
	}

	return ret;
}

int mdw_rv_dev_run_cmd(struct mdw_fpriv *mpriv, struct mdw_cmd *c)
{
	struct mdw_rv_dev *mrdev = (struct mdw_rv_dev *)mpriv->mdev->dev_specific;
	struct mdw_rv_cmd *rc = NULL;
	int ret = 0;

	rc = mrdev->cmd_funcs->create(mpriv, c);
	if (!rc) {
		ret = -EINVAL;
		goto out;
	}

	mdw_drv_debug("run rc(0%llx)\n", (uint64_t)rc);
	mutex_lock(&mrdev->mtx);
	ret = mdw_rv_dev_send_cmd(mrdev, rc);
	mutex_unlock(&mrdev->mtx);

out:
	return ret;
}

static int mdw_rv_callback(struct rpmsg_device *rpdev, void *data,
	int len, void *priv, u32 src)
{
	struct mdw_ipi_msg *msg = (struct mdw_ipi_msg *)data;
	struct mdw_ipi_msg_sync *s_msg = NULL;
	struct mdw_rv_dev *mrdev = (struct mdw_rv_dev *)priv;

	mdw_drv_debug("callback msg(%d/0x%llx)\n", msg->id, msg->sync_id);

	mutex_lock(&mrdev->msg_mtx);
	s_msg = mdw_rv_dev_msg_find(mrdev, msg->sync_id);
	if (s_msg) {
		memcpy(&s_msg->msg, msg, sizeof(*msg));
		list_del(&s_msg->ud_item);
	}
	mutex_unlock(&mrdev->msg_mtx);

	/* complete callback */
	if (s_msg)
		s_msg->complete(s_msg);

	return 0;
}

int mdw_rv_dev_set_param(struct mdw_rv_dev *mrdev, enum mdw_info_type type, uint32_t val)
{
	struct mdw_ipi_msg msg;
	int ret = 0;

	if (type == MDW_INFO_KLOG) {
		mrdev->mdev->mdw_klog = val;
		goto out;
	} else if (type >= MDW_INFO_MAX) {
		ret = -EINVAL;
		goto out;
	}
	memset(&msg, 0, sizeof(msg));
	msg.id = MDW_IPI_PARAM;
	msg.p.type = type;
	msg.p.dir = MDW_INFO_SET;
	msg.p.value = val;

	mdw_drv_debug("set param(%u/%u/%u)\n", msg.p.type, msg.p.dir, msg.p.value);
	ret = mdw_rv_dev_send_sync(mrdev, &msg);
	if (!ret)
		memcpy(&mrdev->param, &msg.p, sizeof(msg.p));

out:
	return ret;
}

int mdw_rv_dev_get_param(struct mdw_rv_dev *mrdev, enum mdw_info_type type, uint32_t *val)
{
	int ret = 0;
	struct mdw_ipi_msg msg;

	switch (type) {
	case MDW_INFO_KLOG:
		*val = mrdev->mdev->mdw_klog;
		break;
	case MDW_INFO_NORMAL_TASK_DLA:
		*val = mrdev->stat->task_num[APUSYS_DEVICE_MDLA][MDW_QUEUE_NORMAL];
		break;
	case MDW_INFO_NORMAL_TASK_DSP:
		*val = mrdev->stat->task_num[APUSYS_DEVICE_VPU][MDW_QUEUE_NORMAL] +
		mrdev->stat->task_num[APUSYS_DEVICE_MVPU][MDW_QUEUE_NORMAL];
		break;
	case MDW_INFO_NORMAL_TASK_DMA:
		*val = mrdev->stat->task_num[APUSYS_DEVICE_EDMA][MDW_QUEUE_NORMAL];
		break;
	case MDW_INFO_RESERV_TIME_REMAIN:
		memset(&msg, 0, sizeof(msg));
		msg.id = MDW_IPI_PARAM;
		msg.p.type = type;
		msg.p.dir = MDW_INFO_GET;

		mdw_drv_debug("get param(%u/%u/%u)\n", msg.p.type, msg.p.dir, msg.p.value);
		ret = mdw_rv_dev_send_sync(mrdev, &msg);
		if (!ret)
			*val = msg.p.value;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int mdw_rv_dev_handshake(struct mdw_rv_dev *mrdev)
{
	struct mdw_ipi_msg msg;
	int ret = 0;
	uint32_t type = 0;

	/* query basic infos */
	memset(&msg, 0, sizeof(msg));
	msg.id = MDW_IPI_HANDSHAKE;
	msg.h.h_id = MDW_IPI_HANDSHAKE_BASIC_INFO;
	msg.h.basic.stat_iova = (uint64_t) mrdev->stat_iova;
	msg.h.basic.stat_size = sizeof(struct mdw_stat);
	ret = mdw_rv_dev_send_sync(mrdev, &msg);
	if (ret)
		goto out;

	if (msg.id != MDW_IPI_HANDSHAKE ||
		msg.h.h_id != MDW_IPI_HANDSHAKE_BASIC_INFO) {
		ret = -EINVAL;
		goto out;
	}

	memcpy(mrdev->dev_mask, &msg.h.basic.dev_bmp, sizeof(mrdev->dev_mask));
	memcpy(mrdev->mem_mask, &msg.h.basic.mem_bmp, sizeof(mrdev->mem_mask));
	mrdev->rv_version = msg.h.basic.version;
	dev_info(mrdev->mdev->dev, "[warn] apusys: rv infos(%u)(0x%lx/0x%llx)(0x%lx/0x%llx)\n",
			mrdev->rv_version, mrdev->dev_mask[0],
			msg.h.basic.dev_bmp, mrdev->mem_mask[0], msg.h.basic.mem_bmp);

	/* query device num */
	type = 0;
	do {
		type = find_next_bit(mrdev->dev_mask, APUSYS_DEVICE_MAX, type);
		if (type >= APUSYS_DEVICE_MAX)
			break;

		memset(&msg, 0, sizeof(msg));
		msg.id = MDW_IPI_HANDSHAKE;
		msg.h.h_id = MDW_IPI_HANDSHAKE_DEV_NUM;
		msg.h.dev.type = type;
		ret = mdw_rv_dev_send_sync(mrdev, &msg);
		if (ret)
			break;

		if (msg.id != MDW_IPI_HANDSHAKE ||
			msg.h.h_id != MDW_IPI_HANDSHAKE_DEV_NUM) {
			ret = -EINVAL;
			break;
		}

		mrdev->dev_num[type] = msg.h.dev.num;
		memcpy(&mrdev->meta_data[msg.h.dev.type][0],
			msg.h.dev.meta, sizeof(msg.h.dev.meta));
		mdw_drv_debug("dev(%d) num(%u)\n", type, msg.h.dev.num);
		type++;
	} while (type < APUSYS_DEVICE_MAX);

	/* query mem info */
	type = 0;
	do {
		type = find_next_bit(mrdev->mem_mask, MDW_MEM_TYPE_MAX, type);
		if (type >= MDW_MEM_TYPE_MAX)
			break;

		memset(&msg, 0, sizeof(msg));
		msg.id = MDW_IPI_HANDSHAKE;
		msg.h.h_id = MDW_IPI_HANDSHAKE_MEM_INFO;
		msg.h.mem.type = type;
		ret = mdw_rv_dev_send_sync(mrdev, &msg);
		if (ret)
			break;

		if (msg.id != MDW_IPI_HANDSHAKE ||
			msg.h.h_id != MDW_IPI_HANDSHAKE_MEM_INFO) {
			ret = -EINVAL;
			break;
		}

		/* only vlm need addr */
		if (type == MDW_MEM_TYPE_VLM)
			mrdev->minfos[type].device_va = msg.h.mem.start;
		mrdev->minfos[type].dva_size = msg.h.mem.size;

		mdw_drv_debug("mem(%d)(0x%llx/0x%x)\n",
			type, msg.h.mem.start, msg.h.mem.size);
		type++;
	} while (type < MDW_MEM_TYPE_MAX);

out:
	return ret;
}

static void mdw_rv_dev_init_func(struct work_struct *wk)
{
	struct mdw_rv_dev *mrdev = container_of(wk, struct mdw_rv_dev, init_wk);
	struct mdw_device *mdev = mrdev->mdev;
	int ret = 0;

	ret = mdw_rv_dev_handshake(mrdev);
	if (ret) {
		pr_err("[error] %s: handshake fail(%d)\n", __func__, ret);
		return;
	}

	if (mrdev->rv_version < 2)
		mrdev->cmd_funcs = &mdw_rv_cmd_func_v2;
	else
		mrdev->cmd_funcs = &mdw_rv_cmd_func_v3;

	memcpy(mdev->dev_mask, mrdev->dev_mask, sizeof(mrdev->dev_mask));
	mdev->inited = true;
	pr_info("%s: late init done\n", __func__);
}

int mdw_rv_dev_init(struct mdw_device *mdev)
{
	struct rpmsg_channel_info chinfo = {};
	struct mdw_rv_dev *mrdev = NULL;
	struct device *dev = mdw_mem_rsc_get_dev(APUSYS_MEMORY_CODE);
	int ret = 0;

	if (!mdev->rpdev || mdev->driver_type != MDW_DRIVER_TYPE_RPMSG) {
		dev_err(mdev->dev, "[error] invalid driver type(%d)\n", mdev->driver_type);
		ret = -EINVAL;
		goto out;
	}

	mrdev = kzalloc(sizeof(*mrdev), GFP_KERNEL);
	if (!mrdev)
		return -ENOMEM;

	mdev->dev_specific = mrdev;
	mrdev->mdev = mdev;
	mrdev->rpdev = mdev->rpdev;

	strscpy(chinfo.name, mrdev->rpdev->id.name, RPMSG_NAME_SIZE);
	chinfo.src = mrdev->rpdev->src;
	chinfo.dst = RPMSG_ADDR_ANY;
	mrdev->ept = rpmsg_create_ept(mrdev->rpdev,
		mdw_rv_callback, mrdev, chinfo);
	if (!mrdev->ept) {
		dev_err(mdev->dev, "[error] create ept fail\n");
		ret = -ENODEV;
		goto free_mrdev;
	}

	/* Allocate stat buffer */
	dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	mrdev->stat = dma_alloc_coherent(dev, sizeof(struct mdw_stat),
			&mrdev->stat_iova, GFP_KERNEL);
	dev_info(mdev->dev, "%pad\n", &mrdev->stat_iova);

	if (!mrdev->stat) {
		ret = -ENOMEM;
		goto free_ept;
	}

	/* init up dev */
	mutex_init(&mrdev->msg_mtx);
	mutex_init(&mrdev->mtx);
	INIT_LIST_HEAD(&mrdev->s_list);
	INIT_WORK(&mrdev->init_wk, &mdw_rv_dev_init_func);

	schedule_work(&mrdev->init_wk);

	goto out;

free_ept:
	rpmsg_destroy_ept(mrdev->ept);
free_mrdev:
	kfree(mrdev);
	mdev->dev_specific = NULL;
out:
	return ret;
}

void mdw_rv_dev_deinit(struct mdw_device *mdev)
{
	struct mdw_rv_dev *mrdev = (struct mdw_rv_dev *)mdev->dev_specific;
	struct device *dev = mdw_mem_rsc_get_dev(APUSYS_MEMORY_CODE);

	if (mrdev == NULL)
		return;

	dma_free_coherent(dev, sizeof(struct mdw_stat), mrdev->stat, mrdev->stat_iova);
	rpmsg_destroy_ept(mrdev->ept);
	kfree(mrdev);
	mdev->dev_specific = NULL;
}
