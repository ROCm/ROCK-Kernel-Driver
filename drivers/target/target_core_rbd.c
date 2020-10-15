// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * Filename:  target_core_rbd.c
 *
 * This file contains the Storage Engine <-> Ceph RBD transport
 * specific functions.
 *
 * [Was based off of target_core_iblock.c from
 *  Nicholas A. Bellinger <nab@kernel.org>]
 *
 * (c) Copyright 2003-2013 Datera, Inc.
 * (c) Copyright 2015 Red Hat, Inc
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 ******************************************************************************/

#include <linux/ceph/libceph.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/striper.h>
#include <linux/ceph/librbd.h>

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/file.h>
#include <linux/module.h>
#include <scsi/scsi_proto.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#include "target_core_rbd.h"

static inline struct tcm_rbd_dev *TCM_RBD_DEV(struct se_device *dev)
{
	return container_of(dev, struct tcm_rbd_dev, dev);
}


static int tcm_rbd_attach_hba(struct se_hba *hba, u32 host_id)
{
	pr_debug("CORE_HBA[%d] - TCM RBD HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		TCM_RBD_VERSION, TARGET_CORE_VERSION);
	return 0;
}

static void tcm_rbd_detach_hba(struct se_hba *hba)
{
}

static struct se_device *tcm_rbd_alloc_device(struct se_hba *hba, const char *name)
{
	struct tcm_rbd_dev *tcm_rbd_dev = NULL;

	tcm_rbd_dev = kzalloc(sizeof(struct tcm_rbd_dev), GFP_KERNEL);
	if (!tcm_rbd_dev) {
		pr_err("Unable to allocate struct tcm_rbd_dev\n");
		return NULL;
	}

	tcm_rbd_dev->emulate_legacy_capacity = true;

	pr_debug( "TCM RBD: Allocated tcm_rbd_dev for %s\n", name);

	return &tcm_rbd_dev->dev;
}

static int tcm_rbd_configure_device(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct request_queue *q;
	struct block_device *bd = NULL;
	fmode_t mode;

	if (!(tcm_rbd_dev->bd_flags & TCM_RBD_HAS_UDEV_PATH)) {
		pr_err("Missing udev_path= parameters for TCM RBD\n");
		return -EINVAL;
	}

	pr_debug( "TCM RBD: Claiming struct block_device: %s\n",
			tcm_rbd_dev->bd_udev_path);

	mode = FMODE_READ|FMODE_EXCL;
	if (!tcm_rbd_dev->bd_readonly)
		mode |= FMODE_WRITE;
	else
		dev->dev_flags |= DF_READ_ONLY;

	bd = blkdev_get_by_path(tcm_rbd_dev->bd_udev_path, mode, tcm_rbd_dev);
	if (IS_ERR(bd)) {
		return PTR_ERR(bd);
	}
	tcm_rbd_dev->bd = bd;

	q = bdev_get_queue(bd);
	tcm_rbd_dev->rbd_dev = q->queuedata;

	dev->dev_attrib.hw_block_size = bdev_logical_block_size(bd);
	dev->dev_attrib.hw_max_sectors = queue_max_hw_sectors(q);
	dev->dev_attrib.hw_queue_depth = q->nr_requests;

	if (target_configure_unmap_from_queue(&dev->dev_attrib, q))
		pr_debug("RBD: BLOCK Discard support available,"
			 " disabled by default\n");

	/*
	 * Enable write same emulation for RBD and use 0xFFFF as
	 * the smaller WRITE_SAME(10) only has a two-byte block count.
	 */
	dev->dev_attrib.max_write_same_len = 0xFFFF;
	dev->dev_attrib.is_nonrot = 1;

	if (tcm_rbd_dev->rbd_dev->layout.stripe_unit % 4096) {
		/*
		 * SCSI compare-and-write must be handled atomically, but this
		 * won't be the case if the single-block SCSI I/O spans multiple
		 * RADOS objects. Can't use dev_attrib.block_size here, as it
		 * may not be configured yet.
		 */
		pr_err("RBD: stripe unit %u must be a multiple of 4K\n",
			tcm_rbd_dev->rbd_dev->layout.stripe_unit);
		/* blkdev_put() called in destroy_device */
		return -EINVAL;
	}

	/* disable standalone reservation handling */
	dev->dev_attrib.emulate_pr = 0;

	return 0;
}

static void tcm_rbd_dev_call_rcu(struct rcu_head *p)
{
	struct se_device *dev = container_of(p, struct se_device, rcu_head);
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);

	kfree(tcm_rbd_dev);
}

