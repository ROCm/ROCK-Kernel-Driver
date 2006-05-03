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

#ifndef __QL4_DEF_H
#define __QL4_DEF_H

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dmapool.h>
#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <asm/semaphore.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

/* XXX(dg): move to pci_ids.h */
#ifndef PCI_DEVICE_ID_QLOGIC_ISP4010
#define PCI_DEVICE_ID_QLOGIC_ISP4010	0x4010
#endif

#ifndef PCI_DEVICE_ID_QLOGIC_ISP4022
#define PCI_DEVICE_ID_QLOGIC_ISP4022	0x4022
#endif

#define IS_QLA4010(ha)	((ha)->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP4010)
#define IS_QLA4022(ha)	((ha)->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP4022)

#if defined(CONFIG_COMPAT) && !defined(CONFIG_IA64)
#define QLA_CONFIG_COMPAT
#endif

/*
 * This file set some defines that are required to compile the
 * command source for 4000 module
 *----------------------------------------------------------------------------*/
#define QLA4010
#define QLA4XXX_BOARD_PORTS		1
#define QLA4XXX_PROC_NAME		"qla4010"

#define MEMORY_MAPPED_IO		1 /* 1=Memory Mapped (preferred),
					   * 0=I/O Mapped */

#define LINESIZE		256
#define MIN(x,y)		((x)<(y)?(x):(y))
#define MAX(x,y)		((x)>(y)?(x):(y))

/*
 * Return status codes for internal routines
 ********************************************/
#define QLA_SUCCESS			0
#define QLA_ERROR			1

/*
 * Data bit definitions
 */
#define BIT_0	0x1
#define BIT_1	0x2
#define BIT_2	0x4
#define BIT_3	0x8
#define BIT_4	0x10
#define BIT_5	0x20
#define BIT_6	0x40
#define BIT_7	0x80
#define BIT_8	0x100
#define BIT_9	0x200
#define BIT_10	0x400
#define BIT_11	0x800
#define BIT_12	0x1000
#define BIT_13	0x2000
#define BIT_14	0x4000
#define BIT_15	0x8000
#define BIT_16	0x10000
#define BIT_17	0x20000
#define BIT_18	0x40000
#define BIT_19	0x80000
#define BIT_20	0x100000
#define BIT_21	0x200000
#define BIT_22	0x400000
#define BIT_23	0x800000
#define BIT_24	0x1000000
#define BIT_25	0x2000000
#define BIT_26	0x4000000
#define BIT_27	0x8000000
#define BIT_28	0x10000000
#define BIT_29	0x20000000
#define BIT_30	0x40000000
#define BIT_31	0x80000000

/*
 * Host adapter default definitions
 ***********************************/
#define MAX_HBAS			16
#define MAX_BUSES       		1
#define MAX_TARGETS     		MAX_PRST_DEV_DB_ENTRIES + MAX_DEV_DB_ENTRIES
#define MAX_LUNS        		256
#define MAX_AEN_ENTRIES 		256 /* should be > EXT_DEF_MAX_AEN_QUEUE */
#define MAX_DDB_ENTRIES 		MAX_PRST_DEV_DB_ENTRIES + MAX_DEV_DB_ENTRIES
#define MAX_PDU_ENTRIES                 32
#define INVALID_ENTRY			0xFFFF
#define MAX_CMDS_TO_RISC		1024
#define MAX_SRBS			MAX_CMDS_TO_RISC
#define MBOX_AEN_REG_COUNT		5
#define MAX_INIT_RETRIES		5
#define IOCB_HIWAT_CUSHION		16

/*
 * Buffer sizes
 ***************/
#define REQUEST_QUEUE_DEPTH       	MAX_CMDS_TO_RISC
#define RESPONSE_QUEUE_DEPTH      	64
#define QUEUE_SIZE			64
#define DMA_BUFFER_SIZE 		512

/*
 * Misc
 *******/
#define MAC_ADDR_LEN			6 /* in bytes */
#define IP_ADDR_LEN			4 /* in bytes */
#define DRIVER_NAME			"qla4xxx"

#define MAX_LINKED_CMDS_PER_LUN		3
#define MAX_REQS_SERVICED_PER_INTR	16


/* Number of seconds to subtract for internal command timer */
#define QLA_CMD_TIMER_DELTA             5


#define	ISCSI_IPADDR_SIZE		4	/* IP address size */
#define	ISCSI_ALIAS_SIZE		32	/* ISCSI Alais name size */
#define	ISCSI_NAME_SIZE			255	/* ISCSI Name size  - usually a string */

#define SYS_DELAY(x)		do {udelay(x);barrier();} while(0);
#define QLA4XXX_DELAY(sec)  	do {mdelay(sec * 1000);} while(0);
#define NVRAM_DELAY() 		do {udelay(500);} while(0); /* 500 microsecond delay */

