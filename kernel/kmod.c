/*
	kmod, the new module loader (replaces kerneld)
	Kirk Petersen

	Reorganized not to be a daemon by Adam Richter, with guidance
	from Greg Zornetzer.

	Modified to avoid chroot and file sharing problems.
	Mikael Pettersson

	Limit the concurrent number of kmod modprobes to catch loops from
	"modprobe needs a service that is in a module".
	Keith Owens <kaos@ocs.com.au> December 1999

	Unblock all signals when we exec a usermode process.
	Shuu Yamaguchi <shuu@wondernetworkresources.com> December 2000

	call_usermodehelper wait flag, and remove exec_usermodehelper.
	Rusty Russell <rusty@rustcorp.com.au>  Jan 2003
*/
#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/kmod.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/namespace.h>
#include <linux/completion.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>

extern int max_threads;

#ifdef CONFIG_KMOD

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[256] = "/sbin/modprobe";

/**
 * request_module - try to load a kernel module
 * @fmt:     printf style format string for the name of the module
 * @varargs: arguements as specified in the format string
 *
 * Load a module using the user mode module loader. The function returns
 * zero on success or a negative errno code on failure. Note that a
 * successful module load does not mean the module did not then unload
 * and exit on an error of its own. Callers must check that the service
 * they requested is now available not blindly invoke it.
 *
 * If module auto-loading support is disabled then this function
 * becomes a no-operation.
 */
int request_module(const char *fmt, ...)
{
	va_list args;
	char module_name[MODULE_NAME_LEN];
	unsigned int max_modprobes;
	int ret;
	char *argv[] = { modprobe_path, "-q", "--", module_name, NULL };
	static char *envp[] = { "HOME=/",
				"TERM=linux",
				"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
				NULL };
	static atomic_t kmod_concurrent = ATOMIC_INIT(0);
#define MAX_KMOD_CONCURRENT 50	/* Completely arbitrary value - KAO */
	static int kmod_loop_msg;

	va_start(args, fmt);
	ret = vsnprintf(module_name, MODULE_NAME_LEN, fmt, args);
	va_end(args);
	if (ret >= MODULE_NAME_LEN)
		return -ENAMETOOLONG;

	/* If modprobe needs a service that is in a module, we get a recursive
	 * loop.  Limit the number of running kmod threads to max_threads/2 or
	 * MAX_KMOD_CONCURRENT, whichever is the smaller.  A cleaner method
	 * would be to run the parents of this process, counting how many times
	 * kmod was invoked.  That would mean accessing the internals of the
	 * process tables to get the command line, proc_pid_cmdline is static
	 * and it is not worth changing the proc code just to handle this case. 
	 * KAO.
	 *
	 * "trace the ppid" is simple, but will fail if someone's
	 * parent exits.  I think this is as good as it gets. --RR
	 */
	max_modprobes = min(max_threads/2, MAX_KMOD_CONCURRENT);
	atomic_inc(&kmod_concurrent);
	if (atomic_read(&kmod_concurrent) > max_modprobes) {
		/* We may be blaming an innocent here, but unlikely */
		if (kmod_loop_msg++ < 5)
			printk(KERN_ERR
			       "request_module: runaway loop modprobe %s\n",
			       module_name);
		atomic_dec(&kmod_concurrent);
		return -ENOMEM;
	}

	ret = call_usermodehelper(modprobe_path, argv, envp, 1);
	if (ret != 0) {
		static unsigned long last;
		unsigned long now = jiffies;
		if (now - last > HZ) {
			last = now;
			printk(KERN_DEBUG
			       "request_module: failed %s -- %s. error = %d\n",
			       modprobe_path, module_name, ret);
		}
	}
	atomic_dec(&kmod_concurrent);
	return ret;
}
#endif /* CONFIG_KMOD */

#ifdef CONFIG_HOTPLUG
/*
	hotplug path is set via /proc/sys
	invoked by hotplug-aware bus drivers,
	with call_usermodehelper

	argv [0] = hotplug_path;
	argv [1] = "usb", "scsi", "pci", "network", etc;
	... plus optional type-specific parameters
	argv [n] = 0;

	envp [*] = HOME, PATH; optional type-specific parameters

	a hotplug bus should invoke this for device add/remove
	events.  the command is expected to load drivers when
	necessary, and may perform additional system setup.
*/
char hotplug_path[256] = "/sbin/hotplug";

EXPORT_SYMBOL(hotplug_path);

#endif /* CONFIG_HOTPLUG */

struct subprocess_info {
	struct completion *complete;
	char *path;
	char **argv;
	char **envp;
	int wait;
	int retval;
};

/*
 * This is the task which runs the usermode application
 */
static int ____call_usermodehelper(void *data)
{
	struct subprocess_info *sub_info = data;
	int retval;

	/* Unblock all signals. */
	flush_signals(current);
	spin_lock_irq(&current->sighand->siglock);
	flush_signal_handlers(current, 1);
	sigemptyset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	retval = -EPERM;
	if (current->fs->root)
		retval = execve(sub_info->path, sub_info->argv,sub_info->envp);

	/* Exec failed? */
	sub_info->retval = retval;
	do_exit(0);
}

/* Keventd can't block, but this (a child) can. */
static int wait_for_helper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;

	sub_info->retval = 0;
	pid = kernel_thread(____call_usermodehelper, sub_info, SIGCHLD);
	if (pid < 0)
		sub_info->retval = pid;
	else
		/* We don't have a SIGCHLD signal handler, so this
		 * always returns -ECHILD, but the important thing is
		 * that it blocks. */
		sys_wait4(pid, NULL, 0, NULL);

	complete(sub_info->complete);
	return 0;
}

/*
 * This is run by keventd.
 */
static void __call_usermodehelper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;

	/* CLONE_VFORK: wait until the usermode helper has execve'd
	 * successfully We need the data structures to stay around
	 * until that is done.  */
	if (sub_info->wait)
		pid = kernel_thread(wait_for_helper, sub_info,
				    CLONE_KERNEL | SIGCHLD);
	else
		pid = kernel_thread(____call_usermodehelper, sub_info,
				    CLONE_VFORK | SIGCHLD);

	if (pid < 0) {
		sub_info->retval = pid;
		complete(sub_info->complete);
	} else if (!sub_info->wait)
		complete(sub_info->complete);
}

/**
 * call_usermodehelper - start a usermode application
 * @path: pathname for the application
 * @argv: null-terminated argument list
 * @envp: null-terminated environment list
 * @wait: wait for the application to finish and return status.
 *
 * Runs a user-space application.  The application is started
 * asynchronously if wait is not set, and runs as a child of keventd.
 * (ie. it runs with full root capabilities).
 *
 * Must be called from process context.  Returns a negative error code
 * if program was not execed successfully, or 0.
 */
int call_usermodehelper(char *path, char **argv, char **envp, int wait)
{
	DECLARE_COMPLETION(done);
	struct subprocess_info sub_info = {
		.complete	= &done,
		.path		= path,
		.argv		= argv,
		.envp		= envp,
		.wait		= wait,
		.retval		= 0,
	};
	DECLARE_WORK(work, __call_usermodehelper, &sub_info);

	if (!system_running)
		return -EBUSY;

	if (path[0] == '\0')
		goto out;

	if (current_is_keventd()) {
		/* We can't wait on keventd! */
		__call_usermodehelper(&sub_info);
	} else {
		schedule_work(&work);
		wait_for_completion(&done);
	}
out:
	return sub_info.retval;
}

EXPORT_SYMBOL(call_usermodehelper);

#ifdef CONFIG_KMOD
EXPORT_SYMBOL(request_module);
#endif

