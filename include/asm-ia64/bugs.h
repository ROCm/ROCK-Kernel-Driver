/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 *
 * Copyright (C) 1998, 1999, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef _ASM_IA64_BUGS_H
#define _ASM_IA64_BUGS_H

#include <asm/processor.h>

extern void check_bugs (void);

#endif /* _ASM_IA64_BUGS_H */
