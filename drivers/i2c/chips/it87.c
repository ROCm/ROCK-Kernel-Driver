/*
    it87.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring.

    Supports: IT8705F  Super I/O chip w/LPC interface
              IT8712F  Super I/O chip w/LPC interface & SMbus
              Sis950   A clone of the IT8705F

    Copyright (c) 2001 Chris Gauthron <chrisg@0-in.com> 
    Largely inspired by lm78.c of the same package

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    djg@pdp8.net David Gesswein 7/18/01
    Modified to fix bug with not all alarms enabled.
    Added ability to read battery voltage and select temperature sensor
    type at module load time.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>
#include <asm/io.h>


/* Addresses to scan */
static unsigned short normal_i2c[] = { I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { 0x20, 0x2f, I2C_CLIENT_END };
static unsigned int normal_isa[] = { 0x0290, I2C_CLIENT_ISA_END };
static unsigned int normal_isa_range[] = { I2C_CLIENT_ISA_END };

/* Insmod parameters */
SENSORS_INSMOD_4(it87, it8705, it8712, sis950);


/* Update battery voltage after every reading if true */
static int update_vbat = 0;


/* Enable Temp1 as thermal resistor */
/* Enable Temp2 as thermal diode */
/* Enable Temp3 as thermal resistor */
static int temp_type = 0x2a;

/* Many IT87 constants specified below */

/* Length of ISA address segment */
#define IT87_EXTENT 8

/* Where are the ISA address/data registers relative to the base address */
#define IT87_ADDR_REG_OFFSET 5
#define IT87_DATA_REG_OFFSET 6

/*----- The IT87 registers -----*/

#define IT87_REG_CONFIG        0x00

#define IT87_REG_ALARM1        0x01
#define IT87_REG_ALARM2        0x02
#define IT87_REG_ALARM3        0x03

#define IT87_REG_VID           0x0a
#define IT87_REG_FAN_DIV       0x0b

/* Monitors: 9 voltage (0 to 7, battery), 3 temp (1 to 3), 3 fan (1 to 3) */

#define IT87_REG_FAN(nr)       (0x0d + (nr))
#define IT87_REG_FAN_MIN(nr)   (0x10 + (nr))
#define IT87_REG_FAN_CTRL      0x13

#define IT87_REG_VIN(nr)       (0x20 + (nr))
#define IT87_REG_TEMP(nr)      (0x29 + (nr))

#define IT87_REG_VIN_MAX(nr)   (0x30 + (nr) * 2)
#define IT87_REG_VIN_MIN(nr)   (0x31 + (nr) * 2)
#define IT87_REG_TEMP_HIGH(nr) (0x40 + ((nr) * 2))
#define IT87_REG_TEMP_LOW(nr)  (0x41 + ((nr) * 2))

#define IT87_REG_I2C_ADDR      0x48

#define IT87_REG_VIN_ENABLE    0x50
#define IT87_REG_TEMP_ENABLE   0x51

#define IT87_REG_CHIPID        0x58

#define IN_TO_REG(val)  (SENSORS_LIMIT((((val) * 10 + 8)/16),0,255))
#define IN_FROM_REG(val) (((val) *  16) / 10)

static inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1,
			     254);
}

#define FAN_FROM_REG(val,div) ((val)==0?-1:(val)==255?0:1350000/((val)*(div)))

#define TEMP_TO_REG(val) (SENSORS_LIMIT(((val)<0?(((val)-5)/10):\
					((val)+5)/10),0,255))
#define TEMP_FROM_REG(val) (((val)>0x80?(val)-0x100:(val))*10)

#define VID_FROM_REG(val) ((val)==0x1f?0:(val)>=0x10?510-(val)*10:\
				205-(val)*5)
#define ALARMS_FROM_REG(val) (val)

static int log2(int val)
{
	int answer = 0;
	while ((val >>= 1))
		answer++;
	return answer;
}
#define DIV_TO_REG(val) log2(val)
#define DIV_FROM_REG(val) (1 << (val))

