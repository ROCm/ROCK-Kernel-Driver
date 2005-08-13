/* 
 *   Creation Date: <1998-10-31 03:11:06 samuel>
 *   Time-stamp: <2003/06/05 09:00:54 samuel>
 *   
 *	<mmu_mappings.h>
 *	
 *	Mappings mac physical <-> linux virtual
 *   
 *   Copyright (C) 1998-2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_MMU_MAPPINGS
#define _H_MMU_MAPPINGS

typedef struct mmu_mapping {
	ulong	mbase;		/* mac physical base */
	char	*lvbase;	/* linux virtual base */
	size_t	size;		/* size (in bytes) */
	int	flags;		/* MAPPING_xxx */

	int	id;		/* set to zero, returned by the kerrnel module */
} mmu_mapping_t;

/* mmu_mapping flags */

#define MAPPING_RO			1	/* read only memory (ROM etc) */
#define MAPPING_PHYSICAL		2 	/* physical (ROM etc) */
#define MAPPING_SCRATCH			4	/* (block transl) scratch area */
#define MAPPING_FORCE_CACHE		8	/* force write-through caching */
#define MAPPING_FB_ACCEL		16	/* offscreen framebuffer, track changes */
#define MAPPING_FB			32	/* framebuffer (ea assumed to be constant) */
#define MAPPING_DBAT			64	/* allow use of a DBAT register */
#define MAPPING_MACOS_CONTROLS_CACHE	128	/* do not force WIM bits to 001 */
#define MAPPING_PUT_FIRST		256	/* take precedence over other translations */

#ifdef __KERNEL__
#define MAPPING_IO			1024	/* I/O translation */
#define MAPPING_VALID			2048	/* valid bit */
#endif

#endif   /* _H_MMU_MAPPINGS */
