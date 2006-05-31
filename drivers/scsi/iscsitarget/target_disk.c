/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 *
 * heavily based on code from kernel/iscsi.c:
 *   Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>,
 *   licensed under the terms of the GNU GPL v2.0,
 */

#include <scsi/scsi.h>

#include "iscsi.h"
#include "iscsi_dbg.h"

static int insert_disconnect_pg(u8 *ptr)
{
	unsigned char disconnect_pg[] = {0x02, 0x0e, 0x80, 0x80, 0x00, 0x0a, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	memcpy(ptr, disconnect_pg, sizeof(disconnect_pg));
	return sizeof(disconnect_pg);
}

static int insert_caching_pg(u8 *ptr)
{
	unsigned char caching_pg[] = {0x08, 0x12, 0x10, 0x00, 0xff, 0xff, 0x00, 0x00,
				      0xff, 0xff, 0xff, 0xff, 0x80, 0x14, 0x00, 0x00,
				      0x00, 0x00, 0x00, 0x00};

	memcpy(ptr, caching_pg, sizeof(caching_pg));
	return sizeof(caching_pg);
}

static int insert_ctrl_m_pg(u8 *ptr)
{
	unsigned char ctrl_m_pg[] = {0x0a, 0x0a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x02, 0x4b};

	memcpy(ptr, ctrl_m_pg, sizeof(ctrl_m_pg));
	return sizeof(ctrl_m_pg);
}

static int insert_iec_m_pg(u8 *ptr)
{
	unsigned char iec_m_pg[] = {0x1c, 0xa, 0x08, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x00};

	memcpy(ptr, iec_m_pg, sizeof(iec_m_pg));
	return sizeof(iec_m_pg);
}

static int insert_format_m_pg(u8 *ptr)
{
	unsigned char format_m_pg[] = {0x03, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				       0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00,
				       0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00};
	memcpy(ptr, format_m_pg, sizeof(format_m_pg));
	return sizeof(format_m_pg);
}

static int insert_geo_m_pg(u8 *ptr, u64 sec)
{
	unsigned char geo_m_pg[] = {0x04, 0x16, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x3a, 0x98, 0x00, 0x00};
	u32 ncyl, *p;

	/* assume 0xff heads, 15krpm. */
	memcpy(ptr, geo_m_pg, sizeof(geo_m_pg));
	ncyl = sec >> 14; /* 256 * 64 */
	p = (u32 *)(ptr + 1);
	*p = *p | cpu_to_be32(ncyl);
	return sizeof(geo_m_pg);
}

static int build_mode_sense_response(struct iscsi_cmnd *cmnd)
{
	struct iscsi_cmd *req = cmnd_hdr(cmnd);
	struct tio *tio = cmnd->tio;
	u8 *data, *scb = req->cdb;
	int len = 4, err = 0;
	u8 pcode;

	pcode = req->cdb[2] & 0x3f;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);

	if ((scb[1] & 0x8))
		data[3] = 0;
	else {
		data[3] = 8;
		len += 8;
		*(u32 *)(data + 4) = (cmnd->lun->blk_cnt >> 32) ?
			cpu_to_be32(0xffffffff) : cpu_to_be32(cmnd->lun->blk_cnt);
		*(u32 *)(data + 8) = cpu_to_be32(1 << cmnd->lun->blk_shift);
	}

	switch (pcode) {
	case 0x0:
		break;
	case 0x2:
		len += insert_disconnect_pg(data + len);
		break;
	case 0x3:
		len += insert_format_m_pg(data + len);
		break;
	case 0x4:
		len += insert_geo_m_pg(data + len, cmnd->lun->blk_cnt);
		break;
	case 0x8:
		len += insert_caching_pg(data + len);
		break;
	case 0xa:
		len += insert_ctrl_m_pg(data + len);
		break;
	case 0x1c:
		len += insert_iec_m_pg(data + len);
		break;
	case 0x3f:
		len += insert_disconnect_pg(data + len);
		len += insert_format_m_pg(data + len);
		len += insert_geo_m_pg(data + len, cmnd->lun->blk_cnt);
		len += insert_caching_pg(data + len);
		len += insert_ctrl_m_pg(data + len);
		len += insert_iec_m_pg(data + len);
		break;
	default:
		err = -1;
	}

	data[0] = len - 1;

	tio_set(tio, len, 0);

	return err;
}