/* delay 30 seconds */
#define RESET_DELAY()		do {int delay; for(delay=30; delay!=0; delay--) \
				{current->state = TASK_UNINTERRUPTIBLE; \
				schedule_timeout(1 * HZ);}} while(0);

#define TOPCAT_RESET_DELAY()	do {udelay(1);} while(0);
#define TOPCAT_POST_RESET_DELAY() do {udelay(523);} while(0);


#define LSB(x)	((uint8_t)(x))
#define MSB(x)	((uint8_t)((uint16_t)(x) >> 8))
#define LSW(x)	((uint16_t)(x))
#define MSW(x)	((uint16_t)((uint32_t)(x) >> 16))
#define LSDW(x)	((uint32_t)((uint64_t)(x)))
#define MSDW(x)	((uint32_t)((((uint64_t)(x)) >> 16) >> 16))

#define IPAddrIsZero( _X1_ )   ((_X1_)[0] == 0 && \
                                (_X1_)[1] == 0 && \
                                (_X1_)[2] == 0 && \
                                (_X1_)[3] == 0)

#define IPAddrIsEqual(_X1_, _X2_) ((_X1_)[0] == (_X2_)[0] && \
                                   (_X1_)[1] == (_X2_)[1] && \
                                   (_X1_)[2] == (_X2_)[2] && \
                                   (_X1_)[3] == (_X2_)[3])

#define IPAddr2Uint32(_X1_,_X2_) { \
                                  *_X2_ = 0; \
				  *_X2_ |= _X1_[3] << 24; \
				  *_X2_ |= _X1_[2] << 16; \
				  *_X2_ |= _X1_[1] << 8;  \
				  *_X2_ |= _X1_[0];}

/*
 * I/O port access macros
 *************************/
#if MEMORY_MAPPED_IO
#   define RD_REG_BYTE(addr)	     readb(addr)
#   define RD_REG_WORD(addr)         readw(addr)
#   define RD_REG_DWORD(addr)        readl(addr)
#   define RD_REG_WORD_RELAXED(addr) 	readw_relaxed(addr)
#   define RD_REG_DWORD_RELAXED(addr)	readl_relaxed(addr)
#   define WRT_REG_BYTE(addr, data)  writeb(data, addr)
#   define WRT_REG_WORD(addr, data)  writew(data, addr)
#   define WRT_REG_DWORD(addr, data) writel(data, addr)
#else
#   define RD_REG_BYTE(addr)	     (inb((u_long)addr))
#   define RD_REG_WORD(addr)         (inw((u_long)addr))
#   define RD_REG_DWORD(addr)        (inl((u_long)addr))
#   define WRT_REG_BYTE(addr, data)  (outb(data,(u_long)addr))
#   define WRT_REG_WORD(addr, data)  (outw((data),(u_long)addr))
#   define WRT_REG_DWORD(addr, data) (outl((data),(u_long)addr))
#endif

#define PCI_POSTING(a) (RD_REG_DWORD(a))

#include "ql4_os.h"
#include "ql4_fw.h"
#include "ql4_nvram.h"

/*---------------------------------------------------------------------------*/

/*
 * Retry & Timeout Values
 *************************/
#define MBOX_TOV			60
#define SOFT_RESET_TOV			30
#define RESET_INTR_TOV			3
#define SEMAPHORE_TOV			10
#define ADAPTER_INIT_TOV		120
#define ADAPTER_RESET_TOV		180
#define INTERNAL_PASSTHRU__TOV		60
#define EXTEND_CMD_TOV			60
#define WAIT_CMD_TOV			30
#define EH_WAIT_CMD_TOV			120
#define FIRMWARE_UP_TOV			60
#define RESET_FIRMWARE_TOV        	30
#define LOGOUT_TOV			10
#define IOCB_TOV_MARGIN			10
#define RELOGIN_TOV			18
#define ISNS_DEREG_TOV			5
#define MIN_CMD_TOV			25

#define MAX_RESET_HA_RETRIES		2

/*---------------------------------------------------------------------------*/
/*
 * SCSI Request Block structure  (srb)  that is placed
 * on cmd->SCp location of every I/O     [We have 22 bytes available]
 */