static void tcm_rbd_free_device(struct se_device *dev)
{
	call_rcu(&dev->rcu_head, tcm_rbd_dev_call_rcu);
}

static void tcm_rbd_destroy_device(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);

	if (tcm_rbd_dev->bd != NULL)
		blkdev_put(tcm_rbd_dev->bd, FMODE_WRITE|FMODE_READ|FMODE_EXCL);
}

static sector_t tcm_rbd_get_blocks(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	u64 blocks_long = div_u64(tcm_rbd_dev->rbd_dev->mapping.size,
				  dev->dev_attrib.block_size);

	/* READ CAPACITY should return the LUN's last LBA */
	if (blocks_long && !tcm_rbd_dev->emulate_legacy_capacity)
		blocks_long -= 1;

	return blocks_long;
}

struct tcm_rbd_cmd {
	struct rbd_img_request *img_request;
	/* for sgl->bvec conversion */
	struct bio_vec *bvecs;
};

static sense_reason_t tcm_rbd_execute_sync_cache(struct se_cmd *cmd)
{
	/* user-space librbd supports caching, but kRBD does not yet */
	target_complete_cmd(cmd, SAM_STAT_GOOD);
	return 0;
}

static int tcm_rbd_sgl_to_bvecs(struct scatterlist *sgl, u32 sgl_nents,
				struct bio_vec **_bvecs)
{
	int i;
	struct scatterlist *sg;
	struct bio_vec *bvecs;

	bvecs = kcalloc(sgl_nents, sizeof(struct bio_vec), GFP_KERNEL);
	if (!bvecs) {
		return -ENOMEM;
	}

	for_each_sg(sgl, sg, sgl_nents, i) {
		pr_debug("sg %d: %u@%u\n", i, sg->length, sg->offset);

		bvecs[i].bv_page = sg_page(sg);
		bvecs[i].bv_offset = sg->offset;
		bvecs[i].bv_len = sg->length;
	}
	*_bvecs = bvecs;

	return 0;
}

/*
 * Convert the blocksize advertised to the initiator to the RBD offset.
 * Returns the equivalent of task_lba << ilog2(blocksize) for the fixed set of
 * blocksizes supported by LIO.
 */
static u64 rbd_lba_shift(struct se_device *dev, unsigned long long task_lba)
{
	switch (dev->dev_attrib.block_size) {
	case 4096:
		return task_lba << 12;
	case 2048:
		return task_lba << 11;
	case 1024:
		return task_lba << 10;
	case 512:
		return task_lba << 9;
	default:
		WARN_ON(1);
	}
	return task_lba << 9;
}

static void tcm_rbd_async_callback(struct rbd_img_request *img_request,
				   int result)
{
	struct se_cmd *cmd = img_request->lio_cmd_data;
	struct tcm_rbd_cmd *trc = cmd->priv;
	u8 status;

	if (result)
		status = SAM_STAT_CHECK_CONDITION;
	else
		status = SAM_STAT_GOOD;

	cmd->priv = NULL;
	target_complete_cmd(cmd, status);
	if (trc) {
		rbd_img_request_put(trc->img_request);
		kfree(trc->bvecs);
		kfree(trc);
	}
}

struct tcm_rbd_sync_notify {
	int result;
	struct completion c;
};

static void tcm_rbd_sync_callback(struct rbd_img_request *img_request,
				  int result)
{
	struct tcm_rbd_sync_notify *notify = img_request->lio_cmd_data;

	notify->result = result;
	complete(&notify->c);
}

