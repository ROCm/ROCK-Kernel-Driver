/*
 * common keywest i2c layer
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sound/core.h>
#include "pmac.h"

/*
 * we have to keep a static variable here since i2c attach_adapter
 * callback cannot pass a private data.
 */
static pmac_keywest_t *keywest_ctx;


#define I2C_DRIVERID_KEYWEST	0xFEBA

static int keywest_attach_adapter(struct i2c_adapter *adapter);
static int keywest_detach_client(struct i2c_client *client);

struct i2c_driver keywest_driver = {  
	.name = "PMac Keywest Audio",
	.id = I2C_DRIVERID_KEYWEST,
	.flags = I2C_DF_NOTIFY,
	.attach_adapter = &keywest_attach_adapter,
	.detach_client = &keywest_detach_client,
};


static int keywest_attach_adapter(struct i2c_adapter *adapter)
{
	int err;
	struct i2c_client *new_client;

	if (! keywest_ctx)
		return -EINVAL;

	if (strncmp(adapter->name, "mac-io", 6))
		return 0;

	new_client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (! new_client)
		return -ENOMEM;

	new_client->addr = keywest_ctx->addr;
	new_client->data = keywest_ctx;
	new_client->adapter = adapter;
	new_client->driver = &keywest_driver;
	new_client->flags = 0;

	strcpy(new_client->name, keywest_ctx->name);

	new_client->id = keywest_ctx->id++; /* Automatically unique */
	keywest_ctx->client = new_client;

	if ((err = keywest_ctx->init_client(keywest_ctx)) < 0)
		goto __err;

	/* Tell the i2c layer a new client has arrived */
	if (i2c_attach_client(new_client)) {
		err = -ENODEV;
		goto __err;
	}

	return 0;

 __err:
	kfree(new_client);
	keywest_ctx->client = NULL;
	return err;
}

static int keywest_detach_client(struct i2c_client *client)
{
	if (! keywest_ctx)
		return 0;
	if (client == keywest_ctx->client)
		keywest_ctx->client = NULL;

	i2c_detach_client(client);
	kfree(client);
	return 0;
}

/* exported */
void snd_pmac_keywest_cleanup(pmac_keywest_t *i2c)
{
	if (keywest_ctx && keywest_ctx == i2c) {
		i2c_del_driver(&keywest_driver);
		keywest_ctx = NULL;
	}
}

/* exported */
int __init snd_pmac_keywest_init(pmac_keywest_t *i2c)
{
	int err;

	if (keywest_ctx)
		return -EBUSY;

	if ((err = i2c_add_driver(&keywest_driver))) {
		snd_printk(KERN_ERR "cannot register keywest i2c driver\n");
		return err;
	}
	keywest_ctx = i2c;
	return 0;
}
