#include <linux/linkage.h>
#include <asm/hardware.h>

		.equ	pcio_high, PCIO_BASE & 0xff000000
		.equ	pcio_low,  PCIO_BASE & 0x00ffffff

		.macro	ioaddr, rd,rn
		add	\rd, \rn, #pcio_high
		.if	pcio_low
		add	\rd, \rd, #pcio_low
		.endif
		.endm

ENTRY(insl)
		ioaddr	r0, r0
		b	__arch_readsl

ENTRY(outsl)
		ioaddr	r0, r0
		b	__arch_writesl

		/* Nobody could say these are optimal, but not to worry. */

ENTRY(outsw)
		ioaddr	r0, r0
		b	__arch_writesw

ENTRY(insw)
		ioaddr	r0, r0
		b	__arch_readsw

ENTRY(insb)
		ioaddr	r0, r0
		b	__arch_readsb

ENTRY(outsb)
		ioaddr	r0, r0
		b	__arch_writesb
