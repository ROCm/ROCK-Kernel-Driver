/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE                                     *
 *                                                                            *
 * QLogic ISP4xxx device driver for Linux 2.4.x                               *
 * Copyright (C) 2004 Qlogic Corporation                                      *
 * (www.qlogic.com)                                                           *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify it    *
 * under the terms of the GNU General Public License as published by the      *
 * Free Software Foundation; either version 2, or (at your option) any        *
 * later version.                                                             *
 *                                                                            *
 * This program is distributed in the hope that it will be useful, but        *
 * WITHOUT ANY WARRANTY; without even the implied warranty of                 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU          *
 * General Public License for more details.                                   *
 *                                                                            *
 ****************************************************************************/
#ifndef _QL4_OS_H_
#define _QL4_OS_H_

#define __KERNEL_SYSCALLS__
#define SHUTDOWN_SIGS	(sigmask(SIGHUP))


#define HOST_STS_TBL(){		  \
	"DID_OK",                 \
	"DID_NO_CONNECT",         \
	"DID_BUS_BUSY",           \
	"DID_TIME_OUT",           \
	"DID_BAD_TARGET",         \
	"DID_ABORT",              \
	"DID_PARITY",             \
	"DID_ERROR",              \
	"DID_RESET",              \
	"DID_BAD_INTR",           \
	NULL			  \
}

/*---------------------------------------------------------------------------*/

/* We use the Scsi_Pointer structure that's included with each command
 * SCSI_Cmnd as a scratchpad for our SRB.
 */
#define CMD_SP(Cmnd)	    ((Cmnd)->SCp.ptr)

/* Additional fields used by ioctl passthru */
#define CMD_PASSTHRU_TYPE(Cmnd) (((Cmnd)->SCp.buffer))
#define CMD_COMPL_STATUS(Cmnd)  ((Cmnd)->SCp.this_residual)
#define CMD_RESID_LEN(Cmnd)     ((Cmnd)->SCp.buffers_residual)
#define CMD_SCSI_STATUS(Cmnd)   ((Cmnd)->SCp.Status)
#define CMD_ACTUAL_SNSLEN(Cmnd) ((Cmnd)->SCp.have_data_in)
#define CMD_HOST_STATUS(Cmnd)   ((Cmnd)->SCp.Message)
#define CMD_ISCSI_RESPONSE(Cmnd)((Cmnd)->SCp.sent_command)
#define CMD_STATE_FLAGS(Cmnd)   ((Cmnd)->SCp.phase)


/*
 * SCSI definitions not defined in Linux's scsi.h
 */

/* The SCSISTAT values are defined in scsi.h,
 * but the values are shifted by one bit.
 * We re-define them here without bit shifting
 * to minimize confusion */
#define SCSISTAT_GOOD			0x00
#define SCSISTAT_CHECK_CONDITION	0x02
#define SCSISTAT_CONDITION_GOOD		0x04
#define SCSISTAT_BUSY			0x08
#define SCSISTAT_INTERMEDIATE_GOOD  	0x10
#define SCSISTAT_INTERMEDIATE_C_GOOD  	0x14
#define SCSISTAT_RESERVATION_CONFLICT 	0x18
#define SCSISTAT_COMMAND_TERMINATED   	0x22
#define SCSISTAT_QUEUE_FULL           	0x28


/* SAM-II compliant lun structure */
typedef struct {
	uint8_t bus_identifier:6;
	uint8_t address_method:2;

	uint8_t single_level_lun;
	uint16_t second_level_lun;
	uint16_t third_level_lun;
	uint16_t fourth_level_lun;
} single_level_lun_t;

typedef struct {
	uint32_t lun_list_length;
	uint8_t reserved[4];
	single_level_lun_t lun[MAX_LUNS];
} report_luns_t;




/*
 * Declarations for load module
 *
 * Scsi_Host_template (see drivers/scsi/hosts.h)
 * Device driver Interfaces to mid-level SCSI driver.
 */

#if 0

//FIXME: Add above, then test
TEMPLATE_HIGHMEM_IO                                             \
TEMPLATE_CAN_DMA_32                                             \
TEMPLATE_SINGLE_SG_OK                                           \
TEMPLATE_CAN_DO_VARYIO                                          \
TEMPLATE_VARY_IO                                                \

#endif

#endif  /* _QL4_OS_H_ */

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */


