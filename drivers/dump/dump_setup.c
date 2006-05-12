/*
 * Standard kernel function entry points for Linux crash dumps.
 *
 * Copyright (C) 1999 - 2005 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2004 - 2005 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2000 - 2002 TurboLinux, Inc.  All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 Free Software Foundation, Inc. All rights reserved.
 *
 * This code is released under version 2 of the GNU GPL.
 */

#include <linux/kernel.h>
#include <linux/dump.h>
#include <linux/sysrq.h>
#include <linux/reboot.h>

#include "dump_methods.h"


#define LKCD_VERSION 	"7.0.1"
#define LKCD_DATE	"2005-06-01"
#define DUMP_MAJOR 0	/* dynamic major by default */

MODULE_AUTHOR("LKCD development team <lkcd-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Linux Kernel Crash Dump (LKCD) driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(LKCD_VERSION);

/*
 * -----------------------------------------------------------------------
 *                         V A R I A B L E S
 * -----------------------------------------------------------------------
 */

/* Dump tunables */
struct dump_config dump_config = {
	.level 		= 0,
	.polling	= 1,
	.reboot		= 1,
	.comp_flag	= 0,
	.comp_val	= 0,
	.dump_device	= NULL,
	.dump_addr	= 0,
	.dumper 	= NULL
};

/* forward declorations */
static int dump_target_init(int);
static int dumper_setup(const char *);
static int dump_compress_init(int compression_type);

/* sysfs input protection semaphore */
static DECLARE_MUTEX(dump_sysfs_mutex);

/* degree of system freeze when dumping */
enum dump_silence_levels dump_silence_level = DUMP_HARD_SPIN_CPUS;

/* Other global fields */
extern struct __dump_header dump_header;
struct dump_dev *dump_dev = NULL;  /* Active dump device */
struct dump_dev_driver *dump_dev_driver = NULL;  /* acutall driver device */
static int dump_compress = 0;
int dump_compress_level = 1;

static u32 dump_compress_none(const u8 *old, u32 oldsize, u8 *new, u32 newsize,
		unsigned long loc);
struct __dump_compress dump_none_compression = {
	.compress_type	= DUMP_COMPRESS_NONE,
	.compress_func	= dump_compress_none,
	.compress_name  = "none",
};

/* static variables */
static int dump_okay = 0;
static spinlock_t dump_lock = SPIN_LOCK_UNLOCKED;

/* used for dump compressors */
static struct list_head dump_compress_list = LIST_HEAD_INIT(
		dump_compress_list);

/* list of registered dump targets */
static struct list_head dump_target_list = LIST_HEAD_INIT(dump_target_list);

/* lkcd info structure -- this is used by lcrash for basic system data */
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

struct dump_attribute {
	struct attribute	attr;
	ssize_t (*show) (struct dump_dev_driver *ddev, char *buf);
	ssize_t (*store)(struct dump_dev_driver *ddev, const char *buf,
			size_t count);
};

#define DUMPVAR_ATTR(_name, _mode, _show, _store) \
	struct dump_attribute dump_attr_##_name = { \
		.attr = {.name = __stringify(_name), \
			.mode = _mode, \
			.owner = THIS_MODULE}, \
		.show = _show, \
		.store = _store, \
	};

#define to_dump_attr(_attr) container_of(_attr,struct dump_attribute,attr)
#define to_dump_dev_driver(obj) container_of(obj,struct dump_dev_driver,kobj)

ssize_t dump_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct dump_dev_driver *dev = to_dump_dev_driver(kobj);
	struct dump_attribute *dump_attr = to_dump_attr(attr);
	ssize_t ret = 0;

	if (!dump_config.dumper)
		/* dump device must be configured first */
		return -ENODEV;

	down_interruptible(&dump_sysfs_mutex);
	if (dump_attr->show)
		ret = dump_attr->show(dev, buf);
	up(&dump_sysfs_mutex);

	return ret;
}

ssize_t dump_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t count)
{
	struct dump_dev_driver *dev = to_dump_dev_driver(kobj);
	struct dump_attribute *dump_attr = to_dump_attr(attr);
	ssize_t ret = 0;

	if (!dump_config.dumper)
		/* dump device must be configured first */
		return -ENODEV;

	if (dump_attr->store)
		ret = dump_attr->store(dev, buf, count);
	return ret;
}

