/*
 * PS3 ROM Storage Driver
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#undef DEBUG

#include <linux/cdrom.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include <asm/lv1call.h>
#include <asm/ps3stor.h>


#define DEVICE_NAME			"ps3rom"

#define BOUNCE_SIZE			(64*1024)

#define PS3ROM_MAX_SECTORS		(BOUNCE_SIZE / CD_FRAMESIZE)

#define LV1_STORAGE_SEND_ATAPI_COMMAND	(1)


struct ps3rom_private {
	spinlock_t lock;
	struct task_struct *thread;
	struct Scsi_Host *host;
	struct scsi_cmnd *cmd;
	void (*scsi_done)(struct scsi_cmnd *);
};
#define ps3rom_priv(dev)	((dev)->sbd.core.driver_data)

struct lv1_atapi_cmnd_block {
	u8	pkt[32];	/* packet command block           */
	u32	pktlen;		/* should be 12 for ATAPI 8020    */
	u32	blocks;
	u32	block_size;
	u32	proto;		/* transfer mode                  */
	u32	in_out;		/* transfer direction             */
	u64	buffer;		/* parameter except command block */
	u32	arglen;		/* length above                   */
};

/*
 * to position parameter
 */
enum {
	NOT_AVAIL          = -1,
	USE_SRB_10         = -2,
	USE_SRB_6          = -3,
	USE_CDDA_FRAME_RAW = -4
};

enum lv1_atapi_proto {
	NA_PROTO = -1,
	NON_DATA_PROTO     = 0,
	PIO_DATA_IN_PROTO  = 1,
	PIO_DATA_OUT_PROTO = 2,
	DMA_PROTO = 3
};

enum lv1_atapi_in_out {
	DIR_NA = -1,
	DIR_WRITE = 0, /* memory -> device */
	DIR_READ = 1 /* device -> memory */
};


