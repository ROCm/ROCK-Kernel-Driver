/*
 * Standard kernel function entry points for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sourceforge.net)
 * Contributions from SGI, IBM, HP, MCL, and others.
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2000 - 2002 TurboLinux, Inc.  All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 Free Software Foundation, Inc. All rights reserved.
 *
 * This code is released under version 2 of the GNU GPL.
 */

/*
 * -----------------------------------------------------------------------
 *
 * DUMP HISTORY
 *
 * This dump code goes back to SGI's first attempts at dumping system
 * memory on SGI systems running IRIX.  A few developers at SGI needed
 * a way to take this system dump and analyze it, and created 'icrash',
 * or IRIX Crash.  The mechanism (the dumps and 'icrash') were used
 * by support people to generate crash reports when a system failure
 * occurred.  This was vital for large system configurations that
 * couldn't apply patch after patch after fix just to hope that the
 * problems would go away.  So the system memory, along with the crash
 * dump analyzer, allowed support people to quickly figure out what the
 * problem was on the system with the crash dump.
 *
 * In comes Linux.  SGI started moving towards the open source community,
 * and upon doing so, SGI wanted to take its support utilities into Linux
 * with the hopes that they would end up the in kernel and user space to
 * be used by SGI's customers buying SGI Linux systems.  One of the first
 * few products to be open sourced by SGI was LKCD, or Linux Kernel Crash
 * Dumps.  LKCD comprises of a patch to the kernel to enable system
 * dumping, along with 'lcrash', or Linux Crash, to analyze the system
 * memory dump.  A few additional system scripts and kernel modifications
 * are also included to make the dump mechanism and dump data easier to
 * process and use.
 *
 * As soon as LKCD was released into the open source community, a number
 * of larger companies started to take advantage of it.  Today, there are
 * many community members that contribute to LKCD, and it continues to
 * flourish and grow as an open source project.
 */

/*
 * DUMP TUNABLES
 *
 * This is the list of system tunables (via /proc) that are available
 * for Linux systems.  All the read, write, etc., functions are listed
 * here.  Currently, there are a few different tunables for dumps:
 *
 * dump_device (used to be dumpdev):
 *     The device for dumping the memory pages out to.  This 
 *     may be set to the primary swap partition for disruptive dumps,
 *     and must be an unused partition for non-disruptive dumps.
 *     Todo: In the case of network dumps, this may be interpreted 
 *     as the IP address of the netdump server to connect to.
 *
 * dump_compress (used to be dump_compress_pages):
 *     This is the flag which indicates which compression mechanism
 *     to use.  This is a BITMASK, not an index (0,1,2,4,8,16,etc.).
 *     This is the current set of values:
 *
 *     0: DUMP_COMPRESS_NONE -- Don't compress any pages.
 *     1: DUMP_COMPRESS_RLE  -- This uses RLE compression.
 *     2: DUMP_COMPRESS_GZIP -- This uses GZIP compression.
 *
 * dump_level:
 *     The amount of effort the dump module should make to save
 *     information for post crash analysis.  This value is now
 *     a BITMASK value, not an index:
 *
 *     0:   Do nothing, no dumping. (DUMP_LEVEL_NONE)
 *
 *     1:   Print out the dump information to the dump header, and
 *          write it out to the dump_device. (DUMP_LEVEL_HEADER)
 *
 *     2:   Write out the dump header and all kernel memory pages.
 *          (DUMP_LEVEL_KERN)
 *
 *     4:   Write out the dump header and all kernel and user
 *          memory pages.  (DUMP_LEVEL_USED)
 *
 *     8:   Write out the dump header and all conventional/cached 
 *	    memory (RAM) pages in the system (kernel, user, free).  
 *	    (DUMP_LEVEL_ALL_RAM)
 *
 *    16:   Write out everything, including non-conventional memory
 *	    like firmware, proms, I/O registers, uncached memory.
 *	    (DUMP_LEVEL_ALL)
 *
 *     The dump_level will default to 1.
 *
 * dump_flags:
 *     These are the flags to use when talking about dumps.  There
 *     are lots of possibilities.  This is a BITMASK value, not an index.
 * 
 * -----------------------------------------------------------------------
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/dump.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>
#include "dump_methods.h"
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/highmem.h>
#include <linux/miscdevice.h>
#include <linux/sysrq.h>
#include <linux/sysctl.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/ltt.h>

#include <asm/hardirq.h>
#include <asm/uaccess.h>

/*
 * -----------------------------------------------------------------------
 *                         V A R I A B L E S
 * -----------------------------------------------------------------------
 */

