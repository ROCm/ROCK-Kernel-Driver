/*
 * SCSI definitions...
 * Largely written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 *
 * $FreeBSD: src/sys/cam/scsi/scsi_all.h,v 1.21 2002/10/08 17:12:44 ken Exp $
 *
 * Copyright (c) 2003, 2004 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id$
 */

#ifndef	_AICLIB_H
#define _AICLIB_H

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <asm/io.h>

#include <linux/slab.h>
#include <linux/interrupt.h> /* For tasklet support. */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <linux/blk.h>
#endif
#include <linux/blkdev.h>

#include "scsi.h"
#include "hosts.h"

/* Name space conflict with BSD queue macros */
#ifdef LIST_HEAD
#undef LIST_HEAD
#endif
#include "cam.h"
#include "queue.h"
#include "scsi_message.h"
#include "scsi_iu.h"

/*
 * Linux Interrupt Support.
 */
#ifndef IRQ_RETVAL
typedef void irqreturn_t;
#define	IRQ_RETVAL(x)
#endif

/*
 * Linux Timer Support.
 */
#define AIC_USECS_PER_JIFFY (1000000/HZ)

/**************************** Module Library Hack *****************************/
/*
 * What we'd like to do is have a single "scsi library" module that both the
 * aic7xxx and aic79xx drivers could load and depend on.  A cursory examination
 * of implementing module dependencies in Linux (handling the install and
 * initrd cases) does not look promissing.  For now, we just duplicate this
 * code in both drivers using a simple symbol renaming scheme that hides this
 * hack from the drivers.
 */
#define AIC_LIB_ENTRY_CONCAT(x, prefix)	prefix ## x
#define	AIC_LIB_ENTRY_EXPAND(x, prefix) AIC_LIB_ENTRY_CONCAT(x, prefix)
#define AIC_LIB_ENTRY(x)		AIC_LIB_ENTRY_EXPAND(x, AIC_LIB_PREFIX)

#define AIC_CONST_ENTRY(x)		AIC_LIB_ENTRY_EXPAND(x,AIC_CONST_PREFIX)

#define	aic_sense_desc			AIC_LIB_ENTRY(_sense_desc)
#define	aic_sense_error_action		AIC_LIB_ENTRY(_sense_error_action)
#define	aic_error_action		AIC_LIB_ENTRY(_error_action)
#define	aic_op_desc			AIC_LIB_ENTRY(_op_desc)
#define	aic_cdb_string			AIC_LIB_ENTRY(_cdb_string)
#define aic_print_inquiry		AIC_LIB_ENTRY(_print_inquiry)
#define aic_calc_syncsrate		AIC_LIB_ENTRY(_calc_syncrate)
#define	aic_calc_syncparam		AIC_LIB_ENTRY(_calc_syncparam)
#define	aic_calc_speed			AIC_LIB_ENTRY(_calc_speed)
#define	aic_inquiry_match		AIC_LIB_ENTRY(_inquiry_match)
#define	aic_static_inquiry_match	AIC_LIB_ENTRY(_static_inquiry_match)
#define	aic_parse_brace_option		AIC_LIB_ENTRY(_parse_brace_option)
#define	aic_power_state_change		AIC_LIB_ENTRY(_power_state_change)
#define	aic_sysrq_handler		AIC_LIB_ENTRY(_sysrq_handler)
#define	aic_install_sysrq		AIC_LIB_ENTRY(_install_sysrq)
#define	aic_remove_sysrq		AIC_LIB_ENTRY(_remove_sysrq)
#define	aic_list_lockinit		AIC_LIB_ENTRY(_list_lockinit)
#define	aic_list_lock			AIC_LIB_ENTRY(_list_lock)
#define	aic_list_unlock			AIC_LIB_ENTRY(_list_unlock)
#define	aic_entrypoint_lock		AIC_LIB_ENTRY(_entrypoint_lock)
#define	aic_entrypoint_unlock		AIC_LIB_ENTRY(_entrypoint_unlock)
#define	aic_lockinit			AIC_LIB_ENTRY(_lockinit)
#define	aic_lock			AIC_LIB_ENTRY(_lock)
#define	aic_unlock			AIC_LIB_ENTRY(_unlock)
#define	aic_dump_card_state		AIC_LIB_ENTRY(_dump_card_state)
#define	aic_linux_dv_complete		AIC_LIB_ENTRY(_linux_dv_complete)
#define	aic_linux_run_device_queue	AIC_LIB_ENTRY(_linux_run_device_queue)
#define	aic_linux_dv_timeout		AIC_LIB_ENTRY(_linux_dv_timeout)
#define	aic_linux_midlayer_timeout	AIC_LIB_ENTRY(_linux_midlayer_timeout)
#define	aic_freeze_simq			AIC_LIB_ENTRY(_freeze_simq)
#define	aic_bus_settle_complete		AIC_LIB_ENTRY(_bus_settle_complete)
#define	aic_release_simq		AIC_LIB_ENTRY(_release_simq)
#define	aic_release_simq		AIC_LIB_ENTRY(_release_simq)
#define	aic_release_simq_locked		AIC_LIB_ENTRY(_release_simq_locked)
#define	aic_dma_tag_create		AIC_LIB_ENTRY(_dma_tag_create)
#define	aic_dma_tag_destroy		AIC_LIB_ENTRY(_dma_tag_destroy)
#define	aic_dmamem_alloc		AIC_LIB_ENTRY(_dmamem_alloc)
#define	aic_dmamem_free			AIC_LIB_ENTRY(_dmamem_free)
#define	aic_dmamap_create		AIC_LIB_ENTRY(_dmamap_create)
#define	aic_dmamap_destroy		AIC_LIB_ENTRY(_dmamap_destroy)
#define	aic_dmamap_load			AIC_LIB_ENTRY(_dmamap_load)
#define	aic_dmamap_unload		AIC_LIB_ENTRY(_dmamap_unload)
#define	aic_dmamap_destroy		AIC_LIB_ENTRY(_dmamap_destroy)
#define	aic_timeout			AIC_LIB_ENTRY(_timeout)
#define	aic_runq_tasklet		AIC_LIB_ENTRY(_runq_tasklet)
#define	aic_unblock_tasklet		AIC_LIB_ENTRY(_unblock_tasklet)
#define	aic_platform_timeout		AIC_LIB_ENTRY(_platform_timeout)
#define	aic_name			AIC_LIB_ENTRY(_name)

#define aic_list_spinlock		AIC_LIB_ENTRY(_list_spinlock)
#define	aic_tailq			AIC_LIB_ENTRY(_tailq)
#define	aic_softc			AIC_LIB_ENTRY(_softc)
#define	aic_transinfo			AIC_LIB_ENTRY(_transinfo)
#define	aic_platform_data		AIC_LIB_ENTRY(_platform_data)
#define	aic_devinfo			AIC_LIB_ENTRY(_devinfo)
#define	aic_callback_t			AIC_LIB_ENTRY(_callback_t)

#define	AIC_NUM_LUNS			AIC_CONST_ENTRY(_NUM_LUNS)
#define	AIC_NUM_TARGETS			AIC_CONST_ENTRY(_NUM_TARGETS)
#define	AIC_RESOURCE_SHORTAGE		AIC_CONST_ENTRY(_RESOURCE_SHORTAGE)

/*************************** Forward Declarations *****************************/
struct aic_softc;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
typedef struct device *aic_dev_softc_t;
#else
typedef struct pci_dev *aic_dev_softc_t;
#endif
typedef Scsi_Cmnd     *aic_io_ctx_t;

/*************************** Timer DataStructures *****************************/
typedef struct timer_list aic_timer_t;

/***************************** Bus Space/DMA **********************************/

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,2,17)
typedef dma_addr_t bus_addr_t;
#else
typedef uint32_t bus_addr_t;
#endif
typedef uint32_t bus_size_t;

typedef enum {
	BUS_SPACE_MEMIO,
	BUS_SPACE_PIO
} bus_space_tag_t;

typedef union {
	u_long		  ioport;
	volatile uint8_t *maddr;
} bus_space_handle_t;

typedef struct bus_dma_segment
{
	bus_addr_t	ds_addr;
	bus_size_t	ds_len;
} bus_dma_segment_t;

struct aic_linux_dma_tag
{
	bus_size_t	alignment;
	bus_size_t	boundary;
	bus_size_t	maxsize;
};
typedef struct aic_linux_dma_tag* bus_dma_tag_t;

struct aic_linux_dmamap
{
	bus_addr_t	bus_addr;
};
typedef struct aic_linux_dmamap* bus_dmamap_t;

typedef int bus_dma_filter_t(void*, bus_addr_t);
typedef void bus_dmamap_callback_t(void *, bus_dma_segment_t *, int, int);

#define BUS_DMA_WAITOK		0x0
#define BUS_DMA_NOWAIT		0x1
#define BUS_DMA_ALLOCNOW	0x2
#define BUS_DMA_LOAD_SEGS	0x4	/*
					 * Argument is an S/G list not
					 * a single buffer.
					 */

#define BUS_SPACE_MAXADDR	0xFFFFFFFF
#define BUS_SPACE_MAXADDR_32BIT	0xFFFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFF

int	aic_dma_tag_create(struct aic_softc *, bus_dma_tag_t /*parent*/,
			   bus_size_t /*alignment*/, bus_size_t /*boundary*/,
			   bus_addr_t /*lowaddr*/, bus_addr_t /*highaddr*/,
			   bus_dma_filter_t*/*filter*/, void */*filterarg*/,
			   bus_size_t /*maxsize*/, int /*nsegments*/,
			   bus_size_t /*maxsegsz*/, int /*flags*/,
			   bus_dma_tag_t */*dma_tagp*/);

void	aic_dma_tag_destroy(struct aic_softc *, bus_dma_tag_t /*tag*/);

int	aic_dmamem_alloc(struct aic_softc *, bus_dma_tag_t /*dmat*/,
			 void** /*vaddr*/, int /*flags*/,
			 bus_dmamap_t* /*mapp*/);

