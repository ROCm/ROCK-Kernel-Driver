/*
 * skystar2.c - driver for the Technisat SkyStar2 PCI DVB card
 *              based on the FlexCopII by B2C2,Inc.
 *
 * Copyright (C) 2003  V.C. , skystar@moldova.cc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>

#include "dvb_i2c.h"
#include "dvb_frontend.h"
#include "dvb_functions.h"

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_filter.h"
#include "dvbdev.h"
#include "demux.h"
#include "dvb_net.h"

static int debug = 0;
#define dprintk(x...) do { if (debug) printk(x); } while (0)

#define SizeOfBufDMA1	0x3AC00
#define SizeOfBufDMA2	0x758

struct dmaq {

	u32 bus_addr;
	u32 head;
	u32 tail;
	u32 buffer_size;
	u8 *buffer;
};

struct packet_header {

	u32 sync_byte;
	u32 transport_error_indicator;
	u32 payload_unit_start_indicator;
	u32 transport_priority;
	u32 pid;
	u32 transport_scrambling_control;
	u32 adaptation_field_control;
	u32 continuity_counter;
};

struct adapter {

	struct pci_dev *pdev;

	u8 card_revision;
	u32 b2c2_revision;
	u32 PidFilterMax;
	u32 MacFilterMax;
	u32 irq;
	unsigned long io_mem;
	unsigned long io_port;
	u8 mac_addr[8];
	u32 dwSramType;

	struct dvb_adapter *dvb_adapter;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	struct dvb_i2c_bus *i2c_bus;
	struct dvb_net dvbnet;

	struct semaphore i2c_sem;

	struct dmaq dmaq1;
	struct dmaq dmaq2;

	u32 dma_ctrl;
	u32 dma_status;

	u32 capturing;

	spinlock_t lock;

	u16 pids[0x27];
	u32 mac_filter;
};

#define WriteRegDW(adapter,reg,value) writel(value, adapter->io_mem + reg)
#define ReadRegDW(adapter,reg) readl(adapter->io_mem + reg)

static void WriteRegOp(struct adapter *adapter, u32 reg, u32 operation, u32 andvalue, u32 orvalue)
{
	u32 tmp;

	tmp = ReadRegDW(adapter, reg);

	if (operation == 1)
		tmp = tmp | orvalue;
	if (operation == 2)
		tmp = tmp & andvalue;
	if (operation == 3)
		tmp = (tmp & andvalue) | orvalue;

	WriteRegDW(adapter, reg, tmp);
}

/* i2c functions */
static int i2cMainWriteForFlex2(struct adapter * adapter, u32 command, u8 * buf, u32 retries)
{
	u32 i;
	u32 value;

	WriteRegDW(adapter, 0x100, 0);
	WriteRegDW(adapter, 0x100, command);

	for (i = 0; i < retries; i++) {
		value = ReadRegDW(adapter, 0x100);

		if ((value & 0x40000000) == 0) {
			if ((value & 0x81000000) == 0x80000000) {
				if (buf != 0)
					*buf = (value >> 0x10) & 0xff;

				return 1;
			}

		} else {

			WriteRegDW(adapter, 0x100, 0);
			WriteRegDW(adapter, 0x100, command);
		}
	}

	return 0;
}

/* device = 0x10000000 for tuner, 0x20000000 for eeprom */
static void i2cMainSetup(u32 device, u32 chip_addr, u8 op, u8 addr, u32 value, u32 len, u32 *command)
{
	*command = device | ((len - 1) << 26) | (value << 16) | (addr << 8) | chip_addr;

	if (op != 0)
		*command = *command | 0x03000000;
	else
		*command = *command | 0x01000000;
}

static int FlexI2cRead4(struct adapter * adapter, u32 device, u32 chip_addr, u16 addr, u8 * buf, u8 len)
{
	u32 command;
	u32 value;

	int result, i;

	i2cMainSetup(device, chip_addr, 1, addr, 0, len, &command);

	result = i2cMainWriteForFlex2(adapter, command, buf, 100000);

	if ((result & 0xff) != 0) {
		if (len > 1) {
			value = ReadRegDW(adapter, 0x104);

			for (i = 1; i < len; i++) {
				buf[i] = value & 0xff;
				value = value >> 8;
			}
		}
	}

	return result;
}

static int FlexI2cWrite4(struct adapter * adapter, u32 device, u32 chip_addr, u32 addr, u8 * buf, u8 len)
{
	u32 command;
	u32 value;
	int i;

	if (len > 1) {
		value = 0;

		for (i = len; i > 1; i--) {
			value = value << 8;
			value = value | buf[i - 1];
		}

		WriteRegDW(adapter, 0x104, value);
	}

	i2cMainSetup(device, chip_addr, 0, addr, buf[0], len, &command);

	return i2cMainWriteForFlex2(adapter, command, 0, 100000);
}

static void fixchipaddr(u32 device, u32 bus, u32 addr, u32 *ret)
{
	if (device == 0x20000000)
		*ret = bus | ((addr >> 8) & 3);

	*ret = bus;
}

static u32 FLEXI2C_read(struct adapter * adapter, u32 device, u32 bus, u32 addr, u8 * buf, u32 len)
{
	u32 chipaddr;
	u32 bytes_to_transfer;
	u8 *start;

//  dprintk("%s:\n", __FUNCTION__);

	start = buf;

	while (len != 0) {
		bytes_to_transfer = len;

		if (bytes_to_transfer > 4)
			bytes_to_transfer = 4;

		fixchipaddr(device, bus, addr, &chipaddr);

		if (FlexI2cRead4(adapter, device, chipaddr, addr, buf, bytes_to_transfer) == 0)
			return buf - start;

		buf = buf + bytes_to_transfer;
		addr = addr + bytes_to_transfer;
		len = len - bytes_to_transfer;
	};

	return buf - start;
}

static u32 FLEXI2C_write(struct adapter * adapter, u32 device, u32 bus, u32 addr, u8 * buf, u32 len)
{
	u32 chipaddr;
	u32 bytes_to_transfer;
	u8 *start;

//  dprintk("%s:\n", __FUNCTION__);

	start = buf;

	while (len != 0) {
		bytes_to_transfer = len;

		if (bytes_to_transfer > 4)
			bytes_to_transfer = 4;

		fixchipaddr(device, bus, addr, &chipaddr);

		if (FlexI2cWrite4(adapter, device, chipaddr, addr, buf, bytes_to_transfer) == 0)
			return buf - start;

		buf = buf + bytes_to_transfer;
		addr = addr + bytes_to_transfer;
		len = len - bytes_to_transfer;
	}

	return buf - start;
}

static int master_xfer(struct dvb_i2c_bus *i2c, const struct i2c_msg *msgs, int num)
{
	struct adapter *tmp = i2c->data;
	int i, ret = 0;

	if (down_interruptible(&tmp->i2c_sem))
		return -ERESTARTSYS;

	if (0) {
		dprintk("%s:\n", __FUNCTION__);

		for (i = 0; i < num; i++) {
			printk("message %d: flags=%x, addr=0x%04x, buf=%p, len=%d \n", i, msgs[i].flags, msgs[i].addr, msgs[i].buf, msgs[i].len);
		}
	}
	
	/* allow only the vp310 frontend to access the bus */
	if ((msgs[0].addr != 0x0E) && (msgs[0].addr != 0x61)) {
		up(&tmp->i2c_sem);

		return -EREMOTEIO;
	}

	if ((num == 1) && (msgs[0].buf != NULL)) {
		if (msgs[0].flags == I2C_M_RD) {
			ret = -EINVAL;

		} else {

			// single writes do have the reg addr in buf[0] and data in buf[1] to buf[n]
			ret = FLEXI2C_write(tmp, 0x10000000, msgs[0].addr, msgs[0].buf[0], &msgs[0].buf[1], msgs[0].len - 1);

			if (ret != msgs[0].len - 1)
				ret = -EREMOTEIO;
			else
				ret = num;
		}

	} else if ((num == 2) && (msgs[1].buf != NULL)) {

		// i2c reads consist of a reg addr _write_ followed by a data read, so msg[1].flags has to be examined
		if (msgs[1].flags == I2C_M_RD) {
			ret = FLEXI2C_read(tmp, 0x10000000, msgs[0].addr, msgs[0].buf[0], msgs[1].buf, msgs[1].len);

		} else {

			ret = FLEXI2C_write(tmp, 0x10000000, msgs[0].addr, msgs[0].buf[0], msgs[1].buf, msgs[1].len);
		}

		if (ret != msgs[1].len)
			ret = -EREMOTEIO;
		else
			ret = num;
	}

	up(&tmp->i2c_sem);

	/* master xfer functions always return the number of successfully
	   transmitted messages, not the number of transmitted bytes.
	   return -EREMOTEIO in case of failure. */
	return ret;
}