/* Dump tunables */
struct dump_config dump_config = {
	.level 		= 0,
	.flags 		= 0,
	.dump_device	= 0,
	.dump_addr	= 0,
	.dumper 	= NULL
};
#ifdef CONFIG_ARM 
static _dump_regs_t all_regs;
#endif

/* Global variables used in dump.h */
/* degree of system freeze when dumping */
enum dump_silence_levels dump_silence_level = DUMP_HARD_SPIN_CPUS;	 

/* Other global fields */
extern struct __dump_header dump_header; 
struct dump_dev *dump_dev = NULL;  /* Active dump device                   */
static int dump_compress = 0;

static u16 dump_compress_none(const u8 *old, u16 oldsize, u8 *new, u16 newsize,
				unsigned long loc);
struct __dump_compress dump_none_compression = {
	.compress_type	= DUMP_COMPRESS_NONE,
	.compress_func	= dump_compress_none,
	.compress_name  = "none",
};

/* our device operations and functions */
static int dump_ioctl(struct inode *i, struct file *f,
	unsigned int cmd, unsigned long arg);

#ifdef CONFIG_COMPAT
static int dw_long(unsigned int, unsigned int, unsigned long, struct file*);
#endif

static struct file_operations dump_fops = {
	.owner	= THIS_MODULE,
	.ioctl	= dump_ioctl,
};

static struct miscdevice dump_miscdev = {
	.minor  = CRASH_DUMP_MINOR,
	.name   = "dump",
	.fops   = &dump_fops,
};
MODULE_ALIAS_MISCDEV(CRASH_DUMP_MINOR);

/* static variables							*/
static int dump_okay = 0;		/* can we dump out to disk?	*/
static spinlock_t dump_lock = SPIN_LOCK_UNLOCKED;

/* used for dump compressors */
static struct list_head dump_compress_list = LIST_HEAD_INIT(dump_compress_list);

/* list of registered dump targets */
static struct list_head dump_target_list = LIST_HEAD_INIT(dump_target_list);

/* lkcd info structure -- this is used by lcrash for basic system data     */
struct __lkcdinfo lkcdinfo = {
	.ptrsz		= (sizeof(void *) * 8),
#if defined(__LITTLE_ENDIAN) 
	.byte_order	= __LITTLE_ENDIAN,
#else
	.byte_order	= __BIG_ENDIAN,
#endif
	.page_shift	= PAGE_SHIFT,
	.page_size	= PAGE_SIZE,
	.page_mask	= PAGE_MASK,
	.page_offset	= PAGE_OFFSET,
};

/*
 * -----------------------------------------------------------------------
 *            / P R O C   T U N A B L E   F U N C T I O N S
 * -----------------------------------------------------------------------
 */

static int proc_dump_device(ctl_table *ctl, int write, struct file *f,
			    void *buffer, size_t *lenp);

static int proc_doulonghex(ctl_table *ctl, int write, struct file *f,
			    void *buffer, size_t *lenp);
/*
 * sysctl-tuning infrastructure.
 */
static ctl_table dump_table[] = {
	{ .ctl_name = CTL_DUMP_LEVEL,
	  .procname = DUMP_LEVEL_NAME, 
	  .data = &dump_config.level, 	 
	  .maxlen = sizeof(int),
	  .mode = 0644,
	  .proc_handler = proc_doulonghex, },

	{ .ctl_name = CTL_DUMP_FLAGS,
	  .procname = DUMP_FLAGS_NAME,
	  .data = &dump_config.flags,	
	  .maxlen = sizeof(int),
	  .mode = 0644,
	  .proc_handler = proc_doulonghex, },

	{ .ctl_name = CTL_DUMP_COMPRESS,
	  .procname = DUMP_COMPRESS_NAME,
	  .data = &dump_compress, /* FIXME */
	  .maxlen = sizeof(int),
	  .mode = 0644,
	  .proc_handler = proc_dointvec, },
	  
	{ .ctl_name = CTL_DUMP_DEVICE,
	  .procname = DUMP_DEVICE_NAME,
	  .mode = 0644,
	  .data = &dump_config.dump_device, /* FIXME */
	  .maxlen = sizeof(int),
	  .proc_handler = proc_dump_device },

#ifdef CONFIG_CRASH_DUMP_MEMDEV
	{ .ctl_name = CTL_DUMP_ADDR,
	  .procname = DUMP_ADDR_NAME,
	  .mode = 0444,
	  .data = &dump_config.dump_addr,
	  .maxlen = sizeof(unsigned long),
	  .proc_handler = proc_doulonghex },
#endif

	{ 0, }
};

