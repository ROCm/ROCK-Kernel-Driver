/*
 * ipr.h -- driver for IBM Power Linux RAID adapters
 *
 * Written By: Brian King, IBM Corporation
 *
 * Copyright (C) 2003 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _IPR_H
#define _IPR_H

#include <asm/semaphore.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/completion.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#ifdef CONFIG_KDB
#include <asm/kdb.h>
#endif

/*
 * Literals
 */
#define IPR_DRIVER_VERSION "2.0.0-1 (January 15, 2004)"

/*
 * IPR_DBG_TRACE: Setting this to 1 will turn on some general function tracing
 *			resulting in a bunch of extra debugging printks to the console
 *
 * IPR_DEBUG:	Setting this to 1 will turn on some error path tracing.
 *			Enables the ipr_trace macro.
 */
#ifdef IPR_DEBUG_ALL
#define IPR_DEBUG				1
#define IPR_DBG_TRACE			1
#else
#define IPR_DEBUG				0
#define IPR_DBG_TRACE			0
#endif

/*
 * IPR_MAX_CMD_PER_LUN: This defines the maximum number of outstanding
 *	ops per device for devices not running tagged command queuing.
 */
#define IPR_MAX_CMD_PER_LUN				6

/*
 * IPR_MAX_TAGGED_CMD_PER_DEV: This defines the maximum number of tagged
 *	ops per device.
 */
#define IPR_MAX_TAGGED_CMD_PER_DEV			64

/*
 * IPR_NUM_CMDS_PER_DEV_IN_VSET: This defines the maximum number of 
 *	ops per device in a disk array. It is used to compute the queue
 *	depth of a disk array device.
 */
#define IPR_NUM_CMDS_PER_DEV_IN_VSET		4

/*
 * IPR_NUM_BASE_CMD_BLKS: This defines the maximum number of 
 *	ops the mid-layer can send to the adapter.
 */
#define IPR_NUM_BASE_CMD_BLKS				100


#ifndef PCI_DEVICE_ID_IBM_GEMSTONE
#define PCI_DEVICE_ID_IBM_GEMSTONE		0xB166
#endif

#ifndef PCI_DEVICE_ID_IBM_SNIPE
#define PCI_DEVICE_ID_IBM_SNIPE		0x0180
#endif

#define IPR_SUBS_DEV_ID_2780	0x0264
#define IPR_SUBS_DEV_ID_5702	0x0266
#define IPR_SUBS_DEV_ID_5703	0x0278

#define IPR_NAME				"ipr"

/*
 * Return codes
 */
#define IPR_RC_JOB_CONTINUE		1
#define IPR_RC_JOB_RETURN		2

/*
 * Dump status values
 */
#define IPR_DUMP_STATUS_SUCCESS			0
#define IPR_DUMP_STATUS_QUAL_SUCCESS		2
#define IPR_DUMP_STATUS_FAILED			0xffffffff

/* xxx */
#ifndef pci_dma_error
#if defined(CONFIG_PPC64)
#define pci_dma_error(x) ((x) == ((dma_addr_t)-1))
#else
#define pci_dma_error(x) (0)
#endif
#endif

/*
 * IOASCs
 */
#define IPR_IOASC_RCV_DEV_BUS_MSG_RECEIVED	0x01430000
#define IPR_IOASC_SYNC_REQUIRED			0x023f0000
#define IPR_IOASC_NR_ACA_ACTIVE			0x02448530
#define IPR_IOASC_MED_DO_NOT_REALLOC		0x03110C00
#define IPR_IOASC_HW_SEL_TIMEOUT			0x04050000
#define IPR_IOASC_HW_DEV_BUS_STATUS			0x04448500
#define	IPR_IOASC_IOASC_MASK			0xFFFFFF00
#define	IPR_IOASC_SCSI_STATUS_MASK		0x000000FF
#define IPR_IOASC_IR_RESOURCE_HANDLE		0x05250000
#define IPR_IOASC_BUS_WAS_RESET			0x06290000
#define IPR_IOASC_BUS_WAS_RESET_BY_OTHER		0x06298000
#define IPR_IOASC_ABORTED_CMD_TERM_BY_HOST	0x0B5A0000

#define IPR_FIRST_DRIVER_IOASC			0x10000000
#define IPR_IOASC_IOA_WAS_RESET			0x10000001
#define IPR_IOASC_PCI_ACCESS_ERROR			0x10000002

#define IPR_NUM_LOG_HCAMS				2
#define IPR_NUM_CFG_CHG_HCAMS				4
#define IPR_NUM_HCAMS	(IPR_NUM_LOG_HCAMS + IPR_NUM_CFG_CHG_HCAMS)
#define IPR_MAX_NUM_TARGETS_PER_BUS			0x10
#define IPR_MAX_NUM_LUNS_PER_TARGET			256
#define IPR_MAX_NUM_VSET_LUNS_PER_TARGET	8
#define IPR_VSET_BUS					0xff
#define IPR_MAX_NUM_BUSES				4
#define IPR_MAX_BUS_TO_SCAN				IPR_MAX_NUM_BUSES

#define IPR_NUM_RESET_RELOAD_RETRIES		3

/* We need resources for HCAMS, IOA reset, IOA bringdown, and ERP */
#define IPR_NUM_INTERNAL_CMD_BLKS	(IPR_NUM_HCAMS + \
                                     ((IPR_NUM_RESET_RELOAD_RETRIES + 1) * 2) + 3)

#define IPR_NUM_IOCTL_CMD_BLKS			10

#define IPR_MAX_COMMANDS		IPR_NUM_BASE_CMD_BLKS
#define IPR_NUM_CMD_BLKS		(IPR_NUM_BASE_CMD_BLKS + \
						IPR_NUM_INTERNAL_CMD_BLKS +\
						IPR_NUM_IOCTL_CMD_BLKS)

#define IPR_MAX_PHYSICAL_DEVS				192

#define IPR_MAX_SGLIST					64
#define IPR_MAX_SECTORS					512
#define IPR_MAX_CDB_LEN					16

#define IPR_DEFAULT_BUS_WIDTH				16
#define IPR_DEFAULT_SCSI_RATE	((320 * 10) / (IPR_DEFAULT_BUS_WIDTH / 8))
#define IPR_SAFE_SCSI_RATE		((80 * 10) / (IPR_DEFAULT_BUS_WIDTH / 8))

#define IPR_IOA_RES_HANDLE				0xffffffff
#define IPR_IOA_RES_ADDR				0x00ffffff

#define IPR_MAX_WRITE_BUFFER_SIZE		(4 * 1024 * 1024)

/*
 * Adapter Commands
 */
#define IPR_QUERY_RESOURCE_STATE			0xC2
#define IPR_RESET_DEVICE				0xC3
#define	IPR_RESET_TYPE_SELECT				0x80
#define	IPR_LUN_RESET					0x40
#define	IPR_TARGET_RESET					0x20
#define	IPR_BUS_RESET					0x10
#define IPR_ID_HOST_RR_Q				0xC4
#define IPR_QUERY_IOA_CONFIG				0xC5
#define IPR_ABORT_TASK					0xC7
#define IPR_CANCEL_ALL_REQUESTS			0xCE
#define IPR_HOST_CONTROLLED_ASYNC			0xCF
#define	IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE	0x01
#define	IPR_HCAM_CDB_OP_CODE_LOG_DATA		0x02
#define IPR_EVAL_DEVICE_CAPABILTIES			0xE4
#define IPR_SKIP_READ					0xE8
#define IPR_SKIP_WRITE					0xEA
#define IPR_SET_SUPPORTED_DEVICES			0xFB
#define IPR_SET_DASD_TIMEOUTS				0xEC
#define IPR_IOA_SHUTDOWN				0xF7
#define IPR_RECLAIM_CACHE_STORE			0xF8
#define	IPR_START_STOP_START				0x01
#define	IPR_START_STOP_STOP				0x00
#define	IPR_WR_BUF_DOWNLOAD_AND_SAVE			0x05

