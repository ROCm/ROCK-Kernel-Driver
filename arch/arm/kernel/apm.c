/*
 * bios-less APM driver for ARM Linux 
 *  Jamey Hicks <jamey@crl.dec.com>
 *  adapted from the APM BIOS driver for Linux by Stephen Rothwell (sfr@linuxcare.com)
 *
 * APM 1.2 Reference:
 *   Intel Corporation, Microsoft Corporation. Advanced Power Management
 *   (APM) BIOS Interface Specification, Revision 1.2, February 1996.
 *
 * [This document is available from Microsoft at:
 *    http://www.microsoft.com/hwdev/busbios/amp_12.htm]
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/apm_bios.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/completion.h>

#include <asm/apm.h> /* apm_power_info */
#include <asm/system.h>

/*
 * The apm_bios device is one of the misc char devices.
 * This is its minor number.
 */
#define APM_MINOR_DEV	134

/*
 * See Documentation/Config.help for the configuration options.
 *
 * Various options can be changed at boot time as follows:
 * (We allow underscores for compatibility with the modules code)
 *	apm=on/off			enable/disable APM
 */

/*
 * Maximum number of events stored
 */
#define APM_MAX_EVENTS		20

/*
 * The per-file APM data
 */
struct apm_user {
	struct list_head	list;

	int			suser: 1;
	int			writer: 1;
	int			reader: 1;
	int			suspend_wait: 1;
	int			suspend_result;

	int			suspends_pending;
	int			standbys_pending;
	unsigned int		suspends_read;
	unsigned int		standbys_read;

	int			event_head;
	int			event_tail;
	apm_event_t		events[APM_MAX_EVENTS];
};

/*
 * Local variables
 */
static int suspends_pending;
static int standbys_pending;
static int apm_disabled;

static DECLARE_WAIT_QUEUE_HEAD(apm_waitqueue);
static DECLARE_WAIT_QUEUE_HEAD(apm_suspend_waitqueue);

/*
 * This is a list of everyone who has opened /dev/apm_bios
 */
static spinlock_t user_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(apm_user_list);

/*
 * The kapmd info.
 */
static struct task_struct *kapmd;
static DECLARE_COMPLETION(kapmd_exit);

static const char driver_version[] = "1.13";	/* no spaces */



/*
 * Compatibility cruft until the IPAQ people move over to the new
 * interface.
 */
static void __apm_get_power_status(struct apm_power_info *info)
{
#if 0 && defined(CONFIG_SA1100_H3600) && defined(CONFIG_TOUCHSCREEN_H3600)
	extern int h3600_apm_get_power_status(u_char *, u_char *, u_char *,
					      u_char *, u_short *);

	if (machine_is_h3600()) {
		int dx;
		h3600_apm_get_power_status(&info->ac_line_status,
				&info->battery_status, &info->battery_flag,
				&info->battery_life, &dx);
		info->time = dx & 0x7fff;
		info->units = dx & 0x8000 ? 0 : 1;
	}
#endif
}

/*
 * This allows machines to provide their own "apm get power status" function.
 */
void (*apm_get_power_status)(struct apm_power_info *) = __apm_get_power_status;
EXPORT_SYMBOL(apm_get_power_status);

static int queue_empty(struct apm_user *as)
{
	return as->event_head == as->event_tail;
}

static apm_event_t get_queued_event(struct apm_user *as)
{
	as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
	return as->events[as->event_tail];
}

static void queue_event_one_user(struct apm_user *as, apm_event_t event)
{
	as->event_head = (as->event_head + 1) % APM_MAX_EVENTS;
	if (as->event_head == as->event_tail) {
		static int notified;

		if (notified++ == 0)
		    printk(KERN_ERR "apm: an event queue overflowed\n");
		as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
	}
	as->events[as->event_head] = event;

	if (!as->suser || !as->writer)
		return;

	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		as->suspends_pending++;
		suspends_pending++;
		break;

	case APM_SYS_STANDBY:
	case APM_USER_STANDBY:
		as->standbys_pending++;
		standbys_pending++;
		break;
	}
}

static void queue_event(apm_event_t event, struct apm_user *sender)
{
	struct list_head *l;

	spin_lock(&user_list_lock);
	list_for_each(l, &apm_user_list) {
		struct apm_user *as = list_entry(l, struct apm_user, list);

		if (as != sender && as->reader)
			queue_event_one_user(as, event);
	}
	spin_unlock(&user_list_lock);
	wake_up_interruptible(&apm_waitqueue);
}

static int apm_suspend(void)
{
	struct list_head *l;
	int err = pm_suspend(PM_SUSPEND_MEM);

	/*
	 * Anyone on the APM queues will think we're still suspended.
	 * Send a message so everyone knows we're now awake again.
	 */
	queue_event(APM_NORMAL_RESUME, NULL);

	/*
	 * Finally, wake up anyone who is sleeping on the suspend.
	 */
	spin_lock(&user_list_lock);
	list_for_each(l, &apm_user_list) {
		struct apm_user *as = list_entry(l, struct apm_user, list);

		as->suspend_result = err;
		as->suspend_wait = 0;
	}
	spin_unlock(&user_list_lock);

	wake_up_interruptible(&apm_suspend_waitqueue);
	return err;
}

