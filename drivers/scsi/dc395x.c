/*
 * dc395x.c
 *
 * Device Driver for Tekram DC395(U/UW/F), DC315(U)
 * PCI SCSI Bus Master Host Adapter
 * (SCSI chip set used Tekram ASIC TRM-S1040)
 *
 * Authors:
 *  C.L. Huang <ching@tekram.com.tw>
 *  Erich Chen <erich@tekram.com.tw>
 *  (C) Copyright 1995-1999 Tekram Technology Co., Ltd.
 *
 *  Kurt Garloff <garloff@suse.de>
 *  (C) 1999-2000 Kurt Garloff
 *
 *  Oliver Neukum <oliver@neukum.name>
 *  Ali Akcaagac <aliakc@web.de>
 *  Jamie Lenehan <lenehan@twibble.org>
 *  (C) 2003
 *
 * License: GNU GPL
 *
 *************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/blkdev.h>
#include <asm/io.h>
#include "scsi.h"
#include "hosts.h"
#include "dc395x.h"
#include <scsi/scsicam.h>	/* needed for scsicam_bios_param */
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/list.h>

/*---------------------------------------------------------------------------
                                  Features
 ---------------------------------------------------------------------------*/
/*
 * Set to disable parts of the driver
 */
/*#define DC395x_NO_DISCONNECT*/
/*#define DC395x_NO_TAGQ*/
/*#define DC395x_NO_SYNC*/
/*#define DC395x_NO_WIDE*/

/*---------------------------------------------------------------------------
                                  Debugging
 ---------------------------------------------------------------------------*/
/*
 * Types of debugging that can be enabled and disabled
 */
#define DBG_KG		0x0001
#define DBG_0		0x0002
#define DBG_1		0x0004
#define DBG_DCB		0x0008
#define DBG_PARSE	0x0010		/* debug command line parsing */
#define DBG_SGPARANOIA	0x0020
#define DBG_FIFO	0x0040
#define DBG_PIO		0x0080
#define DBG_RECURSION	0x0100		/* check for excessive recursion */
#define DBG_MALLOC	0x0200		/* report on memory allocations */
#define DBG_TRACE	0x0400
#define DBG_TRACEALL	0x0800


/*
 * Set set of things to output debugging for.
 * Undefine to remove all debugging
 */
/*#define DEBUG_MASK (DBG_0|DBG_1|DBG_DCB|DBG_PARSE|DBG_SGPARANOIA|DBG_FIFO|DBG_PIO|DBG_TRACE|DBG_TRACEALL)*/
/*#define  DEBUG_MASK	DBG_0*/


/*
 * Output a kernel mesage at the specified level and append the
 * driver name and a ": " to the start of the message
 */
#define dprintkl(level, format, arg...)  \
    printk(level DC395X_NAME ": " format , ## arg)


#ifdef DEBUG_MASK
/*
 * print a debug message - this is formated with KERN_DEBUG, then the
 * driver name followed by a ": " and then the message is output. 
 * This also checks that the specified debug level is enabled before
 * outputing the message
 */
#define dprintkdbg(type, format, arg...) \
	do { \
		if ((type) & (DEBUG_MASK)) \
			dprintkl(KERN_DEBUG , format , ## arg); \
	} while (0)

/*
 * Check if the specified type of debugging is enabled
 */
#define debug_enabled(type)	((DEBUG_MASK) & (type))

#else
/*
 * No debugging. Do nothing
 */
#define dprintkdbg(type, format, arg...) \
	do {} while (0)
#define debug_enabled(type)	(0)

#endif


/*
 * The recursion debugging just counts entries into the driver and
 * prints out a messge if it exceeds a certain limit. This variable
 * hold the count.
 */
#if debug_enabled(DBG_RECURSION)
static int dbg_in_driver = 0;
#endif


/*
 * Memory allocation debugging
 * Just reports when memory is allocated and/or released.
 */
#if debug_enabled(DBG_MALLOC)
inline void *dc395x_kmalloc(size_t sz, int fl)
{
	void *ptr = kmalloc(sz, fl);
	dprintkl(KERN_DEBUG, "Alloc %i bytes @ %p w/ fl %08x\n", sz, ptr, fl);
	return ptr;
}
inline void dc395x_kfree(const void *adr)
{
	dprintkl(KERN_DEBUG, "Free mem @ %p\n", adr);
	kfree(adr);
}
#else
#define dc395x_kmalloc(sz, fl)	kmalloc(sz, fl)
#define dc395x_kfree(adr) kfree(adr)
#endif


/*
 * Debug/trace stuff
 */
#if debug_enabled(DBG_TRACEALL)
# define TRACEOUTALL(x...) printk ( x)
#else
# define TRACEOUTALL(x...) do {} while (0)
#endif

#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
# define DEBUGTRACEBUFSZ 512
static char tracebuf[64];
static char traceoverflow[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
# define TRACEPRINTF(x...) \
	do { \
		int ln = sprintf(tracebuf, x); \
		if (srb->debugpos + ln >= DEBUGTRACEBUFSZ) { \
			srb->debugtrace[srb->debugpos] = 0; \
			srb->debugpos = DEBUGTRACEBUFSZ/5; \
			srb->debugtrace[srb->debugpos++] = '>'; \
		} \
		sprintf(srb->debugtrace + srb->debugpos, "%s", tracebuf); \
		srb->debugpos += ln - 1; \
	} while (0)
# define TRACEOUT(x...) printk (x)
#else
# define TRACEPRINTF(x...) do {} while (0)
# define TRACEOUT(x...) do {} while (0)
#endif


/*---------------------------------------------------------------------------
 ---------------------------------------------------------------------------*/

#ifndef PCI_VENDOR_ID_TEKRAM
#define PCI_VENDOR_ID_TEKRAM                    0x1DE1	/* Vendor ID    */
#endif
#ifndef PCI_DEVICE_ID_TEKRAM_TRMS1040
#define PCI_DEVICE_ID_TEKRAM_TRMS1040           0x0391	/* Device ID    */
#endif



#define DC395x_LOCK_IO(dev,flags)		spin_lock_irqsave(((struct Scsi_Host *)dev)->host_lock, flags)
#define DC395x_UNLOCK_IO(dev,flags)		spin_unlock_irqrestore(((struct Scsi_Host *)dev)->host_lock, flags)

#define DC395x_ACB_INITLOCK(acb)		spin_lock_init(&acb->smp_lock)
#define DC395x_ACB_LOCK(acb,acb_flags)		if (!acb->lock_level_count[cpuid]) { spin_lock_irqsave(&acb->smp_lock,acb_flags); acb->lock_level_count[cpuid]++; } else { acb->lock_level_count[cpuid]++; }
#define DC395x_ACB_UNLOCK(acb,acb_flags)	if (--acb->lock_level_count[cpuid] == 0) { spin_unlock_irqrestore(&acb->smp_lock,acb_flags); }

#define DC395x_SMP_IO_LOCK(dev,irq_flags)	spin_lock_irqsave(((struct Scsi_Host*)dev)->host_lock,irq_flags)
#define DC395x_SMP_IO_UNLOCK(dev,irq_flags)	spin_unlock_irqrestore(((struct Scsi_Host*)dev)->host_lock,irq_flags)


#define DC395x_read8(acb,address)		(u8)(inb(acb->io_port_base + (address)))
#define DC395x_read8_(address, base)		(u8)(inb((USHORT)(base) + (address)))
#define DC395x_read16(acb,address)		(u16)(inw(acb->io_port_base + (address)))
#define DC395x_read32(acb,address)		(u32)(inl(acb->io_port_base + (address)))
#define DC395x_write8(acb,address,value)	outb((value), acb->io_port_base + (address))
#define DC395x_write8_(address,value,base)	outb((value), (USHORT)(base) + (address))
#define DC395x_write16(acb,address,value)	outw((value), acb->io_port_base + (address))
#define DC395x_write32(acb,address,value)	outl((value), acb->io_port_base + (address))


#define BUS_ADDR(sg)		sg_dma_address(&(sg))
#define CPU_ADDR(sg)		(page_address((sg).page)+(sg).offset)
#define PAGE_ADDRESS(sg)	page_address((sg)->page)

/* cmd->result */
#define RES_TARGET		0x000000FF	/* Target State */
#define RES_TARGET_LNX  STATUS_MASK	/* Only official ... */
#define RES_ENDMSG		0x0000FF00	/* End Message */
#define RES_DID			0x00FF0000	/* DID_ codes */
#define RES_DRV			0xFF000000	/* DRIVER_ codes */

#define MK_RES(drv,did,msg,tgt) ((int)(drv)<<24 | (int)(did)<<16 | (int)(msg)<<8 | (int)(tgt))
#define MK_RES_LNX(drv,did,msg,tgt) ((int)(drv)<<24 | (int)(did)<<16 | (int)(msg)<<8 | (int)(tgt)<<1)

#define SET_RES_TARGET(who,tgt) { who &= ~RES_TARGET; who |= (int)(tgt); }
#define SET_RES_TARGET_LNX(who,tgt) { who &= ~RES_TARGET_LNX; who |= (int)(tgt) << 1; }
#define SET_RES_MSG(who,msg) { who &= ~RES_ENDMSG; who |= (int)(msg) << 8; }
#define SET_RES_DID(who,did) { who &= ~RES_DID; who |= (int)(did) << 16; }
#define SET_RES_DRV(who,drv) { who &= ~RES_DRV; who |= (int)(drv) << 24; }

/*
**************************************************************************
*/
#define TAG_NONE 255

struct SGentry {
	u32 address;		/* bus! address */
	u32 length;
};


/*
 * The SEEPROM structure for TRM_S1040 
 */
struct NVRamTarget {
	u8 cfg0;		/* Target configuration byte 0  */
	u8 period;		/* Target period                */
	u8 cfg2;		/* Target configuration byte 2  */
	u8 cfg3;		/* Target configuration byte 3  */
};


struct NvRamType {
	u8 sub_vendor_id[2];	/* 0,1  Sub Vendor ID   */
	u8 sub_sys_id[2];	/* 2,3  Sub System ID   */
	u8 sub_class;		/* 4    Sub Class       */
	u8 vendor_id[2];	/* 5,6  Vendor ID       */
	u8 device_id[2];	/* 7,8  Device ID       */
	u8 reserved;		/* 9    Reserved        */
	struct NVRamTarget target[DC395x_MAX_SCSI_ID];
						/** 10,11,12,13
						 ** 14,15,16,17
						 ** ....
						 ** ....
						 ** 70,71,72,73
						 */
	u8 scsi_id;		/* 74 Host Adapter SCSI ID      */
	u8 channel_cfg;		/* 75 Channel configuration     */
	u8 delay_time;		/* 76 Power on delay time       */
	u8 max_tag;		/* 77 Maximum tags              */
	u8 reserved0;		/* 78  */
	u8 boot_target;		/* 79  */
	u8 boot_lun;		/* 80  */
	u8 reserved1;		/* 81  */
	u16 reserved2[22];	/* 82,..125 */
	u16 cksum;		/* 126,127 */
};


/*-----------------------------------------------------------------------
  SCSI Request Block
  -----------------------------------------------------------------------*/
struct ScsiReqBlk {
	struct list_head list;		/* next/prev ptrs for srb lists */
	struct DeviceCtlBlk *dcb;

	/* HW scatter list (up to 64 entries) */
	struct SGentry *segment_x;
	Scsi_Cmnd *cmd;

	unsigned char *virt_addr;	/* set by update_sg_list */

	u32 total_xfer_length;
	u32 xferred;		/* Backup for the already xferred len */

	u32 sg_bus_addr;	/* bus address of DC395x scatterlist */

	u16 state;
	u8 sg_count;
	u8 sg_index;

	u8 msgin_buf[6];
	u8 msgout_buf[6];

	u8 adapter_status;
	u8 target_status;
	u8 msg_count;
	u8 end_message;

	u8 tag_number;
	u8 status;
	u8 retry_count;
	u8 flag;

	u8 scsi_phase;

#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	u16 debugpos;
	char *debugtrace;
#endif
};


/*-----------------------------------------------------------------------
  Device Control Block
  -----------------------------------------------------------------------*/
struct DeviceCtlBlk {
	struct list_head list;		/* next/prev ptrs for the dcb list */
	struct AdapterCtlBlk *acb;
	struct list_head srb_going_list;	/* head of going srb list */
	struct list_head srb_waiting_list;	/* head of waiting srb list */

	struct ScsiReqBlk *active_srb;
	u32 tag_mask;

	u16 max_command;

	u8 target_id;		/* SCSI Target ID  (SCSI Only) */
	u8 target_lun;		/* SCSI Log.  Unit (SCSI Only) */
	u8 identify_msg;
	u8 dev_mode;

	u8 inquiry7;		/* To store Inquiry flags */
	u8 sync_mode;		/* 0:async mode */
	u8 min_nego_period;	/* for nego. */
	u8 sync_period;		/* for reg.  */

	u8 sync_offset;		/* for reg. and nego.(low nibble) */
	u8 flag;
	u8 dev_type;
	u8 init_tcq_flag;
};

/*-----------------------------------------------------------------------
  Adapter Control Block
  -----------------------------------------------------------------------*/
struct AdapterCtlBlk {
	struct Scsi_Host *scsi_host;

	u16 io_port_base;
	u16 io_port_len;

	struct list_head dcb_list;		/* head of going dcb list */
	struct DeviceCtlBlk *dcb_run_robin;
	struct DeviceCtlBlk *active_dcb;

	struct list_head srb_free_list;		/* head of free srb list */
	struct ScsiReqBlk *tmp_srb;
	struct timer_list waiting_timer;
	struct timer_list selto_timer;

	u16 srb_count;

	u8 sel_timeout;

	u8 irq_level;
	u8 tag_max_num;
	u8 acb_flag;
	u8 gmode2;

	u8 config;
	u8 lun_chk;
	u8 scan_devices;
	u8 hostid_bit;

	u8 dcb_map[DC395x_MAX_SCSI_ID];
	struct DeviceCtlBlk *children[DC395x_MAX_SCSI_ID][32];

	struct pci_dev *dev;

	u8 msg_len;

	struct ScsiReqBlk srb_array[DC395x_MAX_SRB_CNT];
	struct ScsiReqBlk srb;

	struct NvRamType eeprom;	/* eeprom settings for this adapter */
};




/*---------------------------------------------------------------------------
                            Forward declarations
 ---------------------------------------------------------------------------*/
static void data_out_phase0(struct AdapterCtlBlk *acb,
			    struct ScsiReqBlk *srb,
			    u16 * pscsi_status);
static void data_in_phase0(struct AdapterCtlBlk *acb,
			   struct ScsiReqBlk *srb,
			   u16 * pscsi_status);
static void command_phase0(struct AdapterCtlBlk *acb,
			   struct ScsiReqBlk *srb,
			   u16 * pscsi_status);
static void status_phase0(struct AdapterCtlBlk *acb,
			  struct ScsiReqBlk *srb,
			  u16 * pscsi_status);
static void msgout_phase0(struct AdapterCtlBlk *acb,
			  struct ScsiReqBlk *srb,
			  u16 * pscsi_status);
static void msgin_phase0(struct AdapterCtlBlk *acb,
			 struct ScsiReqBlk *srb,
			 u16 * pscsi_status);
static void data_out_phase1(struct AdapterCtlBlk *acb,
			    struct ScsiReqBlk *srb,
			    u16 * pscsi_status);
static void data_in_phase1(struct AdapterCtlBlk *acb,
			   struct ScsiReqBlk *srb,
			   u16 * pscsi_status);
static void command_phase1(struct AdapterCtlBlk *acb,
			   struct ScsiReqBlk *srb,
			   u16 * pscsi_status);
static void status_phase1(struct AdapterCtlBlk *acb,
			  struct ScsiReqBlk *srb,
			  u16 * pscsi_status);
static void msgout_phase1(struct AdapterCtlBlk *acb,
			  struct ScsiReqBlk *srb,
			  u16 * pscsi_status);
static void msgin_phase1(struct AdapterCtlBlk *acb,
			 struct ScsiReqBlk *srb,
			 u16 * pscsi_status);
static void nop0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		 u16 * pscsi_status);
static void nop1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		 u16 * pscsi_status);
static void set_basic_config(struct AdapterCtlBlk *acb);
static void cleanup_after_transfer(struct AdapterCtlBlk *acb,
				   struct ScsiReqBlk *srb);
static void reset_scsi_bus(struct AdapterCtlBlk *acb);
static void data_io_transfer(struct AdapterCtlBlk *acb,
			     struct ScsiReqBlk *srb, u16 io_dir);
static void disconnect(struct AdapterCtlBlk *acb);
static void reselect(struct AdapterCtlBlk *acb);
static u8 start_scsi(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		     struct ScsiReqBlk *srb);
static void build_srb(Scsi_Cmnd * cmd, struct DeviceCtlBlk *dcb,
		      struct ScsiReqBlk *srb);
static void doing_srb_done(struct AdapterCtlBlk *acb, u8 did_code,
			   Scsi_Cmnd * cmd, u8 force);
static void scsi_reset_detect(struct AdapterCtlBlk *acb);
static void pci_unmap_srb(struct AdapterCtlBlk *acb,
			  struct ScsiReqBlk *srb);
static void pci_unmap_srb_sense(struct AdapterCtlBlk *acb,
				struct ScsiReqBlk *srb);
static inline void enable_msgout_abort(struct AdapterCtlBlk *acb,
				       struct ScsiReqBlk *srb);
static void srb_done(struct AdapterCtlBlk *acb,
		     struct DeviceCtlBlk *dcb,
		     struct ScsiReqBlk *srb);
static void request_sense(struct AdapterCtlBlk *acb,
			  struct DeviceCtlBlk *dcb,
			  struct ScsiReqBlk *srb);
static inline void set_xfer_rate(struct AdapterCtlBlk *acb,
				 struct DeviceCtlBlk *dcb);
static void waiting_timeout(unsigned long ptr);


/*---------------------------------------------------------------------------
                                 Static Data
 ---------------------------------------------------------------------------*/
static u16 current_sync_offset = 0;
static char monitor_next_irq = 0;

/* 
 * dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 */
static void *dc395x_scsi_phase0[] = {
	data_out_phase0,/* phase:0 */
	data_in_phase0,	/* phase:1 */
	command_phase0,	/* phase:2 */
	status_phase0,	/* phase:3 */
	nop0,		/* phase:4 PH_BUS_FREE .. initial phase */
	nop0,		/* phase:5 PH_BUS_FREE .. initial phase */
	msgout_phase0,	/* phase:6 */
	msgin_phase0,	/* phase:7 */
};

/*
 * dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 */
static void *dc395x_scsi_phase1[] = {
	data_out_phase1,/* phase:0 */
	data_in_phase1,	/* phase:1 */
	command_phase1,	/* phase:2 */
	status_phase1,	/* phase:3 */
	nop1,		/* phase:4 PH_BUS_FREE .. initial phase */
	nop1,		/* phase:5 PH_BUS_FREE .. initial phase */
	msgout_phase1,	/* phase:6 */
	msgin_phase1,	/* phase:7 */
};

/*
 *Fast20:	000	 50ns, 20.0 MHz
 *		001	 75ns, 13.3 MHz
 *		010	100ns, 10.0 MHz
 *		011	125ns,  8.0 MHz
 *		100	150ns,  6.6 MHz
 *		101	175ns,  5.7 MHz
 *		110	200ns,  5.0 MHz
 *		111	250ns,  4.0 MHz
 *
 *Fast40(LVDS):	000	 25ns, 40.0 MHz
 *		001	 50ns, 20.0 MHz
 *		010	 75ns, 13.3 MHz
 *		011	100ns, 10.0 MHz
 *		100	125ns,  8.0 MHz
 *		101	150ns,  6.6 MHz
 *		110	175ns,  5.7 MHz
 *		111	200ns,  5.0 MHz
 */
/*static u8	clock_period[] = {12,19,25,31,37,44,50,62};*/

/* real period:48ns,76ns,100ns,124ns,148ns,176ns,200ns,248ns */
static u8 clock_period[] = { 12, 18, 25, 31, 37, 43, 50, 62 };
static u16 clock_speed[] = { 200, 133, 100, 80, 67, 58, 50, 40 };
/* real period:48ns,72ns,100ns,124ns,148ns,172ns,200ns,248ns */



/*---------------------------------------------------------------------------
                                Configuration
  ---------------------------------------------------------------------------*/
/*
 * Module/boot parameters currently effect *all* instances of the
 * card in the system.
 */

/*
 * Command line parameters are stored in a structure below.
 * These are the index's into the structure for the various
 * command line options.
 */
#define CFG_ADAPTER_ID		0
#define CFG_MAX_SPEED		1
#define CFG_DEV_MODE		2
#define CFG_ADAPTER_MODE	3
#define CFG_TAGS		4
#define CFG_RESET_DELAY		5

#define CFG_NUM			6	/* number of configuration items */


/*
 * Value used to indicate that a command line override
 * hasn't been used to modify the value.
 */
#define CFG_PARAM_UNSET -1


/*
 * Hold command line parameters.
 */
struct ParameterData {
	int value;		/* value of this setting */
	int min;		/* minimum value */
	int max;		/* maximum value */
	int def;		/* default value */
	int safe;		/* safe value */
};
static struct ParameterData __initdata cfg_data[] = {
	{ /* adapter id */
		CFG_PARAM_UNSET,
		0,
		15,
		7,
		7
	},
	{ /* max speed */
		CFG_PARAM_UNSET,
		  0,
		  7,
		  1,	/* 13.3Mhz */
		  4,	/*  6.7Hmz */
	},
	{ /* dev mode */
		CFG_PARAM_UNSET,
		0,
		0x3f,
		NTC_DO_PARITY_CHK | NTC_DO_DISCONNECT | NTC_DO_SYNC_NEGO |
			NTC_DO_WIDE_NEGO | NTC_DO_TAG_QUEUEING |
			NTC_DO_SEND_START,
		NTC_DO_PARITY_CHK | NTC_DO_SEND_START
	},
	{ /* adapter mode */
		CFG_PARAM_UNSET,
		0,
		0x2f,
#ifdef CONFIG_SCSI_MULTI_LUN
			NAC_SCANLUN |
#endif
		NAC_GT2DRIVES | NAC_GREATER_1G | NAC_POWERON_SCSI_RESET
			/*| NAC_ACTIVE_NEG*/,
		NAC_GT2DRIVES | NAC_GREATER_1G | NAC_POWERON_SCSI_RESET | 0x08
	},
	{ /* tags */
		CFG_PARAM_UNSET,
		0,
		5,
		3,	/* 16 tags (??) */
		2,
	},
	{ /* reset delay */
		CFG_PARAM_UNSET,
		0,
		180,
		1,	/* 1 second */
		10,	/* 10 seconds */
	}
};


/*
 * Safe settings. If set to zero the the BIOS/default values with command line
 * overrides will be used. If set to 1 then safe and slow settings will be used.
 */
static int use_safe_settings = 0;
module_param_named(safe, use_safe_settings, bool, 0);
MODULE_PARM_DESC(safe, "Use safe and slow settings only. Default: false");


module_param_named(adapter_id, cfg_data[CFG_ADAPTER_ID].value, int, 0);
MODULE_PARM_DESC(adapter_id, "Adapter SCSI ID. Default 7 (0-15)");

module_param_named(max_speed, cfg_data[CFG_MAX_SPEED].value, int, 0);
MODULE_PARM_DESC(max_speed, "Maximum bus speed. Default 1 (0-7) Speeds: 0=20, 1=13.3, 2=10, 3=8, 4=6.7, 5=5.8, 6=5, 7=4 Mhz");

module_param_named(dev_mode, cfg_data[CFG_DEV_MODE].value, int, 0);
MODULE_PARM_DESC(dev_mode, "Device mode.");

module_param_named(adapter_mode, cfg_data[CFG_ADAPTER_MODE].value, int, 0);
MODULE_PARM_DESC(adapter_mode, "Adapter mode.");

module_param_named(tags, cfg_data[CFG_TAGS].value, int, 0);
MODULE_PARM_DESC(tags, "Number of tags (1<<x). Default 3 (0-5)");

module_param_named(reset_delay, cfg_data[CFG_RESET_DELAY].value, int, 0);
MODULE_PARM_DESC(reset_delay, "Reset delay in seconds. Default 1 (0-180)");


/**
 * set_safe_settings - if the use_safe_settings option is set then
 * set all values to the safe and slow values.
 **/
static
void __init set_safe_settings(void)
{
	if (use_safe_settings)
	{
		int i;

		dprintkl(KERN_INFO, "Using safe settings.\n");
		for (i = 0; i < CFG_NUM; i++)
		{
			cfg_data[i].value = cfg_data[i].safe;
		}
	}
}


/**
 * fix_settings - reset any boot parameters which are out of range
 * back to the default values.
 **/
static
void __init fix_settings(void)
{
	int i;

	dprintkdbg(DBG_PARSE, "setup %08x %08x %08x %08x %08x %08x\n",
		    cfg_data[CFG_ADAPTER_ID].value,
		    cfg_data[CFG_MAX_SPEED].value,
		    cfg_data[CFG_DEV_MODE].value,
		    cfg_data[CFG_ADAPTER_MODE].value,
		    cfg_data[CFG_TAGS].value,
		    cfg_data[CFG_RESET_DELAY].value);
	for (i = 0; i < CFG_NUM; i++)
	{
		if (cfg_data[i].value < cfg_data[i].min ||
			cfg_data[i].value > cfg_data[i].max)
		{
			cfg_data[i].value = cfg_data[i].def;
		}
	}
}



/*
 * Mapping from the eeprom delay index value (index into this array)
 * to the the number of actual seconds that the delay should be for.
 */
static
char __initdata eeprom_index_to_delay_map[] = { 1, 3, 5, 10, 16, 30, 60, 120 };


/**
 * eeprom_index_to_delay - Take the eeprom delay setting and convert it
 * into a number of seconds.
 *
 * @eeprom: The eeprom structure in which we find the delay index to map.
 **/
static
void __init eeprom_index_to_delay(struct NvRamType *eeprom)
{
	eeprom->delay_time = eeprom_index_to_delay_map[eeprom->delay_time];
}


/**
 * delay_to_eeprom_index - Take a delay in seconds and return the closest
 * eeprom index which will delay for at least that amount of seconds.
 *
 * @delay: The delay, in seconds, to find the eeprom index for.
 **/
static int __init delay_to_eeprom_index(int delay)
{
	u8 idx = 0;
	while (idx < 7 && eeprom_index_to_delay_map[idx] < delay) {
		idx++;
	}
	return idx;
}


/**
 * eeprom_override - Override the eeprom settings, in the provided
 * eeprom structure, with values that have been set on the command
 * line.
 *
 * @eeprom: The eeprom data to override with command line options.
 **/
static
void __init eeprom_override(struct NvRamType *eeprom)
{
	u8 id;

	/* Adapter Settings */
	if (cfg_data[CFG_ADAPTER_ID].value != CFG_PARAM_UNSET) {
		eeprom->scsi_id =
		    (u8)cfg_data[CFG_ADAPTER_ID].value;
	}
	if (cfg_data[CFG_ADAPTER_MODE].value != CFG_PARAM_UNSET) {
		eeprom->channel_cfg =
		    (u8)cfg_data[CFG_ADAPTER_MODE].value;
	}
	if (cfg_data[CFG_RESET_DELAY].value != CFG_PARAM_UNSET) {
		eeprom->delay_time =
		    delay_to_eeprom_index(cfg_data[CFG_RESET_DELAY].value);
	}
	if (cfg_data[CFG_TAGS].value != CFG_PARAM_UNSET) {
		eeprom->max_tag = (u8)cfg_data[CFG_TAGS].value;
	}

	/* Device Settings */
	for (id = 0; id < DC395x_MAX_SCSI_ID; id++) {
		if (cfg_data[CFG_DEV_MODE].value != CFG_PARAM_UNSET) {
			eeprom->target[id].cfg0 =
			    (u8)cfg_data[CFG_DEV_MODE].value;
		}
		if (cfg_data[CFG_MAX_SPEED].value != CFG_PARAM_UNSET) {
			eeprom->target[id].period =
			    (u8)cfg_data[CFG_MAX_SPEED].value;
		}
	}
}


/*---------------------------------------------------------------------------
 ---------------------------------------------------------------------------*/

/**
 * list_size - Returns the size (in number of entries) of the
 * supplied list.
 *
 * @head: The pointer to the head of the list to count the items in.
 **/
static
unsigned int list_size(struct list_head *head)
{
	unsigned int count = 0;
	struct list_head *pos;
	list_for_each(pos, head)
		count++;
	return count;
}                                                                                        


/**
 * dcb_get_next - Given a dcb return the next dcb in the list of
 * dcb's, wrapping back to the start of the dcb list if required.
 * Returns the supplied dcb if there is only one dcb in the list.
 *
 * @head: The pointer to the head of the list to count the items in.
 * @pos: The pointer the dcb for which we are searching for the
 *       following dcb.
 **/
static
struct DeviceCtlBlk *dcb_get_next(
		struct list_head *head,
		struct DeviceCtlBlk *pos)
{
	int use_next = 0;
	struct DeviceCtlBlk* next = NULL;	
	struct DeviceCtlBlk* i;

	if (list_empty(head))
		return NULL;

	/* find supplied dcb and then select the next one */
	list_for_each_entry(i, head, list)
		if (use_next) {
			next = i;
			break;
		} else if (i == pos) {
			use_next = 1;
		}
	/* if no next one take the head one (ie, wraparound) */
	if (!next)
        	list_for_each_entry(i, head, list) {
        		next = i;
        		break;
        	}

	return next;
}


/*
 * Queueing philosphy:
 * There are a couple of lists:
 * - Waiting: Contains a list of SRBs not yet sent (per DCB)
 * - Free: List of free SRB slots
 * 
 * If there are no waiting commands for the DCB, the new one is sent to the bus
 * otherwise the oldest one is taken from the Waiting list and the new one is 
 * queued to the Waiting List
 * 
 * Lists are managed using two pointers and eventually a counter
 */

/* Nomen est omen ... */
static inline
void free_tag(struct DeviceCtlBlk *dcb, struct ScsiReqBlk *srb)
{
	if (srb->tag_number < 255) {
		dcb->tag_mask &= ~(1 << srb->tag_number);	/* free tag mask */
		srb->tag_number = 255;
	}
}


/* Find cmd in SRB list */
inline static
struct ScsiReqBlk *find_cmd(Scsi_Cmnd *cmd,
			    struct list_head *head)
{
	struct ScsiReqBlk *i;
	list_for_each_entry(i, head, list)
		if (i->cmd == cmd)
			return i;
	return NULL;
}


/*
 * srb_get_free - Return a free srb from the list of free SRBs that
 * is stored with the acb.
 */
static
struct ScsiReqBlk *srb_get_free(struct AdapterCtlBlk *acb)
{
	struct list_head *head = &acb->srb_free_list;
	struct ScsiReqBlk *srb;

	if (!list_empty(head)) {
		srb = list_entry(head->next, struct ScsiReqBlk, list);
		list_del(head->next);
		dprintkdbg(DBG_0, "srb_get_free: got srb %p\n", srb);
	} else {
		srb = NULL;
		dprintkl(KERN_ERR, "Out of Free SRBs :-(\n");
	}
	return srb;
}


/*
 * srb_free_insert - Insert an srb to the head of the free list
 * stored in the acb.
 */
static
void srb_free_insert(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	dprintkdbg(DBG_0, "srb_free_insert: put srb %p\n", srb);
        list_add_tail(&srb->list, &acb->srb_free_list);
}


/*
 * srb_waiting_insert - Insert an srb to the head of the wiating list
 * stored in the dcb.
 */
static
void srb_waiting_insert(struct DeviceCtlBlk *dcb, struct ScsiReqBlk *srb)
{
	dprintkdbg(DBG_0, "srb_waiting_insert: srb %p cmd %li\n", srb, srb->cmd->pid);
        list_add(&srb->list, &dcb->srb_waiting_list);
}


/*
 * srb_waiting_append - Append an srb to the tail of the waiting list
 * stored in the dcb.
 */
static inline
void srb_waiting_append(struct DeviceCtlBlk *dcb, struct ScsiReqBlk *srb)
{
	dprintkdbg(DBG_0, "srb_waiting_append: srb %p cmd %li\n", srb, srb->cmd->pid);
        list_add_tail(&srb->list, &dcb->srb_waiting_list);
}


/*
 * srb_going_append - Append an srb to the tail of the going list
 * stored in the dcb.
 */
static inline
void srb_going_append(struct DeviceCtlBlk *dcb, struct ScsiReqBlk *srb)
{
	dprintkdbg(DBG_0, "srb_going_append: srb %p\n", srb);
        list_add_tail(&srb->list, &dcb->srb_going_list);
}



/*
 * srb_going_remove - Remove an srb from the going list stored in the
 * dcb.
 */
static
void srb_going_remove(struct DeviceCtlBlk *dcb,
		      struct ScsiReqBlk *srb)
{
	struct ScsiReqBlk *i;
	struct ScsiReqBlk *tmp;
	dprintkdbg(DBG_0, "srb_going_remove: srb %p\n", srb);

	list_for_each_entry_safe(i, tmp, &dcb->srb_going_list, list)
		if (i == srb) {
			list_del(&srb->list);
			break;
		}
}


/*
 * srb_waiting_remove - Remove an srb from the waiting list stored in the
 * dcb.
 */
static
void srb_waiting_remove(struct DeviceCtlBlk *dcb,
			struct ScsiReqBlk *srb)
{
	struct ScsiReqBlk *i;
	struct ScsiReqBlk *tmp;
	dprintkdbg(DBG_0, "srb_waiting_remove: srb %p\n", srb);

	list_for_each_entry_safe(i, tmp, &dcb->srb_waiting_list, list)
		if (i == srb) {
			list_del(&srb->list);
			break;
		}
}


/*
 * srb_going_to_waiting_move - Remove an srb from the going list in
 * the dcb and insert it at the head of the waiting list in the dcb.
 */
static
void srb_going_to_waiting_move(struct DeviceCtlBlk *dcb,
			       struct ScsiReqBlk *srb)
{
	dprintkdbg(DBG_0, "srb_going_waiting_move: srb %p, pid = %li\n", srb, srb->cmd->pid);
	list_move(&srb->list, &dcb->srb_waiting_list);
}


/*
 * srb_waiting_to_going_move - Remove an srb from the waiting list in
 * the dcb and insert it at the head of the going list in the dcb.
 */
static
void srb_waiting_to_going_move(struct DeviceCtlBlk *dcb,
			       struct ScsiReqBlk *srb)
{
	/* Remove from waiting list */
	dprintkdbg(DBG_0, "srb_waiting_to_going: srb %p\n", srb);
	TRACEPRINTF("WtG *");
	list_move(&srb->list, &dcb->srb_going_list);
}


/* Sets the timer to wake us up */
static
void waiting_set_timer(struct AdapterCtlBlk *acb, unsigned long to)
{
	if (timer_pending(&acb->waiting_timer))
		return;
	init_timer(&acb->waiting_timer);
	acb->waiting_timer.function = waiting_timeout;
	acb->waiting_timer.data = (unsigned long) acb;
	if (time_before(jiffies + to, acb->scsi_host->last_reset - HZ / 2))
		acb->waiting_timer.expires =
		    acb->scsi_host->last_reset - HZ / 2 + 1;
	else
		acb->waiting_timer.expires = jiffies + to + 1;
	add_timer(&acb->waiting_timer);
}


/* Send the next command from the waiting list to the bus */
static
void waiting_process_next(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *start = NULL;
	struct DeviceCtlBlk *pos;
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	struct list_head *dcb_list_head = &acb->dcb_list;

	if ((acb->active_dcb)
	    || (acb->acb_flag & (RESET_DETECT + RESET_DONE + RESET_DEV)))
		return;

	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);

	if (list_empty(dcb_list_head))
		return;

	/*
	 * Find the starting dcb. Need to find it again in the list
	 * since the list may have changed since we set the ptr to it
	 */
	list_for_each_entry(dcb, dcb_list_head, list)
		if (dcb == acb->dcb_run_robin) {
			start = dcb;
			break;
		}
	if (!start) {
		/* This can happen! */
		start = list_entry(dcb_list_head->next, typeof(*start), list);
		acb->dcb_run_robin = start;
	}


	/*
	 * Loop over the dcb, but we start somewhere (potentially) in
	 * the middle of the loop so we need to manully do this.
	 */
	pos = start;
	do {
		struct list_head *waiting_list_head = &pos->srb_waiting_list;

		/* Make sure, the next another device gets scheduled ... */
		acb->dcb_run_robin = dcb_get_next(dcb_list_head,
						  acb->dcb_run_robin);

		if (list_empty(waiting_list_head) ||
		    pos->max_command <= list_size(&pos->srb_going_list)) {
			/* move to next dcb */
			pos = dcb_get_next(dcb_list_head, pos);
		} else {
			srb = list_entry(waiting_list_head->next,
					 struct ScsiReqBlk, list);

			/* Try to send to the bus */
			if (!start_scsi(acb, pos, srb))
				srb_waiting_to_going_move(pos, srb);
			else
				waiting_set_timer(acb, HZ/50);
			break;
		}
	} while (pos != start);
}