static struct sysfs_ops dump_attr_ops = {
	.show = dump_attr_show,
	.store = dump_attr_store,
};

ssize_t show_version(struct dump_dev_driver *ddev, char *buf)
{
	return sprintf(buf, "%s\n%s\n", LKCD_VERSION, LKCD_DATE);
}

ssize_t show_polling(struct dump_dev_driver *ddev, char *buf)
{
	return sprintf(buf, "%d\n", dump_config.polling);
}

ssize_t store_polling(struct dump_dev_driver *ddev, const char *buf,
		size_t count)
{
	ulong tmp;

	if (buf == NULL)
		return -EINVAL;

	if ((sscanf(buf, "%lx", &tmp)) != 1)
		return -EINVAL;

	if ((tmp < 0) || (tmp > 1))
		return -EINVAL;

	dump_config.polling = tmp;

	/* If dump_device has already been initalized and we
	 * want to change the polling status we need to
	 * re-init dumpdev with the new polling value.
	 */
	if (dump_config.dump_device)
		dumper_setup(dump_config.dump_device);

	return count;
}

ssize_t show_dumpdev(struct dump_dev_driver *ddev, char *buf)
{
	return sprintf(buf, "%s\n", dump_config.dump_device);
}

ssize_t store_dumpdev(struct dump_dev_driver *ddev, const char *buf,
		size_t count)
{
	int err, type;
	printk("LKCD: Configuring dump device\n");

	if (buf == NULL)
		return -EINVAL;

	if ((strncmp(buf, "eth", 3) == 0) |
			(strncmp(buf, "ath", 3) == 0) |
			(strncmp(buf, "wlan", 4) == 0)){

		type = DUMP_TYPE_NETDEV;
	} else
				type = DUMP_TYPE_BLOCKDEV;

	if (dump_target_init(type) < 0)
		return -EINVAL;

	__dump_open();

	err = dumper_setup(buf);
	if (err)
		return -EINVAL;

	/* do we have a compress value that was set
	 * before we had a dump dump_dev that needs
	 * to be initalized?
	 */
	if (dump_config.comp_flag) {
		dump_compress_init((int)dump_config.comp_val);
		dump_config.comp_flag = 0;
	}

	return count;
}

ssize_t show_level(struct dump_dev_driver *ddev, char *buf)
{
	return sprintf(buf, "0x%lx\n", dump_config.level);
}

