/*
 * lm80.c - From lm_sensors, Linux kernel modules for hardware
 * monitoring
 * Copyright (C) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
 * and Philip Edelbrock <phil@netroedge.com>
 *
 * Ported to Linux 2.6 by Tiago Sousa <mirage@kaotik.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#ifdef CONFIG_I2C_DEBUG_CHIP
#define DEBUG	1
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { 0x28, 0x2f, I2C_CLIENT_END };
static unsigned int normal_isa[] = { I2C_CLIENT_ISA_END };
static unsigned int normal_isa_range[] = { I2C_CLIENT_ISA_END };

/* Insmod parameters */
SENSORS_INSMOD_1(lm80);

/* Many LM80 constants specified below */

/* The LM80 registers */
#define LM80_REG_IN_MAX(nr)		(0x2a + (nr) * 2)
#define LM80_REG_IN_MIN(nr)		(0x2b + (nr) * 2)
#define LM80_REG_IN(nr)			(0x20 + (nr))

#define LM80_REG_FAN1_MIN		0x3c
#define LM80_REG_FAN2_MIN		0x3d
#define LM80_REG_FAN1			0x28
#define LM80_REG_FAN2			0x29

#define LM80_REG_TEMP			0x27
#define LM80_REG_TEMP_HOT_MAX		0x38
#define LM80_REG_TEMP_HOT_HYST		0x39
#define LM80_REG_TEMP_OS_MAX		0x3a
#define LM80_REG_TEMP_OS_HYST		0x3b

#define LM80_REG_CONFIG			0x00
#define LM80_REG_ALARM1			0x01
#define LM80_REG_ALARM2			0x02
#define LM80_REG_MASK1			0x03
#define LM80_REG_MASK2			0x04
#define LM80_REG_FANDIV			0x05
#define LM80_REG_RES			0x06


/* Conversions. Rounding and limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */

#define IN_TO_REG(val)		(SENSORS_LIMIT((val)/10,0,255))
#define IN_FROM_REG(val)	((val)*10)

static inline unsigned char FAN_TO_REG(unsigned rpm, unsigned div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm*div / 2) / (rpm*div), 1, 254);
}

#define FAN_FROM_REG(val,div)	((val)==0?-1:\
				(val)==255?0:1350000/((div)*(val)))

static inline long TEMP_FROM_REG(u16 temp)
{
	long res;

	temp >>= 4;
	if (temp < 0x0800)
		res = 625 * (long) temp;
	else
		res = ((long) temp - 0x01000) * 625;

	return res / 10;
}

#define TEMP_LIMIT_FROM_REG(val)	(((val)>0x80?(val)-0x100:(val))*1000)

#define TEMP_LIMIT_TO_REG(val)		SENSORS_LIMIT((val)<0?\
					((val)-500)/1000:((val)+500)/1000,0,255)

#define ALARMS_FROM_REG(val)		(val)

#define DIV_FROM_REG(val)		(1 << (val))
#define DIV_TO_REG(val)			((val)==8?3:(val)==4?2:(val)==1?0:1)

/*
 * Client data (each client gets its own)
 */

struct lm80_data {
	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[7];		/* Register value */
	u8 in_max[7];		/* Register value */
	u8 in_min[7];		/* Register value */
	u8 fan[2];		/* Register value */
	u8 fan_min[2];		/* Register value */
	u8 fan_div[2];		/* Register encoding, shifted right */
	u16 temp;		/* Register values, shifted right */
	u8 temp_hot_max;	/* Register value */
	u8 temp_hot_hyst;	/* Register value */
	u8 temp_os_max;		/* Register value */
	u8 temp_os_hyst;	/* Register value */
	u16 alarms;		/* Register encoding, combined */
};

/* 
 * Functions declaration
 */

static int lm80_attach_adapter(struct i2c_adapter *adapter);
static int lm80_detect(struct i2c_adapter *adapter, int address, int kind);
static void lm80_init_client(struct i2c_client *client);
static int lm80_detach_client(struct i2c_client *client);
static void lm80_update_client(struct i2c_client *client);
static int lm80_read_value(struct i2c_client *client, u8 reg);
static int lm80_write_value(struct i2c_client *client, u8 reg, u8 value);

