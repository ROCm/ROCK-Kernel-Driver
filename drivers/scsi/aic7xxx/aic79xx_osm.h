/*
 * Adaptec AIC79xx device driver for Linux.
 *
 * Copyright (c) 2000-2001 Adaptec Inc.
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
#ifndef _AIC79XX_LINUX_H_
#define _AIC79XX_LINUX_H_

#include <linux/version.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#include <linux/config.h>

/*********************************** Debugging ********************************/
#ifdef CONFIG_AIC79XX_DEBUG_ENABLE
#ifdef CONFIG_AIC79XX_DEBUG_MASK
#define AHD_DEBUG 1
#define AHD_DEBUG_OPTS CONFIG_AIC79XX_DEBUG_MASK
#else
/*
 * Compile in debugging code, but do not enable any printfs.
 */
#define AHD_DEBUG 1
#define AHD_DEBUG_OPTS 0
#endif
/* No debugging code. */
#endif

/********************************** Includes **********************************/
/* Core SCSI definitions */
#define AIC_LIB_PREFIX ahd
#define AIC_CONST_PREFIX AHD

#ifdef CONFIG_AIC79XX_REG_PRETTY_PRINT
#define AIC_DEBUG_REGISTERS 1
#else
#define AIC_DEBUG_REGISTERS 0
#endif
#define AIC_CORE_INCLUDE "aic79xx.h"
#include "aiclib.h"

/************************* Configuration Data *********************************/
extern uint32_t aic79xx_allow_memio;
extern int aic79xx_detect_complete;
extern Scsi_Host_Template aic79xx_driver_template;

/***************************** Domain Validation ******************************/
void ahd_linux_dv_complete(Scsi_Cmnd *cmd);
void ahd_linux_dv_timeout(struct scsi_cmnd *cmd);

/***************************** SMP support ************************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,17)
#include <linux/spinlock.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,93)
#include <linux/smp.h>
#endif

#define AIC79XX_DRIVER_VERSION "2.0.8"

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
 * allocator.  See aic79xx_osm.c ahd_linux_size_nseg() for details.
 */
extern u_int ahd_linux_nseg;
#define	AHD_NSEG ahd_linux_nseg
#define	AHD_LINUX_MIN_NSEG 64
#else
#define	AHD_NSEG 128
#endif

/************************** Error Recovery ************************************/
static __inline void	ahd_wakeup_recovery_thread(struct ahd_softc *ahd); 
  
static __inline void
ahd_wakeup_recovery_thread(struct ahd_softc *ahd)
{ 
	up(&ahd->platform_data->recovery_sem);
}
 
int			ahd_spawn_recovery_thread(struct ahd_softc *ahd);
void			ahd_terminate_recovery_thread(struct ahd_softc *ahd);
void			ahd_set_recoveryscb(struct ahd_softc *ahd,
					    struct scb *scb);

/***************************** Low Level I/O **********************************/
static __inline uint8_t ahd_inb(struct ahd_softc * ahd, long port);
static __inline uint16_t ahd_inw_atomic(struct ahd_softc * ahd, long port);
static __inline void ahd_outb(struct ahd_softc * ahd, long port, uint8_t val);
static __inline void ahd_outw_atomic(struct ahd_softc * ahd,
				     long port, uint16_t val);
static __inline void ahd_outsb(struct ahd_softc * ahd, long port,
			       uint8_t *, int count);
static __inline void ahd_insb(struct ahd_softc * ahd, long port,
			       uint8_t *, int count);
static __inline void ahd_flush_device_writes(struct ahd_softc *);

static __inline uint8_t
ahd_inb(struct ahd_softc * ahd, long port)
{
	uint8_t x;

	if (ahd->tags[0] == BUS_SPACE_MEMIO) {
		x = readb(ahd->bshs[0].maddr + port);
	} else {
		x = inb(ahd->bshs[(port) >> 8].ioport + ((port) & 0xFF));
	}
	mb();
	return (x);
}

static __inline uint16_t
ahd_inw_atomic(struct ahd_softc * ahd, long port)
{
	uint8_t x;

	if (ahd->tags[0] == BUS_SPACE_MEMIO) {
		x = readw(ahd->bshs[0].maddr + port);
	} else {
		x = inw(ahd->bshs[(port) >> 8].ioport + ((port) & 0xFF));
	}
	mb();
	return (x);
}