#ifdef DEBUG
static const char *scsi_command(unsigned char cmd)
{
	switch (cmd) {
	case TEST_UNIT_READY:		return "TEST_UNIT_READY/GPCMD_TEST_UNIT_READY";
	case REZERO_UNIT:		return "REZERO_UNIT";
	case REQUEST_SENSE:		return "REQUEST_SENSE/GPCMD_REQUEST_SENSE";
	case FORMAT_UNIT:		return "FORMAT_UNIT/GPCMD_FORMAT_UNIT";
	case READ_BLOCK_LIMITS:		return "READ_BLOCK_LIMITS";
	case REASSIGN_BLOCKS:		return "REASSIGN_BLOCKS/INITIALIZE_ELEMENT_STATUS";
	case READ_6:			return "READ_6";
	case WRITE_6:			return "WRITE_6/MI_REPORT_TARGET_PGS";
	case SEEK_6:			return "SEEK_6";
	case READ_REVERSE:		return "READ_REVERSE";
	case WRITE_FILEMARKS:		return "WRITE_FILEMARKS/SAI_READ_CAPACITY_16";
	case SPACE:			return "SPACE";
	case INQUIRY:			return "INQUIRY/GPCMD_INQUIRY";
	case RECOVER_BUFFERED_DATA:	return "RECOVER_BUFFERED_DATA";
	case MODE_SELECT:		return "MODE_SELECT";
	case RESERVE:			return "RESERVE";
	case RELEASE:			return "RELEASE";
	case COPY:			return "COPY";
	case ERASE:			return "ERASE";
	case MODE_SENSE:		return "MODE_SENSE";
	case START_STOP:		return "START_STOP/GPCMD_START_STOP_UNIT";
	case RECEIVE_DIAGNOSTIC:	return "RECEIVE_DIAGNOSTIC";
	case SEND_DIAGNOSTIC:		return "SEND_DIAGNOSTIC";
	case ALLOW_MEDIUM_REMOVAL:	return "ALLOW_MEDIUM_REMOVAL/GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL";
	case SET_WINDOW:		return "SET_WINDOW";
	case READ_CAPACITY:		return "READ_CAPACITY/GPCMD_READ_CDVD_CAPACITY";
	case READ_10:			return "READ_10/GPCMD_READ_10";
	case WRITE_10:			return "WRITE_10/GPCMD_WRITE_10";
	case SEEK_10:			return "SEEK_10/POSITION_TO_ELEMENT/GPCMD_SEEK";
	case WRITE_VERIFY:		return "WRITE_VERIFY/GPCMD_WRITE_AND_VERIFY_10";
	case VERIFY:			return "VERIFY/GPCMD_VERIFY_10";
	case SEARCH_HIGH:		return "SEARCH_HIGH";
	case SEARCH_EQUAL:		return "SEARCH_EQUAL";
	case SEARCH_LOW:		return "SEARCH_LOW";
	case SET_LIMITS:		return "SET_LIMITS";
	case PRE_FETCH:			return "PRE_FETCH/READ_POSITION";
	case SYNCHRONIZE_CACHE:		return "SYNCHRONIZE_CACHE/GPCMD_FLUSH_CACHE";
	case LOCK_UNLOCK_CACHE:		return "LOCK_UNLOCK_CACHE";
	case READ_DEFECT_DATA:		return "READ_DEFECT_DATA";
	case MEDIUM_SCAN:		return "MEDIUM_SCAN";
	case COMPARE:			return "COMPARE";
	case COPY_VERIFY:		return "COPY_VERIFY";
	case WRITE_BUFFER:		return "WRITE_BUFFER";
	case READ_BUFFER:		return "READ_BUFFER";
	case UPDATE_BLOCK:		return "UPDATE_BLOCK";
	case READ_LONG:			return "READ_LONG";
	case WRITE_LONG:		return "WRITE_LONG";
	case CHANGE_DEFINITION:		return "CHANGE_DEFINITION";
	case WRITE_SAME:		return "WRITE_SAME";
	case READ_TOC:			return "READ_TOC/GPCMD_READ_TOC_PMA_ATIP";
	case LOG_SELECT:		return "LOG_SELECT";
	case LOG_SENSE:			return "LOG_SENSE";
	case MODE_SELECT_10:		return "MODE_SELECT_10/GPCMD_MODE_SELECT_10";
	case RESERVE_10:		return "RESERVE_10";
	case RELEASE_10:		return "RELEASE_10";
	case MODE_SENSE_10:		return "MODE_SENSE_10/GPCMD_MODE_SENSE_10";
	case PERSISTENT_RESERVE_IN:	return "PERSISTENT_RESERVE_IN";
	case PERSISTENT_RESERVE_OUT:	return "PERSISTENT_RESERVE_OUT";
	case REPORT_LUNS:		return "REPORT_LUNS";
	case MAINTENANCE_IN:		return "MAINTENANCE_IN/GPCMD_SEND_KEY";
	case MOVE_MEDIUM:		return "MOVE_MEDIUM";
	case EXCHANGE_MEDIUM:		return "EXCHANGE_MEDIUM/GPCMD_LOAD_UNLOAD";
	case READ_12:			return "READ_12/GPCMD_READ_12";
	case WRITE_12:			return "WRITE_12";
	case WRITE_VERIFY_12:		return "WRITE_VERIFY_12";
	case SEARCH_HIGH_12:		return "SEARCH_HIGH_12";
	case SEARCH_EQUAL_12:		return "SEARCH_EQUAL_12";
	case SEARCH_LOW_12:		return "SEARCH_LOW_12";
	case READ_ELEMENT_STATUS:	return "READ_ELEMENT_STATUS";
	case SEND_VOLUME_TAG:		return "SEND_VOLUME_TAG/GPCMD_SET_STREAMING";
	case WRITE_LONG_2:		return "WRITE_LONG_2";
	case READ_16:			return "READ_16";
	case WRITE_16:			return "WRITE_16";
	case VERIFY_16:			return "VERIFY_16";
	case SERVICE_ACTION_IN:		return "SERVICE_ACTION_IN";
	case ATA_16:			return "ATA_16";
	case ATA_12:			return "ATA_12/GPCMD_BLANK";
	case GPCMD_CLOSE_TRACK:		return "GPCMD_CLOSE_TRACK";
	case GPCMD_GET_CONFIGURATION:	return "GPCMD_GET_CONFIGURATION";
	case GPCMD_GET_EVENT_STATUS_NOTIFICATION:	return "GPCMD_GET_EVENT_STATUS_NOTIFICATION";
	case GPCMD_GET_PERFORMANCE:	return "GPCMD_GET_PERFORMANCE";
	case GPCMD_MECHANISM_STATUS:	return "GPCMD_MECHANISM_STATUS";
	case GPCMD_PAUSE_RESUME:	return "GPCMD_PAUSE_RESUME";
	case GPCMD_PLAY_AUDIO_10:	return "GPCMD_PLAY_AUDIO_10";
	case GPCMD_PLAY_AUDIO_MSF:	return "GPCMD_PLAY_AUDIO_MSF";
	case GPCMD_PLAY_AUDIO_TI:	return "GPCMD_PLAY_AUDIO_TI/GPCMD_PLAYAUDIO_TI";
	case GPCMD_PLAY_CD:		return "GPCMD_PLAY_CD";
	case GPCMD_READ_BUFFER_CAPACITY:	return "GPCMD_READ_BUFFER_CAPACITY";
	case GPCMD_READ_CD:		return "GPCMD_READ_CD";
	case GPCMD_READ_CD_MSF:		return "GPCMD_READ_CD_MSF";
	case GPCMD_READ_DISC_INFO:	return "GPCMD_READ_DISC_INFO";
	case GPCMD_READ_DVD_STRUCTURE:	return "GPCMD_READ_DVD_STRUCTURE";
	case GPCMD_READ_FORMAT_CAPACITIES:	return "GPCMD_READ_FORMAT_CAPACITIES";
	case GPCMD_READ_HEADER:		return "GPCMD_READ_HEADER";
	case GPCMD_READ_TRACK_RZONE_INFO:	return "GPCMD_READ_TRACK_RZONE_INFO";
	case GPCMD_READ_SUBCHANNEL:	return "GPCMD_READ_SUBCHANNEL";
	case GPCMD_REPAIR_RZONE_TRACK:	return "GPCMD_REPAIR_RZONE_TRACK";
	case GPCMD_REPORT_KEY:		return "GPCMD_REPORT_KEY";
	case GPCMD_RESERVE_RZONE_TRACK:	return "GPCMD_RESERVE_RZONE_TRACK";
	case GPCMD_SEND_CUE_SHEET:	return "GPCMD_SEND_CUE_SHEET";
	case GPCMD_SCAN:		return "GPCMD_SCAN";
	case GPCMD_SEND_DVD_STRUCTURE:	return "GPCMD_SEND_DVD_STRUCTURE";
	case GPCMD_SEND_EVENT:		return "GPCMD_SEND_EVENT";
	case GPCMD_SEND_OPC:		return "GPCMD_SEND_OPC";
	case GPCMD_SET_READ_AHEAD:	return "GPCMD_SET_READ_AHEAD";
	case GPCMD_STOP_PLAY_SCAN:	return "GPCMD_STOP_PLAY_SCAN";
	case GPCMD_SET_SPEED:		return "GPCMD_SET_SPEED";
	case GPCMD_GET_MEDIA_STATUS:	return "GPCMD_GET_MEDIA_STATUS";

	default:
	    return "***UNKNOWN***";
	}
}
#else /* !DEBUG */
static inline const char *scsi_command(unsigned char cmd) { return NULL; }
#endif /* DEBUG */