/* Wake up waiting queue */
static void waiting_timeout(unsigned long ptr)
{
	unsigned long flags;
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *) ptr;
	dprintkdbg(DBG_KG, "Debug: Waiting queue woken up by timer.\n");
	DC395x_LOCK_IO(acb->scsi_host, flags);
	waiting_process_next(acb);
	DC395x_UNLOCK_IO(acb->scsi_host, flags);
}


/* Get the DCB for a given ID/LUN combination */
static inline
struct DeviceCtlBlk *find_dcb(struct AdapterCtlBlk *acb, u8 id, u8 lun)
{
	return acb->children[id][lun];
}


/***********************************************************************
 * Function: static void send_srb (struct AdapterCtlBlk* acb, struct ScsiReqBlk* srb)
 *
 * Purpose: Send SCSI Request Block (srb) to adapter (acb)
 *
 *            dc395x_queue_command
 *            waiting_process_next
 *
 ***********************************************************************/
static
void send_srb(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb;

	dcb = srb->dcb;
	if (dcb->max_command <= list_size(&dcb->srb_going_list) ||
	    acb->active_dcb ||
	    (acb->acb_flag & (RESET_DETECT + RESET_DONE + RESET_DEV))) {
		srb_waiting_append(dcb, srb);
		waiting_process_next(acb);
		return;
	}

	if (!start_scsi(acb, dcb, srb)) {
		srb_going_append(dcb, srb);
	} else {
		srb_waiting_insert(dcb, srb);
		waiting_set_timer(acb, HZ / 50);
	}
}


/*
 *********************************************************************
 *
 * Function: static void build_srb (Scsi_Cmd *cmd, struct DeviceCtlBlk* dcb, struct ScsiReqBlk* srb)
 *
 *  Purpose: Prepare SRB for being sent to Device DCB w/ command *cmd
 *
 *********************************************************************
 */
static
void build_srb(Scsi_Cmnd * cmd, struct DeviceCtlBlk *dcb,
	       struct ScsiReqBlk *srb)
{
	int i, max;
	struct SGentry *sgp;
	struct scatterlist *sl;
	u32 request_size;
	int dir;

	dprintkdbg(DBG_0, "build_srb..............\n");
	/*memset (srb, 0, sizeof (struct ScsiReqBlk)); */
	srb->dcb = dcb;
	srb->cmd = cmd;
	/* Find out about direction */
	dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);

	if (cmd->use_sg && dir != PCI_DMA_NONE) {
		unsigned int len = 0;
		/* TODO: In case usg_sg and the no of segments differ, things
		 * will probably go wrong. */
		max = srb->sg_count =
		    pci_map_sg(dcb->acb->dev,
			       (struct scatterlist *) cmd->request_buffer,
			       cmd->use_sg, dir);
		sgp = srb->segment_x;
		request_size = cmd->request_bufflen;
		dprintkdbg(DBG_SGPARANOIA, 
		       "BuildSRB: Bufflen = %d, buffer = %p, use_sg = %d\n",
		       cmd->request_bufflen, cmd->request_buffer,
		       cmd->use_sg);
		dprintkdbg(DBG_SGPARANOIA, 
		       "Mapped %i Segments to %i\n", cmd->use_sg,
		       srb->sg_count);
		sl = (struct scatterlist *) cmd->request_buffer;

		srb->virt_addr = page_address(sl->page);
		for (i = 0; i < max; i++) {
			u32 busaddr = (u32) sg_dma_address(&sl[i]);
			u32 seglen = (u32) sl[i].length;
			sgp[i].address = busaddr;
			sgp[i].length = seglen;
			len += seglen;
			dprintkdbg(DBG_SGPARANOIA,
			       "Setting up sgp %d, address = 0x%08x, length = %d, tot len = %d\n",
			       i, busaddr, seglen, len);
		}
		sgp += max - 1;
		/* Fixup for last buffer too big as it is allocated on even page boundaries */
		if (len > request_size) {
#if debug_enabled(DBG_KG) || debug_enabled(DBG_SGPARANOIA)
			dprintkdbg(DBG_KG|DBG_SGPARANOIA,
			       "Fixup SG total length: %d->%d, last seg %d->%d\n",
			       len, request_size, sgp->length,
			       sgp->length - (len - request_size));
#endif
			sgp->length -= (len - request_size);
			len = request_size;
		}
		/* WIDE padding */
		if (dcb->sync_period & WIDE_SYNC && len % 2) {
			len++;
			sgp->length++;
		}
		srb->total_xfer_length = len;	/*? */
		/* Hopefully this does not cross a page boundary ... */
		srb->sg_bus_addr =
		    pci_map_single(dcb->acb->dev, srb->segment_x,
				   sizeof(struct SGentry) *
				   DC395x_MAX_SG_LISTENTRY,
				   PCI_DMA_TODEVICE);
		dprintkdbg(DBG_SGPARANOIA,
		       "Map SG descriptor list %p (%05x) to %08x\n",
		       srb->segment_x,
		       sizeof(struct SGentry) * DC395x_MAX_SG_LISTENTRY,
		       srb->sg_bus_addr);
	} else {
		if (cmd->request_buffer && dir != PCI_DMA_NONE) {
			u32 len = cmd->request_bufflen;	/* Actual request size */
			srb->sg_count = 1;
			srb->segment_x[0].address =
			    pci_map_single(dcb->acb->dev,
					   cmd->request_buffer, len, dir);
			/* WIDE padding */
			if (dcb->sync_period & WIDE_SYNC && len % 2)
				len++;
			srb->segment_x[0].length = len;
			srb->total_xfer_length = len;
			srb->virt_addr = cmd->request_buffer;
			srb->sg_bus_addr = 0;
			dprintkdbg(DBG_SGPARANOIA,
			       "BuildSRB: len = %d, buffer = %p, use_sg = %d, map %08x\n",
			       len, cmd->request_buffer, cmd->use_sg,
			       srb->segment_x[0].address);
		} else {
			srb->sg_count = 0;
			srb->total_xfer_length = 0;
			srb->sg_bus_addr = 0;
			srb->virt_addr = 0;
			dprintkdbg(DBG_SGPARANOIA,
			       "BuildSRB: buflen = %d, buffer = %p, use_sg = %d, NOMAP %08x\n",
			       cmd->bufflen, cmd->request_buffer,
			       cmd->use_sg, srb->segment_x[0].address);
		}
	}

	srb->sg_index = 0;
	srb->adapter_status = 0;
	srb->target_status = 0;
	srb->msg_count = 0;
	srb->status = 0;
	srb->flag = 0;
	srb->state = 0;
	srb->retry_count = 0;

#if debug_enabled(DBG_TRACE|DBG_TRACEALL) && debug_enabled(DBG_SGPARANOIA)
	if ((unsigned long)srb->debugtrace & (DEBUGTRACEBUFSZ - 1)) {
		dprintkdbg(DBG_SGPARANOIA,
			"SRB %i (%p): debugtrace %p corrupt!\n",
		       (srb - dcb->acb->srb_array) /
		       sizeof(struct ScsiReqBlk), srb, srb->debugtrace);
	}
#endif
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	srb->debugpos = 0;
	srb->debugtrace = 0;
#endif
	TRACEPRINTF("pid %li(%li):%02x %02x..(%i-%i) *", cmd->pid,
		    jiffies, cmd->cmnd[0], cmd->cmnd[1],
		    cmd->device->id, cmd->device->lun);
	srb->tag_number = TAG_NONE;

	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
	srb->end_message = 0;
	return;
}



/**
 * dc395x_queue_command - queue scsi command passed from the mid
 * layer, invoke 'done' on completion
 *
 * @cmd: pointer to scsi command object
 * @done: function pointer to be invoked on completion
 *
 * Returns 1 if the adapter (host) is busy, else returns 0. One
 * reason for an adapter to be busy is that the number
 * of outstanding queued commands is already equal to
 * struct Scsi_Host::can_queue .
 *
 * Required: if struct Scsi_Host::can_queue is ever non-zero
 *           then this function is required.
 *
 * Locks: struct Scsi_Host::host_lock held on entry (with "irqsave")
 *        and is expected to be held on return.
 *
 **/
static int
dc395x_queue_command(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	struct AdapterCtlBlk *acb =
	    (struct AdapterCtlBlk *)cmd->device->host->hostdata;

	dprintkdbg(DBG_0, "Queue Cmd=%02x,Tgt=%d,LUN=%d (pid=%li)\n",
		   cmd->cmnd[0],
		   cmd->device->id,
		   cmd->device->lun,
		   cmd->pid);

#if debug_enabled(DBG_RECURSION)
	if (dbg_in_driver++ > NORM_REC_LVL) {
		dprintkl(KERN_DEBUG,
			"%i queue_command () recursion? (pid=%li)\n",
			dbg_in_driver, cmd->pid);
	}
#endif

	/* Assume BAD_TARGET; will be cleared later */
	cmd->result = DID_BAD_TARGET << 16;

	/* ignore invalid targets */
	if (cmd->device->id >= acb->scsi_host->max_id ||
	    cmd->device->lun >= acb->scsi_host->max_lun ||
	    cmd->device->lun >31) {
		goto complete;
	}

	/* does the specified lun on the specified device exist */
	if (!(acb->dcb_map[cmd->device->id] & (1 << cmd->device->lun))) {
		dprintkl(KERN_INFO, "Ignore target %02x lun %02x\n", cmd->device->id,
		       cmd->device->lun);
		goto complete;
	}

	/* do we have a DCB for the device */
	dcb = find_dcb(acb, cmd->device->id, cmd->device->lun);
	if (!dcb) {
		/* should never happen */
		dprintkl(KERN_ERR, "no DCB failed, target %02x lun %02x\n",
				   cmd->device->id, cmd->device->lun);
		dprintkl(KERN_ERR, "No DCB in queuecommand (2)!\n");
		goto complete;
	}

	/* set callback and clear result in the command */
	cmd->scsi_done = done;
	cmd->result = 0;

	/* get a free SRB */
	srb = srb_get_free(acb);
	if (!srb)
	{
		/*
		 * Return 1 since we are unable to queue this command at this
		 * point in time.
		 */
		dprintkdbg(DBG_0, "No free SRB's in queuecommand\n");
		return 1;
	}

	/* build srb for the command */
	build_srb(cmd, dcb, srb);

	if (!list_empty(&dcb->srb_waiting_list)) {
		/* append to waiting queue */
		srb_waiting_append(dcb, srb);
		waiting_process_next(acb);
	} else {
		/* process immediately */
		send_srb(acb, srb);
	}
	dprintkdbg(DBG_1, "... command (pid %li) queued successfully.\n", cmd->pid);

#if debug_enabled(DBG_RECURSION)
	dbg_in_driver--
#endif
	return 0;

complete:
	/*
	 * Complete the command immediatey, and then return 0 to
	 * indicate that we have handled the command. This is usually
	 * done when the commad is for things like non existent
	 * devices.
	 */
#if debug_enabled(DBG_RECURSION)
		dbg_in_driver--
#endif
	done(cmd);
	return 0;
}




/*
 *********************************************************************
 *
 * Function   : dc395x_bios_param
 * Description: Return the disk geometry for the given SCSI device.
 *********************************************************************
 */
static
int dc395x_bios_param(struct scsi_device *sdev,
		      struct block_device *bdev,
		      sector_t capacity,
		      int *info)
{
#ifdef CONFIG_SCSI_DC395x_TRMS1040_TRADMAP
	int heads, sectors, cylinders;
	struct AdapterCtlBlk *acb;
	int size = capacity;

	dprintkdbg(DBG_0, "dc395x_bios_param..............\n");
	acb = (struct AdapterCtlBlk *) sdev->host->hostdata;
	heads = 64;
	sectors = 32;
	cylinders = size / (heads * sectors);

	if ((acb->gmode2 & NAC_GREATER_1G) && (cylinders > 1024)) {
		heads = 255;
		sectors = 63;
		cylinders = size / (heads * sectors);
	}
	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;
	return 0;
#else
	return scsicam_bios_param(bdev, capacity, info);
#endif
}


/*
 * DC395x register dump
 */
