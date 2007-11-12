/* 
 *   Creation Date: <2004/02/14 11:45:23 samuel>
 *   Time-stamp: <2004/02/21 21:24:46 samuel>
 *   
 *	<hash.h>
 *	
 *	
 *   
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_HASH
#define _H_HASH

typedef struct {
	ulong		sdr1;			/* sdr1 used by MOL */
	
	ulong		pteg_mask;		/* pteg offset mask (e.g. 0xffc0) */
	ulong		pte_mask;		/* pte offset mask (e.g. 0xfff8) */
	
	ulong		*base;			/* kernel mapping of hash */
	ulong		physbase;		/* physical address of hash */
} hash_info_t;

extern hash_info_t	ptehash;

extern int 		init_hash( void );
extern void		cleanup_hash( void );


#endif   /* _H_HASH */