void	aic_dmamem_free(struct aic_softc *, bus_dma_tag_t /*dmat*/,
			void* /*vaddr*/, bus_dmamap_t /*map*/);

void	aic_dmamap_destroy(struct aic_softc *, bus_dma_tag_t /*tag*/,
			   bus_dmamap_t /*map*/);

int	aic_dmamap_load(struct aic_softc *aic, bus_dma_tag_t /*dmat*/,
			bus_dmamap_t /*map*/, void * /*buf*/,
			bus_size_t /*buflen*/, bus_dmamap_callback_t *,
			void */*callback_arg*/, int /*flags*/);

int	aic_dmamap_unload(struct aic_softc *, bus_dma_tag_t, bus_dmamap_t);

/*
 * Operations performed by aic_dmamap_sync().
 */
#define BUS_DMASYNC_PREREAD	0x01	/* pre-read synchronization */
#define BUS_DMASYNC_POSTREAD	0x02	/* post-read synchronization */
#define BUS_DMASYNC_PREWRITE	0x04	/* pre-write synchronization */
#define BUS_DMASYNC_POSTWRITE	0x08	/* post-write synchronization */

/*
 * XXX
 * aic_dmamap_sync is only used on buffers allocated with
 * the pci_alloc_consistent() API.  Although I'm not sure how
 * this works on architectures with a write buffer, Linux does
 * not have an API to sync "coherent" memory.  Perhaps we need
 * to do an mb()?
 */
#define aic_dmamap_sync(aic, dma_tag, dmamap, offset, len, op)

/*************************** Linux DMA Wrappers *******************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#define	aic_alloc_coherent(aic, size, bus_addr_ptr) \
	dma_alloc_coherent(aic->dev_softc, size, bus_addr_ptr, /*flag*/0)

#define	aic_free_coherent(aic, size, vaddr, bus_addr) \
	dma_free_coherent(aic->dev_softc, size, vaddr, bus_addr)

#define	aic_map_single(aic, buf, size, direction) \
	dma_map_single(aic->dev_softc, buf, size, direction)

#define	aic_unmap_single(aic, busaddr, size, direction) \
	dma_unmap_single(aic->dev_softc, busaddr, size, direction)

#define	aic_map_sg(aic, sg_list, num_sg, direction) \
	dma_map_sg(aic->dev_softc, sg_list, num_sg, direction)

#define	aic_unmap_sg(aic, sg_list, num_sg, direction) \
	dma_unmap_sg(aic->dev_softc, sg_list, num_sg, direction)

#else /* LINUX_VERSION_CODE > KERNEL_VERSION(2,4,0) */

#define	aic_alloc_coherent(aic, size, bus_addr_ptr) \
	pci_alloc_consistent(aic->dev_softc, size, bus_addr_ptr)

#define	aic_free_coherent(aic, size, vaddr, bus_addr) \
	pci_free_consistent(aic->dev_softc, size, vaddr, bus_addr)

#define	aic_map_single(aic, buf, size, direction) \
	pci_map_single(aic->dev_softc, buf, size, direction)

#define	aic_unmap_single(aic, busaddr, size, direction) \
	pci_unmap_single(aic->dev_softc, busaddr, size, direction)

#define	aic_map_sg(aic, sg_list, num_sg, direction) \
	pci_map_sg(aic->dev_softc, sg_list, num_sg, direction)

#define	aic_unmap_sg(aic, sg_list, num_sg, direction) \
	pci_unmap_sg(aic->dev_softc, sg_list, num_sg, direction)
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2,4,0) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)

#define aic_set_dma_mask(aic, mask) dma_set_mask(aic->dev_softc, mask)
#define aic_set_consistent_dma_mask(aic, mask) \
	pci_set_consistent_dma_mask(aic_dev_to_pci_dev(aic->dev_softc), mask)

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,3)

/*
 * Device softc is NULL for EISA devices.
 */
#define aic_set_dma_mask(aic, mask) 			\
	((aic)->dev_softc == NULL ? 0 : pci_set_dma_mask(aic->dev_softc, mask))

/* Always successfull in 2.4.X kernels */
#define aic_set_consistent_dma_mask(aic, mask) (0)

#else
/*
 * Device softc is NULL for EISA devices.
 * Always "return" 0 for success.
 */
#define aic_set_dma_mask(aic, mask)			\
    (((aic)->dev_softc == NULL)				\
     ? 0						\
     : (((aic)->dev_softc->dma_mask = mask) && 0))

/* Always successfull in 2.4.X kernels */
#define aic_set_consistent_dma_mask(aic, mask) (0)

#endif

/************************* Host Template Macros *******************************/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) || defined(SCSI_HAS_HOST_LOCK))
#define AIC_SCSI_HAS_HOST_LOCK 1

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#define aic_assign_host_lock(aic)			\
    scsi_assign_lock((aic)->platform_data->host,	\
		     &(aic)->platform_data->spin_lock)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21) \
   && defined(AIC_RED_HAT_LINUX_KERNEL)
#define aic_assign_host_lock(aic)				\
do {								\
        (aic)->platform_data->host->host_lock =			\
		&(aic)->platform_data->spin_lock;		\
} while (0)
#else
#define aic_assign_host_lock(aic)				\
do {								\
        (aic)->platform_data->host->lock =			\
		&(aic)->platform_data->spin_lock;		\
} while (0)
#endif
#else   /* !AIC_SCSI_HAS_HOST_LOCK */
#define AIC_SCSI_HAS_HOST_LOCK 0
#define aic_assign_host_lock(aic)
#endif  /* !AIC_SCSI_HAS_HOST_LOCK */

#if defined CONFIG_HIGHIO
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
/* Assume RedHat Distribution with its different HIGHIO conventions. */
#define	AIC_TEMPLATE_DMA_SETTINGS()	\
	.can_dma_32		= 1,	\
	.single_sg_okay		= 1,
#else
#define	AIC_TEMPLATE_DMA_SETTINGS()	\
	.highmem_io		= 1,
#endif
#else
#define	AIC_TEMPLATE_DMA_SETTINGS()
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,7)
#define	AIC_TEMPLATE_MAX_SECTORS(sectors) \
	.max_sectors		= (sectors),
#else
#define	AIC_TEMPLATE_MAX_SECTORS(sectors)
#endif

#if defined(__i386__)
#define	AIC_TEMPLATE_BIOSPARAM() \
	.bios_param		= AIC_LIB_ENTRY(_linux_biosparam),
#else
#define	AIC_TEMPLATE_BIOSPARAM()
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#define	AIC_TEMPLATE_VERSIONED_ENTRIES() \
	.slave_alloc		= AIC_LIB_ENTRY(_linux_slave_alloc), \
	.slave_configure	= AIC_LIB_ENTRY(_linux_slave_configure), \
	.slave_destroy		= AIC_LIB_ENTRY(_linux_slave_destroy)
#else
#define	AIC_TEMPLATE_VERSIONED_ENTRIES() \
	.detect			= AIC_LIB_ENTRY(_linux_detect), \
	.release		= AIC_LIB_ENTRY(_linux_release), \
	.select_queue_depths	= AIC_LIB_ENTRY(_linux_select_queue_depth), \
	.use_new_eh_code	= 1
#endif

#define AIC_TEMPLATE_INITIALIZER(NAME, MAX_SECTORS)			\
{									\
	.module			= THIS_MODULE,				\
	.name			= NAME,					\
	.proc_info		= AIC_LIB_ENTRY(_linux_proc_info),	\
	.info			= AIC_LIB_ENTRY(_linux_info),		\
	.queuecommand		= AIC_LIB_ENTRY(_linux_queue),		\
	.eh_abort_handler	= AIC_LIB_ENTRY(_linux_abort),		\
	.eh_device_reset_handler = AIC_LIB_ENTRY(_linux_dev_reset),	\
	.eh_bus_reset_handler	= AIC_LIB_ENTRY(_linux_bus_reset),	\
	.can_queue		= AIC_CONST_ENTRY(_MAX_QUEUE),		\
	.this_id		= -1,					\
	.cmd_per_lun		= 2,					\
	.use_clustering		= ENABLE_CLUSTERING,			\
	AIC_TEMPLATE_MAX_SECTORS(MAX_SECTORS)				\
	AIC_TEMPLATE_DMA_SETTINGS()					\
	AIC_TEMPLATE_BIOSPARAM()					\
	AIC_TEMPLATE_VERSIONED_ENTRIES()				\
}

/************************** OS Utility Wrappers *******************************/
#define printf printk
#define M_NOWAIT GFP_ATOMIC
#define M_WAITOK 0
#define malloc(size, type, flags) kmalloc(size, flags)
#define free(ptr, type) kfree(ptr)

static __inline void aic_delay(long);
static __inline void
aic_delay(long usec)
{
	/*
	 * udelay on Linux can have problems for
	 * multi-millisecond waits.  Wait at most
	 * 1024us per call.
	 */
	while (usec > 0) {
		udelay(usec % 1024);
		usec -= 1024;
	}
}

/********************************** Misc Macros *******************************/
#define	roundup(x, y)   ((((x)+((y)-1))/(y))*(y))
#define	powerof2(x)	((((x)-1)&(x))==0)

/******************************* Byte Order ***********************************/
#define aic_htobe16(x)	cpu_to_be16(x)
#define aic_htobe32(x)	cpu_to_be32(x)
#define aic_htobe64(x)	cpu_to_be64(x)
#define aic_htole16(x)	cpu_to_le16(x)
#define aic_htole32(x)	cpu_to_le32(x)
#define aic_htole64(x)	cpu_to_le64(x)

#define aic_be16toh(x)	be16_to_cpu(x)
#define aic_be32toh(x)	be32_to_cpu(x)
#define aic_be64toh(x)	be64_to_cpu(x)
#define aic_le16toh(x)	le16_to_cpu(x)
#define aic_le32toh(x)	le32_to_cpu(x)
#define aic_le64toh(x)	le64_to_cpu(x)

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif

#ifndef BYTE_ORDER
#if defined(__BIG_ENDIAN)
#define BYTE_ORDER BIG_ENDIAN
#endif
#if defined(__LITTLE_ENDIAN)
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif /* BYTE_ORDER */

