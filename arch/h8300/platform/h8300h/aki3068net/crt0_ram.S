/*
 *  linux/arch/h8300/platform/h8300h/aki3068net/crt0_ram.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend startup for uClinux-2.4.x
 *  Target Archtecture:	AE-3068 (aka. aki3068net)
 *  Memory Layout     :	RAM
 */

#define ASSEMBLY

#include <linux/config.h>
#include <asm/linkage.h>
	
	.global SYMBOL_NAME(_start)
	.global SYMBOL_NAME(command_line)
	.global SYMBOL_NAME(_platform_gpio_table)
	.global SYMBOL_NAME(_target_name)
	
	.h8300h

	.section .text
	.file	"crt0_ram.S"

	/* CPU Reset entry */
SYMBOL_NAME_LABEL(_start)
	mov.l	#__ramend,sp
	ldc	#0x80,ccr

	/* Peripheral Setup */
	
	/* .bss clear */
	mov.l	#__sbss,er5
	mov.l	er5,er6
	inc.l	#1,er6
	mov.l	#__ebss,er4
	sub.l	er5,er4
	sub.w	r0,r0
	mov.b	r0l,@er5
1:	
	eepmov.w
	dec.w	#1,e4
	bpl	1b

	/* copy kernel commandline */
	mov.l	#COMMAND_START,er5
	mov.l	#SYMBOL_NAME(command_line),er6
	mov.w	#512,r4
	eepmov.w

	/* RAM Interrupt Vector Table Setup */
#if defined(CONFIG_GDB_DEBUG)
	mov.l	@SYMBOL_NAME(interrupt_redirect_table)+11*4,er0
#endif
	mov.l	#SYMBOL_NAME(_vector_lma),er5
	mov.l	#SYMBOL_NAME(interrupt_redirect_table),er6
	mov.w	#0x100,r4
	eepmov.w
#if defined(CONFIG_GDB_DEBUG)
	mov.l	er0,@SYMBOL_NAME(interrupt_redirect_table)+11*4
#endif

	/* uClinux kernel start */
	ldc	#0x90,ccr	/* running kernel */
	mov.l	#SYMBOL_NAME(init_task_union),sp
	mov.l	sp,@SYMBOL_NAME(_current_task)
	add.l	#0x2000,sp
	jsr	@_start_kernel
_exit:

	jmp	_exit

	rts

	/* I/O port assign information */
__platform_gpio_table:	
	mov.l	#gpio_table,er0
	rts

gpio_table:
	;; P1DDR
	.byte	0xff,0xff
	;; P2DDR
	.byte	0xff,0xff
	;; P3DDR
	.byte	0xff,0x00
	;; P4DDR
	.byte	0x00,0x00
	;; P5DDR
	.byte	0x01,0x01
	;; P6DDR
	.byte	0x00,0x00
	;; dummy
	.byte	0x00,0x00
	;; P8DDR
	.byte	0x0c,0x0c
	;; P9DDR
	.byte	0x00,0x00
	;; PADDR
	.byte	0x00,0x00
	;; PBDDR
	.byte	0x30,0x30

__target_name:	
	.asciz	"AE-3068"
	
	.section .bootvec,"ax"
	jmp	@SYMBOL_NAME(_start)