typedef struct _srb_t {
	struct list_head   list_entry;		/* (8)   */
	struct scsi_qla_host *ha;	/* (4) HA the SP is queued on */

	uint16_t     flags;		/* (2) Status flags. */
	#define SRB_DMA_VALID		BIT_3	/* DMA Buffer mapped. */

	#define SRB_GOT_SENSE		BIT_4	/* sense data recieved. */
	#define SRB_IOCTL_CMD		BIT_5	/* generated from an IOCTL. */
	#define SRB_BUSY		BIT_7	/* in busy retry state. */

	#define SRB_RETRY		BIT_9	/* needs retrying. */
	#define SRB_TAPE		BIT_10	/* FCP2 (Tape) command. */


	uint8_t     state;		/* (1) Status flags. */
	#define SRB_NO_QUEUE_STATE	 0	/* Request is in between states */
	#define SRB_FREE_STATE		 1
	#define SRB_PENDING_STATE	 2
	#define SRB_ACTIVE_STATE	 3
	#define SRB_ACTIVE_TIMEOUT_STATE 4
	#define SRB_RETRY_STATE	 	 5
	#define SRB_DONE_STATE	 	 6
	#define SRB_SUSPENDED_STATE  	 7    /* Request in suspended state */

	#define SRB_STATE_TBL()	  	  \
	{	    			  \
            "NO_QUEUE"	        	, \
            "FREE"		        , \
            "PENDING"	        	, \
	    "ACTIVE"	        	, \
	    "ACTIVE_TIMEOUT"        	, \
	    "RETRY"	        	, \
	    "DONE"	        	, \
	    "SUSPENDED"	        	, \
	    NULL			  \
	}

	uint8_t     entry_count;		/* (1) number of request queue
						 *     entries used */
	struct scsi_cmnd  *cmd;			/* (4) SCSI command block */
	dma_addr_t  saved_dma_handle;		/* (4) for unmap of single transfers */
	atomic_t    ref_count;		/* (4) reference count for this srb */
	uint32_t    fw_ddb_index;	/* (4) */

	/* Target/LUN queue pointers. */
	struct os_tgt *tgt_queue;	/* (4) ptr to visible ha's target */
	struct os_lun *lun_queue;	/* (4) ptr to visible ha's lun */
	struct fc_lun *fclun;		/* (4) FC LUN context pointer. */
	
	/* Raw completion info for use by failover ? */
	uint8_t rsvd1;
	uint8_t err_id;			/* (1) error id */
	#define SRB_ERR_PORT       1    /* Request failed because "port down" */
	#define SRB_ERR_LOOP       2    /* Request failed because "loop down" */
	#define SRB_ERR_DEVICE     3    /* Request failed because "device error" */
	#define SRB_ERR_OTHER      4

	uint16_t    rsvd2;		/* (2) */
	uint32_t    lun;		/* (4) */
	struct timer_list timer;	/* (32) used to timeout command */
	uint16_t    os_tov;		/* (2) */
	uint16_t    iocb_tov;           /* (2) */
	uint16_t    iocb_cnt;		/* (2) Number of used iocbs */
	uint16_t    cc_stat;            /* (2) */
	u_long      r_start;	      	/* (4) Time we recieve a cmd from OS*/
	u_long      u_start;	      	/* (4) Time when we handed the cmd to F/W */
} srb_t;

/*
 * SCSI Target Queue structure
 */
typedef struct os_tgt {
	struct os_lun		*olun[MAX_LUNS];	 /* LUN context pointer. */
	struct scsi_qla_host	*ha;
	uint32_t		down_timer;
	struct fc_port		*fcport;		/* Current fcport for this target */
	unsigned long		flags;
	uint8_t			port_down_retry_count;
	uint8_t			id;

	/* Persistent binding information */
	uint16_t		ddb_index;
	uint8_t			iscsi_name[ISCSI_NAME_SIZE];
	uint8_t			*name;
} os_tgt_t;

/*
 * SCSI Target Queue flags
 */
#define TQF_ONLINE		0		/* Device online to OS. */
#define TQF_SUSPENDED		1
#define TQF_RETRY_CMDS		2

/*
 * LUN structure
 */
typedef struct os_lun {
	struct fc_lun *fclun;		/* (4) FC LUN context pointer. */
	struct list_head list_entry;	/* (8) For suspended lun list */
	struct scsi_device *sdev;	/* (4) */
	
	spinlock_t lun_lock  ____cacheline_aligned; /* (4) For suspended lun list */
	unsigned long flags;		/* 4 */
	#define LF_LUN_DELAYED		0
	#define LF_LUN_SUSPEND		1
	#define LF_LUN_BLOCKED		2
	#define LUN_MPIO_RESET_CNTS	3	/* Lun */

	uint8_t lun_state;		/* (1) */
	#define LS_LUN_READY		0   /* LUN is ready to accept commands */
	#define LS_LUN_SUSPENDED	1   /* LUN is suspended */
	#define LS_LUN_RETRY		2   /* LUN is retrying commands */
	#define LS_LUN_TIMEOUT		3   /*  */
	#define LUN_STATE_TBL()		  \
	{				  \
		"READY"			, \
		"SUSPENDED"		, \
		"RETRY"			, \
		"TIMEOUT"		, \
		NULL			  \
	}

	uint8_t out_count;		/* (1) Number of outstanding commands */
	uint8_t lun;			/* (1) Lun number */

	uint8_t retry_count;		/* (1) Number of times lun is suspended */
	uint8_t max_retry_count;	/* (1) Max number of times lun can be */
					/*        suspended before returning commands */
	uint8_t reserved[3];		/* (3) */
	uint32_t tot_io_count;		/* (4) Total num outstanding I/Os */
	atomic_t suspend_timer;		/* (4) Timer for suspending lun */
} os_lun_t;

