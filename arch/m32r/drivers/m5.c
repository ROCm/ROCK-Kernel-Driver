/*
 * Flash Memory Driver for M32700UT-CPU
 *
 * [support chips]
 *  	- M5M29GT320VP
 *
 * Copyright (C) 2003   Takeo Takahashi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * 2003-02-01: Takeo Takahashi, support M5M29GT320VP page program.
 *
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

#include <linux/config.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioctl.h>
#include <linux/blkpg.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/m32r.h>
#include <asm/io.h>

#include "m5.h"

static const char version[] = "M5 Flash Driver";
static const char date[] = __DATE__;
static const char time[] = __TIME__;

/*
 * Special function
 */
#define M5_SUPPORT_PROBE	1
#define M5_MAX_ERROR		5

/*
 * flash memory start address:
 */
//#define M5_BASE_ADDR         (0x00000000 + NONCACHE_OFFSET)
#define M5_BASE_ADDR         (0x00000000 + PAGE_OFFSET)
#define M5_IDENT_ADDR        (M5_BASE_ADDR + 0)

/*
 * This driver does not have a real partition block, but
 * it can support simulation of partition in single chip.
 */
#define M5_PARTITIONS        (3)
static int m5_len[M5_PARTITIONS] = {
	192 * 1024,	/* 192kB:	0x00000000 - 0x0002ffff */
	1088 * 1024,	/* 1088kB:	0x00030000 - 0x0013ffff */
	2816 * 1024	/* 2816kB:	0x00140000 - 0x003fffff */
};

static int major = 240;
static int debug = 2;
static int led=0;
static int m5_addr[M5_PARTITIONS];
static int m5_blk_size[M5_PARTITIONS];
static int m5_blksize_size[M5_PARTITIONS];
static int m5_hardsect_size[M5_PARTITIONS];
static int m5_read_ahead = 1;
MODULE_PARM(major, "i");
//MODULE_PARM_DESC(major, "major number");
MODULE_PARM(debug, "i");
//MODULE_PARM_DESC(debug, "debug flag");
MODULE_PARM(led, "i");
//MODULE_PARM_DESC(led, "LED2 support");

static devfs_handle_t	devfs_handle = NULL;
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_m5;
#endif

#define MAJOR_NR		major
#define DEVICE_NAME 		"m5"
#define DEVICE_NR(device) 	MINOR(device)
#define DEVICE_NO_RANDOM
#define DEVICE_OFF(device)
#define DEVICE_ON(device)
#define DEVICE_REQUEST 		m5_request
#include <linux/blk.h>

#define WAIT_TIME	10			/* 10 usec */
#define WAIT_COUNT	(50000/WAIT_TIME)	/* 50ms = 10us*5000times */

#define SUCCESS 0
#define FAILED  (-1)

/*
 * definitions for cache
 */
static struct cache {
	int blk;	/* block number, -1:invalid */
	int status;	/* cache status */
#define B_CLEAN	0
#define B_DIRTY	1
	m5_t *addr;	/* block base address */
	m5_t cache[M5_BLOCK_SIZE64];
} m5_cache;
#define M5_BLK_NOCACHED()		(m5_cache.blk == -1)
#define M5_BLK_CACHED(blk)		(m5_cache.blk == (blk))
#define M5_STATE_DIRTY()		(m5_cache.status == B_DIRTY)
#define M5_STATE_CLEAN()		(m5_cache.status == B_CLEAN)
#define M5_MARK_DIRTY()			(m5_cache.status = B_DIRTY)
#define M5_MARK_CLEAN()			(m5_cache.status = B_CLEAN)
#define M5_MARK_NOCACHED()		(m5_cache.blk = -1)
#define M5_MARK_CACHED(blk, addr)	(m5_cache.blk = (blk), \
					 m5_cache.addr = (addr))
#define M5_CACHED_BLK			m5_cache.blk
#define M5_CACHED_ADDR			m5_cache.addr
#define M5_CACHED_BASE			m5_cache.cache

/*
 * Prototype declarations
 */