/*
 * Timeouts
 */
#define IPR_TIMEOUT_MULTIPLIER		2
#define IPR_MAX_SECOND_RADIX_TIMEOUT	0x3fff
#define IPR_TIMEOUT_MINUTE_RADIX		0x4000
#define IPR_SHUTDOWN_TIMEOUT			(10 * 60 * HZ)
#define IPR_STOP_DEVICE_TIMEOUT		(10 * 60 * HZ)
#define IPR_ABBREV_SHUTDOWN_TIMEOUT		(10 * HZ)
#define IPR_DEVICE_RESET_TIMEOUT		(30 * HZ)
#define IPR_CANCEL_ALL_TIMEOUT		(30 * HZ)
#define IPR_ABORT_TASK_TIMEOUT		(30 * HZ)
#define IPR_INTERNAL_TIMEOUT			(30 * HZ)
#define IPR_INTERNAL_DEV_TIMEOUT		(2 * 60 * HZ)
#define IPR_RECLAIM_TIMEOUT			(10 * 60 * HZ)
#define IPR_WRITE_BUFFER_TIMEOUT		(10 * 60 * HZ)
#define IPR_START_STOP_TIMEOUT		IPR_SHUTDOWN_TIMEOUT
#define IPR_SET_SUP_DEVICE_TIMEOUT		(2 * 60 * HZ)
#define IPR_SET_DASD_TIMEOUTS_TIMEOUT	(2 * 60 * HZ)
#define IPR_REQUEST_SENSE_TIMEOUT		(30 * HZ)
#define IPR_EVALUATE_DEVICE_TIMEOUT		(2 * 60 * HZ)
#define IPR_OPERATIONAL_TIMEOUT		(5 * 60 * HZ)
#define IPR_WAIT_FOR_RESET_TIMEOUT		(2 * HZ)
#define IPR_CHECK_FOR_RESET_TIMEOUT		(HZ / 10)
#define IPR_WAIT_FOR_BIST_TIMEOUT		(2 * HZ)
#define IPR_DUMP_TIMEOUT			(30 * HZ) /* xxx too long? */

/*
 * SCSI Literals
 */
#define IPR_IS_DASD_DEVICE(std_inq_data) \
((((std_inq_data).peri_dev_type) == TYPE_DISK) && \
!((std_inq_data).removeable_medium))

#define IPR_IS_SES_DEVICE(std_inq_data) \
(((std_inq_data).peri_dev_type) == TYPE_ENCLOSURE)

#define IPR_VENDOR_ID_LEN			8
#define IPR_PROD_ID_LEN				16
#define IPR_SERIAL_NUM_LEN			8

#define IPR_MAX_NUM_SUPP_INQ_PAGES	8

/*
 * Hardware literals
 */
#define IPR_FMT2_MBX_ADDR_MASK				0x0fffffff
#define IPR_FMT2_MBX_BAR_SEL_MASK			0xf0000000
#define IPR_FMT2_MKR_BAR_SEL_SHIFT			28
#define IPR_GET_FMT2_BAR_SEL(mbx) \
(((mbx) & IPR_FMT2_MBX_BAR_SEL_MASK) >> IPR_FMT2_MKR_BAR_SEL_SHIFT)
#define IPR_SDT_FMT2_BAR0_SEL				0x0
#define IPR_SDT_FMT2_BAR1_SEL				0x1
#define IPR_SDT_FMT2_BAR2_SEL				0x2
#define IPR_SDT_FMT2_BAR3_SEL				0x3
#define IPR_SDT_FMT2_BAR4_SEL				0x4
#define IPR_SDT_FMT2_BAR5_SEL				0x5
#define IPR_SDT_FMT2_EXP_ROM_SEL			0x8
#define IPR_FMT2_SDT_READY_TO_USE			0xC4D4E3F2
#define IPR_DOORBELL					0x82800000

#define IPR_PCII_IOA_TRANS_TO_OPER			(0x80000000 >> 0)
#define IPR_PCII_IOARCB_XFER_FAILED			(0x80000000 >> 3)
#define IPR_PCII_IOA_UNIT_CHECKED			(0x80000000 >> 4)
#define IPR_PCII_NO_HOST_RRQ				(0x80000000 >> 5)
#define IPR_PCII_CRITICAL_OPERATION			(0x80000000 >> 6)
#define IPR_PCII_IO_DEBUG_ACKNOWLEDGE		(0x80000000 >> 7)
#define IPR_PCII_IOARRIN_LOST				(0x80000000 >> 27)
#define IPR_PCII_MMIO_ERROR				(0x80000000 >> 28)
#define IPR_PCII_PROC_ERR_STATE			(0x80000000 >> 29)
#define IPR_PCII_HRRQ_UPDATED				(0x80000000 >> 30)
#define IPR_PCII_CORE_ISSUED_RST_REQ		(0x80000000 >> 31)

#define IPR_PCII_ERROR_INTERRUPTS \
(IPR_PCII_IOARCB_XFER_FAILED | IPR_PCII_IOA_UNIT_CHECKED | \
IPR_PCII_NO_HOST_RRQ | IPR_PCII_IOARRIN_LOST | IPR_PCII_MMIO_ERROR)

#define IPR_PCII_OPER_INTERRUPTS \
(IPR_PCII_ERROR_INTERRUPTS | IPR_PCII_HRRQ_UPDATED | IPR_PCII_IOA_TRANS_TO_OPER)

#define IPR_UPROCI_RESET_ALERT			(0x80000000 >> 7)
#define IPR_UPROCI_IO_DEBUG_ALERT			(0x80000000 >> 9)

#define IPR_LDUMP_MAX_LONG_ACK_DELAY_IN_USEC		1000000	/* 1 second */
#define IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC		500000	/* 500 ms */

/*
 * Dump literals
 */
#define IPR_MIN_DUMP_SIZE				(1 * 1024 * 1024)
#define IPR_MAX_IOA_DUMP_SIZE				(4 * 1024 * 1024)
#define IPR_NUM_SDT_ENTRIES				511

/*
 * Misc literals
 */
#define IPR_MAX_OP_SIZE				(256 * 1024)
#define IPR_NUM_IOADL_ENTRIES			IPR_MAX_SGLIST

#define IPR_MAX_NUM_DUMP_PAGES	((IPR_MAX_IOA_DUMP_SIZE / PAGE_SIZE) + 1)
#define IPR_NUM_MINORS			(1<<MINORBITS)

/*
 * Adapter interface types
 */

struct ipr_res_addr {
	u8 reserved;
	u8 bus;
	u8 target;
	u8 lun;
#define IPR_GET_PHYS_LOC(res_addr) \
	(((res_addr).bus << 16) | ((res_addr).target << 8) | (res_addr).lun)
}__attribute__((packed, aligned (4)));

struct ipr_std_inq_vpids {
	u8 vendor_id[IPR_VENDOR_ID_LEN];
	u8 product_id[IPR_PROD_ID_LEN];
};

