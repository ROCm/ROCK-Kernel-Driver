/*
 * ioctl.c
 *
 * Perform 32->64 ioctl conversion
 *
 * Copyright (C) 2003 SuSE Linux AG
 * Copyright (C) 2003, International Business Machines Corp.
 *
 * Written by Jerone Young.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sys.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/ioctl.h>
#include <linux/ioctl32.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "audit-private.h"

static int	do_audit_usermsg_ioctl(unsigned int fd, unsigned int cmd, 
				unsigned long arg, struct file * file);
static int	do_audit_filter_ioctl(unsigned int fd, unsigned int cmd, 
				unsigned long arg, struct file * file);
static int	do_audit_policy_ioctl(unsigned int fd, unsigned int cmd, 
				unsigned long arg, struct file * file);
static int	do_audit_login_ioctl(unsigned int fd, unsigned int cmd, 
				unsigned long arg, struct file * file);


struct audit_message32 {
	uint32_t	msg_type;
	char	msg_evname[AUD_MAX_EVNAME];
	uint32_t	msg_data;
	uint32_t	msg_size;
};

struct audit_filter32 {
	unsigned short  num;
	unsigned short  op;
	char event[AUD_MAX_EVNAME];
	union {
		struct {
			unsigned short	target;
			unsigned short	filter;
		} apply;
		struct {
			unsigned short filt1,filt2;
		} boolean;
		struct {
			uint32_t action;
		} freturn;
		struct {
			uint64_t value;
			uint64_t mask;
		} integer;
		struct {
			uint32_t value;
		} string;
	} u;
};

#define AUIOCUSERMESSAGE_32        _IOR(AUD_MAGIC, 111, struct audit_message32)

static int 
do_audit_usermsg_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, 
			struct file * file)
{
	int ret, i;
	mm_segment_t old_fs;
	struct audit_message aum;
	struct audit_message32 copy;

	if (copy_from_user(&copy, (struct audit_message32 *) arg, 
			sizeof(struct audit_message32)))
		return -EFAULT;
		
	aum.msg_type = copy.msg_type;
	for (i = 0; i < AUD_MAX_EVNAME; i++) 
		aum.msg_evname[i] = copy.msg_evname[i];
	aum.msg_data = (void *)(unsigned long) copy.msg_data;
	aum.msg_size = copy.msg_size;

	/* Make sure the pointer is good. */
	if (!access_ok(VERIFY_READ, aum.msg_data, aum.msg_size))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, AUIOCUSERMESSAGE, (unsigned long) &aum);
	set_fs(old_fs);

	return ret;
}

static int 
do_audit_filter_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, 
			struct file * file)
{
	int ret, i;
	mm_segment_t old_fs;
	struct audit_filter auf;
	struct audit_filter32 copy;

	if (copy_from_user(&copy, (struct audit_filter32 *) arg, 
			sizeof(struct audit_filter32)))
		return -EFAULT;
	
	auf.num = copy.num;
	auf.op = copy.op;
	for (i = 0; i < AUD_MAX_EVNAME; i++) 
		auf.event[i] = copy.event[i];
	switch (copy.op) {
	case AUD_FILT_OP_AND:
	case AUD_FILT_OP_OR:
	case AUD_FILT_OP_NOT:
		auf.u.boolean.filt1 = copy.u.boolean.filt1;
		auf.u.boolean.filt2 = copy.u.boolean.filt2;
		break;
	case AUD_FILT_OP_APPLY:
		auf.u.apply.target = copy.u.apply.target;
		auf.u.apply.filter = copy.u.apply.filter;
		break;
	case AUD_FILT_OP_RETURN:
		auf.u.freturn.action = copy.u.freturn.action;
		break;
	case AUD_FILT_OP_EQ:
	case AUD_FILT_OP_NE:
	case AUD_FILT_OP_GT:
	case AUD_FILT_OP_GE:
	case AUD_FILT_OP_LE:
	case AUD_FILT_OP_LT:
	case AUD_FILT_OP_MASK:
		auf.u.integer.value = copy.u.integer.value;
		auf.u.integer.mask = copy.u.integer.mask;
		break;
	case AUD_FILT_OP_STREQ:
	case AUD_FILT_OP_PREFIX:
		auf.u.string.value = (char *)(unsigned long) copy.u.string.value;
		break;
	default:
		break;
	}
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long) &auf);
	set_fs(old_fs);

	return ret;
}

static int 
do_audit_policy_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, 
			struct file * file)
{	
	int ret;
	mm_segment_t old_fs;
	struct audit_policy pol;
	
	if (copy_from_user(&pol, (struct audit_policy *) arg, 
			sizeof(struct audit_policy)))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long) &pol);
	set_fs(old_fs);
	
	return ret;
}

static int 
do_audit_login_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg,
			struct file * file)
{
	int ret;
	mm_segment_t old_fs;
	struct audit_login lg;

	if (copy_from_user(&lg, (struct audit_login *) arg, 
			sizeof(struct audit_login)))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long) &lg);
	set_fs(old_fs);

	return ret;	
}

static const struct {
	int		code;
	int		(*handler)(unsigned int,
				   unsigned int,
				   unsigned long,
				   struct file *);
} converters[] = {
	/* compatible */
	{ AUIOCATTACH,		NULL },
	{ AUIOCDETACH,		NULL },
	{ AUIOCSUSPEND,		NULL },
	{ AUIOCRESUME,		NULL },
	{ AUIOCCLRPOLICY,	NULL },
	{ AUIOCIAMAUDITD,	NULL },
	{ AUIOCSETAUDITID,	NULL },
	{ AUIOCCLRFILTER,	NULL },
	{ AUIOCVERSION,		NULL },
	{ AUIOCRESET,		NULL },

	/* need handler */
	{ AUIOCLOGIN,		do_audit_login_ioctl },
	{ AUIOCSETPOLICY,	do_audit_policy_ioctl },
	{ AUIOCUSERMESSAGE,	do_audit_usermsg_ioctl },
	{ AUIOCUSERMESSAGE_32,	do_audit_usermsg_ioctl },
	{ AUIOCSETFILTER,	do_audit_filter_ioctl },

	{ -1 }
};

/*
 * Register ioctl converters
 */
int
audit_register_ioctl_converters(void)
{
	int	i, err = 0;

	for (i = 0; err >= 0 && converters[i].code != -1; i++) {
		err = register_ioctl32_conversion(converters[i].code,
				converters[i].handler);
	}

	if (err < 0)
		printk(KERN_ERR "audit: Failed to register ioctl32 "
				"conversion handlers\n");
	return err;
}

/*
 * Unregister ioctl converters
 */
int
audit_unregister_ioctl_converters(void)
{
	int	i, err = 0;

	for (i = 0; err >= 0 && converters[i].code != -1; i++) {
		err = unregister_ioctl32_conversion(converters[i].code);
	}

	if (err < 0)
		printk(KERN_ERR "audit: Failed to unregister ioctl32 "
				"conversion handlers\n");
	return err;
}
