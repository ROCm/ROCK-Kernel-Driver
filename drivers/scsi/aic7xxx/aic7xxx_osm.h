/*
 * Adaptec AIC7xxx device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * Copyright (c) 2000-2003 Adaptec Inc.
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
 *
 */
#ifndef _AIC7XXX_LINUX_H_
#define _AIC7XXX_LINUX_H_

#include <linux/version.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#include <linux/config.h>
#endif

/*********************************** Debugging ********************************/
#ifdef CONFIG_AIC7XXX_DEBUG_ENABLE
#ifdef CONFIG_AIC7XXX_DEBUG_MASK
#define AHC_DEBUG 1
#define AHC_DEBUG_OPTS CONFIG_AIC7XXX_DEBUG_MASK
#else
/*
 * Compile in debugging code, but do not enable any printfs.
 */
#define AHC_DEBUG 1
#endif
/* No debugging code. */
#endif

/********************************** Includes **********************************/
/* Core SCSI definitions */
#define AIC_LIB_PREFIX ahc
#define AIC_CONST_PREFIX AHC

#ifdef CONFIG_AIC7XXX_REG_PRETTY_PRINT
#define AIC_DEBUG_REGISTERS 1
#else
#define AIC_DEBUG_REGISTERS 0
#endif
#define	AIC_CORE_INCLUDE "aic7xxx.h"
#include "aiclib.h"

/************************* Configuration Data *********************************/
extern u_int aic7xxx_no_probe;
extern u_int aic7xxx_allow_memio;
extern int aic7xxx_detect_complete;
extern Scsi_Host_Template aic7xxx_driver_template;

/***************************** Domain Validation ******************************/
void ahc_linux_dv_complete(Scsi_Cmnd *cmd);
void ahc_linux_dv_timeout(struct scsi_cmnd *cmd);

/***************************** SMP support ************************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,17)
#include <linux/spinlock.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,93)
#include <linux/smp.h>
#endif

#define AIC7XXX_DRIVER_VERSION "6.3.6"

/********************* Definitions Required by the Core ***********************/
/*
 * Number of SG segments we require.  So long as the S/G segments for
 * a particular transaction are allocated in a physically contiguous
 * manner and are allocated below 4GB, the number of S/G segments is
 * unrestricted.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
/*
 * We dynamically adjust the number of segments in pre-2.5 kernels to
 * avoid fragmentation issues in the SCSI mid-layer's private memory
 * allocator.  See aic7xxx_osm.c ahc_linux_size_nseg() for details.
 */
extern u_int ahc_linux_nseg;
#define	AHC_NSEG ahc_linux_nseg
#define	AHC_LINUX_MIN_NSEG 64
#else
#define	AHC_NSEG 128
#endif

/************************** Error Recovery ************************************/
static __inline void	ahc_wakeup_recovery_thread(struct ahc_softc *ahc); 
  
static __inline void
ahc_wakeup_recovery_thread(struct ahc_softc *ahc)
{ 
	up(&ahc->platform_data->recovery_sem);
}
 
int			ahc_spawn_recovery_thread(struct ahc_softc *ahc);
void			ahc_terminate_recovery_thread(struct ahc_softc *ahc);
void			ahc_set_recoveryscb(struct ahc_softc *ahc,
					    struct scb *scb);

/***************************** Low Level I/O **********************************/
static __inline uint8_t ahc_inb(struct ahc_softc * ahc, long port);
static __inline void ahc_outb(struct ahc_softc * ahc, long port, uint8_t val);
static __inline void ahc_outsb(struct ahc_softc * ahc, long port,
			       uint8_t *, int count);
static __inline void ahc_insb(struct ahc_softc * ahc, long port,
			       uint8_t *, int count);
static __inline void ahc_flush_device_writes(struct ahc_softc *);

static __inline uint8_t
ahc_inb(struct ahc_softc * ahc, long port)
{
	uint8_t x;

	if (ahc->tag == BUS_SPACE_MEMIO) {
		x = readb(ahc->bsh.maddr + port);
	} else {
		x = inb(ahc->bsh.ioport + port);
	}
	mb();
	return (x);
}

static __inline void
ahc_outb(struct ahc_softc * ahc, long port, uint8_t val)
{
	if (ahc->tag == BUS_SPACE_MEMIO) {
		writeb(val, ahc->bsh.maddr + port);
	} else {
		outb(val, ahc->bsh.ioport + port);
	}
	mb();
}