/********************************* Core Includes ******************************/
#include AIC_CORE_INCLUDE

/**************************** Front End Queues ********************************/
/*
 * Data structure used to cast the Linux struct scsi_cmnd to something
 * that allows us to use the queue macros.  The linux structure has
 * plenty of space to hold the links fields as required by the queue
 * macros, but the queue macors require them to have the correct type.
 */
struct aic_cmd_internal {
	/* Area owned by the Linux scsi layer. */
	uint8_t	private[offsetof(struct scsi_cmnd, SCp.Status)];
	union {
		STAILQ_ENTRY(aic_cmd)	ste;
		LIST_ENTRY(aic_cmd)	le;
		TAILQ_ENTRY(aic_cmd)	tqe;
	} links;
	uint32_t			end;
};

struct aic_cmd {
	union {
		struct aic_cmd_internal	icmd;
		struct scsi_cmnd	scsi_cmd;
	} un;
};

#define acmd_icmd(cmd) ((cmd)->un.icmd)
#define acmd_scsi_cmd(cmd) ((cmd)->un.scsi_cmd)
#define acmd_links un.icmd.links

/*************************** Device Data Structures ***************************/
/*
 * A per probed device structure used to deal with some error recovery
 * scenarios that the Linux mid-layer code just doesn't know how to
 * handle.  The structure allocated for a device only becomes persistent
 * after a successfully completed inquiry command to the target when
 * that inquiry data indicates a lun is present.
 */
TAILQ_HEAD(aic_busyq, aic_cmd);
typedef enum {
	AIC_DEV_UNCONFIGURED	 = 0x01,
	AIC_DEV_FREEZE_TIL_EMPTY = 0x02, /* Freeze queue until active == 0 */
	AIC_DEV_TIMER_ACTIVE	 = 0x04, /* Our timer is active */
	AIC_DEV_ON_RUN_LIST	 = 0x08, /* Queued to be run later */
	AIC_DEV_Q_BASIC		 = 0x10, /* Allow basic device queuing */
	AIC_DEV_Q_TAGGED	 = 0x20, /* Allow full SCSI2 command queueing */
	AIC_DEV_PERIODIC_OTAG	 = 0x40, /* Send OTAG to prevent starvation */
	AIC_DEV_SLAVE_CONFIGURED = 0x80	 /* slave_configure() has been called */
} aic_linux_dev_flags;

struct aic_linux_target;
struct aic_linux_device {
	TAILQ_ENTRY(aic_linux_device) links;
	struct			aic_busyq busyq;

	/*
	 * The number of transactions currently
	 * queued to the device.
	 */
	int			active;

	/*
	 * The currently allowed number of 
	 * transactions that can be queued to
	 * the device.  Must be signed for
	 * conversion from tagged to untagged
	 * mode where the device may have more
	 * than one outstanding active transaction.
	 */
	int			openings;

	/*
	 * A positive count indicates that this
	 * device's queue is halted.
	 */
	u_int			qfrozen;
	
	/*
	 * Cumulative command counter.
	 */
	u_long			commands_issued;

	/*
	 * The number of tagged transactions when
	 * running at our current opening level
	 * that have been successfully received by
	 * this device since the last QUEUE FULL.
	 */
	u_int			tag_success_count;
#define AIC_TAG_SUCCESS_INTERVAL 50

	aic_linux_dev_flags	flags;

	/*
	 * Per device timer.
	 */
	struct timer_list	timer;

	/*
	 * The high limit for the tags variable.
	 */
	u_int			maxtags;

	/*
	 * The computed number of tags outstanding
	 * at the time of the last QUEUE FULL event.
	 */
	u_int			tags_on_last_queuefull;

	/*
	 * How many times we have seen a queue full
	 * with the same number of tags.  This is used
	 * to stop our adaptive queue depth algorithm
	 * on devices with a fixed number of tags.
	 */
	u_int			last_queuefull_same_count;
#define AIC_LOCK_TAGS_COUNT 50

	/*
	 * How many transactions have been queued
	 * without the device going idle.  We use
	 * this statistic to determine when to issue
	 * an ordered tag to prevent transaction
	 * starvation.  This statistic is only updated
	 * if the AIC_DEV_PERIODIC_OTAG flag is set
	 * on this device.
	 */
	u_int			commands_since_idle_or_otag;
#define AIC_OTAG_THRESH	500

	int			lun;
	Scsi_Device	       *scsi_device;
	struct			aic_linux_target *target;
};

typedef enum {
	AIC_DV_REQUIRED		 = 0x01,
	AIC_INQ_VALID		 = 0x02,
	AIC_BASIC_DV		 = 0x04,
	AIC_ENHANCED_DV		 = 0x08,
	AIC_TARG_TIMER_ACTIVE	 = 0x10
} aic_linux_targ_flags;

/* DV States */
typedef enum {
	AIC_DV_STATE_EXIT = 0,
	AIC_DV_STATE_INQ_SHORT_ASYNC,
	AIC_DV_STATE_INQ_ASYNC,
	AIC_DV_STATE_INQ_ASYNC_VERIFY,
	AIC_DV_STATE_TUR,
	AIC_DV_STATE_REBD,
	AIC_DV_STATE_INQ_VERIFY,
	AIC_DV_STATE_WEB,
	AIC_DV_STATE_REB,
	AIC_DV_STATE_SU,
	AIC_DV_STATE_BUSY
} aic_dv_state;

struct aic_linux_target {
	/*
	 * A positive count indicates that this
	 * target's queue is halted.
	 */
	u_int			  qfrozen;

	struct aic_linux_device	 *devices[AIC_NUM_LUNS];
	int			  channel;
	int			  target;
	int			  refcount;
	struct aic_transinfo	  last_tinfo;
	struct aic_softc	 *softc;
	aic_linux_targ_flags	  flags;
	struct scsi_inquiry_data *inq_data;
	/*
	 * Per target timer.
	 */
	struct timer_list	timer;

	/*
	 * The next "fallback" period to use for narrow/wide transfers.
	 */
	uint8_t			  dv_next_narrow_period;
	uint8_t			  dv_next_wide_period;
	uint8_t			  dv_max_width;
	uint8_t			  dv_max_ppr_options;
	uint8_t			  dv_last_ppr_options;
	u_int			  dv_echo_size;
	aic_dv_state		  dv_state;
	u_int			  dv_state_retry;
	uint8_t			 *dv_buffer;
	uint8_t			 *dv_buffer1;

	/*
	 * Cumulative counter of errors.
	 */
	u_long			errors_detected;
	u_long			cmds_since_error;
};

/*************** OSM Dependent Components of Core Datastructures **************/
/*
 * Per-SCB OSM storage.
 */
typedef enum {
	AIC_SCB_UP_EH_SEM	= 0x1,
	AIC_TIMEOUT_ACTIVE	= 0x2,
	AIC_RELEASE_SIMQ	= 0x4
} aic_linux_scb_flags;

struct scb_platform_data {
	struct aic_linux_device	*dev;
	bus_addr_t		 buf_busaddr;
	uint32_t		 xfer_len;
	uint32_t		 sense_resid;	/* Auto-Sense residual */
	aic_linux_scb_flags	 flags;
};

/*
 * Define a structure used for each host adapter.  All members are
 * aligned on a boundary >= the size of the member to honor the
 * alignment restrictions of the various platforms supported by
 * this driver.
 */
typedef enum {
	AIC_DV_WAIT_SIMQ_EMPTY	 = 0x01,
	AIC_DV_WAIT_SIMQ_RELEASE = 0x02,
	AIC_DV_ACTIVE		 = 0x04,
	AIC_DV_SHUTDOWN		 = 0x08,
	AIC_RUN_CMPLT_Q_TIMER	 = 0x10,
	AIC_BUS_SETTLE_TIMER	 = 0x20
} aic_linux_softc_flags;

TAILQ_HEAD(aic_completeq, aic_cmd);

struct aic_platform_data {
	/*
	 * Fields accessed from interrupt context.
	 */
	struct aic_linux_target *targets[AIC_NUM_TARGETS]; 
	TAILQ_HEAD(, aic_linux_device) device_runq;
	struct aic_completeq	 completeq;

	spinlock_t		 spin_lock;
	struct tasklet_struct	 runq_tasklet;
	struct tasklet_struct	 unblock_tasklet;
	u_int			 qfrozen;
	pid_t			 dv_pid;
	pid_t			 recovery_pid;
	struct timer_list	 completeq_timer;
	struct timer_list	 bus_settle_timer;
	struct timer_list	 stats_timer;
	struct semaphore	 eh_sem;
	struct semaphore	 dv_sem;
	struct semaphore	 dv_cmd_sem;
	struct semaphore	 recovery_sem;
	struct semaphore	 recovery_ending_sem;
	struct scsi_device	*dv_scsi_dev;
	struct Scsi_Host        *host;		/* pointer to scsi host */
#define AIC_LINUX_NOIRQ	((uint32_t)~0)
	uint32_t		 irq;		/* IRQ for this adapter */
	uint32_t		 bios_address;
	uint32_t		 mem_busaddr;	/* Mem Base Addr */
	bus_addr_t		 hw_dma_mask;
	aic_linux_softc_flags	 flags;
};

/******************************** Locking *************************************/
/* Lock protecting internal data structures */
static __inline void aic_lockinit(struct aic_softc *);
static __inline void aic_lock(struct aic_softc *, unsigned long *flags);
static __inline void aic_unlock(struct aic_softc *, unsigned long *flags);

/* Lock acquisition and release of the above lock in midlayer entry points. */
static __inline void aic_entrypoint_lock(struct aic_softc *,
					 unsigned long *flags);
static __inline void aic_entrypoint_unlock(struct aic_softc *,
					   unsigned long *flags);

/* Lock held during aic_list manipulation and aic softc frees */
extern spinlock_t aic_list_spinlock;
static __inline void aic_list_lockinit(void);
static __inline void aic_list_lock(unsigned long *flags);
static __inline void aic_list_unlock(unsigned long *flags);

