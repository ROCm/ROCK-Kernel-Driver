/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_CURRENT_H
#define _ASM_CURRENT_H

#ifdef _LANGUAGE_C

/* MIPS rules... */
register struct task_struct *current asm("$28");

#endif /* _LANGUAGE_C */

#endif /* _ASM_CURRENT_H */