static int build_inquiry_response(struct iscsi_cmnd *cmnd)
{
	struct iscsi_cmd *req = cmnd_hdr(cmnd);
	struct tio *tio = cmnd->tio;
	u8 *data;
	u8 *scb = req->cdb;
	int err = -1;

	if (((req->cdb[1] & 0x3) == 0x3) || (!(req->cdb[1] & 0x3) && req->cdb[2]))
		return err;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);

	if (!(scb[1] & 0x3)) {
		data[2] = 4;
		data[3] = 0x42;
		data[4] = 59;
		data[7] = 0x02;
		memset(data + 8, 0x20, 28);
		memcpy(data + 8,
		       VENDOR_ID, min_t(size_t, strlen(VENDOR_ID), 8));
		memcpy(data + 16,
		       PRODUCT_ID, min_t(size_t, strlen(PRODUCT_ID), 16));
		memcpy(data + 32,
		       PRODUCT_REV, min_t(size_t, strlen(PRODUCT_REV), 4));
		data[58] = 0x03;
		data[59] = 0x20;
		data[60] = 0x09;
		data[61] = 0x60;
		data[62] = 0x03;
		data[63] = 0x00;
		tio_set(tio, 64, 0);
		err = 0;
	} else if (scb[1] & 0x2) {
		/* CmdDt bit is set */
		/* We do not support it now. */
		data[1] = 0x1;
		data[5] = 0;
		tio_set(tio, 6, 0);
		err = 0;
	} else if (scb[1] & 0x1) {
		/* EVPD bit set */
		if (scb[2] == 0x0) {
			data[1] = 0x0;
			data[3] = 3;
			data[4] = 0x0;
			data[5] = 0x80;
			data[6] = 0x83;
			tio_set(tio, 7, 0);
			err = 0;
		} else if (scb[2] == 0x80) {
			data[1] = 0x80;
			data[3] = 4;
			memset(data + 4, 0x20, 4);
			tio_set(tio, 8, 0);
			err = 0;
		} else if (scb[2] == 0x83) {
			u32 len = SCSI_ID_LEN * sizeof(u8);

			data[1] = 0x83;
			data[3] = len + 4;
			data[4] = 0x1;
			data[5] = 0x1;
			data[7] = len;
			if (cmnd->lun) /* We need this ? */
				memcpy(data + 8, cmnd->lun->scsi_id, len);
			tio_set(tio, len + 8, 0);
			err = 0;
		}
	}

	tio_set(tio, min_t(u8, tio->size, scb[4]), 0);
	if (!cmnd->lun)
		data[0] = TYPE_NO_LUN;

	return err;
}

static int build_report_luns_response(struct iscsi_cmnd *cmnd)
{
	struct iscsi_cmd *req = cmnd_hdr(cmnd);
	struct tio *tio = cmnd->tio;
	u32 *data, size, len;
	struct iet_volume *lun;
	int rest, idx = 0;

	size = be32_to_cpu(*(u32 *)&req->cdb[6]);
	if (size < 16)
		return -1;

	len = atomic_read(&cmnd->conn->session->target->nr_volumes) * 8;
	size = min(size & ~(8 - 1), len + 8);

	assert(!tio);
	tio = cmnd->tio = tio_alloc(get_pgcnt(size, 0));
	tio_set(tio, size, 0);

	data = page_address(tio->pvec[idx]);
	assert(data);
	*data++ = cpu_to_be32(len);
	*data++ = 0;
	size -= 8;
	rest = PAGE_CACHE_SIZE - 8;
	list_for_each_entry(lun, &cmnd->conn->session->target->volumes, list) {
		if (lun->l_state != IDEV_RUNNING)
			continue;

		*data++ = cpu_to_be32((0x3ff & lun->lun) << 16 |
				      ((lun->lun > 0xff) ? (0x1 << 30) : 0));
		*data++ = 0;
		if ((size -= 8) == 0)
			break;
		if ((rest -= 8) == 0) {
			idx++;
			data = page_address(tio->pvec[idx]);
			rest = PAGE_CACHE_SIZE;
		}
	}

	return 0;
}

