/*
 * linux/include/asm-mips/namei.h
 *
 * Included from linux/fs/namei.c
 *
 * $Id: namei.h,v 1.6 1999/01/04 16:09:23 ralf Exp $
 */
#ifndef __ASM_MIPS_NAMEI_H
#define __ASM_MIPS_NAMEI_H

#include <linux/config.h>

/* Only one at this time. */
#define IRIX32_EMUL "usr/gnemul/irix/"

#ifdef CONFIG_BINFMT_IRIX

static inline char *__emul_prefix(void)
{
	if (current->personality != PER_IRIX32)
		return NULL;
	return IRIX32_EMUL;
}

#else /* !defined(CONFIG_BINFMT_IRIX) */

#define __emul_prefix() NULL

#endif /* !defined(CONFIG_BINFMT_IRIX) */

#endif /* __ASM_MIPS_NAMEI_H */
