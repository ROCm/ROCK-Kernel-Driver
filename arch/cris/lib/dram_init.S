	;; $Id: dram_init.S,v 1.2 2001/02/08 15:20:00 starvik Exp $
	;; 
	;; DRAM/SDRAM initialization - alter with care
	;; This file is intended to be included from other assembler files
	;;
	;; Copyright (C) 2000 Axis Communications AB
	;;
	;; Authors:  Mikael Starvik (starvik@axis.com)	
	;;           Bjorn Wesen (bjornw@axis.com)
	;; 
	;; $Log: dram_init.S,v $
	;; Revision 1.2  2001/02/08 15:20:00  starvik
	;; Corrected SDRAM initialization
	;; Should now be included as inline
	;;
	;; Revision 1.1  2001/01/29 13:08:02  starvik
	;; Initial version
	;; This file should be included from all assembler files that needs to
	;; initialize DRAM/SDRAM.
	;;
	;;
	;;

#include <linux/config.h>

#ifndef CONFIG_SVINTO_SIM	
	move.d   DEF_R_WAITSTATES, r0
	move.d   r0, [R_WAITSTATES]

	move.d   DEF_R_BUS_CONFIG, r0
	move.d   r0, [R_BUS_CONFIG]
	
#ifndef CONFIG_SDRAM
	move.d   DEF_R_DRAM_CONFIG, r0
	move.d   r0, [R_DRAM_CONFIG]

	move.d   DEF_R_DRAM_TIMING, r0
	move.d   r0, [R_DRAM_TIMING]
#else	
	; Refer to ETRAX 100LX Designers Reference for a description of SDRAM initialization
	
	; Bank configuration
        move.d   DEF_R_SDRAM_CONFIG, r0
	move.d   r0, [R_SDRAM_CONFIG]

	; Calculate value of mrs_data 
	; cas_delay = 2 && bus_width = 32 => 0x40
	; cas_delay = 3 && bus_width = 32 => 0x60
	; cas_delay = 2 && bus_width = 16 => 0x20
	; cas_delay = 3 && bus_width = 16 => 0x30
	
	move.d   0x40, r2       ; Assume 32 bits and cas_delay = 2
	move.d   DEF_R_SDRAM_TIMING, r1
	and.d    0x0c, r1	; Get cas delay
	cmp.d    0x08, r1	; cas_delay = 2?
        beq      bw_check
	nop
	or.d     0x20, r2       ; cas_delay = 3
bw_check:
	move.d   DEF_R_SDRAM_CONFIG, r1
	and.d    0x800000, r1	; DRAM width is bit 23
	bne      set_timing
	nop
	lsrq     1, r2		;  16 bits. Shift down value.

	; Set timing parameters. Starts master clock
set_timing:
	move.d   DEF_R_SDRAM_TIMING, r1
	or.d     0x80000000, r1	; Make sure sdram enable bit is set
	lslq     16, r2		; mrs data starts at bit 16
	or.d     r2, r1 
	move.d   r1, [R_SDRAM_TIMING]	
		
	; Wait 200ns
	move.d   10, r2
sdram_loop:
	bne      sdram_loop
	subq     1, r2
	
	; Issue initialization command sequence
	move.d   sdram_commands_start, r2
	move.d   sdram_commands_end,  r3
command_loop:
	clear.d  r4
	move.b   [r2+], r4
	lslq     9, r4		; Command starts at bit 9
	or.d     r1, r4
	move.d   r4, [R_SDRAM_TIMING]
	nop			; Wait five nop cycles between each command
	nop
	nop
	nop
	nop
	cmp.d    r2, r3
	bne      command_loop
	nop
	ba       sdram_commands_end
	nop

sdram_commands_start:
	.byte   3	; Precharge
	.byte   0       ; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   2	; refresh
	.byte   0	; nop
	.byte   1	; mrs
	.byte   0	; nop 
sdram_commands_end:		
#endif
#endif
