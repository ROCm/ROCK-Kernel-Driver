#ifndef _ASM_KDB_BREAK_H
#define _ASM_KDB_BREAK_H

/*
 * Kernel Debugger Architecture Dependent Global Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Break numbers are used by CONFIG_KDB_LOCK code.  They need to be seperated
 * from asm/kdb.h to let spinlock code build without pulling in all of the kdb
 * headers.
 */

#define KDB_BREAK_BREAK 0x80100		/* kdb breakpoint in kernel */
#define KDB_BREAK_ENTER 0x80101		/* KDB_ENTER() */

#endif	/* !_ASM_KDB_BREAK_H */
