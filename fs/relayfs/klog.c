/*
 * KLOG		Generic Logging facility built upon the relayfs infrastructure
 *
 * Authors:	Hubertus Franke  (frankeh@us.ibm.com)
 *		Tom Zanussi  (zanussi@us.ibm.com)
 *
 *		Please direct all questions/comments to zanussi@us.ibm.com
 *
 *		Copyright (C) 2003, IBM Corp
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/relayfs_fs.h>
#include <linux/klog.h>

/* klog channel id */
static int klog_channel = -1;

/* maximum size of klog formatting buffer beyond which truncation will occur */
#define KLOG_BUF_SIZE (512)
/* per-cpu klog formatting buffer */
static char buf[NR_CPUS][KLOG_BUF_SIZE];

/*
 *	klog_enabled determines whether klog()/klog_raw() actually do write
 *	to the klog channel at any given time. If klog_enabled == 1 they do,
 *	otherwise they don't.  Settable using sysctl fs.relayfs.klog_enabled.
 */
#ifdef CONFIG_KLOG_CHANNEL_AUTOENABLE
static int klog_enabled = 1;
#else
static int klog_enabled = 0;
#endif

/**
 *	klog - write a formatted string into the klog channel
 *	@fmt: format string
 *
 *	Returns number of bytes written, negative number on failure.
 */
int klog(const char *fmt, ...)
{
	va_list args;
	int len, err;
	char *cbuf;
	unsigned long flags;

	if (!klog_enabled || klog_channel < 0) 
		return 0;

	local_irq_save(flags);
	cbuf = buf[smp_processor_id()];

	va_start(args, fmt);
	len = vsnprintf(cbuf, KLOG_BUF_SIZE, fmt, args);
	va_end(args);
	
	err = relay_write(klog_channel, cbuf, len, -1, NULL);
	local_irq_restore(flags);

	return err;
}

/**
 *	klog_raw - directly write into the klog channel
 *	@buf: buffer containing data to write
 *	@len: # bytes to write
 *
 *	Returns number of bytes written, negative number on failure.
 */
int klog_raw(const char *buf,int len)
{
	int err = 0;
	
	if (klog_enabled && klog_channel >= 0)
		err = relay_write(klog_channel, buf, len, -1, NULL);

	return err;
}

/**
 *	relayfs sysctl data
 *
 *	Only sys/fs/relayfs/klog_enabled for now.
 */
#define CTL_ENABLE_KLOG		100
#define CTL_RELAYFS		100

static struct ctl_table_header *relayfs_ctl_table_header;

static struct ctl_table relayfs_table[] =
{
	{
		.ctl_name	= CTL_ENABLE_KLOG,
		.procname	= "klog_enabled",
		.data		= &klog_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		0
	}
};

static struct ctl_table relayfs_dir_table[] =
{
	{
		.ctl_name	= CTL_RELAYFS,
		.procname	= "relayfs",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0555,
		.child		= relayfs_table,
	},
	{
		0
	}
};

static struct ctl_table relayfs_root_table[] =
{
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0555,
		.child		= relayfs_dir_table,
	},
	{
		0
	}
};

/**
 *	create_klog_channel - creates channel /mnt/relay/klog
 *
 *	Returns channel id on success, negative otherwise.
 */
int 
create_klog_channel(void)
{
	u32 bufsize, nbufs;
	u32 channel_flags;

	channel_flags = RELAY_DELIVERY_PACKET | RELAY_USAGE_GLOBAL;
	channel_flags |= RELAY_SCHEME_ANY | RELAY_TIMESTAMP_ANY;

	bufsize = 1 << (CONFIG_KLOG_CHANNEL_SHIFT - 2);
	nbufs = 4;

	klog_channel = relay_open("klog",
				  bufsize,
				  nbufs,
				  channel_flags,
				  NULL,
				  0,
				  0,
				  0,
				  0,
				  0,
				  0,
				  NULL,
				  0);

	if (klog_channel < 0)
		printk("klog channel creation failed, errcode: %d\n", klog_channel);
	else {
		printk("klog channel created (%u bytes)\n", 1 << CONFIG_KLOG_CHANNEL_SHIFT);
		relayfs_ctl_table_header = register_sysctl_table(relayfs_root_table, 1);
	}

	return klog_channel;
}

/**
 *	remove_klog_channel - destroys channel /mnt/relay/klog
 *
 *	Returns 0, negative otherwise.
 */
int
remove_klog_channel(void)
{
	if (relayfs_ctl_table_header)
		unregister_sysctl_table(relayfs_ctl_table_header);
	
	return relay_close(klog_channel);
}

EXPORT_SYMBOL(klog);
EXPORT_SYMBOL(klog_raw);

