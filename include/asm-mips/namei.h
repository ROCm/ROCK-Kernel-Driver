/*
 * Included from linux/fs/namei.c
 */
#ifndef _ASM_NAMEI_H
#define _ASM_NAMEI_H

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

#endif /* _ASM_NAMEI_H */