static
void dump_register_info(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
			struct ScsiReqBlk *srb)
{
	u16 pstat;
	struct pci_dev *dev = acb->dev;
	pci_read_config_word(dev, PCI_STATUS, &pstat);
	if (!dcb)
		dcb = acb->active_dcb;
	if (!srb && dcb)
		srb = dcb->active_srb;
	if (srb) {
		if (!(srb->cmd))
			dprintkl(KERN_INFO, "dump: SRB %p: cmd %p OOOPS!\n", srb,
			       srb->cmd);
		else
			dprintkl(KERN_INFO, "dump: SRB %p: cmd %p pid %li: %02x (%02i-%i)\n",
			       srb, srb->cmd, srb->cmd->pid,
			       srb->cmd->cmnd[0], srb->cmd->device->id,
			       srb->cmd->device->lun);
		printk("              SGList %p Cnt %i Idx %i Len %i\n",
		       srb->segment_x, srb->sg_count, srb->sg_index,
		       srb->total_xfer_length);
		printk
		    ("              State %04x Status %02x Phase %02x (%sconn.)\n",
		     srb->state, srb->status, srb->scsi_phase,
		     (acb->active_dcb) ? "" : "not");
		TRACEOUT("        %s\n", srb->debugtrace);
	}
	dprintkl(KERN_INFO, "dump: SCSI block\n");
	printk
	    ("              Status %04x FIFOCnt %02x Signals %02x IRQStat %02x\n",
	     DC395x_read16(acb, TRM_S1040_SCSI_STATUS),
	     DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
	     DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL),
	     DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS));
	printk
	    ("              Sync %02x Target %02x RSelID %02x SCSICtr %08x\n",
	     DC395x_read8(acb, TRM_S1040_SCSI_SYNC),
	     DC395x_read8(acb, TRM_S1040_SCSI_TARGETID),
	     DC395x_read8(acb, TRM_S1040_SCSI_IDMSG),
	     DC395x_read32(acb, TRM_S1040_SCSI_COUNTER));
	printk
	    ("              IRQEn %02x Config %04x Cfg2 %02x Cmd %02x SelTO %02x\n",
	     DC395x_read8(acb, TRM_S1040_SCSI_INTEN),
	     DC395x_read16(acb, TRM_S1040_SCSI_CONFIG0),
	     DC395x_read8(acb, TRM_S1040_SCSI_CONFIG2),
	     DC395x_read8(acb, TRM_S1040_SCSI_COMMAND),
	     DC395x_read8(acb, TRM_S1040_SCSI_TIMEOUT));
	dprintkl(KERN_INFO, "dump: DMA block\n");
	printk
	    ("              Cmd %04x FIFOCnt %02x FStat %02x IRQStat %02x IRQEn %02x Cfg %04x\n",
	     DC395x_read16(acb, TRM_S1040_DMA_COMMAND),
	     DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
	     DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
	     DC395x_read8(acb, TRM_S1040_DMA_STATUS),
	     DC395x_read8(acb, TRM_S1040_DMA_INTEN),
	     DC395x_read16(acb, TRM_S1040_DMA_CONFIG));
	printk("              TCtr %08x CTCtr %08x Addr %08x%08x\n",
	       DC395x_read32(acb, TRM_S1040_DMA_XCNT),
	       DC395x_read32(acb, TRM_S1040_DMA_CXCNT),
	       DC395x_read32(acb, TRM_S1040_DMA_XHIGHADDR),
	       DC395x_read32(acb, TRM_S1040_DMA_XLOWADDR));
	dprintkl(KERN_INFO, "dump: Misc: GCtrl %02x GStat %02x GTmr %02x\n",
	       DC395x_read8(acb, TRM_S1040_GEN_CONTROL),
	       DC395x_read8(acb, TRM_S1040_GEN_STATUS),
	       DC395x_read8(acb, TRM_S1040_GEN_TIMER));
	dprintkl(KERN_INFO, "dump: PCI Status %04x\n", pstat);


}


static inline void clear_fifo(struct AdapterCtlBlk *acb, char *txt)
{
#if debug_enabled(DBG_FIFO)
	u8 lines = DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL);
	u8 fifocnt = DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT);
	if (!(fifocnt & 0x40))
		dprintkdbg(DBG_FIFO,
		       "Clr FIFO (%i bytes) on phase %02x in %s\n",
			fifocnt & 0x3f, lines, txt);
#endif
#if debug_enabled(DBG_TRACE)   
	if (acb->active_dcb && acb->active_dcb->active_srb) {
		struct ScsiReqBlk *srb = acb->active_dcb->active_srb;
		TRACEPRINTF("#*");
	}
#endif
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);
}


/*
 ********************************************************************
 *
 *		DC395x_reset      scsi_reset_detect
 *
 ********************************************************************
 */
static void reset_dev_param(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *dcb;
	struct NvRamType *eeprom = &acb->eeprom;

	dprintkdbg(DBG_0, "reset_dev_param..............\n");
	list_for_each_entry(dcb, &acb->dcb_list, list) {
		u8 period_index;

		dcb->sync_mode &= ~(SYNC_NEGO_DONE + WIDE_NEGO_DONE);
		dcb->sync_period = 0;
		dcb->sync_offset = 0;

		dcb->dev_mode = eeprom->target[dcb->target_id].cfg0;
		period_index = eeprom->target[dcb->target_id].period & 0x07;
		dcb->min_nego_period = clock_period[period_index];
		if (!(dcb->dev_mode & NTC_DO_WIDE_NEGO)
		    || !(acb->config & HCC_WIDE_CARD))
			dcb->sync_mode &= ~WIDE_NEGO_ENABLE;
	}
}


/*
 *********************************************************************
 * Function : int dc395x_eh_bus_reset(Scsi_Cmnd *cmd)
 * Purpose  : perform a hard reset on the SCSI bus
 * Inputs   : cmd - some command for this host (for fetching hooks)
 * Returns  : SUCCESS (0x2002) on success, else FAILED (0x2003).
 *********************************************************************
 */
static int dc395x_eh_bus_reset(Scsi_Cmnd * cmd)
{
	struct AdapterCtlBlk *acb;
	/*u32         acb_flags=0; */

	dprintkl(KERN_INFO, "reset requested!\n");
	acb = (struct AdapterCtlBlk *) cmd->device->host->hostdata;
	/* mid level guarantees no recursion */
	/*DC395x_ACB_LOCK(acb,acb_flags); */

	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);

	/*
	 * disable interrupt    
	 */
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, 0x00);
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0x00);
	DC395x_write8(acb, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, DMARESETMODULE);

	reset_scsi_bus(acb);
	udelay(500);

	/* We may be in serious trouble. Wait some seconds */
	acb->scsi_host->last_reset =
	    jiffies + 3 * HZ / 2 +
	    HZ * acb->eeprom.delay_time;

	/*
	 * re-enable interrupt      
	 */
	/* Clear SCSI FIFO          */
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);
	clear_fifo(acb, "reset");
	/* Delete pending IRQ */
	DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
	set_basic_config(acb);

	reset_dev_param(acb);
	doing_srb_done(acb, DID_RESET, cmd, 0);

	acb->active_dcb = NULL;

	acb->acb_flag = 0;	/* RESET_DETECT, RESET_DONE ,RESET_DEV */
	waiting_process_next(acb);

	/*DC395x_ACB_LOCK(acb,acb_flags); */
	return SUCCESS;
}


/*
 *********************************************************************
 * Function : int dc395x_eh_abort(Scsi_Cmnd *cmd)
 * Purpose  : abort an errant SCSI command
 * Inputs   : cmd - command to be aborted
 * Returns  : SUCCESS (0x2002) on success, else FAILED (0x2003).
 *********************************************************************
 */
static int dc395x_eh_abort(Scsi_Cmnd * cmd)
{
	/*
	 * Look into our command queues: If it has not been sent already,
	 * we remove it and return success. Otherwise fail.
	 */
	struct AdapterCtlBlk *acb =
	    (struct AdapterCtlBlk *) cmd->device->host->hostdata;
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;

	dprintkl(KERN_INFO, "eh abort: cmd %p (pid %li, %02i-%i) ",
			     cmd,
			     cmd->pid,
			     cmd->device->id,
			     cmd->device->lun);

	dcb = find_dcb(acb, cmd->device->id, cmd->device->lun);
	if (!dcb) {
		dprintkl(KERN_DEBUG, "abort - no DCB found");
		return FAILED;
	}

	srb = find_cmd(cmd, &dcb->srb_waiting_list);
	if (srb) {
		srb_waiting_remove(dcb, srb);
		pci_unmap_srb_sense(acb, srb);
		pci_unmap_srb(acb, srb);
		free_tag(dcb, srb);
		srb_free_insert(acb, srb);
		dprintkl(KERN_DEBUG, "abort - command found in waiting commands queue");
		cmd->result = DID_ABORT << 16;
		return SUCCESS;
	}
	srb = find_cmd(cmd, &dcb->srb_going_list);
	if (srb) {
		dprintkl(KERN_DEBUG, "abort - command currently in progress");
		/* XXX: Should abort the command here */
	} else {
		dprintkl(KERN_DEBUG, "abort - command not found");
	}
	return FAILED;
}


/* SDTR */
static
void build_sdtr(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	u8 *ptr = srb->msgout_buf + srb->msg_count;
	if (srb->msg_count > 1) {
		dprintkl(KERN_INFO,
		       "Build_SDTR: msgout_buf BUSY (%i: %02x %02x)\n",
		       srb->msg_count, srb->msgout_buf[0],
		       srb->msgout_buf[1]);
		return;
	}
	if (!(dcb->dev_mode & NTC_DO_SYNC_NEGO)) {
		dcb->sync_offset = 0;
		dcb->min_nego_period = 200 >> 2;
	} else if (dcb->sync_offset == 0)
		dcb->sync_offset = SYNC_NEGO_OFFSET;

	*ptr++ = MSG_EXTENDED;	/* (01h) */
	*ptr++ = 3;		/* length */
	*ptr++ = EXTENDED_SDTR;	/* (01h) */
	*ptr++ = dcb->min_nego_period;	/* Transfer period (in 4ns) */
	*ptr++ = dcb->sync_offset;	/* Transfer period (max. REQ/ACK dist) */
	srb->msg_count += 5;
	srb->state |= SRB_DO_SYNC_NEGO;
	TRACEPRINTF("S *");
}


/* SDTR */
static
void build_wdtr(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	u8 wide =
	    ((dcb->dev_mode & NTC_DO_WIDE_NEGO) & (acb->
						   config & HCC_WIDE_CARD))
	    ? 1 : 0;
	u8 *ptr = srb->msgout_buf + srb->msg_count;
	if (srb->msg_count > 1) {
		dprintkl(KERN_INFO,
		       "Build_WDTR: msgout_buf BUSY (%i: %02x %02x)\n",
		       srb->msg_count, srb->msgout_buf[0],
		       srb->msgout_buf[1]);
		return;
	}
	*ptr++ = MSG_EXTENDED;	/* (01h) */
	*ptr++ = 2;		/* length */
	*ptr++ = EXTENDED_WDTR;	/* (03h) */
	*ptr++ = wide;
	srb->msg_count += 4;
	srb->state |= SRB_DO_WIDE_NEGO;
	TRACEPRINTF("W *");
}


#if 0
/* Timer to work around chip flaw: When selecting and the bus is 
 * busy, we sometimes miss a Selection timeout IRQ */
void selection_timeout_missed(unsigned long ptr);
/* Sets the timer to wake us up */
static void selto_timer(struct AdapterCtlBlk *acb)
{
	if (timer_pending(&acb->selto_timer))
		return;
	acb->selto_timer.function = selection_timeout_missed;
	acb->selto_timer.data = (unsigned long) acb;
	if (time_before
	    (jiffies + HZ, acb->scsi_host->last_reset + HZ / 2))
		acb->selto_timer.expires =
		    acb->scsi_host->last_reset + HZ / 2 + 1;
	else
		acb->selto_timer.expires = jiffies + HZ + 1;
	add_timer(&acb->selto_timer);
}


void selection_timeout_missed(unsigned long ptr)
{
	unsigned long flags;
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *) ptr;
	struct ScsiReqBlk *srb;
	dprintkl(KERN_DEBUG, "Chip forgot to produce SelTO IRQ!\n");
	if (!acb->active_dcb || !acb->active_dcb->active_srb) {
		dprintkl(KERN_DEBUG, "... but no cmd pending? Oops!\n");
		return;
	}
	DC395x_LOCK_IO(acb->scsi_host, flags);
	srb = acb->active_dcb->active_srb;
	TRACEPRINTF("N/TO *");
	disconnect(acb);
	DC395x_UNLOCK_IO(acb->scsi_host, flags);
}
#endif


/*
 * scsiio
 *		DC395x_DoWaitingSRB    srb_done 
 *		send_srb         request_sense
 */
static
u8 start_scsi(struct AdapterCtlBlk * acb, struct DeviceCtlBlk * dcb,
	      struct ScsiReqBlk * srb)
{
	u16 s_stat2, return_code;
	u8 s_stat, scsicommand, i, identify_message;
	u8 *ptr;

	dprintkdbg(DBG_0, "start_scsi..............\n");
	srb->tag_number = TAG_NONE;	/* acb->tag_max_num: had error read in eeprom */

	s_stat = DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL);
	s_stat2 = 0;
	s_stat2 = DC395x_read16(acb, TRM_S1040_SCSI_STATUS);
	TRACEPRINTF("Start %02x *", s_stat);
#if 1
	if (s_stat & 0x20 /* s_stat2 & 0x02000 */ ) {
		dprintkdbg(DBG_KG,
		       "StartSCSI: pid %li(%02i-%i): BUSY %02x %04x\n",
		       srb->cmd->pid, dcb->target_id, dcb->target_lun,
		       s_stat, s_stat2);
		/*
		 * Try anyway?
		 *
		 * We could, BUT: Sometimes the TRM_S1040 misses to produce a Selection
		 * Timeout, a Disconnect or a Reselction IRQ, so we would be screwed!
		 * (This is likely to be a bug in the hardware. Obviously, most people
		 *  only have one initiator per SCSI bus.)
		 * Instead let this fail and have the timer make sure the command is 
		 * tried again after a short time
		 */
		TRACEPRINTF("^*");
		/*selto_timer (acb); */
		/*monitor_next_irq = 1; */
		return 1;
	}
#endif
	if (acb->active_dcb) {
		dprintkl(KERN_DEBUG, "We try to start a SCSI command (%li)!\n",
		       srb->cmd->pid);
		dprintkl(KERN_DEBUG, "While another one (%li) is active!!\n",
		       (acb->active_dcb->active_srb ? acb->active_dcb->
			active_srb->cmd->pid : 0));
		TRACEOUT(" %s\n", srb->debugtrace);
		if (acb->active_dcb->active_srb)
			TRACEOUT(" %s\n",
				 acb->active_dcb->active_srb->debugtrace);
		return 1;
	}
	if (DC395x_read16(acb, TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT) {
		dprintkdbg(DBG_KG,
		       "StartSCSI failed (busy) for pid %li(%02i-%i)\n",
		       srb->cmd->pid, dcb->target_id, dcb->target_lun);
		TRACEPRINTF("°*");
		return 1;
	}
	/* Allow starting of SCSI commands half a second before we allow the mid-level
	 * to queue them again after a reset */
	if (time_before(jiffies, acb->scsi_host->last_reset - HZ / 2)) {
		dprintkdbg(DBG_KG, 
		       "We were just reset and don't accept commands yet!\n");
		return 1;
	}

	/* Flush FIFO */
	clear_fifo(acb, "Start");
	DC395x_write8(acb, TRM_S1040_SCSI_HOSTID, acb->scsi_host->this_id);
	DC395x_write8(acb, TRM_S1040_SCSI_TARGETID, dcb->target_id);
	DC395x_write8(acb, TRM_S1040_SCSI_SYNC, dcb->sync_period);
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, dcb->sync_offset);
	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */

	identify_message = dcb->identify_msg;
	/*DC395x_TRM_write8(TRM_S1040_SCSI_IDMSG, identify_message); */
	/* Don't allow disconnection for AUTO_REQSENSE: Cont.All.Cond.! */
	if (srb->flag & AUTO_REQSENSE)
		identify_message &= 0xBF;

	if (((srb->cmd->cmnd[0] == INQUIRY)
	     || (srb->cmd->cmnd[0] == REQUEST_SENSE)
	     || (srb->flag & AUTO_REQSENSE))
	    && (((dcb->sync_mode & WIDE_NEGO_ENABLE)
		 && !(dcb->sync_mode & WIDE_NEGO_DONE))
		|| ((dcb->sync_mode & SYNC_NEGO_ENABLE)
		    && !(dcb->sync_mode & SYNC_NEGO_DONE)))
	    && (dcb->target_lun == 0)) {
		srb->msgout_buf[0] = identify_message;
		srb->msg_count = 1;
		scsicommand = SCMD_SEL_ATNSTOP;
		srb->state = SRB_MSGOUT;
#ifndef SYNC_FIRST
		if (dcb->sync_mode & WIDE_NEGO_ENABLE
		    && dcb->inquiry7 & SCSI_INQ_WBUS16) {
			build_wdtr(acb, dcb, srb);
			goto no_cmd;
		}
#endif
		if (dcb->sync_mode & SYNC_NEGO_ENABLE
		    && dcb->inquiry7 & SCSI_INQ_SYNC) {
			build_sdtr(acb, dcb, srb);
			goto no_cmd;
		}
		if (dcb->sync_mode & WIDE_NEGO_ENABLE
		    && dcb->inquiry7 & SCSI_INQ_WBUS16) {
			build_wdtr(acb, dcb, srb);
			goto no_cmd;
		}
		srb->msg_count = 0;
	}
	/* 
	 ** Send identify message   
	 */
	DC395x_write8(acb, TRM_S1040_SCSI_FIFO, identify_message);

	scsicommand = SCMD_SEL_ATN;
	srb->state = SRB_START_;
#ifndef DC395x_NO_TAGQ
	if ((dcb->sync_mode & EN_TAG_QUEUEING)
	    && (identify_message & 0xC0)) {
		/* Send Tag message */
		u32 tag_mask = 1;
		u8 tag_number = 0;
		while (tag_mask & dcb->tag_mask
		       && tag_number <= dcb->max_command) {
			tag_mask = tag_mask << 1;
			tag_number++;
		}
		if (tag_number >= dcb->max_command) {
			dprintkl(KERN_WARNING,
			       "Start_SCSI: Out of tags for pid %li (%i-%i)\n",
			       srb->cmd->pid, srb->cmd->device->id,
			       srb->cmd->device->lun);
			srb->state = SRB_READY;
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
				       DO_HWRESELECT);
			return 1;
		}
		/* 
		 ** Send Tag id
		 */
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, MSG_SIMPLE_QTAG);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, tag_number);
		dcb->tag_mask |= tag_mask;
		srb->tag_number = tag_number;
		TRACEPRINTF("Tag %i *", tag_number);

		scsicommand = SCMD_SEL_ATN3;
		srb->state = SRB_START_;
	}
#endif
/*polling:*/
	/*
	 *          Send CDB ..command block .........                     
	 */
	dprintkdbg(DBG_KG, 
	       "StartSCSI (pid %li) %02x (%i-%i): Tag %i\n",
	       srb->cmd->pid, srb->cmd->cmnd[0],
	       srb->cmd->device->id, srb->cmd->device->lun,
	       srb->tag_number);
	if (srb->flag & AUTO_REQSENSE) {
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, REQUEST_SENSE);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, (dcb->target_lun << 5));
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO,
			      sizeof(srb->cmd->sense_buffer));
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
	} else {
		ptr = (u8 *) srb->cmd->cmnd;
		for (i = 0; i < srb->cmd->cmd_len; i++)
			DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *ptr++);
	}
      no_cmd:
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
		       DO_HWRESELECT | DO_DATALATCH);
	if (DC395x_read16(acb, TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT) {
		/* 
		 * If start_scsi return 1:
		 * we caught an interrupt (must be reset or reselection ... )
		 * : Let's process it first!
		 */
		dprintkdbg(DBG_0, "Debug: StartSCSI failed (busy) for pid %li(%02i-%i)!\n",
			srb->cmd->pid, dcb->target_id, dcb->target_lun);
		/*clear_fifo (acb, "Start2"); */
		/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_HWRESELECT | DO_DATALATCH); */
		srb->state = SRB_READY;
		free_tag(dcb, srb);
		srb->msg_count = 0;
		return_code = 1;
		/* This IRQ should NOT get lost, as we did not acknowledge it */
	} else {
		/* 
		 * If start_scsi returns 0:
		 * we know that the SCSI processor is free
		 */
		srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
		dcb->active_srb = srb;
		acb->active_dcb = dcb;
		return_code = 0;
		/* it's important for atn stop */
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
			       DO_DATALATCH | DO_HWRESELECT);
		/*
		 ** SCSI command
		 */
		TRACEPRINTF("%02x *", scsicommand);
		DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, scsicommand);
	}
	return return_code;
}


/*
 ********************************************************************
 * scsiio
 *		init_adapter
 ********************************************************************
 */

/**
 * dc395x_handle_interrupt - Handle an interrupt that has been confirmed to
 *                           have been triggered for this card.
 *
 * @acb:	 a pointer to the adpter control block
 * @scsi_status: the status return when we checked the card
 **/
static void dc395x_handle_interrupt(struct AdapterCtlBlk *acb, u16 scsi_status)
{
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	u16 phase;
	u8 scsi_intstatus;
	unsigned long flags;
	void (*dc395x_statev) (struct AdapterCtlBlk *, struct ScsiReqBlk *,
			       u16 *);

	DC395x_LOCK_IO(acb->scsi_host, flags);

	/* This acknowledges the IRQ */
	scsi_intstatus = DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
	if ((scsi_status & 0x2007) == 0x2002)
		dprintkl(KERN_DEBUG, "COP after COP completed? %04x\n",
		       scsi_status);
#if 1				/*def DBG_0 */
	if (monitor_next_irq) {
		dprintkl(KERN_INFO,
		       "status=%04x intstatus=%02x\n", scsi_status,
		       scsi_intstatus);
		monitor_next_irq--;
	}
#endif
	/*DC395x_ACB_LOCK(acb,acb_flags); */
	if (debug_enabled(DBG_KG)) {
		if (scsi_intstatus & INT_SELTIMEOUT)
		dprintkdbg(DBG_KG, "Sel Timeout IRQ\n");
	}
	/*dprintkl(KERN_DEBUG, "DC395x_IRQ: intstatus = %02x ", scsi_intstatus); */

	if (timer_pending(&acb->selto_timer))
		del_timer(&acb->selto_timer);

	if (scsi_intstatus & (INT_SELTIMEOUT | INT_DISCONNECT)) {
		disconnect(acb);	/* bus free interrupt  */
		goto out_unlock;
	}
	if (scsi_intstatus & INT_RESELECTED) {
		reselect(acb);
		goto out_unlock;
	}
	if (scsi_intstatus & INT_SELECT) {
		dprintkl(KERN_INFO, "Host does not support target mode!\n");
		goto out_unlock;
	}
	if (scsi_intstatus & INT_SCSIRESET) {
		scsi_reset_detect(acb);
		goto out_unlock;
	}
	if (scsi_intstatus & (INT_BUSSERVICE | INT_CMDDONE)) {
		dcb = acb->active_dcb;
		if (!dcb) {
			dprintkl(KERN_DEBUG,
			       "Oops: BusService (%04x %02x) w/o ActiveDCB!\n",
			       scsi_status, scsi_intstatus);
			goto out_unlock;
		}
		srb = dcb->active_srb;
		if (dcb->flag & ABORT_DEV_) {
			dprintkdbg(DBG_0, "MsgOut Abort Device.....\n");
			enable_msgout_abort(acb, srb);
		}
		/*
		 ************************************************************
		 * software sequential machine
		 ************************************************************
		 */
		phase = (u16) srb->scsi_phase;
		/* 
		 * 62037 or 62137
		 * call  dc395x_scsi_phase0[]... "phase entry"
		 * handle every phase before start transfer
		 */
		/* data_out_phase0,	phase:0 */
		/* data_in_phase0,	phase:1 */
		/* command_phase0,	phase:2 */
		/* status_phase0,	phase:3 */
		/* nop0,		phase:4 PH_BUS_FREE .. initial phase */
		/* nop0,		phase:5 PH_BUS_FREE .. initial phase */
		/* msgout_phase0,	phase:6 */
		/* msgin_phase0,	phase:7 */
		dc395x_statev = (void *) dc395x_scsi_phase0[phase];
		dc395x_statev(acb, srb, &scsi_status);
		/* 
		 *$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ 
		 *
		 *        if there were any exception occured
		 *        scsi_status will be modify to bus free phase
		 * new scsi_status transfer out from ... previous dc395x_statev
		 *
		 *$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ 
		 */
		srb->scsi_phase = scsi_status & PHASEMASK;
		phase = (u16) scsi_status & PHASEMASK;
		/* 
		 * call  dc395x_scsi_phase1[]... "phase entry"
		 * handle every phase do transfer
		 */
		/* data_out_phase1,	phase:0 */
		/* data_in_phase1,	phase:1 */
		/* command_phase1,	phase:2 */
		/* status_phase1,	phase:3 */
		/* nop1,		phase:4 PH_BUS_FREE .. initial phase */
		/* nop1,		phase:5 PH_BUS_FREE .. initial phase */
		/* msgout_phase1,	phase:6 */
		/* msgin_phase1,	phase:7 */
		dc395x_statev = (void *) dc395x_scsi_phase1[phase];
		dc395x_statev(acb, srb, &scsi_status);
	}
      out_unlock:
	DC395x_UNLOCK_IO(acb->scsi_host, flags);
	return;
}


static
irqreturn_t dc395x_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)dev_id;
	u16 scsi_status;
	u8 dma_status;
	irqreturn_t handled = IRQ_NONE;

	dprintkdbg(DBG_0, "dc395x_interrupt..............\n");
#if debug_enabled(DBG_RECURSION)
        if (dbg_in_driver++ > NORM_REC_LVL) {
		dprintkl(KERN_DEBUG, "%i interrupt recursion?\n", dbg_in_driver);
	}
