/* $Id: int-handler.S,v 1.4 1999/01/04 16:03:58 ralf Exp $
 *
 * SNI RM200 PCI specific interrupt handler code.
 *
 * Copyright (C) 1994 - 1997 by Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/sni.h>
#include <asm/stackframe.h>

/* The PCI ASIC has the nasty property that it may delay writes if it is busy.
   As a consequence from writes that have not graduated when we exit from the
   interrupt handler we might catch a spurious interrupt.  To avoid this we
   force the PCI ASIC to graduate all writes by executing a read from the
   PCI bus.  */

		.set	noreorder
		.set	noat
		.align	5
		NESTED(sni_rm200_pci_handle_int, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at

		/* Blinken light ...  */
		lb	t0, led_cache
		addiu	t0, 1
		sb	t0, led_cache
		sb	t0, PCIMT_CSLED			# write only register
		.data
led_cache:	.byte	0
		.text

		mfc0	t0, CP0_STATUS
		mfc0	t1, CP0_CAUSE
		and	t0, t1

		/* The following interrupt dispatch tests for hwint 1 /
		   EISA bridge first such that the timer interrupt get the
		   highest priority.  */
		 andi	t1, t0, 0x0800			# hardware interrupt 1
		bnez	t1, hwint1
		 andi	t1, t0, 0x4000			# hardware interrupt 4
		bnez	t1, hwint4

		 andi	t1, t0, 0x1000			# hardware interrupt 2
		bnez	t1, hwint2
		 andi	t1, t0, 0x2000			# hardware interrupt 3
		bnez	t1, hwint3
		 andi	t1, t0, 0x8000			# hardware interrupt 5
		bnez	t1, hwint5
		 andi	t1, t0, 0x0400			# hardware interrupt 0
		bnez	t1, hwint0
		 nop

		j	return				# spurious interrupt
		 nop

 ##############################################################################

swint0:		PANIC("swint0")
swint1:		PANIC("swint1")

 /* ------------------------------------------------------------------------ */

/* hwint1 deals with EISA and SCSI interrupts.  */
hwint1:		lbu	s0, PCIMT_CSITPEND

		andi	t1, s0, 0x20
		beqz	t1, 1f
		 andi	s1, s0, 0x40
		lbu	a0, PCIMT_INT_ACKNOWLEDGE	# IACK cycle
		xori	t0, a0, 0xff
		beqz	t0, 1f				# spurious interrupt?
		 nop
		jal	i8259_do_irq			# call real handler
		 move	a1, sp

1:		bnez	s1, 1f
		 li	a0, PCIMT_IRQ_SCSI
		jal	do_IRQ
		 move	a1, sp

1:		lui	t0, %hi(PCIMT_CSITPEND)
		j	ret_from_irq
		 lbu	zero, %lo(PCIMT_CSITPEND)(t0)

 /* ------------------------------------------------------------------------ */

/* hwint0 should deal with MP agent, ASIC PCI, EISA NMI and debug
   button interrupts.  */
hwint0:		PANIC("Received int0 but no handler yet ...\n")
1:		j	1b
		 nop

go_spurious:	j	spurious_interrupt		# we got fooled
		 nop

/* hwint4 is used for only the onboard PCnet 32.  */
hwint4:		mfc0	s0, CP0_STATUS
		ori	t0, s0, 0x4000
		xori	t0, 0x4000
		mtc0	t0, CP0_STATUS

		li	a0, PCIMT_IRQ_ETHERNET
		jal	do_IRQ
		 move	a1, sp

		mtc0	s0, CP0_STATUS

		j	ret_from_irq
		 nop

/* This interrupt was used for the com1 console on the first prototypes.  */
hwint2:		PANIC("hwint2 and no handler yet")

/* hwint3 should deal with the PCI A - D interrupts.  */
hwint3:		PANIC("hwint3 and no handler yet")

/* hwint5 is the r4k count / compare interrupt  */
hwint5:		PANIC("hwint5 and no handler yet")

		END(sni_rm200_pci_handle_int)
