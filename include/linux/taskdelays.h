#ifndef _LINUX_TASKDELAYS_H
#define _LINUX_TASKDELAYS_H

#include <linux/config.h>

struct task_delay_info {
#ifdef CONFIG_DELAY_ACCT
        /* delay statistics in usecs */
	unsigned long runs;
	unsigned long waitcpu_total;
	unsigned long runcpu_total;
	unsigned long iowait_total;
	unsigned long mem_iowait_total;
	unsigned long num_iowaits;
	unsigned long num_memwaits;
#endif
};

#endif // _LINUX_TASKDELAYS_H