#endif

	/*
	 * Check for pending interupt
	 */
	scsi_status = DC395x_read16(acb, TRM_S1040_SCSI_STATUS);
	dma_status = DC395x_read8(acb, TRM_S1040_DMA_STATUS);
	if (scsi_status & SCSIINTERRUPT) {
		/* interupt pending - let's process it! */
		dc395x_handle_interrupt(acb, scsi_status);
		handled = IRQ_HANDLED;
	}
	else if (dma_status & 0x20) {
		/* Error from the DMA engine */
		dprintkl(KERN_INFO, "Interrupt from DMA engine: %02x!\n", dma_status);
#if 0
		dprintkl(KERN_INFO, "This means DMA error! Try to handle ...\n");
		if (acb->active_dcb) {
			acb->active_dcb-> flag |= ABORT_DEV_;
			if (acb->active_dcb->active_srb)
				enable_msgout_abort(acb, acb->active_dcb->active_srb);
		}
		DC395x_write8(acb, TRM_S1040_DMA_CONTROL, ABORTXFER | CLRXFIFO);
#else
		dprintkl(KERN_INFO, "Ignoring DMA error (probably a bad thing) ...\n");
		acb = NULL;
#endif
		handled = IRQ_HANDLED;
	}

#if debug_enabled(DBG_RECURSION)
	dbg_in_driver--
#endif
	return handled;
}


/*
 ********************************************************************
 * scsiio
 *	msgout_phase0: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *			           if phase =6
 ********************************************************************
 */
static
void msgout_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		    u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "msgout_phase0.....\n");
	if (srb->state & (SRB_UNEXPECT_RESEL + SRB_ABORT_SENT)) {
		*pscsi_status = PH_BUS_FREE;	/*.. initial phase */
	}
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	srb->state &= ~SRB_MSGOUT;
	TRACEPRINTF("MOP0 *");
}


/*
 ********************************************************************
 * scsiio
 *	msgout_phase1: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *					if phase =6	    
 ********************************************************************
 */
static
void msgout_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		    u16 * pscsi_status)
{
	u16 i;
	u8 *ptr;
	struct DeviceCtlBlk *dcb;

	dprintkdbg(DBG_0, "msgout_phase1..............\n");
	TRACEPRINTF("MOP1*");
	dcb = acb->active_dcb;
	clear_fifo(acb, "MOP1");
	if (!(srb->state & SRB_MSGOUT)) {
		srb->state |= SRB_MSGOUT;
		dprintkl(KERN_DEBUG, "Debug: pid %li: MsgOut Phase unexpected.\n", srb->cmd->pid);	/* So what ? */
	}
	if (!srb->msg_count) {
		dprintkdbg(DBG_0, "Debug: pid %li: NOP Msg (no output message there).\n",
			srb->cmd->pid);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, MSG_NOP);
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
		TRACEPRINTF("\\*");
		TRACEOUT(" %s\n", srb->debugtrace);
		return;
	}
	ptr = (u8 *) srb->msgout_buf;
	TRACEPRINTF("(*");
	/*dprintkl(KERN_DEBUG, "Send msg: "); print_msg (ptr, srb->msg_count); */
	/*dprintkl(KERN_DEBUG, "MsgOut: "); */
	for (i = 0; i < srb->msg_count; i++) {
		TRACEPRINTF("%02x *", *ptr);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *ptr++);
	}
	TRACEPRINTF(")*");
	srb->msg_count = 0;
	/*printk("\n"); */
	if (/*(dcb->flag & ABORT_DEV_) && */
	    (srb->msgout_buf[0] == MSG_ABORT))
		srb->state = SRB_ABORT_SENT;

	/*1.25 */
	/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_DATALATCH); *//* it's important for atn stop */
	/*
	 ** SCSI command 
	 */
	/*TRACEPRINTF (".*"); */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}


/*
 ********************************************************************
 * scsiio
 *	command_phase0: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *				if phase =2 
 ********************************************************************
 */
static
void command_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		    u16 * pscsi_status)
{
	TRACEPRINTF("COP0 *");
	/*1.25 */
	/*clear_fifo (acb, COP0); */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
}


/*
 ********************************************************************
 * scsiio
 *	command_phase1: one of dc395x_scsi_phase1[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 *				if phase =2    	 
 ********************************************************************
 */
static
void command_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		    u16 * pscsi_status)
{
	struct DeviceCtlBlk *dcb;
	u8 *ptr;
	u16 i;

	dprintkdbg(DBG_0, "command_phase1..............\n");
	TRACEPRINTF("COP1*");
	clear_fifo(acb, "COP1");
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_CLRATN);
	if (!(srb->flag & AUTO_REQSENSE)) {
		ptr = (u8 *) srb->cmd->cmnd;
		for (i = 0; i < srb->cmd->cmd_len; i++) {
			DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *ptr);
			ptr++;
		}
	} else {
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, REQUEST_SENSE);
		dcb = acb->active_dcb;
		/* target id */
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, (dcb->target_lun << 5));
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO,
			      sizeof(srb->cmd->sense_buffer));
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
	}
	srb->state |= SRB_COMMAND;
	/* it's important for atn stop */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/* SCSI command */
	TRACEPRINTF(".*");
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}


/* Do sanity checks for S/G list */
static inline void check_sg_list(struct ScsiReqBlk *srb)
{
	if (debug_enabled(DBG_SGPARANOIA)) {
		unsigned len = 0;
		unsigned idx = srb->sg_index;
		struct SGentry *psge = srb->segment_x + idx;
		for (; idx < srb->sg_count; psge++, idx++)
			len += psge->length;
		if (len != srb->total_xfer_length)
			dprintkdbg(DBG_SGPARANOIA,
			       "Inconsistent SRB S/G lengths (Tot=%i, Count=%i) !!\n",
			       srb->total_xfer_length, len);
	}			       
}


/*
 * Compute the next Scatter Gather list index and adjust its length
 * and address if necessary; also compute virt_addr
 */
static void update_sg_list(struct ScsiReqBlk *srb, u32 left)
{
	struct SGentry *psge;
	u32 xferred = 0;
	u8 idx;
	Scsi_Cmnd *cmd = srb->cmd;
	struct scatterlist *sg;
	int segment = cmd->use_sg;

	dprintkdbg(DBG_KG, "Update SG: Total %i, Left %i\n",
	       srb->total_xfer_length, left);
	check_sg_list(srb);
	psge = srb->segment_x + srb->sg_index;
	/* data that has already been transferred */
	xferred = srb->total_xfer_length - left;
	if (srb->total_xfer_length != left) {
		/*check_sg_list_TX (srb, xferred); */
		/* Remaining */
		srb->total_xfer_length = left;
		/* parsing from last time disconnect SGIndex */
		for (idx = srb->sg_index; idx < srb->sg_count; idx++) {
			/* Complete SG entries done */
			if (xferred >= psge->length)
				xferred -= psge->length;
			/* Partial SG entries done */
			else {
				psge->length -= xferred;	/* residue data length  */
				psge->address += xferred;	/* residue data pointer */
				srb->sg_index = idx;
				pci_dma_sync_single(srb->dcb->
						    acb->dev,
						    srb->sg_bus_addr,
						    sizeof(struct SGentry)
						    *
						    DC395x_MAX_SG_LISTENTRY,
						    PCI_DMA_TODEVICE);
				break;
			}
			psge++;
		}
		check_sg_list(srb);
	}
	/* We need the corresponding virtual address sg_to_virt */
	/*dprintkl(KERN_DEBUG, "sg_to_virt: bus %08x -> virt ", psge->address); */
	if (!segment) {
		srb->virt_addr += xferred;
		/*printk("%p\n", srb->virt_addr); */
		return;
	}
	/* We have to walk the scatterlist to find it */
	sg = (struct scatterlist *) cmd->request_buffer;
	while (segment--) {
		/*printk("(%08x)%p ", BUS_ADDR(*sg), PAGE_ADDRESS(sg)); */
		unsigned long mask =
		    ~((unsigned long) sg->length - 1) & PAGE_MASK;
		if ((BUS_ADDR(*sg) & mask) == (psge->address & mask)) {
			srb->virt_addr = (PAGE_ADDRESS(sg)
					   + psge->address -
					   (psge->address & PAGE_MASK));
			/*printk("%p\n", srb->virt_addr); */
			return;
		}
		++sg;
	}
	dprintkl(KERN_ERR, "sg_to_virt failed!\n");
	srb->virt_addr = 0;
}


/* 
 * cleanup_after_transfer
 * 
 * Makes sure, DMA and SCSI engine are empty, after the transfer has finished
 * KG: Currently called from  StatusPhase1 ()
 * Should probably also be called from other places
 * Best might be to call it in DataXXPhase0, if new phase will differ 
 */
static
void cleanup_after_transfer(struct AdapterCtlBlk *acb,
			    struct ScsiReqBlk *srb)
{
	TRACEPRINTF(" Cln*");
	/*DC395x_write8 (TRM_S1040_DMA_STATUS, FORCEDMACOMP); */
	if (DC395x_read16(acb, TRM_S1040_DMA_COMMAND) & 0x0001) {	/* read */
		if (!(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x40))
			clear_fifo(acb, "ClnIn");

		if (!(DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT) & 0x80))
			DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);
	} else {		/* write */
		if (!(DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT) & 0x80))
			DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);

		if (!(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x40))
			clear_fifo(acb, "ClnOut");

	}
	/*1.25 */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
}


/*
 * Those no of bytes will be transfered w/ PIO through the SCSI FIFO
 * Seems to be needed for unknown reasons; could be a hardware bug :-(
 */
#define DC395x_LASTPIO 4
/*
 ********************************************************************
 * scsiio
 *	data_out_phase0: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *				if phase =0 
 ********************************************************************
 */
static
void data_out_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		     u16 * pscsi_status)
{
	u16 scsi_status;
	u32 d_left_counter = 0;
	struct DeviceCtlBlk *dcb = srb->dcb;

	dprintkdbg(DBG_0, "data_out_phase0.....\n");
	TRACEPRINTF("DOP0*");
	dcb = srb->dcb;
	scsi_status = *pscsi_status;

	/*
	 * KG: We need to drain the buffers before we draw any conclusions!
	 * This means telling the DMA to push the rest into SCSI, telling
	 * SCSI to push the rest to the bus.
	 * However, the device might have been the one to stop us (phase
	 * change), and the data in transit just needs to be accounted so
	 * it can be retransmitted.)
	 */
	/* 
	 * KG: Stop DMA engine pushing more data into the SCSI FIFO
	 * If we need more data, the DMA SG list will be freshly set up, anyway
	 */
	dprintkdbg(DBG_PIO, "DOP0: DMA_FCNT: %02x, DMA_FSTAT: %02x, SCSI_FCNT: %02x, CTR %06x, stat %04x, Tot: %06x\n",
	       DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
	       DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
	       DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
	       DC395x_read32(acb, TRM_S1040_SCSI_COUNTER), scsi_status,
	       srb->total_xfer_length);
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, STOPDMAXFER | CLRXFIFO);

	if (!(srb->state & SRB_XFERPAD)) {
		if (scsi_status & PARITYERROR)
			srb->status |= PARITY_ERROR;

		/*
		 * KG: Right, we can't just rely on the SCSI_COUNTER, because this
		 * is the no of bytes it got from the DMA engine not the no it 
		 * transferred successfully to the device. (And the difference could
		 * be as much as the FIFO size, I guess ...)
		 */
		if (!(scsi_status & SCSIXFERDONE)) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			d_left_counter =
			    (u32) (DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) &
				   0x1F);
			if (dcb->sync_period & WIDE_SYNC)
				d_left_counter <<= 1;

			dprintkdbg(DBG_KG,
			       "Debug: SCSI FIFO contains %i %s in DOP0\n",
			       DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
			       (dcb->
				sync_period & WIDE_SYNC) ? "words" :
			       "bytes");
			dprintkdbg(DBG_KG,
			       "SCSI FIFOCNT %02x, SCSI CTR %08x\n",
			       DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
			       DC395x_read32(acb, TRM_S1040_SCSI_COUNTER));
			dprintkdbg(DBG_KG,
			       "DMA FIFOCNT %04x, FIFOSTAT %02x, DMA CTR %08x\n",
			       DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
			       DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
			       DC395x_read32(acb, TRM_S1040_DMA_CXCNT));

			/*
			 * if WIDE scsi SCSI FIFOCNT unit is word !!!
			 * so need to *= 2
			 */
		}
		/*
		 * calculate all the residue data that not yet tranfered
		 * SCSI transfer counter + left in SCSI FIFO data
		 *
		 * .....TRM_S1040_SCSI_COUNTER (24bits)
		 * The counter always decrement by one for every SCSI byte transfer.
		 * .....TRM_S1040_SCSI_FIFOCNT ( 5bits)
		 * The counter is SCSI FIFO offset counter (in units of bytes or! words)
		 */
		if (srb->total_xfer_length > DC395x_LASTPIO)
			d_left_counter +=
			    DC395x_read32(acb, TRM_S1040_SCSI_COUNTER);
		TRACEPRINTF("%06x *", d_left_counter);

		/* Is this a good idea? */
		/*clear_fifo (acb, "DOP1"); */
		/* KG: What is this supposed to be useful for? WIDE padding stuff? */
		if (d_left_counter == 1 && dcb->sync_period & WIDE_SYNC
		    && srb->cmd->request_bufflen % 2) {
			d_left_counter = 0;
			dprintkl(KERN_INFO, "DOP0: Discard 1 byte. (%02x)\n",
			       scsi_status);
		}
		/*
		 * KG: Oops again. Same thinko as above: The SCSI might have been
		 * faster than the DMA engine, so that it ran out of data.
		 * In that case, we have to do just nothing! 
		 * But: Why the interrupt: No phase change. No XFERCNT_2_ZERO. Or?
		 */
		/*
		 * KG: This is nonsense: We have been WRITING data to the bus
		 * If the SCSI engine has no bytes left, how should the DMA engine?
		 */
		if ((d_left_counter ==
		     0) /*|| (scsi_status & SCSIXFERCNT_2_ZERO) ) */ ) {
			/*
			 * int ctr = 6000000; u8 TempDMAstatus;
			 * do
			 * {
			 *  TempDMAstatus = DC395x_read8(acb, TRM_S1040_DMA_STATUS);
			 * } while( !(TempDMAstatus & DMAXFERCOMP) && --ctr);
			 * if (ctr < 6000000-1) dprintkl(KERN_DEBUG, "DMA should be complete ... in DOP1\n");
			 * if (!ctr) dprintkl(KERN_ERR, "Deadlock in DataOutPhase0 !!\n");
			 */
			srb->total_xfer_length = 0;
		} else {	/* Update SG list         */
			/*
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			long oldxferred =
			    srb->total_xfer_length - d_left_counter;
			const int diff =
			    (dcb->sync_period & WIDE_SYNC) ? 2 : 1;
			update_sg_list(srb, d_left_counter);
			/* KG: Most ugly hack! Apparently, this works around a chip bug */
			if ((srb->segment_x[srb->sg_index].length ==
			     diff && srb->cmd->use_sg)
			    || ((oldxferred & ~PAGE_MASK) ==
				(PAGE_SIZE - diff))
			    ) {
				dprintkl(KERN_INFO,
				       "Work around chip bug (%i)?\n", diff);
				d_left_counter =
				    srb->total_xfer_length - diff;
				update_sg_list(srb, d_left_counter);
				/*srb->total_xfer_length -= diff; */
				/*srb->virt_addr += diff; */
				/*if (srb->cmd->use_sg) */
				/*      srb->sg_index++; */
			}
		}
	}
#if 0
	if (!(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x40))
		dprintkl(KERN_DEBUG,
			"DOP0(%li): %i bytes in SCSI FIFO! (Clear!)\n",
			srb->cmd->pid,
			DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x1f);
#endif
	/*clear_fifo (acb, "DOP0"); */
	/*DC395x_write8 (TRM_S1040_DMA_CONTROL, CLRXFIFO | ABORTXFER); */
#if 1
	if ((*pscsi_status & PHASEMASK) != PH_DATA_OUT) {
		/*dprintkl(KERN_DEBUG, "Debug: Clean up after Data Out ...\n"); */
		cleanup_after_transfer(acb, srb);
	}
#endif
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *	data_out_phase1: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *				if phase =0    
 *		62037
 ********************************************************************
 */
static
void data_out_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		     u16 * pscsi_status)
{

	dprintkdbg(DBG_0, "data_out_phase1.....\n");
	/*1.25 */
	TRACEPRINTF("DOP1*");
	clear_fifo(acb, "DOP1");
	/*
	 ** do prepare befor transfer when data out phase
	 */
	data_io_transfer(acb, srb, XFERDATAOUT);
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *	data_in_phase0: one of dc395x_scsi_phase1[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 *				if phase =1  
 ********************************************************************
 */
static
void data_in_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		    u16 * pscsi_status)
{
	u16 scsi_status;
	u32 d_left_counter = 0;
	/*struct DeviceCtlBlk*   dcb = srb->dcb; */
	/*u8 bval; */

	dprintkdbg(DBG_0, "data_in_phase0..............\n");
	TRACEPRINTF("DIP0*");
	scsi_status = *pscsi_status;

	/*
	 * KG: DataIn is much more tricky than DataOut. When the device is finished
	 * and switches to another phase, the SCSI engine should be finished too.
	 * But: There might still be bytes left in its FIFO to be fetched by the DMA
	 * engine and transferred to memory.
	 * We should wait for the FIFOs to be emptied by that (is there any way to 
	 * enforce this?) and then stop the DMA engine, because it might think, that
	 * there are more bytes to follow. Yes, the device might disconnect prior to
	 * having all bytes transferred! 
	 * Also we should make sure that all data from the DMA engine buffer's really
	 * made its way to the system memory! Some documentation on this would not
	 * seem to be a bad idea, actually.
	 */
	if (!(srb->state & SRB_XFERPAD)) {
		if (scsi_status & PARITYERROR) {
			dprintkl(KERN_INFO,
			       "Parity Error (pid %li, target %02i-%i)\n",
			       srb->cmd->pid, srb->cmd->device->id,
			       srb->cmd->device->lun);
			srb->status |= PARITY_ERROR;
		}
		/*
		 * KG: We should wait for the DMA FIFO to be empty ...
		 * but: it would be better to wait first for the SCSI FIFO and then the
		 * the DMA FIFO to become empty? How do we know, that the device not already
		 * sent data to the FIFO in a MsgIn phase, eg.?
		 */
		if (!(DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT) & 0x80)) {
#if 0
			int ctr = 6000000;
			dprintkl(KERN_DEBUG,
			       "DIP0: Wait for DMA FIFO to flush ...\n");
			/*DC395x_write8  (TRM_S1040_DMA_CONTROL, STOPDMAXFER); */
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 7); */
			/*DC395x_write8  (TRM_S1040_SCSI_COMMAND, SCMD_DMA_IN); */
			while (!
			       (DC395x_read16(acb, TRM_S1040_DMA_FIFOSTAT) &
				0x80) && --ctr);
			if (ctr < 6000000 - 1)
				dprintkl(KERN_DEBUG
				       "DIP0: Had to wait for DMA ...\n");
			if (!ctr)
				dprintkl(KERN_ERR,
				       "Deadlock in DIP0 waiting for DMA FIFO empty!!\n");
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 0); */
#endif
			dprintkdbg(DBG_KG, "DIP0: DMA_FIFO: %02x %02x\n",
			       DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
			       DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT));
		}
		/* Now: Check remainig data: The SCSI counters should tell us ... */
		d_left_counter = DC395x_read32(acb, TRM_S1040_SCSI_COUNTER)
		    + ((DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x1f)
		       << ((srb->dcb->sync_period & WIDE_SYNC) ? 1 :
			   0));

		dprintkdbg(DBG_KG, "SCSI FIFO contains %i %s in DIP0\n",
			  DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x1f,
			  (srb->dcb->
			  sync_period & WIDE_SYNC) ? "words" : "bytes");
		dprintkdbg(DBG_KG, "SCSI FIFOCNT %02x, SCSI CTR %08x\n",
			  DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
			  DC395x_read32(acb, TRM_S1040_SCSI_COUNTER));
		dprintkdbg(DBG_KG, "DMA FIFOCNT %02x,%02x DMA CTR %08x\n",
			  DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
			  DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
			  DC395x_read32(acb, TRM_S1040_DMA_CXCNT));
		dprintkdbg(DBG_KG, "Remaining: TotXfer: %i, SCSI FIFO+Ctr: %i\n",
			  srb->total_xfer_length, d_left_counter);
#if DC395x_LASTPIO
		/* KG: Less than or equal to 4 bytes can not be transfered via DMA, it seems. */
		if (d_left_counter
		    && srb->total_xfer_length <= DC395x_LASTPIO) {
			/*u32 addr = (srb->segment_x[srb->sg_index].address); */
			/*update_sg_list (srb, d_left_counter); */
			dprintkdbg(DBG_PIO, "DIP0: PIO (%i %s) to %p for remaining %i bytes:",
				  DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) &
				  0x1f,
				  (srb->dcb->
				   sync_period & WIDE_SYNC) ? "words" :
				  "bytes", srb->virt_addr,
				  srb->total_xfer_length);

			if (srb->dcb->sync_period & WIDE_SYNC)
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2,
					      CFG2_WIDEFIFO);

			while (DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) !=
			       0x40) {
				u8 byte =
				    DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
				*(srb->virt_addr)++ = byte;
				if (debug_enabled(DBG_PIO))
					printk(" %02x", byte);
				srb->total_xfer_length--;
				d_left_counter--;
				srb->segment_x[srb->sg_index].length--;
				if (srb->total_xfer_length
				    && !srb->segment_x[srb->sg_index].
				    length) {
				    	if (debug_enabled(DBG_PIO))
						printk(" (next segment)");
					srb->sg_index++;
					update_sg_list(srb,
							     d_left_counter);
				}
			}
			if (srb->dcb->sync_period & WIDE_SYNC) {
#if 1				/* Read the last byte ... */
				if (srb->total_xfer_length > 0) {
					u8 byte =
					    DC395x_read8
					    (acb, TRM_S1040_SCSI_FIFO);
					*(srb->virt_addr)++ = byte;
					srb->total_xfer_length--;
					if (debug_enabled(DBG_PIO))
						printk(" %02x", byte);
				}
#endif
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2, 0);
			}
			/*printk(" %08x", *(u32*)(bus_to_virt (addr))); */
			/*srb->total_xfer_length = 0; */
			if (debug_enabled(DBG_PIO))
				printk("\n");
		}
#endif				/* DC395x_LASTPIO */

#if 0
		/*
		 * KG: This was in DATAOUT. Does it also belong here?
		 * Nobody seems to know what counter and fifo_cnt count exactly ...
		 */
		if (!(scsi_status & SCSIXFERDONE)) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			d_left_counter =
			    (u32) (DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) &
				   0x1F);
			if (srb->dcb->sync_period & WIDE_SYNC)
				d_left_counter <<= 1;
			/*
			 * if WIDE scsi SCSI FIFOCNT unit is word !!!
			 * so need to *= 2
			 * KG: Seems to be correct ...
			 */
		}
#endif
		/*d_left_counter += DC395x_read32(acb, TRM_S1040_SCSI_COUNTER); */
#if 0
		dprintkl(KERN_DEBUG,
		       "DIP0: ctr=%08x, DMA_FIFO=%02x,%02x SCSI_FIFO=%02x\n",
		       d_left_counter, DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
		       DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
		       DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT));
		dprintkl(KERN_DEBUG, "DIP0: DMAStat %02x\n",
		       DC395x_read8(acb, TRM_S1040_DMA_STATUS));
#endif

		/* KG: This should not be needed any more! */
		if ((d_left_counter == 0)
		    || (scsi_status & SCSIXFERCNT_2_ZERO)) {
#if 0
			int ctr = 6000000;
			u8 TempDMAstatus;
			do {
				TempDMAstatus =
				    DC395x_read8(acb, TRM_S1040_DMA_STATUS);
			} while (!(TempDMAstatus & DMAXFERCOMP) && --ctr);
			if (!ctr)
				dprintkl(KERN_ERR,
				       "Deadlock in DataInPhase0 waiting for DMA!!\n");
			srb->total_xfer_length = 0;
#endif
#if 0				/*def DBG_KG             */
			dprintkl(KERN_DEBUG,
			       "DIP0: DMA not yet ready: %02x: %i -> %i bytes\n",
			       DC395x_read8(acb, TRM_S1040_DMA_STATUS),
			       srb->total_xfer_length, d_left_counter);
#endif
			srb->total_xfer_length = d_left_counter;
		} else {	/* phase changed */
			/*
			 * parsing the case:
			 * when a transfer not yet complete 
			 * but be disconnected by target
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			update_sg_list(srb, d_left_counter);
		}
	}
	/* KG: The target may decide to disconnect: Empty FIFO before! */
	if ((*pscsi_status & PHASEMASK) != PH_DATA_IN) {
		/*dprintkl(KERN_DEBUG, "Debug: Clean up after Data In  ...\n"); */
		cleanup_after_transfer(acb, srb);
	}
#if 0
	/* KG: Make sure, no previous transfers are pending! */
	bval = DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT);
	if (!(bval & 0x40)) {
		bval &= 0x1f;
		dprintkl(KERN_DEBUG,
		       "DIP0(%li): %i bytes in SCSI FIFO (stat %04x) (left %08x)!!\n",
		       srb->cmd->pid, bval & 0x1f, scsi_status,
		       d_left_counter);
		if ((d_left_counter == 0)
		    || (scsi_status & SCSIXFERCNT_2_ZERO)) {
			dprintkl(KERN_DEBUG, "Clear FIFO!\n");
			clear_fifo(acb, "DIP0");
		}
	}