static int m5_erase_blk(m5_t *reg);
static void m5_led_toggle(void);
static void m5_led_on(void);
static void m5_led_off(void);
static m5_t m5_in(m5_t *addr);
static void m5_out(m5_t val, m5_t *addr);
static int m5_probe(m5_t *addr);
static int m5_get_blk_num(m5_t *addr);
static int m5_get_blk_offset(int blk, m5_t *addr);
static int m5_get_blk_size(int blk);
static int m5_read_blk_cache(m5_t *addr);
static int m5_write_blk_cache(void);
static int m5_full_status_check(m5_t status, m5_t *reg);
static void m5_clear_status(m5_t *reg);
#if 0
static int m5_status_check(m5_t *reg);
#endif
static int m5_wait_for_ready(m5_t *reg);
static int m5_write_page(m5_t *addr, const m5_t *buf);
static int m5_write(const m5_t *buf, int offset, int len);
static int m5_read_page(m5_t *buf, m5_t *addr);
static int m5_read(m5_t *buf, int offset, int len);
static int m5_erase_blk(m5_t *addr);
static void m5_request(request_queue_t *req);
static int m5_ioctl(struct inode *inode, struct file *fp, unsigned int cmd, unsigned long arg);
static int m5_open(struct inode *inode, struct file *fp);
static int m5_release(struct inode *inode, struct file *fp);
static int proc_m5_read(char *buf, char **start, off_t offset, int len, int *eof, void *unused);
static int __init m5_init(void);
static int __init m5_init_module(void);
static void __exit m5_cleanup_module(void);

/*
 * special function
 */
static inline void m5_led_toggle(void)
{
	unsigned short status;
	if (!led) return;
	status = inw((unsigned long)PLD_IOLEDCR);
	if (status & PLD_IOLED_2_ON)
		status &= ~PLD_IOLED_2_ON;
	else
		status |= PLD_IOLED_2_ON;
	outw(status, (unsigned long)PLD_IOLEDCR);
}

static inline void m5_led_on(void)
{
	unsigned short status;
	if (!led) return;
	status = inw((unsigned long)PLD_IOLEDCR);
	status |= PLD_IOLED_2_ON;
	outw(status, (unsigned long)PLD_IOLEDCR);
}

static inline void m5_led_off(void)
{
	unsigned short status;
	if (!led) return;
	status = inw((unsigned long)PLD_IOLEDCR);
	status &= ~PLD_IOLED_2_ON;
	outw(status, (unsigned long)PLD_IOLEDCR);
}

/*
 * I/O function
 */
static inline m5_t m5_in(m5_t *addr)
{
	return *addr;
}

static inline void m5_out(m5_t val, m5_t *addr)
{
	*addr = val;
}

#if M5_SUPPORT_PROBE
/*
 * int m5_probe()
 *	m5_t *addr:	probed address
 * 	int SUCCESS: 	supported device
 *	int FAILED: 	unsupported device
 */
static int m5_probe(m5_t *addr)
{
#if 1
	/*
	 * FIXME: cannot read maker & device codes.
	 */
	m5_t val;
	m5_out(M5_CMD_DEVICE_IDENT, addr);
	val = m5_in((m5_t *)M5_IDENT_ADDR);
	val &= 0xff;
	if (val == M5_MAKER)
		printk("m5: detected M5M29GT320VP\n");
	else
		printk("m5: unknown maker(0x%02x)\n", val);
	m5_out(M5_CMD_READ_ARRAY, addr);	/* array mode */
	/* always success */
	return SUCCESS;
#else

	int result;
	m5_t val;
	unsigned char maker, device;

	result = SUCCESS;
	m5_out(M5_CMD_DEVICE_IDENT, addr);
	val = m5_in((m5_t *)M5_IDENT_ADDR);
	maker = (val >> 16);
	device = (val & 0xff);
	if (maker != M5_MAKER) {
		printk("m5: unknown maker(0x%02x)\n", maker);
		// result = FAILED;
		// ignore error
	}
 	if (device != M5_M5M29GT320VP) {
		printk("m5: unknown device(0x%02x)\n", device);
		// result = FAILED;
		// ignore error
	}
	if (result != FAILED)
		printk("m5: detected M5M29GT320VP\n");
	m5_out(M5_CMD_READ_ARRAY, addr);
	return result;
#endif
}
#endif	/* M5_SUPPORT_PROBE */