static int build_read_capacity_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	u32 *data;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);

	data[0] = (cmnd->lun->blk_cnt >> 32) ?
		cpu_to_be32(0xffffffff) : cpu_to_be32(cmnd->lun->blk_cnt - 1);
	data[1] = cpu_to_be32(1U << cmnd->lun->blk_shift);

	tio_set(tio, 8, 0);
	return 0;
}

static int build_request_sense_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	u8 *data;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	memset(data, 0, 18);
	data[0] = 0xf0;
	data[1] = 0;
	data[2] = NO_SENSE;
	data[7] = 10;
	tio_set(tio, 18, 0);

	return 0;
}

static int build_sevice_action_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	u32 *data;
	u64 *data64;

/* 	assert((req->scb[1] & 0x1f) == 0x10); */
	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);
	data64 = (u64*) data;
	data64[0] = cpu_to_be64(cmnd->lun->blk_cnt - 1);
	data[2] = cpu_to_be32(1UL << cmnd->lun->blk_shift);

	tio_set(tio, 32, 0);
	return 0;
}

static int build_read_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;

	assert(tio);
	assert(cmnd->lun);

	return tio_read(cmnd->lun, tio);
}

static int build_write_response(struct iscsi_cmnd *cmnd)
{
	int err;
	struct tio *tio = cmnd->tio;

	assert(cmnd);
	assert(tio);
	assert(cmnd->lun);

	list_del_init(&cmnd->list);
	err = tio_write(cmnd->lun, tio);
	if (!err)
		err = tio_sync(cmnd->lun, tio);

	return err;
}

static int build_generic_response(struct iscsi_cmnd *cmnd)
{
	return 0;
}

static int disk_execute_cmnd(struct iscsi_cmnd *cmnd)
{
	struct iscsi_cmd *req = cmnd_hdr(cmnd);

	req->opcode &= ISCSI_OPCODE_MASK;

	switch (req->cdb[0]) {
	case INQUIRY:
		send_data_rsp(cmnd, build_inquiry_response);
		break;
	case REPORT_LUNS:
		send_data_rsp(cmnd, build_report_luns_response);
		break;
	case READ_CAPACITY:
		send_data_rsp(cmnd, build_read_capacity_response);
		break;
	case MODE_SENSE:
		send_data_rsp(cmnd, build_mode_sense_response);
		break;
	case REQUEST_SENSE:
		send_data_rsp(cmnd, build_request_sense_response);
		break;
	case SERVICE_ACTION_IN:
		send_data_rsp(cmnd, build_sevice_action_response);
		break;
	case READ_6:
	case READ_10:
	case READ_16:
		send_data_rsp(cmnd, build_read_response);
		break;
	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
	case WRITE_VERIFY:
		send_scsi_rsp(cmnd, build_write_response);
		break;
	case START_STOP:
	case TEST_UNIT_READY:
	case SYNCHRONIZE_CACHE:
	case VERIFY:
	case VERIFY_16:
	case RESERVE:
	case RELEASE:
	case RESERVE_10:
	case RELEASE_10:
		send_scsi_rsp(cmnd, build_generic_response);
		break;
	default:
		eprintk("%s\n", "we should not come here!");
		break;
	}

	return 0;
}

struct target_type disk_ops =
{
	.id = 0,
	.execute_cmnd = disk_execute_cmnd,
};
