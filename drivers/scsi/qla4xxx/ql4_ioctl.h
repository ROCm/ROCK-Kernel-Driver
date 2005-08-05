/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
#ifndef _QL4_IOCTL_H_
#define _QL4_IOCTL_H_

#include <linux/blkdev.h>
#include <asm/uaccess.h>

/*---------------------------------------------------------------------------*/

typedef struct {
	int cmd;
	char *s;
} ioctl_tbl_row_t;

#define	QL_KMEM_ZALLOC(siz)	ql4_kzmalloc((siz), GFP_ATOMIC)
#define	QL_KMEM_FREE(ptr)	kfree((ptr))

/* Defines for Passthru */
#define IOCTL_INVALID_STATUS			0xffff
#define IOCTL_PASSTHRU_TOV			30

/*
 * extern from ql4_xioctl.c
 */
extern void *
Q64BIT_TO_PTR(uint64_t);

extern inline void *
ql4_kzmalloc(int, int);

extern char *
IOCTL_TBL_STR(int, int);

extern int
qla4xxx_alloc_ioctl_mem(scsi_qla_host_t *);

extern void
qla4xxx_free_ioctl_mem(scsi_qla_host_t *);

extern int
qla4xxx_get_ioctl_scrap_mem(scsi_qla_host_t *, void **, uint32_t);

extern void
qla4xxx_free_ioctl_scrap_mem(scsi_qla_host_t *);

/*
 * from ql4_inioct.c
 */
extern ioctl_tbl_row_t IOCTL_SCMD_IGET_DATA_TBL[];
extern ioctl_tbl_row_t IOCTL_SCMD_ISET_DATA_TBL[];

extern int
qla4intioctl_logout_iscsi(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

extern int
qla4intioctl_copy_fw_flash(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

extern int
qla4intioctl_iocb_passthru(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

extern int
qla4intioctl_ping(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

extern int
qla4intioctl_get_data(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

extern int
qla4intioctl_set_data(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

extern int
qla4intioctl_hba_reset(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

/*
 * from ql4_init.c
 */
extern uint8_t
qla4xxx_logout_device(scsi_qla_host_t *, uint16_t, uint16_t);

extern uint8_t
qla4xxx_login_device(scsi_qla_host_t *, uint16_t, uint16_t);

extern uint8_t
qla4xxx_delete_device(scsi_qla_host_t *, uint16_t, uint16_t);

#endif