/*
 * int m5_get_blk_num()
 *	m5_t *addr:
 *	blk:	OK, return block number
 *	-1:	NG
 */
static int m5_get_blk_num(m5_t *addr)
{
	int blk;

	blk = (((int)addr & 0x0fff0000) >> 16);
	if (blk > 0x3f) {
		printk("m5: out of block address (0x%08x)\n", (int)addr);
		return -1;
	}
	if (blk == 0x3f)
		blk += (int)(((int)addr & 0x0000e000) >> 13);
	if (blk > MAX_BLOCK_NUM) {
		printk("m5: out of block address (addr=0x%08x, blk=%d)\n",
				(int)addr, blk);
		return -1;
	}
	return blk;
}

/*
 * m5_t *m5_get_blk_base()
 *	return block base address
 */
static inline m5_t *m5_get_blk_base(int blk, m5_t *addr)
{
	if (blk < 0x3f) return (m5_t *)((int)addr & 0xffff0000);
	return (m5_t *)((int)addr & 0xffffe000);
}

/*
 * int m5_get_blk_offset()
 *	return offset of specified address in blk
 */
static inline int m5_get_blk_offset(int blk, m5_t *addr)
{
	if (blk < 0x3f) return ((int)addr & 0xffff);
	return ((int)addr & 0x1fff);
}

/*
 * int m5_get_blk_size()
 *	return block size in byte
 */
static inline int m5_get_blk_size(int blk)
{
	if (blk >= 63) return M5_BLOCK_SIZE8;
	return M5_BLOCK_SIZE64;
}

/*
 * cache operations
 */
static int m5_read_blk_cache(m5_t *addr)
{
	int blk;
	int size;
	int result;

	result = SUCCESS;
	blk = m5_get_blk_num(addr);
	if (blk < 0) return FAILED;
	if (M5_BLK_CACHED(blk))
		return result;
	if (M5_STATE_DIRTY()) {		/* cached other block */
		result = m5_write_blk_cache();
		if (result != SUCCESS) return result;
	}
	/* ok, we can use cache */
	size = m5_get_blk_size(blk);
	addr = m5_get_blk_base(blk, addr);
	memcpy((void *)M5_CACHED_BASE, (void *)addr, size);
	M5_MARK_CACHED(blk, addr);
	M5_MARK_CLEAN();
	return result;
}

static int m5_write_blk_cache(void)
{
	int size;
	int pages;
	int i;
	m5_t *reg, *addr, *buf;
	int result;

	result = SUCCESS;

	if (M5_BLK_NOCACHED())
		return result;
	if (! M5_STATE_DIRTY())
		return result;

	/* we have to write cache */
	m5_led_on();
	size = m5_get_blk_size(M5_CACHED_BLK);
	addr = M5_CACHED_ADDR;
	buf = M5_CACHED_BASE;
	reg = addr;

	result = m5_erase_blk(reg);
	if (result != SUCCESS) return result;

	DEBUG(1, "m5: wrting addr=0x%08x, buf=0x%08x, size=%d\n",\
		(int)addr, (int)buf, size);
	for (pages = 0; pages < (size/M5_PAGE_SIZE); pages++) {
		m5_out(M5_CMD_PROGRAM_PAGE, reg);
		for (i = 0; i < (M5_PAGE_SIZE/sizeof(m5_t)); i++)
			*addr++ = *buf++;
		if (m5_wait_for_ready(reg) != SUCCESS)
			result = FAILED;
		reg = addr;
	}
	m5_out(M5_CMD_READ_ARRAY, reg);	/* array mode */
	if (result == SUCCESS)
		M5_MARK_CLEAN();
	m5_led_off();
	return result;	/* SUCCESS or FAILED */
}

