/*
 * kernel userspace event delivery
 *
 * Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2004 Novell, Inc.  All rights reserved.
 * Copyright (C) 2004 IBM, Inc. All rights reserved.
 *
 * Licensed under the GNU GPL v2.
 *
 * Authors:
 *	Robert Love		<rml@novell.com>
 *	Kay Sievers		<kay.sievers@vrfy.org>
 *	Arjan van de Ven	<arjanv@redhat.com>
 *	Greg Kroah-Hartman	<greg@kroah.com>
 */

#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/string.h>
#include <linux/kobject_uevent.h>
#include <linux/kobject.h>
#include <net/sock.h>

/* 
 * These must match up with the values for enum kobject_action
 * as found in include/linux/kobject_uevent.h
 */
static char *actions[] = {
	"add",		/* 0x00 */
	"remove",	/* 0x01 */
	"change",	/* 0x02 */
	"mount",	/* 0x03 */
	"umount",	/* 0x04 */
};

static char *action_to_string(enum kobject_action action)
{
	if (action >= KOBJ_MAX_ACTION)
		return NULL;
	else
		return actions[action];
}

#ifdef CONFIG_KOBJECT_UEVENT
static struct sock *uevent_sock;

/**
 * send_uevent - notify userspace by sending event trough netlink socket
 *
 * @signal: signal name
 * @obj: object path (kobject)
 * @buf: buffer used to pass auxiliary data like the hotplug environment
 * @buflen:
 * gfp_mask:
 */
static int send_uevent(const char *signal, const char *obj, const void *buf,
			int buflen, int gfp_mask)
{
	struct sk_buff *skb;
	char *pos;
	int len;

	if (!uevent_sock)
		return -EIO;

	len = strlen(signal) + 1;
	len += strlen(obj) + 1;
	len += buflen;

	skb = alloc_skb(len, gfp_mask);
	if (!skb)
		return -ENOMEM;

	pos = skb_put(skb, len);

	pos += sprintf(pos, "%s@%s", signal, obj) + 1;
	memcpy(pos, buf, buflen);

	return netlink_broadcast(uevent_sock, skb, 0, 1, gfp_mask);
}

static int do_kobject_uevent(struct kobject *kobj, enum kobject_action action, 
			     struct attribute *attr, int gfp_mask)
{
	char *path;
	char *attrpath;
	char *signal;
	int len;
	int rc = -ENOMEM;

	path = kobject_get_path(kobj, gfp_mask);
	if (!path)
		return -ENOMEM;

	signal = action_to_string(action);
	if (!signal)
		return -EINVAL;

	if (attr) {
		len = strlen(path);
		len += strlen(attr->name) + 2;
		attrpath = kmalloc(len, gfp_mask);
		if (!attrpath)
			goto exit;
		sprintf(attrpath, "%s/%s", path, attr->name);
		rc = send_uevent(signal, attrpath, NULL, 0, gfp_mask);
		kfree(attrpath);
	} else {
		rc = send_uevent(signal, path, NULL, 0, gfp_mask);
	}

exit:
	kfree(path);
	return rc;
}

/**
 * kobject_uevent - notify userspace by sending event through netlink socket
 * 
 * @signal: signal name
 * @kobj: struct kobject that the event is happening to
 * @attr: optional struct attribute the event belongs to
 */
