/*
    eeprom.c - Part of lm_sensors, Linux kernel modules for hardware
               monitoring
    Copyright (C) 1998, 1999  Frodo Looijaard <frodol@dds.nl> and
			       Philip Edelbrock <phil@netroedge.com>
    Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>
    Copyright (C) 2003 IBM Corp.

    2004-01-16  Jean Delvare <khali@linux-fr.org>
    Divide the eeprom in 32-byte (arbitrary) slices. This significantly
    speeds sensors up, as well as various scripts using the eeprom
    module.

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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { 0x50, 0x57, I2C_CLIENT_END };
static unsigned int normal_isa[] = { I2C_CLIENT_ISA_END };
static unsigned int normal_isa_range[] = { I2C_CLIENT_ISA_END };

/* Insmod parameters */
SENSORS_INSMOD_1(eeprom);

static int checksum = 0;
MODULE_PARM(checksum, "i");
MODULE_PARM_DESC(checksum, "Only accept eeproms whose checksum is correct");


/* EEPROM registers */
#define EEPROM_REG_CHECKSUM	0x3f

/* Size of EEPROM in bytes */
#define EEPROM_SIZE		256

/* possible types of eeprom devices */
enum eeprom_nature {
	UNKNOWN,
	VAIO,
};

/* Each client has this additional data */
struct eeprom_data {
	struct i2c_client client;
	struct semaphore update_lock;
	u8 valid;			/* bitfield, bit!=0 if slice is valid */
	unsigned long last_updated[8];	/* In jiffies, 8 slices */
	u8 data[EEPROM_SIZE];		/* Register values */
	enum eeprom_nature nature;
};


static int eeprom_attach_adapter(struct i2c_adapter *adapter);
static int eeprom_detect(struct i2c_adapter *adapter, int address, int kind);
static int eeprom_detach_client(struct i2c_client *client);

/* This is the driver that will be inserted */
static struct i2c_driver eeprom_driver = {
	.owner		= THIS_MODULE,
	.name		= "eeprom",
	.id		= I2C_DRIVERID_EEPROM,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= eeprom_attach_adapter,
	.detach_client	= eeprom_detach_client,
};

static int eeprom_id = 0;

static void eeprom_update_client(struct i2c_client *client, u8 slice)
{
	struct eeprom_data *data = i2c_get_clientdata(client);
	int i, j;

	down(&data->update_lock);

	if (!(data->valid & (1 << slice)) ||
	    (jiffies - data->last_updated[slice] > 300 * HZ) ||
	    (jiffies < data->last_updated[slice])) {
		dev_dbg(&client->dev, "Starting eeprom update, slice %u\n", slice);

		if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
			for (i = slice << 5; i < (slice + 1) << 5; i += I2C_SMBUS_I2C_BLOCK_MAX)
				if (i2c_smbus_read_i2c_block_data(client, i, data->data + i) != I2C_SMBUS_I2C_BLOCK_MAX)
					goto exit;
		} else {
			if (i2c_smbus_write_byte(client, slice << 5)) {
				dev_dbg(&client->dev, "eeprom read start has failed!\n");
				goto exit;
			}
			for (i = slice << 5; i < (slice + 1) << 5; i++) {
				j = i2c_smbus_read_byte(client);
				if (j < 0)
					goto exit;
				data->data[i] = (u8) j;
			}
		}
		data->last_updated[slice] = jiffies;
		data->valid |= (1 << slice);
	}
exit:
	up(&data->update_lock);
}

