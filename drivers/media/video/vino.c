/* $Id: vino.c,v 1.5 1999/10/09 00:01:14 ralf Exp $
 * drivers/char/vino.c
 *
 * (incomplete) Driver for the Vino Video input system found in SGI Indys.
 *
 * Copyright (C) 1999 Ulf Carlsson (ulfc@bun.falkenberg.se)
 *
 * This isn't complete yet, please don't expect any video until I've written
 * some more code.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/videodev.h>

#include <asm/addrspace.h>
#include <asm/system.h>

#include "vino.h"

struct vino_device {
	struct video_device vdev;

	unsigned long chan;
#define VINO_CHAN_A		0
#define VINO_CHAN_B		1

	unsigned long flags;
#define VINO_DMA_ACTIVE		(1<<0)
};

/* We can actually receive TV and IndyCam input at the same time. Believe it or
 * not..
 */
static struct vino_device vino[2];

/* Those registers have to be accessed by either *one* 64 bit write or *one* 64
 * bit read. We need some asm to fix this. We can't use mips3 as standard
 * because we just save 32 bits at context switch.
 */

static __inline__ unsigned long long vino_reg_read(unsigned long addr)
{
	unsigned long long ret __attribute__ ((aligned (64)));
	unsigned long virt_addr = KSEG1ADDR(addr + VINO_BASE);
	unsigned long flags;

	save_and_cli(flags);
	__asm__ __volatile__(
		".set\tmips3\n\t"
		".set\tnoat\n\t"
		"ld\t$1,(%0)\n\t"
		"sd\t$1,(%1)\n\t"
		".set\tat\n\t"
		".set\tmips0"
		:
		:"r" (virt_addr),
		 "r" (&ret)
		:"$1");
	restore_flags(flags);

	return ret;
}

static __inline__ void vino_reg_write(unsigned long long value,
				      unsigned long addr)
{
	unsigned long virt_addr = KSEG1ADDR(addr + VINO_BASE);
	unsigned long flags;

	/* we might lose the upper parts of the registers which are not saved
	 * if there comes an interrupt in our way, play safe */

	save_and_cli(flags);
	__asm__ __volatile__(
		".set\tmips3\n\t"
		".set\tnoat\n\t"
		"ld\t$1,(%0)\n\t"
		"sd\t$1,(%1)\n\t"
		".set\tat\n\t"
		".set\tmips0"
		:
		:"r" (&value),
		 "r" (virt_addr)
		:"$1");
	restore_flags(flags);
}

static __inline__ void vino_reg_and(unsigned long long value,
				    unsigned long addr)
{
	unsigned long virt_addr = KSEG1ADDR(addr + VINO_BASE);
	unsigned long flags;

	save_and_cli(flags);
	__asm__ __volatile__(
		".set\tmips3\n\t"
		".set\tnoat\n\t"
		"ld\t$1,(%0)\n\t"
		"ld\t$2,(%1)\n\t"
		"and\t$1,$1,$2\n\t"
		"sd\t$1,(%0)\n\t"
		".set\tat\n\t"
		".set\tmips0"
		:
		:"r" (virt_addr),
		 "r" (&value)
		:"$1","$2");
	restore_flags(flags);
}

static __inline__ void vino_reg_or(unsigned long long value,
				   unsigned long addr)
{
	unsigned long virt_addr = KSEG1ADDR(addr + VINO_BASE);
	unsigned long flags;

	save_and_cli(flags);
	__asm__ __volatile__(
		".set\tmips3\n\t"
		".set\tnoat\n\t"
		"ld\t$1,(%0)\n\t"
		"ld\t$2,(%1)\n\t"
		"or\t$1,$1,$2\n\t"
		"sd\t$1,(%0)\n\t"
		".set\tat\n\t"
		".set\tmips0"
		:
		:"r" (virt_addr),
		 "r" (&value)
		:"$1","$2");
	restore_flags(flags);
}

static int vino_dma_setup(void)
{
	return 0;
}

static void vino_dma_stop(void)
{

}

static int vino_init(void)
{
	unsigned long ret;
	unsigned short rev, id;
	unsigned long long foo;
	unsigned long *bar;

	bar = (unsigned long *) &foo;

	ret = vino_reg_read(VINO_REVID);

	rev = (ret & VINO_REVID_REV_MASK);
	id = (ret & VINO_REVID_ID_MASK) >> 4;

	printk("Vino: ID:%02hx Rev:%02hx\n", id, rev);

	foo = vino_reg_read(VINO_A_DESC_DATA0);
	printk("0x%lx", bar[0]);
	printk("%lx ", bar[1]);
	foo = vino_reg_read(VINO_A_DESC_DATA1);
	printk("0x%lx", bar[0]);
	printk("%lx ", bar[1]);
	foo = vino_reg_read(VINO_A_DESC_DATA2);
	printk("0x%lx", bar[0]);
	printk("%lx ", bar[1]);
	foo = vino_reg_read(VINO_A_DESC_DATA3);
	printk("0x%lx", bar[0]);
	printk("%lx\n", bar[1]);
	foo = vino_reg_read(VINO_B_DESC_DATA0);
	printk("0x%lx", bar[0]);
	printk("%lx ", bar[1]);
	foo = vino_reg_read(VINO_B_DESC_DATA1);
	printk("0x%lx", bar[0]);
	printk("%lx ", bar[1]);
	foo = vino_reg_read(VINO_B_DESC_DATA2);
	printk("0x%lx", bar[0]);
	printk("%lx ", bar[1]);
	foo = vino_reg_read(VINO_B_DESC_DATA3);
	printk("0x%lx", bar[0]);
	printk("%lx\n", bar[1]);

	return 0;
}

static void vino_dma_go(struct vino_device *v)
{
	
}

/* Reset the vino back to default state */

static void vino_setup(struct vino_device *v)
{
	
}

static int vino_open(struct video_device *dev, int flags)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static void vino_close(struct video_device *dev)
{
	MOD_DEC_USE_COUNT;
}

static int vino_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	return 0;
}

static int vino_mmap(struct video_device *dev, const char *adr,
		     unsigned long size)
{
	return 0;
}

static struct video_device vino_dev = {
	name:		"Vino IndyCam/TV",
	type:		VID_TYPE_CAPTURE,
	hardware:	VID_HARDWARE_VINO,
	open:		vino_open,
	close:		vino_close,
	ioctl:		vino_ioctl,
	mmap:		vino_mmap,
};

int __init init_vino(struct video_device *dev)
{
	int err;

	err = vino_init();
	if (err)
		return err;

#if 0
	if (video_register_device(&vinodev, VFL_TYPE_GRABBER) == -1) {
		return -ENODEV;
	}
#endif

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	int err;

	err = vino_init();
	if (err)
		return err;

	return 0;
}

void cleanup_module(void)
{
}
#endif