/* Never set this to Zero */
#define SUSPEND_SECONDS	6		
#define SUSPEND_RETRIES	1

/* LUN BitMask structure definition, array of 32bit words,
 * 1 bit per lun.  When bit == 1, the lun is masked.
 * Most significant bit of mask[0] is lun 0, bit 24 is lun 7.
 */
typedef struct lun_bit_mask {
	/* Must allocate at least enough bits to accomodate all LUNs */
#if ((MAX_LUNS & 0x7) == 0)
	UINT8   mask[MAX_LUNS >> 3];
#else
	uint8_t mask[(MAX_LUNS + 8) >> 3];
#endif
} lun_bit_mask_t;

/*---------------------------------------------------------------------------*/

/*
 * Device Database (DDB) structure
 */

typedef struct ddb_entry {
	struct list_head   list_entry;	/* 00 x00 */
	uint16_t bus;			/* 08 x08 SCSI bus number */
	uint16_t target;		/* 10 x0a SCSI target ID */
	struct fc_port          *fcport;
	uint16_t fw_ddb_index;		/* 12 x0c DDB index from firmware's DEV_DB structure */
	uint16_t out_count;		/* 14 x0e Number of active commands */

	uint8_t  num_valid_luns;	/* 16 x10 Number of valid luns */
	uint8_t  reserved[3];		/* 17 x11 */

	/* refer to MBOX_CMD_GET_DATABASE_ENTRY for fw_ddb_fw_ddb_device_state definitions   */
	uint32_t fw_ddb_device_state;		/* 20 x14 Device State */
	#define DDB_STATE_TBL(){	\
                "UNASSIGNED",           \
		"NO_CONNECTION_ACTIVE", \
		"DISCOVERY",            \
		"NO_SESSION_ACTIVE",    \
		"SESSION_ACTIVE",       \
		"LOGGING_OUT",          \
		"SESSION_FAILED",       \
		NULL                    \
	}
	uint32_t CmdSn;			/* 24 x18 */
	uint16_t target_session_id;	/* 28 x1c */
	uint16_t connection_id;		/* 30 x1e */
	uint16_t exe_throttle;		/* 32 x20 Max mumber of cmds outstanding simultaneously */
	uint16_t task_mgmt_timeout;		/* 34 x22 Min time for task mgmt cmds to complete */
	uint16_t default_relogin_timeout; /* 36 x24 Max time to wait for relogin to complete */
	uint16_t tcp_source_port_num;		/* 38 x26 */
	uint32_t default_time2wait;		/* 40 x28 Default Min time between relogins (+aens) */
	atomic_t port_down_timer;		/* 44 x2c Device down time */
	atomic_t retry_relogin_timer;		/* 48 x30 Min Time between relogins (4000 only)*/
	atomic_t relogin_timer;		/* 52 x34 Max Time to wait for relogin to complete */
	atomic_t relogin_retry_count;		/* 56 x38 Num of times relogin has been retried */
	atomic_t state;			/* 60 x3c Device State*/
	#define DEV_STATE_DEAD		0 /* We can no longer talk to this device */
	#define DEV_STATE_ONLINE	1 /* Device ready to accept commands */
	#define DEV_STATE_MISSING	2 /* Device logged off, trying to re-login */
	#define DEV_STATE_TBL(){	  \
		"DEAD"			, \
		"ONLINE"		, \
		"MISSING"		, \
		NULL			  \
	}
	unsigned long flags;			/* 64 x40 */
	#define DF_RELOGIN		0  /* Relogin to device */
	#define DF_NO_RELOGIN		1  /* Do not relogin if IOCTL logged it out */
	#define DF_ISNS_DISCOVERED	2  /* Device was discovered via iSNS */

	unsigned long dev_scan_wait_to_start_relogin;
	unsigned long dev_scan_wait_to_complete_relogin;

	uint8_t  ip_addr[ISCSI_IPADDR_SIZE];
	uint8_t  iscsi_name[ISCSI_NAME_SIZE];	 /* 72 x48 */
} ddb_entry_t;				 /*840 x348 */

/*
 * Fibre channel port type.
 */
typedef enum {
	FCT_UNKNOWN,
	FCT_RSCN,
	FCT_SWITCH,
	FCT_BROADCAST,
	FCT_INITIATOR,
	FCT_TARGET
} fc_port_type_t;