struct ipr_std_inq_data {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 peri_qual:3;
	u8 peri_dev_type:5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 peri_dev_type:5;
	u8 peri_qual:3;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 removeable_medium:1;
	u8 reserved1:7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved1:7;
	u8 removeable_medium:1;
#endif

	u8 version;

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 aen:1;
	u8 obsolete1:1;
	u8 norm_aca:1;
	u8 hi_sup:1;
	u8 resp_data_fmt:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 resp_data_fmt:4;
	u8 hi_sup:1;
	u8 norm_aca:1;
	u8 obsolete1:1;
	u8 aen:1;
#endif

	u8 additional_len;

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 sccs:1;
	u8 reserved2:7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved2:7;
	u8 sccs:1;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 bque:1;
	u8 enc_serv:1;
	u8 vs:1;
	u8 multi_port:1;
	u8 mchngr:1;
	u8 obsolete2:2;
	u8 addr16:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 addr16:1;
	u8 obsolete2:2;
	u8 mchngr:1;
	u8 multi_port:1;
	u8 vs:1;
	u8 enc_serv:1;
	u8 bque:1;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 rel_adr:1;
	u8 obsolete3:1;
	u8 wbus16:1;
	u8 sync:1;
	u8 linked:1;
	u8 trans_dis:1;
	u8 cmd_que:1;
	u8 vs2:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 vs2:1;
	u8 cmd_que:1;
	u8 trans_dis:1;
	u8 linked:1;
	u8 sync:1;
	u8 wbus16:1;
	u8 obsolete3:1;
	u8 rel_adr:1;
#endif

	struct ipr_std_inq_vpids vpids;

	u8 ros_rsvd_ram_rsvd[4];

	u8 serial_num[IPR_SERIAL_NUM_LEN];
}__attribute__ ((packed));

struct ipr_config_table_entry {
	u8 service_level;
	u8 array_id;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 is_ioa_resource:1;
	u8 reserved:1;
	u8 is_array_member:1;
	u8 is_hot_spare:1;
	u8 reserved2:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved2:4;
	u8 is_hot_spare:1;
	u8 is_array_member:1;
	u8 reserved:1;
	u8 is_ioa_resource:1;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved3:4;
	u8 subtype:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 subtype:4;
	u8 reserved3:4;
#endif

#define IPR_SUBTYPE_AF_DASD			0
#define IPR_SUBTYPE_GENERIC_SCSI	1
#define IPR_SUBTYPE_VOLUME_SET		2

	struct ipr_res_addr res_addr;
	u32 res_handle;
	u32 reserved4[2];
	struct ipr_std_inq_data std_inq_data;
}__attribute__ ((packed, aligned (4)));

struct ipr_config_table {
	u8 num_entries;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved1:3;
	u8 ucode_download_req:1;
	u8 reserved2:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved2:4;
	u8 ucode_download_req:1;
	u8 reserved1:3;
#endif

	u16 reserved3;
	struct ipr_config_table_entry dev[IPR_MAX_PHYSICAL_DEVS];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_cfg_ch_not {
	struct ipr_config_table_entry cfgte;
	u8 reserved[936];
}__attribute__((packed, aligned (4)));

struct ipr_supported_device {
	u16 data_length;
	u8 reserved;
	u8 num_records;
	struct ipr_std_inq_vpids vpids;
	u8 reserved2[16];
}__attribute__((packed, aligned (4)));

struct ipr_dasd_timeout_record {
	u8 op_code;
	u8 reserved;
	u16 timeout;		/* Timeout in seconds */
}__attribute__((packed, aligned (4)));

/* xxx is this reasonable? */
/*  A constant array of DASD Timeouts */
static const struct ipr_dasd_timeout_record ipr_dasd_timeouts[] = {
	{TEST_UNIT_READY, 0x00, __constant_cpu_to_be16(30)},
	{REQUEST_SENSE, 0x00, __constant_cpu_to_be16(30)},
	{INQUIRY, 0x00, __constant_cpu_to_be16(30)},
	{MODE_SELECT, 0x00, __constant_cpu_to_be16(30)},
	{MODE_SENSE, 0x00, __constant_cpu_to_be16(30)},
	{READ_CAPACITY, 0x00, __constant_cpu_to_be16(30)},
	{READ_10, 0x00, __constant_cpu_to_be16(30)},
	{WRITE_10, 0x00, __constant_cpu_to_be16(30)},
	{WRITE_VERIFY, 0x00, __constant_cpu_to_be16(30)},
	{FORMAT_UNIT, 0x00, __constant_cpu_to_be16(7200)},	/* 2 Hours */
	{REASSIGN_BLOCKS, 0x00, __constant_cpu_to_be16(600)},	/* 10 minutes */
	{START_STOP, 0x00, __constant_cpu_to_be16(120)},
	{SEND_DIAGNOSTIC, 0x00, __constant_cpu_to_be16(300)},	/* 5 minutes */
	{VERIFY, 0x00, __constant_cpu_to_be16(300)},	/* 5 minutes */
	{WRITE_BUFFER, 0x00, __constant_cpu_to_be16(300)},	/* 5 minutes */
	{WRITE_SAME, 0x00, __constant_cpu_to_be16(14400)},	/* 4 hours */
	{LOG_SENSE, 0x00, __constant_cpu_to_be16(30)},
	{REPORT_LUNS, 0x00, __constant_cpu_to_be16(30)},
	{IPR_SKIP_READ, 0x00, __constant_cpu_to_be16(30)},
	{IPR_SKIP_WRITE, 0x00, __constant_cpu_to_be16(30)}
};

struct ipr_dasd_timeouts {
	u32 length;
	struct ipr_dasd_timeout_record record[ARRAY_SIZE(ipr_dasd_timeouts)];
}__attribute__((packed, aligned (4)));

/* Command packet structure */
struct ipr_cmd_pkt {
	u16 reserved;		/* Reserved by IOA */
	u8 request_type;
#define IPR_RQTYPE_SCSICDB		0x00
#define IPR_RQTYPE_IOACMD		0x01
#define IPR_RQTYPE_HCAM			0x02

	u8 luntar_luntrn;

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 write_not_read:1;
	u8 reserved2:1;
	u8 no_underlength_checking:1;
	u8 sync_override:1;
	u8 sync_complete:1;
	u8 no_link_descriptors:1;
	u8 reserved3:2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved3:2;
	u8 no_link_descriptors:1;
	u8 sync_complete:1;
	u8 sync_override:1;
	u8 no_underlength_checking:1;
	u8 reserved2:1;
	u8 write_not_read:1;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved4:2;
	u8 aligned_client_buffer:1;
	u8 ext_delay_after_reset:1;
	u8 task_attributes:3;
	u8 reserved5:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved5:1;
	u8 task_attributes:3;
	u8 ext_delay_after_reset:1;
	u8 aligned_client_buffer:1;
	u8 reserved4:2;
#endif
#define IPR_UNTAGGED_TASK	0
#define IPR_SIMPLE_TASK		1
#define IPR_ORDERED_TASK	2
#define IPR_HEAD_OF_Q_TASK	3
#define IPR_ACA_TASK		4

	u8 cdb[16];
	u16 timeout;
}__attribute__ ((packed, aligned(4)));

/* IOA Request Control Block    128 bytes  */
struct ipr_ioarcb {
	u32 ioarcb_host_pci_addr;
	u32 reserved;
	u32 res_handle;
	u32 host_response_handle;
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;