/* SRAM (Skystar2 rev2.3 has one "ISSI IS61LV256" chip on board,
   but it seems that FlexCopII can work with more than one chip) */
static void SRAMSetNetDest(struct adapter * adapter, u8 dest)
{
	u32 tmp;

	udelay(1000);

	tmp = (ReadRegDW(adapter, 0x714) & 0xFFFFFFFC) | (dest & 3);

	udelay(1000);

	WriteRegDW(adapter, 0x714, tmp);
	WriteRegDW(adapter, 0x714, tmp);

	udelay(1000);

	/* return value is never used? */
/*	return tmp; */
}

static void SRAMSetCaiDest(struct adapter * adapter, u8 dest)
{
	u32 tmp;

	udelay(1000);

	tmp = (ReadRegDW(adapter, 0x714) & 0xFFFFFFF3) | ((dest & 3) << 2);

	udelay(1000);
	udelay(1000);

	WriteRegDW(adapter, 0x714, tmp);
	WriteRegDW(adapter, 0x714, tmp);

	udelay(1000);

	/* return value is never used? */
/*	return tmp; */
}

static void SRAMSetCaoDest(struct adapter * adapter, u8 dest)
{
	u32 tmp;

	udelay(1000);

	tmp = (ReadRegDW(adapter, 0x714) & 0xFFFFFFCF) | ((dest & 3) << 4);

	udelay(1000);
	udelay(1000);

	WriteRegDW(adapter, 0x714, tmp);
	WriteRegDW(adapter, 0x714, tmp);

	udelay(1000);

	/* return value is never used? */
/*	return tmp; */
}

static void SRAMSetMediaDest(struct adapter * adapter, u8 dest)
{
	u32 tmp;

	udelay(1000);

	tmp = (ReadRegDW(adapter, 0x714) & 0xFFFFFF3F) | ((dest & 3) << 6);

	udelay(1000);
	udelay(1000);

	WriteRegDW(adapter, 0x714, tmp);
	WriteRegDW(adapter, 0x714, tmp);

	udelay(1000);

	/* return value is never used? */
/*	return tmp; */
}

/* SRAM memory is accessed through a buffer register in the FlexCop
   chip (0x700). This register has the following structure:
    bits 0-14  : address
    bit  15    : read/write flag
    bits 16-23 : 8-bit word to write
    bits 24-27 : = 4
    bits 28-29 : memory bank selector
    bit  31    : busy flag
*/
static void FlexSramWrite(struct adapter *adapter, u32 bank, u32 addr, u8 * buf, u32 len)
{
	u32 i, command, retries;

	for (i = 0; i < len; i++) {
		command = bank | addr | 0x04000000 | (*buf << 0x10);

		retries = 2;

		while (((ReadRegDW(adapter, 0x700) & 0x80000000) != 0) && (retries > 0)) {
			mdelay(1);
			retries--;
		};

		if (retries == 0)
			printk("%s: SRAM timeout\n", __FUNCTION__);

		WriteRegDW(adapter, 0x700, command);

		buf++;
		addr++;
	}
}

static void FlexSramRead(struct adapter *adapter, u32 bank, u32 addr, u8 * buf, u32 len)
{
	u32 i, command, value, retries;

	for (i = 0; i < len; i++) {
		command = bank | addr | 0x04008000;

		retries = 10000;

		while (((ReadRegDW(adapter, 0x700) & 0x80000000) != 0) && (retries > 0)) {
			mdelay(1);
			retries--;
		};

		if (retries == 0)
			printk("%s: SRAM timeout\n", __FUNCTION__);

		WriteRegDW(adapter, 0x700, command);

		retries = 10000;

		while (((ReadRegDW(adapter, 0x700) & 0x80000000) != 0) && (retries > 0)) {
			mdelay(1);
			retries--;
		};

		if (retries == 0)
			printk("%s: SRAM timeout\n", __FUNCTION__);

		value = ReadRegDW(adapter, 0x700) >> 0x10;

		*buf = (value & 0xff);

		addr++;
		buf++;
	}
}

static void SRAM_writeChunk(struct adapter *adapter, u32 addr, u8 * buf, u16 len)
{
	u32 bank;

	bank = 0;

	if (adapter->dwSramType == 0x20000) {
		bank = (addr & 0x18000) << 0x0D;
	}

	if (adapter->dwSramType == 0x00000) {
		if ((addr >> 0x0F) == 0)
			bank = 0x20000000;
		else
			bank = 0x10000000;
	}

	FlexSramWrite(adapter, bank, addr & 0x7FFF, buf, len);
}

static void SRAM_readChunk(struct adapter *adapter, u32 addr, u8 * buf, u16 len)
{
	u32 bank;

	bank = 0;

	if (adapter->dwSramType == 0x20000) {
		bank = (addr & 0x18000) << 0x0D;
	}

	if (adapter->dwSramType == 0x00000) {
		if ((addr >> 0x0F) == 0)
			bank = 0x20000000;
		else
			bank = 0x10000000;
	}

	FlexSramRead(adapter, bank, addr & 0x7FFF, buf, len);
}

static void SRAM_read(struct adapter *adapter, u32 addr, u8 * buf, u32 len)
{
	u32 length;

	while (len != 0) {
		length = len;

		// check if the address range belongs to the same 
		// 32K memory chip. If not, the data is read from 
		// one chip at a time.
		if ((addr >> 0x0F) != ((addr + len - 1) >> 0x0F)) {
			length = (((addr >> 0x0F) + 1) << 0x0F) - addr;
		}

		SRAM_readChunk(adapter, addr, buf, length);

		addr = addr + length;
		buf = buf + length;
		len = len - length;
	}
}

static void SRAM_write(struct adapter *adapter, u32 addr, u8 * buf, u32 len)
{
	u32 length;

	while (len != 0) {
		length = len;

		// check if the address range belongs to the same 
		// 32K memory chip. If not, the data is written to
		// one chip at a time.
		if ((addr >> 0x0F) != ((addr + len - 1) >> 0x0F)) {
			length = (((addr >> 0x0F) + 1) << 0x0F) - addr;
		}

		SRAM_writeChunk(adapter, addr, buf, length);

		addr = addr + length;
		buf = buf + length;
		len = len - length;
	}
}

static void SRAM_setSize(struct adapter *adapter, u32 mask)
{
	WriteRegDW(adapter, 0x71C, (mask | (~0x30000 & ReadRegDW(adapter, 0x71C))));
}

static void SRAM_init(struct adapter *adapter)
{
	u32 tmp;

	tmp = ReadRegDW(adapter, 0x71C);

	WriteRegDW(adapter, 0x71C, 1);

	if (ReadRegDW(adapter, 0x71C) != 0) {
		WriteRegDW(adapter, 0x71C, tmp);

		adapter->dwSramType = tmp & 0x30000;

		dprintk("%s: dwSramType = %x\n", __FUNCTION__, adapter->dwSramType);

	} else {

		adapter->dwSramType = 0x10000;

		dprintk("%s: dwSramType = %x\n", __FUNCTION__, adapter->dwSramType);
	}

	/* return value is never used? */
/*	return adapter->dwSramType; */
}

static int SRAM_testLocation(struct adapter *adapter, u32 mask, u32 addr)
{
	u8 tmp1, tmp2;

	dprintk("%s: mask = %x, addr = %x\n", __FUNCTION__, mask, addr);

	SRAM_setSize(adapter, mask);
	SRAM_init(adapter);

	tmp2 = 0xA5;
	tmp1 = 0x4F;

	SRAM_write(adapter, addr, &tmp2, 1);
	SRAM_write(adapter, addr + 4, &tmp1, 1);

	tmp2 = 0;

	mdelay(20);

	SRAM_read(adapter, addr, &tmp2, 1);
	SRAM_read(adapter, addr, &tmp2, 1);

	dprintk("%s: wrote 0xA5, read 0x%2x\n", __FUNCTION__, tmp2);

	if (tmp2 != 0xA5)
		return 0;

	tmp2 = 0x5A;
	tmp1 = 0xF4;

	SRAM_write(adapter, addr, &tmp2, 1);
	SRAM_write(adapter, addr + 4, &tmp1, 1);

	tmp2 = 0;

	mdelay(20);

	SRAM_read(adapter, addr, &tmp2, 1);
	SRAM_read(adapter, addr, &tmp2, 1);

	dprintk("%s: wrote 0x5A, read 0x%2x\n", __FUNCTION__, tmp2);

	if (tmp2 != 0x5A)
		return 0;

	return 1;
}

static u32 SRAM_length(struct adapter * adapter)
{
	if (adapter->dwSramType == 0x10000)
		return 32768;	//  32K
	if (adapter->dwSramType == 0x00000)
		return 65536;	//  64K        
	if (adapter->dwSramType == 0x20000)
		return 131072;	// 128K

	return 32768;		// 32K
}

