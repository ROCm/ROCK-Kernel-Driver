/*
    i2c-sensor.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998 - 2001 Frodo Looijaard <frodol@dds.nl> and
    Mark D. Studebaker <mdsxyz123@yahoo.com>

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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>
#include <asm/uaccess.h>


/* Very inefficient for ISA detects, and won't work for 10-bit addresses! */
int i2c_detect(struct i2c_adapter *adapter,
	       struct i2c_address_data *address_data,
	       int (*found_proc) (struct i2c_adapter *, int, int))
{
	int addr, i, found, j, err;
	struct i2c_force_data *this_force;
	int is_isa = i2c_is_isa_adapter(adapter);
	int adapter_id =
	    is_isa ? ANY_I2C_ISA_BUS : i2c_adapter_id(adapter);

	/* Forget it if we can't probe using SMBUS_QUICK */
	if ((!is_isa) &&
	    !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_QUICK))
		return -1;

	for (addr = 0x00; addr <= (is_isa ? 0xffff : 0x7f); addr++) {
		if (!is_isa && i2c_check_addr(adapter, addr))
			continue;

		/* If it is in one of the force entries, we don't do any
		   detection at all */
		found = 0;
		for (i = 0; !found && (this_force = address_data->forces + i, this_force->force); i++) {
			for (j = 0; !found && (this_force->force[j] != I2C_CLIENT_END); j += 2) {
				if ( ((adapter_id == this_force->force[j]) ||
				      ((this_force->force[j] == ANY_I2C_BUS) && !is_isa)) &&
				      (addr == this_force->force[j + 1]) ) {
					dev_dbg(&adapter->dev, "found force parameter for adapter %d, addr %04x\n", adapter_id, addr);
					if ((err = found_proc(adapter, addr, this_force->kind)))
						return err;
					found = 1;
				}
			}
		}
		if (found)
			continue;

		/* If this address is in one of the ignores, we can forget about it
		   right now */
		for (i = 0; !found && (address_data->ignore[i] != I2C_CLIENT_END); i += 2) {
			if ( ((adapter_id == address_data->ignore[i]) ||
			      ((address_data->ignore[i] == ANY_I2C_BUS) &&
			       !is_isa)) &&
			      (addr == address_data->ignore[i + 1])) {
				dev_dbg(&adapter->dev, "found ignore parameter for adapter %d, addr %04x\n", adapter_id, addr);
				found = 1;
			}
		}
		for (i = 0; !found && (address_data->ignore_range[i] != I2C_CLIENT_END); i += 3) {
			if ( ((adapter_id == address_data->ignore_range[i]) ||
			      ((address_data-> ignore_range[i] == ANY_I2C_BUS) & 
			       !is_isa)) &&
			     (addr >= address_data->ignore_range[i + 1]) &&
			     (addr <= address_data->ignore_range[i + 2])) {
				dev_dbg(&adapter->dev,  "found ignore_range parameter for adapter %d, addr %04x\n", adapter_id, addr);
				found = 1;
			}
		}
		if (found)
			continue;

		/* Now, we will do a detection, but only if it is in the normal or 
		   probe entries */
		if (is_isa) {
			for (i = 0; !found && (address_data->normal_isa[i] != I2C_CLIENT_ISA_END); i += 1) {
				if (addr == address_data->normal_isa[i]) {
					dev_dbg(&adapter->dev, "found normal isa entry for adapter %d, addr %04x\n", adapter_id, addr);
					found = 1;
				}
			}
			for (i = 0; !found && (address_data->normal_isa_range[i] != I2C_CLIENT_ISA_END); i += 3) {
				if ((addr >= address_data->normal_isa_range[i]) &&
				    (addr <= address_data->normal_isa_range[i + 1]) &&
				    ((addr - address_data->normal_isa_range[i]) % address_data->normal_isa_range[i + 2] == 0)) {
					dev_dbg(&adapter->dev, "found normal isa_range entry for adapter %d, addr %04x", adapter_id, addr);
					found = 1;
				}
			}
		} else {
			for (i = 0; !found && (address_data->normal_i2c[i] != I2C_CLIENT_END); i += 1) {
				if (addr == address_data->normal_i2c[i]) {
					found = 1;
					dev_dbg(&adapter->dev, "found normal i2c entry for adapter %d, addr %02x", adapter_id, addr);
				}
			}
			for (i = 0; !found && (address_data->normal_i2c_range[i] != I2C_CLIENT_END); i += 2) {
				if ((addr >= address_data->normal_i2c_range[i]) &&
				    (addr <= address_data->normal_i2c_range[i + 1])) {
					dev_dbg(&adapter->dev, "found normal i2c_range entry for adapter %d, addr %04x\n", adapter_id, addr);
					found = 1;
				}
			}
		}

		for (i = 0;
		     !found && (address_data->probe[i] != I2C_CLIENT_END);
		     i += 2) {
			if (((adapter_id == address_data->probe[i]) ||
			     ((address_data->
			       probe[i] == ANY_I2C_BUS) && !is_isa))
			    && (addr == address_data->probe[i + 1])) {
				dev_dbg(&adapter->dev, "found probe parameter for adapter %d, addr %04x\n", adapter_id, addr);
				found = 1;
			}
		}
		for (i = 0; !found && (address_data->probe_range[i] != I2C_CLIENT_END); i += 3) {
			if ( ((adapter_id == address_data->probe_range[i]) ||
			      ((address_data->probe_range[i] == ANY_I2C_BUS) && !is_isa)) &&
			     (addr >= address_data->probe_range[i + 1]) &&
			     (addr <= address_data->probe_range[i + 2])) {
				found = 1;
				dev_dbg(&adapter->dev, "found probe_range parameter for adapter %d, addr %04x\n", adapter_id, addr);
			}
		}
		if (!found)
			continue;

		/* OK, so we really should examine this address. First check
		   whether there is some client here at all! */
		if (is_isa ||
		    (i2c_smbus_xfer (adapter, addr, 0, 0, 0, I2C_SMBUS_QUICK, NULL) >= 0))
			if ((err = found_proc(adapter, addr, -1)))
				return err;
	}
	return 0;
}

EXPORT_SYMBOL(i2c_detect);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_DESCRIPTION("i2c-sensor driver");
MODULE_LICENSE("GPL");
