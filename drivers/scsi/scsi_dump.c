/*
 *  linux/drivers/scsi/scsi_dump.c
 *
 *  Copyright (C) 2004  FUJITSU LIMITED
 *  Written by Nobuhiro Tachino (ntachino@jp.fujitsu.com)
 *
 * Some codes are derived from drivers/scsi/sd.c
 *
 * Oct 05, 2004 Enhanced cmd_result() by Jim Keniston(jkenisto@us.ibm.com)
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <linux/blkdev.h>
#include <linux/blkpg.h>

#include <linux/genhd.h>
#include <linux/utsname.h>
#include <linux/delay.h>
#include <linux/diskdump.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>

#define MAX_RETRIES 5
#define SD_TIMEOUT (60 * HZ)

#define Dbg(x, ...)	pr_debug("scsi_dump: " x "\n", ## __VA_ARGS__)
#define Err(x, ...)	pr_err  ("scsi_dump: " x "\n", ## __VA_ARGS__)
#define Warn(x, ...)	pr_warn ("scsi_dump: " x "\n", ## __VA_ARGS__)
#define Info(x, ...)	pr_info ("scsi_dump: " x "\n", ## __VA_ARGS__)

/* blocks to 512byte sectors */
#define BLOCK_SECTOR(s)	((s) << (DUMP_BLOCK_SHIFT - 9))

static struct scsi_cmnd scsi_dump_cmnd;
static struct request scsi_dump_req;

static void rw_intr(struct scsi_cmnd * scmd)
{
	del_timer(&scmd->eh_timeout);
	scmd->done = NULL;
}

static void eh_timeout(unsigned long data)
{
}

/*
 * Common code to make Scsi_Cmnd
 */
static void init_scsi_command(struct scsi_device *sdev, struct scsi_cmnd *scmd,
		 	      void *buf, int len, unsigned char direction,
			      int set_lun)
{
	scmd->request   = &scsi_dump_req;
	scmd->device	= sdev;
	scmd->buffer	= scmd->request_buffer = buf;
	scmd->bufflen	= scmd->request_bufflen = len;


	scmd->sc_data_direction = direction;

	memcpy(scmd->data_cmnd, scmd->cmnd, sizeof(scmd->cmnd));
	scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
	scmd->old_cmd_len = scmd->cmd_len;


	if (set_lun)
		scmd->cmnd[1] |= (sdev->scsi_level <= SCSI_2) ?
				  ((sdev->lun << 5) & 0xe0) : 0;

	scmd->transfersize = sdev->sector_size;
	if (direction == DMA_TO_DEVICE)
		scmd->underflow = len;

	scmd->allowed = MAX_RETRIES;
	scmd->timeout_per_command = SD_TIMEOUT;

	/*
	 * This is the completion routine we use.  This is matched in terms
	 * of capability to this function.
	 */
	scmd->done = rw_intr;

	/*
	 * Some low driver put eh_timeout into the timer list.
	 */
	init_timer(&scmd->eh_timeout);
	scmd->eh_timeout.data		= (unsigned long)scmd;
	scmd->eh_timeout.function	= eh_timeout;
}

/* SYNCHRONIZE CACHE */
static void init_sync_command(struct scsi_device *sdev, struct scsi_cmnd * scmd)
{
	memset(scmd, 0, sizeof(*scmd));
	scmd->cmnd[0] = SYNCHRONIZE_CACHE;

	init_scsi_command(sdev, scmd, NULL, 0, DMA_NONE, 0);
}

