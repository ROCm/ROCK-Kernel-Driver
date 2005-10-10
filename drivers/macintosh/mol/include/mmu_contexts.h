/* 
 *   Creation Date: <97/07/17 14:26:14 samuel>
 *   Time-stamp: <2003/06/06 19:17:26 samuel>
 *   
 *	<mmu_contexts.h>
 *	
 *	
 *   
 *   Copyright (C) 1997, 2001, 2002, 2003 Samuel Rydh
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *   
 */

#ifndef _H_MMU_CONTEXTS
#define _H_MMU_CONTEXTS

/**********************************************************
 * EVERYTHING IN THIS FILE IS UESED FOR DEBUGGING ONLY
 *********************************************************/

/* MMU context identifiers */
#define kContextUnmapped	1
#define kContextMapped_S	2
#define kContextMapped_U	3

#define kContextEmulator     	20	/* for debugging purposes ONLY !!!! */
#define kContextKernel     	21	/* for debugging purposes ONLY !!!! */


/* Flags returned by _get_physical_page(). The first flags should
 * equal the _PAGE_XXX of the old MM implementation (<2.4.6).
 */
#define M_PAGE_PRESENT		0x001	/* software: pte contains a translation */
#define M_PAGE_USER		0x002	/* usermode access allowed */
#define M_PAGE_RW		0x004	/* usermode access allowed */
#define M_PAGE_GUARDED		0x008	/* G: prohibit speculative access */
#define M_PAGE_COHERENT		0x010	/* M: enforce memory coherence (SMP systems) */
#define M_PAGE_NO_CACHE		0x020	/* I: cache inhibit */
#define M_PAGE_WRITETHRU	0x040	/* W: cache write-through */
#define M_PAGE_DIRTY		0x080	/* C: page changed */
#define M_PAGE_ACCESSED		0x100	/* R: page referenced */
/* new linux-MM implementation */
#define M_PAGE_HASHPTE		0x1000	/* hash_page has made an HPTE for this pte */
#define M_PAGE_EXEC		0x2000	/* software: i-cache coherency required */

#ifdef __KERNEL__
#define DBG_TRANSL_PAGE_FLAG( val, flagname ) \
	(((val) & flagname )? M##flagname : 0)
#endif


#endif   /* _H_MMU_CONTEXTS */
