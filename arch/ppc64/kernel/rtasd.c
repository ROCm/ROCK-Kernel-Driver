/*
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Communication to userspace based on kernel/printk.c
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/rtas.h>
#include <asm/prom.h>

#if 0
#define DEBUG(A...)	printk(KERN_ERR A)
#else
#define DEBUG(A...)
#endif

static spinlock_t rtas_log_lock = SPIN_LOCK_UNLOCKED;

DECLARE_WAIT_QUEUE_HEAD(rtas_log_wait);

#define LOG_NUMBER		64		/* must be a power of two */
#define LOG_NUMBER_MASK		(LOG_NUMBER-1)

static char *rtas_log_buf;
static unsigned long rtas_log_start;
static unsigned long rtas_log_size;

static int surveillance_requested;
static unsigned int rtas_event_scan_rate;
static unsigned int rtas_error_log_max;

#define EVENT_SCAN_ALL_EVENTS	0xf0000000
#define SURVEILLANCE_TOKEN	9000
#define SURVEILLANCE_TIMEOUT	1
#define SURVEILLANCE_SCANRATE	1

struct proc_dir_entry *proc_rtas;

/*
 * Since we use 32 bit RTAS, the physical address of this must be below
 * 4G or else bad things happen. Allocate this in the kernel data and
 * make it big enough.
 */
#define RTAS_ERROR_LOG_MAX 1024
static unsigned char logdata[RTAS_ERROR_LOG_MAX];

static int rtas_log_open(struct inode * inode, struct file * file)
{
	return 0;
}

static int rtas_log_release(struct inode * inode, struct file * file)
{
	return 0;
}

static ssize_t rtas_log_read(struct file * file, char * buf,
			 size_t count, loff_t *ppos)
{
	int error;
	char *tmp;
	unsigned long offset;

	if (!buf || count < rtas_error_log_max)
		return -EINVAL;

	count = rtas_error_log_max;

	error = verify_area(VERIFY_WRITE, buf, count);
	if (error)
		return -EINVAL;

	tmp = kmalloc(rtas_error_log_max, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	error = wait_event_interruptible(rtas_log_wait, rtas_log_size);
	if (error)
		goto out;

	spin_lock(&rtas_log_lock);
	offset = rtas_error_log_max * (rtas_log_start & LOG_NUMBER_MASK);
	memcpy(tmp, &rtas_log_buf[offset], count);
	rtas_log_start += 1;
	rtas_log_size -= 1;
	spin_unlock(&rtas_log_lock);

	error = copy_to_user(buf, tmp, count) ? -EFAULT : count;
out:
	kfree(tmp);
	return error;
}

static unsigned int rtas_log_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &rtas_log_wait, wait);
	if (rtas_log_size)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations proc_rtas_log_operations = {
	.read =		rtas_log_read,
	.poll =		rtas_log_poll,
	.open =		rtas_log_open,
	.release =	rtas_log_release,
};

static void log_rtas(char *buf)
{
	unsigned long offset;

	DEBUG("logging rtas event\n");

	spin_lock(&rtas_log_lock);

	offset = rtas_error_log_max *
			((rtas_log_start+rtas_log_size) & LOG_NUMBER_MASK);

	memcpy(&rtas_log_buf[offset], buf, rtas_error_log_max);

	if (rtas_log_size < LOG_NUMBER)
		rtas_log_size += 1;
	else
		rtas_log_start += 1;

	spin_unlock(&rtas_log_lock);
	wake_up_interruptible(&rtas_log_wait);
}

static int enable_surveillance(void)
{
	int error;

	error = rtas_call(rtas_token("set-indicator"), 3, 1, NULL,
			  SURVEILLANCE_TOKEN, 0, SURVEILLANCE_TIMEOUT);

	if (error) {
		printk(KERN_ERR "rtasd: could not enable surveillance\n");
		return -1;
	}

	rtas_event_scan_rate = SURVEILLANCE_SCANRATE;

	return 0;
}

