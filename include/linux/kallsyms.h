/* Rewritten and vastly simplified by Rusty Russell for in-kernel
 * module loader:
 *   Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 */
#ifndef _LINUX_KALLSYMS_H
#define _LINUX_KALLSYMS_H

#include <linux/config.h>

#ifdef CONFIG_KALLSYMS
/* Lookup an address.  modname is set to NULL if it's in the kernel. */
const char *kallsyms_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf);

/* Replace "%s" in format with address, if found */
extern void __print_symbol(const char *fmt, unsigned long address);

#else /* !CONFIG_KALLSYMS */

static inline const char *kallsyms_lookup(unsigned long addr,
					  unsigned long *symbolsize,
					  unsigned long *offset,
					  char **modname, char *namebuf)
{
	return NULL;
}

/* Stupid that this does nothing, but I didn't create this mess. */
#define __print_symbol(fmt, addr)
#endif /*CONFIG_KALLSYMS*/

/* This macro allows us to keep printk typechecking */
static void __check_printsym_format(const char *fmt, ...)
__attribute__((format(printf,1,2)));
static inline void __check_printsym_format(const char *fmt, ...)
{
}

#define print_symbol(fmt, addr)			\
do {						\
	__check_printsym_format(fmt, "");	\
	__print_symbol(fmt, addr);		\
} while(0)

#endif /*_LINUX_KALLSYMS_H*/
