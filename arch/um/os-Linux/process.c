/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include "os.h"
#include "user.h"

unsigned long os_process_pc(int pid)
{
	char proc_stat[sizeof("/proc/#####/stat\0")], buf[256];
	unsigned long pc;
	int fd;

	sprintf(proc_stat, "/proc/%d/stat", pid);
	fd = os_open_file(proc_stat, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("os_process_pc - couldn't open '%s', errno = %d\n", 
		       proc_stat, errno);
		return(-1);
	}
	if(read(fd, buf, sizeof(buf)) < 0){
		printk("os_process_pc - couldn't read '%s', errno = %d\n", 
		       proc_stat, errno);
		close(fd);
		return(-1);
	}
	close(fd);
	pc = -1;
	if(sscanf(buf, "%*d %*s %*c %*d %*d %*d %*d %*d %*d %*d %*d "
		  "%*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
		  "%*d %*d %*d %*d %ld", &pc) != 1){
		printk("os_process_pc - couldn't find pc in '%s'\n", buf);
	}
	return(pc);
}

int os_process_parent(int pid)
{
	char stat[sizeof("/proc/nnnnn/stat\0")];
	char data[256];
	int parent, n, fd;

	if(pid == -1) return(-1);

	snprintf(stat, sizeof(stat), "/proc/%d/stat", pid);
	fd = os_open_file(stat, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("Couldn't open '%s', errno = %d\n", stat, -fd);
		return(-1);
	}

	n = read(fd, data, sizeof(data));
	close(fd);

	if(n < 0){
		printk("Couldn't read '%s', errno = %d\n", stat);
		return(-1);
	}

	parent = -1;
	/* XXX This will break if there is a space in the command */
	n = sscanf(data, "%*d %*s %*c %d", &parent);
	if(n != 1) printk("Failed to scan '%s'\n", data);

	return(parent);
}

void os_stop_process(int pid)
{
	kill(pid, SIGSTOP);
}

void os_kill_process(int pid)
{
	kill(pid, SIGKILL);
}

void os_usr1_process(int pid)
{
	kill(pid, SIGUSR1);
}

int os_getpid(void)
{
	return(getpid());
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