/*
 * Fibre channel port structure.
 */
typedef struct fc_port {
	struct list_head list;
	struct list_head fcluns;

	struct scsi_qla_host *ha;
	struct scsi_qla_host *vis_ha;	/* only used when suspending lun */
	ddb_entry_t     *ddbptr;

	uint8_t  *iscsi_name;
	fc_port_type_t port_type;

	atomic_t state;
	uint32_t flags;

	os_tgt_t *tgt_queue;
	uint16_t os_target_id;
	uint8_t device_type;
	uint8_t unused;

	uint8_t mp_byte;		/* multi-path byte (not used) */
	uint8_t cur_path;		/* current path id */

	lun_bit_mask_t lun_mask;
	int loop_id;
} fc_port_t;


/*
 * Fibre channel port/lun states.
 */
#define FCS_UNCONFIGURED	1
#define FCS_DEVICE_DEAD		2
#define FCS_DEVICE_LOST		3
#define FCS_ONLINE		4
#define FCS_NOT_SUPPORTED	5
#define FCS_FAILOVER		6
#define FCS_FAILOVER_FAILED	7

/*
 * FC port flags.
 */
#define FCF_FABRIC_DEVICE	BIT_0
#define	FCF_INITIATOR_DEVICE	BIT_1
#define FCF_FO_MASKED		BIT_2
#define FCF_FAILOVER_NEEDED	BIT_3
#define FCF_RESET_NEEDED	BIT_4
#define FCF_PERSISTENT_BOUND	BIT_5
#define FCF_TAPE_PRESENT	BIT_6
#define FCF_XP_DEVICE            BIT_7
#define FCF_CONFIG_DEVICE        BIT_8
#define FCF_MSA_DEVICE            BIT_9
#define FCF_MSA_PORT_ACTIVE     BIT_10
#define FCF_LOGIN_NEEDED		BIT_12
#define FCF_EVA_DEVICE            BIT_13

#define FCF_RLC_SUPPORT		BIT_14
#define FCF_CONFIG		BIT_15	/* Needed? */
#define FCF_RESCAN_NEEDED	BIT_16
#define FCF_FAILBACK_DISABLE	BIT_17
#define FCF_FAILOVER_DISABLE	BIT_18

#define	FCF_VSA			BIT_19
#define	FCF_HD_DEVICE		BIT_20
#define FCF_NONFO_DEVICE	BIT_21	/* Non Failover device */


/*
 * Fibre channel LUN structure.
 */
typedef struct fc_lun {
	struct list_head list;

	fc_port_t *fcport;
	atomic_t state;
	uint16_t lun;
	uint8_t device_type;
	uint8_t flags;
	#define	FLF_VISIBLE_LUN		BIT_0
	#define	FLF_ACTIVE_LUN		BIT_1

	uint8_t lun_state;		/* 00 x00 */
	uint8_t resvd[3];
	#define LS_LUN_RESET_MARKER_NEEDED 4   /* LUN Reset marker needed */

} fc_lun_t, lun_entry_t;


/*---------------------------------------------------------------------------*/

/*
 * Asynchronous Event Queue structure
 */
typedef struct {
	uint32_t mbox_sts[MBOX_AEN_REG_COUNT];
}aen_t;


/*
 * NOTE: This structure definition really belongs in the ql4isns.h file,
 *       but it's easier to compile when the structure is defined here.
 */
typedef struct _ATTRIBUTE_LIST {
	uint32_t isns_tag;
	#define ISNS_ATTR_TYPE_EMPTY      1   /* Used for delimiter attr. & operating attr. for query. */
	#define ISNS_ATTR_TYPE_STRING     2   /* UTF-8 encoded string */
	#define ISNS_ATTR_TYPE_ULONG      3
	#define ISNS_ATTR_TYPE_ADDRESS    4   /* 128-bit IPv6 */
	uint32_t type;
	unsigned long data;
} ATTRIBUTE_LIST;

/*
 * Linux Host Adapter structure
 */
