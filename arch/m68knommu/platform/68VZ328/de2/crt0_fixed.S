#include <linux/config.h>
	
	.global _start
	.global _stext

	.global _rambase
	.global _ramvec
	.global _ramstart
	.global _ramend

#ifdef CONFIG_INIT_LCD
	.global splash_bits
#endif

	.data

/*
 *      Set up the usable of RAM stuff. Size of RAM is determined then
 *      an initial stack set up at the end.
 */
.align 4
_ramvec:
.long   0
_rambase:
.long   0
_ramstart:
.long   0
_ramend:
.long   0

#ifdef CONFIG_INIT_LCD
splash_bits:
#include "bootlogo.rh"
#endif
	
	.text
_start:
_stext:
	movew	#0x2700,%sr
	moveal  #__ramend-CONFIG_MEMORY_RESERVE*0x100000 - 0x10, %sp
	movel	#0xffffffff, 0xfffff304	/* disable all interrupts */

#ifdef CONFIG_RAMKERNEL
	/* Copy me from ROM to RAM */
	moveal	#__rom_start, %a0
	moveal	#__ram_start, %a1
	moveal	#__data_end, %a2

	/* Copy %a0 to %a1 until %a1 == %a2 */
2:	cmpal	%a1, %a2
	beq	1f
	movel	%a0@+, %a1@+
	bra	2b
1:
        /* jump into the RAM copy */
        jmp     ram_jump
ram_jump:
#endif

#ifdef CONFIG_INIT_LCD
	movel	#splash_bits, 0xfffffA00 /* LSSA */
	moveb	#0x28,   0xfffffA05	/* LVPW */
	movew	#0x280,  0xFFFFFa08	/* LXMAX */
	movew	#0x1df,  0xFFFFFa0a	/* LYMAX */
	moveb	#0,      0xfffffa29	/* LBAR */
	moveb	#0,      0xfffffa25	/* LPXCD */
	moveb	#0x08,   0xFFFFFa20	/* LPICF */
	moveb	#0x01,   0xFFFFFA21	/* -ve pol */
	moveb	#0x81,   0xfffffA27	/* LCKCON */
	movew	#0xff00, 0xfffff412	/* LCD pins */
#endif

	moveq   #13, %d7                /* '\r' */
	jsr putc
	moveq   #10, %d7                /* '\n' */
	jsr putc
	moveq   #65, %d7                /* 'A' */
	jsr putc

	movew	#32767, %d0  /* PLL settle wait loop */
1:	subq	#1, %d0
	bne	1b

	moveq   #66, %d7                /* 'B' */
	jsr putc

#ifndef CONFIG_RAMKERNEL
	/* Copy data segment from ROM to RAM */
	moveal	#__data_rom_start, %a0
	moveal	#_sdata, %a1
	moveal	#_edata, %a2

	/* Copy %a0 to %a1 until %a1 == %a2 */
2:	cmpal	%a1, %a2
	beq	1f
	movel	%a0@+, %a1@+
	bra	2b
1:
#endif
	moveq   #67, %d7                /* 'C' */
	jsr putc

	/* Initialize BSS segment to 0 */
	moveal	#_sbss, %a0
	moveal	#_ebss, %a1

	/* Copy 0 to %a0 until %a0 == %a1 */
2:	cmpal	%a0, %a1
	beq	1f
	clrl	%a0@+
	bra	2b
1:
	moveq   #68, %d7                /* 'D' */
	jsr putc

        movel   #_sdata, %d0    
        movel   %d0,    _rambase        
        movel   #_ebss,  %d0
        movel   %d0,    _ramstart
	movel	#__ramend-CONFIG_MEMORY_RESERVE*0x100000, %d0
	movel	%d0,	_ramend
	movel	#__ramvec,	%d0
	movel	%d0,	_ramvec
	
	moveq   #69, %d7                /* 'E' */
	jsr putc

/*
 * load the current task pointer and stack
 */
	lea	init_thread_union, %a0
	lea	0x2000(%a0), %sp

	moveq   #70, %d7                /* 'F' */
	jsr putc
	moveq   #13, %d7                /* '\r' */
	jsr putc
	moveq   #10, %d7                /* '\n' */
	jsr putc

1:	jsr	start_kernel
	bra	1b

_exit:
	bra	_exit

putc:
	moveb	%d7,0xfffff907
1:
	movew	0xfffff906, %d7
	andw	#0x2000, %d7
	beq	1b
	rts

	.data
env:
	.long	0
	.text