static __inline void
aic_lockinit(struct aic_softc *aic)
{
	spin_lock_init(&aic->platform_data->spin_lock);
}

static __inline void
aic_lock(struct aic_softc *aic, unsigned long *flags)
{
	spin_lock_irqsave(&aic->platform_data->spin_lock, *flags);
}

static __inline void
aic_unlock(struct aic_softc *aic, unsigned long *flags)
{
	spin_unlock_irqrestore(&aic->platform_data->spin_lock, *flags);
}

static __inline void
aic_entrypoint_lock(struct aic_softc *aic, unsigned long *flags)
{
	/*
	 * In 2.5.X and some 2.4.X versions, the midlayer takes our
	 * lock just before calling us, so we avoid locking again.
	 * For other kernel versions, the io_request_lock is taken
	 * just before our entry point is called.  In this case, we
	 * trade the io_request_lock for our per-softc lock.
	 */
#if AIC_SCSI_HAS_HOST_LOCK == 0
	spin_unlock(&io_request_lock);
	spin_lock(&aic->platform_data->spin_lock);
#endif
}

static __inline void
aic_entrypoint_unlock(struct aic_softc *aic, unsigned long *flags)
{
#if AIC_SCSI_HAS_HOST_LOCK == 0
	spin_unlock(&aic->platform_data->spin_lock);
	spin_lock(&io_request_lock);
#endif
}

static __inline void
aic_list_lockinit(void)
{
	spin_lock_init(&aic_list_spinlock);
}

static __inline void
aic_list_lock(unsigned long *flags)
{
	spin_lock_irqsave(&aic_list_spinlock, *flags);
}

static __inline void
aic_list_unlock(unsigned long *flags)
{
	spin_unlock_irqrestore(&aic_list_spinlock, *flags);
}

/***************************** Timer Facilities *******************************/
typedef void aic_linux_callback_t (u_long);  
void aic_platform_timeout(struct scsi_cmnd *);
void aic_linux_midlayer_timeout(struct scsi_cmnd *);

#define aic_timer_init init_timer
#define aic_timer_stop del_timer_sync
static __inline void aic_timer_reset(aic_timer_t *timer, uint32_t usec,
				     aic_callback_t *func, void *arg);
static __inline uint32_t aic_get_timeout(struct scb *);
static __inline void aic_scb_timer_start(struct scb *scb);
static __inline void aic_scb_timer_reset(struct scb *scb, uint32_t usec);

static __inline void
aic_timer_reset(aic_timer_t *timer, uint32_t usec,
		aic_callback_t *func, void *arg)
{
	struct aic_softc *aic;

	aic = (struct aic_softc *)arg;
	del_timer(timer);
	timer->data = (u_long)arg;
	timer->expires = jiffies + (usec / AIC_USECS_PER_JIFFY);
	timer->function = (aic_linux_callback_t*)func;
	add_timer(timer);
}

static __inline uint32_t
aic_get_timeout(struct scb *scb)
{

	/*
	 * Convert from jiffies to usec.
	 */
	return (scb->io_ctx->timeout_per_command * AIC_USECS_PER_JIFFY);
}

static __inline void
aic_scb_timer_start(struct scb *scb)
{
	scb->platform_data->flags |= AIC_TIMEOUT_ACTIVE;
	scsi_add_timer(scb->io_ctx, scb->io_ctx->timeout_per_command,
		       aic_platform_timeout);
}

static __inline void
aic_scb_timer_reset(struct scb *scb, uint32_t usec)
{
	/*
	 * Restore timer data that is clobbered by scsi_delete_timer().
	 */
	scb->io_ctx->eh_timeout.data = (unsigned long)scb->io_ctx;
	scb->io_ctx->eh_timeout.function =
	    (void (*)(unsigned long))aic_platform_timeout;
	scb->platform_data->flags |= AIC_TIMEOUT_ACTIVE;
	mod_timer(&scb->io_ctx->eh_timeout,
		  jiffies + (usec / AIC_USECS_PER_JIFFY));
}

/************************* SCSI command formats *******************************/
/*
 * Define dome bits that are in ALL (or a lot of) scsi commands
 */
#define SCSI_CTL_LINK		0x01
#define SCSI_CTL_FLAG		0x02
#define SCSI_CTL_VENDOR		0xC0
#define	SCSI_CMD_LUN		0xA0	/* these two should not be needed */
#define	SCSI_CMD_LUN_SHIFT	5	/* LUN in the cmd is no longer SCSI */

#define SCSI_MAX_CDBLEN		16	/* 
					 * 16 byte commands are in the 
					 * SCSI-3 spec 
					 */
/* 6byte CDBs special case 0 length to be 256 */
#define SCSI_CDB6_LEN(len)	((len) == 0 ? 256 : len)

/*
 * This type defines actions to be taken when a particular sense code is
 * received.  Right now, these flags are only defined to take up 16 bits,
 * but can be expanded in the future if necessary.
 */
typedef enum {
	SS_NOP		= 0x000000, /* Do nothing */
	SS_RETRY	= 0x010000, /* Retry the command */
	SS_FAIL		= 0x020000, /* Bail out */
	SS_START	= 0x030000, /* Send a Start Unit command to the device,
				     * then retry the original command.
				     */
	SS_TUR		= 0x040000, /* Send a Test Unit Ready command to the
				     * device, then retry the original command.
				     */
	SS_REQSENSE	= 0x050000, /* Send a RequestSense command to the
				     * device, then retry the original command.
				     */
	SS_INQ_REFRESH	= 0x060000,
	SS_MASK		= 0xff0000
} aic_sense_action;

typedef enum {
	SSQ_NONE		= 0x0000,
	SSQ_DECREMENT_COUNT	= 0x0100,  /* Decrement the retry count */
	SSQ_MANY		= 0x0200,  /* send lots of recovery commands */
	SSQ_RANGE		= 0x0400,  /*
					    * This table entry represents the
					    * end of a range of ASCQs that
					    * have identical error actions
					    * and text.
					    */
	SSQ_PRINT_SENSE		= 0x0800,
	SSQ_DELAY		= 0x1000,  /* Delay before retry. */
	SSQ_DELAY_RANDOM	= 0x2000,  /* Randomized delay before retry. */
	SSQ_FALLBACK		= 0x4000,  /* Do a speed fallback to recover */
	SSQ_MASK		= 0xff00
} aic_sense_action_qualifier;

/* Mask for error status values */
#define SS_ERRMASK	0xff

/* The default, retyable, error action */
#define SS_RDEF		SS_RETRY|SSQ_DECREMENT_COUNT|SSQ_PRINT_SENSE|EIO

/* The retyable, error action, with table specified error code */
#define SS_RET		SS_RETRY|SSQ_DECREMENT_COUNT|SSQ_PRINT_SENSE

/* Fatal error action, with table specified error code */
#define SS_FATAL	SS_FAIL|SSQ_PRINT_SENSE

struct scsi_generic
{
	uint8_t opcode;
	uint8_t bytes[11];
};

struct scsi_request_sense
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_test_unit_ready
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[3];
	uint8_t control;
};

struct scsi_send_diag
{
	uint8_t opcode;
	uint8_t byte2;
#define	SSD_UOL		0x01
#define	SSD_DOL		0x02
#define	SSD_SELFTEST	0x04
#define	SSD_PF		0x10
	uint8_t unused[1];
	uint8_t paramlen[2];
	uint8_t control;
};

struct scsi_sense
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_inquiry
{
	uint8_t opcode;
	uint8_t byte2;
#define	SI_EVPD 0x01
	uint8_t page_code;
	uint8_t reserved;
	uint8_t length;
	uint8_t control;
};

struct scsi_mode_sense_6
{
	uint8_t opcode;
	uint8_t byte2;
#define	SMS_DBD				0x08
	uint8_t page;
#define	SMS_PAGE_CODE 			0x3F
#define SMS_VENDOR_SPECIFIC_PAGE	0x00
#define SMS_DISCONNECT_RECONNECT_PAGE	0x02
#define SMS_PERIPHERAL_DEVICE_PAGE	0x09
#define SMS_CONTROL_MODE_PAGE		0x0A
#define SMS_ALL_PAGES_PAGE		0x3F
#define	SMS_PAGE_CTRL_MASK		0xC0
#define	SMS_PAGE_CTRL_CURRENT 		0x00
#define	SMS_PAGE_CTRL_CHANGEABLE 	0x40
#define	SMS_PAGE_CTRL_DEFAULT 		0x80
#define	SMS_PAGE_CTRL_SAVED 		0xC0
	uint8_t unused;
	uint8_t length;
	uint8_t control;
};