typedef struct scsi_qla_host {
	struct list_head list;

	/* Linux adapter configuration data */
	struct Scsi_Host *host;		    /* pointer to host data */
	struct scsi_qla_host *next;

	uint32_t        tot_ddbs;

	unsigned long   flags;
	#define AF_ONLINE		      0 /* 0x00000001 */
	#define AF_INIT_DONE		      1 /* 0x00000002 */
	#define AF_MBOX_COMMAND 	      2 /* 0x00000004 */
	#define AF_MBOX_COMMAND_DONE 	      3 /* 0x00000008 */
	#define AF_DPC_SCHEDULED	      5 /* 0x00000020 */
	#define AF_INTERRUPTS_ON	      6 /* 0x00000040 Not Used */
	#define AF_GET_CRASH_RECORD	      7 /* 0x00000080 */
	#define AF_LINK_UP		      8 /* 0x00000100 */
	#define AF_TOPCAT_CHIP_PRESENT	      9 /* 0x00000200 */
	#define AF_IRQ_ATTACHED	      	     10 /* 0x00000400 */
	#define AF_64BIT_PCI_ADDR	     11 /* 0x00000800 */
	#define AF_MSI_ENABLED	     	     13 /* 0x00002000 */

	unsigned long   dpc_flags;
	#define DPC_RESET_HA		      1 /* 0x00000002 */
	#define DPC_RETRY_RESET_HA	      2 /* 0x00000004 */
	#define DPC_RELOGIN_DEVICE	      3 /* 0x00000008 */
	#define DPC_RESET_HA_DESTROY_DDB_LIST 4 /* 0x00000010 */
	#define DPC_RESET_HA_INTR	      5 /* 0x00000020 */
	#define DPC_IOCTL_ERROR_RECOVERY      6 /* 0x00000040 */
	#define DPC_ISNS_RESTART	      7 /* 0x00000080 */
	#define DPC_ISNS_RESTART_COMPLETION   8 /* 0x00000100 */
	#define DPC_AEN			      9 /* 0x00000200 */

	#define DPC_WAIT_TO_RELOGIN_DEVICE   13 /* 0x00002000 */
	#define DPC_CHECK_LUN		     14 /* 0x00004000 */
	#define DPC_GET_DHCP_IP_ADDR	     15 /* 0x00008000 */
	#define DPC_RESET_NIC_TEST	     17 /* 0x00020000 */

	uint16_t        iocb_cnt;
	uint16_t        iocb_hiwat;
	uint16_t        req_q_count;	/* Number of available request queue entries. */
	uint16_t        resvd;

	u_long          i_start;	/* jiffies at start of IOCTL */
	u_long          i_end;		/* jiffies at end of IOCTL */
	u_long          f_start;	/* jiffies at sending cmd to f/w */
	u_long          f_end;		/* jiffies at receiving cmd from f/w */

	/* pci information */
	struct pci_dev  *pdev;
	struct qla_board_info *brd_info;
	unsigned long   pci_resource_flags;

	uint8_t         marker_needed;
	uint8_t         rsvd1;

	/* adapter instance w.r.t. all scsi hosts in OS */
	uint16_t        host_no;

	/* adapter instance w.r.t. this driver */
	uint16_t        instance;

	uint16_t        resvd2;

	void            *virt_mmapbase;

	uint32_t        function_number;

	/* ISP registers, Base Memory-mapped I/O address */
	isp_reg_t       *reg;

	/* temp only */
	unsigned long io_addr;
	unsigned long mem_addr;
	unsigned long io_len;
	unsigned long mem_len;
	unsigned int irq;		 /* IRQ for adapter            */

	uint8_t		driver_verstr[80];
	uint8_t		driver_version[4];

	/* NVRAM registers */
	eeprom_data_t     *nvram;

	/* Counters for general statistics */
	uint64_t        adapter_error_count;
	uint64_t        device_error_count;
	uint64_t        total_io_count;
	uint64_t        total_mbytes_xferred;
	uint64_t        isr_count;	    /* Interrupt count */
	uint64_t        link_failure_count;
	uint64_t        invalid_crc_count;

	uint32_t        spurious_int_count;
	uint32_t        aborted_io_count;
	uint32_t        io_timeout_count;
	uint32_t        mailbox_timeout_count;
	uint32_t        seconds_since_last_intr;
	uint32_t        seconds_since_last_heartbeat;
	uint32_t        mac_index;

	/* Info Initialized at init time */
	/* --- From GetFwVersion --- */
	uint32_t        firmware_version[2];
	uint32_t        patch_number;
	uint32_t        build_number;
	/* --- From Init_FW --- */
	uint16_t        firmware_options;
	uint16_t        tcp_options;
	uint8_t         ip_address[IP_ADDR_LEN];
	uint8_t         subnet_mask[IP_ADDR_LEN];
	uint8_t         gateway[IP_ADDR_LEN];
	uint8_t         isns_ip_address[IP_ADDR_LEN];
	uint16_t        isns_server_port_number;
	uint16_t        resvd7;
	uint8_t         alias[32];
	uint8_t         name_string[256];
	uint8_t         heartbeat_interval;
	uint8_t         rsvd;
	/* --- From FlashSysInfo --- */
	uint8_t         my_mac[MAC_ADDR_LEN];
	uint8_t         serial_number[16];
	/* --- From GetFwState --- */
	uint32_t        firmware_state;
	uint32_t        board_id;
	uint32_t        addl_fw_state;

	/* iSNS information */
	unsigned long   isns_flags;
	#define ISNS_FLAG_ISNS_ENABLED_IN_ISP   0  /* 0x00000001 */
	#define ISNS_FLAG_ISNS_SRV_ENABLED   	1  /* 0x00000002 */
	#define ISNS_FLAG_ISNS_SRV_REGISTERED   2  /* 0x00000004 */
	#define ISNS_FLAG_ISNS_SCN_REGISTERED   4  /* 0x00000010 */
	#define ISNS_FLAG_QUERY_SINGLE_OBJECT   5  /* 0x00000020 */
	#define ISNS_FLAG_SCN_IN_PROGRESS       6  /* 0x00000040 */
	#define ISNS_FLAG_SCN_RESTART           7  /* 0x00000080 */
	#define ISNS_FLAG_DEV_SCAN_DONE         27 /* 0x08000000 */
	#define ISNS_FLAG_REREGISTER            28 /* 0x10000000 */
	#define ISNS_FLAG_RESTART_SERVICE       31 /* 0x80000000 */

	uint16_t        isns_connection_id;
	uint16_t        isns_scn_conn_id;
	uint16_t        isns_esi_conn_id;
	uint16_t        isns_nsh_conn_id;
	uint16_t        isns_remote_port_num;
	uint16_t        isns_scn_port_num;
	uint16_t        isns_esi_port_num;
	uint16_t        isns_nsh_port_num;
	uint8_t         isns_entity_id[256];

	atomic_t        isns_restart_timer;
	uint16_t        isns_transaction_id;
	uint16_t        isns_num_discovered_targets;

	ATTRIBUTE_LIST  isns_reg_attr_list[13];
	ATTRIBUTE_LIST  isns_dereg_attr_list[7];
	ATTRIBUTE_LIST  isns_scn_reg_attr_list[5];
	ATTRIBUTE_LIST  isns_scn_dereg_attr_list[3];
	ATTRIBUTE_LIST  isns_dev_get_next_attr_list[5];
	ATTRIBUTE_LIST  isns_dev_attr_qry_attr_list[13];

	/* Linux kernel thread */
	pid_t                   dpc_pid;
	int                     dpc_should_die;
	struct completion       dpc_inited;
	struct completion       dpc_exited;
	struct semaphore        *dpc_wait;
	uint8_t dpc_active;		     /* DPC routine is active */
	uint8_t resvd3[3];

	/* Linux timer thread */
	struct timer_list timer;
	uint32_t        timer_active;

	/* Recovery Timers */
	uint32_t        port_down_retry_count;
	uint32_t        discovery_wait;
	atomic_t        check_relogin_timeouts;
	uint32_t        retry_reset_ha_cnt;
	uint32_t        isp_reset_timer;	 /* reset test timer */
	uint32_t        nic_reset_timer;	 /* simulated nic reset test timer */
	uint32_t        eh_display_timer;	 /* simulated nic reset test timer */

	int             eh_start;		/* To wake up the mid layer error
						 * handler thread */

	/* This spinlock must be held with irqs disabled in order to access
	 * the pending, retry and free srb queues.
	 *
	 * The list_lock spinlock is of lower priority than the io_request
	 * lock.
	 *-------------------------------------------------------------------*/
	spinlock_t      list_lock  ____cacheline_aligned;

	/* internal srb queues */
	struct          list_head retry_srb_q;	      /* retry queue request list */
	uint32_t        retry_srb_q_count;

	struct          list_head free_srb_q;
	uint16_t        free_srb_q_count;
	uint16_t        num_srbs_allocated;

	/* Done queue
	 * In order to avoid deadlocks with the list_lock,
	 * place all srbs to be returned to OS on this list.
	 * After the list_lock is released, return all of
	 * these commands to the OS */

	struct list_head done_srb_q;
	uint32_t         done_srb_q_count;

	/* This spinlock is used to protect "io transactions", you must	
	 * aquire it before doing any IO to the card, eg with RD_REG*() and
	 * WRT_REG*() for the duration of your entire command transaction.
	 * It is also used to protect the active_srb_array.
	 *
	 * The hardware_lock spinlock is of lower priority than the
	 * io request lock.
	 *-------------------------------------------------------------------*/
	spinlock_t         hardware_lock  ____cacheline_aligned;

	/* Active array */
	srb_t           *active_srb_array[MAX_SRBS];
	uint16_t        active_srb_count;
	uint16_t        current_active_index;

	int             mem_err;

	/* DMA Memory Block */
	void            *queues;
	dma_addr_t      queues_dma;
	unsigned long   queues_len;
#define MEM_ALIGN_VALUE	\
	((MAX(REQUEST_QUEUE_DEPTH, RESPONSE_QUEUE_DEPTH)) * \
	 sizeof(QUEUE_ENTRY))

	/* request and response queue variables */
	dma_addr_t      request_dma;
	QUEUE_ENTRY     *request_ring;
	QUEUE_ENTRY     *request_ptr;

	dma_addr_t      response_dma;
	QUEUE_ENTRY     *response_ring;
	QUEUE_ENTRY     *response_ptr;

	dma_addr_t      shadow_regs_dma;
	shadow_regs_t   *shadow_regs;

	uint16_t        request_in;		/* Current indexes. */
	uint16_t        request_out;
	uint16_t        response_in;
	uint16_t        response_out;

	/* aen queue variables */
	uint32_t	aen_reg_mask;
	uint16_t        aen_report;	/* Number of aens reported to SDMAPI */
	uint16_t        aen_count;	/* Total number of aens received */
	uint16_t        aen_in;		/* Current indexes */
	uint16_t        aen_out;
	aen_t           aen_q[MAX_AEN_ENTRIES];

	/* pdu variables */
	uint16_t        pdu_count;		/* Number of available aen_q entries */
	uint16_t        pdu_in;		/* Current indexes */
	uint16_t        pdu_out;

	uint16_t        pdu_active;
	PDU_ENTRY       *free_pdu_top;
	PDU_ENTRY       *free_pdu_bottom;
	PDU_ENTRY       pdu_queue[MAX_PDU_ENTRIES];

	/* This semaphore protects several threads to do mailbox commands
	 * concurrently.
	 *-------------------------------------------------------------------*/
	struct semaphore  mbox_sem;
	wait_queue_head_t mailbox_wait_queue;

	/* temporary mailbox status registers */
	volatile uint8_t  mbox_status_count;
	uint8_t resvd5[3];
	volatile uint32_t mbox_status[MBOX_REG_COUNT];

	ISNS_DISCOVERED_TARGET *isns_disc_tgt_databasev;
	dma_addr_t      isns_disc_tgt_databasep;
	uint32_t        isns_disc_tgt_database_size;

	/* local device database list (contains internal ddb entries)*/
	struct list_head ddb_list;
	/* Fibre Channel Device List. */
	struct list_head        fcports;

	/* OS target queue pointers. */
	os_tgt_t        *otgt[MAX_TARGETS+1];

	/* Map ddb_list entry by FW ddb index */
	ddb_entry_t     *fw_ddb_index_map[MAX_DDB_ENTRIES];

	/*
	 * There are several Scsi_Host members that are RHEL3 specific
	 * yet depend on the SCSI_HAS_HOST_LOCK define for visibility.
	 * Unfortuantely, it seems several RH kernels have the define
	 * set, but do not have a host_lock member.
	 *
	 * Use the SH_HAS_HOST_LOCK define determined during driver
	 * compilation rather than SCSI_HAS_HOST_LOCK.
	 */

	/* Scsi midlayer lock */
	#if defined(SH_HAS_HOST_LOCK)
	spinlock_t      host_lock ____cacheline_aligned;
	#endif
}scsi_qla_host_t;