static ctl_table dump_root[] = {
	{ .ctl_name = KERN_DUMP,
	  .procname = "dump",
	  .mode = 0555, 
	  .child = dump_table },
	{ 0, }
};

static ctl_table kernel_root[] = {
	{ .ctl_name = CTL_KERN,
	  .procname = "kernel",
	  .mode = 0555,
	  .child = dump_root, },
	{ 0, }
};

static struct ctl_table_header *sysctl_header;

/*
 * -----------------------------------------------------------------------
 *              C O M P R E S S I O N   F U N C T I O N S
 * -----------------------------------------------------------------------
 */

/*
 * Name: dump_compress_none()
 * Func: Don't do any compression, period.
 */
static u16
dump_compress_none(const u8 *old, u16 oldsize, u8 *new, u16 newsize,
		unsigned long loc)
{
	/* just return the old size */
	return oldsize;
}


/*
 * Name: dump_execute()
 * Func: Execute the dumping process.  This makes sure all the appropriate
 *       fields are updated correctly, and calls dump_execute_memdump(),
 *       which does the real work.
 */
void
dump_execute(const char *panic_str, const struct pt_regs *regs)
{
	int state = -1;
	unsigned long flags;

	/* make sure we can dump */
	if (!dump_okay) {
		pr_info("LKCD not yet configured, can't take dump now\n");
		return;
	}

	/* Exclude multiple dumps at the same time,
	 * and disable interrupts,  some drivers may re-enable
	 * interrupts in with silence()
	 *
	 * Try and acquire spin lock. If successful, leave preempt
	 * and interrupts disabled.  See spin_lock_irqsave in spinlock.h
	 */
	local_irq_save(flags);
	if (!spin_trylock(&dump_lock)) {
		local_irq_restore(flags);
		pr_info("LKCD dump already in progress\n");
		return;
	}

	ltt_flight_pause();
	
	/* Bring system into the strictest level of quiescing for min drift 
	 * dump drivers can soften this as required in dev->ops->silence() 
	 */
	dump_oncpu = smp_processor_id() + 1;
	dump_silence_level = DUMP_HARD_SPIN_CPUS; 

	state = dump_generic_execute(panic_str, regs);
	
	dump_oncpu = 0;
	spin_unlock_irqrestore(&dump_lock, flags);

	if (state < 0) {
		printk("Dump Incomplete or failed!\n");
	} else {
		printk("Dump Complete; %d dump pages saved.\n", 
		       dump_header.dh_num_dump_pages);
	}
	
	ltt_flight_unpause();
}

/*
 * Name: dump_register_compression()
 * Func: Register a dump compression mechanism.
 */
void
dump_register_compression(struct __dump_compress *item)
{
	if (item)
		list_add(&(item->list), &dump_compress_list);
}

/*
 * Name: dump_unregister_compression()
 * Func: Remove a dump compression mechanism, and re-assign the dump
 *       compression pointer if necessary.
 */
void
dump_unregister_compression(int compression_type)
{
	struct list_head *tmp;
	struct __dump_compress *dc;

	/* let's make sure our list is valid */
	if (compression_type != DUMP_COMPRESS_NONE) {
		list_for_each(tmp, &dump_compress_list) {
			dc = list_entry(tmp, struct __dump_compress, list);
			if (dc->compress_type == compression_type) {
				list_del(&(dc->list));
				break;
			}
		}
	}
}

/*
 * Name: dump_compress_init()
 * Func: Initialize (or re-initialize) compression scheme.
 */