/*
 * Internal variables
 */

static int lm80_id = 0;

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver lm80_driver = {
	.owner		= THIS_MODULE,
	.name		= "lm80",
	.id		= I2C_DRIVERID_LM80,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= lm80_attach_adapter,
	.detach_client	= lm80_detach_client,
};

/*
 * Sysfs stuff
 */

#define show_in(suffix, value) \
static ssize_t show_in_##suffix(struct device *dev, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	lm80_update_client(client); \
	return sprintf(buf, "%d\n", IN_FROM_REG(data->value)); \
}
show_in(min0, in_min[0]);
show_in(min1, in_min[1]);
show_in(min2, in_min[2]);
show_in(min3, in_min[3]);
show_in(min4, in_min[4]);
show_in(min5, in_min[5]);
show_in(min6, in_min[6]);
show_in(max0, in_max[0]);
show_in(max1, in_max[1]);
show_in(max2, in_max[2]);
show_in(max3, in_max[3]);
show_in(max4, in_max[4]);
show_in(max5, in_max[5]);
show_in(max6, in_max[6]);
show_in(input0, in[0]);
show_in(input1, in[1]);
show_in(input2, in[2]);
show_in(input3, in[3]);
show_in(input4, in[4]);
show_in(input5, in[5]);
show_in(input6, in[6]);

#define set_in(suffix, value, reg) \
static ssize_t set_in_##suffix(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
	data->value = IN_TO_REG(val); \
	lm80_write_value(client, reg, data->value); \
	return count; \
}
set_in(min0, in_min[0], LM80_REG_IN_MIN(0));
set_in(min1, in_min[1], LM80_REG_IN_MIN(1));
set_in(min2, in_min[2], LM80_REG_IN_MIN(2));
set_in(min3, in_min[3], LM80_REG_IN_MIN(3));
set_in(min4, in_min[4], LM80_REG_IN_MIN(4));
set_in(min5, in_min[5], LM80_REG_IN_MIN(5));
set_in(min6, in_min[6], LM80_REG_IN_MIN(6));
set_in(max0, in_max[0], LM80_REG_IN_MAX(0));
set_in(max1, in_max[1], LM80_REG_IN_MAX(1));
set_in(max2, in_max[2], LM80_REG_IN_MAX(2));
set_in(max3, in_max[3], LM80_REG_IN_MAX(3));
set_in(max4, in_max[4], LM80_REG_IN_MAX(4));
set_in(max5, in_max[5], LM80_REG_IN_MAX(5));
set_in(max6, in_max[6], LM80_REG_IN_MAX(6));

#define show_fan(suffix, value, div) \
static ssize_t show_fan_##suffix(struct device *dev, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	lm80_update_client(client); \
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->value, \
		       DIV_FROM_REG(data->div))); \
}
show_fan(min1, fan_min[0], fan_div[0]);
show_fan(min2, fan_min[1], fan_div[1]);
show_fan(input1, fan[0], fan_div[0]);
show_fan(input2, fan[1], fan_div[1]);

#define show_fan_div(suffix, value) \
static ssize_t show_fan_div##suffix(struct device *dev, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	lm80_update_client(client); \
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->value)); \
}
show_fan_div(1, fan_div[0]);
show_fan_div(2, fan_div[1]);

#define set_fan(suffix, value, reg, div) \
static ssize_t set_fan_##suffix(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	long val = simple_strtoul(buf, NULL, 10); \
	data->value = FAN_TO_REG(val, DIV_FROM_REG(data->div)); \
	lm80_write_value(client, reg, data->value); \
	return count; \
}
set_fan(min1, fan_min[0], LM80_REG_FAN1_MIN, fan_div[0]);
set_fan(min2, fan_min[1], LM80_REG_FAN2_MIN, fan_div[1]);

