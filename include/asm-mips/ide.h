/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains the MIPS architecture specific IDE code.
 *
 * Copyright (C) 1994-1996  Linus Torvalds & authors
 */

#ifndef __ASM_IDE_H
#define __ASM_IDE_H

#ifdef __KERNEL__

#include <ide.h>

#define __ide_mm_insw   ide_insw
#define __ide_mm_insl   ide_insl
#define __ide_mm_outsw  ide_outsw
#define __ide_mm_outsl  ide_outsl

#endif /* __KERNEL__ */

#endif /* __ASM_IDE_H */