/*
 * int m5_full_status_check()
 *	m5_t status:	status
 *	m5_t *reg:	bank address
 *	SUCCESS:	no error
 *	FAILED:		error happen
 */
static inline int m5_full_status_check(m5_t status, m5_t *reg)
{
	if ((status & M5_STATUS_PROGRAM) &&
	    (status & M5_STATUS_ERASE)) {
		printk("m5: command sequence error (addr=0x%08x, sts=0x%02x).\n",
			(int)reg, status);
		return FAILED;
	}
   	if(status & M5_STATUS_ERASE){
      		printk("m5: erase error (addr=0x%08x, sts=0x%02x).\n",
			(int)reg, status);
      		return FAILED;
   	}
   	if(status & M5_STATUS_PROGRAM){
      		printk("m5: program write error (addr=0x%08x, sts=0x%02x).\n",
			(int)reg, status);
      		return FAILED;
   	}
   	if(status & M5_STATUS_BLOCK){
      		printk("m5: block write error (addr=0x%08x, sts=0x%02x).\n",
			(int)reg, status);
      		return FAILED;
   	}
	/* passed */
   	return SUCCESS;
}

/*
 * void m5_clear_status()
 *	m5_t *reg:	address of status register
 */
static inline void m5_clear_status(m5_t *reg)
{
	m5_out(M5_CMD_CLEAR_STATUS, reg);
}

#if 0
/*
 * int m5_status_check()
 *	m5_t *reg:	address of command register
 *	SUCCESS:	ready now
 *	FAILED:		error happen while waiting
 */
static int m5_status_check(m5_t *reg)
{
	m5_t status;
	int result;
	int n;
	static int error_count = 0;

	m5_out(M5_CMD_READ_STATUS, reg);
	for (n = WAIT_COUNT; n > 0; n--) {
		status = m5_in(reg);
		if (status & M5_STATUS_READY)
			break;
		udelay(WAIT_TIME);
	}
	if (n <= 0) {
		if (error_count++ < M5_MAX_ERROR)
			printk("m5: time out (sts=0x%02x).\n", status);
		m5_clear_status(reg);
		return FAILED;
	}
	result = m5_full_status_check(status, reg);
	if (result != SUCCESS) {
		m5_clear_status(reg);
		return FAILED;
	}
	m5_clear_status(reg);
	return SUCCESS;
}
#endif

/*
 * int m5_wait_for_ready()
 *	m5_t *reg:	address of command register
 *	SUCCESS:	ready now
 *	FAILED:		error happen while waiting
 */
static int m5_wait_for_ready(m5_t *reg)
{
	m5_t status;
	int result;
	int n;
	static int error_count = 0;

	for (n = WAIT_COUNT; n > 0; n--) {
		status = m5_in(reg);
		if (status & M5_STATUS_READY)
			break;
		udelay(WAIT_TIME);
	}
	if (n <= 0) {
		if (error_count++ < M5_MAX_ERROR)
			printk("m5: time out (sts=0x%02x).\n", status);
		m5_clear_status(reg);
		return FAILED;
	}
	result = m5_full_status_check(status, reg);
	if (result != SUCCESS) {
		m5_clear_status(reg);
		return FAILED;
	}
	return SUCCESS;
}

/*
 * int m5_write_page(m5_t *addr, const m5_t *buf)
 *	m5_t *addr:	sector address
 *	m5_t *buf:	buffer address
 *	SUCCESS:	ok
 *	FAILED:		error
 */
static int m5_write_page(m5_t *addr, const m5_t *buf)
{
	int blk;
	int offset;

	if (((int)addr % M5_PAGE_SIZE) != 0)
		printk("m5: invalid sector addres (0x%08x)\n", (int)addr);

	if (m5_read_blk_cache(addr) != SUCCESS)
		return FAILED;
	if ((blk = m5_get_blk_num(addr)) < 0)
		return FAILED;
	offset = m5_get_blk_offset(blk, addr);
	memcpy((void *)M5_CACHED_BASE + offset, (void *)buf, M5_PAGE_SIZE);
	M5_MARK_DIRTY();
	return SUCCESS;
}

