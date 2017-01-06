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

struct module;
struct unwind_state;

#ifdef CONFIG_STACK_UNWIND

#include <asm/unwind.h>
#include <asm/stacktrace.h>

#ifndef ARCH_UNWIND_SECTION_NAME
#define ARCH_UNWIND_SECTION_NAME ".eh_frame"
#endif

/*
 * Initialize unwind support.
 */
void unwind_init(void);
void unwind_setup(void);

#ifdef CONFIG_MODULES

void *unwind_add_table(struct module *mod, const void *table_start,
		unsigned long table_size);

void unwind_remove_table(void *handle, bool init_only);

#endif

asmlinkage void arch_unwind_init_running(struct pt_regs *regs);

/*
 * Unwind to previous to frame.  Returns 0 if successful, negative
 * number in case of an error.
 */
int unwind(struct unwind_state *state);

/*
 * Unwind until the return pointer is in user-land (or until an error
 * occurs).  Returns 0 if successful, negative number in case of
 * error.
 */
int unwind_to_user(struct unwind_state *state);

#else /* CONFIG_STACK_UNWIND */

struct unwind_state {};

static inline void unwind_init(void) {}
static inline void unwind_setup(void) {}

#ifdef CONFIG_MODULES

static inline void *unwind_add_table(struct module *mod,
				     const void *table_start,
				     unsigned long table_size)
{
	return NULL;
}

#endif

static inline void unwind_remove_table(void *handle, bool init_only)
{
}

static inline int unwind(struct unwind_state *info)
{
	return -ENODEV;
}

static inline int unwind_to_user(struct unwind_state *info)
{
	return -ENODEV;
}

#endif /* CONFIG_STACK_UNWIND */

#endif /* _LINUX_UNWIND_H */