#endif
	/*DC395x_write8 (TRM_S1040_DMA_CONTROL, CLRXFIFO | ABORTXFER); */

	/*clear_fifo (acb, "DIP0"); */
	/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_DATALATCH); */
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *	data_in_phase1: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *				if phase =1 
 ********************************************************************
 */
static
void data_in_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		    u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "data_in_phase1.....\n");
	/* FIFO should be cleared, if previous phase was not DataPhase */
	/*clear_fifo (acb, "DIP1"); */
	/* Allow data in! */
	/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_DATALATCH); */
	TRACEPRINTF("DIP1:*");
	/*
	 ** do prepare before transfer when data in phase
	 */
	data_io_transfer(acb, srb, XFERDATAIN);
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *		data_out_phase1
 *		data_in_phase1
 ********************************************************************
 */
static
void data_io_transfer(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		      u16 io_dir)
{
	u8 bval;
	struct DeviceCtlBlk *dcb;

	dprintkdbg(DBG_0, "DataIO_transfer %c (pid %li): len = %i, SG: %i/%i\n",
	       ((io_dir & DMACMD_DIR) ? 'r' : 'w'), srb->cmd->pid,
	       srb->total_xfer_length, srb->sg_index,
	       srb->sg_count);
	TRACEPRINTF("%05x(%i/%i)*", srb->total_xfer_length,
		    srb->sg_index, srb->sg_count);
	dcb = srb->dcb;
	if (srb == acb->tmp_srb) {
		dprintkl(KERN_ERR, "Using tmp_srb in DataPhase!\n");
	}
	if (srb->sg_index < srb->sg_count) {
		if (srb->total_xfer_length > DC395x_LASTPIO) {
			u8 dma_status = DC395x_read8(acb, TRM_S1040_DMA_STATUS);
			/*
			 * KG: What should we do: Use SCSI Cmd 0x90/0x92?
			 * Maybe, even ABORTXFER would be appropriate
			 */
			if (dma_status & XFERPENDING) {
				dprintkl(KERN_DEBUG, "Xfer pending! Expect trouble!!\n");
				dump_register_info(acb, dcb, srb);
				DC395x_write8(acb, TRM_S1040_DMA_CONTROL,
					      CLRXFIFO);
			}
			/*clear_fifo (acb, "IO"); */
			/* 
			 * load what physical address of Scatter/Gather list table want to be
			 * transfer 
			 */
			srb->state |= SRB_DATA_XFER;
			DC395x_write32(acb, TRM_S1040_DMA_XHIGHADDR, 0);
			if (srb->cmd->use_sg) {	/* with S/G */
				io_dir |= DMACMD_SG;
				DC395x_write32(acb, TRM_S1040_DMA_XLOWADDR,
					       srb->sg_bus_addr +
					       sizeof(struct SGentry) *
					       srb->sg_index);
				/* load how many bytes in the Scatter/Gather list table */
				DC395x_write32(acb, TRM_S1040_DMA_XCNT,
					       ((u32)
						(srb->sg_count -
						 srb->sg_index) << 3));
			} else {	/* without S/G */
				io_dir &= ~DMACMD_SG;
				DC395x_write32(acb, TRM_S1040_DMA_XLOWADDR,
					       srb->segment_x[0].address);
				DC395x_write32(acb, TRM_S1040_DMA_XCNT,
					       srb->segment_x[0].length);
			}
			/* load total transfer length (24bits) max value 16Mbyte */
			DC395x_write32(acb, TRM_S1040_SCSI_COUNTER,
				       srb->total_xfer_length);
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			if (io_dir & DMACMD_DIR) {	/* read */
				DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
					      SCMD_DMA_IN);
				DC395x_write16(acb, TRM_S1040_DMA_COMMAND,
					       io_dir);
			} else {
				DC395x_write16(acb, TRM_S1040_DMA_COMMAND,
					       io_dir);
				DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
					      SCMD_DMA_OUT);
			}

		}
#if DC395x_LASTPIO
		else if (srb->total_xfer_length > 0) {	/* The last four bytes: Do PIO */
			/*clear_fifo (acb, "IO"); */
			/* 
			 * load what physical address of Scatter/Gather list table want to be
			 * transfer 
			 */
			srb->state |= SRB_DATA_XFER;
			/* load total transfer length (24bits) max value 16Mbyte */
			DC395x_write32(acb, TRM_S1040_SCSI_COUNTER,
				       srb->total_xfer_length);
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			if (io_dir & DMACMD_DIR) {	/* read */
				DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
					      SCMD_FIFO_IN);
			} else {	/* write */
				int ln = srb->total_xfer_length;
				if (srb->dcb->sync_period & WIDE_SYNC)
					DC395x_write8
					    (acb, TRM_S1040_SCSI_CONFIG2,
					     CFG2_WIDEFIFO);
				dprintkdbg(DBG_PIO, "DOP1: PIO %i bytes from %p:",
					  srb->total_xfer_length,
					  srb->virt_addr);
				while (srb->total_xfer_length) {
					if (debug_enabled(DBG_PIO))
						printk(" %02x", (unsigned char) *(srb->virt_addr));
					DC395x_write8
					    (acb, TRM_S1040_SCSI_FIFO,
					     *(srb->virt_addr)++);
					srb->total_xfer_length--;
					srb->segment_x[srb->sg_index].
					    length--;
					if (srb->total_xfer_length
					    && !srb->segment_x[srb->
							       sg_index].
					    length) {
						if (debug_enabled(DBG_PIO))
							printk(" (next segment)");
						srb->sg_index++;
						update_sg_list(srb,
							       srb->total_xfer_length);
					}
				}
				if (srb->dcb->sync_period & WIDE_SYNC) {
					if (ln % 2) {
						DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
						if (debug_enabled(DBG_PIO))
							printk(" |00");
					}
					DC395x_write8
					    (acb, TRM_S1040_SCSI_CONFIG2, 0);
				}
				/*DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, ln); */
				if (debug_enabled(DBG_PIO))
					printk("\n");
				DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
						  SCMD_FIFO_OUT);
			}
		}
#endif				/* DC395x_LASTPIO */
		else {		/* xfer pad */

			u8 data = 0, data2 = 0;
			if (srb->sg_count) {
				srb->adapter_status = H_OVER_UNDER_RUN;
				srb->status |= OVER_RUN;
			}
			/*
			 * KG: despite the fact that we are using 16 bits I/O ops
			 * the SCSI FIFO is only 8 bits according to the docs
			 * (we can set bit 1 in 0x8f to serialize FIFO access ...)
			 */
			if (dcb->sync_period & WIDE_SYNC) {
				DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, 2);
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2,
					      CFG2_WIDEFIFO);
				if (io_dir & DMACMD_DIR) {	/* read */
					data =
					    DC395x_read8
					    (acb, TRM_S1040_SCSI_FIFO);
					data2 =
					    DC395x_read8
					    (acb, TRM_S1040_SCSI_FIFO);
					/*dprintkl(KERN_DEBUG, "DataIO: Xfer pad: %02x %02x\n", data, data2); */
				} else {
					/* Danger, Robinson: If you find KGs scattered over the wide
					 * disk, the driver or chip is to blame :-( */
					DC395x_write8(acb, TRM_S1040_SCSI_FIFO,
						      'K');
					DC395x_write8(acb, TRM_S1040_SCSI_FIFO,
						      'G');
				}
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2, 0);
			} else {
				DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, 1);
				/* Danger, Robinson: If you find a collection of Ks on your disk
				 * something broke :-( */
				if (io_dir & DMACMD_DIR) {	/* read */
					data =
					    DC395x_read8
					    (acb, TRM_S1040_SCSI_FIFO);
					/*dprintkl(KERN_DEBUG, "DataIO: Xfer pad: %02x\n", data); */
				} else {
					DC395x_write8(acb, TRM_S1040_SCSI_FIFO,
						      'K');
				}
			}
			srb->state |= SRB_XFERPAD;
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			/*
			 * SCSI command 
			 */
			bval =
			    (io_dir & DMACMD_DIR) ? SCMD_FIFO_IN :
			    SCMD_FIFO_OUT;
			DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, bval);
		}
	}
	/*monitor_next_irq = 2; */
	/*printk(" done\n"); */
}


/*
 ********************************************************************
 * scsiio
 *	status_phase0: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *				if phase =3  
 ********************************************************************
 */
static
void status_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		   u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "StatusPhase0 (pid %li)\n", srb->cmd->pid);
	TRACEPRINTF("STP0 *");
	srb->target_status = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
	srb->end_message = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);	/* get message */
	srb->state = SRB_COMPLETED;
	*pscsi_status = PH_BUS_FREE;	/*.. initial phase */
	/*1.25 */
	/*clear_fifo (acb, "STP0"); */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/*
	 ** SCSI command 
	 */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


/*
 ********************************************************************
 * scsiio
 *	status_phase1: one of dc395x_scsi_phase1[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 *				if phase =3 
 ********************************************************************
 */
static
void status_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		   u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "StatusPhase1 (pid=%li)\n", srb->cmd->pid);
	TRACEPRINTF("STP1 *");
	/* Cleanup is now done at the end of DataXXPhase0 */
	/*cleanup_after_transfer (acb, srb); */

	srb->state = SRB_STATUS;
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/*
	 * SCSI command 
	 */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_COMP);
}

/* Message handling */

#if 0
/* Print received message */
static void print_msg(u8 * msg_buf, u32 len)
{
	int i;
	printk(" %02x", msg_buf[0]);
	for (i = 1; i < len; i++)
		printk(" %02x", msg_buf[i]);
	printk("\n");
}
#endif

/* Check if the message is complete */
static inline u8 msgin_completed(u8 * msgbuf, u32 len)
{
	if (*msgbuf == EXTENDED_MESSAGE) {
		if (len < 2)
			return 0;
		if (len < msgbuf[1] + 2)
			return 0;
	} else if (*msgbuf >= 0x20 && *msgbuf <= 0x2f)	/* two byte messages */
		if (len < 2)
			return 0;
	return 1;
}

#define DC395x_ENABLE_MSGOUT \
 DC395x_write16 (acb, TRM_S1040_SCSI_CONTROL, DO_SETATN); \
 srb->state |= SRB_MSGOUT


/* reject_msg */
static inline
void msgin_reject(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	srb->msgout_buf[0] = MESSAGE_REJECT;
	srb->msg_count = 1;
	DC395x_ENABLE_MSGOUT;
	srb->state &= ~SRB_MSGIN;
	srb->state |= SRB_MSGOUT;
	dprintkl(KERN_INFO,
	       "Reject message %02x from %02i-%i\n", srb->msgin_buf[0],
	       srb->dcb->target_id, srb->dcb->target_lun);
	TRACEPRINTF("\\*");
}


/* abort command */
static inline
void enable_msgout_abort(struct AdapterCtlBlk *acb,
			 struct ScsiReqBlk *srb)
{
	srb->msgout_buf[0] = ABORT;
	srb->msg_count = 1;
	DC395x_ENABLE_MSGOUT;
	srb->state &= ~SRB_MSGIN;
	srb->state |= SRB_MSGOUT;
	/*
	   if (srb->dcb)
	   srb->dcb->flag &= ~ABORT_DEV_;
	 */
	TRACEPRINTF("#*");
}


static
struct ScsiReqBlk *msgin_qtag(struct AdapterCtlBlk *acb,
			      struct DeviceCtlBlk *dcb,
			      u8 tag)
{
	struct ScsiReqBlk *srb = NULL;
	struct ScsiReqBlk *i;
	        

	dprintkdbg(DBG_0, "QTag Msg (SRB %p): %i\n", srb, tag);
	if (!(dcb->tag_mask & (1 << tag)))
		dprintkl(KERN_DEBUG,
		       "MsgIn_QTag: tag_mask (%08x) does not reserve tag %i!\n",
		       dcb->tag_mask, tag);

	if (list_empty(&dcb->srb_going_list))
		goto mingx0;
	list_for_each_entry(i, &dcb->srb_going_list, list) {
		if (i->tag_number == tag) {
			srb = i;
			break;
		}
	}
	if (!srb)
		goto mingx0;

	dprintkdbg(DBG_0, "pid %li (%i-%i)\n", srb->cmd->pid,
	       srb->dcb->target_id, srb->dcb->target_lun);
	if (dcb->flag & ABORT_DEV_) {
		/*srb->state = SRB_ABORT_SENT; */
		enable_msgout_abort(acb, srb);
	}

	if (!(srb->state & SRB_DISCONNECT))
		goto mingx0;

	/* Tag found */
	{
		struct ScsiReqBlk *last_srb;
		        
		TRACEPRINTF("[%s]*", dcb->active_srb->debugtrace);
		TRACEPRINTF("RTag*");
		/* Just for debugging ... */
		
		last_srb = srb;
		srb = dcb->active_srb;
		TRACEPRINTF("Found.*");
		srb = last_srb;
	}

	memcpy(srb->msgin_buf, dcb->active_srb->msgin_buf, acb->msg_len);
	srb->state |= dcb->active_srb->state;
	srb->state |= SRB_DATA_XFER;
	dcb->active_srb = srb;
	/* How can we make the DORS happy? */
	return srb;

      mingx0:
	srb = acb->tmp_srb;
	srb->state = SRB_UNEXPECT_RESEL;
	dcb->active_srb = srb;
	srb->msgout_buf[0] = MSG_ABORT_TAG;
	srb->msg_count = 1;
	DC395x_ENABLE_MSGOUT;
	TRACEPRINTF("?*");
	dprintkl(KERN_DEBUG, "Unknown tag received: %i: abort !!\n", tag);
	return srb;
}


/* Reprogram registers */
static inline void
reprogram_regs(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb)
{
	DC395x_write8(acb, TRM_S1040_SCSI_TARGETID, dcb->target_id);
	DC395x_write8(acb, TRM_S1040_SCSI_SYNC, dcb->sync_period);
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, dcb->sync_offset);
	set_xfer_rate(acb, dcb);
}


/* set async transfer mode */
static
void msgin_set_async(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	dprintkl(KERN_DEBUG, "Target %02i: No sync transfers\n", dcb->target_id);
	TRACEPRINTF("!S *");
	dcb->sync_mode &= ~(SYNC_NEGO_ENABLE);
	dcb->sync_mode |= SYNC_NEGO_DONE;
	/*dcb->sync_period &= 0; */
	dcb->sync_offset = 0;
	dcb->min_nego_period = 200 >> 2;	/* 200ns <=> 5 MHz */
	srb->state &= ~SRB_DO_SYNC_NEGO;
	reprogram_regs(acb, dcb);
	if ((dcb->sync_mode & WIDE_NEGO_ENABLE)
	    && !(dcb->sync_mode & WIDE_NEGO_DONE)) {
		build_wdtr(acb, dcb, srb);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "SDTR(rej): Try WDTR anyway ...\n");
	}
}


/* set sync transfer mode */
static
void msgin_set_sync(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	u8 bval;
	int fact;
	struct DeviceCtlBlk *dcb = srb->dcb;
	/*u8 oldsyncperiod = dcb->sync_period; */
	/*u8 oldsyncoffset = dcb->sync_offset; */

	dprintkdbg(DBG_1, "Target %02i: Sync: %ins (%02i.%01i MHz) Offset %i\n",
	       dcb->target_id, srb->msgin_buf[3] << 2,
	       (250 / srb->msgin_buf[3]),
	       ((250 % srb->msgin_buf[3]) * 10) / srb->msgin_buf[3],
	       srb->msgin_buf[4]);

	if (srb->msgin_buf[4] > 15)
		srb->msgin_buf[4] = 15;
	if (!(dcb->dev_mode & NTC_DO_SYNC_NEGO))
		dcb->sync_offset = 0;
	else if (dcb->sync_offset == 0)
		dcb->sync_offset = srb->msgin_buf[4];
	if (srb->msgin_buf[4] > dcb->sync_offset)
		srb->msgin_buf[4] = dcb->sync_offset;
	else
		dcb->sync_offset = srb->msgin_buf[4];
	bval = 0;
	while (bval < 7 && (srb->msgin_buf[3] > clock_period[bval]
			    || dcb->min_nego_period >
			    clock_period[bval]))
		bval++;
	if (srb->msgin_buf[3] < clock_period[bval])
		dprintkl(KERN_INFO,
		       "Increase sync nego period to %ins\n",
		       clock_period[bval] << 2);
	srb->msgin_buf[3] = clock_period[bval];
	dcb->sync_period &= 0xf0;
	dcb->sync_period |= ALT_SYNC | bval;
	dcb->min_nego_period = srb->msgin_buf[3];

	if (dcb->sync_period & WIDE_SYNC)
		fact = 500;
	else
		fact = 250;

	dprintkl(KERN_INFO,
	       "Target %02i: %s Sync: %ins Offset %i (%02i.%01i MB/s)\n",
	       dcb->target_id, (fact == 500) ? "Wide16" : "",
	       dcb->min_nego_period << 2, dcb->sync_offset,
	       (fact / dcb->min_nego_period),
	       ((fact % dcb->min_nego_period) * 10 +
		dcb->min_nego_period / 2) / dcb->min_nego_period);

	TRACEPRINTF("S%i *", dcb->min_nego_period << 2);
	if (!(srb->state & SRB_DO_SYNC_NEGO)) {
		/* Reply with corrected SDTR Message */
		dprintkl(KERN_DEBUG, " .. answer w/  %ins %i\n",
		       srb->msgin_buf[3] << 2, srb->msgin_buf[4]);

		memcpy(srb->msgout_buf, srb->msgin_buf, 5);
		srb->msg_count = 5;
		DC395x_ENABLE_MSGOUT;
		dcb->sync_mode |= SYNC_NEGO_DONE;
	} else {
		if ((dcb->sync_mode & WIDE_NEGO_ENABLE)
		    && !(dcb->sync_mode & WIDE_NEGO_DONE)) {
			build_wdtr(acb, dcb, srb);
			DC395x_ENABLE_MSGOUT;
			dprintkdbg(DBG_0, "SDTR: Also try WDTR ...\n");
		}
	}
	srb->state &= ~SRB_DO_SYNC_NEGO;
	dcb->sync_mode |= SYNC_NEGO_DONE | SYNC_NEGO_ENABLE;

	reprogram_regs(acb, dcb);
}


static inline
void msgin_set_nowide(struct AdapterCtlBlk *acb,
		      struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	dprintkdbg(DBG_KG, "WDTR got rejected from target %02i\n",
	       dcb->target_id);
	TRACEPRINTF("!W *");
	dcb->sync_period &= ~WIDE_SYNC;
	dcb->sync_mode &= ~(WIDE_NEGO_ENABLE);
	dcb->sync_mode |= WIDE_NEGO_DONE;
	srb->state &= ~SRB_DO_WIDE_NEGO;
	reprogram_regs(acb, dcb);
	if ((dcb->sync_mode & SYNC_NEGO_ENABLE)
	    && !(dcb->sync_mode & SYNC_NEGO_DONE)) {
		build_sdtr(acb, dcb, srb);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "WDTR(rej): Try SDTR anyway ...\n");
	}
}

static
void msgin_set_wide(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	u8 wide = (dcb->dev_mode & NTC_DO_WIDE_NEGO
		   && acb->config & HCC_WIDE_CARD) ? 1 : 0;
	if (srb->msgin_buf[3] > wide)
		srb->msgin_buf[3] = wide;
	/* Completed */
	if (!(srb->state & SRB_DO_WIDE_NEGO)) {
		dprintkl(KERN_DEBUG,
		       "Target %02i initiates Wide Nego ...\n",
		       dcb->target_id);
		memcpy(srb->msgout_buf, srb->msgin_buf, 4);
		srb->msg_count = 4;
		srb->state |= SRB_DO_WIDE_NEGO;
		DC395x_ENABLE_MSGOUT;
	}

	dcb->sync_mode |= (WIDE_NEGO_ENABLE | WIDE_NEGO_DONE);
	if (srb->msgin_buf[3] > 0)
		dcb->sync_period |= WIDE_SYNC;
	else
		dcb->sync_period &= ~WIDE_SYNC;
	srb->state &= ~SRB_DO_WIDE_NEGO;
	TRACEPRINTF("W%i *", (dcb->sync_period & WIDE_SYNC ? 1 : 0));
	/*dcb->sync_mode &= ~(WIDE_NEGO_ENABLE+WIDE_NEGO_DONE); */
	dprintkdbg(DBG_KG,
	       "Wide transfers (%i bit) negotiated with target %02i\n",
	       (8 << srb->msgin_buf[3]), dcb->target_id);
	reprogram_regs(acb, dcb);
	if ((dcb->sync_mode & SYNC_NEGO_ENABLE)
	    && !(dcb->sync_mode & SYNC_NEGO_DONE)) {
		build_sdtr(acb, dcb, srb);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "WDTR: Also try SDTR ...\n");
	}
}


/*
 ********************************************************************
 * scsiio
 *	msgin_phase0: one of dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *				if phase =7   
 *
 * extended message codes:
 *
 *	code	description
 *
 *	02h	Reserved
 *	00h	MODIFY DATA  POINTER
 *	01h	SYNCHRONOUS DATA TRANSFER REQUEST
 *	03h	WIDE DATA TRANSFER REQUEST
 *   04h - 7Fh	Reserved
 *   80h - FFh	Vendor specific
 *  
 ********************************************************************
 */
static
void msgin_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		  u16 * pscsi_status)
{
	struct DeviceCtlBlk *dcb;

	dprintkdbg(DBG_0, "msgin_phase0..............\n");
	TRACEPRINTF("MIP0*");
	dcb = acb->active_dcb;

	srb->msgin_buf[acb->msg_len++] = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
	if (msgin_completed(srb->msgin_buf, acb->msg_len)) {
		TRACEPRINTF("(%02x)*", srb->msgin_buf[0]);
		/*dprintkl(KERN_INFO, "MsgIn:"); */
		/*print_msg (srb->msgin_buf, acb->msg_len); */

		/* Now eval the msg */
		switch (srb->msgin_buf[0]) {
		case DISCONNECT:
			srb->state = SRB_DISCONNECT;
			break;

		case SIMPLE_QUEUE_TAG:
		case HEAD_OF_QUEUE_TAG:
		case ORDERED_QUEUE_TAG:
			TRACEPRINTF("(%02x)*", srb->msgin_buf[1]);
			srb =
			    msgin_qtag(acb, dcb,
					      srb->msgin_buf[1]);
			break;

		case MESSAGE_REJECT:
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
				       DO_CLRATN | DO_DATALATCH);
			/* A sync nego message was rejected ! */
			if (srb->state & SRB_DO_SYNC_NEGO) {
				msgin_set_async(acb, srb);
				break;
			}
			/* A wide nego message was rejected ! */
			if (srb->state & SRB_DO_WIDE_NEGO) {
				msgin_set_nowide(acb, srb);
				break;
			}
			enable_msgout_abort(acb, srb);
			/*srb->state |= SRB_ABORT_SENT */
			break;

		case EXTENDED_MESSAGE:
			TRACEPRINTF("(%02x)*", srb->msgin_buf[2]);
			/* SDTR */
			if (srb->msgin_buf[1] == 3
			    && srb->msgin_buf[2] == EXTENDED_SDTR) {
				msgin_set_sync(acb, srb);
				break;
			}
			/* WDTR */
			if (srb->msgin_buf[1] == 2 && srb->msgin_buf[2] == EXTENDED_WDTR && srb->msgin_buf[3] <= 2) {	/* sanity check ... */
				msgin_set_wide(acb, srb);
				break;
			}
			msgin_reject(acb, srb);
			break;

			/* Discard  wide residual */
		case MSG_IGNOREWIDE:
			dprintkdbg(DBG_0, "Ignore Wide Residual!\n");
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 1); */
			/*DC395x_read8 (TRM_S1040_SCSI_FIFO); */
			break;

			/* nothing has to be done */
		case COMMAND_COMPLETE:
			break;

			/*
			 * SAVE POINTER may be ignored as we have the struct ScsiReqBlk* associated with the
			 * scsi command. Thanks, Gérard, for pointing it out.
			 */
		case SAVE_POINTERS:
			dprintkdbg(DBG_0, "SAVE POINTER message received (pid %li: rem.%i) ... ignore :-(\n",
			       srb->cmd->pid, srb->total_xfer_length);
			/*srb->Saved_Ptr = srb->TotalxferredLen; */
			break;
			/* The device might want to restart transfer with a RESTORE */
		case RESTORE_POINTERS:
			dprintkl(KERN_DEBUG,
			       "RESTORE POINTER message received ... ignore :-(\n");
			/*dc395x_restore_ptr (acb, srb); */
			break;
		case ABORT:
			dprintkl(KERN_DEBUG,
			       "ABORT msg received (pid %li %02i-%i)\n",
			       srb->cmd->pid, dcb->target_id,
			       dcb->target_lun);
			dcb->flag |= ABORT_DEV_;
			enable_msgout_abort(acb, srb);
			break;
			/* reject unknown messages */
		default:
			if (srb->msgin_buf[0] & IDENTIFY_BASE) {
				dprintkl(KERN_DEBUG, "Identify Message received?\n");
				/*TRACEOUT (" %s\n", srb->debugtrace); */
				srb->msg_count = 1;
				srb->msgout_buf[0] = dcb->identify_msg;
				DC395x_ENABLE_MSGOUT;
				srb->state |= SRB_MSGOUT;
				/*break; */
			}
			msgin_reject(acb, srb);
			TRACEOUT(" %s\n", srb->debugtrace);
		}
		TRACEPRINTF(".*");

		/* Clear counter and MsgIn state */
		srb->state &= ~SRB_MSGIN;
		acb->msg_len = 0;
	}

	/*1.25 */
	if ((*pscsi_status & PHASEMASK) != PH_MSG_IN)
