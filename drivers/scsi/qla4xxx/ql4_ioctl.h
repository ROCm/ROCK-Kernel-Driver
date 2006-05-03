/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
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
#define IOCTL_PASSTHRU_TOV			60

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