ssize_t store_level(struct dump_dev_driver *ddev, const char *buf,
		size_t count)
{
	ulong tmp;

	if (buf == NULL)
		return -EINVAL;

	if ( ( sscanf(buf, "%lx", &tmp)) != 1)
		return -EINVAL;

	if (tmp < 0)
		return -EINVAL;

	dump_config.level = 0;

	/* FIXME this is terrible and make it impossible for
	 * the user to see what they set. I'm leaving it only for
	 * the first rev and will fix this soon! -- troyh
	 */
	switch ((int)tmp){
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
	printk("LKCD: Dump Level 0x%lx\n", dump_config.level);

	return count;
}


ssize_t show_compress(struct dump_dev_driver *ddev, char *buf)
{

	if (dump_config.comp_flag)
		return sprintf(buf, "%d\n", dump_config.comp_val);
	else
		return sprintf(buf, "%d\n", dump_compress);
}

ssize_t store_compress(struct dump_dev_driver *ddev, const char *buf,
		size_t count)
{
	ulong tmp;

	if (buf == NULL)
		return -EINVAL;

	if ((sscanf(buf, "%lx", &tmp)) != 1)
		return -EINVAL;

	if ((tmp < 0) | (tmp > 2))
		return -EINVAL;

	/* dump_config.dump_device must valid first to establish
	 * the compression type. Will take the parameter now and
	 * delay the compress_init until we have a dump_device.
	 */
	if (dump_config.dump_device == 0){
		dump_config.comp_flag = 1;
		dump_config.comp_val = tmp;
	} else {
		dump_compress_init((int)tmp);
	}

	return count;
}

ssize_t show_compress_level(struct dump_dev_driver *ddev, char *buf)
{
	return sprintf(buf, "%d\n", dump_compress_level);
}

ssize_t store_compress_level(struct dump_dev_driver *ddev, const char *buf, size_t count)
{
	int tmp;

	if (buf == NULL)
		return -EINVAL;

	if (sscanf(buf, "%d", &tmp) != 1)
		return -EINVAL;

	/* Small kludge:  We allow levels 1-9 here because that's what
	 * makes sense for gzip, which is currently the only compression
	 * type to make use of multiple levels.
	 */
	if (tmp < 1 || tmp > 9)
		return -EINVAL;

	dump_compress_level = tmp;

	return count;
}

ssize_t show_reboot(struct dump_dev_driver *ddev, char *buf)
{
	return sprintf(buf, "%d\n", dump_config.reboot);
}

ssize_t store_reboot(struct dump_dev_driver *ddev, const char *buf,
		size_t count)
{
	ulong tmp;

	if (buf == NULL)
		return -EINVAL;

	if ((sscanf(buf, "%lx", &tmp)) != 1)
		return -EINVAL;

	if ((tmp < 0) | (tmp > 1))
		return -EINVAL;

	dump_config.reboot = tmp;

	return count;
}



static DUMPVAR_ATTR(polling, 0664, show_polling, store_polling);
static DUMPVAR_ATTR(dumpdev, 0664, show_dumpdev, store_dumpdev);
static DUMPVAR_ATTR(level, 0664, show_level, store_level);
static DUMPVAR_ATTR(compress, 0664, show_compress, store_compress);
static DUMPVAR_ATTR(compress_level, 0664, show_compress_level, store_compress_level);
static DUMPVAR_ATTR(reboot, 0664, show_reboot, store_reboot);
static DUMPVAR_ATTR(version, 0444, show_version, NULL);

/* These are default attributes for the dump device
 * There are none.
 */
static struct attribute *def_attrs[] = {
	NULL,
};

static struct dump_attribute *dump_attrs[] = {
	&dump_attr_polling,
	&dump_attr_dumpdev,
	&dump_attr_level,
	&dump_attr_compress,
	&dump_attr_compress_level,
	&dump_attr_reboot,
	&dump_attr_version,
	NULL,
};

/**
 *	dump_release - free dump structure
 *	@kobj:	kobject of dump structure
 *
 *	This is called when the refcount of the dump structure
 *	reaches 0. This should happen right after we unregister,
 *	but just in case, we use the release callback anyway.
 */

static void dump_release(struct kobject *kobj)
{
	struct dump_dev_driver *dev = to_dump_dev_driver(kobj);
	kfree(dev);
}

static struct kobj_type ktype_dump = {
	.release	= dump_release,
	.sysfs_ops	= &dump_attr_ops,
	.default_attrs	= def_attrs,
};

static decl_subsys(dump, &ktype_dump, NULL);


/*
 * -----------------------------------------------------------------------
 *              C O M P R E S S I O N   F U N C T I O N S
 * -----------------------------------------------------------------------
 */

/*
 * Name: dump_compress_none()
 * Func: Don't do any compression, period.
 */
	static u32
dump_compress_none(const u8 *old, u32 oldsize, u8 *new, u32 newsize,
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
void dump_execute(const char *panic_str, const struct pt_regs *regs)
{
	int state = -1;
	unsigned long flags;
	int loglevel_save = console_loglevel;

	if (console_loglevel < 6) /* KERN_INFO */
		console_loglevel = 6;

	/* make sure we can dump */
	if (!dump_okay) {
		pr_info("LKCD: not yet configured, can't take dump now\n");
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
		pr_info("LKCD: dump already in progress\n");
		return;
	}

	/* What state are interrupts really in? */
	if (in_interrupt()) {
		if(in_irq())
			printk("LKCD: Dumping from interrupt handler!\n");
		else
			printk("LKCD: Dumping from bottom half!\n");

		/*
		 * If we are not doing polling I/O then we should attempt
		 * to clean up the irq state.
		 *
		 * If polling I/O falls back to interrupt-driven mode then
		 * it will need to clean the IRQ state
		 */
		if (!(dump_config.polling))
			__dump_clean_irq_state();
	}

	/* Bring system into the strictest level of quiescing for min drift
	 * dump drivers can soften this as required in dev->ops->silence()
	 */
	dump_oncpu = smp_processor_id() + 1;
	dump_silence_level = DUMP_HARD_SPIN_CPUS;

	state = dump_generic_execute(panic_str, regs);

	dump_oncpu = 0;
	spin_unlock_irqrestore(&dump_lock, flags);

	if (state < 0) {
		printk("LKCD: Dump Incomplete or failed!\n");
	} else {
		printk("LKCD: Dump Complete; %d dump pages saved.\n",
				dump_header.dh_num_dump_pages);
	}

	console_loglevel = loglevel_save;

	if (dump_config.reboot)
		emergency_restart();

}

/*
 * Name: dump_register_compression()
 * Func: Register a dump compression mechanism.
 */
void dump_register_compression(struct __dump_compress *item)
{
	if (item)
		list_add(&(item->list), &dump_compress_list);
}

/*
 * Name: dump_unregister_compression()
 * Func: Remove a dump compression mechanism, and re-assign the dump
 *       compression pointer if necessary.
 */
void dump_unregister_compression(int compression_type)
{
	struct list_head *tmp;
	struct __dump_compress *dc;

	/* let's make sure our list is valid */
	if (compression_type == DUMP_COMPRESS_NONE)
		return;
	list_for_each(tmp, &dump_compress_list) {
		dc = list_entry(tmp, struct __dump_compress, list);
		if (dc->compress_type == compression_type) {
			/*
			 * If we're currently configured to dump using
			 * the compression mechanism we're removing,
			 * unconfigure ourselves.
			 */
			if (dump_config.dumper &&
					dump_config.dumper->compress == dc) {
				dump_okay = 0;
				dump_unconfigure();
				dump_dev = NULL;
				dump_config.dumper = NULL;
			}
			list_del(&(dc->list));
			break;
		}
	}
}

/*
 * Name: dump_compress_init()
 * Func: Initialize (or re-initialize) compression scheme.
 */
static int dump_compress_init(int compression_type)
{
	struct list_head *tmp;
	struct __dump_compress *dc;

	list_for_each(tmp, &dump_compress_list) {
		dc = list_entry(tmp, struct __dump_compress, list);
		if (dc->compress_type == compression_type) {
			dump_config.dumper->compress = dc;
			dump_compress = compression_type;
			printk("LKCD: %s compression initalized\n",
					dc->compress_name);
			return 0;
		}
	}

	printk("LKCD: compression_type:%d not found\n", compression_type);
	return -ENODATA;
}

static int dumper_setup(const char *devid)
{
	int ret = 0;

	/* unconfigure old dumper if it exists */
	dump_okay = 0;
	if (dump_config.dumper) {
		printk("LKCD: Unconfiguring current dumper\n");
		dump_unconfigure();
	}
	/* set up new dumper */
	dump_config.dumper = &dumper_singlestage;

	dump_config.dumper->dev = dump_dev;

	if (dump_config.dump_device != devid) {
		kfree(dump_config.dump_device);
		if (!(dump_config.dump_device = kstrdup(devid, GFP_KERNEL)))
			return -ENOMEM;
	}

	ret = dump_configure(devid);
	if (!ret) {
		dump_okay = 1;
		printk("LKCD: %s dumper set up for dev %s\n",
				dump_config.dumper->name,
				dump_config.dump_device);
	} else {
		printk("LKCD: %s dumper set up failed for dev %s\n",
				dump_config.dumper->name,
				dump_config.dump_device);
		dump_config.dumper = NULL;
	        dump_config.dump_device = NULL;
	}
	return ret;
}

static int dump_target_init(int type)
{
	struct list_head *tmp;
	struct dump_dev *dev;

	list_for_each(tmp, &dump_target_list) {
		dev = list_entry(tmp, struct dump_dev, list);
		if (type == dev->type) {
			dump_dev = dev;
			return 0;
		}
	}

	return -1;
}



/*
 * -----------------------------------------------------------------------
 *                     I N I T   F U N C T I O N S
 * -----------------------------------------------------------------------
 */

static void dump_populate_dir(struct dump_dev_driver * ddev)
{
	struct dump_attribute *attr;
	int err = 0, i;

	for (i = 0; (attr = dump_attrs[i]) && !err; i++) {
		if (attr->show)
			err = sysfs_create_file(&dump_subsys.kset.kobj,
					&attr->attr);
	}
}

/*
 * These register and unregister routines are exported for modules
 * to register their dump drivers (like block, net etc)
 */
int dump_register_device(struct dump_dev *ddev)
{
	struct list_head *tmp;
	struct dump_dev *dev;

	list_for_each(tmp, &dump_target_list) {
		dev = list_entry(tmp, struct dump_dev, list);
		if (ddev->type == dev->type) {
			printk("LKCD: Target type %d already registered\n",
					dev->type);
			return -1; /* return proper error */
		}
	}
	list_add(&(ddev->list), &dump_target_list);

	return 0;
}

void dump_unregister_device(struct dump_dev *ddev)
{
	list_del(&(ddev->list));
	if (ddev != dump_dev)
		return;

	dump_okay = 0;

	if (dump_config.dumper)
		dump_unconfigure();

	dump_okay = 0;
	dump_dev = NULL;
	dump_config.dumper = NULL;
}

static int panic_event(struct notifier_block *this, unsigned long event,
		void *ptr)
{
	struct pt_regs regs;

	get_current_regs(&regs);
	dump_execute((const char *)ptr, &regs);
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
	if (!pt_regs) {
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

static inline void dump_sysrq_register(void)
{
#ifdef CONFIG_MAGIC_SYSRQ
	register_sysrq_key(DUMP_SYSRQ_KEY, &sysrq_crashdump_op);
#endif
}

static inline void dump_sysrq_unregister(void)
{
#ifdef CONFIG_MAGIC_SYSRQ
	unregister_sysrq_key(DUMP_SYSRQ_KEY, &sysrq_crashdump_op);
#endif
}

/*
 * Name: dump_init()
 * Func: Initialize the dump process.  This will set up any architecture
 *       dependent code.
 */
static int __init dump_init(void)
{
	struct sysinfo info;
	int err;

	err = subsystem_register(&dump_subsys);
	if (err)
		return err;


	dump_dev_driver = kmalloc(sizeof(struct dump_dev_driver), GFP_KERNEL);
	if (!dump_dev_driver) {
		subsystem_unregister(&dump_subsys);
		return -ENOMEM;
	}

	memset(dump_dev_driver, 0, sizeof(struct dump_dev_driver));

	kobject_set_name(&dump_dev_driver->kobj, "dump_dev_driver");
	kobj_set_kset_s(dump_dev_driver, dump_subsys);

	/* initalize but do not register the kobject
	 * that represents the dump device, we only want
	 * if for refcount, the important attributes are
	 * assigned to the dump_subsys kobject anyway.
	 */
	kobject_init(&dump_dev_driver->kobj);

	dump_populate_dir(dump_dev_driver);

	__dump_init((u64) PAGE_OFFSET);

	/* set the dump_compression_list structure up */
	dump_register_compression(&dump_none_compression);

	/* grab the total memory size now (not if/when we crash) */
	si_meminfo(&info);

	/* set the memory size */
	dump_header.dh_memory_size = (u64) info.totalram;

	dump_sysrq_register();

	notifier_chain_register(&panic_notifier_list, &panic_block);
	dump_function_ptr = dump_execute;

	pr_info("LKCD: Crash dump driver initialized.\n");
	return 0;
}

static inline void dump_device_driver_unregister(struct dump_dev_driver *dev)
{
	kobject_unregister(&dev->kobj);
}

static void __exit dump_cleanup(void)
{
	subsystem_unregister(&dump_subsys);

	if (dump_dev_driver) {
		dump_device_driver_unregister(dump_dev_driver);
	}

	dump_okay = 0;

	if (dump_config.dumper)
		dump_unconfigure();

	/* arch-specific cleanup routine */
	__dump_cleanup();

	/* ignore errors while unregistering -- since can't do anything */
	dump_sysrq_unregister();
	notifier_chain_unregister(&panic_notifier_list, &panic_block);
	dump_function_ptr = NULL;

	pr_info("LKCD: Crash dump driver unloaded.\n");
}

EXPORT_SYMBOL_GPL(dump_register_compression);
EXPORT_SYMBOL_GPL(dump_unregister_compression);
EXPORT_SYMBOL_GPL(dump_register_device);
EXPORT_SYMBOL_GPL(dump_unregister_device);
EXPORT_SYMBOL_GPL(dump_config);
EXPORT_SYMBOL_GPL(dump_compress_level);
EXPORT_SYMBOL_GPL(dump_silence_level);
EXPORT_SYMBOL_GPL(__dump_irq_enable);
EXPORT_SYMBOL_GPL(__dump_irq_restore);

module_init(dump_init);
module_exit(dump_cleanup);