int kobject_uevent(struct kobject *kobj, enum kobject_action action,
		   struct attribute *attr)
{
	return do_kobject_uevent(kobj, action, attr, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(kobject_uevent);

int kobject_uevent_atomic(struct kobject *kobj, enum kobject_action action,
			  struct attribute *attr)
{
	return do_kobject_uevent(kobj, action, attr, GFP_ATOMIC);
}

EXPORT_SYMBOL_GPL(kobject_uevent_atomic);

static int __init kobject_uevent_init(void)
{
	uevent_sock = netlink_kernel_create(NETLINK_KOBJECT_UEVENT, NULL);

	if (!uevent_sock) {
		printk(KERN_ERR
		       "kobject_uevent: unable to create netlink socket!\n");
		return -ENODEV;
	}

	return 0;
}

core_initcall(kobject_uevent_init);

#else
static inline int send_uevent(const char *signal, const char *obj,
			      const void *buf, int buflen, int gfp_mask)
{
	return 0;
}

#endif /* CONFIG_KOBJECT_UEVENT */


#ifdef CONFIG_HOTPLUG
u64 hotplug_seqnum;
static spinlock_t sequence_lock = SPIN_LOCK_UNLOCKED;

#define BUFFER_SIZE	1024	/* should be enough memory for the env */
#define NUM_ENVP	32	/* number of env pointers */
/**
 * kobject_hotplug - notify userspace by executing /sbin/hotplug
 *
 * @action: action that is happening (usually "ADD" or "REMOVE")
 * @kobj: struct kobject that the action is happening to
 */
void kobject_hotplug(struct kobject *kobj, enum kobject_action action)
{
	char *argv [3];
	char **envp = NULL;
	char *buffer = NULL;
	char *scratch;
	int i = 0;
	int retval;
	char *kobj_path = NULL;
	char *name = NULL;
	char *action_string;
	u64 seq;
	struct kobject *top_kobj = kobj;
	struct kset *kset;

	if (!top_kobj->kset && top_kobj->parent) {
		do {
			top_kobj = top_kobj->parent;
		} while (!top_kobj->kset && top_kobj->parent);
	}

	if (top_kobj->kset && top_kobj->kset->hotplug_ops)
		kset = top_kobj->kset;
	else
		return;

	/* If the kset has a filter operation, call it.
	   Skip the event, if the filter returns zero. */
	if (kset->hotplug_ops->filter) {
		if (!kset->hotplug_ops->filter(kset, kobj))
			return;
	}

	pr_debug ("%s\n", __FUNCTION__);

	action_string = action_to_string(action);
	if (!action_string)
		return;

	envp = kmalloc(NUM_ENVP * sizeof (char *), GFP_KERNEL);
	if (!envp)
		return;
	memset (envp, 0x00, NUM_ENVP * sizeof (char *));

	buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (!buffer)
		goto exit;

	if (kset->hotplug_ops->name)
		name = kset->hotplug_ops->name(kset, kobj);
	if (name == NULL)
		name = kset->kobj.name;

	argv [0] = hotplug_path;
	argv [1] = name;
	argv [2] = NULL;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	scratch = buffer;

	envp [i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", action_string) + 1;

	kobj_path = kobject_get_path(kobj, GFP_KERNEL);
	if (!kobj_path)
		goto exit;

	envp [i++] = scratch;
	scratch += sprintf (scratch, "DEVPATH=%s", kobj_path) + 1;

	spin_lock(&sequence_lock);
	seq = ++hotplug_seqnum;
	spin_unlock(&sequence_lock);

	envp [i++] = scratch;
	scratch += sprintf(scratch, "SEQNUM=%lld", (long long)seq) + 1;

	envp [i++] = scratch;
	scratch += sprintf(scratch, "SUBSYSTEM=%s", name) + 1;

	if (kset->hotplug_ops->hotplug) {
		/* have the kset specific function add its stuff */
		retval = kset->hotplug_ops->hotplug (kset, kobj,
				  &envp[i], NUM_ENVP - i, scratch,
				  BUFFER_SIZE - (scratch - buffer));
		if (retval) {
			pr_debug ("%s - hotplug() returned %d\n",
				  __FUNCTION__, retval);
			goto exit;
		}
	}

	pr_debug ("%s: %s %s %s %s %s %s %s\n", __FUNCTION__, argv[0], argv[1],
		  envp[0], envp[1], envp[2], envp[3], envp[4]);

	send_uevent(action_string, kobj_path, buffer, scratch - buffer, GFP_KERNEL);

	if (!hotplug_path[0])
		goto exit;

	retval = call_usermodehelper (argv[0], argv, envp, 0);
	if (retval)
		pr_debug ("%s - call_usermodehelper returned %d\n",
			  __FUNCTION__, retval);

exit:
	kfree(kobj_path);
	kfree(buffer);
	kfree(envp);
	return;
}
EXPORT_SYMBOL(kobject_hotplug);

/**
 * add_hotplug_env_var - helper for creating hotplug environment variables
 * @envp: Pointer to table of environment variables, as passed into
 * hotplug() method.
 * @num_envp: Number of environment variable slots available, as
 * passed into hotplug() method.
 * @cur_index: Pointer to current index into @envp.  It should be
 * initialized to 0 before the first call to add_hotplug_env_var(),
 * and will be incremented on success.
 * @buffer: Pointer to buffer for environment variables, as passed
 * into hotplug() method.
 * @buffer_size: Length of @buffer, as passed into hotplug() method.
 * @cur_len: Pointer to current length of space used in @buffer.
 * Should be initialized to 0 before the first call to
 * add_hotplug_env_var(), and will be incremented on success.
 * @format: Format for creating environment variable (of the form
 * "XXX=%x") for snprintf().
 *
 * Returns 0 if environment variable was added successfully or -ENOMEM
 * if no space was available.
 */
int add_hotplug_env_var(char **envp, int num_envp, int *cur_index,
			char *buffer, int buffer_size, int *cur_len,
			const char *format, ...)
{
	va_list args;

	/*
	 * We check against num_envp - 1 to make sure there is at
	 * least one slot left after we return, since the hotplug
	 * method needs to set the last slot to NULL.
	 */
	if (*cur_index >= num_envp - 1)
		return -ENOMEM;

	envp[*cur_index] = buffer + *cur_len;

	va_start(args, format);
	*cur_len += vsnprintf(envp[*cur_index],
			      max(buffer_size - *cur_len, 0),
			      format, args) + 1;
	va_end(args);

	if (*cur_len > buffer_size)
		return -ENOMEM;

	(*cur_index)++;
	return 0;
}
EXPORT_SYMBOL(add_hotplug_env_var);

#endif /* CONFIG_HOTPLUG */