static int ps3rom_slave_alloc(struct scsi_device *scsi_dev)
{
	struct ps3_storage_device *dev;

	dev = (struct ps3_storage_device *)scsi_dev->host->hostdata[0];

	dev_dbg(&dev->sbd.core, "%s:%u: id %u, lun %u, channel %u\n", __func__,
		__LINE__, scsi_dev->id, scsi_dev->lun, scsi_dev->channel);

	scsi_dev->hostdata = dev;
	return 0;
}

static int ps3rom_slave_configure(struct scsi_device *scsi_dev)
{
	struct ps3_storage_device *dev = scsi_dev->hostdata;

	dev_dbg(&dev->sbd.core, "%s:%u: id %u, lun %u, channel %u\n", __func__,
		__LINE__, scsi_dev->id, scsi_dev->lun, scsi_dev->channel);

	/*
	 * ATAPI SFF8020 devices use MODE_SENSE_10,
	 * so we can prohibit MODE_SENSE_6
	 */
	scsi_dev->use_10_for_ms = 1;

	return 0;
}

static void ps3rom_slave_destroy(struct scsi_device *scsi_dev)
{
}

static int ps3rom_queuecommand(struct scsi_cmnd *cmd,
			       void (*done)(struct scsi_cmnd *))
{
	struct ps3_storage_device *dev = cmd->device->hostdata;
	struct ps3rom_private *priv = ps3rom_priv(dev);

	dev_dbg(&dev->sbd.core, "%s:%u: command 0x%02x (%s)\n", __func__,
		__LINE__, cmd->cmnd[0], scsi_command(cmd->cmnd[0]));

	spin_lock_irq(&priv->lock);
	if (priv->cmd) {
		/* no more than one can be processed */
		dev_err(&dev->sbd.core, "%s:%u: more than 1 command queued\n",
			__func__, __LINE__);
		spin_unlock_irq(&priv->lock);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	// FIXME Prevalidate commands?
	priv->cmd = cmd;
	priv->scsi_done = done;
	spin_unlock_irq(&priv->lock);
	wake_up_process(priv->thread);
	return 0;


	return -1;
}

/*
 * copy data from device into scatter/gather buffer
 */
static int fill_from_dev_buffer(struct scsi_cmnd *cmd, const void *buf,
				int buflen)
{
	int k, req_len, act_len, len, active;
	void *kaddr;
	struct scatterlist *sgpnt;

	if (!cmd->request_bufflen)
		return 0;

	if (!cmd->request_buffer)
		return DID_ERROR << 16;

	if (cmd->sc_data_direction != DMA_BIDIRECTIONAL &&
	    cmd->sc_data_direction != DMA_FROM_DEVICE)
		return DID_ERROR << 16;

	if (!cmd->use_sg) {
		req_len = cmd->request_bufflen;
		act_len = min(req_len, buflen);
		memcpy(cmd->request_buffer, buf, act_len);
		cmd->resid = req_len - act_len;
		return 0;
	}

	sgpnt = cmd->request_buffer;
	active = 1;
	for (k = 0, req_len = 0, act_len = 0; k < cmd->use_sg; ++k, ++sgpnt) {
		if (active) {
			kaddr = kmap_atomic(sgpnt->page, KM_USER0);
			if (!kaddr)
				return DID_ERROR << 16;
			len = sgpnt->length;
			if ((req_len + len) > buflen) {
				active = 0;
				len = buflen - req_len;
			}
			memcpy(kaddr + sgpnt->offset, buf + req_len, len);
			kunmap_atomic(kaddr, KM_USER0);
			act_len += len;
		}
		req_len += sgpnt->length;
	}
	cmd->resid = req_len - act_len;
	return 0;
}

/*
 * copy data from scatter/gather into device's buffer
 */
static int fetch_to_dev_buffer(struct scsi_cmnd *cmd, void *buf, int buflen)
{
	int k, req_len, len, fin;
	void *kaddr;
	struct scatterlist *sgpnt;

	if (!cmd->request_bufflen)
		return 0;

	if (!cmd->request_buffer)
		return -1;

	if (cmd->sc_data_direction != DMA_BIDIRECTIONAL &&
	    cmd->sc_data_direction != DMA_TO_DEVICE)
		return -1;

	if (!cmd->use_sg) {
		req_len = cmd->request_bufflen;
		len = min(req_len, buflen);
		memcpy(buf, cmd->request_buffer, len);
		return len;
	}

	sgpnt = cmd->request_buffer;
	for (k = 0, req_len = 0, fin = 0; k < cmd->use_sg; ++k, ++sgpnt) {
		kaddr = kmap_atomic(sgpnt->page, KM_USER0);
		if (!kaddr)
			return -1;
		len = sgpnt->length;
		if ((req_len + len) > buflen) {
			len = buflen - req_len;
			fin = 1;
		}
		memcpy(buf + req_len, kaddr + sgpnt->offset, len);
		kunmap_atomic(kaddr, KM_USER0);
		if (fin)
			return req_len + len;
		req_len += sgpnt->length;
	}
	return req_len;
}

static int decode_lv1_status(u64 status, unsigned char *sense_key,
			     unsigned char *asc, unsigned char *ascq)
{
	if (((status >> 24) & 0xff) != SAM_STAT_CHECK_CONDITION)
		return -1;

	*sense_key = (status >> 16) & 0xff;
	*asc       = (status >>  8) & 0xff;
	*ascq      =  status        & 0xff;
	return 0;
}

static inline unsigned int srb6_lba(const struct scsi_cmnd *cmd)
{
	BUG_ON(cmd->cmnd[1] & 0xe0);	// FIXME lun == 0
	return cmd->cmnd[1] << 16 | cmd->cmnd[2] << 8 | cmd->cmnd[3];
}

static inline unsigned int srb6_len(const struct scsi_cmnd *cmd)
{
	return cmd->cmnd[4];
}

static inline unsigned int srb10_lba(const struct scsi_cmnd *cmd)
{
	return cmd->cmnd[2] << 24 | cmd->cmnd[3] << 16 | cmd->cmnd[4] << 8 |
	       cmd->cmnd[5];
}

static inline unsigned int srb10_len(const struct scsi_cmnd *cmd)
{
	return cmd->cmnd[7] << 8 | cmd->cmnd[8];
}

static inline unsigned int cdda_raw_len(const struct scsi_cmnd *cmd)
{
	unsigned int nframes;

	nframes = cmd->cmnd[6] << 16 | cmd->cmnd[7] <<  8 | cmd->cmnd[8];
	return nframes * CD_FRAMESIZE_RAW;
}

static u64 ps3rom_send_atapi_command(struct ps3_storage_device *dev,
				     struct lv1_atapi_cmnd_block *cmd)
{
	int res;
	u64 lpar;

	dev_dbg(&dev->sbd.core, "%s:%u: send ATAPI command 0x%02x (%s)\n",
		__func__, __LINE__, cmd->pkt[0], scsi_command(cmd->pkt[0]));

	init_completion(&dev->irq_done);

	lpar = ps3_mm_phys_to_lpar(__pa(cmd));
	res = lv1_storage_send_device_command(dev->sbd.did.dev_id,
					      LV1_STORAGE_SEND_ATAPI_COMMAND,
					      lpar, sizeof(*cmd), cmd->buffer,
					      cmd->arglen, &dev->tag);
	if (res) {
		dev_err(&dev->sbd.core,
			"%s:%u: send_device_command failed %d\n", __func__,
			__LINE__, res);
		return -1;
	}

	wait_for_completion(&dev->irq_done);
	if (dev->lv1_status)
		dev_dbg(&dev->sbd.core, "%s:%u: ATAPI command failed 0x%lx\n",
			__func__, __LINE__, dev->lv1_status);
	else
		dev_dbg(&dev->sbd.core, "%s:%u: ATAPI command completed\n",
			__func__, __LINE__);

	return dev->lv1_status;
}

static void ps3rom_atapi_request(struct ps3_storage_device *dev,
				 struct scsi_cmnd *cmd, unsigned int len,
				 int proto, int in_out, int auto_sense)
{
	struct lv1_atapi_cmnd_block atapi_cmnd;
	unsigned char *cmnd = cmd->cmnd;
	u64 status;
	unsigned char sense_key, asc, ascq;

	if (len > dev->bounce_size) {
		static int printed;
		if (!printed++)
			dev_err(&dev->sbd.core,
				"%s:%u: data size too large %u > %lu\n",
			       __func__, __LINE__, len, dev->bounce_size);
		cmd->result = DID_ERROR << 16;
		memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		cmd->sense_buffer[0] = 0x70;
		cmd->sense_buffer[2] = ILLEGAL_REQUEST;
		return;
	}

	memset(&atapi_cmnd, 0, sizeof(struct lv1_atapi_cmnd_block));
	memcpy(&atapi_cmnd.pkt, cmnd, 12);
	atapi_cmnd.pktlen = 12;
	atapi_cmnd.proto = proto;
	if (in_out != DIR_NA)
		atapi_cmnd.in_out = in_out;

	if (atapi_cmnd.in_out == DIR_WRITE) {
		// FIXME check error
		fetch_to_dev_buffer(cmd, dev->bounce_buf, len);
	}

	atapi_cmnd.block_size = 1; /* transfer size is block_size * blocks */

	atapi_cmnd.blocks = atapi_cmnd.arglen = len;
	atapi_cmnd.buffer = dev->bounce_lpar;

	status = ps3rom_send_atapi_command(dev, &atapi_cmnd);
	if (status == -1) {
		cmd->result = DID_ERROR << 16; /* FIXME: is better other error code ? */
		return;
	}

	if (!status) {
		/* OK, completed */
		if (atapi_cmnd.in_out == DIR_READ) {
			// FIXME check error
			fill_from_dev_buffer(cmd, dev->bounce_buf, len);
		}
		cmd->result = DID_OK << 16;
		return;
	}

	/* error */
	if (!auto_sense) {
		cmd->result = (DID_ERROR << 16) | (CHECK_CONDITION << 1);
		dev_err(&dev->sbd.core, "%s:%u: end error without autosense\n",
		       __func__, __LINE__);
		return;
	}

	if (!decode_lv1_status(status, &sense_key, &asc, &ascq)) {
		/* lv1 may have issued autosense ... */
		cmd->sense_buffer[0]  = 0x70;
		cmd->sense_buffer[2]  = sense_key;
		cmd->sense_buffer[7]  = 16 - 6;
		cmd->sense_buffer[12] = asc;
		cmd->sense_buffer[13] = ascq;
		cmd->result = SAM_STAT_CHECK_CONDITION;
		return;
	}

	/* do auto sense by ourselves */
	memset(&atapi_cmnd, 0, sizeof(struct lv1_atapi_cmnd_block));
	atapi_cmnd.pkt[0] = REQUEST_SENSE;
	atapi_cmnd.pkt[4] = 18;
	atapi_cmnd.pktlen = 12;
	atapi_cmnd.arglen = atapi_cmnd.blocks = atapi_cmnd.pkt[4];
	atapi_cmnd.block_size = 1;
	atapi_cmnd.proto = DMA_PROTO;
	atapi_cmnd.in_out = DIR_READ;
	atapi_cmnd.buffer = dev->bounce_lpar;

	/* issue REQUEST_SENSE command */
	status = ps3rom_send_atapi_command(dev, &atapi_cmnd);
	if (status == -1) {
		cmd->result = DID_ERROR << 16; /* FIXME: is better other error code ? */
		return;
	}

	/* scsi spec says request sense should never get error */
	if (status) {
		decode_lv1_status(status, &sense_key, &asc, &ascq);
		dev_err(&dev->sbd.core,
			"%s:%u: auto REQUEST_SENSE error %#x %#x %#x\n",
			__func__, __LINE__, sense_key, asc, ascq);
	}

	memcpy(cmd->sense_buffer, dev->bounce_buf,
	       min_t(size_t, atapi_cmnd.pkt[4], SCSI_SENSE_BUFFERSIZE));
	cmd->result = SAM_STAT_CHECK_CONDITION;
}

static void ps3rom_read_request(struct ps3_storage_device *dev,
				struct scsi_cmnd *cmd, u32 start_sector,
				u32 sectors)
{
	u64 status;

	status = ps3stor_read_write_sectors(dev, dev->bounce_lpar,
					    start_sector, sectors, 0);
	if (status == -1) {
		cmd->result = DID_ERROR << 16; /* FIXME: other error code? */
		return;
	}

	if (status) {
		memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		decode_lv1_status(dev->lv1_status, &cmd->sense_buffer[2],
				  &cmd->sense_buffer[12],
				  &cmd->sense_buffer[13]);
		cmd->sense_buffer[7] = 16 - 6;	// FIXME hardcoded numbers?
		cmd->result = SAM_STAT_CHECK_CONDITION;
		return;
	}

	// FIXME check error
	fill_from_dev_buffer(cmd, dev->bounce_buf, sectors * CD_FRAMESIZE);

	cmd->result = DID_OK << 16;
}

static void ps3rom_write_request(struct ps3_storage_device *dev,
				 struct scsi_cmnd *cmd, u32 start_sector,
				 u32 sectors)
{
	u64 status;

	// FIXME check error
	fetch_to_dev_buffer(cmd, dev->bounce_buf, sectors * CD_FRAMESIZE);

	status = ps3stor_read_write_sectors(dev, dev->bounce_lpar,
					    start_sector, sectors, 1);
	if (status == -1) {
		cmd->result = DID_ERROR << 16; /* FIXME: other error code? */
		return;
	}

	if (status) {
		memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		decode_lv1_status(dev->lv1_status, &cmd->sense_buffer[2],
				  &cmd->sense_buffer[12],
				  &cmd->sense_buffer[13]);
		cmd->sense_buffer[7] = 16 - 6;	// FIXME hardcoded numbers?
		cmd->result = SAM_STAT_CHECK_CONDITION;
		return;
	}

	cmd->result = DID_OK << 16;
}

static void ps3rom_request(struct ps3_storage_device *dev,
			   struct scsi_cmnd *cmd)
{
	unsigned char opcode = cmd->cmnd[0];
	struct ps3rom_private *priv = ps3rom_priv(dev);

	dev_dbg(&dev->sbd.core, "%s:%u: command 0x%02x (%s)\n", __func__,
		__LINE__, opcode, scsi_command(opcode));

	switch (opcode) {
	case INQUIRY:
		ps3rom_atapi_request(dev, cmd, srb6_len(cmd),
				     PIO_DATA_IN_PROTO, DIR_READ, 1);
		break;

	case REQUEST_SENSE:
		ps3rom_atapi_request(dev, cmd, srb6_len(cmd),
				     PIO_DATA_IN_PROTO, DIR_READ, 0);
		break;

	case ALLOW_MEDIUM_REMOVAL:
	case START_STOP:
	case TEST_UNIT_READY:
		ps3rom_atapi_request(dev, cmd, 0, NON_DATA_PROTO, DIR_NA, 1);
		break;

	case READ_CAPACITY:
		ps3rom_atapi_request(dev, cmd, 8, PIO_DATA_IN_PROTO, DIR_READ,
				     1);
		break;

	case MODE_SENSE_10:
	case READ_TOC:
	case GPCMD_GET_CONFIGURATION:
	case GPCMD_READ_DISC_INFO:
		ps3rom_atapi_request(dev, cmd, srb10_len(cmd),
				     PIO_DATA_IN_PROTO, DIR_READ, 1);
		break;

	case READ_6:
		ps3rom_read_request(dev, cmd, srb6_lba(cmd), srb6_len(cmd));
		break;

	case READ_10:
		ps3rom_read_request(dev, cmd, srb10_lba(cmd), srb10_len(cmd));
		break;

	case WRITE_6:
		ps3rom_write_request(dev, cmd, srb6_lba(cmd), srb6_len(cmd));
		break;

	case WRITE_10:
		ps3rom_write_request(dev, cmd, srb10_lba(cmd), srb10_len(cmd));
		break;

	case GPCMD_READ_CD:
		ps3rom_atapi_request(dev, cmd, cdda_raw_len(cmd), DMA_PROTO,
				     DIR_READ, 1);
		break;

	default:
		dev_err(&dev->sbd.core, "%s:%u: illegal request 0x%02x (%s)\n",
			__func__, __LINE__, opcode, scsi_command(opcode));
		cmd->result = DID_ERROR << 16;
		memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		cmd->sense_buffer[0] = 0x70;
		cmd->sense_buffer[2] = ILLEGAL_REQUEST;
	}

	spin_lock_irq(&priv->lock);
	priv->cmd = NULL;
	priv->scsi_done(cmd);
	spin_unlock_irq(&priv->lock);
}

static int ps3rom_thread(void *data)
{
	struct ps3_storage_device *dev = data;
	struct ps3rom_private *priv = ps3rom_priv(dev);
	struct scsi_cmnd *cmd;

	dev_dbg(&dev->sbd.core, "%s thread init\n", __func__);

	current->flags |= PF_NOFREEZE;

	while (!kthread_should_stop()) {
		spin_lock_irq(&priv->lock);
		set_current_state(TASK_INTERRUPTIBLE);
		cmd = priv->cmd;
		spin_unlock_irq(&priv->lock);
		if (!cmd) {
			schedule();
			continue;
		}
		ps3rom_request(dev, cmd);
	}

	dev_dbg(&dev->sbd.core, "%s thread exit\n", __func__);
	return 0;
}


static struct scsi_host_template ps3rom_host_template = {
	.name =			DEVICE_NAME,
	.slave_alloc =		ps3rom_slave_alloc,
	.slave_configure =	ps3rom_slave_configure,
	.slave_destroy =	ps3rom_slave_destroy,
	.queuecommand =		ps3rom_queuecommand,
	.can_queue =		1,
	.this_id =		7,
	.sg_tablesize =		SG_ALL,
	.cmd_per_lun =		1,
	.emulated =             1,		/* only sg driver uses this */
	.max_sectors =		PS3ROM_MAX_SECTORS,
	.use_clustering =	ENABLE_CLUSTERING,
	.module =		THIS_MODULE,
};


static int __devinit ps3rom_probe(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);
	struct ps3rom_private *priv;
	int res, error;
	struct Scsi_Host *host;
	struct task_struct *task;

	/* special case: CD-ROM is assumed always accessible */
	dev->accessible_regions = 1;

	if (dev->blk_size != CD_FRAMESIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u: cannot handle block size %lu\n", __func__,
			__LINE__, dev->blk_size);
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ps3rom_priv(dev) = priv;
	spin_lock_init(&priv->lock);

	dev->bounce_size = BOUNCE_SIZE;
	dev->bounce_buf = kmalloc(BOUNCE_SIZE, GFP_DMA);
	if (!dev->bounce_buf) {
		error = -ENOMEM;
		goto fail_free_priv;
	}

	error = ps3_open_hv_device(&dev->sbd);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_open_hv_device failed %d\n", __func__,
			__LINE__, error);
		goto fail_free_bounce;
	}

	error = ps3_sb_event_receive_port_setup(PS3_BINDING_CPU_ANY,
						&dev->sbd.did,
						dev->sbd.interrupt_id,
						&dev->irq);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_sb_event_receive_port_setup failed %d\n",
		       __func__, __LINE__, error);
		goto fail_close_device;
	}

	error = request_irq(dev->irq, ps3stor_interrupt, IRQF_DISABLED,
			    DEVICE_NAME, dev);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: request_irq failed %d\n",
			__func__, __LINE__, error);
		goto fail_sb_event_receive_port_destroy;
	}

	dev->bounce_lpar = ps3_mm_phys_to_lpar(__pa(dev->bounce_buf));

	dev->sbd.d_region = &dev->dma_region;
	ps3_dma_region_init(&dev->dma_region, &dev->sbd.did, PS3_DMA_4K,
			    PS3_DMA_OTHER, dev->bounce_buf,
			    dev->bounce_size, PS3_IOBUS_SB);
	res = ps3_dma_region_create(&dev->dma_region);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: cannot create DMA region\n",
			__func__, __LINE__);
		error = -ENOMEM;
		goto fail_free_irq;
	}

	dev->bounce_dma = dma_map_single(&dev->sbd.core, dev->bounce_buf,
					 dev->bounce_size, DMA_BIDIRECTIONAL);
	if (!dev->bounce_dma) {
		dev_err(&dev->sbd.core, "%s:%u: map DMA region failed\n",
			__func__, __LINE__);
		error = -ENODEV;
		goto fail_free_dma;
	}

	host = scsi_host_alloc(&ps3rom_host_template,
			       sizeof(struct ps3_system_bus_device *));
	if (!host) {
		dev_err(&dev->sbd.core, "%s:%u: scsi_host_alloc failed\n",
			__func__, __LINE__);
		goto fail_unmap_dma;
	}

	priv->host = host;
	host->hostdata[0] = (unsigned long)dev;

	/* One device/LUN per SCSI bus */
	host->max_id = 1;
	host->max_lun = 1;

	error = scsi_add_host(host, &dev->sbd.core);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: scsi_host_alloc failed %d\n",
			__func__, __LINE__, error);
		error = -ENODEV;
		goto fail_host_put;
	}

	task = kthread_run(ps3rom_thread, dev, DEVICE_NAME);
	if (IS_ERR(task)) {
		error = PTR_ERR(task);
		goto fail_remove_host;
	}
	priv->thread = task;

	scsi_scan_host(host);
	return 0;

