/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */

#include <asm/io.h>
#include <linux/pci.h>
#include <asm/sn/simulator.h>
#include <asm/pio_flush.h>
#include <asm/delay.h>

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
		if ((port >= 0x1f0 && port <= 0x1f7) ||
			port == 0x3f6 || port == 0x3f7) {
			io_base = __IA64_UNCACHED_OFFSET | 0x00000FFFFC000000;
			addr = io_base | ((port >> 2) << 12) | (port & 0xfff);
		} else {
			addr = __ia64_get_io_port_base() | ((port >> 2) << 2);
		}
		return(void *) addr;
	}
}

/**
 * sn1_inb - read a byte from a port
 * @port: port to read from
 *
 * Reads a byte from @port and returns it to the caller.
 */
unsigned int
sn1_inb (unsigned long port)
{
return __ia64_inb ( port );
}

/**
 * sn1_inw - read a word from a port
 * @port: port to read from
 *
 * Reads a word from @port and returns it to the caller.
 */
unsigned int
sn1_inw (unsigned long port)
{
return __ia64_inw ( port );
}

/**
 * sn1_inl - read a word from a port
 * @port: port to read from
 *
 * Reads a word from @port and returns it to the caller.
 */
unsigned int
sn1_inl (unsigned long port)
{
return __ia64_inl ( port );
}

/**
 * sn1_outb - write a byte to a port
 * @port: port to write to
 * @val: value to write
 *
 * Writes @val to @port.
 */
void
sn1_outb (unsigned char val, unsigned long port)
{
return __ia64_outb ( val, port );
}

/**
 * sn1_outw - write a word to a port
 * @port: port to write to
 * @val: value to write
 *
 * Writes @val to @port.
 */
void
sn1_outw (unsigned short val, unsigned long port)
{
return __ia64_outw ( val, port );
}

/**
 * sn1_outl - write a word to a port
 * @port: port to write to
 * @val: value to write
 *
 * Writes @val to @port.
 */
void
sn1_outl (unsigned int val, unsigned long port)
{
return __ia64_outl ( val, port );
}

/**
 * sn1_inb - read a byte from a port
 * @port: port to read from
 *
 * Reads a byte from @port and returns it to the caller.
 */
unsigned int
sn1_inb (unsigned long port)
{
	volatile unsigned char *addr = sn1_io_addr(port);
	unsigned char ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

/**
 * sn1_inw - read a word from a port
 * 2port: port to read from
 *
 * Reads a word from @port and returns it to the caller.
 */
unsigned int
sn1_inw (unsigned long port)
{
	volatile unsigned short *addr = sn1_io_addr(port);
	unsigned short ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

/**
 * sn1_inl - read a word from a port
 * @port: port to read from
 *
 * Reads a word from @port and returns it to the caller.
 */
unsigned int
sn1_inl (unsigned long port)
{
	volatile unsigned int *addr = sn1_io_addr(port);
	unsigned int ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

/**
 * sn1_outb - write a byte to a port
 * @port: port to write to
 * @val: value to write
 *
 * Writes @val to @port.
 */
void
sn1_outb (unsigned char val, unsigned long port)
{
	volatile unsigned char *addr = sn1_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

/**
 * sn1_outw - write a word to a port
 * @port: port to write to
 * @val: value to write
 *
 * Writes @val to @port.
 */
void
sn1_outw (unsigned short val, unsigned long port)
{
	volatile unsigned short *addr = sn1_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

/**
 * sn1_outl - write a word to a port
 * @port: port to write to
 * @val: value to write
 *
 * Writes @val to @port.
 */
void
sn1_outl (unsigned int val, unsigned long port)
{
	volatile unsigned int *addr = sn1_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}
#endif /* SN1_IOPORTS */

void
sn_mmiob ()
{
	PIO_FLUSH();
}