/* follows rbd_queue_workfn() */
static sense_reason_t
tcm_rbd_execute_cmd(struct se_cmd *cmd, struct rbd_device *rbd_dev,
		    struct scatterlist *sgl, u32 sgl_nents,
		    enum obj_operation_type op_type,
		    u64 offset, u64 length, bool sync)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(cmd->se_dev);
	struct tcm_rbd_cmd *trc;
	struct rbd_img_request *img_request;
	struct ceph_snap_context *snapc = NULL;
	u64 mapping_size;
	struct tcm_rbd_sync_notify sync_notify = {
		0,
		COMPLETION_INITIALIZER_ONSTACK(sync_notify.c),
	};
	sense_reason_t sense = TCM_NO_SENSE;
	int result;

	/* Ignore/skip any zero-length requests */

	if (!length) {
		dout("%s: zero-length request\n", __func__);
		goto err;
	}

	if (op_type != OBJ_OP_READ && rbd_dev->spec->snap_id != CEPH_NOSNAP) {
		pr_warn("write or %d on read-only snapshot", op_type);
		sense = TCM_WRITE_PROTECTED;
		goto err;
	}

	/*
	 * Quit early if the mapped snapshot no longer exists.  It's
	 * still possible the snapshot will have disappeared by the
	 * time our request arrives at the osd, but there's no sense in
	 * sending it if we already know.
	 */
	if (!test_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags)) {
		pr_warn("request for non-existent snapshot");
		BUG_ON(rbd_dev->spec->snap_id == CEPH_NOSNAP);
		sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err;
	}

	if (offset && length > U64_MAX - offset + 1) {
		pr_warn("bad request range (%llu~%llu)", offset, length);
		sense = TCM_INVALID_CDB_FIELD;
		goto err;	/* Shouldn't happen */
	}

	down_read(&rbd_dev->header_rwsem);
	mapping_size = rbd_dev->mapping.size;
	if (op_type != OBJ_OP_READ) {
		snapc = rbd_dev->header.snapc;
		ceph_get_snap_context(snapc);
	}
	up_read(&rbd_dev->header_rwsem);

	if (offset + length > mapping_size) {
		pr_warn("beyond EOD (%llu~%llu > %llu)", offset,
			length, mapping_size);
		if (!tcm_rbd_dev->emulate_legacy_capacity) {
			sense = TCM_ADDRESS_OUT_OF_RANGE;
			goto err_snapc;
		}
	}

	trc = kzalloc(sizeof(struct tcm_rbd_cmd), GFP_KERNEL);
	if (!trc) {
		sense = TCM_OUT_OF_RESOURCES;
		goto err_snapc;
	}

	img_request = rbd_img_request_create(rbd_dev, op_type, snapc,
			sync ? tcm_rbd_sync_callback : tcm_rbd_async_callback);
	if (!img_request) {
		sense = TCM_OUT_OF_RESOURCES;
		goto err_trc;
	}
	snapc = NULL; /* img_request consumes a ref */
	trc->img_request = img_request;

	pr_debug("rbd_dev %p img_req %p %d %llu~%llu\n", rbd_dev,
	     img_request, op_type, offset, length);

	if (op_type == OBJ_OP_DISCARD || op_type == OBJ_OP_ZEROOUT)
		result = rbd_img_fill_nodata(img_request, offset, length);
	else {
		struct ceph_file_extent img_extent = {
			.fe_off = offset,
			.fe_len = length,
		};
		result = tcm_rbd_sgl_to_bvecs(sgl, sgl_nents, &trc->bvecs);
		if (!result) {
			result = rbd_img_fill_from_bvecs(img_request,
							 &img_extent, 1,
							 trc->bvecs);
		}
	}
	if (result == -ENOMEM) {
		sense = TCM_OUT_OF_RESOURCES;
		goto err_img_request;
	} else if (result) {
		sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_img_request;
	}

	if (sync) {
		img_request->lio_cmd_data = &sync_notify;
	} else {
		img_request->lio_cmd_data = cmd;
		cmd->priv = trc;
	}

	rbd_img_handle_request(img_request, 0);

	if (sync) {
		wait_for_completion(&sync_notify.c);
		if (sync_notify.result)
			sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		else
			sense = TCM_NO_SENSE;
		goto err_img_request;
	}

	return TCM_NO_SENSE;

err_img_request:
	rbd_img_request_put(img_request);
err_trc:
	kfree(trc->bvecs);
	kfree(trc);
err_snapc:
	if (sense)
		pr_warn("RBD op type %d %llx at %llx sense %d",
			op_type, length, offset, sense);
	ceph_put_snap_context(snapc);
err:
	return sense;
}

static sense_reason_t tcm_rbd_execute_unmap(struct se_cmd *cmd,
					    sector_t lba, sector_t nolb)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(cmd->se_dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	enum obj_operation_type type = OBJ_OP_DISCARD;

	if (nolb == 0) {
		pr_debug("ignoring zero length unmap at lba: %llu\n",
			 (unsigned long long)lba);
		return TCM_NO_SENSE;
	}

	/* RBD discard is best effort. zeroout provides zeroing guarantees */
	if (cmd->se_dev->dev_attrib.unmap_zeroes_data)
		type = OBJ_OP_ZEROOUT;

	return tcm_rbd_execute_cmd(cmd, rbd_dev, NULL, 0, type,
				   rbd_lba_shift(cmd->se_dev, lba),
				   rbd_lba_shift(cmd->se_dev, nolb),
				   true);
}