/* READ/WRITE */
static int init_rw_command(struct disk_dump_partition *dump_part,
			   struct scsi_device *sdev, struct scsi_cmnd * scmd,
			   int rw, int block, void *buf, unsigned int len)
{
	int this_count = len >> 9;

	memset(scmd, 0, sizeof(*scmd));

	if (block + this_count > dump_part->nr_sects) {
		Err("scsi_dump: block number %d is larger than %lu",
				block + this_count, dump_part->nr_sects);
		return -EFBIG;
	}

	block += dump_part->start_sect;

	/*
	 * If we have a 1K hardware sectorsize, prevent access to single
	 * 512 byte sectors.  In theory we could handle this - in fact
	 * the scsi cdrom driver must be able to handle this because
	 * we typically use 1K blocksizes, and cdroms typically have
	 * 2K hardware sectorsizes.  Of course, things are simpler
	 * with the cdrom, since it is read-only.  For performance
	 * reasons, the filesystems should be able to handle this
	 * and not force the scsi disk driver to use bounce buffers
	 * for this.
	 */
	if (sdev->sector_size == 1024) {
		block = block >> 1;
		this_count = this_count >> 1;
	}
	if (sdev->sector_size == 2048) {
		block = block >> 2;
		this_count = this_count >> 2;
	}
	if (sdev->sector_size == 4096) {
		block = block >> 3;
		this_count = this_count >> 3;
	}
	switch (rw) {
	case WRITE:
		if (!sdev->writeable) {
			Err("scsi_dump: writable media");
			return 0;
		}
		scmd->cmnd[0] = WRITE_10;
		break;
	case READ:
		scmd->cmnd[0] = READ_10;
		break;
	default:
		Err("scsi_dump: Unknown command %d", rw);
		return -EINVAL;
	}

	if (this_count > 0xffff)
		this_count = 0xffff;

	scmd->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
	scmd->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
	scmd->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
	scmd->cmnd[5] = (unsigned char) block & 0xff;
	scmd->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
	scmd->cmnd[8] = (unsigned char) this_count & 0xff;

	init_scsi_command(sdev, scmd, buf, len,
			(rw == WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE), 1);
	return 0;
}

/*
 * Check the status of scsi command and determine whether it is
 * success, fail, or retriable.
 *
 * Return code
 * 	> 0: should retry
 * 	= 0: success
 * 	< 0: fail
 */
static int cmd_result(struct scsi_cmnd *scmd)
{
	int status;

	status = status_byte(scmd->result);

	switch (scsi_decide_disposition(scmd)) {
	case FAILED:
		break;
	case NEEDS_RETRY:
	case ADD_TO_MLQUEUE:
		return 1 /* retry */;
	case SUCCESS:
		if (host_byte(scmd->result) == DID_RESET) {
			Err("scsi_dump: host_byte(scmd->result) set to : DID_RESET");
			return 1;  /* retry */
		} else if ((status == CHECK_CONDITION) &&
			   (scmd->sense_buffer[2] == UNIT_ATTENTION)) {
			/*
			 * if we are expecting a cc/ua because of a bus reset
			 * that we performed, treat this just as a retry.
			 * otherwise this is information that we should pass up
			 * to the upper-level driver so that we can deal with
			 * it there.
			 */
			Err("scsi_dump: CHECK_CONDITION and UNIT_ATTENTION");
			if (scmd->device->expecting_cc_ua) {
				Err("scsi_dump: expecting_cc_ua is set. setting it to zero");
				scmd->device->expecting_cc_ua = 0;
				return 1; /* retry */
			}
			/*
			 * if the device is in the process of becoming ready, we
			 * should retry.
			 */
			if ((scmd->sense_buffer[12] == 0x04) &&
			    (scmd->sense_buffer[13] == 0x01)) {
				Err("scsi_dump: device is in the process of becoming ready..");
				return 1; /* retry */
			}
			/*
			 * if the device is not started, we need to wake
			 * the error handler to start the motor
			 */
			if (scmd->device->allow_restart &&
			    (scmd->sense_buffer[12] == 0x04) &&
			    (scmd->sense_buffer[13] == 0x02)) {
				Err("scsi_dump: the device is not started..");
				break;
			}
		} else if (host_byte(scmd->result) != DID_OK) {
			Err("csi_dump: ome undefined error");
			Err("host_byte(scmd->result) : %d", host_byte(scmd->result));
			break;
		}

		if (status == GOOD ||
		    status == CONDITION_GOOD ||
		    status == INTERMEDIATE_GOOD ||
		    status == INTERMEDIATE_C_GOOD)
			return 0;
		if (status == CHECK_CONDITION &&
		    scmd->sense_buffer[2] == RECOVERED_ERROR)
			return 0;
		break;
	default:
		Err("scsi_dump: bad disposition: %d", scmd->result);
		return -EIO;
	}

	Err("scsi_dump: command %x failed with 0x%x", scmd->cmnd[0], scmd->result);
	return -EIO;
}

