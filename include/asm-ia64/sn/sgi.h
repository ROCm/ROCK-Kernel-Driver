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
#include <asm/uaccess.h>		/* for copy_??_user */
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/sn/hwgfs.h>

typedef hwgfs_handle_t vertex_hdl_t;

/* Nice general name length that lots of people like to use */
#ifndef MAXDEVNAME
#define MAXDEVNAME 256
#endif

typedef enum { B_FALSE, B_TRUE } boolean_t;


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