static ssize_t show_temp_input1(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm80_data *data = i2c_get_clientdata(client);
	lm80_update_client(client);
	return sprintf(buf, "%ld\n", TEMP_FROM_REG(data->temp));
}

#define show_temp(suffix, value) \
static ssize_t show_temp_##suffix(struct device *dev, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	lm80_update_client(client); \
	return sprintf(buf, "%d\n", TEMP_LIMIT_FROM_REG(data->value)); \
}
show_temp(hot_max, temp_hot_max);
show_temp(hot_hyst, temp_hot_hyst);
show_temp(os_max, temp_os_max);
show_temp(os_hyst, temp_os_hyst);

#define set_temp(suffix, value, reg) \
static ssize_t set_temp_##suffix(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	long val = simple_strtoul(buf, NULL, 10); \
	data->value = TEMP_LIMIT_TO_REG(val); \
	lm80_write_value(client, reg, data->value); \
	return count; \
}
set_temp(hot_max, temp_hot_max, LM80_REG_TEMP_HOT_MAX);
set_temp(hot_hyst, temp_hot_hyst, LM80_REG_TEMP_HOT_HYST);
set_temp(os_max, temp_os_max, LM80_REG_TEMP_OS_MAX);
set_temp(os_hyst, temp_os_hyst, LM80_REG_TEMP_OS_HYST);

static ssize_t show_alarms(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm80_data *data = i2c_get_clientdata(client);
	lm80_update_client(client);
	return sprintf(buf, "%d\n", ALARMS_FROM_REG(data->alarms));
}

static DEVICE_ATTR(in_min0, S_IWUSR | S_IRUGO, show_in_min0, set_in_min0);
static DEVICE_ATTR(in_min1, S_IWUSR | S_IRUGO, show_in_min1, set_in_min1);
static DEVICE_ATTR(in_min2, S_IWUSR | S_IRUGO, show_in_min2, set_in_min2);
static DEVICE_ATTR(in_min3, S_IWUSR | S_IRUGO, show_in_min3, set_in_min3);
static DEVICE_ATTR(in_min4, S_IWUSR | S_IRUGO, show_in_min4, set_in_min4);
static DEVICE_ATTR(in_min5, S_IWUSR | S_IRUGO, show_in_min5, set_in_min5);
static DEVICE_ATTR(in_min6, S_IWUSR | S_IRUGO, show_in_min6, set_in_min6);
static DEVICE_ATTR(in_max0, S_IWUSR | S_IRUGO, show_in_max0, set_in_max0);
static DEVICE_ATTR(in_max1, S_IWUSR | S_IRUGO, show_in_max1, set_in_max1);
static DEVICE_ATTR(in_max2, S_IWUSR | S_IRUGO, show_in_max2, set_in_max2);
static DEVICE_ATTR(in_max3, S_IWUSR | S_IRUGO, show_in_max3, set_in_max3);
static DEVICE_ATTR(in_max4, S_IWUSR | S_IRUGO, show_in_max4, set_in_max4);
static DEVICE_ATTR(in_max5, S_IWUSR | S_IRUGO, show_in_max5, set_in_max5);
static DEVICE_ATTR(in_max6, S_IWUSR | S_IRUGO, show_in_max6, set_in_max6);
static DEVICE_ATTR(in_input0, S_IRUGO, show_in_input0, NULL);
static DEVICE_ATTR(in_input1, S_IRUGO, show_in_input1, NULL);
static DEVICE_ATTR(in_input2, S_IRUGO, show_in_input2, NULL);
static DEVICE_ATTR(in_input3, S_IRUGO, show_in_input3, NULL);
static DEVICE_ATTR(in_input4, S_IRUGO, show_in_input4, NULL);
static DEVICE_ATTR(in_input5, S_IRUGO, show_in_input5, NULL);
static DEVICE_ATTR(in_input6, S_IRUGO, show_in_input6, NULL);
static DEVICE_ATTR(fan_min1, S_IWUSR | S_IRUGO, show_fan_min1,
    set_fan_min1);
static DEVICE_ATTR(fan_min2, S_IWUSR | S_IRUGO, show_fan_min2,
    set_fan_min2);