fail_remove_host:
	scsi_remove_host(host);
fail_host_put:
	scsi_host_put(host);
fail_unmap_dma:
	dma_unmap_single(&dev->sbd.core, dev->bounce_dma, dev->bounce_size,
			 DMA_BIDIRECTIONAL);
fail_free_dma:
	ps3_dma_region_free(&dev->dma_region);
fail_free_irq:
	free_irq(dev->irq, dev);
fail_sb_event_receive_port_destroy:
	ps3_sb_event_receive_port_destroy(&dev->sbd.did, dev->sbd.interrupt_id,
					  dev->irq);
fail_close_device:
	ps3_close_hv_device(&dev->sbd);
fail_free_bounce:
	kfree(dev->bounce_buf);
fail_free_priv:
	kfree(priv);
	return error;
}

static int ps3rom_remove(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);
	struct ps3rom_private *priv = ps3rom_priv(dev);
	int error;

	dev_dbg(&dev->sbd.core, "%s:%u\n", __func__, __LINE__);

	if (priv->host) {
		scsi_remove_host(priv->host);
		scsi_host_put(priv->host);
	}

	if (priv->thread)
		kthread_stop(priv->thread);

	dma_unmap_single(&dev->sbd.core, dev->bounce_dma, dev->bounce_size,
			 DMA_BIDIRECTIONAL);
	ps3_dma_region_free(&dev->dma_region);

	free_irq(dev->irq, dev);

	error = ps3_sb_event_receive_port_destroy(&dev->sbd.did,
						  dev->sbd.interrupt_id,
						  dev->irq);
	if (error)
		dev_err(&dev->sbd.core,
			"%s:%u: destroy event receive port failed %d\n",
			__func__, __LINE__, error);

	error = ps3_close_hv_device(&dev->sbd);
	if (error)
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_close_hv_device failed %d\n", __func__,
			__LINE__, error);

	kfree(dev->bounce_buf);
	kfree(priv);
	return 0;
}


static struct ps3_system_bus_driver ps3rom = {
	.match_id	= PS3_MATCH_ID_STOR_ROM,
	.core.name	= DEVICE_NAME,
	.probe		= ps3rom_probe,
	.remove		= ps3rom_remove
};


static int __init ps3rom_init(void)
{
	return ps3_system_bus_driver_register(&ps3rom, PS3_IOBUS_SB);
}

static void __exit ps3rom_exit(void)
{
	return ps3_system_bus_driver_unregister(&ps3rom);
}

module_init(ps3rom_init);
module_exit(ps3rom_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 ROM Storage Driver");
MODULE_AUTHOR("Sony Corporation");