static int get_eventscan_parms(void)
{
	struct device_node *node;
	int *ip;

	node = find_path_device("/rtas");

	ip = (int *)get_property(node, "rtas-event-scan-rate", NULL);
	if (ip == NULL) {
		printk(KERN_ERR "rtasd: no rtas-event-scan-rate\n");
		return -1;
	}
	rtas_event_scan_rate = *ip;
	DEBUG("rtas-event-scan-rate %d\n", rtas_event_scan_rate);

	ip = (int *)get_property(node, "rtas-error-log-max", NULL);
	if (ip == NULL) {
		printk(KERN_ERR "rtasd: no rtas-error-log-max\n");
		return -1;
	}
	rtas_error_log_max = *ip;
	DEBUG("rtas-error-log-max %d\n", rtas_error_log_max);

	if (rtas_error_log_max > RTAS_ERROR_LOG_MAX) {
		printk(KERN_ERR "rtasd: truncated error log from %d to %d bytes\n", rtas_error_log_max, RTAS_ERROR_LOG_MAX);
		rtas_error_log_max = RTAS_ERROR_LOG_MAX;
	}

	return 0;
}

extern long sys_sched_get_priority_max(int policy);

static int rtasd(void *unused)
{
	int cpu = 0;
	int error;
	int first_pass = 1;
	int event_scan = rtas_token("event-scan");

	if (event_scan == RTAS_UNKNOWN_SERVICE || get_eventscan_parms() == -1)
		goto error;

	rtas_log_buf = vmalloc(rtas_error_log_max*LOG_NUMBER);
	if (!rtas_log_buf) {
		printk(KERN_ERR "rtasd: no memory\n");
		goto error;
	}

	DEBUG("will sleep for %d jiffies\n", (HZ*60/rtas_event_scan_rate) / 2);

	daemonize("rtasd");

#if 0
	/* Rusty unreal time task */
	current->policy = SCHED_FIFO;
	current->nice = sys_sched_get_priority_max(SCHED_FIFO) + 1;
#endif

repeat:
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;

		DEBUG("scheduling on %d\n", cpu);
		set_cpus_allowed(current, cpumask_of_cpu(cpu));
		DEBUG("watchdog scheduled on cpu %d\n", smp_processor_id());

		do {
			memset(logdata, 0, rtas_error_log_max);
			error = rtas_call(event_scan, 4, 1, NULL,
					EVENT_SCAN_ALL_EVENTS, 0,
					__pa(logdata), rtas_error_log_max);
			if (error == -1) {
				printk(KERN_ERR "event-scan failed\n");
				break;
			}

			if (error == 0)
				log_rtas(logdata);

		} while(error == 0);

		/*
		 * Check all cpus for pending events quickly, sleeping for
		 * at least one second since some machines have problems
		 * if we call event-scan too quickly
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(first_pass ? HZ : (HZ*60/rtas_event_scan_rate) / 2);
	}

	if (first_pass && surveillance_requested) {
		DEBUG("enabling surveillance\n");
		if (enable_surveillance())
			goto error_vfree;
		DEBUG("surveillance enabled\n");
	}

	first_pass = 0;
	goto repeat;

error_vfree:
	vfree(rtas_log_buf);
error:
	/* Should delete proc entries */
	return -EINVAL;
}

static int __init rtas_init(void)
{
	struct proc_dir_entry *entry;

	if (proc_rtas == NULL) {
		proc_rtas = proc_mkdir("rtas", 0);
	}

	if (proc_rtas == NULL) {
		printk(KERN_ERR "Failed to create /proc/rtas in rtas_init\n");
	} else {
		entry = create_proc_entry("error_log", S_IRUSR, proc_rtas);
		if (entry)
			entry->proc_fops = &proc_rtas_log_operations;
		else
			printk(KERN_ERR "Failed to create rtas/error_log proc entry\n");
	}

	if (kernel_thread(rtasd, 0, CLONE_FS) < 0)
		printk(KERN_ERR "Failed to start RTAS daemon\n");

	printk(KERN_ERR "RTAS daemon started\n");

	return 0;
}

static int __init surveillance_setup(char *str)
{
	int i;

	if (get_option(&str,&i)) {
		if (i == 1)
			surveillance_requested = 1;
	}

	return 1;
}

__initcall(rtas_init);
__setup("surveillance=", surveillance_setup);
