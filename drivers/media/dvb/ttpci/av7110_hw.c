/*
 * av7110_hw.c: av7110 low level hardware access and firmware interface
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

/* for debugging ARM communication: */
//#define COM_DEBUG

#include <stdarg.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/byteorder/swabb.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>

#define DEBUG_VARIABLE av7110_debug
extern int av7110_debug;

#include "av7110.h"
#include "av7110_hw.h"
#include "dvb_functions.h"

/****************************************************************************
 * DEBI functions
 ****************************************************************************/

/* This DEBI code is based on the Stradis driver
   by Nathan Laredo <laredo@gnu.org> */

int av7110_debiwrite(struct av7110 *av7110, u32 config,
		     int addr, u32 val, int count)
{
	struct saa7146_dev *dev = av7110->dev;

	if (count <= 0 || count > 32764)
		return -1;
	if (saa7146_wait_for_debi_done(av7110->dev) < 0)
		return -1;
	saa7146_write(dev, DEBI_CONFIG, config);
	if (count <= 4)		/* immediate transfer */
		saa7146_write(dev, DEBI_AD, val);
	else			/* block transfer */
		saa7146_write(dev, DEBI_AD, av7110->debi_bus);
	saa7146_write(dev, DEBI_COMMAND, (count << 17) | (addr & 0xffff));
	saa7146_write(dev, MC2, (2 << 16) | 2);
	return 0;
}

u32 av7110_debiread(struct av7110 *av7110, u32 config, int addr, int count)
{
	struct saa7146_dev *dev = av7110->dev;
	u32 result = 0;

	if (count > 32764 || count <= 0)
		return 0;
	if (saa7146_wait_for_debi_done(av7110->dev) < 0)
		return 0;
	saa7146_write(dev, DEBI_AD, av7110->debi_bus);
	saa7146_write(dev, DEBI_COMMAND, (count << 17) | 0x10000 | (addr & 0xffff));

	saa7146_write(dev, DEBI_CONFIG, config);
	saa7146_write(dev, MC2, (2 << 16) | 2);
	if (count > 4)
		return count;
	saa7146_wait_for_debi_done(av7110->dev);
	result = saa7146_read(dev, DEBI_AD);
	result &= (0xffffffffUL >> ((4 - count) * 8));
	return result;
}



/* av7110 ARM core boot stuff */

void av7110_reset_arm(struct av7110 *av7110)
{
	saa7146_setgpio(av7110->dev, RESET_LINE, SAA7146_GPIO_OUTLO);

	/* Disable DEBI and GPIO irq */
	IER_DISABLE(av7110->dev, (MASK_19 | MASK_03));
	saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));

	saa7146_setgpio(av7110->dev, RESET_LINE, SAA7146_GPIO_OUTHI);
	dvb_delay(30);	/* the firmware needs some time to initialize */

	ARM_ResetMailBox(av7110);

	saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));
	IER_ENABLE(av7110->dev, MASK_03);

	av7110->arm_ready = 1;
	printk("av7110: ARM RESET\n");
}


static int waitdebi(struct av7110 *av7110, int adr, int state)
{
	int k;

	DEB_EE(("av7110: %p\n", av7110));

	for (k = 0; k < 100; k++) {
		if (irdebi(av7110, DEBINOSWAP, adr, 0, 2) == state)
			return 0;
		udelay(5);
	}
	return -1;
}

static int load_dram(struct av7110 *av7110, u32 *data, int len)
{
	int i;
	int blocks, rest;
	u32 base, bootblock = BOOT_BLOCK;

	DEB_EE(("av7110: %p\n", av7110));

	blocks = len / BOOT_MAX_SIZE;
	rest = len % BOOT_MAX_SIZE;
	base = DRAM_START_CODE;

	for (i = 0; i < blocks; i++) {
		if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
			return -1;
		DEB_D(("Writing DRAM block %d\n", i));
		mwdebi(av7110, DEBISWAB, bootblock,
		       ((char*)data) + i * BOOT_MAX_SIZE, BOOT_MAX_SIZE);
		bootblock ^= 0x1400;
		iwdebi(av7110, DEBISWAB, BOOT_BASE, swab32(base), 4);
		iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, BOOT_MAX_SIZE, 2);
		iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
		base += BOOT_MAX_SIZE;
	}

	if (rest > 0) {
		if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
			return -1;
		if (rest > 4)
			mwdebi(av7110, DEBISWAB, bootblock,
			       ((char*)data) + i * BOOT_MAX_SIZE, rest);
		else
			mwdebi(av7110, DEBISWAB, bootblock,
			       ((char*)data) + i * BOOT_MAX_SIZE - 4, rest + 4);

		iwdebi(av7110, DEBISWAB, BOOT_BASE, swab32(base), 4);
		iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, rest, 2);
		iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
	}
	if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
		return -1;
	iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, 0, 2);
	iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
	if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BOOT_COMPLETE) < 0)
		return -1;
	return 0;
}