struct scsi_mode_sense_10
{
	uint8_t opcode;
	uint8_t byte2;		/* same bits as small version */
	uint8_t page; 		/* same bits as small version */
	uint8_t unused[4];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_mode_select_6
{
	uint8_t opcode;
	uint8_t byte2;
#define	SMS_SP	0x01
#define	SMS_PF	0x10
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_mode_select_10
{
	uint8_t opcode;
	uint8_t byte2;		/* same bits as small version */
	uint8_t unused[5];
	uint8_t length[2];
	uint8_t control;
};

/*
 * When sending a mode select to a tape drive, the medium type must be 0.
 */
struct scsi_mode_hdr_6
{
	uint8_t datalen;
	uint8_t medium_type;
	uint8_t dev_specific;
	uint8_t block_descr_len;
};

struct scsi_mode_hdr_10
{
	uint8_t datalen[2];
	uint8_t medium_type;
	uint8_t dev_specific;
	uint8_t reserved[2];
	uint8_t block_descr_len[2];
};

struct scsi_mode_block_descr
{
	uint8_t density_code;
	uint8_t num_blocks[3];
	uint8_t reserved;
	uint8_t block_len[3];
};

struct scsi_log_sense
{
	uint8_t opcode;
	uint8_t byte2;
#define	SLS_SP				0x01
#define	SLS_PPC				0x02
	uint8_t page;
#define	SLS_PAGE_CODE 			0x3F
#define	SLS_ALL_PAGES_PAGE		0x00
#define	SLS_OVERRUN_PAGE		0x01
#define	SLS_ERROR_WRITE_PAGE		0x02
#define	SLS_ERROR_READ_PAGE		0x03
#define	SLS_ERROR_READREVERSE_PAGE	0x04
#define	SLS_ERROR_VERIFY_PAGE		0x05
#define	SLS_ERROR_NONMEDIUM_PAGE	0x06
#define	SLS_ERROR_LASTN_PAGE		0x07
#define	SLS_PAGE_CTRL_MASK		0xC0
#define	SLS_PAGE_CTRL_THRESHOLD		0x00
#define	SLS_PAGE_CTRL_CUMULATIVE	0x40
#define	SLS_PAGE_CTRL_THRESH_DEFAULT	0x80
#define	SLS_PAGE_CTRL_CUMUL_DEFAULT	0xC0
	uint8_t reserved[2];
	uint8_t paramptr[2];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_log_select
{
	uint8_t opcode;
	uint8_t byte2;
/*	SLS_SP				0x01 */
#define	SLS_PCR				0x02
	uint8_t page;
/*	SLS_PAGE_CTRL_MASK		0xC0 */
/*	SLS_PAGE_CTRL_THRESHOLD		0x00 */
/*	SLS_PAGE_CTRL_CUMULATIVE	0x40 */
/*	SLS_PAGE_CTRL_THRESH_DEFAULT	0x80 */
/*	SLS_PAGE_CTRL_CUMUL_DEFAULT	0xC0 */
	uint8_t reserved[4];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_log_header
{
	uint8_t page;
	uint8_t reserved;
	uint8_t datalen[2];
};

struct scsi_log_param_header {
	uint8_t param_code[2];
	uint8_t param_control;
#define	SLP_LP				0x01
#define	SLP_LBIN			0x02
#define	SLP_TMC_MASK			0x0C
#define	SLP_TMC_ALWAYS			0x00
#define	SLP_TMC_EQUAL			0x04
#define	SLP_TMC_NOTEQUAL		0x08
#define	SLP_TMC_GREATER			0x0C
#define	SLP_ETC				0x10
#define	SLP_TSD				0x20
#define	SLP_DS				0x40
#define	SLP_DU				0x80
	uint8_t param_len;
};

struct scsi_control_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t rlec;
#define SCB_RLEC			0x01	/*Report Log Exception Cond*/
	uint8_t queue_flags;
#define SCP_QUEUE_ALG_MASK		0xF0
#define SCP_QUEUE_ALG_RESTRICTED	0x00
#define SCP_QUEUE_ALG_UNRESTRICTED	0x10
#define SCP_QUEUE_ERR			0x02	/*Queued I/O aborted for CACs*/
#define SCP_QUEUE_DQUE			0x01	/*Queued I/O disabled*/
	uint8_t eca_and_aen;
#define SCP_EECA			0x80	/*Enable Extended CA*/
#define SCP_RAENP			0x04	/*Ready AEN Permission*/
#define SCP_UAAENP			0x02	/*UA AEN Permission*/
#define SCP_EAENP			0x01	/*Error AEN Permission*/
	uint8_t reserved;
	uint8_t aen_holdoff_period[2];
};

struct scsi_reserve
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_release
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_prevent
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t how;
	uint8_t control;
};
#define	PR_PREVENT 0x01
#define PR_ALLOW   0x00

struct scsi_sync_cache
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t begin_lba[4];
	uint8_t reserved;
	uint8_t lb_count[2];
	uint8_t control;	
};


struct scsi_changedef
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused1;
	uint8_t how;
	uint8_t unused[4];
	uint8_t datalen;
	uint8_t control;
};

struct scsi_read_buffer
{
	uint8_t opcode;
	uint8_t byte2;
#define	RWB_MODE		0x07
#define	RWB_MODE_HDR_DATA	0x00
#define	RWB_MODE_DATA		0x02
#define	RWB_MODE_DOWNLOAD	0x04
#define	RWB_MODE_DOWNLOAD_SAVE	0x05
        uint8_t buffer_id;
        uint8_t offset[3];
        uint8_t length[3];
        uint8_t control;
};

struct scsi_write_buffer
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t buffer_id;
	uint8_t offset[3];
	uint8_t length[3];
	uint8_t control;
};

struct scsi_rw_6
{
	uint8_t opcode;
	uint8_t addr[3];
/* only 5 bits are valid in the MSB address byte */
#define	SRW_TOPADDR	0x1F
	uint8_t length;
	uint8_t control;
};

struct scsi_rw_10
{
	uint8_t opcode;
#define	SRW10_RELADDR	0x01
#define SRW10_FUA	0x08
#define	SRW10_DPO	0x10
	uint8_t byte2;
	uint8_t addr[4];
	uint8_t reserved;
	uint8_t length[2];
	uint8_t control;
};

struct scsi_rw_12
{
	uint8_t opcode;
#define	SRW12_RELADDR	0x01
#define SRW12_FUA	0x08
#define	SRW12_DPO	0x10
	uint8_t byte2;
	uint8_t addr[4];
	uint8_t length[4];
	uint8_t reserved;
	uint8_t control;
};

struct scsi_start_stop_unit
{
	uint8_t opcode;
	uint8_t byte2;
#define	SSS_IMMED		0x01
	uint8_t reserved[2];
	uint8_t how;
#define	SSS_START		0x01
#define	SSS_LOEJ		0x02
	uint8_t control;
};

#define SC_SCSI_1 0x01
#define SC_SCSI_2 0x03

/*
 * Opcodes
 */

#define	TEST_UNIT_READY		0x00
#define REQUEST_SENSE		0x03
#define	READ_6			0x08
#define WRITE_6			0x0a
#define INQUIRY			0x12
#define MODE_SELECT_6		0x15
#define MODE_SENSE_6		0x1a
#define START_STOP_UNIT		0x1b
#define START_STOP		0x1b
#define RESERVE      		0x16
#define RELEASE      		0x17
#define	RECEIVE_DIAGNOSTIC	0x1c
#define	SEND_DIAGNOSTIC		0x1d
#define PREVENT_ALLOW		0x1e
#define	READ_CAPACITY		0x25
#define	READ_10			0x28
#define WRITE_10		0x2a
#define POSITION_TO_ELEMENT	0x2b
#define	SYNCHRONIZE_CACHE	0x35
#define	WRITE_BUFFER            0x3b
#define	READ_BUFFER             0x3c
#define	CHANGE_DEFINITION	0x40
#define	LOG_SELECT		0x4c
#define	LOG_SENSE		0x4d
#ifdef XXXCAM
#define	MODE_SENSE_10		0x5A
#endif
#define	MODE_SELECT_10		0x55
#define MOVE_MEDIUM     	0xa5
#define READ_12			0xa8
#define WRITE_12		0xaa
#define READ_ELEMENT_STATUS	0xb8


/*
 * Device Types
 */
#define T_DIRECT	0x00
#define T_SEQUENTIAL	0x01
#define T_PRINTER	0x02
#define T_PROCESSOR	0x03
#define T_WORM		0x04
#define T_CDROM		0x05
#define T_SCANNER 	0x06
#define T_OPTICAL 	0x07
#define T_CHANGER	0x08
#define T_COMM		0x09
#define T_ASC0		0x0a
#define T_ASC1		0x0b
#define	T_STORARRAY	0x0c
#define	T_ENCLOSURE	0x0d
#define	T_RBC		0x0e
#define	T_OCRW		0x0f
#define T_NODEVICE	0x1F
#define	T_ANY		0xFF	/* Used in Quirk table matches */

#define T_REMOV		1
#define	T_FIXED		0

/*
 * This length is the initial inquiry length used by the probe code, as    
 * well as the legnth necessary for aic_print_inquiry() to function 
 * correctly.  If either use requires a different length in the future, 
 * the two values should be de-coupled.
 */
#define	SHORT_INQUIRY_LENGTH	36

struct scsi_inquiry_data
{
	uint8_t device;
#define	SID_TYPE(inq_data) ((inq_data)->device & 0x1f)
#define	SID_QUAL(inq_data) (((inq_data)->device & 0xE0) >> 5)
#define	SID_QUAL_LU_CONNECTED	0x00	/*
					 * The specified peripheral device
					 * type is currently connected to
					 * logical unit.  If the target cannot
					 * determine whether or not a physical
					 * device is currently connected, it
					 * shall also use this peripheral
					 * qualifier when returning the INQUIRY
					 * data.  This peripheral qualifier
					 * does not mean that the device is
					 * ready for access by the initiator.
					 */
#define	SID_QUAL_LU_OFFLINE	0x01	/*
					 * The target is capable of supporting
					 * the specified peripheral device type
					 * on this logical unit; however, the
					 * physical device is not currently
					 * connected to this logical unit.
					 */
#define SID_QUAL_RSVD		0x02
#define	SID_QUAL_BAD_LU		0x03	/*
					 * The target is not capable of
					 * supporting a physical device on
					 * this logical unit. For this
					 * peripheral qualifier the peripheral
					 * device type shall be set to 1Fh to
					 * provide compatibility with previous
					 * versions of SCSI. All other
					 * peripheral device type values are
					 * reserved for this peripheral
					 * qualifier.
					 */
#define	SID_QUAL_IS_VENDOR_UNIQUE(inq_data) ((SID_QUAL(inq_data) & 0x08) != 0)
	uint8_t dev_qual2;
#define	SID_QUAL2	0x7F
#define	SID_IS_REMOVABLE(inq_data) (((inq_data)->dev_qual2 & 0x80) != 0)
	uint8_t version;
#define SID_ANSI_REV(inq_data) ((inq_data)->version & 0x07)
#define		SCSI_REV_0		0
#define		SCSI_REV_CCS		1
#define		SCSI_REV_2		2
#define		SCSI_REV_SPC		3
#define		SCSI_REV_SPC2		4

#define SID_ECMA	0x38
#define SID_ISO		0xC0
	uint8_t response_format;
#define SID_AENC	0x80
#define SID_TrmIOP	0x40
	uint8_t additional_length;
	uint8_t reserved[2];
	uint8_t flags;
#define	SID_SftRe	0x01
#define	SID_CmdQue	0x02
#define	SID_Linked	0x08
#define	SID_Sync	0x10
#define	SID_WBus16	0x20
#define	SID_WBus32	0x40
#define	SID_RelAdr	0x80
#define SID_VENDOR_SIZE   8
	char	 vendor[SID_VENDOR_SIZE];
#define SID_PRODUCT_SIZE  16
	char	 product[SID_PRODUCT_SIZE];
#define SID_REVISION_SIZE 4
	char	 revision[SID_REVISION_SIZE];
	/*
	 * The following fields were taken from SCSI Primary Commands - 2
	 * (SPC-2) Revision 14, Dated 11 November 1999
	 */
#define	SID_VENDOR_SPECIFIC_0_SIZE	20
	uint8_t vendor_specific0[SID_VENDOR_SPECIFIC_0_SIZE];
	/*
	 * An extension of SCSI Parallel Specific Values
	 */
#define	SID_SPI_IUS		0x01
#define	SID_SPI_QAS		0x02
#define	SID_SPI_CLOCK_ST	0x00
#define	SID_SPI_CLOCK_DT	0x04
#define	SID_SPI_CLOCK_DT_ST	0x0C
#define	SID_SPI_MASK		0x0F
	uint8_t spi3data;
	uint8_t reserved2;
	/*
	 * Version Descriptors, stored 2 byte values.
	 */
	uint8_t version1[2];
	uint8_t version2[2];
	uint8_t version3[2];
	uint8_t version4[2];
	uint8_t version5[2];
	uint8_t version6[2];
	uint8_t version7[2];
	uint8_t version8[2];