#if 0
		clear_fifo(acb, "MIP0_");
#else
		TRACEPRINTF("N/Cln *");
#endif
	*pscsi_status = PH_BUS_FREE;
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important ... you know! */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


/*
 ********************************************************************
 * scsiio
 *	msgin_phase1: one of dc395x_scsi_phase1[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 *				if phase =7	   
 ********************************************************************
 */
static
void msgin_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		  u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "msgin_phase1..............\n");
	TRACEPRINTF("MIP1 *");
	clear_fifo(acb, "MIP1");
	DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, 1);
	if (!(srb->state & SRB_MSGIN)) {
		srb->state &= ~SRB_DISCONNECT;
		srb->state |= SRB_MSGIN;
	}
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/*
	 * SCSI command 
	 */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_IN);
}


/*
 ********************************************************************
 * scsiio
 *	nop0: one of dc395x_scsi_phase1[] ,dc395x_scsi_phase0[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *	 dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 *				if phase =4 ..PH_BUS_FREE
 ********************************************************************
 */
static
void nop0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
	  u16 * pscsi_status)
{
	/*TRACEPRINTF("NOP0 *"); */
}


/*
 ********************************************************************
 * scsiio
 *	nop1: one of dc395x_scsi_phase0[] ,dc395x_scsi_phase1[] vectors
 *	 dc395x_statev = (void *)dc395x_scsi_phase0[phase]
 *	 dc395x_statev = (void *)dc395x_scsi_phase1[phase]
 *				if phase =5
 ********************************************************************
 */
static
void nop1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
	  u16 * pscsi_status)
{
	/*TRACEPRINTF("NOP1 *"); */
}


/*
 ********************************************************************
 * scsiio
 *		msgin_phase0
 ********************************************************************
 */
static
void set_xfer_rate(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb)
{
	struct DeviceCtlBlk *i;

	/*
	 * set all lun device's  period , offset
	 */
	if (dcb->identify_msg & 0x07)
		return;

	if (acb->scan_devices) {
		current_sync_offset = dcb->sync_offset;
		return;
	}

	list_for_each_entry(i, &acb->dcb_list, list)
		if (i->target_id == dcb->target_id) {
			i->sync_period = dcb->sync_period;
			i->sync_offset = dcb->sync_offset;
			i->sync_mode = dcb->sync_mode;
			i->min_nego_period = dcb->min_nego_period;
		}
}


/*
 ********************************************************************
 * scsiio
 *		dc395x_interrupt
 ********************************************************************
 */
static void disconnect(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;

	dprintkdbg(DBG_0, "Disconnect (pid=%li)\n", acb->active_dcb->active_srb->cmd->pid);
	dcb = acb->active_dcb;
	if (!dcb) {
		dprintkl(KERN_ERR, "Disc: Exception Disconnect dcb=NULL !!\n ");
		udelay(500);
		/* Suspend queue for a while */
		acb->scsi_host->last_reset =
		    jiffies + HZ / 2 +
		    HZ * acb->eeprom.delay_time;
		clear_fifo(acb, "DiscEx");
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT);
		return;
	}
	srb = dcb->active_srb;
	acb->active_dcb = NULL;
	TRACEPRINTF("DISC *");

	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
	clear_fifo(acb, "Disc");
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT);
	if (srb->state & SRB_UNEXPECT_RESEL) {
		dprintkl(KERN_ERR, "Disc: Unexpected Reselection (%i-%i)\n",
		       dcb->target_id, dcb->target_lun);
		srb->state = 0;
		waiting_process_next(acb);
	} else if (srb->state & SRB_ABORT_SENT) {
		/*Scsi_Cmnd* cmd = srb->cmd; */
		dcb->flag &= ~ABORT_DEV_;
		acb->scsi_host->last_reset = jiffies + HZ / 2 + 1;
		dprintkl(KERN_ERR, "Disc: SRB_ABORT_SENT!\n");
		doing_srb_done(acb, DID_ABORT, srb->cmd, 1);
		waiting_process_next(acb);
	} else {
		if ((srb->state & (SRB_START_ + SRB_MSGOUT))
		    || !(srb->
			 state & (SRB_DISCONNECT + SRB_COMPLETED))) {
			/*
			 * Selection time out 
			 * SRB_START_ || SRB_MSGOUT || (!SRB_DISCONNECT && !SRB_COMPLETED)
			 */
			/* Unexp. Disc / Sel Timeout */
			if (srb->state != SRB_START_
			    && srb->state != SRB_MSGOUT) {
				srb->state = SRB_READY;
				dprintkl(KERN_DEBUG, "Unexpected Disconnection (pid %li)!\n",
				       srb->cmd->pid);
				srb->target_status = SCSI_STAT_SEL_TIMEOUT;
				TRACEPRINTF("UnExpD *");
				TRACEOUT("%s\n", srb->debugtrace);
				goto disc1;
			} else {
				/* Normal selection timeout */
				TRACEPRINTF("SlTO *");
				dprintkdbg(DBG_KG,
				       "Disc: SelTO (pid=%li) for dev %02i-%i\n",
				       srb->cmd->pid, dcb->target_id,
				       dcb->target_lun);
				if (srb->retry_count++ > DC395x_MAX_RETRIES
				    || acb->scan_devices) {
					srb->target_status =
					    SCSI_STAT_SEL_TIMEOUT;
					goto disc1;
				}
				free_tag(dcb, srb);
				srb_going_to_waiting_move(dcb, srb);
				dprintkdbg(DBG_KG, "Retry pid %li ...\n",
				       srb->cmd->pid);
				waiting_set_timer(acb, HZ / 20);
			}
		} else if (srb->state & SRB_DISCONNECT) {
			u8 bval = DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL);
			/*
			 * SRB_DISCONNECT (This is what we expect!)
			 */
			/* dprintkl(KERN_DEBUG, "DoWaitingSRB (pid=%li)\n", srb->cmd->pid); */
			TRACEPRINTF("+*");
			if (bval & 0x40) {
				dprintkdbg(DBG_0, "Debug: DISC: SCSI bus stat %02x: ACK set! Other controllers?\n",
					bval);
				/* It could come from another initiator, therefore don't do much ! */
				TRACEPRINTF("ACK(%02x) *", bval);
				/*dump_register_info (acb, dcb, srb); */
				/*TRACEOUT (" %s\n", srb->debugtrace); */
				/*dcb->flag |= ABORT_DEV_; */
				/*enable_msgout_abort (acb, srb); */
				/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_CLRFIFO | DO_CLRATN | DO_HWRESELECT); */
			} else
				waiting_process_next(acb);
		} else if (srb->state & SRB_COMPLETED) {
		      disc1:
			/*
			 ** SRB_COMPLETED
			 */
			free_tag(dcb, srb);
			dcb->active_srb = NULL;
			srb->state = SRB_FREE;
			/*dprintkl(KERN_DEBUG, "done (pid=%li)\n", srb->cmd->pid); */
			srb_done(acb, dcb, srb);
		}
	}
	return;
}


/*
 ********************************************************************
 * scsiio
 *		reselect
 ********************************************************************
 */
static void reselect(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb = NULL;
	u16 rsel_tar_lun_id;
	u8 id, lun;
	u8 arblostflag = 0;

	dprintkdbg(DBG_0, "reselect..............\n");

	clear_fifo(acb, "Resel");
	/*DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT | DO_DATALATCH); */
	/* Read Reselected Target ID and LUN */
	rsel_tar_lun_id = DC395x_read16(acb, TRM_S1040_SCSI_TARGETID);
	dcb = acb->active_dcb;
	if (dcb) {		/* Arbitration lost but Reselection win */
		srb = dcb->active_srb;
		if (!srb) {
			dprintkl(KERN_DEBUG, "Arb lost Resel won, but active_srb == NULL!\n");
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			return;
		}
		/* Why the if ? */
		if (!(acb->scan_devices)) {
			dprintkdbg(DBG_KG,
			       "Arb lost but Resel win pid %li (%02i-%i) Rsel %04x Stat %04x\n",
			       srb->cmd->pid, dcb->target_id,
			       dcb->target_lun, rsel_tar_lun_id,
			       DC395x_read16(acb, TRM_S1040_SCSI_STATUS));
			TRACEPRINTF("ArbLResel!*");
			/*TRACEOUT (" %s\n", srb->debugtrace); */
			arblostflag = 1;
			/*srb->state |= SRB_DISCONNECT; */

			srb->state = SRB_READY;
			free_tag(dcb, srb);
			srb_going_to_waiting_move(dcb, srb);
			waiting_set_timer(acb, HZ / 20);

			/* return; */
		}
	}
	/* Read Reselected Target Id and LUN */
	if (!(rsel_tar_lun_id & (IDENTIFY_BASE << 8)))
		dprintkl(KERN_DEBUG, "Resel expects identify msg! Got %04x!\n",
		       rsel_tar_lun_id);
	id = rsel_tar_lun_id & 0xff;
	lun = (rsel_tar_lun_id >> 8) & 7;
	dcb = find_dcb(acb, id, lun);
	if (!dcb) {
		dprintkl(KERN_ERR, "Reselect from non existing device (%02i-%i)\n",
		       id, lun);
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		return;
	}

	acb->active_dcb = dcb;

	if (!(dcb->dev_mode & NTC_DO_DISCONNECT))
		dprintkl(KERN_DEBUG, "Reselection in spite of forbidden disconnection? (%02i-%i)\n",
		       dcb->target_id, dcb->target_lun);

	if ((dcb->sync_mode & EN_TAG_QUEUEING) /*&& !arblostflag */ ) {
		struct ScsiReqBlk *oldSRB = srb;
		srb = acb->tmp_srb;
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
		srb->debugpos = 0;
		srb->debugtrace[0] = 0;
#endif
		dcb->active_srb = srb;
		if (oldSRB)
			TRACEPRINTF("ArbLResel(%li):*", oldSRB->cmd->pid);
		/*if (arblostflag) dprintkl(KERN_DEBUG, "Reselect: Wait for Tag ... \n"); */
	} else {
		/* There can be only one! */
		srb = dcb->active_srb;
		if (srb)
			TRACEPRINTF("RSel *");
		if (!srb || !(srb->state & SRB_DISCONNECT)) {
			/*
			 * abort command
			 */
			dprintkl(KERN_DEBUG,
			       "Reselected w/o disconnected cmds from %02i-%i?\n",
			       dcb->target_id, dcb->target_lun);
			srb = acb->tmp_srb;
			srb->state = SRB_UNEXPECT_RESEL;
			dcb->active_srb = srb;
			enable_msgout_abort(acb, srb);
		} else {
			if (dcb->flag & ABORT_DEV_) {
				/*srb->state = SRB_ABORT_SENT; */
				enable_msgout_abort(acb, srb);
			} else
				srb->state = SRB_DATA_XFER;

		}
		/*if (arblostflag) TRACEOUT (" %s\n", srb->debugtrace); */
	}
	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
	/* 
	 ***********************************************
	 ** Program HA ID, target ID, period and offset
	 ***********************************************
	 */
	DC395x_write8(acb, TRM_S1040_SCSI_HOSTID, acb->scsi_host->this_id);	/* host   ID */
	DC395x_write8(acb, TRM_S1040_SCSI_TARGETID, dcb->target_id);		/* target ID */
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, dcb->sync_offset);		/* offset    */
	DC395x_write8(acb, TRM_S1040_SCSI_SYNC, dcb->sync_period);		/* sync period, wide */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);		/* it's important for atn stop */
	/* SCSI command */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}






static inline u8 tagq_blacklist(char *name)
{
#ifndef DC395x_NO_TAGQ
#if 0
	u8 i;
	for (i = 0; i < BADDEVCNT; i++)
		if (memcmp(name, DC395x_baddevname1[i], 28) == 0)
			return 1;
#endif
	return 0;
#else
	return 1;
#endif
}


static
void disc_tagq_set(struct DeviceCtlBlk *dcb, struct ScsiInqData *ptr)
{
	/* Check for SCSI format (ANSI and Response data format) */
	if ((ptr->Vers & 0x07) >= 2 || (ptr->RDF & 0x0F) == 2) {
		if ((ptr->Flags & SCSI_INQ_CMDQUEUE)
		    && (dcb->dev_mode & NTC_DO_TAG_QUEUEING) &&
		    /*(dcb->dev_mode & NTC_DO_DISCONNECT) */
		    /* ((dcb->dev_type == TYPE_DISK) 
		       || (dcb->dev_type == TYPE_MOD)) && */
		    !tagq_blacklist(((char *) ptr) + 8)) {
			if (dcb->max_command == 1)
				dcb->max_command =
				    dcb->acb->tag_max_num;
			dcb->sync_mode |= EN_TAG_QUEUEING;
			/*dcb->tag_mask = 0; */
		} else
			dcb->max_command = 1;
	}
}


static
void add_dev(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
	     struct ScsiInqData *ptr)
{
	u8 bval1 = ptr->DevType & SCSI_DEVTYPE;
	dcb->dev_type = bval1;
	/* if (bval1 == TYPE_DISK || bval1 == TYPE_MOD) */
	disc_tagq_set(dcb, ptr);
}


/* 
 ********************************************************************
 * unmap mapped pci regions from SRB
 ********************************************************************
 */
static
void pci_unmap_srb(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	int dir;
	Scsi_Cmnd *cmd = srb->cmd;
	dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
	if (cmd->use_sg && dir != PCI_DMA_NONE) {
		/* unmap DC395x SG list */
		dprintkdbg(DBG_SGPARANOIA,
		       "Unmap SG descriptor list %08x (%05x)\n",
		       srb->sg_bus_addr,
		       sizeof(struct SGentry) * DC395x_MAX_SG_LISTENTRY);
		pci_unmap_single(acb->dev, srb->sg_bus_addr,
				 sizeof(struct SGentry) *
				 DC395x_MAX_SG_LISTENTRY,
				 PCI_DMA_TODEVICE);
		dprintkdbg(DBG_SGPARANOIA, "Unmap %i SG segments from %p\n",
		       cmd->use_sg, cmd->request_buffer);
		/* unmap the sg segments */
		pci_unmap_sg(acb->dev,
			     (struct scatterlist *) cmd->request_buffer,
			     cmd->use_sg, dir);
	} else if (cmd->request_buffer && dir != PCI_DMA_NONE) {
		dprintkdbg(DBG_SGPARANOIA, "Unmap buffer at %08x (%05x)\n",
		       srb->segment_x[0].address, cmd->request_bufflen);
		pci_unmap_single(acb->dev, srb->segment_x[0].address,
				 cmd->request_bufflen, dir);
	}
}


/* 
 ********************************************************************
 * unmap mapped pci sense buffer from SRB
 ********************************************************************
 */
static
void pci_unmap_srb_sense(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	if (!(srb->flag & AUTO_REQSENSE))
		return;
	/* Unmap sense buffer */
	dprintkdbg(DBG_SGPARANOIA, "Unmap sense buffer from %08x\n",
	       srb->segment_x[0].address);
	pci_unmap_single(acb->dev, srb->segment_x[0].address,
			 srb->segment_x[0].length, PCI_DMA_FROMDEVICE);
	/* Restore SG stuff */
	/*printk ("Auto_ReqSense finished: Restore Counters ...\n"); */
	srb->total_xfer_length = srb->xferred;
	srb->segment_x[0].address =
	    srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].address;
	srb->segment_x[0].length =
	    srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].length;
}


/*
 ********************************************************************
 * scsiio
 *		disconnect
 *	Complete execution of a SCSI command
 *	Signal completion to the generic SCSI driver  
 ********************************************************************
 */
static
void srb_done(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
	      struct ScsiReqBlk *srb)
{
	u8 tempcnt, status;
	Scsi_Cmnd *cmd;
	struct ScsiInqData *ptr;
	/*u32              drv_flags=0; */
	int dir;

	cmd = srb->cmd;
	TRACEPRINTF("DONE *");

	dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
	ptr = (struct ScsiInqData *) (cmd->request_buffer);
	if (cmd->use_sg)
		ptr =
		    (struct ScsiInqData *) CPU_ADDR(*(struct scatterlist *)
						    ptr);
	dprintkdbg(DBG_SGPARANOIA, 
	       "SRBdone SG=%i (%i/%i), req_buf = %p, adr = %p\n",
	       cmd->use_sg, srb->sg_index, srb->sg_count,
	       cmd->request_buffer, ptr);
	dprintkdbg(DBG_KG,
	       "SRBdone (pid %li, target %02i-%i): ", srb->cmd->pid,
	       srb->cmd->device->id, srb->cmd->device->lun);
	status = srb->target_status;
	if (srb->flag & AUTO_REQSENSE) {
		dprintkdbg(DBG_0, "AUTO_REQSENSE1..............\n");
		pci_unmap_srb_sense(acb, srb);
		/*
		 ** target status..........................
		 */
		srb->flag &= ~AUTO_REQSENSE;
		srb->adapter_status = 0;
		srb->target_status = CHECK_CONDITION << 1;
		if (debug_enabled(DBG_KG)) {
			switch (cmd->sense_buffer[2] & 0x0f) {
			case NOT_READY:
				dprintkl(KERN_DEBUG,
				     "ReqSense: NOT_READY (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case UNIT_ATTENTION:
				dprintkl(KERN_DEBUG,
				     "ReqSense: UNIT_ATTENTION (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case ILLEGAL_REQUEST:
				dprintkl(KERN_DEBUG,
				     "ReqSense: ILLEGAL_REQUEST (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case MEDIUM_ERROR:
				dprintkl(KERN_DEBUG,
				     "ReqSense: MEDIUM_ERROR (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case HARDWARE_ERROR:
				dprintkl(KERN_DEBUG,
				     "ReqSense: HARDWARE_ERROR (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			}
			if (cmd->sense_buffer[7] >= 6)
				dprintkl(KERN_DEBUG, 
				     "Sense=%02x, ASC=%02x, ASCQ=%02x (%08x %08x) ",
				     cmd->sense_buffer[2], cmd->sense_buffer[12],
				     cmd->sense_buffer[13],
				     *((unsigned int *) (cmd->sense_buffer + 3)),
				     *((unsigned int *) (cmd->sense_buffer + 8)));
			else
				dprintkl(KERN_DEBUG,
				     "Sense=%02x, No ASC/ASCQ (%08x) ",
				     cmd->sense_buffer[2],
				     *((unsigned int *) (cmd->sense_buffer + 3)));
		}

		if (status == (CHECK_CONDITION << 1)) {
			cmd->result = DID_BAD_TARGET << 16;
			goto ckc_e;
		}
		dprintkdbg(DBG_0, "AUTO_REQSENSE2..............\n");

		if ((srb->total_xfer_length)
		    && (srb->total_xfer_length >= cmd->underflow))
			cmd->result =
			    MK_RES_LNX(DRIVER_SENSE, DID_OK,
				       srb->end_message, CHECK_CONDITION);
		/*SET_RES_DID(cmd->result,DID_OK) */
		else
			cmd->result =
			    MK_RES_LNX(DRIVER_SENSE, DID_OK,
				       srb->end_message, CHECK_CONDITION);

		goto ckc_e;
	}

/*************************************************************/
	if (status) {
		/*
		 * target status..........................
		 */
		if (status_byte(status) == CHECK_CONDITION) {
			request_sense(acb, dcb, srb);
			return;
		} else if (status_byte(status) == QUEUE_FULL) {
			tempcnt = (u8)list_size(&dcb->srb_going_list);
			printk
			    ("\nDC395x:  QUEUE_FULL for dev %02i-%i with %i cmnds\n",
			     dcb->target_id, dcb->target_lun, tempcnt);
			if (tempcnt > 1)
				tempcnt--;
			dcb->max_command = tempcnt;
			free_tag(dcb, srb);
			srb_going_to_waiting_move(dcb, srb);
			waiting_set_timer(acb, HZ / 20);
			srb->adapter_status = 0;
			srb->target_status = 0;
			return;
		} else if (status == SCSI_STAT_SEL_TIMEOUT) {
			srb->adapter_status = H_SEL_TIMEOUT;
			srb->target_status = 0;
			cmd->result = DID_NO_CONNECT << 16;
		} else {
			srb->adapter_status = 0;
			SET_RES_DID(cmd->result, DID_ERROR);
			SET_RES_MSG(cmd->result, srb->end_message);
			SET_RES_TARGET(cmd->result, status);

		}
	} else {
		/*
		 ** process initiator status..........................
		 */
		status = srb->adapter_status;
		if (status & H_OVER_UNDER_RUN) {
			srb->target_status = 0;
			SET_RES_DID(cmd->result, DID_OK);
			SET_RES_MSG(cmd->result, srb->end_message);
		} else if (srb->status & PARITY_ERROR) {
			SET_RES_DID(cmd->result, DID_PARITY);
			SET_RES_MSG(cmd->result, srb->end_message);
		} else {	/* No error */

			srb->adapter_status = 0;
			srb->target_status = 0;
			SET_RES_DID(cmd->result, DID_OK);
		}
	}

	if (dir != PCI_DMA_NONE) {
		if (cmd->use_sg)
			pci_dma_sync_sg(acb->dev,
					(struct scatterlist *) cmd->
					request_buffer, cmd->use_sg, dir);
		else if (cmd->request_buffer)
			pci_dma_sync_single(acb->dev,
					    srb->segment_x[0].address,
					    cmd->request_bufflen, dir);
	}

	if ((cmd->result & RES_DID) == 0 && cmd->cmnd[0] == INQUIRY
	    && cmd->cmnd[2] == 0 && cmd->request_bufflen >= 8
	    && dir != PCI_DMA_NONE && ptr && (ptr->Vers & 0x07) >= 2)
		dcb->inquiry7 = ptr->Flags;
/* Check Error Conditions */
      ckc_e:

	/*if( srb->cmd->cmnd[0] == INQUIRY && */
	/*  (host_byte(cmd->result) == DID_OK || status_byte(cmd->result) & CHECK_CONDITION) ) */
	if (cmd->cmnd[0] == INQUIRY && (cmd->result == (DID_OK << 16)
					 || status_byte(cmd->
							result) &
					 CHECK_CONDITION)) {

		if (!dcb->init_tcq_flag) {
			add_dev(acb, dcb, ptr);
			dcb->init_tcq_flag = 1;
		}

	}


	/* Here is the info for Doug Gilbert's sg3 ... */
	cmd->resid = srb->total_xfer_length;
	/* This may be interpreted by sb. or not ... */
	cmd->SCp.this_residual = srb->total_xfer_length;
	cmd->SCp.buffers_residual = 0;
	if (debug_enabled(DBG_KG)) {
		if (srb->total_xfer_length)
			dprintkdbg(DBG_KG, "pid %li: %02x (%02i-%i): Missed %i bytes\n",
			     cmd->pid, cmd->cmnd[0], cmd->device->id,
			     cmd->device->lun, srb->total_xfer_length);
	}

	srb_going_remove(dcb, srb);
	/* Add to free list */
	if (srb == acb->tmp_srb)
		dprintkl(KERN_ERR, "ERROR! Completed Cmnd with tmp_srb!\n");
	else
		srb_free_insert(acb, srb);

	dprintkdbg(DBG_0, "SRBdone: done pid %li\n", cmd->pid);
	if (debug_enabled(DBG_KG)) {
		printk(" 0x%08x\n", cmd->result);
	}
	TRACEPRINTF("%08x(%li)*", cmd->result, jiffies);
	pci_unmap_srb(acb, srb);
	/*DC395x_UNLOCK_ACB_NI; */
	cmd->scsi_done(cmd);
	/*DC395x_LOCK_ACB_NI; */
	TRACEOUTALL(KERN_INFO " %s\n", srb->debugtrace);

	waiting_process_next(acb);
	return;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_reset
 * abort all cmds in our queues
 ********************************************************************
 */
static
void doing_srb_done(struct AdapterCtlBlk *acb, u8 did_flag,
		    Scsi_Cmnd * cmd, u8 force)
{
	struct DeviceCtlBlk *dcb;

	dprintkl(KERN_INFO, "doing_srb_done: pids ");
	list_for_each_entry(dcb, &acb->dcb_list, list) {
		struct ScsiReqBlk *srb;
		struct ScsiReqBlk *tmp;
		Scsi_Cmnd *p;

		list_for_each_entry_safe(srb, tmp, &dcb->srb_going_list, list) {
			int result;
			int dir;

			p = srb->cmd;
			dir = scsi_to_pci_dma_dir(p->sc_data_direction);
			result = MK_RES(0, did_flag, 0, 0);

			/*result = MK_RES(0,DID_RESET,0,0); */
			TRACEPRINTF("Reset(%li):%08x*", jiffies, result);
			printk(" (G)");
#if 1				/*ndef DC395x_DEBUGTRACE */
			printk("%li(%02i-%i) ", p->pid,
			       p->device->id, p->device->lun);
#endif
			TRACEOUT("%s\n", srb->debugtrace);

			srb_going_remove(dcb, srb);
			free_tag(dcb, srb);
			srb_free_insert(acb, srb);
			p->result = result;
			pci_unmap_srb_sense(acb, srb);
			pci_unmap_srb(acb, srb);
			if (force) {
				/* For new EH, we normally don't need to give commands back,
				 * as they all complete or all time out */
				p->scsi_done(p);
			}
		}
		if (!list_empty(&dcb->srb_going_list))
			dprintkl(KERN_DEBUG, 
			       "How could the ML send cmnds to the Going queue? (%02i-%i)!!\n",
			       dcb->target_id, dcb->target_lun);
		if (dcb->tag_mask)
			dprintkl(KERN_DEBUG,
			       "tag_mask for %02i-%i should be empty, is %08x!\n",
			       dcb->target_id, dcb->target_lun,
			       dcb->tag_mask);

		/* Waiting queue */
		list_for_each_entry_safe(srb, tmp, &dcb->srb_waiting_list, list) {
			int result;
			p = srb->cmd;

			result = MK_RES(0, did_flag, 0, 0);
			TRACEPRINTF("Reset(%li):%08x*", jiffies, result);
			printk(" (W)");
#if 1				/*ndef DC395x_DEBUGTRACE */
			printk("%li(%i-%i)", p->pid, p->device->id,
			       p->device->lun);
#endif
			TRACEOUT("%s\n", srb->debugtrace);
			srb_waiting_remove(dcb, srb);
			srb_free_insert(acb, srb);

			p->result = result;
			pci_unmap_srb_sense(acb, srb);
			pci_unmap_srb(acb, srb);
			if (force) {
				/* For new EH, we normally don't need to give commands back,
				 * as they all complete or all time out */
				cmd->scsi_done(cmd);
			}
		}
		if (!list_empty(&dcb->srb_waiting_list))
			printk
			    ("\nDC395x: Debug: ML queued %i cmnds again to %02i-%i\n",
			     list_size(&dcb->srb_waiting_list), dcb->target_id,
			     dcb->target_lun);

		dcb->flag &= ~ABORT_DEV_;
	}
	printk("\n");
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_shutdown   DC395x_reset
 ********************************************************************
 */
static void reset_scsi_bus(struct AdapterCtlBlk *acb)
{
	/*u32  drv_flags=0; */

	dprintkdbg(DBG_0, "reset_scsi_bus..............\n");

	/*DC395x_DRV_LOCK(drv_flags); */
	acb->acb_flag |= RESET_DEV;	/* RESET_DETECT, RESET_DONE, RESET_DEV */

	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);
	while (!(DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET));

	/*DC395x_DRV_UNLOCK(drv_flags); */
	return;
}


/* Set basic config */
static void set_basic_config(struct AdapterCtlBlk *acb)
{
	u8 bval;
	u16 wval;
	DC395x_write8(acb, TRM_S1040_SCSI_TIMEOUT, acb->sel_timeout);
	if (acb->config & HCC_PARITY)
		bval = PHASELATCH | INITIATOR | BLOCKRST | PARITYCHECK;
	else
		bval = PHASELATCH | INITIATOR | BLOCKRST;

	DC395x_write8(acb, TRM_S1040_SCSI_CONFIG0, bval);

	/* program configuration 1: Act_Neg (+ Act_Neg_Enh? + Fast_Filter? + DataDis?) */
	DC395x_write8(acb, TRM_S1040_SCSI_CONFIG1, 0x03);	/* was 0x13: default */
	/* program Host ID                  */
	DC395x_write8(acb, TRM_S1040_SCSI_HOSTID, acb->scsi_host->this_id);
	/* set ansynchronous transfer       */
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, 0x00);
	/* Turn LED control off */
	wval = DC395x_read16(acb, TRM_S1040_GEN_CONTROL) & 0x7F;
	DC395x_write16(acb, TRM_S1040_GEN_CONTROL, wval);
	/* DMA config          */
	wval = DC395x_read16(acb, TRM_S1040_DMA_CONFIG) & ~DMA_FIFO_CTRL;
	wval |=
	    DMA_FIFO_HALF_HALF | DMA_ENHANCE /*| DMA_MEM_MULTI_READ */ ;
	/*dprintkl(KERN_INFO, "DMA_Config: %04x\n", wval); */
	DC395x_write16(acb, TRM_S1040_DMA_CONFIG, wval);
	/* Clear pending interrupt status */
	DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
	/* Enable SCSI interrupt    */
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0x7F);
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, EN_SCSIINTR | EN_DMAXFERERROR
		      /*| EN_DMAXFERABORT | EN_DMAXFERCOMP | EN_FORCEDMACOMP */
		      );
}


/*
 ********************************************************************
 * scsiio
 *		dc395x_interrupt
 ********************************************************************
 */
static void scsi_reset_detect(struct AdapterCtlBlk *acb)
{
	dprintkl(KERN_INFO, "scsi_reset_detect\n");
	/* delay half a second */
	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);

	DC395x_write8(acb, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, DMARESETMODULE);
	/*DC395x_write8(acb, TRM_S1040_DMA_CONTROL,STOPDMAXFER); */
	udelay(500);
	/* Maybe we locked up the bus? Then lets wait even longer ... */
	acb->scsi_host->last_reset =
	    jiffies + 5 * HZ / 2 +
	    HZ * acb->eeprom.delay_time;

	clear_fifo(acb, "RstDet");
	set_basic_config(acb);
	/*1.25 */
	/*DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT); */

	if (acb->acb_flag & RESET_DEV) {	/* RESET_DETECT, RESET_DONE, RESET_DEV */
		acb->acb_flag |= RESET_DONE;
	} else {
		acb->acb_flag |= RESET_DETECT;
		reset_dev_param(acb);
		doing_srb_done(acb, DID_RESET, 0, 1);
		/*DC395x_RecoverSRB( acb ); */
		acb->active_dcb = NULL;
		acb->acb_flag = 0;
		waiting_process_next(acb);
	}

	return;
}


/*
 ********************************************************************
 * scsiio
 *		srb_done
 ********************************************************************
 */
static
void request_sense(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		   struct ScsiReqBlk *srb)
{
	Scsi_Cmnd *cmd;

	cmd = srb->cmd;
	dprintkdbg(DBG_KG,
	       "request_sense for pid %li, target %02i-%i\n",
	       cmd->pid, cmd->device->id, cmd->device->lun);
	TRACEPRINTF("RqSn*");
	srb->flag |= AUTO_REQSENSE;
	srb->adapter_status = 0;
	srb->target_status = 0;

	/* KG: Can this prevent crap sense data ? */
	memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));

	/* Save some data */
	srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].address =
	    srb->segment_x[0].address;
	srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].length =
	    srb->segment_x[0].length;
	srb->xferred = srb->total_xfer_length;
	/* srb->segment_x : a one entry of S/G list table */
	srb->total_xfer_length = sizeof(cmd->sense_buffer);
	srb->segment_x[0].length = sizeof(cmd->sense_buffer);
	/* Map sense buffer */
	srb->segment_x[0].address =
	    pci_map_single(acb->dev, cmd->sense_buffer,
			   sizeof(cmd->sense_buffer), PCI_DMA_FROMDEVICE);
	dprintkdbg(DBG_SGPARANOIA, "Map sense buffer at %p (%05x) to %08x\n",
	       cmd->sense_buffer, sizeof(cmd->sense_buffer),
	       srb->segment_x[0].address);
	srb->sg_count = 1;
	srb->sg_index = 0;

	if (start_scsi(acb, dcb, srb)) {	/* Should only happen, if sb. else grabs the bus */
		dprintkl(KERN_DEBUG,
		       "Request Sense failed for pid %li (%02i-%i)!\n",
		       srb->cmd->pid, dcb->target_id, dcb->target_lun);
		TRACEPRINTF("?*");
		srb_going_to_waiting_move(dcb, srb);
		waiting_set_timer(acb, HZ / 100);
	}
	TRACEPRINTF(".*");
}





