/*
    Retrieve encoded MAC address from 24C16 serial 2-wire EEPROM,
    decode it and store it in the associated adapter struct for
    use by dvb_net.c

    This code was tested on TT-Budget/WinTV-NOVA-CI PCI boards with
    Atmel and ST Microelectronics EEPROMs.

    This card appear to have the 24C16 write protect held to ground,
    thus permitting normal read/write operation. Theoretically it
    would be possible to write routines to burn a different (encoded)
    MAC address into the EEPROM.

    Robert Schlabbach	GMX
    Michael Glaum	KVH Industries
    Holger Waechtler	Convergence

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

#include <asm/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "dvb_i2c.h"
#include "dvb_functions.h"

#if 1
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif


static int ttpci_eeprom_read_encodedMAC(struct dvb_i2c_bus *i2c, u8 * encodedMAC)
{
	int ret;
	u8 b0[] = { 0xd4 };

	struct i2c_msg msg[] = {
		{.addr = 0x50,.flags = 0,.buf = b0,.len = 1},
		{.addr = 0x50,.flags = I2C_M_RD,.buf = encodedMAC,.len = 6}
	};

	dprintk("%s\n", __FUNCTION__);

	ret = i2c->xfer(i2c, msg, 2);

	if (ret != 2)		/* Assume EEPROM isn't there */
		return (-ENODEV);

	return 0;
}

static void decodeMAC(u8 * decodedMAC, const u8 * encodedMAC)
{
	u8 ormask0[3] = { 0x54, 0x7B, 0x9E };
	u8 ormask1[3] = { 0xD3, 0xF1, 0x23 };
	u8 low;
	u8 high;
	u8 shift;
	int i;

	decodedMAC[0] = 0x00;
	decodedMAC[1] = 0xD0;
	decodedMAC[2] = 0x5C;

	for (i = 0; i < 3; i++) {
		low = encodedMAC[2 * i] ^ ormask0[i];
		high = encodedMAC[2 * i + 1] ^ ormask1[i];
		shift = (high >> 6) & 0x3;

		decodedMAC[5 - i] = ((high << 8) | low) >> shift;
	}

}


int ttpci_eeprom_parse_mac(struct dvb_i2c_bus *i2c)
{
	int ret;
	u8 encodedMAC[6];
	u8 decodedMAC[6];

	ret = ttpci_eeprom_read_encodedMAC(i2c, encodedMAC);

	if (ret != 0) {		/* Will only be -ENODEV */
		dprintk("Couldn't read from EEPROM: not there?\n");
		memset(i2c->adapter->proposed_mac, 0, 6);
		return ret;
	}

	decodeMAC(decodedMAC, encodedMAC);
	memcpy(i2c->adapter->proposed_mac, decodedMAC, 6);

	dprintk("%s adapter %i has MAC addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
		i2c->adapter->name, i2c->adapter->num,
		decodedMAC[0], decodedMAC[1], decodedMAC[2],
		decodedMAC[3], decodedMAC[4], decodedMAC[5]);
	dprintk("encoded MAC was %02x:%02x:%02x:%02x:%02x:%02x\n",
		encodedMAC[0], encodedMAC[1], encodedMAC[2],
		encodedMAC[3], encodedMAC[4], encodedMAC[5]);
	return 0;
}

EXPORT_SYMBOL(ttpci_eeprom_parse_mac);
