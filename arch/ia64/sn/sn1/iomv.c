/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 * Copyright (C) 2000 Kanoj Sarcar (kanoj@sgi.com)
 */

#include <asm/io.h>
#include <linux/pci.h>

static inline void *
sn1_io_addr(unsigned long port)
{
	if (!IS_RUNNING_ON_SIMULATOR()) {
		return( (void *)  (port | __IA64_UNCACHED_OFFSET));
	} else {
		unsigned long io_base;
		unsigned long addr;
 
		/*
 		 * word align port, but need more than 10 bits
 		 * for accessing registers in bedrock local block
 		 * (so we don't do port&0xfff)
 		 */
		if (port == 0x1f6 || port == 0x1f7
			|| port == 0x3f6 || port == 0x3f7
			|| port == 0x1f0 || port == 0x1f1
			|| port == 0x1f3 || port == 0x1f4
			|| port == 0x1f2 || port == 0x1f5)  {
			io_base = __IA64_UNCACHED_OFFSET | 0x00000FFFFC000000;
			addr = io_base | ((port >> 2) << 12) | (port & 0xfff);
		} else {
			addr = __ia64_get_io_port_base() | ((port >> 2) << 2);
		}
		return(void *) addr;
	}
}

unsigned int
sn1_inb (unsigned long port)
{
	volatile unsigned char *addr = sn1_io_addr(port);
	unsigned char ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

unsigned int
sn1_inw (unsigned long port)
{
	volatile unsigned short *addr = sn1_io_addr(port);
	unsigned short ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

unsigned int
sn1_inl (unsigned long port)
{
	volatile unsigned int *addr = sn1_io_addr(port);
	unsigned int ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

void
sn1_outb (unsigned char val, unsigned long port)
{
	volatile unsigned char *addr = sn1_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

void
sn1_outw (unsigned short val, unsigned long port)
{
	volatile unsigned short *addr = sn1_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

void
sn1_outl (unsigned int val, unsigned long port)
{
	volatile unsigned int *addr = sn1_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}