/* FlexcopII can work with 32K, 64K or 128K of external SRAM memory.
    - for 128K there are 4x32K chips at bank 0,1,2,3.
    - for  64K there are 2x32K chips at bank 1,2.
    - for  32K there is one 32K chip at bank 0.

   FlexCop works only with one bank at a time. The bank is selected
   by bits 28-29 of the 0x700 register.
  
   bank 0 covers addresses 0x00000-0x07FFF
   bank 1 covers addresses 0x08000-0x0FFFF
   bank 2 covers addresses 0x10000-0x17FFF
   bank 3 covers addresses 0x18000-0x1FFFF
*/
static int SramDetectForFlex2(struct adapter *adapter)
{
	u32 tmp, tmp2, tmp3;

	dprintk("%s:\n", __FUNCTION__);

	tmp = ReadRegDW(adapter, 0x208);
	WriteRegDW(adapter, 0x208, 0);

	tmp2 = ReadRegDW(adapter, 0x71C);

	dprintk("%s: tmp2 = %x\n", __FUNCTION__, tmp2);

	WriteRegDW(adapter, 0x71C, 1);

	tmp3 = ReadRegDW(adapter, 0x71C);

	dprintk("%s: tmp3 = %x\n", __FUNCTION__, tmp3);

	WriteRegDW(adapter, 0x71C, tmp2);

	// check for internal SRAM ???
	tmp3--;
	if (tmp3 != 0) {
		SRAM_setSize(adapter, 0x10000);
		SRAM_init(adapter);
		WriteRegDW(adapter, 0x208, tmp);

		dprintk("%s: sram size = 32K\n", __FUNCTION__);

		return 32;
	}

	if (SRAM_testLocation(adapter, 0x20000, 0x18000) != 0) {
		SRAM_setSize(adapter, 0x20000);
		SRAM_init(adapter);
		WriteRegDW(adapter, 0x208, tmp);

		dprintk("%s: sram size = 128K\n", __FUNCTION__);

		return 128;
	}

	if (SRAM_testLocation(adapter, 0x00000, 0x10000) != 0) {
		SRAM_setSize(adapter, 0x00000);
		SRAM_init(adapter);
		WriteRegDW(adapter, 0x208, tmp);

		dprintk("%s: sram size = 64K\n", __FUNCTION__);

		return 64;
	}

	if (SRAM_testLocation(adapter, 0x10000, 0x00000) != 0) {
		SRAM_setSize(adapter, 0x10000);
		SRAM_init(adapter);
		WriteRegDW(adapter, 0x208, tmp);

		dprintk("%s: sram size = 32K\n", __FUNCTION__);

		return 32;
	}

	SRAM_setSize(adapter, 0x10000);
	SRAM_init(adapter);
	WriteRegDW(adapter, 0x208, tmp);

	dprintk("%s: SRAM detection failed. Set to 32K \n", __FUNCTION__);

	return 0;
}

static void SLL_detectSramSize(struct adapter *adapter)
{
	SramDetectForFlex2(adapter);
}
/* EEPROM (Skystar2 has one "24LC08B" chip on board) */
/*
static int EEPROM_write(struct adapter *adapter, u16 addr, u8 * buf, u16 len)
{
	return FLEXI2C_write(adapter, 0x20000000, 0x50, addr, buf, len);
}
*/

static int EEPROM_read(struct adapter *adapter, u16 addr, u8 * buf, u16 len)
{
	return FLEXI2C_read(adapter, 0x20000000, 0x50, addr, buf, len);
}

u8 calc_LRC(u8 * buf, u32 len)
{
	u32 i;
	u8 sum;

	sum = 0;

	for (i = 0; i < len; i++)
		sum = sum ^ buf[i];

	return sum;
}

static int EEPROM_LRC_read(struct adapter *adapter, u32 addr, u32 len, u8 * buf, u32 retries)
{
	int i;

	for (i = 0; i < retries; i++) {
		if (EEPROM_read(adapter, addr, buf, len) == len) {
			if (calc_LRC(buf, len - 1) == buf[len - 1])
				return 1;
		}
	}

	return 0;
}

/*
static int EEPROM_LRC_write(struct adapter *adapter, u32 addr, u32 len, u8 * wbuf, u8 * rbuf, u32 retries)
{
	int i;

	for (i = 0; i < retries; i++) {
		if (EEPROM_write(adapter, addr, wbuf, len) == len) {
			if (EEPROM_LRC_read(adapter, addr, len, rbuf, retries) == 1)
				return 1;
		}
	}

	return 0;
}
*/

/* These functions could be called from the initialization routine 
   to unlock SkyStar2 cards, locked by "Europe On Line".
        
   in cards from "Europe On Line" the key is:

       u8 key[20] = {
 	    0xB2, 0x01, 0x00, 0x00,
 	    0x00, 0x00, 0x00, 0x00,
 	    0x00, 0x00, 0x00, 0x00,
 	    0x00, 0x00, 0x00, 0x00,
       };

       LRC = 0xB3;

  in unlocked cards the key is:

       u8 key[20] = {
 	    0xB2, 0x00, 0x00, 0x00,
 	    0x00, 0x00, 0x00, 0x00,
 	    0x00, 0x00, 0x00, 0x00,
 	    0x00, 0x00, 0x00, 0x00,
       };

      LRC = 0xB2;
*/
/*
static int EEPROM_writeKey(struct adapter *adapter, u8 * key, u32 len)
{
	u8 rbuf[20];
	u8 wbuf[20];

	if (len != 16)
		return 0;

	memcpy(wbuf, key, len);

	wbuf[16] = 0;
	wbuf[17] = 0;
	wbuf[18] = 0;
	wbuf[19] = calc_LRC(wbuf, 19);

	return EEPROM_LRC_write(adapter, 0x3E4, 20, wbuf, rbuf, 4);
}
*/
static int EEPROM_readKey(struct adapter *adapter, u8 * key, u32 len)
{
	u8 buf[20];

	if (len != 16)
		return 0;

	if (EEPROM_LRC_read(adapter, 0x3E4, 20, buf, 4) == 0)
		return 0;

	memcpy(key, buf, len);

	return 1;
}

static int EEPROM_getMacAddr(struct adapter *adapter, char type, u8 * mac)
{
	u8 tmp[8];

	if (EEPROM_LRC_read(adapter, 0x3F8, 8, tmp, 4) != 0) {
		if (type != 0) {
			mac[0] = tmp[0];
			mac[1] = tmp[1];
			mac[2] = tmp[2];
			mac[3] = 0xFE;
			mac[4] = 0xFF;
			mac[5] = tmp[3];
			mac[6] = tmp[4];
			mac[7] = tmp[5];

		} else {

			mac[0] = tmp[0];
			mac[1] = tmp[1];
			mac[2] = tmp[2];
			mac[3] = tmp[3];
			mac[4] = tmp[4];
			mac[5] = tmp[5];
		}

		return 1;

	} else {

		if (type == 0) {
			memset(mac, 0, 6);

		} else {

			memset(mac, 0, 8);
		}

		return 0;
	}
}

/*
static char EEPROM_setMacAddr(struct adapter *adapter, char type, u8 * mac)
{
	u8 tmp[8];

	if (type != 0) {
		tmp[0] = mac[0];
		tmp[1] = mac[1];
		tmp[2] = mac[2];
		tmp[3] = mac[5];
		tmp[4] = mac[6];
		tmp[5] = mac[7];

	} else {

		tmp[0] = mac[0];
		tmp[1] = mac[1];
		tmp[2] = mac[2];
		tmp[3] = mac[3];
		tmp[4] = mac[4];
		tmp[5] = mac[5];
	}

	tmp[6] = 0;
	tmp[7] = calc_LRC(tmp, 7);

	if (EEPROM_write(adapter, 0x3F8, tmp, 8) == 8)
		return 1;

	return 0;
}
*/

/* PID filter */
static void FilterEnableStream1Filter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000001, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000001);
	}
}

static void FilterEnableStream2Filter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000002, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000002);
	}
}

static void FilterEnablePcrFilter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000004, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000004);
	}
}

static void FilterEnablePmtFilter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000008, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000008);
	}
}

static void FilterEnableEmmFilter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000010, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000010);
	}
}

static void FilterEnableEcmFilter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000020, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000020);
	}
}

/*
static void FilterEnableNullFilter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000040, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000040);
	}
}
*/

static void FilterEnableMaskFilter(struct adapter *adapter, u32 op)
{
	dprintk("%s: op=%x\n", __FUNCTION__, op);

	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000080, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000080);
	}
}


static void CtrlEnableMAC(struct adapter *adapter, u32 op)
{
	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00004000, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00004000);
	}
}