/*
 * int m5_write(const m5_t *buf, int offset, int len)
 *	m5_t *buf:	write buffer address
 *	int offset:	offset from flash base address
 *	int len:	write length
 *	SUCCESS:	ok
 *	FAILE:		error
 */
static int m5_write(const m5_t *buf, int offset, int len)
{
	m5_t *addr;
	int result;

	if (len % M5_PAGE_SIZE) {
		printk("m5: invalid write size (%d).\n", len);
		return FAILED;
	}
	addr = (m5_t *)(m5_addr[MINOR(CURRENT_DEV)] + offset);

	DEBUG(1, "m5: writing 0x%08x - 0x%08x\n", \
		(int)addr, (int)addr + len);

	for (; len > 0; len -= M5_PAGE_SIZE) {
		result = m5_write_page(addr, buf);
		if (result != SUCCESS)
			return result;
		addr = (m5_t *)((int)addr + M5_PAGE_SIZE);
		buf = (m5_t *)((int)buf + M5_PAGE_SIZE);
	}
	return SUCCESS;
}

/*
 * int m5_read_page()
 *	m5_t *buf:	read buffer address
 *	m5_t *addr:	page address
 *	SUCCESS:	ok
 *	FAILED:		error
 */
static int m5_read_page(m5_t *buf, m5_t *addr)
{
	int blk;
	int offset;

	if ((blk = m5_get_blk_num(addr)) < 0)
		return FAILED;
	if (M5_BLK_CACHED(blk)) {
		offset = m5_get_blk_offset(blk, addr);
		memcpy((void *)buf, (void *)M5_CACHED_BASE + offset,
				M5_PAGE_SIZE);
	} else {
		/* read from flash memory directory */
		memcpy((void *)buf, (void *)addr, M5_PAGE_SIZE);
	}
	return SUCCESS;
}

/*
 * int m5_read()
 *	m5_t *buf:		read buffer address
 *	int offset:		offset from flash base address
 *	int len:		read length
 *	SUCCESS:		ok
 *	FAILED:			error
 */
static int m5_read(m5_t *buf, int offset, int len)
{
	m5_t *addr;

	if (len % M5_PAGE_SIZE) {
		printk("m5: invalid read size (%d).\n", len);
		return FAILED;
	}

	addr = (m5_t *)(m5_addr[MINOR(CURRENT_DEV)] + offset);

	DEBUG(1, "m5: reading 0x%08x - 0x%08x\n", \
		(int)addr, (int)addr + len);

	for (; len > 0; len -= M5_PAGE_SIZE) {
		if (m5_read_page(buf, addr) != SUCCESS)
			return FAILED;
		buf = (m5_t *)((int)buf + M5_PAGE_SIZE);
		addr = (m5_t *)((int)addr + M5_PAGE_SIZE);
	}
	return SUCCESS;
}

/*
 * int m5_erase_blk()
 *	m5_t *addr:
 */
static int m5_erase_blk(m5_t *addr)
{
	int result;

	DEBUG(1, "m5: erasing addr=0x%08x\n", (int)addr);
	m5_out(M5_CMD_BLOCK_ERASE, addr);
	m5_out(M5_CMD_CONFIRM, addr);
	result = m5_wait_for_ready(addr);
	return result;
}

/*
 * void m5_request()
 *	request_queue_t *req:	I/O request
 *	end_request(0):	error (-EIO)
 *	end_request(1): ok
 */
