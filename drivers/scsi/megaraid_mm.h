/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid_mm.h
 * Version	: v2.20.0 (Apr 14 2004)
 */

#ifndef MEGARAID_MM_H
#define MEGARAID_MM_H

#include <linux/spinlock.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/reboot.h>

#include "mbox_defs.h"

#define LSI_COMMON_MOD_VERSION	\
	"v1.0.0.B1.04.07.2004 (Release Date: Wed Apr  7 17:20:39 EDT 2004)"

#define LSI_DBGLVL	dbglevel
/*
 * Sz of mem allocated for each internal ioctl packet
 */
#define MEMBLK_SZ	(28*1024)

/*
 * Localizing ioctl32 differences
 */

#if defined (CONFIG_COMPAT) || defined(__x86_64__) || defined(IA32_EMULATION)
#define LSI_CONFIG_COMPAT
#endif

#ifdef LSI_CONFIG_COMPAT
#include <asm/ioctl32.h>
#else
#define register_ioctl32_conversion(a,b)	do{}while(0)
#define unregister_ioctl32_conversion(a)	do{}while(0)
#endif /* LSI_CONFIG_COMPAT */

/**
 * mimd_t	: Old style ioctl packet structure (depracated)
 *
 * @inlen	:
 * @outlen	:
 * @fca		:
 * @opcode	:
 * @subopcode	:
 * @adapno	:
 * @buffer	:
 * @pad		:
 * @length	:
 * @mbox	:
 * @pthru	:
 * @data	:
 * @pad		:
 *
 * Note		: This structure is DEPRACATED. New applications must use
 *		: uioc_t structure instead. All new hba drivers use the new
 *		: format. If we get this mimd packet, we will convert it into
 *		: new uioc_t format and send it to the hba drivers.
 */

typedef struct mimd {

	uint32_t inlen;
	uint32_t outlen;

	union {
		uint8_t fca[16];
		struct {
			uint8_t opcode;
			uint8_t subopcode;
			uint16_t adapno;
#if BITS_PER_LONG == 32
			uint8_t *buffer;
			uint8_t pad[4];
#endif
#if BITS_PER_LONG == 64
			uint8_t *buffer;
#endif
			uint32_t length;
		} __attribute__ ((packed)) fcs;
	} __attribute__ ((packed)) ui;

	uint8_t mbox[18];		/* 16 bytes + 2 status bytes */
	mraid_passthru_t pthru;

#if BITS_PER_LONG == 32
	char *data;		/* buffer <= 4096 for 0x80 commands */
	char pad[4];
#endif
#if BITS_PER_LONG == 64
	char *data;
#endif

} __attribute__ ((packed))mimd_t;

/*
 * Entry points for char node driver
 */
static int	megaraid_mm_open ( struct inode*, struct file* );
static int	megaraid_mm_close( struct inode*, struct file* );
static int	megaraid_mm_ioctl( struct inode*, struct file*, uint, ulong );

/*
 * routines to convert to and from the old the format
 */
static int	mimd_to_kioc( mimd_t*, int* );
static int	kioc_to_mimd( uioc_t*, mimd_t* );

/*
 * Helper functions
 */
static int	handle_drvrcmd( ulong, uint8_t, int* );
static int	lld_ioctl( mraid_mmadp_t*, uioc_t* );
void		ioctl_done( uioc_t* );
void		lld_timedout( ulong );
static void	hinfo_to_cinfo( mraid_hba_info_t*, mcontroller_t*);

#ifdef LSI_CONFIG_COMPAT
static int	megaraid_mm_compat_ioctl( unsigned int, unsigned int,
						ulong, struct file* );
#endif

#endif /*MEGARAID_MM_H*/

