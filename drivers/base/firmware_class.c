/*
 * firmware_class.c - Multi purpose firmware loading support
 *
 * Copyright (c) 2003 Manuel Estrada Sainz <ranty@debian.org>
 *
 * Please see Documentation/firmware_class/ for more information.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <asm/hardirq.h>

#include <linux/firmware.h>
#include "base.h"

MODULE_AUTHOR("Manuel Estrada Sainz <ranty@debian.org>");
MODULE_DESCRIPTION("Multi purpose firmware loading support");
MODULE_LICENSE("GPL");

static int loading_timeout = 10;	/* In seconds */

struct firmware_priv {
	char fw_id[FIRMWARE_NAME_MAX];
	struct completion completion;
	struct bin_attribute attr_data;
	struct firmware *fw;
	int loading;
	int abort;
	int alloc_size;
	struct timer_list timeout;
};

static ssize_t
firmware_timeout_show(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", loading_timeout);
}

/**
 * firmware_timeout_store:
 * Description:
 *	Sets the number of seconds to wait for the firmware.  Once
 *	this expires an error will be return to the driver and no
 *	firmware will be provided.
 *
 *	Note: zero means 'wait for ever'
 *  
 **/
static ssize_t
firmware_timeout_store(struct class *class, const char *buf, size_t count)
{
	loading_timeout = simple_strtol(buf, NULL, 10);
	return count;
}

static CLASS_ATTR(timeout, 0644, firmware_timeout_show, firmware_timeout_store);

static void  fw_class_dev_release(struct class_device *class_dev);
int firmware_class_hotplug(struct class_device *dev, char **envp,
			   int num_envp, char *buffer, int buffer_size);

static struct class firmware_class = {
	.name		= "firmware",
	.hotplug	= firmware_class_hotplug,
	.release	= fw_class_dev_release,
};

int
firmware_class_hotplug(struct class_device *class_dev, char **envp,
		       int num_envp, char *buffer, int buffer_size)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	int i = 0;
	char *scratch = buffer;

	if (buffer_size < (FIRMWARE_NAME_MAX + 10))
		return -ENOMEM;
	if (num_envp < 1)
		return -ENOMEM;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "FIRMWARE=%s", fw_priv->fw_id) + 1;
	return 0;
}

static ssize_t
firmware_loading_show(struct class_device *class_dev, char *buf)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	return sprintf(buf, "%d\n", fw_priv->loading);
}

/**
 * firmware_loading_store: - loading control file
 * Description:
 *	The relevant values are: 
 *
 *	 1: Start a load, discarding any previous partial load.
 *	 0: Conclude the load and handle the data to the driver code.
 *	-1: Conclude the load with an error and discard any written data.
 **/
static ssize_t
firmware_loading_store(struct class_device *class_dev,
		       const char *buf, size_t count)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	int prev_loading = fw_priv->loading;

	fw_priv->loading = simple_strtol(buf, NULL, 10);

	switch (fw_priv->loading) {
	case -1:
		fw_priv->abort = 1;
		wmb();
		complete(&fw_priv->completion);
		break;
	case 1:
		kfree(fw_priv->fw->data);
		fw_priv->fw->data = NULL;
		fw_priv->fw->size = 0;
		fw_priv->alloc_size = 0;
		break;
	case 0:
		if (prev_loading == 1)
			complete(&fw_priv->completion);
		break;
	}

	return count;
}

static CLASS_DEVICE_ATTR(loading, 0644,
			firmware_loading_show, firmware_loading_store);

static ssize_t
firmware_data_read(struct kobject *kobj,
		   char *buffer, loff_t offset, size_t count)
{
	struct class_device *class_dev = to_class_dev(kobj);
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	struct firmware *fw = fw_priv->fw;

	if (offset > fw->size)
		return 0;
	if (offset + count > fw->size)
		count = fw->size - offset;

	memcpy(buffer, fw->data + offset, count);
	return count;
}
static int
fw_realloc_buffer(struct firmware_priv *fw_priv, int min_size)
{
	u8 *new_data;

	if (min_size <= fw_priv->alloc_size)
		return 0;

	new_data = vmalloc(fw_priv->alloc_size + PAGE_SIZE);
	if (!new_data) {
		printk(KERN_ERR "%s: unable to alloc buffer\n", __FUNCTION__);
		/* Make sure that we don't keep incomplete data */
		fw_priv->abort = 1;
		return -ENOMEM;
	}
	fw_priv->alloc_size += PAGE_SIZE;
	if (fw_priv->fw->data) {
		memcpy(new_data, fw_priv->fw->data, fw_priv->fw->size);
		vfree(fw_priv->fw->data);
	}
	fw_priv->fw->data = new_data;
	BUG_ON(min_size > fw_priv->alloc_size);
	return 0;
}