static __inline void
ahc_outsb(struct ahc_softc * ahc, long port, uint8_t *array, int count)
{
	int i;

	/*
	 * There is probably a more efficient way to do this on Linux
	 * but we don't use this for anything speed critical and this
	 * should work.
	 */
	for (i = 0; i < count; i++)
		ahc_outb(ahc, port, *array++);
}

static __inline void
ahc_insb(struct ahc_softc * ahc, long port, uint8_t *array, int count)
{
	int i;

	/*
	 * There is probably a more efficient way to do this on Linux
	 * but we don't use this for anything speed critical and this
	 * should work.
	 */
	for (i = 0; i < count; i++)
		*array++ = ahc_inb(ahc, port);
}

static __inline void
ahc_flush_device_writes(struct ahc_softc *ahc)
{
	/* XXX Is this sufficient for all architectures??? */
	ahc_inb(ahc, INTSTAT);
}

/**************************** Initialization **********************************/
extern int	ahc_init_status;
int		ahc_linux_register_host(struct ahc_softc *,
					Scsi_Host_Template *);

uint64_t	ahc_linux_get_memsize(void);

/*************************** Pretty Printing **********************************/
struct info_str {
	char *buffer;
	int length;
	off_t offset;
	int pos;
};

void	ahc_format_transinfo(struct info_str *info,
			     struct ahc_transinfo *tinfo);

/******************************* PCI Definitions ******************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
extern struct pci_driver aic7xxx_pci_driver;
#endif

/**************************** VL/EISA Routines ********************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) \
  && (defined(__i386__) || defined(__alpha__)) \
  && (!defined(CONFIG_EISA)))
#define CONFIG_EISA
#endif

#ifdef CONFIG_EISA
extern uint32_t aic7xxx_probe_eisa_vl;
int			 ahc_linux_eisa_init(void);
void			 ahc_linux_eisa_exit(void);
int			 aic7770_map_registers(struct ahc_softc *ahc,
					       u_int port);
int			 aic7770_map_int(struct ahc_softc *ahc, u_int irq);
#endif

/******************************* PCI Routines *********************************/
#ifdef CONFIG_PCI
int			 ahc_linux_pci_init(void);
void			 ahc_linux_pci_exit(void);
int			 ahc_pci_map_registers(struct ahc_softc *ahc);
int			 ahc_pci_map_int(struct ahc_softc *ahc);
#endif 

/**************************** Proc FS Support *********************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
int	ahc_linux_proc_info(char *, char **, off_t, int, int, int);
#else
int	ahc_linux_proc_info(struct Scsi_Host *, char *, char **,
			    off_t, int, int);
#endif

/*************************** Domain Validation ********************************/
#define AHC_DV_CMD(cmd) ((cmd)->scsi_done == ahc_linux_dv_complete)
#define AHC_DV_SIMQ_FROZEN(ahc)					\
	((((ahc)->platform_data->flags & AHC_DV_ACTIVE) != 0)	\
	 && (ahc)->platform_data->qfrozen == 1)

/*********************** Transaction Access Wrappers *************************/
int	ahc_platform_alloc(struct ahc_softc *ahc, void *platform_arg);
void	ahc_platform_free(struct ahc_softc *ahc);
void	ahc_platform_freeze_devq(struct ahc_softc *ahc, struct scb *scb);
void	ahc_platform_set_tags(struct ahc_softc *ahc,
			      struct ahc_devinfo *devinfo, ahc_queue_alg);
int	ahc_platform_abort_scbs(struct ahc_softc *ahc, int target,
				char channel, int lun, u_int tag,
				role_t role, uint32_t status);
irqreturn_t
	ahc_linux_isr(int irq, void *dev_id, struct pt_regs * regs);
void	ahc_platform_flushwork(struct ahc_softc *ahc);
int	ahc_softc_comp(struct ahc_softc *, struct ahc_softc *);
void	ahc_done(struct ahc_softc*, struct scb*);
void	ahc_send_async(struct ahc_softc *, char channel,
		       u_int target, u_int lun, ac_code, void *);
void	ahc_print_path(struct ahc_softc *, struct scb *);
void	ahc_platform_dump_card_state(struct ahc_softc *ahc);

#ifdef CONFIG_PCI
#define AIC_PCI_CONFIG 1
#else
#define AIC_PCI_CONFIG 0
#endif
#define bootverbose aic7xxx_verbose
extern u_int aic7xxx_verbose;
#endif /* _AIC7XXX_LINUX_H_ */