static ssize_t eeprom_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = to_i2c_client(container_of(kobj, struct device, kobj));
	struct eeprom_data *data = i2c_get_clientdata(client);
	u8 slice;

	if (off > EEPROM_SIZE)
		return 0;
	if (off + count > EEPROM_SIZE)
		count = EEPROM_SIZE - off;

	/* Only refresh slices which contain requested bytes */
	for (slice = off >> 5; slice <= (off + count - 1) >> 5; slice++)
		eeprom_update_client(client, slice);

	/* Hide Vaio security settings to regular users (16 first bytes) */
	if (data->nature == VAIO && off < 16 && !capable(CAP_SYS_ADMIN)) {
		int in_row1 = 16 - off;
		memset(buf, 0, in_row1);
		if (count - in_row1 > 0)
			memcpy(buf + in_row1, &data->data[16], count - in_row1);
	} else {
		memcpy(buf, &data->data[off], count);
	}

	return count;
}

static struct bin_attribute eeprom_attr = {
	.attr = {
		.name = "eeprom",
		.mode = S_IRUGO,
		.owner = THIS_MODULE,
	},
	.size = EEPROM_SIZE,
	.read = eeprom_read,
};

static int eeprom_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_detect(adapter, &addr_data, eeprom_detect);
}

/* This function is called by i2c_detect */
int eeprom_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i, cs;
	struct i2c_client *new_client;
	struct eeprom_data *data;
	int err = 0;

	/* Make sure we aren't probing the ISA bus!! This is just a safety check
	   at this moment; i2c_detect really won't call us. */
#ifdef DEBUG
	if (i2c_is_isa_adapter(adapter)) {
		dev_dbg(&adapter->dev, " eeprom_detect called for an ISA bus adapter?!?\n");
		return 0;
	}
#endif

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access eeprom_{read,write}_value. */
	if (!(data = kmalloc(sizeof(struct eeprom_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data, 0, sizeof(struct eeprom_data));

	new_client = &data->client;
	memset(data->data, 0xff, EEPROM_SIZE);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &eeprom_driver;
	new_client->flags = 0;

	/* prevent 24RF08 corruption */
	i2c_smbus_write_quick(new_client, 0);

	/* Now, we do the remaining detection. It is not there, unless you force
	   the checksum to work out. */
	if (checksum) {
		cs = 0;
		for (i = 0; i <= 0x3e; i++)
			cs += i2c_smbus_read_byte_data(new_client, i);
		cs &= 0xff;
		if (i2c_smbus_read_byte_data (new_client, EEPROM_REG_CHECKSUM) != cs)
			goto exit_kfree;
	}

	data->nature = UNKNOWN;
	/* Detect the Vaio nature of EEPROMs.
	   We use the "PCG-" prefix as the signature. */
	if (address == 0x57) {
		if (i2c_smbus_read_byte_data(new_client, 0x80) == 'P' && 
		    i2c_smbus_read_byte_data(new_client, 0x81) == 'C' && 
		    i2c_smbus_read_byte_data(new_client, 0x82) == 'G' &&
		    i2c_smbus_read_byte_data(new_client, 0x83) == '-')
			data->nature = VAIO;
	}

	/* Fill in the remaining client fields */
	strncpy(new_client->name, "eeprom", I2C_NAME_SIZE);
	new_client->id = eeprom_id++;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_kfree;

	/* create the sysfs eeprom file */
	sysfs_create_bin_file(&new_client->dev.kobj, &eeprom_attr);

	return 0;

exit_kfree:
	kfree(data);
exit:
	return err;
}

static int eeprom_detach_client(struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err) {
		dev_err(&client->dev, "Client deregistration failed, client not detached.\n");
		return err;
	}

	kfree(i2c_get_clientdata(client));

	return 0;
}

static int __init eeprom_init(void)
{
	return i2c_add_driver(&eeprom_driver);
}

static void __exit eeprom_exit(void)
{
	i2c_del_driver(&eeprom_driver);
}


MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
		"Philip Edelbrock <phil@netroedge.com> and "
		"Greg Kroah-Hartman <greg@kroah.com>");
MODULE_DESCRIPTION("I2C EEPROM driver");
MODULE_LICENSE("GPL");

module_init(eeprom_init);
module_exit(eeprom_exit);