/* we cannot write av7110 DRAM directly, so load a bootloader into
 * the DPRAM which implements a simple boot protocol */
static u8 bootcode[] = {
  0xea, 0x00, 0x00, 0x0e, 0xe1, 0xb0, 0xf0, 0x0e, 0xe2, 0x5e, 0xf0, 0x04,
  0xe2, 0x5e, 0xf0, 0x04, 0xe2, 0x5e, 0xf0, 0x08, 0xe2, 0x5e, 0xf0, 0x04,
  0xe2, 0x5e, 0xf0, 0x04, 0xe2, 0x5e, 0xf0, 0x04, 0x2c, 0x00, 0x00, 0x24,
  0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x34,
  0x00, 0x00, 0x00, 0x00, 0xa5, 0xa5, 0x5a, 0x5a, 0x00, 0x1f, 0x15, 0x55,
  0x00, 0x00, 0x00, 0x09, 0xe5, 0x9f, 0xd0, 0x7c, 0xe5, 0x9f, 0x40, 0x74,
  0xe3, 0xa0, 0x00, 0x00, 0xe5, 0x84, 0x00, 0x00, 0xe5, 0x84, 0x00, 0x04,
  0xe5, 0x9f, 0x10, 0x70, 0xe5, 0x9f, 0x20, 0x70, 0xe5, 0x9f, 0x30, 0x64,
  0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0, 0xe1, 0x51, 0x00, 0x02,
  0xda, 0xff, 0xff, 0xfb, 0xe5, 0x9f, 0xf0, 0x50, 0xe1, 0xd4, 0x10, 0xb0,
  0xe3, 0x51, 0x00, 0x00, 0x0a, 0xff, 0xff, 0xfc, 0xe1, 0xa0, 0x10, 0x0d,
  0xe5, 0x94, 0x30, 0x04, 0xe1, 0xd4, 0x20, 0xb2, 0xe2, 0x82, 0x20, 0x3f,
  0xe1, 0xb0, 0x23, 0x22, 0x03, 0xa0, 0x00, 0x02, 0xe1, 0xc4, 0x00, 0xb0,
  0x0a, 0xff, 0xff, 0xf4, 0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0,
  0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0, 0xe2, 0x52, 0x20, 0x01,
  0x1a, 0xff, 0xff, 0xf9, 0xe2, 0x2d, 0xdb, 0x05, 0xea, 0xff, 0xff, 0xec,
  0x2c, 0x00, 0x03, 0xf8, 0x2c, 0x00, 0x04, 0x00, 0x9e, 0x00, 0x08, 0x00,
  0x2c, 0x00, 0x00, 0x74, 0x2c, 0x00, 0x00, 0xc0
};

