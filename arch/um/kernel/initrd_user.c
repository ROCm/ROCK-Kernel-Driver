/*
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "initrd.h"
#include "os.h"

int load_initrd(char *filename, void *buf, int size)
{
	int fd, n;

	if((fd = os_open_file(filename, of_read(OPENFLAGS()), 0)) < 0){
		printk("Opening '%s' failed - errno = %d\n", filename, errno);
		return(-1);
	}
	if((n = read(fd, buf, size)) != size){
		printk("Read of %d bytes from '%s' returned %d, errno = %d\n",
		       size, filename, n, errno);
		return(-1);
	}
	return(0);
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