	uint8_t reserved3[22];

#define	SID_VENDOR_SPECIFIC_1_SIZE	160
	uint8_t vendor_specific1[SID_VENDOR_SPECIFIC_1_SIZE];
};

struct scsi_vpd_unit_serial_number
{
	uint8_t device;
	uint8_t page_code;
#define SVPD_UNIT_SERIAL_NUMBER	0x80
	uint8_t reserved;
	uint8_t length; /* serial number length */
#define SVPD_SERIAL_NUM_SIZE 251
	uint8_t serial_num[SVPD_SERIAL_NUM_SIZE];
};

struct scsi_read_capacity
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t addr[4];
	uint8_t unused[3];
	uint8_t control;
};

struct scsi_read_capacity_data
{
	uint8_t addr[4];
	uint8_t length[4];
};

struct scsi_report_luns
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[3];
	uint8_t addr[4];
	uint8_t control;
};

struct scsi_report_luns_data {
	uint8_t length[4];	/* length of LUN inventory, in bytes */
	uint8_t reserved[4];	/* unused */
	/*
	 * LUN inventory- we only support the type zero form for now.
	 */
	struct {
		uint8_t lundata[8];
	} luns[1];
};
#define	RPL_LUNDATA_ATYP_MASK	0xc0	/* MBZ for type 0 lun */
#define	RPL_LUNDATA_T0LUN	1	/* @ lundata[1] */


struct scsi_sense_data
{
	uint8_t error_code;
#define	SSD_ERRCODE			0x7F
#define		SSD_CURRENT_ERROR	0x70
#define		SSD_DEFERRED_ERROR	0x71
#define	SSD_ERRCODE_VALID	0x80	
	uint8_t segment;
	uint8_t flags;
#define	SSD_KEY				0x0F
#define		SSD_KEY_NO_SENSE	0x00
#define		SSD_KEY_RECOVERED_ERROR	0x01
#define		SSD_KEY_NOT_READY	0x02
#define		SSD_KEY_MEDIUM_ERROR	0x03
#define		SSD_KEY_HARDWARE_ERROR	0x04
#define		SSD_KEY_ILLEGAL_REQUEST	0x05
#define		SSD_KEY_UNIT_ATTENTION	0x06
#define		SSD_KEY_DATA_PROTECT	0x07
#define		SSD_KEY_BLANK_CHECK	0x08
#define		SSD_KEY_Vendor_Specific	0x09
#define		SSD_KEY_COPY_ABORTED	0x0a
#define		SSD_KEY_ABORTED_COMMAND	0x0b		
#define		SSD_KEY_EQUAL		0x0c
#define		SSD_KEY_VOLUME_OVERFLOW	0x0d
#define		SSD_KEY_MISCOMPARE	0x0e
#define		SSD_KEY_RESERVED	0x0f			
#define	SSD_ILI		0x20
#define	SSD_EOM		0x40
#define	SSD_FILEMARK	0x80
	uint8_t info[4];
	uint8_t extra_len;
	uint8_t cmd_spec_info[4];
	uint8_t add_sense_code;
	uint8_t add_sense_code_qual;
	uint8_t fru;
	uint8_t sense_key_spec[3];
#define	SSD_SCS_VALID		0x80
#define SSD_FIELDPTR_CMD	0x40
#define SSD_BITPTR_VALID	0x08
#define SSD_BITPTR_VALUE	0x07
#define SSD_MIN_SIZE 18
	uint8_t extra_bytes[14];
#define SSD_FULL_SIZE sizeof(struct scsi_sense_data)
};

struct scsi_mode_header_6
{
	uint8_t data_length;	/* Sense data length */
	uint8_t medium_type;
	uint8_t dev_spec;
	uint8_t blk_desc_len;
};

struct scsi_mode_header_10
{
	uint8_t data_length[2];/* Sense data length */
	uint8_t medium_type;
	uint8_t dev_spec;
	uint8_t unused[2];
	uint8_t blk_desc_len[2];
};

struct scsi_mode_page_header
{
	uint8_t page_code;
	uint8_t page_length;
};

struct scsi_mode_blk_desc
{
	uint8_t density;
	uint8_t nblocks[3];
	uint8_t reserved;
	uint8_t blklen[3];
};

#define	SCSI_DEFAULT_DENSITY	0x00	/* use 'default' density */
#define	SCSI_SAME_DENSITY	0x7f	/* use 'same' density- >= SCSI-2 only */


/*
 * Status Byte
 */
#define	SCSI_STATUS_OK			0x00
#define	SCSI_STATUS_CHECK_COND		0x02
#define	SCSI_STATUS_COND_MET		0x04
#define	SCSI_STATUS_BUSY		0x08
#define SCSI_STATUS_INTERMED		0x10
#define SCSI_STATUS_INTERMED_COND_MET	0x14
#define SCSI_STATUS_RESERV_CONFLICT	0x18
#define SCSI_STATUS_CMD_TERMINATED	0x22	/* Obsolete in SAM-2 */
#define SCSI_STATUS_QUEUE_FULL		0x28
#define SCSI_STATUS_ACA_ACTIVE		0x30
#define SCSI_STATUS_TASK_ABORTED	0x40

struct scsi_inquiry_pattern {
	uint8_t   type;
	uint8_t   media_type;
#define	SIP_MEDIA_REMOVABLE	0x01
#define	SIP_MEDIA_FIXED		0x02
	const char *vendor;
	const char *product;
	const char *revision;
}; 

struct scsi_static_inquiry_pattern {
	uint8_t   type;
	uint8_t   media_type;
	char       vendor[SID_VENDOR_SIZE+1];
	char       product[SID_PRODUCT_SIZE+1];
	char       revision[SID_REVISION_SIZE+1];
};

struct scsi_sense_quirk_entry {
	struct scsi_inquiry_pattern	inq_pat;
	int				num_sense_keys;
	int				num_ascs;
	struct sense_key_table_entry	*sense_key_info;
	struct asc_table_entry		*asc_info;
};

struct sense_key_table_entry {
	uint8_t    sense_key;
	uint32_t   action;
	const char *desc;
};

struct asc_table_entry {
	uint8_t    asc;
	uint8_t    ascq;
	uint32_t   action;
	const char *desc;
};

struct op_table_entry {
	uint8_t    opcode;
	uint16_t   opmask;
	const char  *desc;
};

struct scsi_op_quirk_entry {
	struct scsi_inquiry_pattern	inq_pat;
	int				num_ops;
	struct op_table_entry		*op_table;
};

typedef enum {
	SSS_FLAG_NONE		= 0x00,
	SSS_FLAG_PRINT_COMMAND	= 0x01
} scsi_sense_string_flags;

extern const char *scsi_sense_key_text[];

/*************************** Domain Validation ********************************/
#define AIC_DV_CMD(cmd) ((cmd)->scsi_done == aic_linux_dv_complete)
#define AIC_DV_SIMQ_FROZEN(aic)					\
	((((aic)->platform_data->flags & AIC_DV_ACTIVE) != 0)	\
	 && (aic)->platform_data->qfrozen == 1)

/******************************* PCI Definitions ******************************/
/*
 * PCIM_xxx: mask to locate subfield in register
 * PCIR_xxx: config register offset
 * PCIC_xxx: device class
 * PCIS_xxx: device subclass
 * PCIP_xxx: device programming interface
 * PCIV_xxx: PCI vendor ID (only required to fixup ancient devices)
 * PCID_xxx: device ID
 */
#define PCIR_DEVVENDOR		0x00
#define PCIR_VENDOR		0x00
#define PCIR_DEVICE		0x02
#define PCIR_COMMAND		0x04
#define PCIM_CMD_PORTEN		0x0001
#define PCIM_CMD_MEMEN		0x0002
#define PCIM_CMD_BUSMASTEREN	0x0004
#define PCIM_CMD_MWRICEN	0x0010
#define PCIM_CMD_PERRESPEN	0x0040
#define	PCIM_CMD_SERRESPEN	0x0100
#define PCIR_STATUS		0x06
#define PCIR_REVID		0x08
#define PCIR_PROGIF		0x09
#define PCIR_SUBCLASS		0x0a
#define PCIR_CLASS		0x0b
#define PCIR_CACHELNSZ		0x0c
#define PCIR_LATTIMER		0x0d
#define PCIR_HEADERTYPE		0x0e
#define PCIM_MFDEV		0x80
#define PCIR_BIST		0x0f
#define PCIR_CAP_PTR		0x34