static ssize_t apm_read(struct file *fp, char __user *buf, size_t count, loff_t *ppos)
{
	struct apm_user *as = fp->private_data;
	apm_event_t event;
	int i = count, ret = 0, nonblock = fp->f_flags & O_NONBLOCK;

	if (count < sizeof(apm_event_t))
		return -EINVAL;

	if (queue_empty(as) && nonblock)
		return -EAGAIN;

	wait_event_interruptible(apm_waitqueue, !queue_empty(as));

	while ((i >= sizeof(event)) && !queue_empty(as)) {
		event = get_queued_event(as);
		printk("  apm_read: event=%d\n", event);

		ret = -EFAULT;
		if (copy_to_user(buf, &event, sizeof(event)))
			break;

		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_read++;
			break;

		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			as->standbys_read++;
			break;
		}

		buf += sizeof(event);
		i -= sizeof(event);
	}

	if (i < count)
		ret = count - i;

	return ret;
}

static unsigned int apm_poll(struct file *fp, poll_table * wait)
{
	struct apm_user * as = fp->private_data;

	poll_wait(fp, &apm_waitqueue, wait);
	return queue_empty(as) ? 0 : POLLIN | POLLRDNORM;
}

/*
 * apm_ioctl - handle APM ioctl
 *
 * APM_IOC_SUSPEND
 *   This IOCTL is overloaded, and performs two functions.  It is used to:
 *     - initiate a suspend
 *     - acknowledge a suspend read from /dev/apm_bios.
 *   Only when everyone who has opened /dev/apm_bios with write permission
 *   has acknowledge does the actual suspend happen.
 */
static int
apm_ioctl(struct inode * inode, struct file *filp, u_int cmd, u_long arg)
{
	struct apm_user *as = filp->private_data;
	int err = -EINVAL;

	if (!as->suser || !as->writer)
		return -EPERM;

	switch (cmd) {
	case APM_IOC_STANDBY:
		break;

	case APM_IOC_SUSPEND:
		/*
		 * If we read a suspend command from /dev/apm_bios,
		 * then the corresponding APM_IOC_SUSPEND ioctl is
		 * interpreted as an acknowledge.
		 */
		if (as->suspends_read > 0) {
			as->suspends_read--;
			as->suspends_pending--;
			suspends_pending--;
		} else {
			queue_event(APM_USER_SUSPEND, as);
		}

		/*
		 * If there are outstanding suspend requests for other
		 * people on /dev/apm_bios, we must sleep for them.
		 * Last one to bed turns the lights out.
		 */
		if (suspends_pending > 0) {
			as->suspend_wait = 1;
			err = wait_event_interruptible(apm_suspend_waitqueue,
						 as->suspend_wait == 0);
			if (err == 0)
				err = as->suspend_result;
		} else {			
			err = apm_suspend();
		}
		break;
	}

	return err;
}

static int apm_release(struct inode * inode, struct file * filp)
{
	struct apm_user *as = filp->private_data;
	filp->private_data = NULL;

	spin_lock(&user_list_lock);
	list_del(&as->list);
	spin_unlock(&user_list_lock);

	/*
	 * We are now unhooked from the chain.  As far as new
	 * events are concerned, we no longer exist.  However, we
	 * need to balance standbys_pending and suspends_pending,
	 * which means the possibility of sleeping.
	 */
	if (as->standbys_pending > 0) {
		standbys_pending -= as->standbys_pending;
//		if (standbys_pending <= 0)
//			standby();
	}
	if (as->suspends_pending > 0) {
		suspends_pending -= as->suspends_pending;
		if (suspends_pending <= 0)
			apm_suspend();
	}

	kfree(as);
	return 0;
}

static int apm_open(struct inode * inode, struct file * filp)
{
	struct apm_user *as;

	as = (struct apm_user *)kmalloc(sizeof(*as), GFP_KERNEL);
	if (as) {
		memset(as, 0, sizeof(*as));

		/*
		 * XXX - this is a tiny bit broken, when we consider BSD
		 * process accounting. If the device is opened by root, we
		 * instantly flag that we used superuser privs. Who knows,
		 * we might close the device immediately without doing a
		 * privileged operation -- cevans
		 */
		as->suser = capable(CAP_SYS_ADMIN);
		as->writer = (filp->f_mode & FMODE_WRITE) == FMODE_WRITE;
		as->reader = (filp->f_mode & FMODE_READ) == FMODE_READ;

		spin_lock(&user_list_lock);
		list_add(&as->list, &apm_user_list);
		spin_unlock(&user_list_lock);

		filp->private_data = as;
	}

	return as ? 0 : -ENOMEM;
}

static struct file_operations apm_bios_fops = {
	.owner		= THIS_MODULE,
	.read		= apm_read,
	.poll		= apm_poll,
	.ioctl		= apm_ioctl,
	.open		= apm_open,
	.release	= apm_release,
};