static bool
tcm_rbd_write_same_can_zero_out(struct se_cmd *cmd)
{
	struct scatterlist *sg = &cmd->t_data_sg[0];
	unsigned char *buf, *not_zero;

	buf = kmap_atomic(sg_page(sg));
	/*
	 * Fall back to slow-path if incoming WRITE_SAME payload does not
	 * contain zeros.
	 */
	not_zero = memchr_inv(buf + sg->offset, 0x00, cmd->data_length);
	kunmap_atomic(buf);

	return (not_zero == NULL);
}

static sense_reason_t
tcm_rbd_execute_write_same(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	struct scatterlist *sg;
	struct scatterlist *ws_sgl = NULL;
	u32 ws_sgl_nents = 0;
	u64 ws_off_bytes = rbd_lba_shift(dev, cmd->t_task_lba);
	u64 ws_len_bytes = rbd_lba_shift(dev, sbc_get_write_same_sectors(cmd));
	sense_reason_t sense;
	u32 i;

	if (!ws_len_bytes) {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
		return 0;
	}
	if (cmd->prot_op) {
		pr_err("WRITE_SAME: Protection information not supported\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	sg = &cmd->t_data_sg[0];

	if (cmd->t_data_nents > 1 ||
	    sg->length != cmd->se_dev->dev_attrib.block_size ||
	    sg->length != cmd->data_length) {
		pr_err("WRITE_SAME: Illegal SGL t_data_nents: %u length: %u"
			" block_size: %u\n", cmd->t_data_nents, sg->length,
			cmd->se_dev->dev_attrib.block_size);
		return TCM_INVALID_CDB_FIELD;
	}

	if (tcm_rbd_write_same_can_zero_out(cmd)) {
		pr_debug("WRITE_SAME: mapped to zero-out for fast path\n");
		return tcm_rbd_execute_cmd(cmd, rbd_dev, NULL, 0,
					   OBJ_OP_ZEROOUT,
					   ws_off_bytes,
					   ws_len_bytes,
					   false);
	}

	pr_debug("WRITE_SAME: slow path for non-zero buffer\n");
	/*  I/O could be up to max_write_same_len */
	ws_sgl_nents = div_u64(ws_len_bytes, sg->length);
	ws_sgl = kzalloc(ws_sgl_nents * sizeof(*sg), GFP_KERNEL);
	if (!ws_sgl) {
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	sg_init_table(ws_sgl, ws_sgl_nents);
	for (i = 0; i < ws_sgl_nents; i++)
		sg_set_page(&ws_sgl[i], sg_page(sg), sg->length, sg->offset);

	sense = tcm_rbd_execute_cmd(cmd, rbd_dev, ws_sgl, ws_sgl_nents,
				    OBJ_OP_WRITE,
				    ws_off_bytes,
				    ws_len_bytes,
				    false);
	kfree(ws_sgl);
	return sense;
}

static void tcm_rbd_cmp_and_write_callback(struct rbd_img_request *img_request,
					   int result)
{
	struct se_cmd *cmd = img_request->lio_cmd_data;
	struct tcm_rbd_cmd *trc = cmd->priv;

	cmd->priv = NULL;
	if (result <= -MAX_ERRNO) {
		/*
		 * OSDs return -MAX_ERRNO - offset_of_mismatch
		 * This offset calculation would be incorrect if we supported
		 * compare-and-write with multi-object striping.
		 */
		cmd->bad_sector = (sector_t)(-1 * (result + MAX_ERRNO));
		/*
		 * kABI: we can't easily propagate TCM_MISCOMPARE_VERIFY here,
		 * so signal it via the scsi_asc field.
		 */
		cmd->scsi_asc = 0x1d;	/* MISCOMPARE DURING VERIFY OPERATION */
		pr_debug("COMPARE_AND_WRITE: miscompare at offset %llu\n",
			 cmd->bad_sector);
		target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
	} else if (result) {
		target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
	} else {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
	}

	if (trc) {
		rbd_img_request_put(trc->img_request);
		kfree(trc->bvecs);
		kfree(trc);
	}
}

static sense_reason_t
tcm_rbd_execute_cmp_and_write(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	struct tcm_rbd_cmd *trc;
	struct rbd_img_request *img_request;
	struct ceph_snap_context *snapc = NULL;
	u64 mapping_size;
	sense_reason_t sense = TCM_NO_SENSE;
	u64 offset = rbd_lba_shift(dev, cmd->t_task_lba);
	u64 length = rbd_lba_shift(dev, cmd->t_task_nolb);
	int result;

	if (!length) {
		dout("zero-length compare-and-write request\n");
		target_complete_cmd(cmd, SAM_STAT_GOOD);
		return TCM_NO_SENSE;
	}

	if (rbd_dev->spec->snap_id != CEPH_NOSNAP) {
		pr_warn("compare-and-write on read-only snapshot");
		sense = TCM_WRITE_PROTECTED;
		goto err;
	}

	if (!test_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags)) {
		pr_warn("request for non-existent snapshot");
		BUG_ON(rbd_dev->spec->snap_id == CEPH_NOSNAP);
		sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err;
	}

	if (offset && length > U64_MAX - offset + 1) {
		pr_warn("bad request range (%llu~%llu)", offset, length);
		sense = TCM_INVALID_CDB_FIELD;
		goto err;	/* Shouldn't happen */
	}

	down_read(&rbd_dev->header_rwsem);
	mapping_size = rbd_dev->mapping.size;
	snapc = rbd_dev->header.snapc;
	ceph_get_snap_context(snapc);
	up_read(&rbd_dev->header_rwsem);

	/*
	 * No need to take dev->caw_sem here, as the IO is mapped to a compound
	 * compare+write OSD request, which is handled atomically by the OSD.
	 */

	if (offset + length > mapping_size) {
		pr_warn("beyond EOD (%llu~%llu > %llu)", offset,
			length, mapping_size);
		if (!tcm_rbd_dev->emulate_legacy_capacity) {
			sense = TCM_ADDRESS_OUT_OF_RANGE;
			goto err_snapc;
		}
	}

	/* need twice as much data for each compare & write operation */
	if (cmd->data_length < length * 2) {
		sense = TCM_INVALID_CDB_FIELD;
		goto err_snapc;
	}

	trc = kzalloc(sizeof(struct tcm_rbd_cmd), GFP_KERNEL);
	if (!trc) {
		sense = TCM_OUT_OF_RESOURCES;
		goto err_snapc;
	}

	img_request = rbd_img_request_create(rbd_dev, OBJ_OP_CMP_AND_WRITE,
					     snapc,
					     tcm_rbd_cmp_and_write_callback);
	if (!img_request) {
		sense = TCM_OUT_OF_RESOURCES;
		goto err_trc;
	}
	snapc = NULL; /* img_request consumes a ref */
	trc->img_request = img_request;

	pr_debug("rbd_dev %p compare-and-write img_req %p %llu~%llu\n",
		 rbd_dev, img_request, offset, length);

	/*
	 * data in cmd->t_data_sg is arrange as:
	 * [len * data for compare | len * data for write]
	 */
	result = tcm_rbd_sgl_to_bvecs(cmd->t_data_sg, cmd->t_data_nents,
				      &trc->bvecs);
	if (!result) {
		struct ceph_file_extent img_extent = {
			.fe_off = offset,
			.fe_len = length,
		};
		result = rbd_img_fill_cmp_and_write_from_bvecs(img_request,
						      &img_extent,
						      trc->bvecs);
	}

	if (result == -ENOMEM) {
		sense = TCM_OUT_OF_RESOURCES;
		goto err_img_request;
	} else if (result) {
		sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_img_request;
	}

	img_request->lio_cmd_data = cmd;
	cmd->priv = trc;

	rbd_img_handle_request(img_request, 0);

	return TCM_NO_SENSE;

err_img_request:
	rbd_img_request_put(img_request);
err_trc:
	kfree(trc->bvecs);
	kfree(trc);
err_snapc:
	if (sense)
		pr_warn("RBD compare-and-write %llx at %llx sense %d",
			length, offset, sense);
	ceph_put_snap_context(snapc);
err:
	return sense;
}

enum {
	Opt_udev_path, Opt_readonly, Opt_force, Opt_err
};

static match_table_t tokens = {
	{Opt_udev_path, "udev_path=%s"},
	{Opt_readonly, "readonly=%d"},
	{Opt_force, "force=%d"},
	{Opt_err, NULL}
};

static ssize_t
tcm_rbd_set_configfs_dev_params(struct se_device *dev,
				const char *page, ssize_t count)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char *orig, *ptr, *arg_p, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, token;
	unsigned long tmp_readonly;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_udev_path:
			if (tcm_rbd_dev->bd) {
				pr_err("Unable to set udev_path= while"
					" tcm_rbd_dev->bd exists\n");
				ret = -EEXIST;
				goto out;
			}
			if (match_strlcpy(tcm_rbd_dev->bd_udev_path, &args[0],
				SE_UDEV_PATH_LEN) == 0) {
				ret = -EINVAL;
				break;
			}
			pr_debug("TCM RBD: Referencing UDEV path: %s\n",
				 tcm_rbd_dev->bd_udev_path);
			tcm_rbd_dev->bd_flags |= TCM_RBD_HAS_UDEV_PATH;
			break;
		case Opt_readonly:
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			ret = kstrtoul(arg_p, 0, &tmp_readonly);
			kfree(arg_p);
			if (ret < 0) {
				pr_err("kstrtoul() failed for"
						" readonly=\n");
				goto out;
			}
			tcm_rbd_dev->bd_readonly = tmp_readonly;
			pr_debug("TCM RBD: readonly: %d\n",
				 tcm_rbd_dev->bd_readonly);
			break;
		case Opt_force:
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t tcm_rbd_show_configfs_dev_params(struct se_device *dev, char *b)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;
	char buf[BDEVNAME_SIZE];
	ssize_t bl = 0;

	if (bd)
		bl += sprintf(b + bl, "rbd device: %s", bdevname(bd, buf));
	if (tcm_rbd_dev->bd_flags & TCM_RBD_HAS_UDEV_PATH)
		bl += sprintf(b + bl, "  UDEV PATH: %s",
			      tcm_rbd_dev->bd_udev_path);
	bl += sprintf(b + bl, "  readonly: %d\n", tcm_rbd_dev->bd_readonly);

	bl += sprintf(b + bl, "        ");
	if (bd) {
		bl += sprintf(b + bl, "Major: %d Minor: %d  %s\n",
			      MAJOR(bd->bd_dev), MINOR(bd->bd_dev),
			      (!bd->bd_contains) ?
			      "" : (bd->bd_holder == tcm_rbd_dev) ?
			      "CLAIMED: RBD" : "CLAIMED: OS");
	} else {
		bl += sprintf(b + bl, "Major: 0 Minor: 0\n");
	}

	return bl;
}

static sense_reason_t
tcm_rbd_execute_rw(struct se_cmd *cmd, struct scatterlist *sgl, u32 sgl_nents,
		   enum dma_data_direction data_direction)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	enum obj_operation_type op_type;

	if (!sgl_nents) {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
		return 0;
	}

	if (data_direction == DMA_FROM_DEVICE) {
		op_type = OBJ_OP_READ;
	} else {
		op_type = OBJ_OP_WRITE;
	}

	return tcm_rbd_execute_cmd(cmd, rbd_dev, sgl, sgl_nents, op_type,
				   rbd_lba_shift(dev, cmd->t_task_lba),
				   cmd->data_length, false);
}

