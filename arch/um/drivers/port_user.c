/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "chan_user.h"
#include "port.h"
#include "helper.h"
#include "os.h"

struct port_chan {
	int raw;
	struct termios tt;
	void *kernel_data;
	char dev[sizeof("32768\0")];
};

void *port_init(char *str, int device, struct chan_opts *opts)
{
	struct port_chan *data;
	void *kern_data;
	char *end;
	int port;

	if(*str != ':'){
		printk("port_init : channel type 'port' must specify a "
		       "port number\n");
		return(NULL);
	}
	str++;
	port = strtoul(str, &end, 0);
	if((*end != '\0') || (end == str)){
		printk("port_init : couldn't parse port '%s'\n", str);
		return(NULL);
	}

	if((kern_data = port_data(port)) == NULL) 
		return(NULL);

	if((data = um_kmalloc(sizeof(*data))) == NULL) 
		goto err;

	*data = ((struct port_chan) { .raw  		= opts->raw,
				      .kernel_data 	= kern_data });
	sprintf(data->dev, "%d", port);

	return(data);
 err:
	port_kern_free(kern_data);
	return(NULL);
}

void port_free(void *d)
{
	struct port_chan *data = d;

	port_kern_free(data->kernel_data);
	kfree(data);
}

int port_open(int input, int output, int primary, void *d, char **dev_out)
{
	struct port_chan *data = d;
	int fd;

	fd = port_wait(data->kernel_data);
	if((fd >= 0) && data->raw){
		tcgetattr(fd, &data->tt);
		raw(fd, 0);
	}
	*dev_out = data->dev;
	return(fd);
}

void port_close(int fd, void *d)
{
	struct port_chan *data = d;

	port_remove_dev(data->kernel_data);
	close(fd);
}

int port_console_write(int fd, const char *buf, int n, void *d)
{
	struct port_chan *data = d;

	return(generic_console_write(fd, buf, n, &data->tt));
}

struct chan_ops port_ops = {
	.type		= "port",
	.init		= port_init,
	.open		= port_open,
	.close		= port_close,
	.read	        = generic_read,
	.write		= generic_write,
	.console_write	= port_console_write,
	.window_size	= generic_window_size,
	.free		= port_free,
	.winch		= 1,
};

int port_listen_fd(int port)
{
	struct sockaddr_in addr;
	int fd, err;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if(fd == -1) 
		return(-errno);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0){
		err = -errno;
		goto out;
	}
  
	if((listen(fd, 1) < 0) || (os_set_fd_block(fd, 0))){
		err = -errno;
		goto out;
	}

	return(fd);
 out:
	os_close_file(fd);
	return(err);
}

struct port_pre_exec_data {
	int sock_fd;
	int pipe_fd;
};

void port_pre_exec(void *arg)
{
	struct port_pre_exec_data *data = arg;

	dup2(data->sock_fd, 0);
	dup2(data->sock_fd, 1);
	dup2(data->sock_fd, 2);
	close(data->sock_fd);
	dup2(data->pipe_fd, 3);
	os_shutdown_socket(3, 1, 0);
	close(data->pipe_fd);
}

int port_connection(int fd, int *socket, int *pid_out)
{
	int new, err;
	char *argv[] = { "/usr/sbin/in.telnetd", "-L", 
			 "/usr/lib/uml/port-helper", NULL };
	struct port_pre_exec_data data;

	if((new = os_accept_connection(fd)) < 0)
		return(-errno);

	err = os_pipe(socket, 0, 0);
	if(err) 
		goto out_close;

	data = ((struct port_pre_exec_data)
		{ .sock_fd  		= new,
		  .pipe_fd 		= socket[1] });

	err = run_helper(port_pre_exec, &data, argv, NULL);
	if(err < 0) 
		goto out_shutdown;

	*pid_out = err;
	return(new);

 out_shutdown:
	os_shutdown_socket(socket[0], 1, 1);
	close(socket[0]);
	os_shutdown_socket(socket[1], 1, 1);	
	close(socket[1]);
 out_close:
	close(new);
	return(err);
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
