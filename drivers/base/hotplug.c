/*
 * drivers/base/hotplug.c - hotplug call code
 * 
 * Copyright (c) 2000-2001 David Brownell
 * Copyright (c) 2002 Greg Kroah-Hartman
 * Copyright (c) 2002 IBM Corp.
 *
 * Based off of drivers/usb/core/usb.c:call_agent(), which was 
 * written by David Brownell.
 *
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kmod.h>
#include <linux/interrupt.h>
#include "base.h"
#include "fs/fs.h"

/*
 * hotplugging invokes what /proc/sys/kernel/hotplug says (normally
 * /sbin/hotplug) when devices get added or removed.
 *
 * This invokes a user mode policy agent, typically helping to load driver
 * or other modules, configure the device, and more.  Drivers can provide
 * a MODULE_DEVICE_TABLE to help with module loading subtasks.
 */
#define BUFFER_SIZE	1024	/* should be enough memory for the env */
#define NUM_ENVP	32	/* number of env pointers */
int dev_hotplug (struct device *dev, const char *action)
{
	char *argv [3], **envp, *buffer, *scratch;
	char *dev_path;
	int retval;
	int i = 0;
	int dev_length;

	pr_debug ("%s\n", __FUNCTION__);
	if (!dev)
		return -ENODEV;

	if (!dev->bus)
		return -ENODEV;

	if (!hotplug_path [0])
		return -ENODEV;

	if (in_interrupt ()) {
		pr_debug ("%s - in_interrupt, not allowed!", __FUNCTION__);
		return -EIO;
	}

	if (!current->fs->root) {
		/* don't try to do anything unless we have a root partition */
		pr_debug ("%s - %s -- no FS yet\n", __FUNCTION__, action);
		return -EIO;
	}

	envp = (char **) kmalloc (NUM_ENVP * sizeof (char *), GFP_KERNEL);
	if (!envp)
		return -ENOMEM;
	memset (envp, 0x00, NUM_ENVP * sizeof (char *));

	buffer = kmalloc (BUFFER_SIZE, GFP_KERNEL);
	if (!buffer) {
		kfree (envp);
		return -ENOMEM;
	}

	dev_length = get_devpath_length (dev);
	dev_length += strlen("root");
	dev_path = kmalloc (dev_length, GFP_KERNEL);
	if (!dev_path) {
		kfree (buffer);
		kfree (envp);
		return -ENOMEM;
	}
	memset (dev_path, 0x00, dev_length);
	strcpy (dev_path, "root");
	fill_devpath (dev, dev_path, dev_length);

	/* only one standardized param to hotplug command: the bus name */
	argv [0] = hotplug_path;
	argv [1] = dev->bus->name;
	argv [2] = 0;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	scratch = buffer;

	envp [i++] = scratch;
	scratch += sprintf (scratch, "ACTION=%s", action) + 1;

	envp [i++] = scratch;
	scratch += sprintf (scratch, "DEVICE=%s", dev_path) + 1;
	
	if (dev->bus->hotplug) {
		/* have the bus specific function add its stuff */
		retval = dev->bus->hotplug (dev, &envp[i], NUM_ENVP - i,
					    scratch,
					    BUFFER_SIZE - (scratch - buffer));
		if (retval) {
			pr_debug ("%s - hotplug() returned %d\n",
				  __FUNCTION__, retval);
			goto exit;
		}
	}

	pr_debug ("%s: %s %s %s %s %s %s\n", __FUNCTION__, argv [0], argv[1],
		  envp[0], envp[1], envp[2], envp[3]);
	retval = call_usermodehelper (argv [0], argv, envp);
	if (retval)
		pr_debug ("%s - call_usermodehelper returned %d\n",
			  __FUNCTION__, retval);

exit:
	kfree (dev_path);
	kfree (buffer);
	kfree (envp);
	return retval;
}