static DEVICE_ATTR(fan_input1, S_IRUGO, show_fan_input1, NULL);
static DEVICE_ATTR(fan_input2, S_IRUGO, show_fan_input2, NULL);
static DEVICE_ATTR(fan_div1, S_IRUGO, show_fan_div1, NULL);
static DEVICE_ATTR(fan_div2, S_IRUGO, show_fan_div2, NULL);
static DEVICE_ATTR(temp_input1, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp_max1, S_IWUSR | S_IRUGO, show_temp_hot_max,
    set_temp_hot_max);
static DEVICE_ATTR(temp_max1_hyst, S_IWUSR | S_IRUGO, show_temp_hot_hyst,
    set_temp_hot_hyst);
static DEVICE_ATTR(temp_crit1, S_IWUSR | S_IRUGO, show_temp_os_max,
    set_temp_os_max);
static DEVICE_ATTR(temp_crit1_hyst, S_IWUSR | S_IRUGO, show_temp_os_hyst,
    set_temp_os_hyst);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

/*
 * Real code
 */

static int lm80_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_ADAP_CLASS_SMBUS))
		return 0;
	return i2c_detect(adapter, &addr_data, lm80_detect);
}

int lm80_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i, cur;
	struct i2c_client *new_client;
	struct lm80_data *data;
	int err = 0;
	const char *name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access lm80_{read,write}_value. */
	if (!(new_client = kmalloc(sizeof(struct i2c_client) +
	    sizeof(struct lm80_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	memset(new_client, 0x00, sizeof(struct i2c_client) +
	       sizeof(struct lm80_data));

	data = (struct lm80_data *) (new_client + 1);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &lm80_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. It is lousy. */
	if (lm80_read_value(new_client, LM80_REG_ALARM2) & 0xc0)
		goto error_free;
	for (i = 0x2a; i <= 0x3d; i++) {
		cur = i2c_smbus_read_byte_data(new_client, i);
		if ((i2c_smbus_read_byte_data(new_client, i + 0x40) != cur)
		 || (i2c_smbus_read_byte_data(new_client, i + 0x80) != cur)
		 || (i2c_smbus_read_byte_data(new_client, i + 0xc0) != cur))
		    goto error_free;
	}

	/* Determine the chip type - only one kind supported! */
	kind = lm80;
	name = "lm80";

	/* Fill in the remaining client fields and put it into the global list */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);

	new_client->id = lm80_id++;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto error_free;

	/* Initialize the LM80 chip */
	lm80_init_client(new_client);

	/* Register sysfs hooks */
	device_create_file(&new_client->dev, &dev_attr_in_min0);
	device_create_file(&new_client->dev, &dev_attr_in_min1);
	device_create_file(&new_client->dev, &dev_attr_in_min2);
	device_create_file(&new_client->dev, &dev_attr_in_min3);
	device_create_file(&new_client->dev, &dev_attr_in_min4);
	device_create_file(&new_client->dev, &dev_attr_in_min5);
	device_create_file(&new_client->dev, &dev_attr_in_min6);
	device_create_file(&new_client->dev, &dev_attr_in_max0);
	device_create_file(&new_client->dev, &dev_attr_in_max1);
	device_create_file(&new_client->dev, &dev_attr_in_max2);
	device_create_file(&new_client->dev, &dev_attr_in_max3);
	device_create_file(&new_client->dev, &dev_attr_in_max4);
	device_create_file(&new_client->dev, &dev_attr_in_max5);
	device_create_file(&new_client->dev, &dev_attr_in_max6);
	device_create_file(&new_client->dev, &dev_attr_in_input0);
	device_create_file(&new_client->dev, &dev_attr_in_input1);
	device_create_file(&new_client->dev, &dev_attr_in_input2);
	device_create_file(&new_client->dev, &dev_attr_in_input3);
	device_create_file(&new_client->dev, &dev_attr_in_input4);
	device_create_file(&new_client->dev, &dev_attr_in_input5);
	device_create_file(&new_client->dev, &dev_attr_in_input6);
	device_create_file(&new_client->dev, &dev_attr_fan_min1);
	device_create_file(&new_client->dev, &dev_attr_fan_min2);
	device_create_file(&new_client->dev, &dev_attr_fan_input1);
	device_create_file(&new_client->dev, &dev_attr_fan_input2);
	device_create_file(&new_client->dev, &dev_attr_fan_div1);
	device_create_file(&new_client->dev, &dev_attr_fan_div2);
	device_create_file(&new_client->dev, &dev_attr_temp_input1);
	device_create_file(&new_client->dev, &dev_attr_temp_max1);
	device_create_file(&new_client->dev, &dev_attr_temp_max1_hyst);
	device_create_file(&new_client->dev, &dev_attr_temp_crit1);
	device_create_file(&new_client->dev, &dev_attr_temp_crit1_hyst);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

error_free:
	kfree(new_client);
exit:
	return err;
}

static int lm80_detach_client(struct i2c_client *client)
{
	int err;

	if ((err = i2c_detach_client(client))) {
		dev_err(&client->dev, "Client deregistration failed, "
			"client not detached.\n");
		return err;
	}

	kfree(client);
	return 0;
}

static int lm80_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int lm80_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* Called when we have found a new LM80. */
static void lm80_init_client(struct i2c_client *client)
{
	/* Reset all except Watchdog values and last conversion values
	   This sets fan-divs to 2, among others. This makes most other
	   initializations unnecessary */
	lm80_write_value(client, LM80_REG_CONFIG, 0x80);
	/* Set 11-bit temperature resolution */
	lm80_write_value(client, LM80_REG_RES, 0x08);

	/* Start monitoring */
	lm80_write_value(client, LM80_REG_CONFIG, 0x01);
}

static void lm80_update_client(struct i2c_client *client)
{
	struct lm80_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if ((jiffies - data->last_updated > 2 * HZ) ||
	    (jiffies < data->last_updated) || !data->valid) {

		dev_dbg(&client->dev, "Starting lm80 update\n");
		for (i = 0; i <= 6; i++) {
			data->in[i] =
			    lm80_read_value(client, LM80_REG_IN(i));
			data->in_min[i] =
			    lm80_read_value(client, LM80_REG_IN_MIN(i));
			data->in_max[i] =
			    lm80_read_value(client, LM80_REG_IN_MAX(i));
		}
		data->fan[0] = lm80_read_value(client, LM80_REG_FAN1);
		data->fan_min[0] =
		    lm80_read_value(client, LM80_REG_FAN1_MIN);
		data->fan[1] = lm80_read_value(client, LM80_REG_FAN2);
		data->fan_min[1] =
		    lm80_read_value(client, LM80_REG_FAN2_MIN);

		data->temp =
		    (lm80_read_value(client, LM80_REG_TEMP) << 8) |
		    (lm80_read_value(client, LM80_REG_RES) & 0xf0);
		data->temp_os_max =
		    lm80_read_value(client, LM80_REG_TEMP_OS_MAX);
		data->temp_os_hyst =
		    lm80_read_value(client, LM80_REG_TEMP_OS_HYST);
		data->temp_hot_max =
		    lm80_read_value(client, LM80_REG_TEMP_HOT_MAX);
		data->temp_hot_hyst =
		    lm80_read_value(client, LM80_REG_TEMP_HOT_HYST);

		i = lm80_read_value(client, LM80_REG_FANDIV);
		data->fan_div[0] = (i >> 2) & 0x03;
		data->fan_div[1] = (i >> 4) & 0x03;
		data->alarms = lm80_read_value(client, LM80_REG_ALARM1) +
		    (lm80_read_value(client, LM80_REG_ALARM2) << 8);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);
}

static int __init sensors_lm80_init(void)
{
	return i2c_add_driver(&lm80_driver);
}

static void __exit sensors_lm80_exit(void)
{
	i2c_del_driver(&lm80_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
	"Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("LM80 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm80_init);
module_exit(sensors_lm80_exit);
