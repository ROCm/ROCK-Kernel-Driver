#ifndef _LINUX_THREADS_H
#define _LINUX_THREADS_H

#include <linux/config.h>

/*
 * The default limit for the nr of threads is now in
 * /proc/sys/kernel/threads-max.
 */
 
#ifdef CONFIG_SMP
#define NR_CPUS	32		/* Max processors that can be running in SMP */
#else
#define NR_CPUS 1
#endif

#define MIN_THREADS_LEFT_FOR_ROOT 4

/*
 * This controls the default maximum pid allocated to a process
 */
#define PID_MAX_DEFAULT 0x8000

/*
 * A maximum of 4 million PIDs should be enough for a while:
 */
#define PID_MAX_LIMIT (4*1024*1024)

#endif
