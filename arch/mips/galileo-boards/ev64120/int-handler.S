/*
 * int-handler.S
 *
 * Based on the cobalt handler.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * We check for the timer first, then check PCI ints A and D.
 * Then check for serial IRQ and fall through.
 */
		.align	5
		.set	reorder
		.set	noat
		NESTED(galileo_handle_int, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at

		mfc0	t0, CP0_CAUSE
		mfc0	t2, CP0_STATUS

		and	t0, t2

		.set	noreorder
		andi	t1, t0, STATUSF_IP4 /* int2 hardware line (timer) */
		andi	t2, t0, STATUSF_IP2 /* int0 hardware line */
		bnez	t1, ll_galileo_irq
		 andi	t1, t0, STATUSF_IP5 /* int3 hardware line */
		bnez	t2, ll_pci_intA
		 andi	t2, t0, STATUSF_IP6 /* int4 hardware line */
		bnez	t1, ll_pci_intD
		 andi	t1, t0, STATUSF_IP7 /* compare int */
		bnez	t2, ll_serial_irq
		 nop
		bnez	t1, ll_compare_irq
		 nop
		.set	reorder

		j	spurious_interrupt
		END(galileo_handle_int)

		.align	5
ll_galileo_irq:	li	a0, 4
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

		.align	5
ll_compare_irq:	li 	a0, 7
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

		.align	5
ll_pci_intA:	move	a0, sp
		jal	pci_intA
		j	ret_from_irq

#if 0
		.align	5
ll_pci_intB:	move 	a0, sp
		jal	pci_intB
		j	ret_from_irq

		.align	5
ll_pci_intC:	move 	a0, sp
		jal	pci_intC
		j	ret_from_irq
#endif

		.align	5
ll_pci_intD:	move 	a0, sp
		jal	pci_intD
		j	ret_from_irq

		.align	5
ll_serial_irq:	li	a0, 6
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
