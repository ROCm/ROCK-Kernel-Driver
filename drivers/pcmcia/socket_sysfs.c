/*
 * socket_sysfs.c -- most of socket-related sysfs output
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) 2003 - 2004		Dominik Brodowski
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <asm/system.h>
#include <asm/irq.h>

#define IN_CARD_SERVICES
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "cs_internal.h"

#define to_socket(_dev) container_of(_dev, struct pcmcia_socket, dev)

static ssize_t pccard_show_type(struct class_device *dev, char *buf)
{
	int val;
	struct pcmcia_socket *s = to_socket(dev);

        if (!(s->state & SOCKET_PRESENT))
                return -ENODEV;
	s->ops->get_status(s, &val);
	if (val & SS_CARDBUS)
		return sprintf(buf, "32-bit\n");
	if (val & SS_DETECT)
		return sprintf(buf, "16-bit\n");
	return sprintf(buf, "invalid\n");
}
static CLASS_DEVICE_ATTR(card_type, 0400, pccard_show_type, NULL);

static struct class_device_attribute *pccard_socket_attributes[] = {
	&class_device_attr_card_type,
	NULL,
};

int pccard_sysfs_init(struct pcmcia_socket *s)
{
	struct class_device_attribute *attr;
	unsigned int i;
	int ret = 0;
        for (i = 0; (attr = pccard_socket_attributes[i]); i++) {
                if ((ret = class_device_create_file(&s->dev, attr)))
			return (ret);
        }
	return (ret);
}