/* Initial limits. Use the config file to set better limits. */
#define IT87_INIT_IN_0 170
#define IT87_INIT_IN_1 250
#define IT87_INIT_IN_2 (330 / 2)
#define IT87_INIT_IN_3 (((500)   * 100)/168)
#define IT87_INIT_IN_4 (((1200)  * 10)/38)
#define IT87_INIT_IN_5 (((1200)  * 10)/72)
#define IT87_INIT_IN_6 (((500)   * 10)/56)
#define IT87_INIT_IN_7 (((500)   * 100)/168)

#define IT87_INIT_IN_PERCENTAGE 10

#define IT87_INIT_IN_MIN_0 \
	(IT87_INIT_IN_0 - IT87_INIT_IN_0 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_0 \
	(IT87_INIT_IN_0 + IT87_INIT_IN_0 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_1 \
	(IT87_INIT_IN_1 - IT87_INIT_IN_1 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_1 \
	(IT87_INIT_IN_1 + IT87_INIT_IN_1 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_2 \
	(IT87_INIT_IN_2 - IT87_INIT_IN_2 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_2 \
	(IT87_INIT_IN_2 + IT87_INIT_IN_2 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_3 \
	(IT87_INIT_IN_3 - IT87_INIT_IN_3 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_3 \
	(IT87_INIT_IN_3 + IT87_INIT_IN_3 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_4 \
	(IT87_INIT_IN_4 - IT87_INIT_IN_4 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_4 \
	(IT87_INIT_IN_4 + IT87_INIT_IN_4 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_5 \
	(IT87_INIT_IN_5 - IT87_INIT_IN_5 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_5 \
	(IT87_INIT_IN_5 + IT87_INIT_IN_5 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_6 \
	(IT87_INIT_IN_6 - IT87_INIT_IN_6 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_6 \
	(IT87_INIT_IN_6 + IT87_INIT_IN_6 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MIN_7 \
	(IT87_INIT_IN_7 - IT87_INIT_IN_7 * IT87_INIT_IN_PERCENTAGE / 100)
#define IT87_INIT_IN_MAX_7 \
	(IT87_INIT_IN_7 + IT87_INIT_IN_7 * IT87_INIT_IN_PERCENTAGE / 100)

#define IT87_INIT_FAN_MIN_1 3000
#define IT87_INIT_FAN_MIN_2 3000
#define IT87_INIT_FAN_MIN_3 3000

#define IT87_INIT_TEMP_HIGH_1 600
#define IT87_INIT_TEMP_LOW_1  200
#define IT87_INIT_TEMP_HIGH_2 600
#define IT87_INIT_TEMP_LOW_2  200
#define IT87_INIT_TEMP_HIGH_3 600 
#define IT87_INIT_TEMP_LOW_3  200

/* For each registered IT87, we need to keep some data in memory. That
   data is pointed to by it87_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new it87 client is
   allocated. */
struct it87_data {
	struct semaphore lock;
	enum chips type;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[9];		/* Register value */
	u8 in_max[9];		/* Register value */
	u8 in_min[9];		/* Register value */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u8 temp[3];		/* Register value */
	u8 temp_high[3];	/* Register value */
	u8 temp_low[3];		/* Register value */
	u8 sensor;		/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u32 alarms;		/* Register encoding, combined */
};


static int it87_attach_adapter(struct i2c_adapter *adapter);
static int it87_detect(struct i2c_adapter *adapter, int address, int kind);
static int it87_detach_client(struct i2c_client *client);

static int it87_read_value(struct i2c_client *client, u8 register);
static int it87_write_value(struct i2c_client *client, u8 register,
			u8 value);
static void it87_update_client(struct i2c_client *client);
static void it87_init_client(struct i2c_client *client, struct it87_data *data);


static struct i2c_driver it87_driver = {
	.owner		= THIS_MODULE,
	.name		= "IT87xx",
	.id		= I2C_DRIVERID_IT87,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= it87_attach_adapter,
	.detach_client	= it87_detach_client,
};

static int it87_id = 0;

static ssize_t show_in(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[nr])*10 );
}

static ssize_t show_in_min(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_min[nr])*10 );
}

static ssize_t show_in_max(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_max[nr])*10 );
}

static ssize_t set_in_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10)/10;
	data->in_min[nr] = IN_TO_REG(val);
	it87_write_value(client, IT87_REG_VIN_MIN(nr), 
			data->in_min[nr]);
	return count;
}
static ssize_t set_in_max(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10)/10;
	data->in_max[nr] = IN_TO_REG(val);
	it87_write_value(client, IT87_REG_VIN_MAX(nr), 
			data->in_max[nr]);
	return count;
}

#define show_in_offset(offset)					\
static ssize_t							\
	show_in##offset (struct device *dev, char *buf)		\
{								\
	return show_in(dev, buf, 0x##offset);			\
}								\
static DEVICE_ATTR(in_input##offset, S_IRUGO, show_in##offset, NULL)

#define limit_in_offset(offset)					\
static ssize_t							\
	show_in##offset##_min (struct device *dev, char *buf)	\
{								\
	return show_in_min(dev, buf, 0x##offset);		\
}								\
static ssize_t							\
	show_in##offset##_max (struct device *dev, char *buf)	\
{								\
	return show_in_max(dev, buf, 0x##offset);		\
}								\
static ssize_t set_in##offset##_min (struct device *dev, 	\
		const char *buf, size_t count) 			\
{								\
	return set_in_min(dev, buf, count, 0x##offset);		\
}								\
static ssize_t set_in##offset##_max (struct device *dev,	\
			const char *buf, size_t count)		\
{								\
	return set_in_max(dev, buf, count, 0x##offset);		\
}								\
static DEVICE_ATTR(in_min##offset, S_IRUGO | S_IWUSR, 		\
		show_in##offset##_min, set_in##offset##_min)	\
static DEVICE_ATTR(in_max##offset, S_IRUGO | S_IWUSR, 		\
		show_in##offset##_max, set_in##offset##_max)

show_in_offset(0);
limit_in_offset(0);
show_in_offset(1);
limit_in_offset(1);
show_in_offset(2);
limit_in_offset(2);
show_in_offset(3);
limit_in_offset(3);
show_in_offset(4);
limit_in_offset(4);
show_in_offset(5);
limit_in_offset(5);
show_in_offset(6);
limit_in_offset(6);
show_in_offset(7);
limit_in_offset(7);
show_in_offset(8);

/* 3 temperatures */
static ssize_t show_temp(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr])*100 );
}
/* more like overshoot temperature */
static ssize_t show_temp_max(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_high[nr])*100);
}
/* more like hysteresis temperature */
static ssize_t show_temp_min(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_low[nr])*100);
}
static ssize_t set_temp_max(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10)/100;
	data->temp_high[nr] = TEMP_TO_REG(val);
	it87_write_value(client, IT87_REG_TEMP_HIGH(nr), data->temp_high[nr]);
	return count;
}
static ssize_t set_temp_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10)/100;
	data->temp_low[nr] = TEMP_TO_REG(val);
	it87_write_value(client, IT87_REG_TEMP_LOW(nr), data->temp_low[nr]);
	return count;
}
#define show_temp_offset(offset)					\
static ssize_t show_temp_##offset (struct device *dev, char *buf)	\
{									\
	return show_temp(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t								\
show_temp_##offset##_max (struct device *dev, char *buf)		\
{									\
	return show_temp_max(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t								\
show_temp_##offset##_min (struct device *dev, char *buf)		\
{									\
	return show_temp_min(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t set_temp_##offset##_max (struct device *dev, 		\
		const char *buf, size_t count) 				\
{									\
	return set_temp_max(dev, buf, count, 0x##offset - 1);		\
}									\
static ssize_t set_temp_##offset##_min (struct device *dev, 		\
		const char *buf, size_t count) 				\
{									\
	return set_temp_min(dev, buf, count, 0x##offset - 1);		\
}									\
static DEVICE_ATTR(temp_input##offset, S_IRUGO, show_temp_##offset, NULL) \
static DEVICE_ATTR(temp_max##offset, S_IRUGO | S_IWUSR, 		\
		show_temp_##offset##_max, set_temp_##offset##_max) 	\
static DEVICE_ATTR(temp_min##offset, S_IRUGO | S_IWUSR, 		\
		show_temp_##offset##_min, set_temp_##offset##_min)	

show_temp_offset(1);
show_temp_offset(2);
show_temp_offset(3);

/* more like overshoot temperature */
static ssize_t show_sensor(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	if (data->sensor & (1 << nr))
	    return sprintf(buf, "1\n");
	if (data->sensor & (8 << nr))
	    return sprintf(buf, "2\n");
	return sprintf(buf, "0\n");
}
static ssize_t set_sensor(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	data->sensor &= ~(1 << nr);
	data->sensor &= ~(8 << nr);
	if (val == 1)
	    data->sensor |= 1 << nr;
	else if (val == 2)
	    data->sensor |= 8 << nr;
	it87_write_value(client, IT87_REG_TEMP_ENABLE, data->sensor);
	return count;
}
#define show_sensor_offset(offset)					\
static ssize_t show_sensor_##offset (struct device *dev, char *buf)	\
{									\
	return show_sensor(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t set_sensor_##offset (struct device *dev, 		\
		const char *buf, size_t count) 				\
{									\
	return set_sensor(dev, buf, count, 0x##offset - 1);		\
}									\
static DEVICE_ATTR(sensor##offset, S_IRUGO | S_IWUSR,	 		\
		show_sensor_##offset, set_sensor_##offset)

show_sensor_offset(1);
show_sensor_offset(2);
show_sensor_offset(3);

/* 3 Fans */
static ssize_t show_fan(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan[nr], 
				DIV_FROM_REG(data->fan_div[nr])) );
}
static ssize_t show_fan_min(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf,"%d\n",
		FAN_FROM_REG(data->fan_min[nr], DIV_FROM_REG(data->fan_div[nr])) );
}
static ssize_t show_fan_div(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf,"%d\n", DIV_FROM_REG(data->fan_div[nr]) );
}
static ssize_t set_fan_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	it87_write_value(client, IT87_REG_FAN_MIN(nr), data->fan_min[nr]);
	return count;
}
static ssize_t set_fan_div(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);
	int i, min[3];
	u8 old = it87_read_value(client, IT87_REG_FAN_DIV);

	for (i = 0; i < 3; i++)
		min[i] = FAN_FROM_REG(data->fan_min[i], DIV_FROM_REG(data->fan_div[i]));

	switch (nr) {
	case 0:
	case 1:
		data->fan_div[nr] = DIV_TO_REG(val);
		break;
	case 2:
		if (val < 8)
			data->fan_div[nr] = 1;
		else
			data->fan_div[nr] = 3;
	}
	val = old & 0x100;
	val |= (data->fan_div[0] & 0x07);
	val |= (data->fan_div[1] & 0x07) << 3;
	if (data->fan_div[2] == 3)
		val |= 0x1 << 6;
	it87_write_value(client, IT87_REG_FAN_DIV, val);

	for (i = 0; i < 3; i++) {
		data->fan_min[i]=FAN_TO_REG(min[i], DIV_FROM_REG(data->fan_div[i]));
		it87_write_value(client, IT87_REG_FAN_MIN(i), data->fan_min[i]);
	}
	return count;
}

#define show_fan_offset(offset)						\
static ssize_t show_fan_##offset (struct device *dev, char *buf)	\
{									\
	return show_fan(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t show_fan_##offset##_min (struct device *dev, char *buf)	\
{									\
	return show_fan_min(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t show_fan_##offset##_div (struct device *dev, char *buf)	\
{									\
	return show_fan_div(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t set_fan_##offset##_min (struct device *dev, 		\
	const char *buf, size_t count) 					\
{									\
	return set_fan_min(dev, buf, count, 0x##offset - 1);		\
}									\
static ssize_t set_fan_##offset##_div (struct device *dev, 		\
		const char *buf, size_t count) 				\
{									\
	return set_fan_div(dev, buf, count, 0x##offset - 1);		\
}									\
static DEVICE_ATTR(fan_input##offset, S_IRUGO, show_fan_##offset, NULL) \
static DEVICE_ATTR(fan_min##offset, S_IRUGO | S_IWUSR, 			\
		show_fan_##offset##_min, set_fan_##offset##_min) 	\
static DEVICE_ATTR(fan_div##offset, S_IRUGO | S_IWUSR, 			\
		show_fan_##offset##_div, set_fan_##offset##_div)

show_fan_offset(1);
show_fan_offset(2);
show_fan_offset(3);

/* Alarm */
static ssize_t show_alarm(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	it87_update_client(client);
	return sprintf(buf,"%d\n", ALARMS_FROM_REG(data->alarms));
}
static DEVICE_ATTR(alarm, S_IRUGO | S_IWUSR, show_alarm, NULL);

/* This function is called when:
     * it87_driver is inserted (when this module is loaded), for each
       available adapter
     * when a new adapter is inserted (and it87_driver is still present) */
static int it87_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_ADAP_CLASS_SMBUS))
		return 0;
	return i2c_detect(adapter, &addr_data, it87_detect);
}

/* This function is called by i2c_detect */
int it87_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i;
	struct i2c_client *new_client = NULL;
	struct it87_data *data;
	int err = 0;
	const char *name = "";
	int is_isa = i2c_is_isa_adapter(adapter);

	if (!is_isa && 
	    !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto ERROR0;

	/* Reserve the ISA region */
	if (is_isa)
		if (!request_region(address, IT87_EXTENT, name))
			goto ERROR0;

	/* Probe whether there is anything available on this address. Already
	   done for SMBus clients */
	if (kind < 0) {
		if (is_isa) {

#define REALLY_SLOW_IO
			/* We need the timeouts for at least some IT87-like chips. But only
			   if we read 'undefined' registers. */
			i = inb_p(address + 1);
			if (inb_p(address + 2) != i)
				goto ERROR1;
			if (inb_p(address + 3) != i)
				goto ERROR1;
			if (inb_p(address + 7) != i)
				goto ERROR1;
#undef REALLY_SLOW_IO

			/* Let's just hope nothing breaks here */
			i = inb_p(address + 5) & 0x7f;
			outb_p(~i & 0x7f, address + 5);
			if ((inb_p(address + 5) & 0x7f) != (~i & 0x7f)) {
				outb_p(i, address + 5);
				return 0;
			}
		}
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access it87_{read,write}_value. */

	if (!(new_client = kmalloc((sizeof(struct i2c_client)) +
					sizeof(struct it87_data),
					GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}
	memset(new_client, 0x00, sizeof(struct i2c_client) +
				 sizeof(struct it87_data));

	data = (struct it87_data *) (new_client + 1);
	if (is_isa)
		init_MUTEX(&data->lock);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &it87_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	if (kind < 0) {
		if (it87_read_value(new_client, IT87_REG_CONFIG) & 0x80)
			goto ERROR1;
		if (!is_isa
			&& (it87_read_value(new_client, IT87_REG_I2C_ADDR) !=
			address)) goto ERROR1;
	}

	/* Determine the chip type. */
	if (kind <= 0) {
		i = it87_read_value(new_client, IT87_REG_CHIPID);
		if (i == 0x90) {
			kind = it87;
		}
		else {
			if (kind == 0)
				printk
				    ("it87.o: Ignoring 'force' parameter for unknown chip at "
				     "adapter %d, address 0x%02x\n",
				     i2c_adapter_id(adapter), address);
			goto ERROR1;
		}
	}

	if (kind == it87) {
		name = "it87";
	} /* else if (kind == it8712) {
		name = "it8712";
	} */ else {
		dev_dbg(&adapter->dev, "Internal error: unknown kind (%d)?!?",
			kind);
		goto ERROR1;
	}

	/* Fill in the remaining client fields and put it into the global list */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);

	data->type = kind;

	new_client->id = it87_id++;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR1;

	/* Initialize the IT87 chip */
	it87_init_client(new_client, data);

	/* Register sysfs hooks */
	device_create_file(&new_client->dev, &dev_attr_in_input0);
	device_create_file(&new_client->dev, &dev_attr_in_input1);
	device_create_file(&new_client->dev, &dev_attr_in_input2);
	device_create_file(&new_client->dev, &dev_attr_in_input3);
	device_create_file(&new_client->dev, &dev_attr_in_input4);
	device_create_file(&new_client->dev, &dev_attr_in_input5);
	device_create_file(&new_client->dev, &dev_attr_in_input6);
	device_create_file(&new_client->dev, &dev_attr_in_input7);
	device_create_file(&new_client->dev, &dev_attr_in_input8);
	device_create_file(&new_client->dev, &dev_attr_in_min0);
	device_create_file(&new_client->dev, &dev_attr_in_min1);
	device_create_file(&new_client->dev, &dev_attr_in_min2);
	device_create_file(&new_client->dev, &dev_attr_in_min3);
	device_create_file(&new_client->dev, &dev_attr_in_min4);
	device_create_file(&new_client->dev, &dev_attr_in_min5);
	device_create_file(&new_client->dev, &dev_attr_in_min6);
	device_create_file(&new_client->dev, &dev_attr_in_min7);
	device_create_file(&new_client->dev, &dev_attr_in_max0);
	device_create_file(&new_client->dev, &dev_attr_in_max1);
	device_create_file(&new_client->dev, &dev_attr_in_max2);
	device_create_file(&new_client->dev, &dev_attr_in_max3);
	device_create_file(&new_client->dev, &dev_attr_in_max4);
	device_create_file(&new_client->dev, &dev_attr_in_max5);
	device_create_file(&new_client->dev, &dev_attr_in_max6);
	device_create_file(&new_client->dev, &dev_attr_in_max7);
	device_create_file(&new_client->dev, &dev_attr_temp_input1);
	device_create_file(&new_client->dev, &dev_attr_temp_input2);
	device_create_file(&new_client->dev, &dev_attr_temp_input3);
	device_create_file(&new_client->dev, &dev_attr_temp_max1);
	device_create_file(&new_client->dev, &dev_attr_temp_max2);
	device_create_file(&new_client->dev, &dev_attr_temp_max3);
	device_create_file(&new_client->dev, &dev_attr_temp_min1);
	device_create_file(&new_client->dev, &dev_attr_temp_min2);
	device_create_file(&new_client->dev, &dev_attr_temp_min3);
	device_create_file(&new_client->dev, &dev_attr_sensor1);
	device_create_file(&new_client->dev, &dev_attr_sensor2);
	device_create_file(&new_client->dev, &dev_attr_sensor3);
	device_create_file(&new_client->dev, &dev_attr_fan_input1);
	device_create_file(&new_client->dev, &dev_attr_fan_input2);
	device_create_file(&new_client->dev, &dev_attr_fan_input3);
	device_create_file(&new_client->dev, &dev_attr_fan_min1);
	device_create_file(&new_client->dev, &dev_attr_fan_min2);
	device_create_file(&new_client->dev, &dev_attr_fan_min3);
	device_create_file(&new_client->dev, &dev_attr_fan_div1);
	device_create_file(&new_client->dev, &dev_attr_fan_div2);
	device_create_file(&new_client->dev, &dev_attr_fan_div3);
	device_create_file(&new_client->dev, &dev_attr_alarm);

	return 0;

ERROR1:
	kfree(new_client);

	if (is_isa)
		release_region(address, IT87_EXTENT);
ERROR0:
	return err;
}

static int it87_detach_client(struct i2c_client *client)
{
	int err;

	if ((err = i2c_detach_client(client))) {
		dev_err(&client->dev,
			"Client deregistration failed, client not detached.\n");
		return err;
	}

	if(i2c_is_isa_client(client))
		release_region(client->addr, IT87_EXTENT);
	kfree(client);

	return 0;
}

/* The SMBus locks itself, but ISA access must be locked explicitely! 
   We don't want to lock the whole ISA bus, so we lock each client
   separately.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. 
   There are some ugly typecasts here, but the good new is - they should
   nowhere else be necessary! */
static int it87_read_value(struct i2c_client *client, u8 reg)
{
	struct it87_data *data = i2c_get_clientdata(client);

	int res;
	if (i2c_is_isa_client(client)) {
		down(&data->lock);
		outb_p(reg, client->addr + IT87_ADDR_REG_OFFSET);
		res = inb_p(client->addr + IT87_DATA_REG_OFFSET);
		up(&data->lock);
		return res;
	} else
		return i2c_smbus_read_byte_data(client, reg);
}

/* The SMBus locks itself, but ISA access muse be locked explicitely! 
   We don't want to lock the whole ISA bus, so we lock each client
   separately.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. 
   There are some ugly typecasts here, but the good new is - they should
   nowhere else be necessary! */
static int it87_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	struct it87_data *data = i2c_get_clientdata(client);

	if (i2c_is_isa_client(client)) {
		down(&data->lock);
		outb_p(reg, client->addr + IT87_ADDR_REG_OFFSET);
		outb_p(value, client->addr + IT87_DATA_REG_OFFSET);
		up(&data->lock);
		return 0;
	} else
		return i2c_smbus_write_byte_data(client, reg, value);
}

/* Called when we have found a new IT87. It should set limits, etc. */
static void it87_init_client(struct i2c_client *client, struct it87_data *data)
{
	/* Reset all except Watchdog values and last conversion values
	   This sets fan-divs to 2, among others */
	it87_write_value(client, IT87_REG_CONFIG, 0x80);
	it87_write_value(client, IT87_REG_VIN_MIN(0),
			 IN_TO_REG(IT87_INIT_IN_MIN_0));
	it87_write_value(client, IT87_REG_VIN_MAX(0),
			 IN_TO_REG(IT87_INIT_IN_MAX_0));
	it87_write_value(client, IT87_REG_VIN_MIN(1),
			 IN_TO_REG(IT87_INIT_IN_MIN_1));
	it87_write_value(client, IT87_REG_VIN_MAX(1),
			 IN_TO_REG(IT87_INIT_IN_MAX_1));
	it87_write_value(client, IT87_REG_VIN_MIN(2),
			 IN_TO_REG(IT87_INIT_IN_MIN_2));
	it87_write_value(client, IT87_REG_VIN_MAX(2),
			 IN_TO_REG(IT87_INIT_IN_MAX_2));
	it87_write_value(client, IT87_REG_VIN_MIN(3),
			 IN_TO_REG(IT87_INIT_IN_MIN_3));
	it87_write_value(client, IT87_REG_VIN_MAX(3),
			 IN_TO_REG(IT87_INIT_IN_MAX_3));
	it87_write_value(client, IT87_REG_VIN_MIN(4),
			 IN_TO_REG(IT87_INIT_IN_MIN_4));
	it87_write_value(client, IT87_REG_VIN_MAX(4),
			 IN_TO_REG(IT87_INIT_IN_MAX_4));
	it87_write_value(client, IT87_REG_VIN_MIN(5),
			 IN_TO_REG(IT87_INIT_IN_MIN_5));
	it87_write_value(client, IT87_REG_VIN_MAX(5),
			 IN_TO_REG(IT87_INIT_IN_MAX_5));
	it87_write_value(client, IT87_REG_VIN_MIN(6),
			 IN_TO_REG(IT87_INIT_IN_MIN_6));
	it87_write_value(client, IT87_REG_VIN_MAX(6),
			 IN_TO_REG(IT87_INIT_IN_MAX_6));
	it87_write_value(client, IT87_REG_VIN_MIN(7),
			 IN_TO_REG(IT87_INIT_IN_MIN_7));
	it87_write_value(client, IT87_REG_VIN_MAX(7),
			 IN_TO_REG(IT87_INIT_IN_MAX_7));
	/* Note: Battery voltage does not have limit registers */
	it87_write_value(client, IT87_REG_FAN_MIN(0),
			 FAN_TO_REG(IT87_INIT_FAN_MIN_1, 2));
	it87_write_value(client, IT87_REG_FAN_MIN(1),
			 FAN_TO_REG(IT87_INIT_FAN_MIN_2, 2));
	it87_write_value(client, IT87_REG_FAN_MIN(2),
			 FAN_TO_REG(IT87_INIT_FAN_MIN_3, 2));
	it87_write_value(client, IT87_REG_TEMP_HIGH(0),
			 TEMP_TO_REG(IT87_INIT_TEMP_HIGH_1));
	it87_write_value(client, IT87_REG_TEMP_LOW(0),
			 TEMP_TO_REG(IT87_INIT_TEMP_LOW_1));
	it87_write_value(client, IT87_REG_TEMP_HIGH(1),
			 TEMP_TO_REG(IT87_INIT_TEMP_HIGH_2));
	it87_write_value(client, IT87_REG_TEMP_LOW(1),
			 TEMP_TO_REG(IT87_INIT_TEMP_LOW_2));
	it87_write_value(client, IT87_REG_TEMP_HIGH(2),
			 TEMP_TO_REG(IT87_INIT_TEMP_HIGH_3));
	it87_write_value(client, IT87_REG_TEMP_LOW(2),
			 TEMP_TO_REG(IT87_INIT_TEMP_LOW_3));

	/* Enable voltage monitors */
	it87_write_value(client, IT87_REG_VIN_ENABLE, 0xff);

	/* Enable Temp1-Temp3 */
	data->sensor = (it87_read_value(client, IT87_REG_TEMP_ENABLE) & 0xc0);
	data->sensor |= temp_type & 0x3f;
	it87_write_value(client, IT87_REG_TEMP_ENABLE, data->sensor);

	/* Enable fans */
	it87_write_value(client, IT87_REG_FAN_CTRL,
			(it87_read_value(client, IT87_REG_FAN_CTRL) & 0x8f)
			| 0x70);

	/* Start monitoring */
	it87_write_value(client, IT87_REG_CONFIG,
			 (it87_read_value(client, IT87_REG_CONFIG) & 0xb7)
			 | (update_vbat ? 0x41 : 0x01));
}

static void it87_update_client(struct i2c_client *client)
{
	struct it87_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if ((jiffies - data->last_updated > HZ + HZ / 2) ||
	    (jiffies < data->last_updated) || !data->valid) {

		if (update_vbat) {
			/* Cleared after each update, so reenable.  Value
		 	  returned by this read will be previous value */	
			it87_write_value(client, IT87_REG_CONFIG,
			   it87_read_value(client, IT87_REG_CONFIG) | 0x40);
		}
		for (i = 0; i <= 7; i++) {
			data->in[i] =
			    it87_read_value(client, IT87_REG_VIN(i));
			data->in_min[i] =
			    it87_read_value(client, IT87_REG_VIN_MIN(i));
			data->in_max[i] =
			    it87_read_value(client, IT87_REG_VIN_MAX(i));
		}
		data->in[8] =
		    it87_read_value(client, IT87_REG_VIN(8));
		/* Temperature sensor doesn't have limit registers, set
		   to min and max value */
		data->in_min[8] = 0;
		data->in_max[8] = 255;

		for (i = 0; i < 3; i++) {
			data->fan[i] =
			    it87_read_value(client, IT87_REG_FAN(i));
			data->fan_min[i] =
			    it87_read_value(client, IT87_REG_FAN_MIN(i));
		}
		for (i = 0; i < 3; i++) {
			data->temp[i] =
			    it87_read_value(client, IT87_REG_TEMP(i));
			data->temp_high[i] =
			    it87_read_value(client, IT87_REG_TEMP_HIGH(i));
			data->temp_low[i] =
			    it87_read_value(client, IT87_REG_TEMP_LOW(i));
		}

		/* The 8705 does not have VID capability */
		/*if (data->type == it8712) {
			data->vid = it87_read_value(client, IT87_REG_VID);
			data->vid &= 0x1f;
		}
		else */ {
			data->vid = 0x1f;
		}

		i = it87_read_value(client, IT87_REG_FAN_DIV);
		data->fan_div[0] = i & 0x07;
		data->fan_div[1] = (i >> 3) & 0x07;
		data->fan_div[2] = (i & 0x40) ? 3 : 1;

		data->alarms =
			it87_read_value(client, IT87_REG_ALARM1) |
			(it87_read_value(client, IT87_REG_ALARM2) << 8) |
			(it87_read_value(client, IT87_REG_ALARM3) << 16);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);
}

static int __init sm_it87_init(void)
{
	return i2c_add_driver(&it87_driver);
}

static void __exit sm_it87_exit(void)
{
	i2c_del_driver(&it87_driver);
}


MODULE_AUTHOR("Chris Gauthron <chrisg@0-in.com>");
MODULE_DESCRIPTION("IT8705F, IT8712F, Sis950 driver");
MODULE_PARM(update_vbat, "i");
MODULE_PARM_DESC(update_vbat, "Update vbat if set else return powerup value");
MODULE_PARM(temp_type, "i");
MODULE_PARM_DESC(temp_type, "Temperature sensor type, normally leave unset");
MODULE_LICENSE("GPL");

module_init(sm_it87_init);
module_exit(sm_it87_exit);