	u32 write_data_transfer_length;
	u32 read_data_transfer_length;
	u32 write_ioadl_addr;
	u32 write_ioadl_len;
	u32 read_ioadl_addr;
	u32 read_ioadl_len;

	u32 ioasa_host_pci_addr;
	u16 ioasa_len;
	u16 reserved4;

	struct ipr_cmd_pkt cmd_pkt;

	u32 add_cmd_parms_len;
	u32 add_cmd_parms[10];
}__attribute__((packed, aligned (4)));

struct ipr_ioadl_desc {
	u32 flags_and_data_len;
#define IPR_IOADL_FLAGS_MASK		0xff000000
#define IPR_IOADL_GET_FLAGS(x) (be32_to_cpu(x) & IPR_IOADL_FLAGS_MASK)
#define IPR_IOADL_DATA_LEN_MASK		0x00ffffff
#define IPR_IOADL_GET_DATA_LEN(x) (be32_to_cpu(x) & IPR_IOADL_DATA_LEN_MASK)
#define IPR_IOADL_FLAGS_READ		0x48000000
#define IPR_IOADL_FLAGS_READ_LAST	0x49000000
#define IPR_IOADL_FLAGS_WRITE		0x68000000
#define IPR_IOADL_FLAGS_WRITE_LAST	0x69000000
#define IPR_IOADL_FLAGS_LAST		0x01000000

	u32 address;
}__attribute__((packed, aligned (8)));

struct ipr_ioasa_vset {
	u32 failing_lba_hi;
	u32 failing_lba_lo;
	u32 ioa_data[22];
}__attribute__((packed, aligned (4)));

struct ipr_ioasa_gpdd {
	u8 device_end_state;
	u8 device_bus_phase;
	u16 reserved;
	u32 ioa_data[23];
}__attribute__((packed, aligned (4)));

struct ipr_ioasa_raw {
	u32 ioa_data[24];
}__attribute__((packed, aligned (4)));

struct ipr_ioasa {
	u32 ioasc;
#define IPR_IOASC_SENSE_KEY(ioasc) ((ioasc) >> 24)
#define IPR_IOASC_SENSE_CODE(ioasc) (((ioasc) & 0x00ff0000) >> 16)
#define IPR_IOASC_SENSE_QUAL(ioasc) (((ioasc) & 0x0000ff00) >> 8)
#define IPR_IOASC_SENSE_STATUS(ioasc) ((ioasc) & 0x000000ff)

	u16 ret_stat_len;	/* Length of the returned IOASA */

	u16 avail_stat_len;	/* Total Length of status available. */

	u32 residual_data_len;	/* number of bytes in the host data */
	/* buffers that were not used by the IOARCB command. */

	u32 ilid;
#define IPR_NO_ILID			0
#define IPR_DRIVER_ILID		0xffffffff

	u32 fd_ioasc;

	u32 fd_phys_locator;

	u32 fd_res_handle;

	u32 ioasc_specific;	/* status code specific field */
#define IPR_IOASC_SPECIFIC_MASK		0x00ffffff
#define IPR_FIELD_POINTER_VALID		(0x80000000 >> 8)
#define IPR_FIELD_POINTER_MASK		0x0000ffff

	union {
		struct ipr_ioasa_vset vset;
		struct ipr_ioasa_gpdd gpdd;
		struct ipr_ioasa_raw raw;
	};
}__attribute__((packed, aligned (4)));

struct ipr_mode_parm_hdr {
	u8 length;
	u8 medium_type;
	u8 device_spec_parms;
	u8 block_desc_len;
}__attribute__((packed));

struct ipr_mode_pages {
	struct ipr_mode_parm_hdr hdr;
	u8 data[255 - sizeof(struct ipr_mode_parm_hdr)];
}__attribute__((packed));

struct ipr_mode_page_hdr {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 parms_saveable:1;
	u8 reserved1:1;
	u8 page_code:6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 page_code:6;
	u8 reserved1:1;
	u8 parms_saveable:1;
#endif
	u8 page_length;
}__attribute__ ((packed));

struct ipr_rw_err_mode_page {
	/* Page code 0x01 */
	struct ipr_mode_page_hdr hdr;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 awre:1;
	u8 arre:1;
	u8 tb:1;
	u8 rc:1;
	u8 eer:1;
	u8 per:1;
	u8 dte:1;
	u8 dcr:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 dcr:1;
	u8 dte:1;
	u8 per:1;
	u8 eer:1;
	u8 rc:1;
	u8 tb:1;
	u8 arre:1;
	u8 awre:1;
#endif

	u8 read_retry_count;
	u8 correction_span;
	u8 head_offset_count;
	u8 data_strobe_offset_count;
	u8 reserved1;
	u8 write_retry_count;
	u8 reserved2;
	u16 recovery_time_limit;
}__attribute__ ((packed));

struct ipr_ioa_dasd_page_20 {
	struct ipr_mode_page_hdr hdr;
	u8 reserved;
	u8 max_tcq_depth;
}__attribute__ ((packed));

struct ipr_dev_bus_entry {
	struct ipr_res_addr res_addr;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 qas_capability:2;
	u8 enable_target_mode:1;
	u8 term_power_absent:1;
	u8 target_mode_supported:1;
	u8 lvd_to_se_transition_not_allowed:1;
	u8 reserved2:2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved2:2;
	u8 lvd_to_se_transition_not_allowed:1;
	u8 target_mode_supported:1;
	u8 term_power_absent:1;
	u8 enable_target_mode:1;
	u8 qas_capability:2;
#endif

#define IPR_MODEPAGE28_QAS_CAPABILITY_DISABLE_ALL	1
#define IPR_MODEPAGE28_QAS_CAPABILITY_ENABLE_ALL	2

	u8 scsi_id;
	u8 bus_width;
	u8 extended_reset_delay;
#define IPR_EXTENDED_RESET_DELAY	7

	u32 max_xfer_rate;

	u8 spinup_delay;
	u8 reserved3;
	u16 reserved4;
}__attribute__((packed, aligned (4)));

struct ipr_control_mode_page {
	/* Mode page 0x0A */
	struct ipr_mode_page_hdr hdr;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 tst:3;
	u8 reserved1:3;
	u8 gltsd:1;
	u8 rlec:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rlec:1;
	u8 gltsd:1;
	u8 reserved1:3;
	u8 tst:3;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 queue_algorithm_modifier:4;
	u8 reserved2:1;
	u8 qerr:2;
	u8 dque:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 dque:1;
	u8 qerr:2;
	u8 reserved2:1;
	u8 queue_algorithm_modifier:4;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved3:1;
	u8 rac:1;
	u8 reserved4:2;
	u8 swp:1;
	u8 raerp:1;
	u8 uaaerp:1;
	u8 eaerp:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 eaerp:1;
	u8 uaaerp:1;
	u8 raerp:1;
	u8 swp:1;
	u8 reserved4:2;
	u8 rac:1;
	u8 reserved3:1;
#endif
	u8 reserved5;
	u16 ready_aen_holdoff_period;
	u16 busy_timeout_period;
	u16 reserved6;
}__attribute__ ((packed));

struct ipr_vset_res_state {
	u16 stripe_size;
	u8 prot_level;
	u8 num_devices_in_vset;
	u32 reserved6;
}__attribute__((packed, aligned (4)));

struct ipr_dasd_res_state {
	u32 data_path_width;	/* bits */
	u32 data_xfer_rate;	/* 100 KBytes/second */
}__attribute__((packed, aligned (4)));

struct ipr_query_res_state {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved1:1;
	u8 not_oper:1;
	u8 not_ready:1;
	u8 not_func:1;
	u8 reserved2:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved2:4;
	u8 not_func:1;
	u8 not_ready:1;
	u8 not_oper:1;
	u8 reserved1:1;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 read_write_prot:1;
	u8 reserved3:7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved3:7;
	u8 read_write_prot:1;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 prot_dev_failed:1;
	u8 prot_suspended:1;
	u8 prot_resuming:1;
	u8 degraded_oper:1;
	u8 service_req:1;
	u8 reserved4:3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved4:3;
	u8 service_req:1;
	u8 degraded_oper:1;
	u8 prot_resuming:1;
	u8 prot_suspended:1;
	u8 prot_dev_failed:1;
#endif

