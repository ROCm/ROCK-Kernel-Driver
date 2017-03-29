/*
 * Copyright (C) 2002-2017 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 *	Jiri Slaby <jirislaby@kernel.org>
 * This code is released under version 2 of the GNU GPL.
 */

#ifndef _LINUX_DWARF_H
#define _LINUX_DWARF_H

#include <linux/linkage.h>

struct module;
struct dwarf_table;

#ifdef CONFIG_DWARF_UNWIND

#include <asm/stacktrace.h>
#include <asm/unwind.h>

#ifndef ARCH_DWARF_SECTION_NAME
#define ARCH_DWARF_SECTION_NAME ".eh_frame"
#endif

void dwarf_init(void);
void dwarf_setup(void);

#ifdef CONFIG_MODULES

struct dwarf_table *dwarf_add_table(struct module *mod, const void *table_start,
		unsigned long table_size);
void dwarf_remove_table(struct dwarf_table *table, bool init_only);

#endif /* CONFIG_MODULES */

asmlinkage void arch_dwarf_init_running(struct pt_regs *regs);

int dwarf_unwind(struct unwind_state *state);

#else /* CONFIG_DWARF_UNWIND */

static inline void dwarf_init(void) {}
static inline void dwarf_setup(void) {}

#ifdef CONFIG_MODULES

static inline struct dwarf_table *dwarf_add_table(struct module *mod,
		const void *table_start, unsigned long table_size)
{
	return NULL;
}

static inline void dwarf_remove_table(struct dwarf_table *table, bool init_only)
{
}

#endif /* CONFIG_MODULES */

#endif /* CONFIG_DWARF_UNWIND */

#endif /* _LINUX_DWARF_H */