static int CASetMacDstAddrFilter(struct adapter *adapter, u8 * mac)
{
	u32 tmp1, tmp2;

	tmp1 = (mac[3] << 0x18) | (mac[2] << 0x10) | (mac[1] << 0x08) | mac[0];
	tmp2 = (mac[5] << 0x08) | mac[4];

	WriteRegDW(adapter, 0x418, tmp1);
	WriteRegDW(adapter, 0x41C, tmp2);

	return 0;
}

/*
static void SetIgnoreMACFilter(struct adapter *adapter, u8 op)
{
	if (op != 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00004000, 0);

		adapter->mac_filter = 1;

	} else {

		if (adapter->mac_filter != 0) {
			adapter->mac_filter = 0;

			WriteRegOp(adapter, 0x208, 1, 0, 0x00004000);
		}
	}
}
*/

/*
static void CheckNullFilterEnable(struct adapter *adapter)
{
	FilterEnableNullFilter(adapter, 1);
	FilterEnableMaskFilter(adapter, 1);
}
*/

static void InitPIDsInfo(struct adapter *adapter)
{
	int i;

	for (i = 0; i < 0x27; i++)
		adapter->pids[i] = 0x1FFF;
}

static int CheckPID(struct adapter *adapter, u16 pid)
{
	u32 i;

	if (pid == 0x1FFF)
		return 0;

	for (i = 0; i < 0x27; i++) {
		if (adapter->pids[i] == pid)
			return 1;
	}

	return 0;
}

