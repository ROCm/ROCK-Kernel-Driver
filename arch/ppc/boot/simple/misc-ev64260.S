/*
 * arch/ppc/boot/simple/misc-ev64260.S
 *
 * Host bridge init code for the Marvell/Galileo EV-64260-BP evaluation board
 * with a GT64260 onboard.
 *
 * Author: Mark Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <asm/ppc_asm.h>
#include <asm/cache.h>
#include <asm/gt64260_defs.h>

#include <platforms/ev64260.h>

	.globl	ev64260_init
ev64260_init:
	li	r20,0
	li	r23,20

	/* Relocate galileo's regs */
        addis	r25,0,GT64260_INTERNAL_SPACE_DEFAULT_ADDR@h
        ori	r25,r25,GT64260_INTERNAL_SPACE_DECODE
        lwbrx	r26,0,(r25)
	lis	r24,0xffff
	and	r26,r26,r24
	addis	r24,0,EV64260_BRIDGE_REG_BASE@h
	srw	r24,r24,r23
	or	r26,r26,r24
        stwbrx  r26,0,(r25)
	sync

	/* Wait for write to take effect */
        addis	r25,0,EV64260_BRIDGE_REG_BASE@h
	ori	r25,r25,GT64260_INTERNAL_SPACE_DECODE
1:	lwbrx	r24,0,(r25)
	cmpw	r24,r26
	bne	1b

        /* Change CS2 (UARTS on device module) window */
        addis	r25,0,EV64260_BRIDGE_REG_BASE@h
        ori	r25,r25,GT64260_CPU_CS_DECODE_2_BOT
	addis	r26,0,EV64260_UART_BASE@h
	srw	r26,r26,r23
        stwbrx  r26,0,(r25)
	sync

        addis	r25,0,EV64260_BRIDGE_REG_BASE@h
        ori	r25,r25,GT64260_CPU_CS_DECODE_2_TOP
	addis	r26,0,EV64260_UART_END@h
	srw	r26,r26,r23
        stwbrx  r26,0,(r25)
	sync

        blr