static __inline void
ahd_outb(struct ahd_softc * ahd, long port, uint8_t val)
{
	if (ahd->tags[0] == BUS_SPACE_MEMIO) {
		writeb(val, ahd->bshs[0].maddr + port);
	} else {
		outb(val, ahd->bshs[(port) >> 8].ioport + (port & 0xFF));
	}
	mb();
}

static __inline void
ahd_outw_atomic(struct ahd_softc * ahd, long port, uint16_t val)
{
	if (ahd->tags[0] == BUS_SPACE_MEMIO) {
		writew(val, ahd->bshs[0].maddr + port);
	} else {
		outw(val, ahd->bshs[(port) >> 8].ioport + (port & 0xFF));
	}
	mb();
}

static __inline void
ahd_outsb(struct ahd_softc * ahd, long port, uint8_t *array, int count)
{
	int i;

	/*
	 * There is probably a more efficient way to do this on Linux
	 * but we don't use this for anything speed critical and this
	 * should work.
	 */
	for (i = 0; i < count; i++)
		ahd_outb(ahd, port, *array++);
}

static __inline void
ahd_insb(struct ahd_softc * ahd, long port, uint8_t *array, int count)
{
	int i;

	/*
	 * There is probably a more efficient way to do this on Linux
	 * but we don't use this for anything speed critical and this
	 * should work.
	 */
	for (i = 0; i < count; i++)
		*array++ = ahd_inb(ahd, port);
}

static __inline void
ahd_flush_device_writes(struct ahd_softc *ahd)
{
	/* XXX Is this sufficient for all architectures??? */
	ahd_inb(ahd, INTSTAT);
}

/**************************** Initialization **********************************/
extern int	ahd_init_status;
int		ahd_linux_register_host(struct ahd_softc *,
					Scsi_Host_Template *);

uint64_t	ahd_linux_get_memsize(void);

/*************************** Pretty Printing **********************************/
struct info_str {
	char *buffer;
	int length;
	off_t offset;
	int pos;
};

void	ahd_format_transinfo(struct info_str *info,
			     struct ahd_transinfo *tinfo);

/******************************* PCI Definitions ******************************/
extern struct pci_driver aic79xx_pci_driver;

/******************************* PCI Routines *********************************/
int			 ahd_linux_pci_init(void);
void			 ahd_linux_pci_exit(void);
int			 ahd_pci_map_registers(struct ahd_softc *ahd);
int			 ahd_pci_map_int(struct ahd_softc *ahd);

/**************************** Proc FS Support *********************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
int	ahd_linux_proc_info(char *, char **, off_t, int, int, int);
#else
int	ahd_linux_proc_info(struct Scsi_Host *, char *, char **,
			    off_t, int, int);
#endif

/*********************** Transaction Access Wrappers **************************/
int	ahd_platform_alloc(struct ahd_softc *ahd, void *platform_arg);
void	ahd_platform_free(struct ahd_softc *ahd);
void	ahd_platform_init(struct ahd_softc *ahd);
void	ahd_platform_freeze_devq(struct ahd_softc *ahd, struct scb *scb);

void	ahd_platform_set_tags(struct ahd_softc *ahd,
			      struct ahd_devinfo *devinfo, ahd_queue_alg);
int	ahd_platform_abort_scbs(struct ahd_softc *ahd, int target,
				char channel, int lun, u_int tag,
				role_t role, uint32_t status);
irqreturn_t
	ahd_linux_isr(int irq, void *dev_id, struct pt_regs * regs);
void	ahd_platform_flushwork(struct ahd_softc *ahd);
int	ahd_softc_comp(struct ahd_softc *, struct ahd_softc *);
void	ahd_done(struct ahd_softc*, struct scb*);
void	ahd_send_async(struct ahd_softc *, char channel,
		       u_int target, u_int lun, ac_code, void *);
void	ahd_print_path(struct ahd_softc *, struct scb *);
void	ahd_platform_dump_card_state(struct ahd_softc *ahd);

#ifdef CONFIG_PCI
#define AIC_PCI_CONFIG 1
#else
#define AIC_PCI_CONFIG 0
#endif
#define bootverbose aic79xx_verbose
extern uint32_t aic79xx_verbose;
#endif /* _AIC79XX_LINUX_H_ */