int av7110_bootarm(struct av7110 *av7110)
{
	struct saa7146_dev *dev = av7110->dev;
	u32 ret;
	int i;

	DEB_EE(("av7110: %p\n", av7110));

	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTLO);

	/* Disable DEBI and GPIO irq */
	IER_DISABLE(av7110->dev, MASK_03 | MASK_19);
	saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));

	/* enable DEBI */
	saa7146_write(av7110->dev, MC1, 0x08800880);
	saa7146_write(av7110->dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(av7110->dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	/* test DEBI */
	iwdebi(av7110, DEBISWAP, DPRAM_BASE, 0x76543210, 4);
	if ((ret=irdebi(av7110, DEBINOSWAP, DPRAM_BASE, 0, 4)) != 0x10325476) {
		printk(KERN_ERR "dvb: debi test in av7110_bootarm() failed: "
		       "%08x != %08x (check your BIOS hotplug settings)\n",
		       ret, 0x10325476);
		return -1;
	}
	for (i = 0; i < 8192; i += 4)
		iwdebi(av7110, DEBISWAP, DPRAM_BASE + i, 0x00, 4);
	DEB_D(("av7110_bootarm: debi test OK\n"));

	/* boot */
	DEB_D(("av7110_bootarm: load boot code\n"));
	saa7146_setgpio(dev, ARM_IRQ_LINE, SAA7146_GPIO_IRQLO);
	//saa7146_setgpio(dev, DEBI_DONE_LINE, SAA7146_GPIO_INPUT);
	//saa7146_setgpio(dev, 3, SAA7146_GPIO_INPUT);

	mwdebi(av7110, DEBISWAB, DPRAM_BASE, bootcode, sizeof(bootcode));
	iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);

	if (saa7146_wait_for_debi_done(av7110->dev)) {
		printk(KERN_ERR "dvb: av7110_bootarm(): "
		       "saa7146_wait_for_debi_done() timed out\n");
		return -1;
	}
	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTHI);
	mdelay(1);

	DEB_D(("av7110_bootarm: load dram code\n"));
	if (load_dram(av7110, (u32 *)av7110->bin_root, av7110->size_root) < 0)
		return -1;

	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTLO);
	mdelay(1);

	DEB_D(("av7110_bootarm: load dpram code\n"));
	mwdebi(av7110, DEBISWAB, DPRAM_BASE, av7110->bin_dpram, av7110->size_dpram);

	if (saa7146_wait_for_debi_done(av7110->dev)) {
		printk(KERN_ERR "dvb: av7110_bootarm(): "
		       "saa7146_wait_for_debi_done() timed out after loading DRAM\n");
		return -1;
	}
	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTHI);
	dvb_delay(30);	/* the firmware needs some time to initialize */

	//ARM_ClearIrq(av7110);
	ARM_ResetMailBox(av7110);
	saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));
	IER_ENABLE(av7110->dev, MASK_03);

	av7110->arm_errors = 0;
	av7110->arm_ready = 1;
	return 0;
}


/****************************************************************************
 * DEBI command polling
 ****************************************************************************/

int __av7110_send_fw_cmd(struct av7110 *av7110, u16* buf, int length)
{
	int i;
	unsigned long start;
#ifdef COM_DEBUG
	u32 stat;
#endif

//	DEB_EE(("av7110: %p\n", av7110));

	if (!av7110->arm_ready) {
		DEB_D(("arm not ready.\n"));
		return -1;
	}

	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2 )) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_FREE)) {
			printk(KERN_ERR "%s: timeout waiting for COMMAND idle\n", __FUNCTION__);
			return -1;
		}
	}

#ifndef _NOHANDSHAKE
	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 )) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_SHAKE)) {
			printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n", __FUNCTION__);
			return -1;
		}
	}
#endif

	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2) & OSDQFull) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_OSD)) {
			printk(KERN_ERR "%s: timeout waiting for !OSDQFull\n", __FUNCTION__);
			return -1;
		}
	}
	for (i = 2; i < length; i++)
		wdebi(av7110, DEBINOSWAP, COMMAND + 2 * i, (u32) buf[i], 2);

	if (length)
		wdebi(av7110, DEBINOSWAP, COMMAND + 2, (u32) buf[1], 2);
	else
		wdebi(av7110, DEBINOSWAP, COMMAND + 2, 0, 2);

	wdebi(av7110, DEBINOSWAP, COMMAND, (u32) buf[0], 2);

#ifdef COM_DEBUG
	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2 )) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_FREE)) {
			printk(KERN_ERR "%s: timeout waiting for COMMAND to complete\n",
			       __FUNCTION__);
			return -1;
		}
	}

	stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
	if (stat & GPMQOver) {
		printk(KERN_ERR "%s: GPMQOver\n", __FUNCTION__);
		return -1;
	}
	else if (stat & OSDQOver) {
		printk(KERN_ERR "%s: OSDQOver\n", __FUNCTION__);
		return -1;
	}
#endif

	return 0;
}

