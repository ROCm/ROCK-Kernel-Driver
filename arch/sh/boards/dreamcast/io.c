/*
 *	$Id: io.c,v 1.1.2.1 2002/01/19 23:54:19 mrbrown Exp $
 *	I/O routines for SEGA Dreamcast
 */

#include <asm/io.h>
#include <asm/machvec.h>

unsigned long dreamcast_isa_port2addr(unsigned long offset)
{
	return offset + 0xa0000000;
}
