#include <linux/linkage.h>
#include <asm/assembler.h>
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
		teq	r2, #0
		bne	__arch_readsl
		RETINSTR(mov, pc, lr)

ENTRY(outsl)
		ioaddr	r0, r0
		teq	r2, #0
		bne	__arch_writesl
		RETINSTR(mov, pc, lr)

ENTRY(outsw)
		ioaddr	r0, r0
		teq	r2, #0
		bne	__arch_writesw
		RETINSTR(mov, pc, lr)

ENTRY(insw)
		ioaddr	r0, r0
		teq	r2, #0
		bne	__arch_readsw
		RETINSTR(mov, pc, lr)

ENTRY(insb)
		ioaddr	r0, r0
		teq	r2, #0
		bne	__arch_readsb
		RETINSTR(mov, pc, lr)

ENTRY(outsb)
		ioaddr	r0, r0
		teq	r2, #0
		bne	__arch_writesb
		RETINSTR(mov, pc, lr)

