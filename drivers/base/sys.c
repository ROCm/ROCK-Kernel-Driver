/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002 Patrick Mochel
 *              2002 Open Source Development Lab
 * 
 * This exports a 'system' bus type. 
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use register_sys_device() to
 * add themselves as children of the system bus.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>

static struct device system_bus = {
       .name           = "System Bus",
       .bus_id         = "sys",
};

int register_sys_device(struct device * dev)
{
       int error = -EINVAL;

       if (dev) {
               if (!dev->parent)
                       dev->parent = &system_bus;
               error = device_register(dev);
       }
       return error;
}

void unregister_sys_device(struct device * dev)
{
       if (dev)
               put_device(dev);
}

static int sys_bus_init(void)
{
       return device_register(&system_bus);
}

postcore_initcall(sys_bus_init);
EXPORT_SYMBOL(register_sys_device);
EXPORT_SYMBOL(unregister_sys_device);