/**
 * firmware_data_write:
 *
 * Description:
 *
 *	Data written to the 'data' attribute will be later handled to
 *	the driver as a firmware image.
 **/
static ssize_t
firmware_data_write(struct kobject *kobj,
		    char *buffer, loff_t offset, size_t count)
{
	struct class_device *class_dev = to_class_dev(kobj);
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	struct firmware *fw = fw_priv->fw;
	int retval;

	retval = fw_realloc_buffer(fw_priv, offset + count);
	if (retval)
		return retval;

	memcpy(fw->data + offset, buffer, count);

	fw->size = max_t(size_t, offset + count, fw->size);

	return count;
}
static struct bin_attribute firmware_attr_data_tmpl = {
	.attr = {.name = "data", .mode = 0644},
	.size = 0,
	.read = firmware_data_read,
	.write = firmware_data_write,
};

static void
fw_class_dev_release(struct class_device *class_dev)
{
	kfree(class_dev);
}

static void
firmware_class_timeout(u_long data)
{
	struct firmware_priv *fw_priv = (struct firmware_priv *) data;
	fw_priv->abort = 1;
	wmb();
	complete(&fw_priv->completion);
}

static inline void
fw_setup_class_device_id(struct class_device *class_dev, struct device *dev)
{
	/* XXX warning we should watch out for name collisions */
	strncpy(class_dev->class_id, dev->bus_id, BUS_ID_SIZE);
	class_dev->class_id[BUS_ID_SIZE - 1] = '\0';
}
static int
fw_setup_class_device(struct class_device **class_dev_p,
		      const char *fw_name, struct device *device)
{
	int retval = 0;
	struct firmware_priv *fw_priv = kmalloc(sizeof (struct firmware_priv),
						GFP_KERNEL);
	struct class_device *class_dev = kmalloc(sizeof (struct class_device),
						 GFP_KERNEL);

	if (!fw_priv || !class_dev) {
		retval = -ENOMEM;
		goto error_kfree;
	}
	memset(fw_priv, 0, sizeof (*fw_priv));
	memset(class_dev, 0, sizeof (*class_dev));

	init_completion(&fw_priv->completion);
	memcpy(&fw_priv->attr_data, &firmware_attr_data_tmpl,
	       sizeof (firmware_attr_data_tmpl));

	strncpy(&fw_priv->fw_id[0], fw_name, FIRMWARE_NAME_MAX);
	fw_priv->fw_id[FIRMWARE_NAME_MAX - 1] = '\0';

	fw_setup_class_device_id(class_dev, device);
	class_dev->dev = device;

	fw_priv->timeout.function = firmware_class_timeout;
	fw_priv->timeout.data = (u_long) fw_priv;
	init_timer(&fw_priv->timeout);

	class_dev->class = &firmware_class;
	class_set_devdata(class_dev, fw_priv);
	retval = class_device_register(class_dev);
	if (retval) {
		printk(KERN_ERR "%s: class_device_register failed\n",
		       __FUNCTION__);
		goto error_kfree;
	}

	retval = sysfs_create_bin_file(&class_dev->kobj, &fw_priv->attr_data);
	if (retval) {
		printk(KERN_ERR "%s: sysfs_create_bin_file failed\n",
		       __FUNCTION__);
		goto error_unreg_class_dev;
	}

	retval = class_device_create_file(class_dev,
					  &class_device_attr_loading);
	if (retval) {
		printk(KERN_ERR "%s: class_device_create_file failed\n",
		       __FUNCTION__);
		goto error_remove_data;
	}

	fw_priv->fw = kmalloc(sizeof (struct firmware), GFP_KERNEL);
	if (!fw_priv->fw) {
		printk(KERN_ERR "%s: kmalloc(struct firmware) failed\n",
		       __FUNCTION__);
		retval = -ENOMEM;
		goto error_remove_loading;
	}
	memset(fw_priv->fw, 0, sizeof (*fw_priv->fw));

	goto out;

error_remove_loading:
	class_device_remove_file(class_dev, &class_device_attr_loading);
error_remove_data:
	sysfs_remove_bin_file(&class_dev->kobj, &fw_priv->attr_data);
error_unreg_class_dev:
	class_device_unregister(class_dev);
error_kfree:
	kfree(fw_priv);
	kfree(class_dev);
	*class_dev_p = NULL;
out:
	*class_dev_p = class_dev;
	return retval;
}
static void
fw_remove_class_device(struct class_device *class_dev)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);

	class_device_remove_file(class_dev, &class_device_attr_loading);
	sysfs_remove_bin_file(&class_dev->kobj, &fw_priv->attr_data);
	class_device_unregister(class_dev);
}