	u8 reserved5;

	union {
		struct ipr_vset_res_state vset;
		struct ipr_dasd_res_state dasd;
	};

	u32 ilid;
	u32 failing_dev_ioasc;
	struct ipr_res_addr failing_dev_res_addr;
	u32 failing_dev_res_handle;
	u8 protection_level_str[8];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_device_data_entry {
	struct ipr_std_inq_vpids dev_vpids;
	u8 dev_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_res_addr dev_res_addr;
	struct ipr_std_inq_vpids new_dev_vpids;
	u8 new_dev_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids ioa_last_with_dev_vpids;
	u8 ioa_last_with_dev_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids cfc_last_with_dev_vpids;
	u8 cfc_last_with_dev_sn[IPR_SERIAL_NUM_LEN];
	u32 ioa_data[5];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_array_data_entry {
	struct ipr_std_inq_vpids vpids;
	u8 serial_num[IPR_SERIAL_NUM_LEN];
	struct ipr_res_addr expected_dev_res_addr;
	struct ipr_res_addr dev_res_addr;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_ff_error {
	u32 ioa_data[246];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_01_error {
	u32 seek_counter;
	u32 read_counter;
	u8 sense_data[32];
	u32 ioa_data[236];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_02_error {
	struct ipr_std_inq_vpids ioa_vpids;
	u8 ioa_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids cfc_vpids;
	u8 cfc_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids ioa_last_attached_to_cfc_vpids;
	u8 ioa_last_attached_to_cfc_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids cfc_last_attached_to_ioa_vpids;
	u8 cfc_last_attached_to_ioa_sn[IPR_SERIAL_NUM_LEN];
	u32 ioa_data[3];
	u8 reserved[844];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_03_error {
	struct ipr_std_inq_vpids ioa_vpids;
	u8 ioa_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids cfc_vpids;
	u8 cfc_sn[IPR_SERIAL_NUM_LEN];
	u32 errors_detected;
	u32 errors_logged;
	u8 ioa_data[12];
	struct ipr_hostrcb_device_data_entry dev_entry[3];
	u8 reserved[444];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_04_error {
	struct ipr_std_inq_vpids ioa_vpids;
	u8 ioa_sn[IPR_SERIAL_NUM_LEN];
	struct ipr_std_inq_vpids cfc_vpids;
	u8 cfc_sn[IPR_SERIAL_NUM_LEN];
	u8 ioa_data[12];
	struct ipr_hostrcb_array_data_entry array_member[10];
	u32 exposed_mode_adn;
	u32 array_id;
	struct ipr_std_inq_vpids incomp_dev_vpids;
	u8 incomp_dev_sn[IPR_SERIAL_NUM_LEN];
	u32 ioa_data2;
	struct ipr_hostrcb_array_data_entry array_member2[8];
	struct ipr_res_addr last_func_vset_res_addr;
	u8 vset_serial_num[IPR_SERIAL_NUM_LEN];
	u8 protection_level[8];
	u8 reserved[124];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_error {
	u32 failing_dev_ioasc;
	struct ipr_res_addr failing_dev_res_addr;
	u32 failing_dev_res_handle;
	u32 prc;
	union {
		struct ipr_hostrcb_type_ff_error type_ff_error;
		struct ipr_hostrcb_type_01_error type_01_error;
		struct ipr_hostrcb_type_02_error type_02_error;
		struct ipr_hostrcb_type_03_error type_03_error;
		struct ipr_hostrcb_type_04_error type_04_error;
	};
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_raw {
	u32 data[sizeof(struct ipr_hostrcb_error)/4];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb {
	u8 op_code;
#define IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE			0xE1u
#define IPR_HOST_RCB_OP_CODE_LOG_DATA				0xE2u

	u8 notify_type;
#define IPR_HOST_RCB_NOTIF_TYPE_EXISTING_CHANGED	0x00u
#define IPR_HOST_RCB_NOTIF_TYPE_NEW_ENTRY			0x01u
#define IPR_HOST_RCB_NOTIF_TYPE_REM_ENTRY			0x02u
#define IPR_HOST_RCB_NOTIF_TYPE_ERROR_LOG_ENTRY		0x10u
#define IPR_HOST_RCB_NOTIF_TYPE_INFORMATION_ENTRY	0x11u

	u8 notifications_lost;
#define IPR_HOST_RCB_NO_NOTIFICATIONS_LOST			0
#define IPR_HOST_RCB_NOTIFICATIONS_LOST				0x80u

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 internal_oper_flag:1;
	u8 error_resp_sent:1;
	u8 reserved0:6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved0:6;
	u8 error_resp_sent:1;
	u8 internal_oper_flag:1;
#endif

	u8 overlay_id;
#define IPR_HOST_RCB_OVERLAY_ID_1				0x01u
#define IPR_HOST_RCB_OVERLAY_ID_2				0x02u
#define IPR_HOST_RCB_OVERLAY_ID_3				0x03u
#define IPR_HOST_RCB_OVERLAY_ID_4				0x04u
#define IPR_HOST_RCB_OVERLAY_ID_6				0x06u
#define IPR_HOST_RCB_OVERLAY_ID_DEFAULT			0xFFu

	u8 reserved1[3];
	u32 ilid;
	u32 time_since_last_ioa_reset;
	u32 reserved2;
	u32 length;

	union {
		struct ipr_hostrcb_error error;
		struct ipr_hostrcb_cfg_ch_not ccn;
		struct ipr_hostrcb_raw raw;
	};

	/* Driver added data */
	u32 hostrcb_dma;
	struct list_head queue;

	/* Used for device initialization */
	struct ipr_resource_entry *res;
	struct ipr_dasd_timeouts dasd_timeouts;
	struct ipr_mode_pages mode_pages;
	struct ipr_mode_pages changeable_parms;
	struct ipr_std_inq_data std_inq;
	struct ipr_supported_device supp_dev;
	struct ipr_query_res_state res_query;
}__attribute__((packed, aligned (4)));

/* IPR smart dump table structures */
struct ipr_sdt_entry {
	u32 bar_str_offset;
	u32 end_offset;
	u8 entry_byte;
	u8 reserved[3];

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 endian:1;
	u8 reserved1:1;
	u8 valid_entry:1;
	u8 reserved2:5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved2:5;
	u8 valid_entry:1;
	u8 reserved1:1;
	u8 endian:1;
#endif

	u8 resv;
	u16 priority;
}__attribute__((packed, aligned (4)));

/*
 * Driver types
 */
struct ipr_driver_cfg {
	u16 debug_level;
	u16 debug_level_max;
}__attribute__((packed, aligned (2)));

struct ipr_dump_ioctl {
	u32 reserved;
	u32 buffer_len;
	u8 buffer[0];
}__attribute__((packed, aligned (4)));

struct ipr_bus_attributes {
	u8 bus;
	u8 qas_enabled;
	u8 bus_width;
	u8 reserved;
	u32 max_xfer_rate;
}__attribute__((packed, aligned (4)));

struct ipr_get_trace_ioctl {
	u32 reserved;
	u32 buffer_len;
	u8 buffer[0];
}__attribute__((packed, aligned (4)));

struct ipr_reclaim_cache_ioctl {
	u32 reserved;
	u32 ioasc;
	u32 buffer_len;
	u8 buffer[0];
}__attribute__((packed, aligned (4)));

struct ipr_query_config_ioctl {
	u32 reserved;
	u32 buffer_len;
	u8 buffer[0];
}__attribute__((packed, aligned (4)));

struct ipr_ucode_download_ioctl {
	u32 reserved;
	u32 ioasc;
	u32 buffer_len;
	u8 buffer[0];
}__attribute__((packed, aligned (4)));

struct ipr_passthru_ioctl {
	u32 timeout_in_sec;
	u32 ioasc;
	u32 res_handle;
	struct ipr_cmd_pkt cmd_pkt;
	u32 buffer_len;
	u8 buffer[0];
}__attribute__((packed, aligned (4)));

#define IPR_IOCTL_CODE 'i'
#define IPR_IOCTL_PASSTHRU _IOWR(IPR_IOCTL_CODE, 0x40, struct ipr_passthru_ioctl)
#define IPR_IOCTL_RUN_DIAGNOSTICS _IO(IPR_IOCTL_CODE, 0x41)
#define IPR_IOCTL_DUMP_IOA _IOW(IPR_IOCTL_CODE, 0x42, struct ipr_dump_ioctl)
#define IPR_IOCTL_RESET_IOA _IO(IPR_IOCTL_CODE, 0x43)
#define IPR_IOCTL_READ_DRIVER_CFG _IOR(IPR_IOCTL_CODE, 0x44, struct ipr_driver_cfg)
#define IPR_IOCTL_WRITE_DRIVER_CFG _IOW(IPR_IOCTL_CODE, 0x45, struct ipr_driver_cfg)
#define IPR_IOCTL_GET_BUS_CAPABILTIES _IOWR(IPR_IOCTL_CODE, 0x46, struct ipr_bus_attributes)
#define IPR_IOCTL_SET_BUS_ATTRIBUTES _IOW(IPR_IOCTL_CODE, 0x47, struct ipr_bus_attributes)
#define IPR_IOCTL_GET_TRACE _IOW(IPR_IOCTL_CODE, 0x48, struct ipr_get_trace_ioctl)
#define IPR_IOCTL_RECLAIM_CACHE _IOWR(IPR_IOCTL_CODE, 0x49, struct ipr_reclaim_cache_ioctl)
#define IPR_IOCTL_QUERY_CONFIGURATION _IOW(IPR_IOCTL_CODE, 0x4A, struct ipr_query_config_ioctl)
#define IPR_IOCTL_UCODE_DOWNLOAD _IOWR(IPR_IOCTL_CODE, 0x4B, struct ipr_ucode_download_ioctl)
#define IPR_IOCTL_CHANGE_ADAPTER_ASSIGNMENT _IO(IPR_IOCTL_CODE, 0x4C)
 
/*
 * SCSI Structures
 */
struct ipr_block_desc {
	u8 num_blocks[4];
	u8 density_code;
	u8 block_length[3];
}__attribute__((packed));

struct ipr_mode_page28 {
	struct ipr_mode_page_hdr hdr;
	u8 num_entries;
	u8 entry_length;
	struct ipr_dev_bus_entry bus[0];
}__attribute__((packed));

struct ipr_std_inq_vpids_sn {
	struct ipr_std_inq_vpids vpids;
	u8 serial_num[IPR_SERIAL_NUM_LEN];
}__attribute__((packed));

struct ipr_ioa_vpd {
	struct ipr_std_inq_data std_inq_data;
	u8 ascii_part_num[12];
	u8 reserved[40];
	u8 ascii_plant_code[4];
}__attribute__((packed));

struct ipr_cfc_vpd {
	u8 peri_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 add_page_len;
	u8 ascii_len;
	u8 cache_size[3];
	struct ipr_std_inq_vpids vpids;
	u8 model_num[3];
	u8 reserved2[9];
	u8 revision_level[4];
	u8 serial_num[IPR_SERIAL_NUM_LEN];
	u8 ascii_part_num[12];
	u8 reserved3[40];
	u8 ascii_plant_code[4];
}__attribute__((packed));

struct ipr_dram_vpd {
	u8 peri_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 add_page_len;
	u8 ascii_len;
	u8 dram_size[3];
}__attribute__((packed));

struct ipr_inquiry_page0 {	/* Supported Vital Product Data Pages */
	u8 peri_qual_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 page_length;
	u8 supported_page_codes[IPR_MAX_NUM_SUPP_INQ_PAGES];
}__attribute__((packed));

struct ipr_inquiry_page3 {
	u8 peri_qual_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 page_length;
	u8 ascii_len;
	u8 reserved2[3];
	u8 load_id[4];
	u8 major_release;
	u8 card_type;
	u8 minor_release[2];
	u8 ptf_number[4];
	u8 patch_number[4];
}__attribute__((packed));

/*
 * Driver types
 */
struct ipr_resource_entry {
	struct ipr_config_table_entry cfgte;
	u8 in_init:1;
	u8 redo_init:1;
	u8 needs_sync_complete:1;
	u8 in_erp:1;
	u8 add_to_ml:1;
	u8 del_from_ml:1;
	u8 resetting_device:1;

	struct scsi_device *scsi_device;
	struct list_head queue;
};

struct ipr_resource_hdr {
	u16 num_entries;
	u16 reserved;
};

struct ipr_resource_table {
	struct ipr_resource_hdr hdr;
	struct ipr_resource_entry dev[IPR_MAX_PHYSICAL_DEVS];
};

struct ipr_vpd_cbs {
	struct ipr_ioa_vpd ioa_vpd;
	struct ipr_inquiry_page3 page3_data;
	struct ipr_inquiry_page0 page0_data;
	struct ipr_mode_pages mode_pages;
};

struct ipr_interrupts {
	unsigned long set_interrupt_mask_reg;
	unsigned long clr_interrupt_mask_reg;
	unsigned long sense_interrupt_mask_reg;
	unsigned long clr_interrupt_reg;

	unsigned long sense_interrupt_reg;
	unsigned long ioarrin_reg;
	unsigned long sense_uproc_interrupt_reg;
	unsigned long set_uproc_interrupt_reg;
	unsigned long clr_uproc_interrupt_reg;
};

struct ipr_chip_cfg_t {
	u32 mailbox;
	u8 cache_line_size;
	struct ipr_interrupts regs;
};

enum ipr_shutdown_type {
	IPR_SHUTDOWN_NORMAL = 0x00,
	IPR_SHUTDOWN_PREPARE_FOR_NORMAL = 0x40,
	IPR_SHUTDOWN_ABBREV = 0x80,
	IPR_SHUTDOWN_NONE = 0x100
};

struct ipr_sdt_header {
	u32 state;
	u32 num_entries;
	u32 num_entries_used;
	u32 dump_size;
}__attribute__((packed, aligned (4)));

struct ipr_sdt {
	struct ipr_sdt_header hdr;
	struct ipr_sdt_entry entry[IPR_NUM_SDT_ENTRIES];
}__attribute__((packed, aligned (4)));

struct ipr_uc_sdt {
	struct ipr_sdt_header hdr;
	struct ipr_sdt_entry entry[1];
}__attribute__((packed, aligned (4)));

struct ipr_trace_entry {
	u32 time;

	u8 op_code;
	u8 type;
#define IPR_TRACE_START			0x00
#define IPR_TRACE_FINISH		0xff
	u16 cmd_index;

	u32 res_handle;
	union {
		u32 ioasc;
		u32 add_data;
		u32 res_addr;
	};
};

struct ipr_sglist {
	u32 order;
	u32 num_sg;
	struct scatterlist scatterlist[1];
};

enum ipr_sdt_state {
	INACTIVE,
	WAIT_FOR_DUMP,
	GET_DUMP,
	ABORT_DUMP,
	DUMP_OBTAINED
};

/* Per-controller data */
struct ipr_ioa_cfg {
	char eye_catcher[8];
#define IPR_EYECATCHER			"iprcfg"

	struct list_head queue;

	u8 allow_interrupts:1;
	u8 in_reset_reload:1;
	u8 in_ioa_bringdown:1;
	u8 ioa_unit_checked:1;
	u8 ioa_is_dead:1;
	u8 dump_taken:1;
	u8 allow_cmds:1;
	u8 allow_ml_add_del:1;

	u16 type; /* CCIN of the card */

	u8 debug_level;
#define IPR_MAX_DEBUG_LEVEL			4
#define IPR_DEFAULT_DEBUG_LEVEL		2

#define IPR_NUM_TRACE_INDEX_BITS	9
#define IPR_NUM_TRACE_ENTRIES		(1 << IPR_NUM_TRACE_INDEX_BITS)
#define IPR_TRACE_SIZE	(sizeof(struct ipr_trace_entry) * IPR_NUM_TRACE_ENTRIES)
	char trace_start[8];
#define IPR_TRACE_START_LABEL			"trace"
	struct ipr_trace_entry *trace;
	u32 trace_index:IPR_NUM_TRACE_INDEX_BITS;

	/*
	 * Queue for free command blocks
	 */
	char ipr_free_label[8];
#define IPR_FREEQ_LABEL			"free-q"
	struct list_head free_q;

	/*
	 * Queue for command blocks outstanding to the adapter
	 */
	char ipr_pending_label[8];
#define IPR_PENDQ_LABEL			"pend-q"
	struct list_head pending_q;

	char cfg_table_start[8];
#define IPR_CFG_TBL_START		"cfg"
	struct ipr_config_table *cfg_table;
	u32 cfg_table_dma;

	char resource_table_label[8];
#define IPR_RES_TABLE_LABEL		"res_tbl"
	struct ipr_resource_entry *res_entries;
	struct list_head free_res_q;
	struct list_head used_res_q;

	char ipr_hcam_label[8];
#define IPR_HCAM_LABEL			"hcams"
	struct ipr_hostrcb *hostrcb[IPR_NUM_HCAMS];
	u32 hostrcb_dma[IPR_NUM_HCAMS];
	struct list_head hostrcb_free_q;
	struct list_head hostrcb_pending_q;

	u32 *host_rrq;
	u32 host_rrq_dma;
#define IPR_HRRQ_REQ_RESP_HANDLE_MASK	0xfffffffc
#define IPR_HRRQ_RESP_BIT_SET			0x00000002
#define IPR_HRRQ_TOGGLE_BIT				0x00000001
#define IPR_HRRQ_REQ_RESP_HANDLE_SHIFT	2
	volatile u32 *hrrq_start;
	volatile u32 *hrrq_end;
	volatile u32 *hrrq_curr;
	volatile u32 toggle_bit;

	struct ipr_bus_attributes bus_attr[IPR_MAX_NUM_BUSES];

	const struct ipr_chip_cfg_t *chip_cfg;

	unsigned long hdw_dma_regs;	/* iomapped PCI memory space */
	unsigned long hdw_dma_regs_pci;	/* raw PCI memory space */
	unsigned long ioa_mailbox;
	struct ipr_interrupts regs;

	u32 pci_cfg_buf[64];
	u16 saved_pcix_cmd_reg;
	u16 reset_retries;

	u32 errors_logged;

	struct Scsi_Host *host;
	struct pci_dev *pdev;

	struct work_struct low_pri_work;
	int minor_num;
	int block_host_ops;

	struct semaphore ioctl_semaphore;

	wait_queue_head_t reset_wait_q;

	struct ipr_dump_ioa_entry *ioa_dump;
	struct ipr_dump_driver_header *dump;
	wait_queue_head_t sdt_wait_q;
	enum ipr_sdt_state sdt_state;

	struct ipr_vpd_cbs *vpd_cbs;
	u32 vpd_cbs_dma;

	struct pci_pool *ipr_cmd_pool;

	struct ipr_cmnd *reset_cmd;

	char ipr_cmd_label[8];
#define IPR_CMD_LABEL		"ipr_cmnd"
	struct ipr_cmnd *ipr_cmnd_list[IPR_NUM_CMD_BLKS];
	u32 ipr_cmnd_list_dma[IPR_NUM_CMD_BLKS];
};

struct ipr_cmnd {
	struct ipr_ioarcb ioarcb;
	struct ipr_ioasa ioasa;
	struct ipr_ioadl_desc ioadl[IPR_NUM_IOADL_ENTRIES];
	struct list_head queue;
	struct scsi_cmnd *scsi_cmd;
	struct completion completion;
	struct timer_list timer;
	void (*done) (struct ipr_cmnd *);
	int (*job_step) (struct ipr_cmnd *);
	u16 cmd_index;
	u16 flags;
	u8 sense_buffer[SCSI_SENSE_BUFFERSIZE];
	dma_addr_t sense_buffer_dma;
	unsigned short dma_use_sg;
	dma_addr_t dma_handle;
	union {
		enum ipr_shutdown_type shutdown_type;
		struct ipr_hostrcb *hostrcb;
		unsigned long time_left;
		unsigned long scratch;
		struct ipr_resource_entry *res;
	};

	struct ipr_cmnd *parent;
	struct ipr_ioa_cfg *ioa_cfg;
};

struct ipr_ses_table_entry {
	char product_id[17];
	char compare_product_id_byte[17];
	u32 max_bus_speed_limit;	/* MB/sec limit for this backplane */
};

struct ipr_dump_header {
	u32 eye_catcher;
#define IPR_DUMP_EYE_CATCHER		0xC5D4E3F2
	u32 len;
	u32 num_elems;
	u32 first_entry_offset;
	u32 status;
}__attribute__((packed, aligned (4)));

struct ipr_dump_entry_header {
	u32 length;		/* MUST be the first member of the structure */
	u32 id;
#define IPR_DUMP_IOA_DUMP_ID	2
#define IPR_DUMP_TEXT_ID		3
#define IPR_DUMP_TRACE_ID		4
}__attribute__((packed, aligned (4)));

struct ipr_dump_location_entry {
	struct ipr_dump_entry_header hdr;
	u8 location[BUS_ID_SIZE];
}__attribute__((packed));

struct ipr_dump_trace_entry {
	struct ipr_dump_entry_header hdr;
	u32 trace[IPR_TRACE_SIZE / sizeof(u32)];
}__attribute__((packed, aligned (4)));

struct ipr_dump_driver_header {
	struct ipr_dump_header hdr;
	struct ipr_dump_location_entry location_entry;
	struct ipr_dump_trace_entry trace_entry;
}__attribute__((packed));

struct ipr_dump_ioa_entry {
	struct ipr_dump_entry_header hdr;
	u32 next_page_index;
	u32 page_offset;
	u32 format;
#define IPR_SDT_FMT2		2
#define IPR_SDT_UNKNOWN		3
	u32 reserved;
	struct ipr_sdt sdt;
	u32 *ioa_data[IPR_MAX_NUM_DUMP_PAGES];
}__attribute__((packed, aligned (4)));

struct ipr_error_table_t {
	u32 ioasc;
	u16 dev_urc;
	u16 iop_urc;
	char *class;
	int log_ioasa;
	char *error;
};

/*
 * Macros
 */
#if IPR_DEBUG
#define IPR_DBG_CMD(CMD) { (CMD); }
#else
#define IPR_DBG_CMD(CMD)
#endif

#define IPR_SET_MODE(change_mask, cur_val, new_val) \
{                                                   \
int mod_bits = (cur_val ^ new_val);                 \
if ((change_mask & mod_bits) == mod_bits)           \
{                                                   \
cur_val = new_val;                                  \
}                                                   \
}

#define ipr_breakpoint_data KERN_ERR IPR_NAME\
": %s: %s: Line: %d ipr_cfg: %p\n", __FILE__, \
__FUNCTION__, __LINE__, ipr_cfg

#if defined(CONFIG_KDB) && !defined(CONFIG_PPC_ISERIES)
#define ipr_breakpoint {printk(ipr_breakpoint_data); KDB_ENTER();}
#define ipr_breakpoint_or_die {printk(ipr_breakpoint_data); KDB_ENTER();}
#else
#define ipr_breakpoint
#define ipr_breakpoint_or_die panic(ipr_breakpoint_data)
#endif

/*
 * Error logging macros
 */
#define ipr_err(...) printk(KERN_ERR IPR_NAME ": "__VA_ARGS__);
#define ipr_info(...) printk(KERN_INFO IPR_NAME ": "__VA_ARGS__);
#define ipr_crit(...) printk(KERN_CRIT IPR_NAME ": "__VA_ARGS__);
#define ipr_warn(...) printk(KERN_WARNING IPR_NAME": "__VA_ARGS__);
#define ipr_dbg(...) IPR_DBG_CMD(printk(KERN_DEBUG IPR_NAME ": "__VA_ARGS__));

#define ipr_sdev_printk(level, sdev, fmt, ...) \
	printk(level IPR_NAME ": %d:%d:%d:%d: " fmt, sdev->host->host_no, \
		sdev->channel, sdev->id, sdev->lun, ##__VA_ARGS__)

#define ipr_sdev_err(sdev, fmt, ...) \
	ipr_sdev_printk(KERN_ERR, sdev, fmt, ##__VA_ARGS__)

#define ipr_sdev_info(sdev, fmt, ...) \
	ipr_sdev_printk(KERN_INFO, sdev, fmt, ##__VA_ARGS__)

#define ipr_res_printk(level, ioa_cfg, res, fmt, ...) \
	printk(level IPR_NAME ": %d:%d:%d:%d: " fmt, ioa_cfg->host->host_no, \
		res.bus, res.target, res.lun, ##__VA_ARGS__)

#define ipr_res_err(ioa_cfg, res, fmt, ...) \
	ipr_res_printk(KERN_ERR, ioa_cfg, res, fmt, ##__VA_ARGS__);
#define ipr_res_dbg(ioa_cfg, res, fmt, ...) \
	ipr_res_printk(KERN_DEBUG, ioa_cfg, res, fmt, ##__VA_ARGS__);

#define ipr_trace ipr_dbg("%s: %s: Line: %d\n",\
	__FILE__, __FUNCTION__, __LINE__);

#if IPR_DBG_TRACE
#define ENTER printk(KERN_DEBUG IPR_NAME": Entering %s\n", __FUNCTION__);
#define LEAVE printk(KERN_DEBUG IPR_NAME": Leaving %s\n", __FUNCTION__);
#else
#define ENTER
#define LEAVE
#endif

#define ipr_err_separator \
ipr_err("----------------------------------------------------------\n")


/*
 * Inlines
 */

/**
 * ipr_is_af_dasd_device - Determine if a resource is an AF DASD
 * @res:	resource entry struct
 *
 * Return value: 
 * 	1 if AF DASD / 0 if not AF DASD
 **/
static inline int ipr_is_af_dasd_device(struct ipr_resource_entry *res)
{
	if (IPR_IS_DASD_DEVICE(res->cfgte.std_inq_data) &&
	    (!res->cfgte.is_ioa_resource) &&
	    (res->cfgte.subtype == IPR_SUBTYPE_AF_DASD))
		return 1;
	else
		return 0;
}

/**
 * ipr_is_vset_device - Determine if a resource is a VSET
 * @res:	resource entry struct
 *
 * Return value: 
 * 	1 if VSET / 0 if not VSET
 **/
static inline int ipr_is_vset_device(struct ipr_resource_entry *res)
{
	if (IPR_IS_DASD_DEVICE(res->cfgte.std_inq_data) &&
	    (!res->cfgte.is_ioa_resource) &&
	    (res->cfgte.subtype == IPR_SUBTYPE_VOLUME_SET))
		return 1;
	else
		return 0;
}

/**
 * ipr_is_af - Determine if a resource is an advanced function resource
 * @res:	resource entry struct
 *
 * Return value: 
 * 	1 if AF / 0 if not AF
 **/
static inline int ipr_is_af(struct ipr_resource_entry *res)
{
	return (ipr_is_vset_device(res) | ipr_is_af_dasd_device(res));
}

/**
 * ipr_is_device - Determine if resource address is that of a device
 * @res_addr:	resource address struct
 *
 * Return value: 
 * 	1 if AF / 0 if not AF
 **/
static inline int ipr_is_device(struct ipr_res_addr *res_addr)
{
	if ((res_addr->bus < IPR_MAX_NUM_BUSES) &&
	    (res_addr->target < IPR_MAX_NUM_TARGETS_PER_BUS))
		return 1;

	return 0;
}

/**
 * ipr_sdt_is_fmt2 - Determine if a SDT address is in format 2
 * @sdt_word:	SDT address
 *
 * Return value: 
 * 	1 if format 2 / 0 if not
 **/
static inline int ipr_sdt_is_fmt2(u32 sdt_word)
{
	u32 bar_sel = IPR_GET_FMT2_BAR_SEL(sdt_word);

	switch (bar_sel) {
	case IPR_SDT_FMT2_BAR0_SEL:
	case IPR_SDT_FMT2_BAR1_SEL:
	case IPR_SDT_FMT2_BAR2_SEL:
	case IPR_SDT_FMT2_BAR3_SEL:
	case IPR_SDT_FMT2_BAR4_SEL:
	case IPR_SDT_FMT2_BAR5_SEL:
	case IPR_SDT_FMT2_EXP_ROM_SEL:
		return 1;
	};

	return 0;
}

#ifndef CONFIG_COMPAT
static inline int
register_ioctl32_conversion(unsigned int cmd,
			    int (*handler)(unsigned int, unsigned int, unsigned long, struct file *))
{ return 0; }

static inline int unregister_ioctl32_conversion(unsigned int cmd) { return 0; }
#endif

#endif