static int
dump_compress_init(int compression_type)
{
	struct list_head *tmp;
	struct __dump_compress *dc;

	/* try to remove the compression item */
	list_for_each(tmp, &dump_compress_list) {
		dc = list_entry(tmp, struct __dump_compress, list);
		if (dc->compress_type == compression_type) {
			dump_config.dumper->compress = dc;
			dump_compress = compression_type;
 			pr_debug("Dump Compress %s\n", dc->compress_name);
			return 0;
		}
	}

	/* 
	 * nothing on the list -- return ENODATA to indicate an error 
	 *
	 * NB: 
	 *	EAGAIN: reports "Resource temporarily unavailable" which
	 *		isn't very enlightening.
	 */
	printk("compression_type:%d not found\n", compression_type);

	return -ENODATA;
}

static int
dumper_setup(unsigned long flags, unsigned long devid)
{
	int ret = 0;

	/* unconfigure old dumper if it exists */
	dump_okay = 0;
	if (dump_config.dumper) {
		pr_debug("Unconfiguring current dumper\n");
		dump_unconfigure();
	}
	/* set up new dumper */
	if (dump_config.flags & DUMP_FLAGS_SOFTBOOT) {
		printk("Configuring softboot based dump \n");
#ifdef CONFIG_CRASH_DUMP_MEMDEV
		dump_config.dumper = &dumper_stage1; 
#else
		printk("Requires CONFIG_CRASHDUMP_MEMDEV. Can't proceed.\n");
		return -1;
#endif
	} else {
		dump_config.dumper = &dumper_singlestage;
	}	
	dump_config.dumper->dev = dump_dev;

	ret = dump_configure(devid);
	if (!ret) {
		dump_okay = 1;
		pr_debug("%s dumper set up for dev 0x%lx\n", 
			dump_config.dumper->name, devid);
 		dump_config.dump_device = devid;
	} else {
		printk("%s dumper set up failed for dev 0x%lx\n", 
		       dump_config.dumper->name, devid);
 		dump_config.dumper = NULL;
	}
	return ret;
}

static int
dump_target_init(int target)
{
	char type[20];
	struct list_head *tmp;
	struct dump_dev *dev;
	
	switch (target) {
		case DUMP_FLAGS_DISKDUMP:
			strcpy(type, "blockdev"); break;
		case DUMP_FLAGS_NETDUMP:
			strcpy(type, "networkdev"); break;
		default:
			return -1;
	}

	/*
	 * This is a bit stupid, generating strings from flag
	 * and doing strcmp. This is done because 'struct dump_dev'
	 * has string 'type_name' and not interger 'type'.
	 */
	list_for_each(tmp, &dump_target_list) {
		dev = list_entry(tmp, struct dump_dev, list);
		if (strcmp(type, dev->type_name) == 0) {
			dump_dev = dev;
			return 0;
		}
	}
	return -1;
}

/*
 * Name: dump_ioctl()
 * Func: Allow all dump tunables through a standard ioctl() mechanism.
 *       This is far better than before, where we'd go through /proc,
 *       because now this will work for multiple OS and architectures.
 */