/**
 * device_alloc - Allocate a new device instance. This create the
 * devices instance and sets up all the data items. The adapter
 * instance is required to obtain confiuration information for this
 * device. This does *not* add this device to the adapters device
 * list.
 *
 * @acb: The adapter to obtain configuration information from.
 * @target: The target for the new device.
 * @lun: The lun for the new device.
 *
 * Return the new device if succesfull or NULL on failure.
 **/
static
struct DeviceCtlBlk *device_alloc(struct AdapterCtlBlk *acb, u8 target, u8 lun)
{
	struct NvRamType *eeprom = &acb->eeprom;
	u8 period_index = eeprom->target[target].period & 0x07;
	struct DeviceCtlBlk *dcb;

	dcb = dc395x_kmalloc(sizeof(struct DeviceCtlBlk), GFP_ATOMIC);
	dprintkdbg(DBG_0, "device_alloc: device %p\n", dcb);
	if (!dcb) {
		return NULL;
	}
	dcb->acb = NULL;
	INIT_LIST_HEAD(&dcb->srb_going_list);
	INIT_LIST_HEAD(&dcb->srb_waiting_list);
	dcb->active_srb = NULL;
	dcb->tag_mask = 0;
	dcb->max_command = 1;
	dcb->target_id = target;
	dcb->target_lun = lun;
#ifndef DC395x_NO_DISCONNECT
	dcb->identify_msg =
	    IDENTIFY(dcb->dev_mode & NTC_DO_DISCONNECT, lun);
#else
	dcb->identify_msg = IDENTIFY(0, lun);
#endif
	dcb->dev_mode = eeprom->target[target].cfg0;
	dcb->inquiry7 = 0;
	dcb->sync_mode = 0;
	dcb->min_nego_period = clock_period[period_index];
	dcb->sync_period = 0;
	dcb->sync_offset = 0;
	dcb->flag = 0;

#ifndef DC395x_NO_WIDE
	if ((dcb->dev_mode & NTC_DO_WIDE_NEGO)
	    && (acb->config & HCC_WIDE_CARD))
		dcb->sync_mode |= WIDE_NEGO_ENABLE;
#endif
#ifndef DC395x_NO_SYNC
	if (dcb->dev_mode & NTC_DO_SYNC_NEGO)
		if (!(lun) || current_sync_offset)
			dcb->sync_mode |= SYNC_NEGO_ENABLE;
#endif
	if (dcb->target_lun != 0) {
		/* Copy settings */
		struct DeviceCtlBlk *p;
		list_for_each_entry(p, &acb->dcb_list, list)
			if (p->target_id == dcb->target_id)
				break;
		dprintkdbg(DBG_KG, 
		       "Copy settings from %02i-%02i to %02i-%02i\n",
		       p->target_id, p->target_lun,
		       dcb->target_id, dcb->target_lun);
		dcb->sync_mode = p->sync_mode;
		dcb->sync_period = p->sync_period;
		dcb->min_nego_period = p->min_nego_period;
		dcb->sync_offset = p->sync_offset;
		dcb->inquiry7 = p->inquiry7;
	}
	return dcb;
}


/**
 * adapter_add_device - Adds the device instance to the adaptor instance.
 *
 * @acb: The adapter device to be updated
 * @dcb: A newly created and intialised device instance to add.
 **/
static
void adapter_add_device(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb)
{
	/* backpointer to adapter */
	dcb->acb = acb;
	
	/* set run_robin to this device if it is currently empty */
	if (list_empty(&acb->dcb_list))
		acb->dcb_run_robin = dcb;

	/* add device to list */
	list_add_tail(&dcb->list, &acb->dcb_list);

	/* update device maps */
	acb->dcb_map[dcb->target_id] |= (1 << dcb->target_lun);
	acb->children[dcb->target_id][dcb->target_lun] = dcb;
}


/**
 * adapter_remove_device - Removes the device instance from the adaptor
 * instance. The device instance is not check in any way or freed by this. 
 * The caller is expected to take care of that. This will simply remove the
 * device from the adapters data strcutures.
 *
 * @acb: The adapter device to be updated
 * @dcb: A device that has previously been added to the adapter.
 **/
static
void adapter_remove_device(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb)
{
	struct DeviceCtlBlk *i;
	struct DeviceCtlBlk *tmp;
	dprintkdbg(DBG_0, "adapter_remove_device: Remove device (ID %i, LUN %i): %p\n",
		   dcb->target_id, dcb->target_lun, dcb);

	/* fix up any pointers to this device that we have in the adapter */
	if (acb->active_dcb == dcb)
		acb->active_dcb = NULL;
	if (acb->dcb_run_robin == dcb)
		acb->dcb_run_robin = dcb_get_next(&acb->dcb_list, dcb);

	/* unlink from list */
	list_for_each_entry_safe(i, tmp, &acb->dcb_list, list)
		if (dcb == i) {
			list_del(&i->list);
			break;
		}

	/* clear map and children */	
	acb->dcb_map[dcb->target_id] &= ~(1 << dcb->target_lun);
	acb->children[dcb->target_id][dcb->target_lun] = NULL;
	dcb->acb = NULL;
}


/**
 * adapter_remove_and_free_device - Removes a single device from the adapter
 * and then frees the device information.
 *
 * @acb: The adapter device to be updated
 * @dcb: A device that has previously been added to the adapter.
 */
static
void adapter_remove_and_free_device(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb)
{
	if (list_size(&dcb->srb_going_list) > 1) {
		dprintkdbg(DBG_DCB, "adapter_remove_and_free_device: "
		           "Won't remove because of %i active requests\n",
			   list_size(&dcb->srb_going_list));
		return;
	}
	adapter_remove_device(acb, dcb);
	dc395x_kfree(dcb);
}


/**
 * adapter_remove_and_free_all_devices - Removes and frees all of the
 * devices associated with the specified adapter.
 *
 * @acb: The adapter from which all devices should be removed.
 **/
static
void adapter_remove_and_free_all_devices(struct AdapterCtlBlk* acb)
{
	struct DeviceCtlBlk *dcb;
	struct DeviceCtlBlk *tmp;
	dprintkdbg(DBG_DCB, "adapter_remove_and_free_all_devices: Free all devices (%i devices)\n",
		   list_size(&acb->dcb_list));

	list_for_each_entry_safe(dcb, tmp, &acb->dcb_list, list)
		adapter_remove_and_free_device(acb, dcb);
}


/**
 * dc395x_slave_alloc - Called by the scsi mid layer to tell us about a new
 * scsi device that we need to deal with. We allocate a new device and then
 * insert that device into the adapters device list.
 *
 * @scsi_device: The new scsi device that we need to handle.
 **/
static
int dc395x_slave_alloc(struct scsi_device *scsi_device)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)scsi_device->host->hostdata;
	struct DeviceCtlBlk *dcb;

	dcb = device_alloc(acb, scsi_device->id, scsi_device->lun);
	if (!dcb)
		return -ENOMEM;
	adapter_add_device(acb, dcb);

	return 0;
}


/**
 * dc395x_slave_destroy - Called by the scsi mid layer to tell us about a
 * device that is going away.
 *
 * @scsi_device: The new scsi device that we need to handle.
 **/
static
void dc395x_slave_destroy(struct scsi_device *scsi_device)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)scsi_device->host->hostdata;
	struct DeviceCtlBlk *dcb = find_dcb(acb, scsi_device->id, scsi_device->lun);
	if (dcb)
		adapter_remove_and_free_device(acb, dcb);
}




/**
 * trms1040_wait_30us: wait for 30 us
 *
 * Waits for 30us (using the chip by the looks of it..)
 *
 * @io_port: base I/O address
 **/
static
void __init trms1040_wait_30us(u16 io_port)
{
	/* ScsiPortStallExecution(30); wait 30 us */
	outb(5, io_port + TRM_S1040_GEN_TIMER);
	while (!(inb(io_port + TRM_S1040_GEN_STATUS) & GTIMEOUT))
		/* nothing */ ;
	return;
}


/**
 * trms1040_write_cmd - write the secified command and address to
 * chip
 *
 * @io_port:	base I/O address
 * @cmd:	SB + op code (command) to send
 * @addr:	address to send
 **/
static
void __init trms1040_write_cmd(u16 io_port, u8 cmd, u8 addr)
{
	int i;
	u8 send_data;

	/* program SB + OP code */
	for (i = 0; i < 3; i++, cmd <<= 1) {
		send_data = NVR_SELECT;
		if (cmd & 0x04)	/* Start from bit 2 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
	}

	/* send address */
	for (i = 0; i < 7; i++, addr <<= 1) {
		send_data = NVR_SELECT;
		if (addr & 0x40)	/* Start from bit 6 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
	}
	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);
}


/**
 * trms1040_set_data - store a single byte in the eeprom
 *
 * Called from write all to write a single byte into the SSEEPROM
 * Which is done one bit at a time.
 *
 * @io_port:	base I/O address
 * @addr:	offset into EEPROM
 * @byte:	bytes to write
 **/
static
void __init trms1040_set_data(u16 io_port, u8 addr, u8 byte)
{
	int i;
	u8 send_data;

	/* Send write command & address */
	trms1040_write_cmd(io_port, 0x05, addr);

	/* Write data */
	for (i = 0; i < 8; i++, byte <<= 1) {
		send_data = NVR_SELECT;
		if (byte & 0x80)	/* Start from bit 7 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK), io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
	}
	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* Wait for write ready */
	while (1) {
		outb((NVR_SELECT | NVR_CLOCK), io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);

		outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);

		if (inb(io_port + TRM_S1040_GEN_NVRAM) & NVR_BITIN)
			break;
	}

	/*  Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
}


/**
 * trms1040_write_all - write 128 bytes to the eeprom
 *
 * Write the supplied 128 bytes to the chips SEEPROM
 *
 * @eeprom:	the data to write
 * @io_port:	the base io port
 **/
static
void __init trms1040_write_all(struct NvRamType *eeprom, u16 io_port)
{
	u8 *b_eeprom = (u8 *) eeprom;
	u8 addr;

	/* Enable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) | EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);

	/* write enable */
	trms1040_write_cmd(io_port, 0x04, 0xFF);
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* write */
	for (addr = 0; addr < 128; addr++, b_eeprom++) {
		trms1040_set_data(io_port, addr, *b_eeprom);
	}

	/* write disable */
	trms1040_write_cmd(io_port, 0x04, 0x00);
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* Disable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) & ~EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);
}


/**
 * trms1040_get_data - get a single byte from the eeprom
 *
 * Called from read all to read a single byte into the SSEEPROM
 * Which is done one bit at a time.
 *
 * @io_port:	base I/O address
 * @addr:	offset into SEEPROM
 *
 * Returns the the byte read.
 **/
static
u8 __init trms1040_get_data(u16 io_port, u8 addr)
{
	int i;
	u8 read_byte;
	u8 result = 0;

	/* Send read command & address */
	trms1040_write_cmd(io_port, 0x06, addr);

	/* read data */
	for (i = 0; i < 8; i++) {
		outb((NVR_SELECT | NVR_CLOCK), io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);

		/* Get data bit while falling edge */
		read_byte = inb(io_port + TRM_S1040_GEN_NVRAM);
		result <<= 1;
		if (read_byte & NVR_BITIN)
			result |= 1;

		trms1040_wait_30us(io_port);
	}

	/* Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	return result;
}


/**
 * trms1040_read_all - read all bytes from the eeprom
 *
 * Read the 128 bytes from the SEEPROM.
 *
 * @eeprom:	where to store the data
 * @io_port:	the base io port
 **/
static
void __init trms1040_read_all(struct NvRamType *eeprom, u16 io_port)
{
	u8 *b_eeprom = (u8 *) eeprom;
	u8 addr;

	/* Enable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) | EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);

	/* read details */
	for (addr = 0; addr < 128; addr++, b_eeprom++) {
		*b_eeprom = trms1040_get_data(io_port, addr);
	}

	/* Disable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) & ~EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);
}



/**
 * check_eeprom - get and check contents of the eeprom
 *
 * Read seeprom 128 bytes into the memory provider in eeprom.
 * Checks the checksum and if it's not correct it uses a set of default
 * values.
 *
 * @eeprom:	caller allocated strcuture to read the eeprom data into
 * @io_port:	io port to read from
 **/
static
void __init check_eeprom(struct NvRamType *eeprom, u16 io_port)
{
	u16 *w_eeprom = (u16 *) eeprom;
	u16 w_addr;
	u16 cksum;
	u32 d_addr;
	u32 *d_eeprom;

	trms1040_read_all(eeprom, io_port);	/* read eeprom */

	cksum = 0;
	for (w_addr = 0, w_eeprom = (u16 *) eeprom; w_addr < 64;
	     w_addr++, w_eeprom++)
		cksum += *w_eeprom;
	if (cksum != 0x1234) {
		/*
		 * Checksum is wrong.
		 * Load a set of defaults into the eeprom buffer
		 */
		dprintkl(KERN_WARNING,
		       "EEProm checksum error: using default values and options.\n");
		eeprom->sub_vendor_id[0] = (u8) PCI_VENDOR_ID_TEKRAM;
		eeprom->sub_vendor_id[1] = (u8) (PCI_VENDOR_ID_TEKRAM >> 8);
		eeprom->sub_sys_id[0] = (u8) PCI_DEVICE_ID_TEKRAM_TRMS1040;
		eeprom->sub_sys_id[1] =
		    (u8) (PCI_DEVICE_ID_TEKRAM_TRMS1040 >> 8);
		eeprom->sub_class = 0x00;
		eeprom->vendor_id[0] = (u8) PCI_VENDOR_ID_TEKRAM;
		eeprom->vendor_id[1] = (u8) (PCI_VENDOR_ID_TEKRAM >> 8);
		eeprom->device_id[0] = (u8) PCI_DEVICE_ID_TEKRAM_TRMS1040;
		eeprom->device_id[1] =
		    (u8) (PCI_DEVICE_ID_TEKRAM_TRMS1040 >> 8);
		eeprom->reserved = 0x00;

		for (d_addr = 0, d_eeprom = (u32 *) eeprom->target;
		     d_addr < 16; d_addr++, d_eeprom++)
			*d_eeprom = 0x00000077;	/* cfg3,cfg2,period,cfg0 */

		*d_eeprom++ = 0x04000F07;	/* max_tag,delay_time,channel_cfg,scsi_id */
		*d_eeprom++ = 0x00000015;	/* reserved1,boot_lun,boot_target,reserved0 */
		for (d_addr = 0; d_addr < 12; d_addr++, d_eeprom++)
			*d_eeprom = 0x00;

		/* Now load defaults (maybe set by boot/module params) */
		set_safe_settings();
		fix_settings();
		eeprom_override(eeprom);

		eeprom->cksum = 0x00;
		for (w_addr = 0, cksum = 0, w_eeprom = (u16 *) eeprom;
		     w_addr < 63; w_addr++, w_eeprom++)
			cksum += *w_eeprom;

		*w_eeprom = 0x1234 - cksum;
		trms1040_write_all(eeprom, io_port);
		eeprom->delay_time = cfg_data[CFG_RESET_DELAY].value;
	} else {
		set_safe_settings();
		eeprom_index_to_delay(eeprom);
		eeprom_override(eeprom);
	}
}




/**
 * print_eeprom_settings - output the eeprom settings
 * to the kernel log so people can see what they were.
 *
 * @eeprom: The eeprom data strucutre to show details for.
 **/
static
void __init print_eeprom_settings(struct NvRamType *eeprom)
{
	dprintkl(KERN_INFO, "Used settings: AdapterID=%02i, Speed=%i(%02i.%01iMHz), dev_mode=0x%02x\n",
	       eeprom->scsi_id,
	       eeprom->target[0].period,
	       clock_speed[eeprom->target[0].period] / 10,
	       clock_speed[eeprom->target[0].period] % 10,
	       eeprom->target[0].cfg0);
	dprintkl(KERN_INFO, "               AdaptMode=0x%02x, Tags=%i(%02i), DelayReset=%is\n",
	       eeprom->channel_cfg,
	       eeprom->max_tag,
	       1 << eeprom->max_tag,
	       eeprom->delay_time);
}



#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
/*
 * Memory for trace buffers
 */
static
void free_tracebufs(struct AdapterCtlBlk *acb)
{
	int i;
	const unsigned bufs_per_page = PAGE_SIZE / DEBUGTRACEBUFSZ;

	for (i = 0; i < srb_idx; i += bufs_per_page)
		if (acb->srb_array[i].debugtrace)
			dc395x_kfree(acb->srb_array[i].debugtrace);
}


static
int alloc_tracebufs(struct AdapterCtlBlk *acb)
{
	const unsigned mem_needed =
	    (DC395x_MAX_SRB_CNT + 1) * DEBUGTRACEBUFSZ;
	int pages = (mem_needed + (PAGE_SIZE - 1)) / PAGE_SIZE;
	const unsigned bufs_per_page = PAGE_SIZE / DEBUGTRACEBUFSZ;
	int srb_idx = 0;
	unsigned i = 0;
	unsigned char *ptr;

	for (i = 0; i < DC395x_MAX_SRB_CNT; i++)
		acb->srb_array[i].debugtrace = NULL;

	while (pages--) {
		ptr = dc395x_kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!ptr) {
			free_tracebufs(acb);
			return 1;
		}
		/*dprintkl(KERN_DEBUG, "Alloc %li bytes at %p for tracebuf %i\n", */
		/*      PAGE_SIZE, ptr, srb_idx); */
		i = 0;
		while (i < bufs_per_page && srb_idx < DC395x_MAX_SRB_CNT)
			acb->srb_array[srb_idx++].debugtrace =
			    ptr + (i++ * DEBUGTRACEBUFSZ);
	}
	if (i < bufs_per_page) {
		acb->srb.debugtrace = ptr + (i * DEBUGTRACEBUFSZ);
		acb->srb.debugtrace[0] = 0;
	} else
		dprintkl(KERN_DEBUG, "No space for tmsrb tracebuf reserved?!\n");
	return 0;
}
#else
static void free_tracebufs(struct AdapterCtlBlk *acb) {}
static int alloc_tracebufs(struct AdapterCtlBlk *acb) { return 0; }
#endif

/* Free SG tables */
static
void adapter_sg_tables_free(struct AdapterCtlBlk *acb)
{
	int i;
	const unsigned srbs_per_page = PAGE_SIZE/(DC395x_MAX_SG_LISTENTRY
						  *sizeof(struct SGentry));

	for (i = 0; i < DC395x_MAX_SRB_CNT; i += srbs_per_page)
		if (acb->srb_array[i].segment_x)
			dc395x_kfree(acb->srb_array[i].segment_x);
}


/*
 * Allocate SG tables; as we have to pci_map them, an SG list (struct SGentry*)
 * should never cross a page boundary */
static
int __init adapter_sg_tables_alloc(struct AdapterCtlBlk *acb)
{
	const unsigned mem_needed = (DC395x_MAX_SRB_CNT+1)
	                            *DC395x_MAX_SG_LISTENTRY
	                            *sizeof(struct SGentry);
	int pages = (mem_needed+(PAGE_SIZE-1))/PAGE_SIZE;
	const unsigned srbs_per_page = PAGE_SIZE/(DC395x_MAX_SG_LISTENTRY
	                                          *sizeof(struct SGentry));
	int srb_idx = 0;
	unsigned i = 0;
	struct SGentry *ptr;

	for (i = 0; i < DC395x_MAX_SRB_CNT; i++)
		acb->srb_array[i].segment_x = NULL;

	dprintkdbg(DBG_1, "Allocate %i pages for SG tables\n", pages);
	while (pages--) {
		ptr = (struct SGentry *)dc395x_kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!ptr) {
			adapter_sg_tables_free(acb);
			return 1;
		}
		dprintkdbg(DBG_1, "Allocate %li bytes at %p for SG segments %i\n",
				  PAGE_SIZE, ptr, srb_idx);
		i = 0;
		while (i < srbs_per_page && srb_idx < DC395x_MAX_SRB_CNT)
			acb->srb_array[srb_idx++].segment_x =
			    ptr + (i++ * DC395x_MAX_SG_LISTENTRY);
	}
	if (i < srbs_per_page)
		acb->srb.segment_x =
		    ptr + (i * DC395x_MAX_SG_LISTENTRY);
	else
		dprintkl(KERN_DEBUG, "No space for tmsrb SG table reserved?!\n");
	return 0;
}



/**
 * adapter_print_config - print adapter connection and termination
 * config
 *
 * The io port in the adapter needs to have been set before calling
 * this function.
 *
 * @acb: The adapter to print the information for.
 **/
static
void __init adapter_print_config(struct AdapterCtlBlk *acb)
{
	u8 bval;

	bval = DC395x_read8(acb, TRM_S1040_GEN_STATUS);
	dprintkl(KERN_INFO, "%s Connectors: ",
	       ((bval & WIDESCSI) ? "(Wide)" : ""));
	if (!(bval & CON5068))
		printk("ext%s ", !(bval & EXT68HIGH) ? "68" : "50");
	if (!(bval & CON68))
		printk("int68%s ", !(bval & INT68HIGH) ? "" : "(50)");
	if (!(bval & CON50))
		printk("int50 ");
	if ((bval & (CON5068 | CON50 | CON68)) ==
	    0 /*(CON5068 | CON50 | CON68) */ )
		printk(" Oops! (All 3?) ");
	bval = DC395x_read8(acb, TRM_S1040_GEN_CONTROL);
	printk(" Termination: ");
	if (bval & DIS_TERM)
		printk("Disabled\n");
	else {
		if (bval & AUTOTERM)
			printk("Auto ");
		if (bval & LOW8TERM)
			printk("Low ");
		if (bval & UP8TERM)
			printk("High ");
		printk("\n");
	}
}


/**
 * adapter_init_params - Initialize the various parameters in the
 * adapter structure. Note that the pointer to the scsi_host is set
 * early (when this instance is created) and the io_port and irq
 * values are set later after they have been reserved. This just gets
 * everything set to a good starting position.
 *
 * The eeprom structure in the adapter needs to have been set before
 * calling this function.
 *
 * @acb: The adapter to initialize.
 **/
static
void __init adapter_init_params(struct AdapterCtlBlk *acb)
{
	struct NvRamType *eeprom = &acb->eeprom;
	int i;

	/* NOTE: acb->scsi_host is set at scsi_host/acb creation time */
	/* NOTE: acb->io_port_base is set at port registration time */
	/* NOTE: acb->io_port_len is set at port registration time */

	INIT_LIST_HEAD(&acb->dcb_list);
	acb->dcb_run_robin = NULL;
	acb->active_dcb = NULL;

	INIT_LIST_HEAD(&acb->srb_free_list);
	/*  temp SRB for Q tag used or abort command used  */
	acb->tmp_srb = &acb->srb;
	init_timer(&acb->waiting_timer);
	init_timer(&acb->selto_timer);

	acb->srb_count = DC395x_MAX_SRB_CNT;

	acb->sel_timeout = DC395x_SEL_TIMEOUT;	/* timeout=250ms */
	/* NOTE: acb->irq_level is set at IRQ registration time */

	acb->tag_max_num = 1 << eeprom->max_tag;
	if (acb->tag_max_num > 30)
		acb->tag_max_num = 30;

	acb->acb_flag = 0;	/* RESET_DETECT, RESET_DONE, RESET_DEV */
	acb->gmode2 = eeprom->channel_cfg;
	acb->config = 0;	/* NOTE: actually set in adapter_init_chip */

	if (eeprom->channel_cfg & NAC_SCANLUN)
		acb->lun_chk = 1;
	acb->scan_devices = 1;

	acb->scsi_host->this_id = eeprom->scsi_id;
	acb->hostid_bit = (1 << acb->scsi_host->this_id);

	for (i = 0; i < DC395x_MAX_SCSI_ID; i++)
		acb->dcb_map[i] = 0;

	acb->msg_len = 0;
	
	/* link static array of srbs into the srb free list */
	for (i = 0; i < acb->srb_count - 1; i++)
		srb_free_insert(acb, &acb->srb_array[i]);
}


/**
 * adapter_init_host - Initialize the scsi host instance based on
 * values that we have already stored in the adapter instance. There's
 * some mention that a lot of these are deprecated, so we won't use
 * them (we'll use the ones in the adapter instance) but we'll fill
 * them in in case something else needs them.
 *
 * The eeprom structure, irq and io ports in the adapter need to have
 * been set before calling this function.
 *
 * @host: The scsi host instance to fill in the values for.
 **/
static
void __init adapter_init_scsi_host(struct Scsi_Host *host)
{
        struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)host->hostdata;
	struct NvRamType *eeprom = &acb->eeprom;
        
	host->max_cmd_len = 24;
	host->can_queue = DC395x_MAX_CMD_QUEUE;
	host->cmd_per_lun = DC395x_MAX_CMD_PER_LUN;
	host->this_id = (int)eeprom->scsi_id;
	host->io_port = acb->io_port_base;
	host->n_io_port = acb->io_port_len;
	host->dma_channel = -1;
	host->unique_id = acb->io_port_base;
	host->irq = acb->irq_level;
	host->last_reset = jiffies;

	host->max_id = 16;
	if (host->max_id - 1 == eeprom->scsi_id)
		host->max_id--;

#ifdef CONFIG_SCSI_MULTI_LUN
	if (eeprom->channel_cfg & NAC_SCANLUN)
		host->max_lun = 8;
	else
		host->max_lun = 1;
#else
	host->max_lun = 1;
#endif

}


/**
 * adapter_init_chip - Get the chip into a know state and figure out
 * some of the settings that apply to this adapter.
 *
 * The io port in the adapter needs to have been set before calling
 * this function. The config will be configured correctly on return.
 *
 * @acb: The adapter which we are to init.
 **/
void __init adapter_init_chip(struct AdapterCtlBlk *acb)
{
        struct NvRamType *eeprom = &acb->eeprom;
        
        /* Mask all the interrupt */
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, 0x00);
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0x00);

	/* Reset SCSI module */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);

	/* Reset PCI/DMA module */
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, DMARESETMODULE);
	udelay(20);

	/* program configuration 0 */
	acb->config = HCC_AUTOTERM | HCC_PARITY;
	if (DC395x_read8(acb, TRM_S1040_GEN_STATUS) & WIDESCSI)
		acb->config |= HCC_WIDE_CARD;

	if (eeprom->channel_cfg & NAC_POWERON_SCSI_RESET)
		acb->config |= HCC_SCSI_RESET;

	if (acb->config & HCC_SCSI_RESET) {
		dprintkl(KERN_INFO, "Performing initial SCSI bus reset\n");
		DC395x_write8(acb, TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);

		/*while (!( DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET )); */
		/*spin_unlock_irq (&io_request_lock); */
		udelay(500);

		acb->scsi_host->last_reset =
		    jiffies + HZ / 2 +
		    HZ * acb->eeprom.delay_time;

		/*spin_lock_irq (&io_request_lock); */
	}
}


