/*
 *  arch/etrax100/boot/compressed/head.S
 *
 *  Copyright (C) 1999 Axis Communications AB
 *
 *  Code that sets up the DRAM registers, calls the
 *  decompressor to unpack the piggybacked kernel, and jumps.
 *
 */

#include <linux/config.h>
#define ASSEMBLER_MACROS_ONLY
#include <asm/sv_addr_ag.h>

	;; Exported symbols
	
	.globl	_input_data

	
	.text

	nop
	di

#ifndef CONFIG_SVINTO_SIM	

	;; We need to setup the bus registers before we start using the DRAM

	move.d   DEF_R_WAITSTATES, r0
	move.d   r0, [R_WAITSTATES]

	move.d   DEF_R_BUS_CONFIG, r0
	move.d   r0, [R_BUS_CONFIG]
  
	move.d   DEF_R_DRAM_CONFIG, r0
	move.d   r0, [R_DRAM_CONFIG]

	move.d   DEF_R_DRAM_TIMING, r0
	move.d   r0, [R_DRAM_TIMING]

#endif

	;; Setup the stack to a suitably high address.
	;; We assume 8 MB is the minimum DRAM in an eLinux
	;; product and put the sp at the top for now.

	move.d	0x40800000, sp

	;; Figure out where the compressed piggyback image is
	;; in the flash (since we wont try to copy it to DRAM
	;; before unpacking). It is at _edata, but in flash.
	;; Use (_edata - basse) as offset to the current PC.
	
basse:	move.d	pc, r5
	and.d	0x7fffffff, r5	; strip any non-cache bit
	subq	2, r5		; compensate for the move.d pc instr
	move.d	r5, r0		; save for later - flash address of 'basse'
	add.d	_edata, r5
	sub.d	basse, r5	; r5 = flash address of '_edata'
	
	;; Copy text+data to DRAM
	
	move.d	basse, r1	; destination
	move.d	_edata, r2	; end destination
1:	move.w	[r0+], r3
	move.w	r3, [r1+]
	cmp.d	r2, r1
	bcs	1b
	nop

	move.d	r5, [_input_data] ; for the decompressor

	;; Clear the decompressors BSS (between _edata and _end)
	
	moveq	0, r0
	move.d	_edata, r1
	move.d	_end, r2
1:	move.w	r0, [r1+]
	cmp.d	r2, r1
	bcs	1b
	nop
	
	;; Do the decompression and save compressed size in _inptr
	
	jsr	_decompress_kernel

	;; Put start address of cramfs in r9 so the kernel can use it
	;; when mounting from flash

	move.d	[_input_data], r9	; flash address of compressed kernel
	add.d	[_inptr], r9		; size of compressed kernel

	;; Enter the decompressed kernel
		
	jump	0x40004000	; kernel is linked to this address
	
	.data

_input_data:
	.dword	0		; used by the decompressor