static sector_t tcm_rbd_get_alignment_offset_lbas(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;
	int ret;

	ret = bdev_alignment_offset(bd);
	if (ret == -1)
		return 0;

	/* convert offset-bytes to offset-lbas */
	return ret / bdev_logical_block_size(bd);
}

static unsigned int tcm_rbd_get_lbppbe(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;
	int logs_per_phys = bdev_physical_block_size(bd) / bdev_logical_block_size(bd);

	return ilog2(logs_per_phys);
}

static unsigned int tcm_rbd_get_io_min(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;

	return bdev_io_min(bd);
}

static unsigned int tcm_rbd_get_io_opt(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;

	return bdev_io_opt(bd);
}

static struct sbc_ops tcm_rbd_sbc_ops = {
	.execute_rw		= tcm_rbd_execute_rw,
	.execute_sync_cache	= tcm_rbd_execute_sync_cache,
	.execute_write_same	= tcm_rbd_execute_write_same,
	.execute_unmap		= tcm_rbd_execute_unmap,
};

static sense_reason_t
tcm_rbd_parse_cdb(struct se_cmd *cmd)
{
	sense_reason_t sense = sbc_parse_cdb(cmd, &tcm_rbd_sbc_ops);
	if (sense)
		return sense;

	/* we provide our own atomic COMPARE_AND_WRITE handler */
	if (cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE) {
		cmd->execute_cmd = tcm_rbd_execute_cmp_and_write;
		cmd->transport_complete_callback = NULL;
	}

	return TCM_NO_SENSE;
}