static struct miscdevice apm_device = {
	.minor		= APM_MINOR_DEV,
	.name		= "apm_bios",
	.fops		= &apm_bios_fops
};


#ifdef CONFIG_PROC_FS
/*
 * Arguments, with symbols from linux/apm_bios.h.
 *
 *   0) Linux driver version (this will change if format changes)
 *   1) APM BIOS Version.  Usually 1.0, 1.1 or 1.2.
 *   2) APM flags from APM Installation Check (0x00):
 *	bit 0: APM_16_BIT_SUPPORT
 *	bit 1: APM_32_BIT_SUPPORT
 *	bit 2: APM_IDLE_SLOWS_CLOCK
 *	bit 3: APM_BIOS_DISABLED
 *	bit 4: APM_BIOS_DISENGAGED
 *   3) AC line status
 *	0x00: Off-line
 *	0x01: On-line
 *	0x02: On backup power (BIOS >= 1.1 only)
 *	0xff: Unknown
 *   4) Battery status
 *	0x00: High
 *	0x01: Low
 *	0x02: Critical
 *	0x03: Charging
 *	0x04: Selected battery not present (BIOS >= 1.2 only)
 *	0xff: Unknown
 *   5) Battery flag
 *	bit 0: High
 *	bit 1: Low
 *	bit 2: Critical
 *	bit 3: Charging
 *	bit 7: No system battery
 *	0xff: Unknown
 *   6) Remaining battery life (percentage of charge):
 *	0-100: valid
 *	-1: Unknown
 *   7) Remaining battery life (time units):
 *	Number of remaining minutes or seconds
 *	-1: Unknown
 *   8) min = minutes; sec = seconds
 */
static int apm_get_info(char *buf, char **start, off_t fpos, int length)
{
	struct apm_power_info info;
	char *units;
	int ret;

	info.ac_line_status = 0xff;
	info.battery_status = 0xff;
	info.battery_flag   = 0xff;
	info.battery_life   = 255;
	info.time	    = -1;
	info.units	    = -1;

	if (apm_get_power_status)
		apm_get_power_status(&info);

	switch (info.units) {
	default:	units = "?";	break;
	case 0: 	units = "min";	break;
	case 1: 	units = "sec";	break;
	}

	ret = sprintf(buf, "%s 1.2 0x%02x 0x%02x 0x%02x 0x%02x %d%% %d %s\n",
		     driver_version, APM_32_BIT_SUPPORT,
		     info.ac_line_status, info.battery_status,
		     info.battery_flag, info.battery_life,
		     info.time, units);

 	return ret;
}
#endif

#if 0
static int kapmd(void *startup)
{
	struct task_struct *tsk = current;

	daemonize();
	strcpy(tsk->comm, "kapmd");
	kapmd = tsk;

	spin_lock_irq(&tsk->sigmask_lock);
	siginitsetinv(&tsk->blocked, sigmask(SIGQUIT));
	recalc_sigpending(tsk);
	spin_unlock_irq(&tsk->sigmask_lock);

	complete((struct completion *)startup);

	do {
		set_task_state(tsk, TASK_INTERRUPTIBLE);
		schedule();
	} while (!signal_pending(tsk));

	complete_and_exit(&kapmd_exit, 0);
}
#endif

static int __init apm_init(void)
{
//	struct completion startup = COMPLETION_INITIALIZER(startup);
	int ret;

	if (apm_disabled) {
		printk(KERN_NOTICE "apm: disabled on user request.\n");
		return -ENODEV;
	}

	if (PM_IS_ACTIVE()) {
		printk(KERN_NOTICE "apm: overridden by ACPI.\n");
		return -EINVAL;
	}

//	ret = kernel_thread(kapmd, &startup, CLONE_FS | CLONE_FILES);
//	if (ret)
//		return ret;
//	wait_for_completion(&startup);

	pm_active = 1;

#ifdef CONFIG_PROC_FS
	create_proc_info_entry("apm", 0, NULL, apm_get_info);
#endif

	ret = misc_register(&apm_device);
	if (ret != 0) {
		pm_active = 0;
		remove_proc_entry("apm", NULL);
		send_sig(SIGQUIT, kapmd, 1);
		wait_for_completion(&kapmd_exit);
	}

	return ret;
}

static void __exit apm_exit(void)
{
	misc_deregister(&apm_device);
	remove_proc_entry("apm", NULL);
	pm_active = 0;
//	send_sig(SIGQUIT, kapmd, 1);
//	wait_for_completion(&kapmd_exit);
}

module_init(apm_init);
module_exit(apm_exit);

MODULE_AUTHOR("Stephen Rothwell");
MODULE_DESCRIPTION("Advanced Power Management");
MODULE_LICENSE("GPL");

#ifndef MODULE
static int __init apm_setup(char *str)
{
	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			apm_disabled = 1;
		if (strncmp(str, "on", 2) == 0)
			apm_disabled = 0;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("apm=", apm_setup);
#endif