static void PidSetGroupPID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = (pid & 0x3FFF) | (ReadRegDW(adapter, 0x30C) & 0xFFFF0000);

	WriteRegDW(adapter, 0x30C, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetGroupMASK(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = ((pid & 0x3FFF) << 0x10) | (ReadRegDW(adapter, 0x30C) & 0xFFFF);

	WriteRegDW(adapter, 0x30C, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetStream1PID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = (pid & 0x3FFF) | (ReadRegDW(adapter, 0x300) & 0xFFFFC000);

	WriteRegDW(adapter, 0x300, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetStream2PID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = ((pid & 0x3FFF) << 0x10) | (ReadRegDW(adapter, 0x300) & 0xFFFF);

	WriteRegDW(adapter, 0x300, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetPcrPID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = (pid & 0x3FFF) | (ReadRegDW(adapter, 0x304) & 0xFFFFC000);

	WriteRegDW(adapter, 0x304, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetPmtPID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = ((pid & 0x3FFF) << 0x10) | (ReadRegDW(adapter, 0x304) & 0x3FFF);

	WriteRegDW(adapter, 0x304, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetEmmPID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = (pid & 0xFFFF) | (ReadRegDW(adapter, 0x308) & 0xFFFF0000);

	WriteRegDW(adapter, 0x308, value);

	/* return value is never used? */
/*	return value; */
}

static void PidSetEcmPID(struct adapter * adapter, u32 pid)
{
	u32 value;

	dprintk("%s: pid=%x\n", __FUNCTION__, pid);

	value = (pid << 0x10) | (ReadRegDW(adapter, 0x308) & 0xFFFF);

	WriteRegDW(adapter, 0x308, value);

	/* return value is never used? */
/*	return value; */
}

static int PidGetStream1PID(struct adapter * adapter)
{
	return ReadRegDW(adapter, 0x300) & 0x00001FFF;
}

static int PidGetStream2PID(struct adapter * adapter)
{
	return (ReadRegDW(adapter, 0x300) >> 0x10)& 0x00001FFF;
}

static int PidGetPcrPID(struct adapter * adapter)
{
	return ReadRegDW(adapter, 0x304) & 0x00001FFF;
}

static int PidGetPmtPID(struct adapter * adapter)
{
	return (ReadRegDW(adapter, 0x304) >> 0x10)& 0x00001FFF;
}

static int PidGetEmmPID(struct adapter * adapter)
{
	return ReadRegDW(adapter, 0x308) & 0x00001FFF;
}

static int PidGetEcmPID(struct adapter * adapter)
{
	return (ReadRegDW(adapter, 0x308) >> 0x10)& 0x00001FFF;
}

static int PidGetGroupPID(struct adapter * adapter)
{
	return ReadRegDW(adapter, 0x30C) & 0x00001FFF;
}

static int PidGetGroupMASK(struct adapter * adapter)
{
	return (ReadRegDW(adapter, 0x30C) >> 0x10)& 0x00001FFF;
}

/*
static void ResetHardwarePIDFilter(struct adapter *adapter)
{
	PidSetStream1PID(adapter, 0x1FFF);

	PidSetStream2PID(adapter, 0x1FFF);
	FilterEnableStream2Filter(adapter, 0);

	PidSetPcrPID(adapter, 0x1FFF);
	FilterEnablePcrFilter(adapter, 0);

	PidSetPmtPID(adapter, 0x1FFF);
	FilterEnablePmtFilter(adapter, 0);

	PidSetEcmPID(adapter, 0x1FFF);
	FilterEnableEcmFilter(adapter, 0);

	PidSetEmmPID(adapter, 0x1FFF);
	FilterEnableEmmFilter(adapter, 0);
}
*/

static void OpenWholeBandwidth(struct adapter *adapter)
{
	PidSetGroupPID(adapter, 0);

	PidSetGroupMASK(adapter, 0);

	FilterEnableMaskFilter(adapter, 1);
}

static int AddHwPID(struct adapter *adapter, u32 pid)
{
	dprintk("%s: pid=%d\n", __FUNCTION__, pid);

	if (pid <= 0x1F)
		return 1;

	if ((PidGetGroupMASK(adapter) == 0) && (PidGetGroupPID(adapter) == 0))
		return 0;

	if (PidGetStream1PID(adapter) == 0x1FFF) {
		PidSetStream1PID(adapter, pid & 0xFFFF);

		FilterEnableStream1Filter(adapter, 1);

		return 1;
	}

	if (PidGetStream2PID(adapter) == 0x1FFF) {
		PidSetStream2PID(adapter, (pid & 0xFFFF));

		FilterEnableStream2Filter(adapter, 1);

		return 1;
	}

	if (PidGetPcrPID(adapter) == 0x1FFF) {
		PidSetPcrPID(adapter, (pid & 0xFFFF));

		FilterEnablePcrFilter(adapter, 1);

		return 1;
	}

	if ((PidGetPmtPID(adapter) & 0x1FFF) == 0x1FFF) {
		PidSetPmtPID(adapter, (pid & 0xFFFF));

		FilterEnablePmtFilter(adapter, 1);

		return 1;
	}

	if ((PidGetEmmPID(adapter) & 0x1FFF) == 0x1FFF) {
		PidSetEmmPID(adapter, (pid & 0xFFFF));

		FilterEnableEmmFilter(adapter, 1);

		return 1;
	}

	if ((PidGetEcmPID(adapter) & 0x1FFF) == 0x1FFF) {
		PidSetEcmPID(adapter, (pid & 0xFFFF));

		FilterEnableEcmFilter(adapter, 1);

		return 1;
	}

	return -1;
}

static int RemoveHwPID(struct adapter *adapter, u32 pid)
{
	dprintk("%s: pid=%d\n", __FUNCTION__, pid);

	if (pid <= 0x1F)
		return 1;

	if (PidGetStream1PID(adapter) == pid) {
		PidSetStream1PID(adapter, 0x1FFF);

		return 1;
	}

	if (PidGetStream2PID(adapter) == pid) {
		PidSetStream2PID(adapter, 0x1FFF);

		FilterEnableStream2Filter(adapter, 0);

		return 1;
	}

	if (PidGetPcrPID(adapter) == pid) {
		PidSetPcrPID(adapter, 0x1FFF);

		FilterEnablePcrFilter(adapter, 0);

		return 1;
	}

	if (PidGetPmtPID(adapter) == pid) {
		PidSetPmtPID(adapter, 0x1FFF);

		FilterEnablePmtFilter(adapter, 0);

		return 1;
	}

	if (PidGetEmmPID(adapter) == pid) {
		PidSetEmmPID(adapter, 0x1FFF);

		FilterEnableEmmFilter(adapter, 0);

		return 1;
	}

	if (PidGetEcmPID(adapter) == pid) {
		PidSetEcmPID(adapter, 0x1FFF);

		FilterEnableEcmFilter(adapter, 0);

		return 1;
	}

	return -1;
}

static int AddPID(struct adapter *adapter, u32 pid)
{
	int i;

	dprintk("%s: pid=%d\n", __FUNCTION__, pid);

	if (pid > 0x1FFE)
		return -1;

	if (CheckPID(adapter, pid) == 1)
		return 1;

	for (i = 0; i < 0x27; i++) {
		if (adapter->pids[i] == 0x1FFF)	// find free pid filter
		{
			adapter->pids[i] = pid;

			if (AddHwPID(adapter, pid) < 0)
				OpenWholeBandwidth(adapter);

			return 1;
		}
	}

	return -1;
}

static int RemovePID(struct adapter *adapter, u32 pid)
{
	u32 i;

	dprintk("%s: pid=%d\n", __FUNCTION__, pid);

	if (pid > 0x1FFE)
		return -1;

	for (i = 0; i < 0x27; i++) {
		if (adapter->pids[i] == pid) {
			adapter->pids[i] = 0x1FFF;

			RemoveHwPID(adapter, pid);

			return 1;
		}
	}

	return -1;
}

/* dma & irq */
static void CtrlEnableSmc(struct adapter *adapter, u32 op)
{
	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00000800, 0);

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00000800);
	}
}

static void DmaEnableDisableIrq(struct adapter *adapter, u32 flag1, u32 flag2, u32 flag3)
{
	adapter->dma_ctrl = adapter->dma_ctrl & 0x000F0000;

	if (flag1 == 0) {
		if (flag2 == 0)
			adapter->dma_ctrl = adapter->dma_ctrl & ~0x00010000;
		else
			adapter->dma_ctrl = adapter->dma_ctrl | 0x00010000;

		if (flag3 == 0)
			adapter->dma_ctrl = adapter->dma_ctrl & ~0x00020000;
		else
			adapter->dma_ctrl = adapter->dma_ctrl | 0x00020000;

	} else {

		if (flag2 == 0)
			adapter->dma_ctrl = adapter->dma_ctrl & ~0x00040000;
		else
			adapter->dma_ctrl = adapter->dma_ctrl | 0x00040000;

		if (flag3 == 0)
			adapter->dma_ctrl = adapter->dma_ctrl & ~0x00080000;
		else
			adapter->dma_ctrl = adapter->dma_ctrl | 0x00080000;
	}
}

static void IrqDmaEnableDisableIrq(struct adapter * adapter, u32 op)
{
	u32 value;

	value = ReadRegDW(adapter, 0x208) & 0xFFF0FFFF;

	if (op != 0)
		value = value | (adapter->dma_ctrl & 0x000F0000);

	WriteRegDW(adapter, 0x208, value);
}

/* FlexCopII has 2 dma channels. DMA1 is used to transfer TS data to
   system memory.

   The DMA1 buffer is divided in 2 subbuffers of equal size.
   FlexCopII will transfer TS data to one subbuffer, signal an interrupt
   when the subbuffer is full and continue fillig the second subbuffer.

   For DMA1:
       subbuffer size in 32-bit words is stored in the first 24 bits of
       register 0x004. The last 8 bits of register 0x004 contain the number
       of subbuffers.
       
       the first 30 bits of register 0x000 contain the address of the first
       subbuffer. The last 2 bits contain 0, when dma1 is disabled and 1,
       when dma1 is enabled.

       the first 30 bits of register 0x00C contain the address of the second
       subbuffer. the last 2 bits contain 1.

       register 0x008 will contain the address of the subbuffer that was filled
       with TS data, when FlexCopII will generate an interrupt.

   For DMA2:
       subbuffer size in 32-bit words is stored in the first 24 bits of
       register 0x014. The last 8 bits of register 0x014 contain the number
       of subbuffers.
       
       the first 30 bits of register 0x010 contain the address of the first
       subbuffer.  The last 2 bits contain 0, when dma1 is disabled and 1,
       when dma1 is enabled.

       the first 30 bits of register 0x01C contain the address of the second
       subbuffer. the last 2 bits contain 1.

       register 0x018 contains the address of the subbuffer that was filled
       with TS data, when FlexCopII generates an interrupt.
*/
static int DmaInitDMA(struct adapter *adapter, u32 dma_channel)
{
	u32 subbuffers, subbufsize, subbuf0, subbuf1;

	if (dma_channel == 0) {
		dprintk("%s: Initializing DMA1 channel\n", __FUNCTION__);

		subbuffers = 2;

		subbufsize = (((adapter->dmaq1.buffer_size / 2) / 4) << 8) | subbuffers;

		subbuf0 = adapter->dmaq1.bus_addr & 0xFFFFFFFC;

		subbuf1 = ((adapter->dmaq1.bus_addr + adapter->dmaq1.buffer_size / 2) & 0xFFFFFFFC) | 1;

		dprintk("%s: first subbuffer address = 0x%x\n", __FUNCTION__, subbuf0);
		udelay(1000);
		WriteRegDW(adapter, 0x000, subbuf0);

		dprintk("%s: subbuffer size = 0x%x\n", __FUNCTION__, (subbufsize >> 8) * 4);
		udelay(1000);
		WriteRegDW(adapter, 0x004, subbufsize);

		dprintk("%s: second subbuffer address = 0x%x\n", __FUNCTION__, subbuf1);
		udelay(1000);
		WriteRegDW(adapter, 0x00C, subbuf1);

		dprintk("%s: counter = 0x%x\n", __FUNCTION__, adapter->dmaq1.bus_addr & 0xFFFFFFFC);
		WriteRegDW(adapter, 0x008, adapter->dmaq1.bus_addr & 0xFFFFFFFC);
		udelay(1000);

		if (subbuffers == 0)
			DmaEnableDisableIrq(adapter, 0, 1, 0);
		else
			DmaEnableDisableIrq(adapter, 0, 1, 1);

		IrqDmaEnableDisableIrq(adapter, 1);

		SRAMSetMediaDest(adapter, 1);
		SRAMSetNetDest(adapter, 1);
		SRAMSetCaiDest(adapter, 2);
		SRAMSetCaoDest(adapter, 2);
	}

	if (dma_channel == 1) {
		dprintk("%s: Initializing DMA2 channel\n", __FUNCTION__);

		subbuffers = 2;

		subbufsize = (((adapter->dmaq2.buffer_size / 2) / 4) << 8) | subbuffers;

		subbuf0 = adapter->dmaq2.bus_addr & 0xFFFFFFFC;

		subbuf1 = ((adapter->dmaq2.bus_addr + adapter->dmaq2.buffer_size / 2) & 0xFFFFFFFC) | 1;

		dprintk("%s: first subbuffer address = 0x%x\n", __FUNCTION__, subbuf0);
		udelay(1000);
		WriteRegDW(adapter, 0x010, subbuf0);

		dprintk("%s: subbuffer size = 0x%x\n", __FUNCTION__, (subbufsize >> 8) * 4);
		udelay(1000);
		WriteRegDW(adapter, 0x014, subbufsize);

		dprintk("%s: second buffer address = 0x%x\n", __FUNCTION__, subbuf1);
		udelay(1000);
		WriteRegDW(adapter, 0x01C, subbuf1);

		SRAMSetCaiDest(adapter, 2);
	}

	return 0;
}

static void CtrlEnableReceiveData(struct adapter *adapter, u32 op)
{
	if (op == 0) {
		WriteRegOp(adapter, 0x208, 2, ~0x00008000, 0);

		adapter->dma_status = adapter->dma_status & ~0x00000004;

	} else {

		WriteRegOp(adapter, 0x208, 1, 0, 0x00008000);

		adapter->dma_status = adapter->dma_status | 0x00000004;
	}
}

/* bit 0 of dma_mask is set to 1 if dma1 channel has to be enabled/disabled
   bit 1 of dma_mask is set to 1 if dma2 channel has to be enabled/disabled
*/
static void DmaStartStop0x2102(struct adapter *adapter, u32 dma_mask, u32 start_stop)
{
	u32 dma_enable, dma1_enable, dma2_enable;

	dprintk("%s: dma_mask=%x\n", __FUNCTION__, dma_mask);

	if (start_stop == 1) {
		dprintk("%s: starting dma\n", __FUNCTION__);

		dma1_enable = 0;
		dma2_enable = 0;

		if (((dma_mask & 1) != 0) && ((adapter->dma_status & 1) == 0) && (adapter->dmaq1.bus_addr != 0)) {
			adapter->dma_status = adapter->dma_status | 1;
			dma1_enable = 1;
		}

		if (((dma_mask & 2) != 0) && ((adapter->dma_status & 2) == 0) && (adapter->dmaq2.bus_addr != 0)) {
			adapter->dma_status = adapter->dma_status | 2;
			dma2_enable = 1;
		}
		// enable dma1 and dma2
		if ((dma1_enable == 1) && (dma2_enable == 1)) {
			WriteRegDW(adapter, 0x000, adapter->dmaq1.bus_addr | 1);
			WriteRegDW(adapter, 0x00C, (adapter->dmaq1.bus_addr + adapter->dmaq1.buffer_size / 2) | 1);
			WriteRegDW(adapter, 0x010, adapter->dmaq2.bus_addr | 1);

			CtrlEnableReceiveData(adapter, 1);

			return;
		}
		// enable dma1
		if ((dma1_enable == 1) && (dma2_enable == 0)) {
			WriteRegDW(adapter, 0x000, adapter->dmaq1.bus_addr | 1);
			WriteRegDW(adapter, 0x00C, (adapter->dmaq1.bus_addr + adapter->dmaq1.buffer_size / 2) | 1);

			CtrlEnableReceiveData(adapter, 1);

			return;
		}
		// enable dma2
		if ((dma1_enable == 0) && (dma2_enable == 1)) {
			WriteRegDW(adapter, 0x010, adapter->dmaq2.bus_addr | 1);

			CtrlEnableReceiveData(adapter, 1);

			return;
		}
		// start dma
		if ((dma1_enable == 0) && (dma2_enable == 0)) {
			CtrlEnableReceiveData(adapter, 1);

			return;
		}

	} else {

		dprintk("%s: stoping dma\n", __FUNCTION__);

		dma_enable = adapter->dma_status & 0x00000003;

		if (((dma_mask & 1) != 0) && ((adapter->dma_status & 1) != 0)) {
			dma_enable = dma_enable & 0xFFFFFFFE;
		}

		if (((dma_mask & 2) != 0) && ((adapter->dma_status & 2) != 0)) {
			dma_enable = dma_enable & 0xFFFFFFFD;
		}
		//stop dma
		if ((dma_enable == 0) && ((adapter->dma_status & 4) != 0)) {
			CtrlEnableReceiveData(adapter, 0);

			udelay(3000);
		}
		//disable dma1
		if (((dma_mask & 1) != 0) && ((adapter->dma_status & 1) != 0) && (adapter->dmaq1.bus_addr != 0)) {
			WriteRegDW(adapter, 0x000, adapter->dmaq1.bus_addr);
			WriteRegDW(adapter, 0x00C, (adapter->dmaq1.bus_addr + adapter->dmaq1.buffer_size / 2) | 1);

			adapter->dma_status = adapter->dma_status & ~0x00000001;
		}
		//disable dma2
		if (((dma_mask & 2) != 0) && ((adapter->dma_status & 2) != 0) && (adapter->dmaq2.bus_addr != 0)) {
			WriteRegDW(adapter, 0x010, adapter->dmaq2.bus_addr);

			adapter->dma_status = adapter->dma_status & ~0x00000002;
		}
	}
}

static void OpenStream(struct adapter *adapter, u32 pid)
{
	u32 dma_mask;

	if (adapter->capturing == 0)
		adapter->capturing = 1;

	FilterEnableMaskFilter(adapter, 1);

	AddPID(adapter, pid);

	dprintk("%s: adapter->dma_status=%x\n", __FUNCTION__, adapter->dma_status);

	if ((adapter->dma_status & 7) != 7) {
		dma_mask = 0;

		if (((adapter->dma_status & 0x10000000) != 0) && ((adapter->dma_status & 1) == 0)) {
			dma_mask = dma_mask | 1;

			adapter->dmaq1.head = 0;
			adapter->dmaq1.tail = 0;

			memset(adapter->dmaq1.buffer, 0, adapter->dmaq1.buffer_size);
		}

		if (((adapter->dma_status & 0x20000000) != 0) && ((adapter->dma_status & 2) == 0)) {
			dma_mask = dma_mask | 2;

			adapter->dmaq2.head = 0;
			adapter->dmaq2.tail = 0;
		}

		if (dma_mask != 0) {
			IrqDmaEnableDisableIrq(adapter, 1);

			DmaStartStop0x2102(adapter, dma_mask, 1);
		}
	}
}

static void CloseStream(struct adapter *adapter, u32 pid)
{
	u32 dma_mask;

	if (adapter->capturing != 0)
		adapter->capturing = 0;

	dprintk("%s: dma_status=%x\n", __FUNCTION__, adapter->dma_status);

	dma_mask = 0;

	if ((adapter->dma_status & 1) != 0)
		dma_mask = dma_mask | 0x00000001;
	if ((adapter->dma_status & 2) != 0)
		dma_mask = dma_mask | 0x00000002;

	if (dma_mask != 0) {
		DmaStartStop0x2102(adapter, dma_mask, 0);
	}

	RemovePID(adapter, pid);
}

static void InterruptServiceDMA1(struct adapter *adapter)
{
	struct dvb_demux *dvbdmx = &adapter->demux;
	struct packet_header packet_header;

	int nCurDmaCounter;
	u32 nNumBytesParsed;
	u32 nNumNewBytesTransferred;
	u32 dwDefaultPacketSize = 188;
	u8 gbTmpBuffer[188];
	u8 *pbDMABufCurPos;

	nCurDmaCounter = readl(adapter->io_mem + 0x008) - adapter->dmaq1.bus_addr;
	nCurDmaCounter = (nCurDmaCounter / dwDefaultPacketSize) * dwDefaultPacketSize;

	if ((nCurDmaCounter < 0) || (nCurDmaCounter > adapter->dmaq1.buffer_size)) {
		dprintk("%s: dma counter outside dma buffer\n", __FUNCTION__);
		return;
	}

	adapter->dmaq1.head = nCurDmaCounter;

	if (adapter->dmaq1.tail <= nCurDmaCounter) {
		nNumNewBytesTransferred = nCurDmaCounter - adapter->dmaq1.tail;

	} else {

		nNumNewBytesTransferred = (adapter->dmaq1.buffer_size - adapter->dmaq1.tail) + nCurDmaCounter;
	}

//  dprintk("%s: nCurDmaCounter   = %d\n" , __FUNCTION__, nCurDmaCounter);
//	dprintk("%s: dmaq1.tail       = %d\n" , __FUNCTION__, adapter->dmaq1.tail):
//  dprintk("%s: BytesTransferred = %d\n" , __FUNCTION__, nNumNewBytesTransferred);

	if (nNumNewBytesTransferred < dwDefaultPacketSize)
		return;

	nNumBytesParsed = 0;

	while (nNumBytesParsed < nNumNewBytesTransferred) {
		pbDMABufCurPos = adapter->dmaq1.buffer + adapter->dmaq1.tail;

		if (adapter->dmaq1.buffer + adapter->dmaq1.buffer_size < adapter->dmaq1.buffer + adapter->dmaq1.tail + 188) {
			memcpy(gbTmpBuffer, adapter->dmaq1.buffer + adapter->dmaq1.tail, adapter->dmaq1.buffer_size - adapter->dmaq1.tail);
			memcpy(gbTmpBuffer + (adapter->dmaq1.buffer_size - adapter->dmaq1.tail), adapter->dmaq1.buffer, (188 - (adapter->dmaq1.buffer_size - adapter->dmaq1.tail)));

			pbDMABufCurPos = gbTmpBuffer;
		}

		if (adapter->capturing != 0) {
			u32 *dq = (u32 *) pbDMABufCurPos;

			packet_header.sync_byte = *dq & 0x000000FF;
			packet_header.transport_error_indicator = *dq & 0x00008000;
			packet_header.payload_unit_start_indicator = *dq & 0x00004000;
			packet_header.transport_priority = *dq & 0x00002000;
			packet_header.pid = ((*dq & 0x00FF0000) >> 0x10) | (*dq & 0x00001F00);
			packet_header.transport_scrambling_control = *dq >> 0x1E;
			packet_header.adaptation_field_control = (*dq & 0x30000000) >> 0x1C;
			packet_header.continuity_counter = (*dq & 0x0F000000) >> 0x18;

			if ((packet_header.sync_byte == 0x47) && (packet_header.transport_error_indicator == 0) && (packet_header.pid != 0x1FFF)) {
				if (CheckPID(adapter, packet_header.pid & 0x0000FFFF) != 0) {
					dvb_dmx_swfilter_packets(dvbdmx, pbDMABufCurPos, dwDefaultPacketSize / 188);

				} else {

//                  dprintk("%s: pid=%x\n", __FUNCTION__, packet_header.pid);
				}
			}
		}

		nNumBytesParsed = nNumBytesParsed + dwDefaultPacketSize;

		adapter->dmaq1.tail = adapter->dmaq1.tail + dwDefaultPacketSize;

		if (adapter->dmaq1.tail >= adapter->dmaq1.buffer_size)
			adapter->dmaq1.tail = adapter->dmaq1.tail - adapter->dmaq1.buffer_size;
	};
}

static void InterruptServiceDMA2(struct adapter *adapter)
{
	printk("%s:\n", __FUNCTION__);
}

static irqreturn_t isr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct adapter *tmp = dev_id;

	u32 value;

//  dprintk("%s:\n", __FUNCTION__);

	spin_lock_irq(&tmp->lock);

	if (0 == ((value = ReadRegDW(tmp, 0x20C)) & 0x0F)) {
		spin_unlock_irq(&tmp->lock);
		return IRQ_NONE;
	}
	
	while (value != 0) {
		if ((value & 0x03) != 0)
			InterruptServiceDMA1(tmp);
		if ((value & 0x0C) != 0)
			InterruptServiceDMA2(tmp);
		value = ReadRegDW(tmp, 0x20C) & 0x0F;
	}

	spin_unlock_irq(&tmp->lock);
	return IRQ_HANDLED;
}

static void Initdmaqueue(struct adapter *adapter)
{
	dma_addr_t dma_addr;

	if (adapter->dmaq1.buffer != 0)
		return;

	adapter->dmaq1.head = 0;
	adapter->dmaq1.tail = 0;
	adapter->dmaq1.buffer = 0;

	adapter->dmaq1.buffer = pci_alloc_consistent(adapter->pdev, SizeOfBufDMA1 + 0x80, &dma_addr);

	if (adapter->dmaq1.buffer != 0) {
		memset(adapter->dmaq1.buffer, 0, SizeOfBufDMA1);

		adapter->dmaq1.bus_addr = dma_addr;
		adapter->dmaq1.buffer_size = SizeOfBufDMA1;

		DmaInitDMA(adapter, 0);

		adapter->dma_status = adapter->dma_status | 0x10000000;

		dprintk("%s: allocated dma buffer at 0x%p, length=%d\n", __FUNCTION__, adapter->dmaq1.buffer, SizeOfBufDMA1);

	} else {

		adapter->dma_status = adapter->dma_status & ~0x10000000;
	}

	if (adapter->dmaq2.buffer != 0)
		return;

	adapter->dmaq2.head = 0;
	adapter->dmaq2.tail = 0;
	adapter->dmaq2.buffer = 0;

	adapter->dmaq2.buffer = pci_alloc_consistent(adapter->pdev, SizeOfBufDMA2 + 0x80, &dma_addr);

	if (adapter->dmaq2.buffer != 0) {
		memset(adapter->dmaq2.buffer, 0, SizeOfBufDMA2);

		adapter->dmaq2.bus_addr = dma_addr;
		adapter->dmaq2.buffer_size = SizeOfBufDMA2;

		DmaInitDMA(adapter, 1);

		adapter->dma_status = adapter->dma_status | 0x20000000;

		dprintk("%s: allocated dma buffer at 0x%p, length=%d\n", __FUNCTION__, adapter->dmaq2.buffer, (int) SizeOfBufDMA2);

	} else {

		adapter->dma_status = adapter->dma_status & ~0x20000000;
	}
}

static void Freedmaqueue(struct adapter *adapter)
{
	if (adapter->dmaq1.buffer != 0) {
		pci_free_consistent(adapter->pdev, SizeOfBufDMA1 + 0x80, adapter->dmaq1.buffer, adapter->dmaq1.bus_addr);

		adapter->dmaq1.bus_addr = 0;
		adapter->dmaq1.head = 0;
		adapter->dmaq1.tail = 0;
		adapter->dmaq1.buffer_size = 0;
		adapter->dmaq1.buffer = 0;
	}

	if (adapter->dmaq2.buffer != 0) {
		pci_free_consistent(adapter->pdev, SizeOfBufDMA2 + 0x80, adapter->dmaq2.buffer, adapter->dmaq2.bus_addr);

		adapter->dmaq2.bus_addr = 0;
		adapter->dmaq2.head = 0;
		adapter->dmaq2.tail = 0;
		adapter->dmaq2.buffer_size = 0;
		adapter->dmaq2.buffer = 0;
	}
}

static void FreeAdapterObject(struct adapter *adapter)
{
	dprintk("%s:\n", __FUNCTION__);

	CloseStream(adapter, 0);

	if (adapter->irq != 0)
		free_irq(adapter->irq, adapter);

	Freedmaqueue(adapter);

	if (adapter->io_mem != 0)
		iounmap((void *) adapter->io_mem);

	if (adapter != 0)
		kfree(adapter);
}

static struct pci_driver skystar2_pci_driver;

static int ClaimAdapter(struct adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	u16 var;

	if (!request_region(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1), skystar2_pci_driver.name))
		return -EBUSY;

	if (!request_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0), skystar2_pci_driver.name))
		return -EBUSY;

	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &adapter->card_revision);

	dprintk("%s: card revision %x \n", __FUNCTION__, adapter->card_revision);

	if (pci_enable_device(pdev))
		return -EIO;

	pci_read_config_word(pdev, 4, &var);

	if ((var & 4) == 0)
		pci_set_master(pdev);

	adapter->io_port = pdev->resource[1].start;

	adapter->io_mem = (unsigned long) ioremap(pdev->resource[0].start, 0x800);

	if (adapter->io_mem == 0) {
		dprintk("%s: can not map io memory\n", __FUNCTION__);

		return 2;
	}

	dprintk("%s: io memory maped at %lx\n", __FUNCTION__, adapter->io_mem);

	return 1;
}

