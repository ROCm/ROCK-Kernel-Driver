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
 */

#ifndef MEGARAID_MM_H
#define MEGARAID_MM_H

#include <linux/spinlock.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/list.h>

#include "mbox_defs.h"
#include "megaraid_ioctl.h"


#define LSI_COMMON_MOD_VERSION	"2.20.0.0"
#define LSI_COMMON_MOD_EXT_VERSION	\
		"(Release Date: Wed Jun 23 11:38:38 EDT 2004)"


#define LSI_DBGLVL			dbglevel

// The smallest dma pool
#define MRAID_MM_INIT_BUFF_SIZE		4096

/*
 * Localizing ioctl32 differences
 */
#include <linux/ioctl32.h>

/**
 * mimd_t	: Old style ioctl packet structure (deprecated)
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
 * Note		: This structure is DEPRECATED. New applications must use
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


// Entry points for char node driver
static int mraid_mm_open(struct inode *, struct file *);
static int mraid_mm_ioctl(struct inode *, struct file *, uint, unsigned long);


// routines to convert to and from the old the format
static int mimd_to_kioc(mimd_t *, mraid_mmadp_t *, uioc_t *);
static int kioc_to_mimd(uioc_t *, mimd_t *);


// Helper functions
static int handle_drvrcmd(unsigned long, uint8_t, int *);
static int lld_ioctl(mraid_mmadp_t *, uioc_t *);
static void ioctl_done(uioc_t *);
static void lld_timedout(unsigned long);
static void hinfo_to_cinfo(mraid_hba_info_t *, mcontroller_t *);
static int mraid_mm_get_adpindex(mimd_t *, int *);
static uioc_t *mraid_mm_alloc_kioc(mraid_mmadp_t *);
static void mraid_mm_dealloc_kioc(mraid_mmadp_t *, uioc_t *);
static int mraid_mm_attach_buf(mraid_mmadp_t *, uioc_t *, int);
static int mraid_mm_setup_dma_pools(mraid_mmadp_t *);
static void mraid_mm_free_adp_resources(mraid_mmadp_t *);
static void mraid_mm_teardown_dma_pools(mraid_mmadp_t *);

#ifdef CONFIG_COMPAT
static int mraid_mm_compat_ioctl(unsigned int, unsigned int, unsigned long,
		struct file *);
#endif

#endif // MEGARAID_MM_H

// vi: set ts=8 sw=8 tw=78:
