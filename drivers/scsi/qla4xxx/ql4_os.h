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