/*
static int SLL_reset_FlexCOP(struct adapter *adapter)
{
	WriteRegDW(adapter, 0x208, 0);
	WriteRegDW(adapter, 0x210, 0xB2FF);

	return 0;
}
*/

static int DriverInitialize(struct pci_dev * pdev)
{
	struct adapter *adapter;
	u32 tmp;
	u8 key[16];

	if (!(adapter = kmalloc(sizeof(struct adapter), GFP_KERNEL))) {
		dprintk("%s: out of memory!\n", __FUNCTION__);

		return -ENOMEM;
	}

	memset(adapter, 0, sizeof(struct adapter));

	pci_set_drvdata(pdev,adapter);

	adapter->pdev = pdev;
	adapter->irq = pdev->irq;

	if ((ClaimAdapter(adapter)) != 1) {
		FreeAdapterObject(adapter);

		return -ENODEV;
	}

	IrqDmaEnableDisableIrq(adapter, 0);

	if (request_irq(pdev->irq, isr, 0x4000000, "Skystar2", adapter) != 0) {
		dprintk("%s: unable to allocate irq=%d !\n", __FUNCTION__, pdev->irq);

		FreeAdapterObject(adapter);

		return -ENODEV;
	}

	ReadRegDW(adapter, 0x208);
	WriteRegDW(adapter, 0x208, 0);
	WriteRegDW(adapter, 0x210, 0xB2FF);
	WriteRegDW(adapter, 0x208, 0x40);

	InitPIDsInfo(adapter);

	PidSetGroupPID(adapter, 0);
	PidSetGroupMASK(adapter, 0x1FE0);
	PidSetStream1PID(adapter, 0x1FFF);
	PidSetStream2PID(adapter, 0x1FFF);
	PidSetPmtPID(adapter, 0x1FFF);
	PidSetPcrPID(adapter, 0x1FFF);
	PidSetEcmPID(adapter, 0x1FFF);
	PidSetEmmPID(adapter, 0x1FFF);

	Initdmaqueue(adapter);

	if ((adapter->dma_status & 0x30000000) == 0) {
		FreeAdapterObject(adapter);

		return -ENODEV;
	}

	adapter->b2c2_revision = (ReadRegDW(adapter, 0x204) >> 0x18);

	if ((adapter->b2c2_revision != 0x82) && (adapter->b2c2_revision != 0xC3))
		if (adapter->b2c2_revision != 0x82) {
			dprintk("%s: The revision of the FlexCopII chip on your card is - %d\n", __FUNCTION__, adapter->b2c2_revision);
			dprintk("%s: This driver works now only with FlexCopII(rev.130) and FlexCopIIB(rev.195).\n", __FUNCTION__);

			FreeAdapterObject(adapter);

			return -ENODEV;
		}

	tmp = ReadRegDW(adapter, 0x204);

	WriteRegDW(adapter, 0x204, 0);
	mdelay(20);

	WriteRegDW(adapter, 0x204, tmp);
	mdelay(10);

	tmp = ReadRegDW(adapter, 0x308);
	WriteRegDW(adapter, 0x308, 0x4000 | tmp);

	adapter->dwSramType = 0x10000;

	SLL_detectSramSize(adapter);

	dprintk("%s sram length = %d, sram type= %x\n", __FUNCTION__, SRAM_length(adapter), adapter->dwSramType);

	SRAMSetMediaDest(adapter, 1);
	SRAMSetNetDest(adapter, 1);

	CtrlEnableSmc(adapter, 0);

	SRAMSetCaiDest(adapter, 2);
	SRAMSetCaoDest(adapter, 2);

	DmaEnableDisableIrq(adapter, 1, 0, 0);

	if (EEPROM_getMacAddr(adapter, 0, adapter->mac_addr) != 0) {
		printk("%s MAC address = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x \n", __FUNCTION__, adapter->mac_addr[0], adapter->mac_addr[1], adapter->mac_addr[2], adapter->mac_addr[3], adapter->mac_addr[4], adapter->mac_addr[5], adapter->mac_addr[6], adapter->mac_addr[7]
		    );

		CASetMacDstAddrFilter(adapter, adapter->mac_addr);
		CtrlEnableMAC(adapter, 1);
	}

	EEPROM_readKey(adapter, key, 16);

	printk("%s key = \n %02x %02x %02x %02x \n %02x %02x %02x %02x \n %02x %02x %02x %02x \n %02x %02x %02x %02x \n", __FUNCTION__, key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7], key[8], key[9], key[10], key[11], key[12], key[13], key[14], key[15]);

	adapter->lock = SPIN_LOCK_UNLOCKED;

	return 0;
}