static int
dump_ioctl(struct inode *i, struct file *f, unsigned int cmd, unsigned long arg)
{
	/* check capabilities */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!dump_config.dumper && cmd == DIOSDUMPCOMPRESS)
		/* dump device must be configured first */
		return -ENODEV;

	/*
	 * This is the main mechanism for controlling get/set data
	 * for various dump device parameters.  The real trick here
	 * is setting the dump device (DIOSDUMPDEV).  That's what
	 * triggers everything else.
	 */
	switch (cmd) {
	case DIOSDUMPDEV:	/* set dump_device */
		pr_debug("Configuring dump device\n"); 
		if (!(f->f_flags & O_RDWR))
			return -EPERM;

		__dump_open();
		return dumper_setup(dump_config.flags, arg);

		
	case DIOGDUMPDEV:	/* get dump_device */
		return put_user((long)dump_config.dump_device, (long *)arg);

	case DIOSDUMPLEVEL:	/* set dump_level */
		if (!(f->f_flags & O_RDWR))
			return -EPERM;

		/* make sure we have a positive value */
		if (arg < 0)
			return -EINVAL;

		/* Fixme: clean this up */
		dump_config.level = 0;
		switch ((int)arg) {
			case DUMP_LEVEL_ALL:
			case DUMP_LEVEL_ALL_RAM:
				dump_config.level |= DUMP_MASK_UNUSED;
			case DUMP_LEVEL_USED:
				dump_config.level |= DUMP_MASK_USED;
			case DUMP_LEVEL_KERN:
				dump_config.level |= DUMP_MASK_KERN;
			case DUMP_LEVEL_HEADER:
				dump_config.level |= DUMP_MASK_HEADER;
			case DUMP_LEVEL_NONE:
				break;
			default:
				return (-EINVAL);
			}
		pr_debug("Dump Level 0x%lx\n", dump_config.level);
		break;

	case DIOGDUMPLEVEL:	/* get dump_level */
		/* fixme: handle conversion */
		return put_user((long)dump_config.level, (long *)arg);

		
	case DIOSDUMPFLAGS:	/* set dump_flags */
		/* check flags */
		if (!(f->f_flags & O_RDWR))
			return -EPERM;

		/* make sure we have a positive value */
		if (arg < 0)
			return -EINVAL;
			
		if (dump_target_init(arg & DUMP_FLAGS_TARGETMASK) < 0)
			return -EINVAL; /* return proper error */

		dump_config.flags = arg;
		
		pr_debug("Dump Flags 0x%lx\n", dump_config.flags);
		break;
		
	case DIOGDUMPFLAGS:	/* get dump_flags */
		return put_user((long)dump_config.flags, (long *)arg);

	case DIOSDUMPCOMPRESS:	/* set the dump_compress status */
		if (!(f->f_flags & O_RDWR))
			return -EPERM;

		return dump_compress_init((int)arg);

	case DIOGDUMPCOMPRESS:	/* get the dump_compress status */
		return put_user((long)(dump_config.dumper ? 
			dump_config.dumper->compress->compress_type : 0), 
			(long *)arg);
	case DIOGDUMPOKAY: /* check if dump is configured */
		return put_user((long)dump_okay, (long *)arg);
	
	case DIOSDUMPTAKE: /* Trigger a manual dump */
		/* Do not proceed if lkcd not yet configured */
		if(!dump_okay) {
			printk("LKCD not yet configured. Cannot take manual dump\n");
			return -ENODEV;
		}

		/* Take the dump */
		return	manual_handle_crashdump();
			
	default:
		/* 
		 * these are network dump specific ioctls, let the
		 * module handle them.
		 */
		return dump_dev_ioctl(cmd, arg);
	}
	return 0;
}

/*
 * Handle special cases for dump_device 
 * changing dump device requires doing an opening the device
 */
static int 
proc_dump_device(ctl_table *ctl, int write, struct file *f,
		 void *buffer, size_t *lenp)
{
	int *valp = ctl->data;
	int oval = *valp;
	int ret = -EPERM;

	/* same permission checks as ioctl */
	if (capable(CAP_SYS_ADMIN)) {
		ret = proc_doulonghex(ctl, write, f, buffer, lenp);
		if (ret == 0 && write && *valp != oval) {
			/* need to restore old value to close properly */
			dump_config.dump_device = (dev_t) oval;
			__dump_open();
			ret = dumper_setup(dump_config.flags, (dev_t) *valp);
		}
	}

	return ret;
}

/* All for the want of a proc_do_xxx routine which prints values in hex */
static int 
proc_doulonghex(ctl_table *ctl, int write, struct file *f,
		 void *buffer, size_t *lenp)
{
#define TMPBUFLEN 20
	unsigned long *i;
	size_t len, left;
	char buf[TMPBUFLEN];

	if (!ctl->data || !ctl->maxlen || !*lenp || (f->f_pos)) {
		*lenp = 0;
		return 0;
	}
	
	i = (unsigned long *) ctl->data;
	left = *lenp;
	
	sprintf(buf, "0x%lx\n", (*i));
	len = strlen(buf);
	if (len > left)
		len = left;
	if(copy_to_user(buffer, buf, len))
		return -EFAULT;
	
	left -= len;
	*lenp -= left;
	f->f_pos += *lenp;
	return 0;
}

/*
 * -----------------------------------------------------------------------
 *                     I N I T   F U N C T I O N S
 * -----------------------------------------------------------------------
 */

#ifdef CONFIG_COMPAT
static int dw_long(unsigned int fd, unsigned int cmd, unsigned long arg,
                struct file *f)
{
        mm_segment_t old_fs = get_fs();
        int err;
        unsigned long val;

        set_fs (KERNEL_DS);
        err = sys_ioctl(fd, cmd, (u64)&val);
        set_fs (old_fs);
        if (!err && put_user((unsigned int) val, (u32 *)arg))
               return -EFAULT;
        return err;
}
#endif

