/*
 * Remote control driver for the TV-card based on bt829
 *
 *  by Leonid Froenchenko <lfroen@galileo.co.il>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>

#include "lirc_dev.h"

int poll_main(void);
int atir_init_start(void);

void write_index(unsigned char index,unsigned int value);
unsigned int read_index(unsigned char index);

void do_i2c_start(void);
void do_i2c_stop(void);

void seems_wr_byte(unsigned char al);
unsigned char seems_rd_byte(void);

unsigned int read_index(unsigned char al);
void write_index(unsigned char ah,unsigned int edx);

void cycle_delay(int cycle);

void do_set_bits(unsigned char bl);
unsigned char do_get_bits(void);

#define DATA_PCI_OFF 0x7FFC00
#define WAIT_CYCLE   20


int atir_minor;
unsigned long pci_addr_phys, pci_addr_lin;

struct lirc_plugin atir_plugin;

int do_pci_probe(void)
{
	struct pci_dev *my_dev;
	my_dev = (struct pci_dev *)pci_find_device(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VT,NULL);
	if ( my_dev ) {
		printk(KERN_ERR "ATIR: Using device: %s\n",my_dev->slot_name);
		pci_addr_phys = 0;
		if ( my_dev->resource[0].flags & IORESOURCE_MEM ) {
			pci_addr_phys = my_dev->resource[0].start;
			printk(KERN_INFO "ATIR memory at 0x%08X \n",(unsigned int)pci_addr_phys);
		}
		if ( pci_addr_phys == 0 ) {
			printk(KERN_ERR "ATIR no memory resource ?\n");
			return 0;
		}
	} else {
		printk(KERN_ERR "ATIR: pci_prob failed\n");
		return 0;
	}
	return 1;
}


int atir_add_to_buf (void* data, struct lirc_buffer* buf)
{
	unsigned char key;
	int status;
	status = poll_main();
	key = (status >> 8) & 0xFF;
	if( status & 0xFF )
	{
	//    printk(KERN_INFO "ATIR reading key %02X\n",*key);
		lirc_buffer_write_1( buf, &key );
		return 0;
	}
	return -ENODATA;
}

int atir_set_use_inc(void* data)
{
	printk(KERN_DEBUG "ATIR driver is opened\n");
	return 0;
}

void atir_set_use_dec(void* data)
{
	printk(KERN_DEBUG "ATIR driver is closed\n");
}

static int __init lirc_bt829_init(void)
{
	if ( !do_pci_probe() ) {
		return 1;
	}

	if ( !atir_init_start() ) {
		return 1;
	}

	strcpy(atir_plugin.name,"ATIR");
	atir_plugin.minor       = -1;
	atir_plugin.code_length = 8;
	atir_plugin.sample_rate = 10;
	atir_plugin.data        = 0;
	atir_plugin.add_to_buf  = atir_add_to_buf;
	atir_plugin.set_use_inc = atir_set_use_inc;
	atir_plugin.set_use_dec = atir_set_use_dec;

	atir_minor = lirc_register_plugin(&atir_plugin);
	printk(KERN_DEBUG "ATIR driver is registered on minor %d\n",atir_minor);

	return 0;
}


static void __exit lirc_bt829_exit(void)
{
	lirc_unregister_plugin(atir_minor);
}


int atir_init_start(void)
{
	pci_addr_lin = (unsigned long)ioremap(pci_addr_phys + DATA_PCI_OFF,0x400);
	if ( pci_addr_lin == 0 ) {
		printk(KERN_INFO "atir: pci mem must be mapped\n");
		return 0;
	}
	return 1;
}

void cycle_delay(int cycle)
{
	udelay(WAIT_CYCLE*cycle);
}


int poll_main()
{
	unsigned char status_high, status_low;

	do_i2c_start();

	seems_wr_byte(0xAA);
	seems_wr_byte(0x01);

	do_i2c_start();

	seems_wr_byte(0xAB);

	status_low = seems_rd_byte();
	status_high = seems_rd_byte();

	do_i2c_stop();

	return (status_high << 8) | status_low;
}

void do_i2c_start(void)
{
	do_set_bits(3);
	cycle_delay(4);

	do_set_bits(1);
	cycle_delay(7);

	do_set_bits(0);
	cycle_delay(2);
}

void do_i2c_stop(void)
{
	unsigned char bits;
	bits =  do_get_bits() & 0xFD;
	do_set_bits(bits);
	cycle_delay(1);

	bits |= 1;
	do_set_bits(bits);
	cycle_delay(2);

	bits |= 2;
	do_set_bits(bits);
	bits = 3;
	do_set_bits(bits);
	cycle_delay(2);
}


void seems_wr_byte(unsigned char value)
{
	int i;
	unsigned char reg;

	reg = do_get_bits();
	for(i = 0;i < 8;i++) {
		if ( value & 0x80 ) {
			reg |= 0x02;
		} else {
			reg &= 0xFD;
		}
		do_set_bits(reg);
		cycle_delay(1);

		reg |= 1;
		do_set_bits(reg);
		cycle_delay(1);

		reg &= 0xFE;
		do_set_bits(reg);
		cycle_delay(1);
		value <<= 1;
	}
	cycle_delay(2);

	reg |= 2;
	do_set_bits(reg);

	reg |= 1;
	do_set_bits(reg);

	cycle_delay(1);
	do_get_bits();

	reg &= 0xFE;
	do_set_bits(reg);
	cycle_delay(3);
}

unsigned char seems_rd_byte(void)
{
	int i;
	int rd_byte;
	unsigned char bits_2, bits_1;

	bits_1 = do_get_bits() | 2;
	do_set_bits(bits_1);

	rd_byte = 0;
	for(i = 0;i < 8;i++) {
		bits_1 &= 0xFE;
		do_set_bits(bits_1);
		cycle_delay(2);

		bits_1 |= 1;
		do_set_bits(bits_1);
		cycle_delay(1);

		if ( (bits_2 = do_get_bits()) & 2 ) {
			rd_byte |= 1;
		}
		rd_byte <<= 1;
	}

	bits_1 = 0;
	if ( bits_2 == 0 ) {
		bits_1 |= 2;
	}
	do_set_bits(bits_1);
	cycle_delay(2);

	bits_1 |= 1;
	do_set_bits(bits_1);
	cycle_delay(3);

	bits_1 &= 0xFE;
	do_set_bits(bits_1);
	cycle_delay(2);

	rd_byte >>= 1;
	rd_byte &= 0xFF;
	return rd_byte;
}

void do_set_bits(unsigned char new_bits)
{
	int reg_val;
	reg_val = read_index(0x34);
	if ( new_bits & 2 ) {
		reg_val &= 0xFFFFFFDF;
		reg_val |= 1;
	} else {
		reg_val &= 0xFFFFFFFE;
		reg_val |= 0x20;
	}
	reg_val |= 0x10;
	write_index(0x34,reg_val);

	reg_val = read_index(0x31);
	if ( new_bits & 1 ) {
		reg_val |= 0x1000000;
	} else {
		reg_val &= 0xFEFFFFFF;
	}
	reg_val |= 0x8000000;
	write_index(0x31,reg_val);
}

unsigned char do_get_bits(void)
{
	unsigned char bits;
	int reg_val;

	reg_val = read_index(0x34);
	reg_val |= 0x10;
	reg_val &= 0xFFFFFFDF;
	write_index(0x34,reg_val);

	reg_val = read_index(0x34);
	bits = 0;
	if ( reg_val & 8 ) {
		bits |= 2;
	} else {
		bits &= 0xFD;
	}
	reg_val = read_index(0x31);
	if ( reg_val & 0x1000000 ) {
		bits |= 1;
	} else {
		bits &= 0xFE;
	}
	return bits;
}

unsigned int read_index(unsigned char index)
{
	unsigned int addr, value;
	//  addr = pci_addr_lin + DATA_PCI_OFF + ((index & 0xFF) << 2);
	addr = pci_addr_lin + ((index & 0xFF) << 2);
	value = readl(addr);
	return value;
}

void write_index(unsigned char index,unsigned int reg_val)
{
	unsigned int addr;
	addr = pci_addr_lin + ((index & 0xFF) << 2);
	writel(reg_val,addr);
}

MODULE_AUTHOR("Froenchenko Leonid");
MODULE_DESCRIPTION("IR remote driver for bt829 based TV cards");
MODULE_LICENSE("GPL");

module_init(lirc_bt829_init);
module_exit(lirc_bt829_exit);