#define ADAPTER_UP(ha) ((test_bit(AF_ONLINE, &ha->flags) != 0) && (test_bit(AF_LINK_UP, &ha->flags) != 0))

typedef struct {
	uint8_t ha_mac[MAX_HBAS][MAC_ADDR_LEN];
} mac_cfgs_t;

/*
 * Other macros
 */
#define TGT_Q(ha, t) (ha->otgt[t])
#define LUN_Q(ha, t, l)	(TGT_Q(ha, t)->olun[l])
#define GET_LU_Q(ha, t, l) ((TGT_Q(ha,t) != NULL)? TGT_Q(ha, t)->olun[l] : NULL)

#define to_qla_host(x)		((scsi_qla_host_t *) (x)->hostdata)

#define ql4_printk(level, ha, format, arg...) \
	dev_printk(level , &((ha)->pdev->dev) , format , ## arg)


/*---------------------------------------------------------------------------*/

/* Defines for qla4xxx_initialize_adapter() and qla4xxx_recover_adapter() */
#define PRESERVE_DDB_LIST	0
#define REBUILD_DDB_LIST	1

/* Defines for process_aen() */
#define PROCESS_ALL_AENS	 0
#define FLUSH_DDB_CHANGED_AENS	 1
#define RELOGIN_DDB_CHANGED_AENS 2

/* Defines for qla4xxx_take_hw_semaphore */
#define NO_WAIT		0
#define WAIT_FOREVER	1
#define TIMED_WAIT	2


#ifndef __QIM_DEF_H__
#include "ql4_version.h"
#include "ql4_settings.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"
#include "ql4_listops.h"
#include "ql4_isns.h"
#endif

#endif /*_QLA4XXX_H */

/*
 * Overrides for Emacs so that we get a uniform tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