int av7110_send_fw_cmd(struct av7110 *av7110, u16* buf, int length)
{
	int ret;

//	DEB_EE(("av7110: %p\n", av7110));

	if (!av7110->arm_ready) {
		DEB_D(("arm not ready.\n"));
		return -1;
	}
	if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

	ret = __av7110_send_fw_cmd(av7110, buf, length);
	up(&av7110->dcomlock);
	if (ret)
		printk("av7110_send_fw_cmd error\n");
	return ret;
}

int av7110_fw_cmd(struct av7110 *av7110, int type, int com, int num, ...)
{
	va_list args;
	u16 buf[num + 2];
	int i, ret;

//	DEB_EE(("av7110: %p\n",av7110));

	buf[0] = ((type << 8) | com);
	buf[1] = num;

	if (num) {
		va_start(args, num);
		for (i = 0; i < num; i++)
			buf[i + 2] = va_arg(args, u32);
		va_end(args);
	}

	ret = av7110_send_fw_cmd(av7110, buf, num + 2);
	if (ret)
		printk("av7110_fw_cmd error\n");
	return ret;
}

int av7110_send_ci_cmd(struct av7110 *av7110, u8 subcom, u8 *buf, u8 len)
{
	int i, ret;
	u16 cmd[18] = { ((COMTYPE_COMMON_IF << 8) + subcom),
		16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	DEB_EE(("av7110: %p\n", av7110));

	for(i = 0; i < len && i < 32; i++)
	{
		if(i % 2 == 0)
			cmd[(i / 2) + 2] = (u16)(buf[i]) << 8;
		else
			cmd[(i / 2) + 2] |= buf[i];
	}

	ret = av7110_send_fw_cmd(av7110, cmd, 18);
	if (ret)
		printk("av7110_send_ci_cmd error\n");
	return ret;
}

int av7110_fw_request(struct av7110 *av7110, u16 *request_buf,
		      int request_buf_len, u16 *reply_buf, int reply_buf_len)
{
	int err;
	s16 i;
	unsigned long start;
#ifdef COM_DEBUG
	u32 stat;
#endif

	DEB_EE(("av7110: %p\n", av7110));

	if (!av7110->arm_ready) {
		DEB_D(("arm not ready.\n"));
		return -1;
	}

	if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

	if ((err = __av7110_send_fw_cmd(av7110, request_buf, request_buf_len)) < 0) {
		up(&av7110->dcomlock);
		printk("av7110_fw_request error\n");
		return err;
	}

	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2)) {
#ifdef _NOHANDSHAKE
		dvb_delay(1);
#endif
		if (time_after(jiffies, start + ARM_WAIT_FREE)) {
			printk("%s: timeout waiting for COMMAND to complete\n", __FUNCTION__);
			up(&av7110->dcomlock);
			return -1;
		}
	}

#ifndef _NOHANDSHAKE
	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 )) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_SHAKE)) {
			printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n", __FUNCTION__);
			up(&av7110->dcomlock);
			return -1;
		}
	}
#endif

#ifdef COM_DEBUG
	stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
	if (stat & GPMQOver) {
		printk(KERN_ERR "%s: GPMQOver\n", __FUNCTION__);
		up(&av7110->dcomlock);
		return -1;
	}
	else if (stat & OSDQOver) {
		printk(KERN_ERR "%s: OSDQOver\n", __FUNCTION__);
		up(&av7110->dcomlock);
		return -1;
	}
#endif

	for (i = 0; i < reply_buf_len; i++)
		reply_buf[i] = rdebi(av7110, DEBINOSWAP, COM_BUFF + 2 * i, 0, 2);

	up(&av7110->dcomlock);
	return 0;
}

int av7110_fw_query(struct av7110 *av7110, u16 tag, u16* buf, s16 length)
{
	int ret;
	ret = av7110_fw_request(av7110, &tag, 0, buf, length);
	if (ret)
		printk("av7110_fw_query error\n");
	return ret;
}


/****************************************************************************
 * Firmware commands
 ****************************************************************************/

