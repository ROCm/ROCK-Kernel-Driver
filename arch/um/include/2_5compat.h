/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __2_5_COMPAT_H__
#define __2_5_COMPAT_H__

#include "linux/version.h"

#define INIT_CONSOLE(dev_name, write_proc, device_proc, setup_proc, f) { \
	name :		dev_name, \
	write :		write_proc, \
	read :		NULL, \
	device :	device_proc, \
	setup :		setup_proc, \
	flags :		f, \
	index :		-1, \
	cflag :		0, \
	next :		NULL \
}

#define INIT_HARDSECT(arr, maj, sizes)

#define SET_PRI(task) do ; while(0)

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