/**
 * init_adapter - Grab the resource for the card, setup the adapter
 * information, set the card into a known state, create the various
 * tables etc etc. This basically gets all adapter information all up
 * to date, intialised and gets the chip in sync with it.
 *
 * @host:	This hosts adapter structure
 * @io_port:	The base I/O port
 * @irq:	IRQ
 *
 * Returns 0 if the initialization succeeds, any other value on
 * failure.
 **/
static
int __init adapter_init(struct AdapterCtlBlk *acb, u32 io_port, u32 io_port_len, u8 irq)
{
	if (!request_region(io_port, io_port_len, DC395X_NAME)) {
		dprintkl(KERN_ERR, "Failed to reserve IO region 0x%x\n", io_port);
		goto failed;
	}
	/* store port base to indicate we have registered it */
	acb->io_port_base = io_port;
	acb->io_port_len = io_port_len;
	
	if (request_irq(irq, dc395x_interrupt, SA_SHIRQ, DC395X_NAME, acb)) {
	    	/* release the region we just claimed */
		dprintkl(KERN_INFO, "Failed to register IRQ\n");
		goto failed;
	}
	/* store irq to indicate we have registered it */
	acb->irq_level = irq;

	/* get eeprom configuration information and command line settings etc */
	check_eeprom(&acb->eeprom, (u16)io_port);
 	print_eeprom_settings(&acb->eeprom);

	/* setup adapter control block */	
	adapter_init_params(acb);
	
	/* display card connectors/termination settings */
 	adapter_print_config(acb);

	if (adapter_sg_tables_alloc(acb)) {
		dprintkl(KERN_DEBUG, "Memory allocation for SG tables failed\n");
		goto failed;
	}
	if (alloc_tracebufs(acb)) {
		dprintkl(KERN_DEBUG, "Memory allocation for trace buffers failed\n");
		goto failed;
	}
	adapter_init_scsi_host(acb->scsi_host);
	adapter_init_chip(acb);
	set_basic_config(acb);

	dprintkdbg(DBG_0, "adapter_init: acb=%p, pdcb_map=%p "
	                  "psrb_array=%p ACB size=%04x, DCB size=%04x "
	                  "SRB size=%04x\n",
		   acb, acb->dcb_map, acb->srb_array, sizeof(struct AdapterCtlBlk),
		   sizeof(struct DeviceCtlBlk), sizeof(struct ScsiReqBlk));
	return 0;

failed:
	if (acb->irq_level)
		free_irq(acb->irq_level, acb);
	if (acb->io_port_base)
		release_region(acb->io_port_base, acb->io_port_len);
	adapter_sg_tables_free(acb);
	free_tracebufs(acb);

	return 1;
}


/**
 * adapter_uninit_chip - cleanly shut down the scsi controller chip,
 * stopping all operations and disabling interrupt generation on the
 * card.
 *
 * @acb: The adapter which we are to shutdown.
 **/
static
void adapter_uninit_chip(struct AdapterCtlBlk *acb)
{
	/* disable interrupts */
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, 0);
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0);

	/* reset the scsi bus */
	if (acb->config & HCC_SCSI_RESET)
		reset_scsi_bus(acb);

	/* clear any pending interupt state */
	DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
}



/**
 * adapter_uninit - Shut down the chip and release any resources that
 * we had allocated. Once this returns the adapter should not be used
 * anymore.
 *
 * @acb: The adapter which we are to un-initialize.
 **/
static
void adapter_uninit(struct AdapterCtlBlk *acb)
{
	unsigned long flags;
	DC395x_LOCK_IO(acb->scsi_host, flags);

	/* remove timers */
	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);
	if (timer_pending(&acb->selto_timer))
		del_timer(&acb->selto_timer);

	adapter_uninit_chip(acb);
	adapter_remove_and_free_all_devices(acb);
	DC395x_UNLOCK_IO(acb->scsi_host, flags);

	if (acb->irq_level)
		free_irq(acb->irq_level, acb);
	if (acb->io_port_base)
		release_region(acb->io_port_base, acb->io_port_len);

	adapter_sg_tables_free(acb);
	free_tracebufs(acb);
}


/*
 ******************************************************************
 * Function: dc395x_proc_info(char* buffer, char **start,
 *			 off_t offset, int length, int hostno, int inout)
 *  Purpose: return SCSI Adapter/Device Info
 *    Input:
 *          buffer: Pointer to a buffer where to write info
 *		 start :
 *		 offset:
 *		 hostno: Host adapter index
 *		 inout : Read (=0) or set(!=0) info
 *   Output:
 *          buffer: contains info length 
 *		         
 *    return value: length of info in buffer
 *
 ******************************************************************
 */

/* KG: dc395x_proc_info taken from driver aha152x.c */

#undef SPRINTF
#define SPRINTF(args...) pos += sprintf(pos, args)

#undef YESNO
#define YESNO(YN) \
 if (YN) SPRINTF(" Yes ");\
 else SPRINTF(" No  ")

static
int dc395x_proc_info(struct Scsi_Host *host, char *buffer, char **start, off_t offset, int length,
		     int inout)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)host->hostdata;
	int spd, spd1;
	char *pos = buffer;
	struct DeviceCtlBlk *dcb;
	unsigned long flags;
	int dev;

	if (inout)		/* Has data been written to the file ? */
		return -EPERM;

	SPRINTF(DC395X_BANNER " PCI SCSI Host Adapter\n");
	SPRINTF(" Driver Version " DC395X_VERSION "\n");

	DC395x_LOCK_IO(acb->scsi_host, flags);

	SPRINTF("SCSI Host Nr %i, ", host->host_no);
	SPRINTF("DC395U/UW/F DC315/U %s\n",
		(acb->config & HCC_WIDE_CARD) ? "Wide" : "");
	SPRINTF("io_port_base 0x%04x, ", acb->io_port_base);
	SPRINTF("irq_level 0x%02x, ", acb->irq_level);
	SPRINTF(" SelTimeout %ims\n", (1638 * acb->sel_timeout) / 1000);

	SPRINTF("MaxID %i, MaxLUN %i, ", host->max_id, host->max_lun);
	SPRINTF("AdapterID %i\n", host->this_id);

	SPRINTF("tag_max_num %i", acb->tag_max_num);
	/*SPRINTF(", DMA_Status %i\n", DC395x_read8(acb, TRM_S1040_DMA_STATUS)); */
	SPRINTF(", FilterCfg 0x%02x",
		DC395x_read8(acb, TRM_S1040_SCSI_CONFIG1));
	SPRINTF(", DelayReset %is\n", acb->eeprom.delay_time);
	/*SPRINTF("\n"); */

	SPRINTF("Nr of DCBs: %i\n", list_size(&acb->dcb_list));
	SPRINTF
	    ("Map of attached LUNs: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     acb->dcb_map[0], acb->dcb_map[1], acb->dcb_map[2],
	     acb->dcb_map[3], acb->dcb_map[4], acb->dcb_map[5],
	     acb->dcb_map[6], acb->dcb_map[7]);
	SPRINTF
	    ("                      %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     acb->dcb_map[8], acb->dcb_map[9], acb->dcb_map[10],
	     acb->dcb_map[11], acb->dcb_map[12], acb->dcb_map[13],
	     acb->dcb_map[14], acb->dcb_map[15]);

	SPRINTF
	    ("Un ID LUN Prty Sync Wide DsCn SndS TagQ nego_period SyncFreq SyncOffs MaxCmd\n");

	dev = 0;
	list_for_each_entry(dcb, &acb->dcb_list, list) {
		int nego_period;
		SPRINTF("%02i %02i  %02i ", dev, dcb->target_id,
			dcb->target_lun);
		YESNO(dcb->dev_mode & NTC_DO_PARITY_CHK);
		YESNO(dcb->sync_offset);
		YESNO(dcb->sync_period & WIDE_SYNC);
		YESNO(dcb->dev_mode & NTC_DO_DISCONNECT);
		YESNO(dcb->dev_mode & NTC_DO_SEND_START);
		YESNO(dcb->sync_mode & EN_TAG_QUEUEING);
		nego_period = clock_period[dcb->sync_period & 0x07] << 2;
		if (dcb->sync_offset)
			SPRINTF("  %03i ns ", nego_period);
		else
			SPRINTF(" (%03i ns)", (dcb->min_nego_period << 2));

		if (dcb->sync_offset & 0x0f) {
			spd = 1000 / (nego_period);
			spd1 = 1000 % (nego_period);
			spd1 = (spd1 * 10 + nego_period / 2) / (nego_period);
			SPRINTF("   %2i.%1i M     %02i ", spd, spd1,
				(dcb->sync_offset & 0x0f));
		} else
			SPRINTF("                 ");

		/* Add more info ... */
		SPRINTF("     %02i\n", dcb->max_command);
		dev++;
	}

	if (timer_pending(&acb->waiting_timer))
		SPRINTF("Waiting queue timer running\n");
	else
		SPRINTF("\n");

	list_for_each_entry(dcb, &acb->dcb_list, list) {
		struct ScsiReqBlk *srb;
		if (!list_empty(&dcb->srb_waiting_list))
			SPRINTF("DCB (%02i-%i): Waiting: %i:",
				dcb->target_id, dcb->target_lun,
				list_size(&dcb->srb_waiting_list));
                list_for_each_entry(srb, &dcb->srb_waiting_list, list)
			SPRINTF(" %li", srb->cmd->pid);
		if (!list_empty(&dcb->srb_going_list))
			SPRINTF("\nDCB (%02i-%i): Going  : %i:",
				dcb->target_id, dcb->target_lun,
				list_size(&dcb->srb_going_list));
		list_for_each_entry(srb, &dcb->srb_going_list, list)
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
			SPRINTF("\n  %s", srb->debugtrace);
#else
			SPRINTF(" %li", srb->cmd->pid);
#endif
		if (!list_empty(&dcb->srb_waiting_list) || !list_empty(&dcb->srb_going_list))
			SPRINTF("\n");
	}

	if (debug_enabled(DBG_DCB)) {
		SPRINTF("DCB list for ACB %p:\n", acb);
		list_for_each_entry(dcb, &acb->dcb_list, list) {
			SPRINTF("%p -> ", dcb);
		}
		SPRINTF("END\n");
	}

	*start = buffer + offset;
	DC395x_UNLOCK_IO(acb->scsi_host, flags);

	if (pos - buffer < offset)
		return 0;
	else if (pos - buffer - offset < length)
		return pos - buffer - offset;
	else
		return length;
}




/*
 * SCSI host template
 */
static Scsi_Host_Template dc395x_driver_template = {
	.module                 = THIS_MODULE,
	.proc_name              = DC395X_NAME,
	.proc_info              = dc395x_proc_info,
	.name                   = DC395X_BANNER " " DC395X_VERSION,
	.queuecommand           = dc395x_queue_command,
	.bios_param             = dc395x_bios_param,
	.slave_alloc            = dc395x_slave_alloc,
	.slave_destroy          = dc395x_slave_destroy,
	.can_queue              = DC395x_MAX_CAN_QUEUE,
	.this_id                = 7,
	.sg_tablesize           = DC395x_MAX_SG_TABLESIZE,
	.cmd_per_lun            = DC395x_MAX_CMD_PER_LUN,
	.eh_abort_handler       = dc395x_eh_abort,
	.eh_bus_reset_handler   = dc395x_eh_bus_reset,
	.unchecked_isa_dma      = 0,
	.use_clustering         = DISABLE_CLUSTERING,
};


/**
 * banner_display - Display banner on first instance of driver
 * initialized.
 **/
static
void banner_display(void)
{
	static int banner_done = 0;
	if (!banner_done)
	{
		dprintkl(KERN_INFO, "%s %s\n", DC395X_BANNER, DC395X_VERSION);
		banner_done = 1;
	}
}


/**
 * dc395x_init_one - Initialise a single instance of the adapter.
 *
 * The PCI layer will call this once for each instance of the adapter
 * that it finds in the system. The pci_dev strcuture indicates which
 * instance we are being called from.
 * 
 * @dev: The PCI device to intialize.
 * @id: Looks like a pointer to the entry in our pci device table
 * that was actually matched by the PCI subsystem.
 *
 * Returns 0 on success, or an error code (-ve) on failure.
 **/
static
int __devinit dc395x_init_one(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	struct Scsi_Host *scsi_host;
	struct AdapterCtlBlk *acb;
	unsigned int io_port_base;
	unsigned int io_port_len;
	u8 irq;
	
	dprintkdbg(DBG_0, "Init one instance (%s)\n", pci_name(dev));
	banner_display();

	if (pci_enable_device(dev))
	{
		dprintkl(KERN_INFO, "PCI Enable device failed.\n");
		return -ENODEV;
	}
	io_port_base = pci_resource_start(dev, 0) & PCI_BASE_ADDRESS_IO_MASK;
	io_port_len = pci_resource_len(dev, 0);
	irq = dev->irq;
	dprintkdbg(DBG_0, "IO_PORT=%04x, IRQ=%x\n", io_port_base, dev->irq);

	/* allocate scsi host information (includes out adapter) */
	scsi_host = scsi_host_alloc(&dc395x_driver_template,
				    sizeof(struct AdapterCtlBlk));
	if (!scsi_host) {
		dprintkl(KERN_INFO, "scsi_host_alloc failed\n");
		return -ENOMEM;
	}
 	acb = (struct AdapterCtlBlk*)scsi_host->hostdata;
 	acb->scsi_host = scsi_host;

	/* initialise the adapter and everything we need */
 	if (adapter_init(acb, io_port_base, io_port_len, irq)) {
		dprintkl(KERN_INFO, "DC395x_initAdapter initial ERROR\n");
		scsi_host_put(scsi_host);
		return -ENODEV;
	}

	pci_set_master(dev);

	/* get the scsi mid level to scan for new devices on the bus */
	if (scsi_add_host(scsi_host, &dev->dev)) {
		dprintkl(KERN_ERR, "scsi_add_host failed\n");
		adapter_uninit(acb);
                scsi_host_put(scsi_host);
		return -ENODEV;
	}
	pci_set_drvdata(dev, scsi_host);
	scsi_scan_host(scsi_host);
        	
	return 0;
}


/**
 * dc395x_remove_one - Called to remove a single instance of the
 * adapter.
 *
 * @dev: The PCI device to intialize.
 **/
static void __devexit dc395x_remove_one(struct pci_dev *dev)
{
	struct Scsi_Host *scsi_host = pci_get_drvdata(dev);
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)(scsi_host->hostdata);

	dprintkdbg(DBG_0, "Removing instance\n");

	scsi_remove_host(scsi_host);
	adapter_uninit(acb);
	scsi_host_put(scsi_host);
	pci_set_drvdata(dev, NULL);
}


/*
 * Table which identifies the PCI devices which
 * are handled by this device driver.
 */
static struct pci_device_id dc395x_pci_table[] = {
	{
		.vendor		= PCI_VENDOR_ID_TEKRAM,
		.device		= PCI_DEVICE_ID_TEKRAM_TRMS1040,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	 },
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, dc395x_pci_table);


/*
 * PCI driver operations.
 * Tells the PCI sub system what can be done with the card.
 */
static struct pci_driver dc395x_driver = {
	.name           = DC395X_NAME,
	.id_table       = dc395x_pci_table,
	.probe          = dc395x_init_one,
	.remove         = __devexit_p(dc395x_remove_one),
};


/**
 * dc395x_module_init - Module initialization function
 *
 * Used by both module and built-in driver to initialise this driver.
 **/
static
int __init dc395x_module_init(void)
{
	return pci_module_init(&dc395x_driver);
}


/**
 * dc395x_module_exit - Module cleanup function.
 **/
static
void __exit dc395x_module_exit(void)
{
	pci_unregister_driver(&dc395x_driver);
}


module_init(dc395x_module_init);
module_exit(dc395x_module_exit);

MODULE_AUTHOR("C.L. Huang / Erich Chen / Kurt Garloff");
MODULE_DESCRIPTION("SCSI host adapter driver for Tekram TRM-S1040 based adapters: Tekram DC395 and DC315 series");
MODULE_LICENSE("GPL");