static void DriverHalt(struct pci_dev *pdev)
{
	struct adapter *adapter;

	adapter = pci_get_drvdata(pdev);

	IrqDmaEnableDisableIrq(adapter, 0);

	CtrlEnableReceiveData(adapter, 0);

	FreeAdapterObject(adapter);

	pci_set_drvdata(pdev, NULL);

	release_region(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1));

	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
}

static int dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct adapter *adapter = (struct adapter *) dvbdmx->priv;

	dprintk("%s: PID=%d, type=%d\n", __FUNCTION__, dvbdmxfeed->pid, dvbdmxfeed->type);

	OpenStream(adapter, dvbdmxfeed->pid);

	return 0;
}

static int dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct adapter *adapter = (struct adapter *) dvbdmx->priv;

	dprintk("%s: PID=%d, type=%d\n", __FUNCTION__, dvbdmxfeed->pid, dvbdmxfeed->type);

	CloseStream(adapter, dvbdmxfeed->pid);

	return 0;
}

/* lnb control */
static void set_tuner_tone(struct adapter *adapter, u8 tone)
{
	u16 wzHalfPeriodFor45MHz[] = { 0x01FF, 0x0154, 0x00FF, 0x00CC };
	u16 ax;

	dprintk("%s: %u\n", __FUNCTION__, tone);

	switch (tone) {
	case 1:
		ax = wzHalfPeriodFor45MHz[0];
		break;
	case 2:
		ax = wzHalfPeriodFor45MHz[1];
		break;
	case 3:
		ax = wzHalfPeriodFor45MHz[2];
		break;
	case 4:
		ax = wzHalfPeriodFor45MHz[3];
		break;

	default:
		ax = 0;
	}

	if (ax != 0) {
		WriteRegDW(adapter, 0x200, ((ax << 0x0F) + (ax & 0x7FFF)) | 0x40000000);

	} else {

		WriteRegDW(adapter, 0x200, 0x40FF8000);
	}
}

