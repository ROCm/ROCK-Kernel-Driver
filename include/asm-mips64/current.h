/* $Id: current.h,v 1.3 1999/12/04 03:59:12 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999 Ralf Baechle
 */
#ifndef _ASM_CURRENT_H
#define _ASM_CURRENT_H

#ifdef _LANGUAGE_C

/* MIPS rules... */
register struct task_struct *current asm("$28");

#endif /* _LANGUAGE_C */
#endif /* _ASM_CURRENT_H */