static bool tcm_rbd_get_write_cache(struct se_device *dev)
{
	return false;
}

static struct target_backend_ops tcm_rbd_ops = {
	.name			= "rbd",
	.inquiry_prod		= "RBD",
	.inquiry_rev		= TCM_RBD_VERSION,
	.owner			= THIS_MODULE,
	.attach_hba		= tcm_rbd_attach_hba,
	.detach_hba		= tcm_rbd_detach_hba,
	.alloc_device		= tcm_rbd_alloc_device,
	.configure_device	= tcm_rbd_configure_device,
	.destroy_device		= tcm_rbd_destroy_device,
	.free_device		= tcm_rbd_free_device,
	.parse_cdb		= tcm_rbd_parse_cdb,
	.set_configfs_dev_params = tcm_rbd_set_configfs_dev_params,
	.show_configfs_dev_params = tcm_rbd_show_configfs_dev_params,
	.get_device_type	= sbc_get_device_type,
	.get_blocks		= tcm_rbd_get_blocks,
	.get_alignment_offset_lbas = tcm_rbd_get_alignment_offset_lbas,
	.get_lbppbe		= tcm_rbd_get_lbppbe,
	.get_io_min		= tcm_rbd_get_io_min,
	.get_io_opt		= tcm_rbd_get_io_opt,
	.get_write_cache	= tcm_rbd_get_write_cache,
};

