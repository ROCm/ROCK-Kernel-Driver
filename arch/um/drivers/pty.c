/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include "chan_user.h"
#include "user.h"
#include "user_util.h"
#include "kern_util.h"

struct pty_chan {
	void (*announce)(char *dev_name, int dev);
	int dev;
	int raw;
	struct termios tt;
	char dev_name[sizeof("/dev/pts/0123456\0")];
};

void *pty_chan_init(char *str, int device, struct chan_opts *opts)
{
	struct pty_chan *data;

	if((data = um_kmalloc(sizeof(*data))) == NULL) return(NULL);
	*data = ((struct pty_chan) { .announce  	= opts->announce, 
				     .dev  		= device,
				     .raw  		= opts->raw });
	return(data);
}

int pts_open(int input, int output, int primary, void *d, char **dev_out)
{
	struct pty_chan *data = d;
	char *dev;
	int fd;

	if((fd = get_pty()) < 0){
		printk("open_pts : Failed to open pts\n");
		return(-errno);
	}
	if(data->raw){
		tcgetattr(fd, &data->tt);
		raw(fd, 0);
	}

	dev = ptsname(fd);
	sprintf(data->dev_name, "%s", dev);
	*dev_out = data->dev_name;
	if(data->announce) (*data->announce)(dev, data->dev);
	return(fd);
}

int getmaster(char *line)
{
	struct stat stb;
	char *pty, *bank, *cp;
	int master;

	pty = &line[strlen("/dev/ptyp")];
	for (bank = "pqrs"; *bank; bank++) {
		line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		if (stat(line, &stb) < 0)
			break;
		for (cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			master = open(line, O_RDWR);
			if (master >= 0) {
				char *tp = &line[strlen("/dev/")];
				int ok;

				/* verify slave side is usable */
				*tp = 't';
				ok = access(line, R_OK|W_OK) == 0;
				*tp = 'p';
				if (ok) return(master);
				(void) close(master);
			}
		}
	}
	return(-1);
}

int pty_open(int input, int output, int primary, void *d, char **dev_out)
{
	struct pty_chan *data = d;
	int fd;
	char dev[sizeof("/dev/ptyxx\0")] = "/dev/ptyxx";

	fd = getmaster(dev);
	if(fd < 0) return(-errno);
	
	if(data->raw) raw(fd, 0);
	if(data->announce) (*data->announce)(dev, data->dev);

	sprintf(data->dev_name, "%s", dev);
	*dev_out = data->dev_name;
	return(fd);
}

int pty_console_write(int fd, const char *buf, int n, void *d)
{
	struct pty_chan *data = d;

	return(generic_console_write(fd, buf, n, &data->tt));
}

struct chan_ops pty_ops = {
	.type		= "pty",
	.init		= pty_chan_init,
	.open		= pty_open,
	.close		= generic_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= pty_console_write,
	.window_size	= generic_window_size,
	.free		= generic_free,
	.winch		= 0,
};

struct chan_ops pts_ops = {
	.type		= "pts",
	.init		= pty_chan_init,
	.open		= pts_open,
	.close		= generic_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= pty_console_write,
	.window_size	= generic_window_size,
	.free		= generic_free,
	.winch		= 0,
};

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