/* get version of the firmware ROM, RTSL, video ucode and ARM application  */
int av7110_firmversion(struct av7110 *av7110)
{
	u16 buf[20];
	u16 tag = ((COMTYPE_REQUEST << 8) + ReqVersion);

	DEB_EE(("av7110: %p\n", av7110));

	if (av7110_fw_query(av7110, tag, buf, 16)) {
		printk("DVB: AV7110-%d: ERROR: Failed to boot firmware\n",
		       av7110->dvb_adapter->num);
		return -EIO;
	}

	av7110->arm_fw = (buf[0] << 16) + buf[1];
	av7110->arm_rtsl = (buf[2] << 16) + buf[3];
	av7110->arm_vid = (buf[4] << 16) + buf[5];
	av7110->arm_app = (buf[6] << 16) + buf[7];
	av7110->avtype = (buf[8] << 16) + buf[9];

	printk("DVB: AV711%d(%d) - firm %08x, rtsl %08x, vid %08x, app %08x\n",
	       av7110->avtype, av7110->dvb_adapter->num, av7110->arm_fw,
	       av7110->arm_rtsl, av7110->arm_vid, av7110->arm_app);

	/* print firmware capabilities */
	if (FW_CI_LL_SUPPORT(av7110->arm_app))
		printk("DVB: AV711%d(%d) - firmware supports CI link layer interface\n",
		       av7110->avtype, av7110->dvb_adapter->num);
	else
		printk("DVB: AV711%d(%d) - no firmware support for CI link layer interface\n",
		       av7110->avtype, av7110->dvb_adapter->num);

	return 0;
}


int av7110_diseqc_send(struct av7110 *av7110, int len, u8 *msg, unsigned long burst)
{
	int i;
	u16 buf[18] = { ((COMTYPE_AUDIODAC << 8) + SendDiSEqC),
			16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	DEB_EE(("av7110: %p\n", av7110));

	if (len > 10)
		len = 10;

	buf[1] = len + 2;
	buf[2] = len;

	if (burst != -1)
		buf[3] = burst ? 0x01 : 0x00;
	else
		buf[3] = 0xffff;

	for (i = 0; i < len; i++)
		buf[i + 4] = msg[i];

	if (av7110_send_fw_cmd(av7110, buf, 18))
		printk("av7110_diseqc_send error\n");

	return 0;
}


#ifdef CONFIG_DVB_AV7110_OSD

static inline int ResetBlend(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetNonBlend, 1, windownr);
}

static inline int SetColorBlend(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetCBlend, 1, windownr);
}

static inline int SetWindowBlend(struct av7110 *av7110, u8 windownr, u8 blending)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetWBlend, 2, windownr, blending);
}

static inline int SetBlend_(struct av7110 *av7110, u8 windownr,
		     enum av7110_osd_palette_type colordepth, u16 index, u8 blending)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetBlend, 4,
			     windownr, colordepth, index, blending);
}

static inline int SetColor_(struct av7110 *av7110, u8 windownr,
		     enum av7110_osd_palette_type colordepth, u16 index, u16 colorhi, u16 colorlo)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetColor, 5,
			     windownr, colordepth, index, colorhi, colorlo);
}

static inline int BringToTop(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WTop, 1, windownr);
}

static inline int SetFont(struct av7110 *av7110, u8 windownr, u8 fontsize,
			  u16 colorfg, u16 colorbg)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, Set_Font, 4,
			     windownr, fontsize, colorfg, colorbg);
}

static int FlushText(struct av7110 *av7110)
{
	unsigned long start;

	if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;
	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2)) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_OSD)) {
			printk(KERN_ERR "%s: timeout waiting for BUFF1_BASE == 0\n",
			       __FUNCTION__);
			up(&av7110->dcomlock);
			return -1;
		}
	}
	up(&av7110->dcomlock);
	return 0;
}

static int WriteText(struct av7110 *av7110, u8 win, u16 x, u16 y, u8* buf)
{
	int i, ret;
	unsigned long start;
	int length = strlen(buf) + 1;
	u16 cbuf[5] = { (COMTYPE_OSD << 8) + DText, 3, win, x, y };

	if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2)) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_OSD)) {
			printk(KERN_ERR "%s: timeout waiting for BUFF1_BASE == 0\n",
			       __FUNCTION__);
			up(&av7110->dcomlock);
			return -1;
		}
	}