static void m5_request(request_queue_t *req)
{
    	unsigned int minor;
    	int offset;
	int len;
	static int error_count = 0;

	sti();
	while (1) {
		INIT_REQUEST;
		minor = MINOR(CURRENT_DEV);
		if (minor >= M5_PARTITIONS) {
	    		printk( "m5: out of partition (%d)\n", minor );
	    		end_request(0);
	    		continue;
		}
		offset = CURRENT->sector * m5_hardsect_size[minor];
		len = (CURRENT->current_nr_sectors *
			m5_hardsect_size[minor]);
		if ((offset + len) > m5_len[minor]) {
	    		printk( "m5: out of sector (sector=%d, nr=%d)\n",
				(int)(CURRENT->sector),
				(int)(CURRENT->current_nr_sectors));
	    		end_request(0);
	    		continue;
		}
		switch(CURRENT->cmd) {
	    	case READ:
			if (m5_read((m5_t *)CURRENT->buffer,
					offset, len) != SUCCESS)
		    		end_request(0);
			else
		    		end_request(1);
			break;
	    	case WRITE:
			if (m5_write((m5_t *)(CURRENT->buffer),
					offset, len) != SUCCESS)
		    		end_request(0);
			else
		    		end_request(1);
                	break;
	    	default:
			if (error_count++ < M5_MAX_ERROR) {
				printk("m5: invalid I/O request(%d)\n",
					CURRENT->cmd);
			}
			end_request(0);
			break;
		}
	}
}

/*
 * int m5_ioctl()
 *	struct inode *inode:
 *	struct file *file:
 *	unsigned int cmd:
 *	unsigned long arg:
 */
static int m5_ioctl(struct inode *inode, struct file *fp, unsigned int cmd, unsigned long arg)
{
	int size;

	DEBUG(2, "m5_ioctl()\n");
    	switch (cmd) {
      	case BLKGETSIZE:
		if (!arg) return -EINVAL;
		size = (1024 * m5_blk_size[MINOR(inode->i_rdev)] /
			m5_hardsect_size[MINOR(inode->i_rdev)]);
		return put_user(size, (long __user *)arg);
      	case BLKFLSBUF:
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		return 0;
	case BLKRRPART:
		return -EINVAL;

	case BLKRAGET:
	case BLKRASET:
	case BLKGETSIZE64:
	case BLKROSET:
	case BLKROGET:
      	case BLKSSZGET:
		return blk_ioctl(inode->i_rdev, cmd, arg);
      	default:
		printk( "m5: unsupported ioctl(0x%08x)\n", cmd);
		return -EINVAL;
    	}
    	return 0;
}

/*
 * int m5_open()
 *	struct inode *inode:
 *	struct file *fp:
 */
static int m5_open(struct inode *inode, struct file *fp)
{
    	if (DEVICE_NR(inode->i_rdev) >= M5_PARTITIONS) return -ENXIO;
    	MOD_INC_USE_COUNT;
    	return 0;
}

/*
 * int m5_release()
 *	struct inode *inode:
 *	struct file *fp:
 */
static int m5_release(struct inode *inode, struct file *fp)
{
    	sync_dev(inode->i_rdev);
    	MOD_DEC_USE_COUNT;
    	return 0;
}

#ifdef CONFIG_PROC_FS
/*
 * int proc_m5_read()
 *	char *buf:
 *	char **start:
 *	off_t offset:
 *	int len:
 *	int *eof:
 *	void *unused:
 */
static int proc_m5_read(char *buf, char **start, off_t offset, int len, int *eof, void *unused)
{
	len = sprintf(buf, "partition: %d\n", M5_PARTITIONS);
	return len;
}
#endif

static struct block_device_operations m5_fops = {
	open:		m5_open,
	release:	m5_release,
	ioctl:		m5_ioctl,
	/* check_media_change: */
	/* revalidate: */
	owner:		THIS_MODULE,
};

/*
 * int m5_init()
 */
