/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <asm/io.h>
#include <linux/pci.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/simulator.h>

#ifdef Colin /* Use the same calls as Generic IA64 defined in io.h */
/**
 * sn1_io_addr - convert a in/out port to an i/o address
 * @port: port to convert
 *
 * Legacy in/out instructions are converted to ld/st instructions
 * on IA64.  This routine will convert a port number into a valid 
 * SN i/o address.  Used by sn1_in*() and sn1_out*().
 */
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
			io_base = (0xc000000fcc000000 | ((unsigned long)get_nasid() << 38));
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

#endif