static void set_tuner_polarity(struct adapter *adapter, u8 polarity)
{
	u32 var;

	dprintk("%s : polarity = %u \n", __FUNCTION__, polarity);

	var = ReadRegDW(adapter, 0x204);

	if (polarity == 0) {
		dprintk("%s: LNB power off\n", __FUNCTION__);
		var = var | 1;
	};

	if (polarity == 1) {
		var = var & ~1;
		var = var & ~4;
	};

	if (polarity == 2) {
		var = var & ~1;
		var = var | 4;
	}

	WriteRegDW(adapter, 0x204, var);
}

static int flexcop_diseqc_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct adapter *adapter = fe->before_after_data;

	switch (cmd) {
	case FE_SLEEP:
		{
			printk("%s: FE_SLEEP\n", __FUNCTION__);

			set_tuner_polarity(adapter, 0);

			// return -EOPNOTSUPP, to make DVB core also send "FE_SLEEP" command to frontend.
			return -EOPNOTSUPP;
		}

	case FE_SET_VOLTAGE:
		{
			dprintk("%s: FE_SET_VOLTAGE\n", __FUNCTION__);

			switch ((fe_sec_voltage_t) arg) {
			case SEC_VOLTAGE_13:

				printk("%s: SEC_VOLTAGE_13, %x\n", __FUNCTION__, SEC_VOLTAGE_13);

				set_tuner_polarity(adapter, 1);

				break;

			case SEC_VOLTAGE_18:

				printk("%s: SEC_VOLTAGE_18, %x\n", __FUNCTION__, SEC_VOLTAGE_18);

				set_tuner_polarity(adapter, 2);

				break;

			default:

				return -EINVAL;
			};

			break;
		}

	case FE_SET_TONE:
		{
			dprintk("%s: FE_SET_TONE\n", __FUNCTION__);

			switch ((fe_sec_tone_mode_t) arg) {
			case SEC_TONE_ON:

				printk("%s: SEC_TONE_ON, %x\n", __FUNCTION__, SEC_TONE_ON);

				set_tuner_tone(adapter, 1);

				break;

			case SEC_TONE_OFF:

				printk("%s: SEC_TONE_OFF, %x\n", __FUNCTION__, SEC_TONE_OFF);

				set_tuner_tone(adapter, 0);

				break;

			default:

				return -EINVAL;
			};

			break;
		}

	default:

		return -EOPNOTSUPP;
	};

	return 0;
}

static int skystar2_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct adapter *adapter;
	struct dvb_adapter *dvb_adapter;
	struct dvb_demux *dvbdemux;

	int ret;

	if (pdev == NULL)
		return -ENODEV;

	if (DriverInitialize(pdev) != 0)
		return -ENODEV;

	dvb_register_adapter(&dvb_adapter, skystar2_pci_driver.name);

	if (dvb_adapter == NULL) {
		printk("%s: Error registering DVB adapter\n", __FUNCTION__);

		DriverHalt(pdev);

		return -ENODEV;
	}

	adapter = (struct adapter *) pci_get_drvdata(pdev);

	adapter->dvb_adapter = dvb_adapter;

	init_MUTEX(&adapter->i2c_sem);

	adapter->i2c_bus = dvb_register_i2c_bus(master_xfer, adapter, adapter->dvb_adapter, 0);

	if (!adapter->i2c_bus)
		return -ENOMEM;

	dvb_add_frontend_ioctls(adapter->dvb_adapter, flexcop_diseqc_ioctl, NULL, adapter);

	dvbdemux = &adapter->demux;

	dvbdemux->priv = (void *) adapter;
	dvbdemux->filternum = 32;
	dvbdemux->feednum = 32;
	dvbdemux->start_feed = dvb_start_feed;
	dvbdemux->stop_feed = dvb_stop_feed;
	dvbdemux->write_to_decoder = 0;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING | DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING);

	dvb_dmx_init(&adapter->demux);

	adapter->hw_frontend.source = DMX_FRONTEND_0;

	adapter->dmxdev.filternum = 32;
	adapter->dmxdev.demux = &dvbdemux->dmx;
	adapter->dmxdev.capabilities = 0;

	dvb_dmxdev_init(&adapter->dmxdev, adapter->dvb_adapter);

	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &adapter->hw_frontend);
	if (ret < 0)
		return ret;

	adapter->mem_frontend.source = DMX_MEMORY_FE;

	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &adapter->mem_frontend);
	if (ret < 0)
		return ret;

	ret = dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, &adapter->hw_frontend);
	if (ret < 0)
		return ret;

	dvb_net_init(adapter->dvb_adapter, &adapter->dvbnet, &dvbdemux->dmx);
	return 0;
}

static void skystar2_remove(struct pci_dev *pdev)
{
	struct adapter *adapter;
	struct dvb_demux *dvbdemux;

	if (pdev == NULL)
		return;

	adapter = pci_get_drvdata(pdev);

	if (adapter != NULL) {
		dvb_net_release(&adapter->dvbnet);
		dvbdemux = &adapter->demux;

		dvbdemux->dmx.close(&dvbdemux->dmx);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &adapter->hw_frontend);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &adapter->mem_frontend);

		dvb_dmxdev_release(&adapter->dmxdev);
		dvb_dmx_release(&adapter->demux);

		if (adapter->dvb_adapter != NULL) {
			dvb_remove_frontend_ioctls(adapter->dvb_adapter, flexcop_diseqc_ioctl, NULL);

			if (adapter->i2c_bus != NULL)
				dvb_unregister_i2c_bus(master_xfer, adapter->i2c_bus->adapter, adapter->i2c_bus->id);

			dvb_unregister_adapter(adapter->dvb_adapter);
		}

		DriverHalt(pdev);
	}
}

static struct pci_device_id skystar2_pci_tbl[] = {
	{0x000013D0, 0x00002103, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000},
	{0,},
};

static struct pci_driver skystar2_pci_driver = {
	.name = "Technisat SkyStar2 driver",
	.id_table = skystar2_pci_tbl,
	.probe = skystar2_probe,
	.remove = skystar2_remove,
};

static int skystar2_init(void)
{
	return pci_module_init(&skystar2_pci_driver);
}

static void skystar2_cleanup(void)
{
	pci_unregister_driver(&skystar2_pci_driver);
}

module_init(skystar2_init);
module_exit(skystar2_cleanup);

MODULE_DESCRIPTION("Technisat SkyStar2 DVB PCI Driver");
MODULE_LICENSE("GPL");