/* config registers for header type 0 devices */
#define PCIR_MAPS	0x10
#define PCIR_BARS	PCIR_MAPS
#define PCIR_BAR(x)	(PCIR_BARS + (x) * 4)
#define PCIR_SUBVEND_0	0x2c
#define PCIR_SUBDEV_0	0x2e

typedef enum
{
	AIC_POWER_STATE_D0,
	AIC_POWER_STATE_D1,
	AIC_POWER_STATE_D2,
	AIC_POWER_STATE_D3
} aic_power_state;

/****************************** PCI-X definitions *****************************/
#define PCIXR_COMMAND	0x96
#define PCIXR_DEVADDR	0x98
#define PCIXM_DEVADDR_FNUM	0x0003	/* Function Number */
#define PCIXM_DEVADDR_DNUM	0x00F8	/* Device Number */
#define PCIXM_DEVADDR_BNUM	0xFF00	/* Bus Number */
#define PCIXR_STATUS	0x9A
#define PCIXM_STATUS_64BIT	0x0001	/* Active 64bit connection to device. */
#define PCIXM_STATUS_133CAP	0x0002	/* Device is 133MHz capable */
#define PCIXM_STATUS_SCDISC	0x0004	/* Split Completion Discarded */
#define PCIXM_STATUS_UNEXPSC	0x0008	/* Unexpected Split Completion */
#define PCIXM_STATUS_CMPLEXDEV	0x0010	/* Device Complexity (set == bridge) */
#define PCIXM_STATUS_MAXMRDBC	0x0060	/* Maximum Burst Read Count */
#define PCIXM_STATUS_MAXSPLITS	0x0380	/* Maximum Split Transactions */
#define PCIXM_STATUS_MAXCRDS	0x1C00	/* Maximum Cumulative Read Size */
#define PCIXM_STATUS_RCVDSCEM	0x2000	/* Received a Split Comp w/Error msg */

/**************************** KObject Wrappers ********************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#define	aic_dev_to_pci_dev(dev)		to_pci_dev(dev)
#define	aic_dev_to_eisa_dev(dev)	to_eisa_dev(dev)
#define	aic_pci_dev_to_dev(pci)		(&pci->dev)
#define	aic_eisa_dev_to_dev(eisa)	(&eisa->dev)
#else
#define	aic_dev_to_pci_dev(dev)		(dev)
#define	aic_dev_to_eisa_dev(dev)	(NULL)
#define	aic_pci_dev_to_dev(pci)		(pci)
#define	aic_eisa_dev_to_dev(eisa)	(NULL)
#endif

#define	aic_pci_dev(aic)		aic_dev_to_pci_dev((aic)->dev_softc)
#define	aic_eisa_dev(aic)		aic_dev_to_eisa_dev((aic)->dev_softc)
/***************************** PCI Routines ***********************************/
static __inline uint32_t aic_pci_read_config(aic_dev_softc_t dev,
					     int reg, int width);
static __inline void aic_pci_write_config(aic_dev_softc_t dev,
					  int reg, uint32_t value,
					  int width);
static __inline int aic_get_pci_function(aic_dev_softc_t);
static __inline int aic_get_pci_slot(aic_dev_softc_t);
static __inline int aic_get_pci_bus(aic_dev_softc_t);

static __inline uint32_t
aic_pci_read_config(aic_dev_softc_t dev, int reg, int width)
{
	struct pci_dev *pci;

	pci = aic_dev_to_pci_dev(dev);
	switch (width) {
	case 1:
	{
		uint8_t retval;

		pci_read_config_byte(pci, reg, &retval);
		return (retval);
	}
	case 2:
	{
		uint16_t retval;
		pci_read_config_word(pci, reg, &retval);
		return (retval);
	}
	case 4:
	{
		uint32_t retval;
		pci_read_config_dword(pci, reg, &retval);
		return (retval);
	}
	default:
		panic("aic_pci_read_config: Read size too big");
		/* NOTREACHED */
		return (0);
	}
}

static __inline void
aic_pci_write_config(aic_dev_softc_t dev, int reg, uint32_t value, int width)
{
	struct pci_dev *pci;

	pci = aic_dev_to_pci_dev(dev);
	switch (width) {
	case 1:
		pci_write_config_byte(pci, reg, value);
		break;
	case 2:
		pci_write_config_word(pci, reg, value);
		break;
	case 4:
		pci_write_config_dword(pci, reg, value);
		break;
	default:
		panic("aic_pci_write_config: Write size too big");
		/* NOTREACHED */
	}
}

static __inline int
aic_get_pci_function(aic_dev_softc_t dev)
{
	struct pci_dev *pci;

	pci = aic_dev_to_pci_dev(dev);
	return (PCI_FUNC(pci->devfn));
}

static __inline int
aic_get_pci_slot(aic_dev_softc_t dev)
{
	struct pci_dev *pci;

	pci = aic_dev_to_pci_dev(dev);
	return (PCI_SLOT(pci->devfn));
}

static __inline int
aic_get_pci_bus(aic_dev_softc_t dev)
{
	struct pci_dev *pci;

	pci = aic_dev_to_pci_dev(dev);
	return (pci->bus->number);
}

/************************* Large Disk Handling ********************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static __inline int aic_sector_div(u_long capacity, int heads, int sectors);

static __inline int
aic_sector_div(u_long capacity, int heads, int sectors)
{
	return (capacity / (heads * sectors));
}
#else
static __inline int aic_sector_div(sector_t capacity, int heads, int sectors);

static __inline int
aic_sector_div(sector_t capacity, int heads, int sectors)
{
	/* ugly, ugly sector_div calling convention.. */
	sector_div(capacity, (heads * sectors));
	return (int)capacity;
}
#endif

/************************* Magic SysReq Support *******************************/
#include <linux/sysrq.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
typedef void aic_sysrq_handler_t (int, struct pt_regs *, struct kbd_struct *,
				  struct tty_struct *);
#else
typedef void aic_sysrq_handler_t (int, struct pt_regs *, struct tty_struct *);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
#define	aic_sysrq_key_op sysrq_key_op
#else
struct aic_sysrq_key_op {
	aic_sysrq_handler_t *handler;
	char *help_msg;
	char *action_msg;
};
#endif

aic_sysrq_handler_t	aic_sysrq_handler;
int			aic_install_sysrq(struct aic_sysrq_key_op *);
void			aic_remove_sysrq(int key,
					 struct aic_sysrq_key_op *key_op);
/************************ SCSI Library Functions *****************************/
void			aic_sense_desc(int /*sense_key*/, int /*asc*/,
				       int /*ascq*/, struct scsi_inquiry_data*,
				       const char** /*sense_key_desc*/,
				       const char** /*asc_desc*/);
aic_sense_action	aic_sense_error_action(struct scsi_sense_data*,
					       struct scsi_inquiry_data*,
					       uint32_t /*sense_flags*/);
uint32_t		aic_error_action(struct scsi_cmnd *,
					 struct scsi_inquiry_data *,
					 cam_status, u_int);

#define	SF_RETRY_UA	0x01
#define SF_NO_PRINT	0x02
#define SF_QUIET_IR	0x04	/* Be quiet about Illegal Request reponses */
#define SF_PRINT_ALWAYS	0x08


const char *	aic_op_desc(uint16_t /*opcode*/, struct scsi_inquiry_data*);
char *		aic_cdb_string(uint8_t* /*cdb_ptr*/, char* /*cdb_string*/,
			       size_t /*len*/);
void		aic_print_inquiry(struct scsi_inquiry_data*);

u_int		aic_calc_syncsrate(u_int /*period_factor*/);
u_int		aic_calc_syncparam(u_int /*period*/);
u_int		aic_calc_speed(u_int width, u_int period, u_int offset,
			       u_int min_rate);
	
int		aic_inquiry_match(caddr_t /*inqbuffer*/,
				  caddr_t /*table_entry*/);
int		aic_static_inquiry_match(caddr_t /*inqbuffer*/,
					 caddr_t /*table_entry*/);

typedef void aic_option_callback_t(u_long, int, int, int32_t);
char *		aic_parse_brace_option(char *opt_name, char *opt_arg,
				       char *end, int depth,
				       aic_option_callback_t *, u_long);

static __inline void	 scsi_extract_sense(struct scsi_sense_data *sense,
					    int *error_code, int *sense_key,
					    int *asc, int *ascq);
static __inline void	 scsi_ulto2b(uint32_t val, uint8_t *bytes);
static __inline void	 scsi_ulto3b(uint32_t val, uint8_t *bytes);
static __inline void	 scsi_ulto4b(uint32_t val, uint8_t *bytes);
static __inline uint32_t scsi_2btoul(uint8_t *bytes);
static __inline uint32_t scsi_3btoul(uint8_t *bytes);
static __inline int32_t	 scsi_3btol(uint8_t *bytes);
static __inline uint32_t scsi_4btoul(uint8_t *bytes);

static __inline void scsi_extract_sense(struct scsi_sense_data *sense,
				       int *error_code, int *sense_key,
				       int *asc, int *ascq)
{
	*error_code = sense->error_code & SSD_ERRCODE;
	*sense_key = sense->flags & SSD_KEY;
	*asc = (sense->extra_len >= 5) ? sense->add_sense_code : 0;
	*ascq = (sense->extra_len >= 6) ? sense->add_sense_code_qual : 0;
}

static __inline void
scsi_ulto2b(uint32_t val, uint8_t *bytes)
{

	bytes[0] = (val >> 8) & 0xff;
	bytes[1] = val & 0xff;
}

static __inline void
scsi_ulto3b(uint32_t val, uint8_t *bytes)
{

	bytes[0] = (val >> 16) & 0xff;
	bytes[1] = (val >> 8) & 0xff;
	bytes[2] = val & 0xff;
}