static int __init m5_init(void)
{
	int i;
	int result;

	result = -EIO;

	printk("%s (%s %s)\n", version, date, time);
#if M5_SUPPORT_PROBE
	if (m5_probe((m5_t *)M5_BASE_ADDR) != SUCCESS)
		return result;
#endif	/* M5_SUPPORT_PROBE */

	if (register_blkdev(major, DEVICE_NAME, &m5_fops) ) {
		printk("m5: can not not get major %d", major);
		return result;
	}

	if (devfs_register_blkdev(major, DEVICE_NAME, &m5_fops) ) {
		printk("m5: can not not get major %d", major);
		goto fail_devfs;
	}
	devfs_handle = devfs_mk_dir(NULL, "m5", NULL);
	devfs_register_series(devfs_handle, "%u", M5_PARTITIONS,
			      DEVFS_FL_DEFAULT, major, 0,
			      S_IFBLK | S_IRUSR | S_IWUSR,
			      &m5_fops, NULL);

	for (i = 0; i < M5_PARTITIONS; i++) {
		if (i) {
			m5_addr[i] = m5_addr[i-1] + m5_len[i-1];
		} else {
			m5_addr[i] = M5_BASE_ADDR;
		}
		m5_blk_size[i] = m5_len[i]/1024;	/* KB order */
		m5_blksize_size[i] = BLOCK_SIZE;  	/* 1024 byte */
		m5_hardsect_size[i] = BLOCK_SIZE/2;	/* 512 byte */
	}
	/* defined in ll_rw_blk.c */
	blk_size[major] = m5_blk_size;
	blksize_size[major] = m5_blksize_size;
	hardsect_size[major] = m5_hardsect_size;
	read_ahead[major] = m5_read_ahead;

	blk_init_queue(BLK_DEFAULT_QUEUE(major), &m5_request);

	for (i = 0; i < M5_PARTITIONS; i++)
		register_disk(  NULL, MKDEV(major,i), 1,
				&m5_fops, m5_len[i]>>9 );

#ifdef CONFIG_PROC_FS
#if	1
	proc_m5 = proc_mkdir("m5", proc_root_driver);
	if (!proc_m5) {
		printk(KERN_ERR "m5: unable to register driver/m5\n");
		goto fail_proc;
	}
	create_proc_read_entry("0", 0, proc_m5, proc_m5_read, 0);
	create_proc_read_entry("1", 0, proc_m5, proc_m5_read, 0);
	create_proc_read_entry("2", 0, proc_m5, proc_m5_read, 0);
#else
	proc_m5 = create_proc_entry("driver/m5", 666, 0);
	if (!proc_m5) {
		printk(KERN_ERR "m5: unable to register driver/m5\n");
		goto fail_proc;
	}
	proc_m5->read_proc = proc_m5_read;
#endif
#endif

	printk("m5: Major=%d Partitions=%d\n", major,M5_PARTITIONS);
	for (i = 0; i < M5_PARTITIONS; i++) {
	printk("  %d: 0x%08x-0x%08x (all=%dKB,blk=%dB,sect=%dB,ahead=%d)\n",
	        i, m5_addr[i],
		m5_addr[i] + m5_len[i],
		m5_blk_size[i],
		m5_blksize_size[i],
		m5_hardsect_size[i],
		m5_read_ahead);
	}

	/* clear cache */
	M5_MARK_CLEAN();
	M5_MARK_NOCACHED();

	m5_led_off();
	return 0;

fail_proc:
	devfs_unregister(devfs_handle);
fail_devfs:
	unregister_blkdev(major, DEVICE_NAME);
	return result;
}

static int __init m5_init_module(void)
{
    return m5_init();
}

static void __exit m5_cleanup_module(void)
{
    	int i;

    	for (i = 0 ; i < M5_PARTITIONS; i++) {
		fsync_dev(MKDEV(major, i));	/* flush buffer */
      		destroy_buffers(MKDEV(major, i));
	}
	m5_write_blk_cache();		/* flush m5 cache */
    	devfs_unregister(devfs_handle);
    	unregister_blkdev(major, DEVICE_NAME);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("0", proc_m5);
	remove_proc_entry("1", proc_m5);
	remove_proc_entry("2", proc_m5);
	remove_proc_entry("m5", proc_root_driver);
#endif
    	blk_cleanup_queue(BLK_DEFAULT_QUEUE(major));
	blk_size[major] = NULL;
	blksize_size[major] = NULL;
	hardsect_size[major] = NULL;
	read_ahead[major] = 0;
}

module_init(m5_init_module);
module_exit(m5_cleanup_module);

MODULE_AUTHOR("Takeo Takahashi");
MODULE_DESCRIPTION("M5 Flash Driver for M32700UT-CPU");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;

