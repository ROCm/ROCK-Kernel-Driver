/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/signal.h>
#include "kern_util.h"
#include "user.h"
#include "sysdep/ptrace.h"
#include "task.h"

#define MAXTOKEN 64

/* Set during early boot */
int cpu_has_cmov = 1;
int cpu_has_xmm = 0;

static char token(int fd, char *buf, int len, char stop)
{
	int n;
	char *ptr, *end, c;

	ptr = buf;
	end = &buf[len];
	do {
		n = read(fd, ptr, sizeof(*ptr));
		c = *ptr++;
		if(n == 0) return(0);
		else if(n != sizeof(*ptr)){
			printk("Reading /proc/cpuinfo failed, "
			       "errno = %d\n", errno);
			return(-errno);
		}
	} while((c != '\n') && (c != stop) && (ptr < end));

	if(ptr == end){
		printk("Failed to find '%c' in /proc/cpuinfo\n", stop);
		return(-1);
	}
	*(ptr - 1) = '\0';
	return(c);
}

static int check_cpu_feature(char *feature, int *have_it)
{
	char buf[MAXTOKEN], c;
	int fd, len = sizeof(buf)/sizeof(buf[0]), n;

	printk("Checking for host processor %s support...", feature);
	fd = open("/proc/cpuinfo", O_RDONLY);
	if(fd < 0){
		printk("Couldn't open /proc/cpuinfo, errno = %d\n", errno);
		return(0);
	}

	*have_it = 0;
	buf[len - 1] = '\0';
	while(1){
		c = token(fd, buf, len - 1, ':');
		if(c <= 0) goto out;
		else if(c != ':'){
			printk("Failed to find ':' in /proc/cpuinfo\n");
			goto out;
		}

		if(!strncmp(buf, "flags", strlen("flags"))) break;

		do {
			n = read(fd, &c, sizeof(c));
			if(n != sizeof(c)){
				printk("Failed to find newline in "
				       "/proc/cpuinfo, n = %d, errno = %d\n",
				       n, errno);
				goto out;
			}
		} while(c != '\n');
	}

	c = token(fd, buf, len - 1, ' ');
	if(c < 0) goto out;
	else if(c != ' '){
		printk("Failed to find ':' in /proc/cpuinfo\n");
		goto out;
	}

	while(1){
		c = token(fd, buf, len - 1, ' ');
		if(c < 0) goto out;
		else if(c == '\n') break;

		if(!strcmp(buf, feature)){
			*have_it = 1;
			goto out;
		}
	}
 out:
	if(*have_it == 0) printk("No\n");
	else if(*have_it == 1) printk("Yes\n");
	close(fd);
	return(1);
}

void arch_check_bugs(void)
{
	int have_it;

	if(access("/proc/cpuinfo", R_OK)){
		printk("/proc/cpuinfo not available - skipping CPU capability "
		       "checks\n");
		return;
	}
	if(check_cpu_feature("cmov", &have_it)) cpu_has_cmov = have_it;
	if(check_cpu_feature("xmm", &have_it)) cpu_has_xmm = have_it;
}

int arch_handle_signal(int sig, union uml_pt_regs *regs)
{
	unsigned long ip;

	/* This is testing for a cmov (0x0f 0x4x) instruction causing a
	 * SIGILL in init.
	 */
	if((sig != SIGILL) || (TASK_PID(get_current()) != 1)) return(0);

	ip = UPT_IP(regs);
	if((*((char *) ip) != 0x0f) || ((*((char *) (ip + 1)) & 0xf0) != 0x40))
		return(0);

	if(cpu_has_cmov == 0)
		panic("SIGILL caused by cmov, which this processor doesn't "
		      "implement, boot a filesystem compiled for older "
		      "processors");
	else if(cpu_has_cmov == 1)
		panic("SIGILL caused by cmov, which this processor claims to "
		      "implement");
	else if(cpu_has_cmov == -1)
		panic("SIGILL caused by cmov, couldn't tell if this processor "
		      "implements it, boot a filesystem compiled for older "
		      "processors");
	else panic("Bad value for cpu_has_cmov (%d)", cpu_has_cmov);
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
