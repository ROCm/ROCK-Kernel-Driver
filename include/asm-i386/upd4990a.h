/*
 *  Architecture dependent definitions
 *  for NEC uPD4990A serial I/O real-time clock.
 *
 *  Copyright 2001  TAKAI Kousuke <tak@kmc.kyoto-u.ac.jp>
 *		    Kyoto University Microcomputer Club (KMC).
 *
 *  References:
 *	uPD4990A serial I/O real-time clock users' manual (Japanese)
 *	No. S12828JJ4V0UM00 (4th revision), NEC Corporation, 1999.
 */

#ifndef _ASM_I386_uPD4990A_H
#define _ASM_I386_uPD4990A_H

#include <asm/io.h>

#define UPD4990A_IO		(0x0020)
#define UPD4990A_IO_DATAOUT	(0x0033)

#define UPD4990A_OUTPUT_DATA_CLK(data, clk)		\
	outb((((data) & 1) << 5) | (((clk) & 1) << 4)	\
	      | UPD4990A_PAR_SERIAL_MODE, UPD4990A_IO)

#define UPD4990A_OUTPUT_CLK(clk)	UPD4990A_OUTPUT_DATA_CLK(0, (clk))

#define UPD4990A_OUTPUT_STROBE(stb) \
	outb(((stb) << 3) | UPD4990A_PAR_SERIAL_MODE, UPD4990A_IO)

/*
 * Note: udelay() is *not* usable for UPD4990A_DELAY because
 *	 the Linux kernel reads uPD4990A to set up system clock
 *	 before calibrating delay...
 */
#define UPD4990A_DELAY(usec)						\
	do {								\
		if (__builtin_constant_p((usec)) && (usec) < 5)	\
			__asm__ (".rept %c1\n\toutb %%al,%0\n\t.endr"	\
				 : : "N" (0x5F),			\
				     "i" (((usec) * 10 + 5) / 6));	\
		else {							\
			int _count = ((usec) * 10 + 5) / 6;		\
			__asm__ volatile ("1: outb %%al,%1\n\tloop 1b"	\
					  : "=c" (_count)		\
					  : "N" (0x5F), "0" (_count));	\
		}							\
	} while (0)

/* Caller should ignore all bits except bit0 */
#define UPD4990A_READ_DATA()	inb(UPD4990A_IO_DATAOUT)

#endif
