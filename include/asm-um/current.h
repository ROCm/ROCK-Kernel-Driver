/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_CURRENT_H
#define __UM_CURRENT_H

#ifndef __ASSEMBLY__

struct thread_info;

#include "linux/config.h"
#include "asm/page.h"

#define CURRENT_THREAD(dummy) (((unsigned long) &dummy) & \
			        (PAGE_MASK << CONFIG_KERNEL_STACK_ORDER))

#define current ({ int dummy; \
                   ((struct thread_info *) CURRENT_THREAD(dummy))->task; })

#endif /* __ASSEMBLY__ */

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
