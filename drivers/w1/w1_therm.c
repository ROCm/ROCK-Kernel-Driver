/*
 * 	w1_therm.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the therms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>

#include "w1.h"
#include "w1_io.h"
#include "w1_int.h"
#include "w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, temperature family.");

static ssize_t w1_therm_read_name(struct device *, char *);
static ssize_t w1_therm_read_temp(struct device *, char *);
static ssize_t w1_therm_read_bin(struct kobject *, char *, loff_t, size_t);

static struct w1_family_ops w1_therm_fops = {
	.rname = &w1_therm_read_name,
	.rbin = &w1_therm_read_bin,
	.rval = &w1_therm_read_temp,
	.rvalname = "temp1_input",
};

static ssize_t w1_therm_read_name(struct device *dev, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	return sprintf(buf, "%s\n", sl->name);
}

static ssize_t w1_therm_read_temp(struct device *dev, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	s16 temp;

	/* 
	 * Must be more precise.
	 */
	temp = 0;
	temp <<= sl->rom[1] / 2;
	temp |= sl->rom[0] / 2;

	return sprintf(buf, "%d\n", temp * 1000);
}

static ssize_t w1_therm_read_bin(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = container_of(container_of(kobj, struct device, kobj),
			      			struct w1_slave, dev);
	struct w1_master *dev = sl->master;
	u8 rom[9], crc, verdict;
	size_t icount;
	int i;
	u16 temp;

	atomic_inc(&sl->refcnt);
	if (down_interruptible(&sl->master->mutex)) {
		count = 0;
		goto out_dec;
	}

	if (off > W1_SLAVE_DATA_SIZE) {
		count = 0;
		goto out;
	}
	if (off + count > W1_SLAVE_DATA_SIZE)
		count = W1_SLAVE_DATA_SIZE - off;

	icount = count;

	memset(buf, 0, count);
	memset(rom, 0, sizeof(rom));

	count = 0;
	verdict = 0;
	crc = 0;
	if (!w1_reset_bus(dev)) {
		u64 id = *(u64 *) & sl->reg_num;
		int count = 0;

		w1_write_8(dev, W1_MATCH_ROM);
		for (i = 0; i < 8; ++i)
			w1_write_8(dev, (id >> i * 8) & 0xff);

		w1_write_8(dev, W1_CONVERT_TEMP);

		while (dev->bus_master->read_bit(dev->bus_master->data) == 0
		       && count < 10) {
			w1_delay(1);
			count++;
		}

		if (count < 10) {
			if (!w1_reset_bus(dev)) {
				w1_write_8(dev, W1_MATCH_ROM);
				for (i = 0; i < 8; ++i)
					w1_write_8(dev,
						   (id >> i * 8) & 0xff);

				w1_write_8(dev, W1_READ_SCRATCHPAD);
				for (i = 0; i < 9; ++i)
					rom[i] = w1_read_8(dev);

				crc = w1_calc_crc8(rom, 8);

				if (rom[8] == crc && rom[0])
					verdict = 1;
			}
		}
		else
			dev_warn(&dev->dev,
				  "18S20 doesn't respond to CONVERT_TEMP.\n");
	}

	for (i = 0; i < 9; ++i)
		count += snprintf(buf + count, icount - count, "%02x ", rom[i]);
	count += snprintf(buf + count, icount - count, ": crc=%02x %s\n",
			   crc, (verdict) ? "YES" : "NO");
	if (verdict)
		memcpy(sl->rom, rom, sizeof(sl->rom));
	for (i = 0; i < 9; ++i)
		count += snprintf(buf + count, icount - count, "%02x ", sl->rom[i]);
	temp = 0;
	temp <<= sl->rom[1] / 2;
	temp |= sl->rom[0] / 2;
	count += snprintf(buf + count, icount - count, "t=%u\n", temp);
out:
	up(&dev->mutex);
out_dec:
	atomic_dec(&sl->refcnt);

	return count;
}

static struct w1_family w1_therm_family = {
	.fid = W1_FAMILY_THERM,
	.fops = &w1_therm_fops,
};

static int __init w1_therm_init(void)
{
	return w1_register_family(&w1_therm_family);
}

static void __exit w1_therm_fini(void)
{
	w1_unregister_family(&w1_therm_family);
}

module_init(w1_therm_init);
module_exit(w1_therm_fini);