static ssize_t tcm_rbd_emulate_legacy_capacity_show(struct config_item *item,
						     char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%d\n",
			tcm_rbd_dev->emulate_legacy_capacity);
}

static ssize_t tcm_rbd_emulate_legacy_capacity_store(struct config_item *item,
						      const char *page,
						      size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(da->da_dev);
	bool flag = 0;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	tcm_rbd_dev->emulate_legacy_capacity = flag;
	pr_debug("dev[%p]: tcm_rbd_emulate_legacy_capacity: %s\n",
		da->da_dev, flag ? "Enabled" : "Disabled");

	return count;
}
CONFIGFS_ATTR(tcm_rbd_, emulate_legacy_capacity);

static struct configfs_attribute *tcm_rbd_attrib_attrs[] = {
	&tcm_rbd_attr_emulate_legacy_capacity,
	NULL,
};

static int __init tcm_rbd_module_init(void)
{
	struct configfs_attribute **tcm_rbd_attrs;
	int i, k, ret, len = 0;

	for (i = 0; sbc_attrib_attrs[i] != NULL; i++) {
		len += sizeof(struct configfs_attribute *);
	}
	for (i = 0; tcm_rbd_attrib_attrs[i] != NULL; i++) {
		len += sizeof(struct configfs_attribute *);
	}
	len += sizeof(struct configfs_attribute *);

	tcm_rbd_attrs = kzalloc(len, GFP_KERNEL);
	if (!tcm_rbd_attrs) {
		return -ENOMEM;
	}

	for (i = 0; sbc_attrib_attrs[i] != NULL; i++) {
		tcm_rbd_attrs[i] = sbc_attrib_attrs[i];
	}
	for (k = 0; tcm_rbd_attrib_attrs[k] != NULL; k++) {
		tcm_rbd_attrs[i] = tcm_rbd_attrib_attrs[k];
		i++;
	}
	tcm_rbd_ops.tb_dev_attrib_attrs = tcm_rbd_attrs;
	ret = transport_backend_register(&tcm_rbd_ops);
	if (ret) {
		kfree(tcm_rbd_ops.tb_dev_attrib_attrs);
		tcm_rbd_ops.tb_dev_attrib_attrs = NULL;
	}
	return ret;
}

static void __exit tcm_rbd_module_exit(void)
{
	target_backend_unregister(&tcm_rbd_ops);
	kfree(tcm_rbd_ops.tb_dev_attrib_attrs);
}

MODULE_DESCRIPTION("TCM Ceph RBD subsystem plugin");
MODULE_AUTHOR("Mike Christie");
MODULE_LICENSE("GPL");

module_init(tcm_rbd_module_init);
module_exit(tcm_rbd_module_exit);
