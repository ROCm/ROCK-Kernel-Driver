/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/uaccess.h"

extern int modify_ldt(int func, void *ptr, unsigned long bytecount);

int sys_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
	if(verify_area(VERIFY_READ, ptr, bytecount)) return(-EFAULT);
	return(modify_ldt(func, ptr, bytecount));
}

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
