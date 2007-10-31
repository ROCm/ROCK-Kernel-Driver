/*
 *   Creation Date: <2004/01/25 17:48:51 samuel>
 *   Time-stamp: <2004/01/25 22:12:13 samuel>
 *
 *	<context.h>
 *
 *
 *
 *   Copyright (C) 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_CONTEXT
#define _H_CONTEXT

#define CTX_MASK		0xfffff		/* VSID_MASK >> 4 */

/*
 * Three types of contexts are used:
 *
 *	VSIDs		(24 bit, loaded into the CPU register)
 *	mol_contexts	(number between FIRST_ and LAST_MOL_CONTEXT)
 *	arch_contexts	(context number used by the kernel)
 *
 * The relation between them is
 *
 *	mol_context 	= (os_context << 4) + segment#
 *	VSID_context	= MUNGE_CONTEXT(mol_context>>4) + ESID_ADD * (mol_context & 0xf)
 */

/*
 * The new MM implementation (about 2.4.14 (?)) uses the following algorithm
 *
 *	VSID = (((context * 897) << 4) + ((va>>28) * 0x111)) & 0xffffff
 *
 * Only context 0..32767 are used. We can use context 32768..0xfffff.
 * The old MM implementation used
 *
 *	VSID = (((context * 897) << 4) + (va>>28)) & 0xffffff
 */

#define MUNGE_ADD_NEXT		897
#define MUNGE_MUL_INVERSE	2899073		/* Multiplicative inverse of 897 (modulo VSID_MASK+1) */
#define MUNGE_ESID_ADD		0x111
#define MUNGE_CONTEXT(c)	(((c) * (MUNGE_ADD_NEXT * 16)) & (CTX_MASK <<4))

/* mol_contexts == linux_context * 16 + esid */
#define PER_SESSION_CONTEXTS	0x10000		/* more than we will need (10^6) */
#define FIRST_MOL_CONTEXT(sess)	((CTX_MASK - PER_SESSION_CONTEXTS*((sess)+1)) << 4)
#define LAST_MOL_CONTEXT(sess)	(((CTX_MASK - PER_SESSION_CONTEXTS*(sess)) << 4) | 0xf)


#if FIRST_MOL_CONTEXT(MAX_NUM_SESSIONS-1) < (32768 << 4)
#error "Too many MOL contexts..."
#endif


#endif   /* _H_CONTEXT */