#ifndef _NOHANDSHAKE
	start = jiffies;
	while (rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2)) {
		dvb_delay(1);
		if (time_after(jiffies, start + ARM_WAIT_SHAKE)) {
			printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n",
			       __FUNCTION__);
			up(&av7110->dcomlock);
			return -1;
		}
	}
#endif
	for (i = 0; i < length / 2; i++)
		wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i * 2,
		      swab16(*(u16 *)(buf + 2 * i)), 2);
	if (length & 1)
		wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i * 2, 0, 2);
	ret = __av7110_send_fw_cmd(av7110, cbuf, 5);
	up(&av7110->dcomlock);
	if (ret)
		printk("WriteText error\n");
	return ret;
}

static inline int DrawLine(struct av7110 *av7110, u8 windownr,
			   u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, DLine, 6,
			     windownr, x, y, dx, dy, color);
}

static inline int DrawBlock(struct av7110 *av7110, u8 windownr,
			    u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, DBox, 6,
			     windownr, x, y, dx, dy, color);
}

static inline int HideWindow(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WHide, 1, windownr);
}

static inline int MoveWindowRel(struct av7110 *av7110, u8 windownr, u16 x, u16 y)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WMoveD, 3, windownr, x, y);
}

static inline int MoveWindowAbs(struct av7110 *av7110, u8 windownr, u16 x, u16 y)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WMoveA, 3, windownr, x, y);
}

static inline int DestroyOSDWindow(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WDestroy, 1, windownr);
}

static inline int CreateOSDWindow(struct av7110 *av7110, u8 windownr,
				  enum av7110_window_display_type disptype,
				  u16 width, u16 height)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WCreate, 4,
			     windownr, disptype, width, height);
}


static enum av7110_osd_palette_type bpp2pal[8] = {
	Pal1Bit, Pal2Bit, 0, Pal4Bit, 0, 0, 0, Pal8Bit
};
static enum av7110_window_display_type bpp2bit[8] = {
	BITMAP1, BITMAP2, 0, BITMAP4, 0, 0, 0, BITMAP8
};

static inline int LoadBitmap(struct av7110 *av7110, u16 format,
			     u16 dx, u16 dy, int inc, u8* data)
{
	int bpp;
	int i;
	int d, delta;
	u8 c;
	DECLARE_WAITQUEUE(wait, current);

	DEB_EE(("av7110: %p\n", av7110));

	if (av7110->bmp_state == BMP_LOADING) {
		add_wait_queue(&av7110->bmpq, &wait);
		while (1) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (av7110->bmp_state != BMP_LOADING
			    || signal_pending(current))
				break;
			schedule();
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&av7110->bmpq, &wait);
	}
	if (av7110->bmp_state == BMP_LOADING)
		return -1;
	av7110->bmp_state = BMP_LOADING;
	if	(format == BITMAP8) {
		bpp=8; delta = 1;
	} else if (format == BITMAP4) {
		bpp=4; delta = 2;
	} else if (format == BITMAP2) {
		bpp=2; delta = 4;
	} else if (format == BITMAP1) {
		bpp=1; delta = 8;
	} else {
		av7110->bmp_state = BMP_NONE;
		return -1;
	}
	av7110->bmplen = ((dx * dy * bpp + 7) & ~7) / 8;
	av7110->bmpp = 0;
	if (av7110->bmplen > 32768) {
		av7110->bmp_state = BMP_NONE;
		return -1;
	}
	for (i = 0; i < dy; i++) {
		if (copy_from_user(av7110->bmpbuf + 1024 + i * dx, data + i * inc, dx)) {
			av7110->bmp_state = BMP_NONE;
			return -1;
		}
	}
	if (format != BITMAP8) {
		for (i = 0; i < dx * dy / delta; i++) {
			c = ((u8 *)av7110->bmpbuf)[1024 + i * delta + delta - 1];
			for (d = delta - 2; d >= 0; d--) {
				c |= (((u8 *)av7110->bmpbuf)[1024 + i * delta + d]
				      << ((delta - d - 1) * bpp));
				((u8 *)av7110->bmpbuf)[1024 + i] = c;
			}
		}
	}
	av7110->bmplen += 1024;
	return av7110_fw_cmd(av7110, COMTYPE_OSD, LoadBmp, 3, format, dx, dy);
}