/*
 * These register and unregister routines are exported for modules
 * to register their dump drivers (like block, net etc)
 */
int
dump_register_device(struct dump_dev *ddev)
{
	struct list_head *tmp;
	struct dump_dev *dev;

	list_for_each(tmp, &dump_target_list) {
		dev = list_entry(tmp, struct dump_dev, list);
		if (strcmp(ddev->type_name, dev->type_name) == 0) {
			printk("Target type %s already registered\n",
					dev->type_name);
			return -1; /* return proper error */
		}
	}
	list_add(&(ddev->list), &dump_target_list);
	
	return 0;
}

void
dump_unregister_device(struct dump_dev *ddev)
{
	list_del(&(ddev->list));
	if (ddev != dump_dev)
		return;

	dump_okay = 0;

	if (dump_config.dumper)
		dump_unconfigure();

	dump_config.flags &= ~DUMP_FLAGS_TARGETMASK;
	dump_okay = 0;
	dump_dev = NULL;
	dump_config.dumper = NULL;
}

static int panic_event(struct notifier_block *this, unsigned long event,
		       void *ptr)
{
#ifdef CONFIG_ARM
	get_current_general_regs(&all_regs);
	get_current_cp14_regs(&all_regs);
	get_current_cp15_regs(&all_regs);
	dump_execute((const char *)ptr, &all_regs);
#else
	struct pt_regs regs;
	
	get_current_regs(&regs);
	dump_execute((const char *)ptr, &regs);
#endif
	return 0;
}

extern struct notifier_block *panic_notifier_list;
static int panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block panic_block = {
	.notifier_call = panic_event,
};

#ifdef CONFIG_MAGIC_SYSRQ
/* Sysrq handler */
static void sysrq_handle_crashdump(int key, struct pt_regs *pt_regs,
		struct tty_struct *tty) {
	if(!pt_regs) {
		struct pt_regs regs;
		get_current_regs(&regs);
		dump_execute("sysrq", &regs);

	} else {
		dump_execute("sysrq", pt_regs);
	}
}

static struct sysrq_key_op sysrq_crashdump_op = {
	.handler	=	sysrq_handle_crashdump,
	.help_msg	=	"Dump",
	.action_msg	=	"Starting crash dump",
};
#endif

static inline void
dump_sysrq_register(void) 
{
#ifdef CONFIG_MAGIC_SYSRQ
	__sysrq_lock_table();
	__sysrq_put_key_op(DUMP_SYSRQ_KEY, &sysrq_crashdump_op);
	__sysrq_unlock_table();
#endif
}

static inline void
dump_sysrq_unregister(void)
{
#ifdef CONFIG_MAGIC_SYSRQ
	__sysrq_lock_table();
	if (__sysrq_get_key_op(DUMP_SYSRQ_KEY) == &sysrq_crashdump_op)
		__sysrq_put_key_op(DUMP_SYSRQ_KEY, NULL);
	__sysrq_unlock_table();
#endif
}

/*
 * Name: dump_init()
 * Func: Initialize the dump process.  This will set up any architecture
 *       dependent code.  The big key is we need the memory offsets before
 *       the page table is initialized, because the base memory offset
 *       is changed after paging_init() is called.
 */
