/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */


#ifndef _ASM_IA64_SN_SGI_H
#define _ASM_IA64_SN_SGI_H

#include <linux/config.h>

#include <asm/sn/types.h>
#include <asm/sn/hwgfs.h>

typedef hwgfs_handle_t vertex_hdl_t;

/* Nice general name length that lots of people like to use */
#ifndef MAXDEVNAME
#define MAXDEVNAME 256
#endif


/*
 * Possible return values from graph routines.
 */
typedef enum graph_error_e {
	GRAPH_SUCCESS,		/* 0 */
	GRAPH_DUP,		/* 1 */
	GRAPH_NOT_FOUND,	/* 2 */
	GRAPH_BAD_PARAM,	/* 3 */
	GRAPH_HIT_LIMIT,	/* 4 */
	GRAPH_CANNOT_ALLOC,	/* 5 */
	GRAPH_ILLEGAL_REQUEST,	/* 6 */
	GRAPH_IN_USE		/* 7 */
} graph_error_t;

#define CNODEID_NONE ((cnodeid_t)-1)
#define CPU_NONE		(-1)
#define GRAPH_VERTEX_NONE ((vertex_hdl_t)-1)

/*
 * Defines for individual WARs. Each is a bitmask of applicable
 * part revision numbers. (1 << 1) == rev A, (1 << 2) == rev B,
 * (3 << 1) == (rev A or rev B), etc
 */
#define PV854697 (~0)     /* PIC: write 64bit regs as 64bits. permanent */
#define PV854827 (~0UL)   /* PIC: fake widget 0xf presence bit. permanent */
#define PV855271 (1 << 1) /* PIC: use virt chan iff 64-bit device. */
#define PV878674 (~0)     /* PIC: Dont allow 64bit PIOs.  permanent */
#define PV855272 (1 << 1) /* PIC: runaway interrupt WAR */
#define PV856155 (1 << 1) /* PIC: arbitration WAR */
#define PV856864 (1 << 1) /* PIC: lower timeout to free TNUMs quicker */
#define PV856866 (1 << 1) /* PIC: avoid rrb's 0/1/8/9. */
#define PV862253 (1 << 1) /* PIC: don't enable write req RAM parity checking */
#define PV867308 (3 << 1) /* PIC: make LLP error interrupts FATAL for PIC */

/*
 * No code is complete without an Assertion macro
 */

#if defined(DISABLE_ASSERT)
#define ASSERT(expr)
#define ASSERT_ALWAYS(expr)
#else
#define ASSERT(expr)  do {	\
        if(!(expr)) { \
		printk( "Assertion [%s] failed! %s:%s(line=%d)\n",\
			#expr,__FILE__,__FUNCTION__,__LINE__); \
		panic("Assertion panic\n"); 	\
        } } while(0)

#define ASSERT_ALWAYS(expr)	do {\
        if(!(expr)) { \
		printk( "Assertion [%s] failed! %s:%s(line=%d)\n",\
			#expr,__FILE__,__FUNCTION__,__LINE__); \
		panic("Assertion always panic\n"); 	\
        } } while(0)
#endif	/* DISABLE_ASSERT */

#endif /* _ASM_IA64_SN_SGI_H */
