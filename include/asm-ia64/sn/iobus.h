/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_IOBUS_H
#define _ASM_SN_IOBUS_H

#ifdef __cplusplus
extern "C" {
#endif

struct eframe_s;
struct piomap;
struct dmamap;


/* for ilvl_t interrupt level, for use with intr_block_level.  Can't
 * typedef twice without causing warnings, and some users of this header
 * file do not already include driver.h, but expect ilvl_t to be defined,
 * while others include both, leading to the warning ...
 */

#include <asm/types.h>
#include <asm/sn/driver.h>


typedef __psunsigned_t iobush_t;

#if __KERNEL__
/* adapter handle */
typedef devfs_handle_t adap_t;
#endif


/* interrupt function */
typedef void	       *intr_arg_t;
typedef void		intr_func_f(intr_arg_t);
typedef intr_func_f    *intr_func_t;

#define	INTR_ARG(n)	((intr_arg_t)(__psunsigned_t)(n))

/* system interrupt resource handle -- returned from intr_alloc */
typedef struct intr_s *intr_t;
#define INTR_HANDLE_NONE ((intr_t)0)

/*
 * restore interrupt level value, returned from intr_block_level
 * for use with intr_unblock_level.
 */
typedef void *rlvl_t;


/* 
 * A basic, platform-independent description of I/O requirements for
 * a device. This structure is usually formed by lboot based on information 
 * in configuration files.  It contains information about PIO, DMA, and
 * interrupt requirements for a specific instance of a device.
 *
 * The pio description is currently unused.
 *
 * The dma description describes bandwidth characteristics and bandwidth
 * allocation requirements. (TBD)
 *
 * The Interrupt information describes the priority of interrupt, desired 
 * destination, policy (TBD), whether this is an error interrupt, etc.  
 * For now, interrupts are targeted to specific CPUs.
 */

typedef struct device_desc_s {
	/* pio description (currently none) */

	/* dma description */
	/* TBD: allocated badwidth requirements */

	/* interrupt description */
	devfs_handle_t	intr_target;	/* Hardware locator string */
	int 		intr_policy;	/* TBD */
	ilvl_t		intr_swlevel;	/* software level for blocking intr */
	char		*intr_name;	/* name of interrupt, if any */

	int		flags;
} *device_desc_t;

/* flag values */
#define	D_INTR_ISERR	0x1		/* interrupt is for error handling */
#define D_IS_ASSOC	0x2		/* descriptor is associated with a dev */
#define D_INTR_NOTHREAD	0x4		/* Interrupt handler isn't threaded. */

#define INTR_SWLEVEL_NOTHREAD_DEFAULT 	0	/* Default
						 * Interrupt level in case of
						 * non-threaded interrupt 
						 * handlers
						 */
/* 
 * Drivers use these interfaces to manage device descriptors.
 *
 * To examine defaults:
 *	desc = device_desc_default_get(dev);
 *	device_desc_*_get(desc);
 *
 * To modify defaults:
 *	desc = device_desc_default_get(dev);
 *	device_desc_*_set(desc);
 *
 * To eliminate defaults:
 *	device_desc_default_set(dev, NULL);
 *
 * To override defaults:
 *	desc = device_desc_dup(dev);
 *	device_desc_*_set(desc,...);
 *	use device_desc in calls
 *	device_desc_free(desc);
 *
 * Software must not set or eliminate default device descriptors for a device while
 * concurrently get'ing, dup'ing or using them.  Default device descriptors can be 
 * changed only for a device that is quiescent.  In general, device drivers have no
 * need to permanently change defaults anyway -- they just override defaults, when
 * necessary.
 */
extern device_desc_t	device_desc_dup(devfs_handle_t dev);
extern void		device_desc_free(device_desc_t device_desc);
extern device_desc_t	device_desc_default_get(devfs_handle_t dev);
extern void		device_desc_default_set(devfs_handle_t dev, device_desc_t device_desc);

extern devfs_handle_t	device_desc_intr_target_get(device_desc_t device_desc);
extern int		device_desc_intr_policy_get(device_desc_t device_desc);
extern ilvl_t		device_desc_intr_swlevel_get(device_desc_t device_desc);
extern char *		device_desc_intr_name_get(device_desc_t device_desc);
extern int		device_desc_flags_get(device_desc_t device_desc);

extern void		device_desc_intr_target_set(device_desc_t device_desc, devfs_handle_t target);
extern void		device_desc_intr_policy_set(device_desc_t device_desc, int policy);
extern void		device_desc_intr_swlevel_set(device_desc_t device_desc, ilvl_t swlevel);
extern void		device_desc_intr_name_set(device_desc_t device_desc, char *name);
extern void		device_desc_flags_set(device_desc_t device_desc, int flags);


/* IO state */
#ifdef COMMENT
#define IO_STATE_EMPTY			0x01	/* non-existent */
#define IO_STATE_INITIALIZING		0x02	/* being initialized */
#define IO_STATE_ATTACHING   		0x04    /* becoming active */
#define IO_STATE_ACTIVE      		0x08    /* active */
#define IO_STATE_DETACHING   		0x10    /* becoming inactive */
#define IO_STATE_INACTIVE    		0x20    /* not in use */
#define IO_STATE_ERROR			0x40    /* problems */
#define IO_STATE_BAD_HARDWARE		0x80	/* broken hardware */
#endif

struct edt;


/* return codes */
#define RC_OK				0	
#define RC_ERROR			1

/* bus configuration management op code */
#define IOBUS_CONFIG_ATTACH		0	/* vary on */
#define IOBUS_CONFIG_DETACH		1	/* vary off */
#define IOBUS_CONFIG_RECOVER		2	/* clear error then vary on */

/* get low-level PIO handle */
extern int pio_geth(struct piomap*, int bus, int bus_id, int subtype, 
	iopaddr_t addr, int size);	

/* get low-level DMA handle */
extern int dma_geth(struct dmamap*, int bus_type, int bus_id, int dma_type, 
	int npages, int page_size, int flags);	

#ifdef __cplusplus
}
#endif

/*
 * Macros for page number and page offsets, using ps as page size
 */
#define x_pnum(addr, ps) ((__psunsigned_t)(addr) / (__psunsigned_t)(ps))
#define x_poff(addr, ps) ((__psunsigned_t)(addr) & ((__psunsigned_t)(ps) - 1))

#endif /* _ASM_SN_IOBUS_H */