static int BlitBitmap(struct av7110 *av7110, u16 win, u16 x, u16 y, u16 trans)
{
	DECLARE_WAITQUEUE(wait, current);

	DEB_EE(("av7110: %p\n", av7110));

       if (av7110->bmp_state == BMP_NONE)
		return -1;
	if (av7110->bmp_state == BMP_LOADING) {
		add_wait_queue(&av7110->bmpq, &wait);
		while (1) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (av7110->bmp_state != BMP_LOADING
			    || signal_pending(current))
				break;
			schedule();
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&av7110->bmpq, &wait);
	}
	if (av7110->bmp_state == BMP_LOADED)
		return av7110_fw_cmd(av7110, COMTYPE_OSD, BlitBmp, 4, win, x, y, trans);
	return -1;
}

static inline int ReleaseBitmap(struct av7110 *av7110)
{
	DEB_EE(("av7110: %p\n",av7110));

	if (av7110->bmp_state != BMP_LOADED)
		return -1;
	av7110->bmp_state = BMP_NONE;
	return av7110_fw_cmd(av7110, COMTYPE_OSD, ReleaseBmp, 0);
}

static u32 RGB2YUV(u16 R, u16 G, u16 B)
{
	u16 y, u, v;
	u16 Y, Cr, Cb;

	y = R * 77 + G * 150 + B * 29;	/* Luma=0.299R+0.587G+0.114B 0..65535 */
	u = 2048 + B * 8 -(y >> 5);	/* Cr 0..4095 */
	v = 2048 + R * 8 -(y >> 5);	/* Cb 0..4095 */

	Y = y / 256;
	Cb = u / 16;
	Cr = v / 16;

	return Cr | (Cb << 16) | (Y << 8);
}

static void OSDSetColor(struct av7110 *av7110, u8 color, u8 r, u8 g, u8 b, u8 blend)
{
	u16 ch, cl;
	u32 yuv;

	yuv = blend ? RGB2YUV(r,g,b) : 0;
	cl = (yuv & 0xffff);
	ch = ((yuv >> 16) & 0xffff);
	SetColor_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
		  color, ch, cl);
	SetBlend_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
		  color, ((blend >> 4) & 0x0f));
}

static int OSDSetPalette(struct av7110 *av7110, u32 *colors, u8 first, u8 last)
{
       int i;
       int length = last - first + 1;

       if (length * 4 > DATA_BUFF3_SIZE)
	       return -1;

       for (i = 0; i < length; i++) {
	       u32 blend = (colors[i] & 0xF0000000) >> 4;
	       u32 yuv = blend ? RGB2YUV(colors[i] & 0xFF, (colors[i] >> 8) & 0xFF,
					 (colors[i] >> 16) & 0xFF) | blend : 0;
	       yuv = ((yuv & 0xFFFF0000) >> 16) | ((yuv & 0x0000FFFF) << 16);
	       wdebi(av7110, DEBINOSWAP, DATA_BUFF3_BASE + i * 4, yuv, 4);
       }
       return av7110_fw_cmd(av7110, COMTYPE_OSD, Set_Palette, 4,
			    av7110->osdwin,
			    bpp2pal[av7110->osdbpp[av7110->osdwin]],
			    first, last);
}

static int OSDSetBlock(struct av7110 *av7110, int x0, int y0,
		       int x1, int y1, int inc, u8 *data)
{
	uint w, h, bpp, bpl, size, lpb, bnum, brest;
	int i;

	w = x1 - x0 + 1;
	h = y1 - y0 + 1;
	if (inc <= 0)
		inc = w;
	if (w <= 0 || w > 720 || h <= 0 || h > 576)
		return -1;
	bpp = av7110->osdbpp[av7110->osdwin] + 1;
	bpl = ((w * bpp + 7) & ~7) / 8;
	size = h * bpl;
	lpb = (32 * 1024) / bpl;
	bnum = size / (lpb * bpl);
	brest = size - bnum * lpb * bpl;

	for (i = 0; i < bnum; i++) {
		LoadBitmap(av7110, bpp2bit[av7110->osdbpp[av7110->osdwin]],
			   w, lpb, inc, data);
		BlitBitmap(av7110, av7110->osdwin, x0, y0 + i * lpb, 0);
		data += lpb * inc;
	}
	if (brest) {
		LoadBitmap(av7110, bpp2bit[av7110->osdbpp[av7110->osdwin]],
			   w, brest / bpl, inc, data);
		BlitBitmap(av7110, av7110->osdwin, x0, y0 + bnum * lpb, 0);
	}
	ReleaseBitmap(av7110);
	return 0;
}

