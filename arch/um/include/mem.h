/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __MEM_H__
#define __MEM_H__

struct vm_reserved {
	struct list_head list;
	unsigned long start;
	unsigned long end;
};

extern void set_usable_vm(unsigned long start, unsigned long end);
extern void set_kmem_end(unsigned long new);

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
