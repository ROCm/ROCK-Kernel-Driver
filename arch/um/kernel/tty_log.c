/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com) and 
 * geoffrey hing <ghing@net.ohio-state.edu>
 * Licensed under the GPL
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include "init.h"
#include "user.h"
#include "os.h"

#define TTY_LOG_DIR "./"

/* Set early in boot and then unchanged */
static char *tty_log_dir = TTY_LOG_DIR;
static int tty_log_fd = -1;

#define TTY_LOG_OPEN 1
#define TTY_LOG_CLOSE 2
#define TTY_LOG_WRITE 3

struct tty_log_buf {
	int what;
	unsigned long tty;
	int len;
};

int open_tty_log(void *tty)
{
	struct timeval tv;
	struct tty_log_buf data;
	char buf[strlen(tty_log_dir) + sizeof("01234567890-01234567\0")];
	int fd;

	if(tty_log_fd != -1){
		data = ((struct tty_log_buf) { what :	TTY_LOG_OPEN,
					       tty : (unsigned long) tty,
					       len : 0 });
		write(tty_log_fd, &data, sizeof(data));
		return(tty_log_fd);
	}

	gettimeofday(&tv, NULL);
	sprintf(buf, "%s/%0u-%0u", tty_log_dir, (unsigned int) tv.tv_sec, 
 		(unsigned int) tv.tv_usec);

	fd = os_open_file(buf, of_append(of_create(of_rdwr(OPENFLAGS()))),
			  0644);
	if(fd < 0){
		printk("open_tty_log : couldn't open '%s', errno = %d\n",
		       buf, -fd);
	}
	return(fd);
}

void close_tty_log(int fd, void *tty)
{
	struct tty_log_buf data;

	if(tty_log_fd != -1){
		data = ((struct tty_log_buf) { what :	TTY_LOG_CLOSE,
					       tty : (unsigned long) tty,
					       len : 0 });
		write(tty_log_fd, &data, sizeof(data));
		return;
	}
	close(fd);
}

int write_tty_log(int fd, char *buf, int len, void *tty)
{
	struct tty_log_buf data;

	if(fd == tty_log_fd){
		data = ((struct tty_log_buf) { what :	TTY_LOG_WRITE,
					       tty : (unsigned long) tty,
					       len : len });
		write(tty_log_fd, &data, sizeof(data));
	}
	return(write(fd, buf, len));
}

static int __init set_tty_log_dir(char *name, int *add)
{
	tty_log_dir = name;
	return 0;
}

__uml_setup("tty_log_dir=", set_tty_log_dir,
"tty_log_dir=<directory>\n"
"    This is used to specify the directory where the logs of all pty\n"
"    data from this UML machine will be written.\n\n"
);

static int __init set_tty_log_fd(char *name, int *add)
{
	char *end;

	tty_log_fd = strtoul(name, &end, 0);
	if((*end != '\0') || (end == name)){
		printk("set_tty_log_fd - strtoul failed on '%s'\n", name);
		tty_log_fd = -1;
	}
	return 0;
}

__uml_setup("tty_log_fd=", set_tty_log_fd,
"tty_log_fd=<fd>\n"
"    This is used to specify a preconfigured file descriptor to which all\n"
"    tty data will be written.  Preconfigure the descriptor with something\n"
"    like '10>tty_log tty_log_fd=10'.\n\n"
);


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
