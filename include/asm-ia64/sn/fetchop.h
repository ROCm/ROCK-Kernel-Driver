/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2002 Silicon Graphics, Inc.  All rights reserved.
 */


#ifndef _ASM_IA64_SN_FETCHOP_H
#define _ASM_IA64_SN_FETCHOP_H

#define FETCHOP_BASENAME	"sgi_fetchop"
#define FETCHOP_FULLNAME	"/dev/sgi_fetchop"



#define FETCHOP_VAR_SIZE 64 /* 64 byte per fetchop variable */

#define FETCHOP_LOAD		0
#define FETCHOP_INCREMENT	8
#define FETCHOP_DECREMENT	16
#define FETCHOP_CLEAR		24

#define FETCHOP_STORE		0
#define FETCHOP_AND		24
#define FETCHOP_OR		32

#define FETCHOP_CLEAR_CACHE	56

#define FETCHOP_LOAD_OP(addr, op) ( \
         *(long *)((char*) (addr) + (op)))

#define FETCHOP_STORE_OP(addr, op, x) ( \
         *(long *)((char*) (addr) + (op)) = \
              (long) (x))

#endif /* _ASM_IA64_SN_FETCHOP_H */