int av7110_osd_cmd(struct av7110 *av7110, osd_cmd_t *dc)
{
	switch (dc->cmd) {
	case OSD_Close:
		DestroyOSDWindow(av7110, av7110->osdwin);
		return 0;
	case OSD_Open:
		av7110->osdbpp[av7110->osdwin] = (dc->color - 1) & 7;
		CreateOSDWindow(av7110, av7110->osdwin,
				bpp2bit[av7110->osdbpp[av7110->osdwin]],
				dc->x1 - dc->x0 + 1, dc->y1 - dc->y0 + 1);
		if (!dc->data) {
			MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
			SetColorBlend(av7110, av7110->osdwin);
		}
		return 0;
	case OSD_Show:
		MoveWindowRel(av7110, av7110->osdwin, 0, 0);
		return 0;
	case OSD_Hide:
		HideWindow(av7110, av7110->osdwin);
		return 0;
	case OSD_Clear:
		DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, 0);
		return 0;
	case OSD_Fill:
		DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, dc->color);
		return 0;
	case OSD_SetColor:
		OSDSetColor(av7110, dc->color, dc->x0, dc->y0, dc->x1, dc->y1);
		return 0;
	case OSD_SetPalette:
	{
		if (FW_VERSION(av7110->arm_app) >= 0x2618)
			OSDSetPalette(av7110, (u32 *)dc->data, dc->color, dc->x0);
		else {
			int i, len = dc->x0-dc->color+1;
			u8 *colors = (u8 *)dc->data;

			for (i = 0; i<len; i++)
				OSDSetColor(av7110, dc->color + i,
					colors[i * 4], colors[i * 4 + 1],
					colors[i * 4 + 2], colors[i * 4 + 3]);
		}
		return 0;
	}
	case OSD_SetTrans:
		return 0;
	case OSD_SetPixel:
		DrawLine(av7110, av7110->osdwin,
			 dc->x0, dc->y0, 0, 0, dc->color);
		return 0;
	case OSD_GetPixel:
		return 0;

	case OSD_SetRow:
		dc->y1 = dc->y0;
		/* fall through */
	case OSD_SetBlock:
		OSDSetBlock(av7110, dc->x0, dc->y0, dc->x1, dc->y1, dc->color, dc->data);
		return 0;

	case OSD_FillRow:
		DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
			  dc->x1-dc->x0+1, dc->y1, dc->color);
		return 0;
	case OSD_FillBlock:
		DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
			  dc->x1 - dc->x0 + 1, dc->y1 - dc->y0 + 1, dc->color);
		return 0;
	case OSD_Line:
		DrawLine(av7110, av7110->osdwin,
			 dc->x0, dc->y0, dc->x1 - dc->x0, dc->y1 - dc->y0, dc->color);
		return 0;
	case OSD_Query:
		return 0;
	case OSD_Test:
		return 0;
	case OSD_Text:
	{
		char textbuf[240];

		if (strncpy_from_user(textbuf, dc->data, 240) < 0)
			return -EFAULT;
		textbuf[239] = 0;
		if (dc->x1 > 3)
			dc->x1 = 3;
		SetFont(av7110, av7110->osdwin, dc->x1,
			(u16) (dc->color & 0xffff), (u16) (dc->color >> 16));
		FlushText(av7110);
		WriteText(av7110, av7110->osdwin, dc->x0, dc->y0, textbuf);
		return 0;
	}
	case OSD_SetWindow:
		if (dc->x0 < 1 || dc->x0 > 7)
			return -EINVAL;
		av7110->osdwin = dc->x0;
		return 0;
	case OSD_MoveWindow:
		MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
		SetColorBlend(av7110, av7110->osdwin);
		return 0;
	default:
		return -EINVAL;
	}
}
#endif /* CONFIG_DVB_AV7110_OSD */