static __inline void
scsi_ulto4b(uint32_t val, uint8_t *bytes)
{

	bytes[0] = (val >> 24) & 0xff;
	bytes[1] = (val >> 16) & 0xff;
	bytes[2] = (val >> 8) & 0xff;
	bytes[3] = val & 0xff;
}

static __inline uint32_t
scsi_2btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 8) |
	     bytes[1];
	return (rv);
}

static __inline uint32_t
scsi_3btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 16) |
	     (bytes[1] << 8) |
	     bytes[2];
	return (rv);
}

static __inline int32_t 
scsi_3btol(uint8_t *bytes)
{
	uint32_t rc = scsi_3btoul(bytes);
 
	if (rc & 0x00800000)
		rc |= 0xff000000;

	return (int32_t) rc;
}

static __inline uint32_t
scsi_4btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 24) |
	     (bytes[1] << 16) |
	     (bytes[2] << 8) |
	     bytes[3];
	return (rv);
}

/******************************* PCI Funcitons ********************************/
void aic_power_state_change(struct aic_softc *aic, aic_power_state new_state);

/******************************* Queue Handling *******************************/
void		     aic_runq_tasklet(unsigned long data);
void		     aic_unblock_tasklet(unsigned long data);
void		     aic_linux_run_device_queue(struct aic_softc*,
						struct aic_linux_device*);
void		     aic_bus_settle_complete(u_long data);
void		     aic_freeze_simq(struct aic_softc *aic);
void		     aic_release_simq(struct aic_softc *aic);
void		     aic_release_simq_locked(struct aic_softc *aic);
static __inline void aic_schedule_runq(struct aic_softc *aic);
static __inline void aic_schedule_unblock(struct aic_softc *aic);
static __inline struct aic_linux_device *
		     aic_linux_next_device_to_run(struct aic_softc *aic);
static __inline void aic_linux_check_device_queue(struct aic_softc *aic,
						  struct aic_linux_device *dev);
static __inline void aic_linux_run_device_queues(struct aic_softc *aic);

/*
 * Must be called with our lock held.
 */
static __inline void
aic_schedule_runq(struct aic_softc *aic)
{
	tasklet_schedule(&aic->platform_data->runq_tasklet);
}

static __inline void
aic_schedule_unblock(struct aic_softc *aic)
{
	tasklet_schedule(&aic->platform_data->unblock_tasklet);
}

static __inline struct aic_linux_device *
aic_linux_next_device_to_run(struct aic_softc *aic)
{
	
	if (aic->platform_data->qfrozen != 0
	 && AIC_DV_SIMQ_FROZEN(aic) == 0)
		return (NULL);
	return (TAILQ_FIRST(&aic->platform_data->device_runq));
}

static __inline void
aic_linux_check_device_queue(struct aic_softc *aic,
			     struct aic_linux_device *dev)
{
	if ((dev->flags & AIC_DEV_FREEZE_TIL_EMPTY) != 0
	 && dev->active == 0) {
		dev->flags &= ~AIC_DEV_FREEZE_TIL_EMPTY;
		dev->qfrozen--;
	}

	if (TAILQ_FIRST(&dev->busyq) == NULL
	 || dev->openings == 0 || dev->qfrozen != 0
	 || dev->target->qfrozen != 0)
		return;

	aic_linux_run_device_queue(aic, dev);
}

static __inline void
aic_linux_run_device_queues(struct aic_softc *aic)
{
	struct aic_linux_device *dev;

	while ((dev = aic_linux_next_device_to_run(aic)) != NULL) {
		TAILQ_REMOVE(&aic->platform_data->device_runq, dev, links);
		dev->flags &= ~AIC_DEV_ON_RUN_LIST;
		aic_linux_check_device_queue(aic, dev);
	}
}

/****************************** Tasklet Support *******************************/
static __inline void	aic_setup_tasklets(struct aic_softc *aic);
static __inline void	aic_teardown_tasklets(struct aic_softc *aic);

static __inline void
aic_setup_tasklets(struct aic_softc *aic)
{
	tasklet_init(&aic->platform_data->runq_tasklet, aic_runq_tasklet,
		     (unsigned long)aic);
	tasklet_init(&aic->platform_data->unblock_tasklet, aic_unblock_tasklet,
		     (unsigned long)aic);
}

static __inline void
aic_teardown_tasklets(struct aic_softc *aic)
{
	tasklet_kill(&aic->platform_data->runq_tasklet);
	tasklet_kill(&aic->platform_data->unblock_tasklet);
}

/*********************** Transaction Access Wrappers **************************/
static __inline void aic_cmd_set_transaction_status(Scsi_Cmnd *, uint32_t);
static __inline void aic_set_transaction_status(struct scb *, uint32_t);
static __inline void aic_cmd_set_scsi_status(Scsi_Cmnd *, uint32_t);
static __inline void aic_set_scsi_status(struct scb *, uint32_t);
static __inline uint32_t aic_cmd_get_transaction_status(Scsi_Cmnd *cmd);
static __inline uint32_t aic_get_transaction_status(struct scb *);
static __inline uint32_t aic_cmd_get_scsi_status(Scsi_Cmnd *cmd);
static __inline uint32_t aic_get_scsi_status(struct scb *);
static __inline void aic_set_transaction_tag(struct scb *, int, u_int);
static __inline u_long aic_get_transfer_length(struct scb *);
static __inline int aic_get_transfer_dir(struct scb *);
static __inline void aic_set_residual(struct scb *, u_long);
static __inline void aic_set_sense_residual(struct scb *scb, u_long resid);
static __inline u_long aic_get_residual(struct scb *);
static __inline u_long aic_get_sense_residual(struct scb *);
static __inline int aic_perform_autosense(struct scb *);
static __inline uint32_t aic_get_sense_bufsize(struct aic_softc *,
					       struct scb *);
static __inline void aic_notify_xfer_settings_change(struct aic_softc *,
						     struct aic_devinfo *);
static __inline void aic_platform_scb_free(struct aic_softc *aic,
					   struct scb *scb);
static __inline void aic_freeze_scb(struct scb *scb);

static __inline
void aic_cmd_set_transaction_status(Scsi_Cmnd *cmd, uint32_t status)
{
	cmd->result &= ~(CAM_STATUS_MASK << 16);
	cmd->result |= status << 16;
}

static __inline
void aic_set_transaction_status(struct scb *scb, uint32_t status)
{
	aic_cmd_set_transaction_status(scb->io_ctx,status);
}

static __inline
void aic_cmd_set_scsi_status(Scsi_Cmnd *cmd, uint32_t status)
{
	cmd->result &= ~0xFFFF;
	cmd->result |= status;
}

static __inline
void aic_set_scsi_status(struct scb *scb, uint32_t status)
{
	aic_cmd_set_scsi_status(scb->io_ctx, status);
}

static __inline
uint32_t aic_cmd_get_transaction_status(Scsi_Cmnd *cmd)
{
	return ((cmd->result >> 16) & CAM_STATUS_MASK);
}

static __inline
uint32_t aic_get_transaction_status(struct scb *scb)
{
	return (aic_cmd_get_transaction_status(scb->io_ctx));
}

static __inline
uint32_t aic_cmd_get_scsi_status(Scsi_Cmnd *cmd)
{
	return (cmd->result & 0xFFFF);
}

static __inline
uint32_t aic_get_scsi_status(struct scb *scb)
{
	return (aic_cmd_get_scsi_status(scb->io_ctx));
}

static __inline
void aic_set_transaction_tag(struct scb *scb, int enabled, u_int type)
{
	/*
	 * Nothing to do for linux as the incoming transaction
	 * has no concept of tag/non tagged, etc.
	 */
}

static __inline
u_long aic_get_transfer_length(struct scb *scb)
{
	return (scb->platform_data->xfer_len);
}

static __inline
int aic_get_transfer_dir(struct scb *scb)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,40)
	return (scb->io_ctx->sc_data_direction);
#else
	if (scb->io_ctx->bufflen == 0)
		return (CAM_DIR_NONE);

	switch(scb->io_ctx->cmnd[0]) {
	case 0x08:  /* READ(6)  */
	case 0x28:  /* READ(10) */
	case 0xA8:  /* READ(12) */
		return (CAM_DIR_IN);
        case 0x0A:  /* WRITE(6)  */
        case 0x2A:  /* WRITE(10) */
        case 0xAA:  /* WRITE(12) */
		return (CAM_DIR_OUT);
        default:
		return (CAM_DIR_NONE);
        }
#endif
}

static __inline
void aic_set_residual(struct scb *scb, u_long resid)
{
	scb->io_ctx->resid = resid;
}

static __inline
void aic_set_sense_residual(struct scb *scb, u_long resid)
{
	scb->platform_data->sense_resid = resid;
}

static __inline
u_long aic_get_residual(struct scb *scb)
{
	return (scb->io_ctx->resid);
}

static __inline
u_long aic_get_sense_residual(struct scb *scb)
{
	return (scb->platform_data->sense_resid);
}

static __inline
int aic_perform_autosense(struct scb *scb)
{
	/*
	 * We always perform autosense in Linux.
	 * On other platforms this is set on a
	 * per-transaction basis.
	 */
	return (1);
}

static __inline uint32_t
aic_get_sense_bufsize(struct aic_softc *aic, struct scb *scb)
{
	return (sizeof(struct scsi_sense_data));
}

static __inline void
aic_notify_xfer_settings_change(struct aic_softc *aic,
				struct aic_devinfo *devinfo)
{
	/* Nothing to do here for linux */
}

static __inline void
aic_platform_scb_free(struct aic_softc *aic, struct scb *scb)
{
	if ((aic->flags & AIC_RESOURCE_SHORTAGE) != 0) {
		aic->flags &= ~AIC_RESOURCE_SHORTAGE;
		aic_release_simq_locked(aic);
	}
}

static __inline void
aic_freeze_scb(struct scb *scb)
{
	if ((scb->io_ctx->result & (CAM_DEV_QFRZN << 16)) == 0) {
                scb->io_ctx->result |= CAM_DEV_QFRZN << 16;
                scb->platform_data->dev->qfrozen++;
        }
}

#endif /*_AICLIB_H */