static int send_command(struct scsi_cmnd *scmd)
{
	struct Scsi_Host *host = scmd->device->host;
	struct scsi_device *sdev = scmd->device;
	int ret;

	do {
		if (!scsi_device_online(sdev)) {
			Err("scsi_dump: Scsi disk is not online");
			return -EIO;
		}
		if (sdev->changed) {
			Err("scsi_dump: SCSI disk has been changed. Prohibiting further I/O");
			return -EIO;
		}

		spin_lock(host->host_lock);
		host->hostt->queuecommand(scmd, rw_intr);
		spin_unlock(host->host_lock);

		while (scmd->done != NULL) {
			host->hostt->dump_poll(scmd->device);
			udelay(100);
			diskdump_update();
		}
		scmd->done = rw_intr;
	} while ((ret = cmd_result(scmd)) > 0);

	return ret;
}

static int
scsi_dump_quiesce(struct disk_dump_device *dump_device)
{
	struct scsi_device *sdev = dump_device->device;
	struct Scsi_Host *host = sdev->host;

	if (host->hostt->dump_quiesce)
		return host->hostt->dump_quiesce(sdev);

	return 0;
}

static int scsi_dump_rw_block(struct disk_dump_partition *dump_part, int rw,
			      unsigned long dump_block_nr, void *buf, int len)
{
	struct disk_dump_device *dump_device = dump_part->device;
	struct scsi_device *sdev = dump_device->device;
	int block_nr = BLOCK_SECTOR(dump_block_nr);
	int ret;

	ret = init_rw_command(dump_part, sdev, &scsi_dump_cmnd, rw,
					block_nr, buf, DUMP_BLOCK_SIZE * len);
	if (ret < 0) {
		Err("scsi_dump: scsi_dump: init_rw_command failed");
		return ret;
	}
	return send_command(&scsi_dump_cmnd);
}

static int
scsi_dump_shutdown(struct disk_dump_device *dump_device)
{
	struct scsi_device *sdev = dump_device->device;
	struct Scsi_Host *host = sdev->host;
	int ret;

	if (sdev->scsi_level >= SCSI_2) {
		init_sync_command(sdev, &scsi_dump_cmnd);
		ret = send_command(&scsi_dump_cmnd);
		if (ret < 0)
			Warn("SYNCHRONIZE_CACHE failed.");
	}

	if (host->hostt->dump_shutdown)
		return host->hostt->dump_shutdown(sdev);

	return 0;
}

struct disk_dump_device_ops scsi_dump_device_ops = {
	.rw_block	= scsi_dump_rw_block,
	.quiesce	= scsi_dump_quiesce,
	.shutdown	= scsi_dump_shutdown,
};

static void *scsi_dump_probe(struct device *dev)
{
	struct scsi_device *sdev;

	if ((dev->bus == NULL) || (dev->bus->name == NULL) ||
	    strncmp(dev->bus->name, "scsi", 4))
		return NULL;

	sdev = to_scsi_device(dev);
	if (!sdev->host->hostt->dump_poll)
		return NULL;

	return sdev;
}

static int scsi_dump_add_device(struct disk_dump_device *dump_device)
{
	struct scsi_device *sdev;
	int error;

	sdev = dump_device->device;
	if (!sdev->host->hostt->dump_poll)
		return -ENOTSUPP;

	if ((error = scsi_device_get(sdev)) != 0)
		return error;

	memcpy(&dump_device->ops, &scsi_dump_device_ops,
		sizeof(scsi_dump_device_ops));
	if (sdev->host->max_sectors) {
		dump_device->max_blocks =
			(sdev->sector_size * sdev->host->max_sectors)
			  >> DUMP_BLOCK_SHIFT;
	}
	return 0;
}

static void scsi_dump_remove_device(struct disk_dump_device *dump_device)
{
	struct scsi_device *sdev = dump_device->device;

	scsi_device_put(sdev);
}

static struct disk_dump_type scsi_dump_type = {
	.probe		= scsi_dump_probe,
	.add_device	= scsi_dump_add_device,
	.remove_device	= scsi_dump_remove_device,
	.owner		= THIS_MODULE,
};

static int init_scsi_dump(void)
{
	int ret;

	if ((ret = register_disk_dump_type(&scsi_dump_type)) < 0) {
		Err("scsi_dump: register failed");
		return ret;
	}
	return ret;
}

static void cleanup_scsi_dump(void)
{
	if (unregister_disk_dump_type(&scsi_dump_type) < 0)
		Err("scsi_dump: unregister failed");
}

module_init(init_scsi_dump);
module_exit(cleanup_scsi_dump);

MODULE_LICENSE("GPL");