/** 
 * request_firmware: - request firmware to hotplug and wait for it
 * Description:
 *	@firmware will be used to return a firmware image by the name
 *	of @name for device @device.
 *
 *	Should be called from user context where sleeping is allowed.
 *
 *	@name will be use as $FIRMWARE in the hotplug environment and
 *	should be distinctive enough not to be confused with any other
 *	firmware image for this or any other device.
 **/
int
request_firmware(const struct firmware **firmware, const char *name,
		 struct device *device)
{
	struct class_device *class_dev;
	struct firmware_priv *fw_priv;
	int retval;

	if (!firmware)
		return -EINVAL;

	*firmware = NULL;

	retval = fw_setup_class_device(&class_dev, name, device);
	if (retval)
		goto out;

	fw_priv = class_get_devdata(class_dev);

	if (loading_timeout) {
		fw_priv->timeout.expires = jiffies + loading_timeout * HZ;
		add_timer(&fw_priv->timeout);
	}

	wait_for_completion(&fw_priv->completion);

	del_timer(&fw_priv->timeout);
	fw_remove_class_device(class_dev);

	if (fw_priv->fw->size && !fw_priv->abort) {
		*firmware = fw_priv->fw;
	} else {
		retval = -ENOENT;
		vfree(fw_priv->fw->data);
		kfree(fw_priv->fw);
	}
	kfree(fw_priv);
out:
	return retval;
}

/**
 * release_firmware: - release the resource associated with a firmware image
 **/
void
release_firmware(const struct firmware *fw)
{
	if (fw) {
		vfree(fw->data);
		kfree(fw);
	}
}

/**
 * register_firmware: - provide a firmware image for later usage
 * 
 * Description:
 *	Make sure that @data will be available by requesting firmware @name.
 *
 *	Note: This will not be possible until some kind of persistence
 *	is available.
 **/
void
register_firmware(const char *name, const u8 *data, size_t size)
{
	/* This is meaningless without firmware caching, so until we
	 * decide if firmware caching is reasonable just leave it as a
	 * noop */
}

/* Async support */
struct firmware_work {
	struct work_struct work;
	struct module *module;
	const char *name;
	struct device *device;
	void *context;
	void (*cont)(const struct firmware *fw, void *context);
};

static void
request_firmware_work_func(void *arg)
{
	struct firmware_work *fw_work = arg;
	const struct firmware *fw;
	if (!arg)
		return;
	request_firmware(&fw, fw_work->name, fw_work->device);
	fw_work->cont(fw, fw_work->context);
	release_firmware(fw);
	module_put(fw_work->module);
	kfree(fw_work);
}

/**
 * request_firmware_nowait:
 *
 * Description:
 *	Asynchronous variant of request_firmware() for contexts where
 *	it is not possible to sleep.
 *
 *	@cont will be called asynchronously when the firmware request is over.
 *
 *	@context will be passed over to @cont.
 *
 *	@fw may be %NULL if firmware request fails.
 *
 **/
int
request_firmware_nowait(
	struct module *module,
	const char *name, struct device *device, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	struct firmware_work *fw_work = kmalloc(sizeof (struct firmware_work),
						GFP_ATOMIC);
	if (!fw_work)
		return -ENOMEM;
	if (!try_module_get(module)) {
		kfree(fw_work);
		return -EFAULT;
	}

	*fw_work = (struct firmware_work) {
		.module = module,
		.name = name,
		.device = device,
		.context = context,
		.cont = cont,
	};
	INIT_WORK(&fw_work->work, request_firmware_work_func, fw_work);

	schedule_work(&fw_work->work);
	return 0;
}

static int __init
firmware_class_init(void)
{
	int error;
	error = class_register(&firmware_class);
	if (error) {
		printk(KERN_ERR "%s: class_register failed\n", __FUNCTION__);
	}
	error = class_create_file(&firmware_class, &class_attr_timeout);
	if (error) {
		printk(KERN_ERR "%s: class_create_file failed\n",
		       __FUNCTION__);
		class_unregister(&firmware_class);
	}
	return error;

}
static void __exit
firmware_class_exit(void)
{
	class_remove_file(&firmware_class, &class_attr_timeout);
	class_unregister(&firmware_class);
}

module_init(firmware_class_init);
module_exit(firmware_class_exit);

EXPORT_SYMBOL(release_firmware);
EXPORT_SYMBOL(request_firmware);
EXPORT_SYMBOL(request_firmware_nowait);
EXPORT_SYMBOL(register_firmware);
EXPORT_SYMBOL(firmware_class);
