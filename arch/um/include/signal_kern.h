/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SIGNAL_KERN_H__
#define __SIGNAL_KERN_H__

#include "sysdep/ptrace.h"

extern void signal_deliverer(int sig);
extern int probe_stack(unsigned long sp, int delta);
extern int have_signals(void *t);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
