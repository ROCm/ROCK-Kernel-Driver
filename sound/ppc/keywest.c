/*
 * Keywest i2c code
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *
 * based on i2c-keywest.c from lm_sensors.
 *    Copyright (c) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>
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
#include <sound/core.h>
#include "pmac.h"

/* The Tumbler audio equalizer can be really slow sometimes */
#define KW_POLL_SANITY 10000

/* address indices */
#define KW_ADDR_MODE	0
#define KW_ADDR_CONTROL	1
#define KW_ADDR_STATUS	2
#define KW_ADDR_ISR	3
#define KW_ADDR_IER	4
#define KW_ADDR_ADDR	5
#define KW_ADDR_SUBADDR	6
#define KW_ADDR_DATA	7

#define KW_I2C_ADDR(i2c,type)	((i2c)->base + (type) * (i2c)->steps)

/* keywest needs a small delay to defuddle itself after changing a setting */
inline static void keywest_writeb_wait(pmac_keywest_t *i2c, int addr, int value)
{
	writeb(value, KW_I2C_ADDR(i2c, addr));
	udelay(10);
}

inline static void keywest_writeb(pmac_keywest_t *i2c, int addr, int value)
{
	writeb(value, KW_I2C_ADDR(i2c, addr));
}

inline unsigned char keywest_readb(pmac_keywest_t *i2c, int addr)
{
	return readb(KW_I2C_ADDR(i2c, addr));
}

static int keywest_poll_interrupt(pmac_keywest_t *i2c)
{
	int i, res;
	for (i = 0; i < KW_POLL_SANITY; i++) {
		udelay(100);
		res = keywest_readb(i2c, KW_ADDR_ISR) & 0x0f;
		if (res > 0)
			return res;
	}

	//snd_printd("Sanity check failed!  Expected interrupt never happened.\n");
	return -ENODEV;
}


static void keywest_reset(pmac_keywest_t *i2c)
{
	int interrupt_state;

	/* Clear all past interrupts */
	interrupt_state = keywest_readb(i2c, KW_ADDR_ISR) & 0x0f;
	if (interrupt_state > 0)
		keywest_writeb_wait(i2c, KW_ADDR_ISR, interrupt_state);
}

static int keywest_start(pmac_keywest_t *i2c, unsigned char cmd, int is_read)
{
	int interrupt_state;
	int ack;

	keywest_reset(i2c);

	/* Set up address and r/w bit */
	keywest_writeb_wait(i2c, KW_ADDR_ADDR, (i2c->addr << 1) | (is_read ? 1 : 0));

	/* Set up 'sub address' which I'm guessing is the command field? */
	keywest_writeb_wait(i2c, KW_ADDR_SUBADDR, cmd);
	
	/* Start sending address */
	keywest_writeb_wait(i2c, KW_ADDR_CONTROL, keywest_readb(i2c, KW_ADDR_CONTROL) | 2);
	interrupt_state = keywest_poll_interrupt(i2c);
	if (interrupt_state < 0)
		return interrupt_state;

	ack = keywest_readb(i2c, KW_ADDR_STATUS) & 0x0f;
	if ((ack & 0x02) == 0) {
		snd_printd("Ack Status on addr expected but got: 0x%02x on addr: 0x%02x\n", ack, i2c->addr);
		return -EINVAL;
	} 
	return interrupt_state;
}

/* exported */
int snd_pmac_keywest_write(pmac_keywest_t *i2c, unsigned char cmd, int len, unsigned char *data)
{
	int interrupt_state;
	int error_state = 0;
	int i;

	snd_assert(len >= 1 && len <= 32, return -EINVAL);

	if ((interrupt_state = keywest_start(i2c, cmd, 0)) < 0)
		return -EINVAL;

	for(i = 0; i < len; i++) {
		keywest_writeb_wait(i2c, KW_ADDR_DATA, data[i]);

		/* Clear interrupt and go */
		keywest_writeb_wait(i2c, KW_ADDR_ISR, interrupt_state);

		interrupt_state = keywest_poll_interrupt(i2c);
		if (interrupt_state < 0) {
			error_state = -EINVAL;
			interrupt_state = 0;
		}
		if ((keywest_readb(i2c, KW_ADDR_STATUS) & 0x02) == 0) {
			snd_printd("Ack Expected by not received(block)!\n");
			error_state = -EINVAL;
		}
	}

	/* Send stop */
	keywest_writeb_wait(i2c, KW_ADDR_CONTROL,
			    keywest_readb(i2c, KW_ADDR_CONTROL) | 4);

	keywest_writeb_wait(i2c, KW_ADDR_CONTROL, interrupt_state);
		
	interrupt_state = keywest_poll_interrupt(i2c);
	if (interrupt_state < 0) {
		error_state = -EINVAL;
		interrupt_state = 0;
	}
	keywest_writeb_wait(i2c, KW_ADDR_ISR, interrupt_state);

	return error_state;
}

/* exported */
void snd_pmac_keywest_cleanup(pmac_keywest_t *i2c)
{
	if (i2c->base) {
		iounmap((void*)i2c->base);
		i2c->base = 0;
	}
}

/* exported */
int snd_pmac_keywest_find(pmac_t *chip, pmac_keywest_t *i2c, int addr,
			  int (*init_client)(pmac_t *, pmac_keywest_t *))
{
	struct device_node *i2c_device;
	void **temp;
	void *base = NULL;
	u32 steps = 0;

	i2c_device = find_compatible_devices("i2c", "keywest");
	
	if (i2c_device == 0) {
		snd_printk("No Keywest i2c devices found.\n");
		return -ENODEV;
	}
	
	for (; i2c_device; i2c_device = i2c_device->next) {
		snd_printd("Keywest device found: %s\n", i2c_device->full_name);
		temp = (void **) get_property(i2c_device, "AAPL,address", NULL);
		if (temp != NULL) {
			base = *temp;
		} else {
			snd_printd("pmac: no 'address' prop!\n");
			continue;
		}

		temp = (void **) get_property(i2c_device, "AAPL,address-step", NULL);
		if (temp != NULL) {
			steps = *(uint *)temp;
		} else {
			snd_printd("pmac: no 'address-step' prop!\n");
			continue;
		}
		
		i2c->base = (unsigned long)ioremap((unsigned long)base, steps * 8);
		i2c->steps = steps;

		/* Select standard sub mode 
		 *  
		 * ie for <Address><Ack><Command><Ack><data><Ack>... style transactions
		 */
		keywest_writeb_wait(i2c, KW_ADDR_MODE, 0x08);

		/* Enable interrupts */
		keywest_writeb_wait(i2c, KW_ADDR_IER, 1 + 2 + 4 + 8);

		keywest_reset(i2c);

		i2c->addr = addr;
		if (init_client(chip, i2c) < 0) {
			snd_pmac_keywest_cleanup(i2c);
			continue;
		}

		return 0; /* ok */
	}
	
	return -ENODEV;
}