static int __init
dump_init(void)
{
	struct sysinfo info;
	int err;

	/* try to create our dump device */
	err = misc_register(&dump_miscdev);
	if (err) {
		printk("cannot register dump character device!\n");
		return err;
	}

	__dump_init((u64)PAGE_OFFSET);

#ifdef CONFIG_COMPAT
       err = register_ioctl32_conversion(DIOSDUMPDEV, NULL);
       err |= register_ioctl32_conversion(DIOGDUMPDEV, NULL);
       err |= register_ioctl32_conversion(DIOSDUMPLEVEL, NULL);
       err |= register_ioctl32_conversion(DIOGDUMPLEVEL, dw_long);
       err |= register_ioctl32_conversion(DIOSDUMPFLAGS, NULL);
       err |= register_ioctl32_conversion(DIOGDUMPFLAGS, dw_long);
       err |= register_ioctl32_conversion(DIOSDUMPCOMPRESS, NULL);
       err |= register_ioctl32_conversion(DIOGDUMPCOMPRESS, dw_long);
       err |= register_ioctl32_conversion(DIOSTARGETIP, NULL);
       err |= register_ioctl32_conversion(DIOGTARGETIP, NULL);
       err |= register_ioctl32_conversion(DIOSTARGETPORT, NULL);
       err |= register_ioctl32_conversion(DIOGTARGETPORT, NULL);
       err |= register_ioctl32_conversion(DIOSSOURCEPORT, NULL);
       err |= register_ioctl32_conversion(DIOGSOURCEPORT, NULL);
       err |= register_ioctl32_conversion(DIOSETHADDR, NULL);
       err |= register_ioctl32_conversion(DIOGETHADDR, NULL);
       err |= register_ioctl32_conversion(DIOGDUMPOKAY, dw_long);
       err |= register_ioctl32_conversion(DIOSDUMPTAKE, NULL);
       if (err) {
                printk(KERN_ERR "LKCD: registering ioctl32 translations failed\
");
       }
#endif
	/* set the dump_compression_list structure up */
	dump_register_compression(&dump_none_compression);

	/* grab the total memory size now (not if/when we crash) */
	si_meminfo(&info);

	/* set the memory size */
	dump_header.dh_memory_size = (u64)info.totalram;

	sysctl_header = register_sysctl_table(kernel_root, 0);
	dump_sysrq_register();

	notifier_chain_register(&panic_notifier_list, &panic_block);
	dump_function_ptr = dump_execute;

	pr_info("Crash dump driver initialized.\n");
	return 0;
}

static void __exit
dump_cleanup(void)
{
	int err;
	dump_okay = 0;

	if (dump_config.dumper)
		dump_unconfigure();

	/* arch-specific cleanup routine */
	__dump_cleanup();

#ifdef CONFIG_COMPAT
	err = unregister_ioctl32_conversion(DIOSDUMPDEV);
	err |= unregister_ioctl32_conversion(DIOGDUMPDEV);
	err |= unregister_ioctl32_conversion(DIOSDUMPLEVEL);
	err |= unregister_ioctl32_conversion(DIOGDUMPLEVEL);
	err |= unregister_ioctl32_conversion(DIOSDUMPFLAGS);
	err |= unregister_ioctl32_conversion(DIOGDUMPFLAGS);
	err |= unregister_ioctl32_conversion(DIOSDUMPCOMPRESS);
	err |= unregister_ioctl32_conversion(DIOGDUMPCOMPRESS);
	err |= unregister_ioctl32_conversion(DIOSTARGETIP);
	err |= unregister_ioctl32_conversion(DIOGTARGETIP);
	err |= unregister_ioctl32_conversion(DIOSTARGETPORT);
	err |= unregister_ioctl32_conversion(DIOGTARGETPORT);
	err |= unregister_ioctl32_conversion(DIOSSOURCEPORT);
	err |= unregister_ioctl32_conversion(DIOGSOURCEPORT);
	err |= unregister_ioctl32_conversion(DIOSETHADDR);
	err |= unregister_ioctl32_conversion(DIOGETHADDR);
	err |= unregister_ioctl32_conversion(DIOGDUMPOKAY);
	err |= unregister_ioctl32_conversion(DIOSDUMPTAKE);
	if (err) {
		printk(KERN_ERR "LKCD: Unregistering ioctl32 translations failed\n");
	}
#endif

	/* ignore errors while unregistering -- since can't do anything */
	unregister_sysctl_table(sysctl_header);
	misc_deregister(&dump_miscdev);
	dump_sysrq_unregister();
	notifier_chain_unregister(&panic_notifier_list, &panic_block);
	dump_function_ptr = NULL;
}

EXPORT_SYMBOL(dump_register_compression);
EXPORT_SYMBOL(dump_unregister_compression);
EXPORT_SYMBOL(dump_register_device);
EXPORT_SYMBOL(dump_unregister_device);
EXPORT_SYMBOL(dump_config);
EXPORT_SYMBOL(dump_silence_level);

EXPORT_SYMBOL(__dump_irq_enable);
EXPORT_SYMBOL(__dump_irq_restore);

MODULE_AUTHOR("Matt D. Robinson <yakker@sourceforge.net>");
MODULE_DESCRIPTION("Linux Kernel Crash Dump (LKCD) driver");
MODULE_LICENSE("GPL");

module_init(dump_init);
module_exit(dump_cleanup);
