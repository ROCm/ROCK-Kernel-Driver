#ifndef _SERIO_H
#define _SERIO_H

/*
 * Copyright (C) 1999-2002 Vojtech Pavlik
*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/ioctl.h>
#include <linux/interrupt.h>

#define SPIOCSTYPE	_IOW('q', 0x01, unsigned long)

#ifdef __KERNEL__

#include <linux/list.h>

struct serio {
	void *private;
	void *driver;
	char *name;
	char *phys;

	unsigned short idbus;
	unsigned short idvendor;
	unsigned short idproduct;
	unsigned short idversion;

	unsigned long type;
	unsigned long event;

	int (*write)(struct serio *, unsigned char);
	int (*open)(struct serio *);
	void (*close)(struct serio *);

	struct serio_dev *dev;

	struct list_head node;
};

struct serio_dev {
	void *private;
	char *name;

	void (*write_wakeup)(struct serio *);
	irqreturn_t (*interrupt)(struct serio *, unsigned char,
			unsigned int, struct pt_regs *);
	void (*connect)(struct serio *, struct serio_dev *dev);
	int  (*reconnect)(struct serio *);
	void (*disconnect)(struct serio *);
	void (*cleanup)(struct serio *);

	struct list_head node;
};

int serio_open(struct serio *serio, struct serio_dev *dev);
void serio_close(struct serio *serio);
void serio_rescan(struct serio *serio);
void serio_reconnect(struct serio *serio);
irqreturn_t serio_interrupt(struct serio *serio, unsigned char data, unsigned int flags, struct pt_regs *regs);

void serio_register_port(struct serio *serio);
void serio_register_port_delayed(struct serio *serio);
void __serio_register_port(struct serio *serio);
void serio_unregister_port(struct serio *serio);
void serio_unregister_port_delayed(struct serio *serio);
void __serio_unregister_port(struct serio *serio);
void serio_register_device(struct serio_dev *dev);
void serio_unregister_device(struct serio_dev *dev);

static __inline__ int serio_write(struct serio *serio, unsigned char data)
{
	if (serio->write)
		return serio->write(serio, data);
	else
		return -1;
}

static __inline__ void serio_dev_write_wakeup(struct serio *serio)
{
	if (serio->dev && serio->dev->write_wakeup)
		serio->dev->write_wakeup(serio);
}

static __inline__ void serio_cleanup(struct serio *serio)
{
	if (serio->dev && serio->dev->cleanup)
		serio->dev->cleanup(serio);
}

#endif

/*
 * bit masks for use in "interrupt" flags (3rd argument)
 */
#define SERIO_TIMEOUT	1
#define SERIO_PARITY	2
#define SERIO_FRAME	4

#define SERIO_TYPE	0xff000000UL
#define SERIO_XT	0x00000000UL
#define SERIO_8042	0x01000000UL
#define SERIO_RS232	0x02000000UL
#define SERIO_HIL_MLC	0x03000000UL
#define SERIO_PS_PSTHRU	0x05000000UL
#define SERIO_8042_XL	0x06000000UL

#define SERIO_PROTO	0xFFUL
#define SERIO_MSC	0x01
#define SERIO_SUN	0x02
#define SERIO_MS	0x03
#define SERIO_MP	0x04
#define SERIO_MZ	0x05
#define SERIO_MZP	0x06
#define SERIO_MZPP	0x07
#define SERIO_VSXXXAA	0x08
#define SERIO_SUNKBD	0x10
#define SERIO_WARRIOR	0x18
#define SERIO_SPACEORB	0x19
#define SERIO_MAGELLAN	0x1a
#define SERIO_SPACEBALL	0x1b
#define SERIO_GUNZE	0x1c
#define SERIO_IFORCE	0x1d
#define SERIO_STINGER	0x1e
#define SERIO_NEWTON	0x1f
#define SERIO_STOWAWAY	0x20
#define SERIO_H3600	0x21
#define SERIO_PS2SER	0x22
#define SERIO_TWIDKBD	0x23
#define SERIO_TWIDJOY	0x24
#define SERIO_HIL	0x25
#define SERIO_SNES232	0x26
#define SERIO_SEMTECH	0x27
#define SERIO_LKKBD	0x28

#define SERIO_ID	0xff00UL
#define SERIO_EXTRA	0xff0000UL

#endif
