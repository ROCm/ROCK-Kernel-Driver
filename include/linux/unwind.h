#ifndef _LINUX_UNWIND_H
#define _LINUX_UNWIND_H

/*
 * Copyright (C) 2002-2009 Novell, Inc.
 *     Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 *
 * A simple API for unwinding kernel stacks.  This is used for
 * debugging and error reporting purposes.  The kernel doesn't need
 * full-blown stack unwinding with all the bells and whistles, so there
 * is not much point in implementing the full Dwarf2 unwind API.
 */

#include <linux/linkage.h>

#ifdef CONFIG_X86
#include <asm/unwind.h>
#endif

struct module;

#ifdef CONFIG_DWARF_UNWIND

#include <asm/stacktrace.h>

#ifndef ARCH_DWARF_SECTION_NAME
#define ARCH_DWARF_SECTION_NAME ".eh_frame"
#endif

/*
 * Initialize unwind support.
 */
void dwarf_init(void);
void dwarf_setup(void);

#ifdef CONFIG_MODULES

void *dwarf_add_table(struct module *mod, const void *table_start,
		unsigned long table_size);

void dwarf_remove_table(void *handle, bool init_only);

#endif

asmlinkage void arch_dwarf_init_running(struct pt_regs *regs);

/*
 * Unwind to previous to frame.  Returns 0 if successful, negative
 * number in case of an error.
 */
int dwarf_unwind(struct unwind_state *state);

#else /* CONFIG_DWARF_UNWIND */

static inline void dwarf_init(void) {}
static inline void dwarf_setup(void) {}

#ifdef CONFIG_MODULES

static inline void *dwarf_add_table(struct module *mod,
		const void *table_start, unsigned long table_size)
{
	return NULL;
}

static inline void dwarf_remove_table(void *handle, bool init_only)
{
}

#endif

#endif /* CONFIG_DWARF_UNWIND */

#endif /* _LINUX_UNWIND_H */
