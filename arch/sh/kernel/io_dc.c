/*
 *	$Id: io_dc.c,v 1.1 2001/04/01 15:02:00 yaegashi Exp $
 *	I/O routines for SEGA Dreamcast
 */

#include <linux/config.h>
#include <asm/io.h>
#include <asm/machvec.h>

unsigned long dreamcast_isa_port2addr(unsigned long offset)
{
	return offset + 0xa0000000;
}
