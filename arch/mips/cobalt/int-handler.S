/*
 * arch/mips/cobalt/int-handler.S
 */
#include <asm/asm.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/cobalt.h>
#include <asm/irq.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * cobalt_handle_int: Interrupt handler for the twenty-seven board.
 */
		.align	5
		.set	noreorder
		NESTED(cobalt_handle_int, PT_SIZE, ra)
		.set	noat
		SAVE_ALL
		REG_S	sp,PT_OR2(sp)
		CLI
		.set	at
		mfc0	t0,CP0_CAUSE
		mfc0	t1,CP0_STATUS
		and	t0,t1
		xor	t1,t0
		mtc0	t1,CP0_STATUS	# mask all active ints
		/* Such a kind of cascade is optimal for R5000 */

		andi	t1,t0,STATUSF_IP2
		bnez	t1,ll_galileo_irq
		 andi	t1,t0,STATUSF_IP3
		bnez	t1,ll_ethernet0_irq
/*
 * This should be conditional, and not used for the cube-1, but
 * there is not a config flag that is useful.
 */
	
		 andi	t1,t0,STATUSF_IP4
		bnez	t1,ll_ethernet1_irq
/* #endif  */
		 andi	t1,t0,STATUSF_IP6
		bnez	t1,ll_via_irq
		 andi	t1,t0,STATUSF_IP5
		bnez	t1,ll_serial_irq
		 andi	t1,t0,STATUSF_IP7
		bnez	t1,ll_pci_irq
		  nop
		/* wrong alarm ... */
		j	spurious_interrupt
		 nop
		END(cobalt_handle_int)
	

		.align	5
		.set	reorder
ll_galileo_irq:	move	a0,sp
		INC_INTR_COUNT(s1,s2)
		jal	galileo_irq
		  nop
		DEC_INTR_COUNT(s1,s2)
		j	ret_from_irq
		  nop

		.align	5
		.set	reorder
ll_via_irq:	move	a0,sp
		INC_INTR_COUNT(s1,s2)
		jal	via_irq
		  nop
		DEC_INTR_COUNT(s1,s2)
		j	ret_from_irq
		  nop

		.align	5
		.set	reorder
ll_ethernet0_irq:
		INC_INTR_COUNT(s1,s2)
		mfc0	s0,CP0_STATUS		# mask interrupt
		ori	t0,s0,(STATUSF_IP3 | STATUSF_IP4)
		xori	t0,(STATUSF_IP3 | STATUSF_IP4)
		mtc0	t0,CP0_STATUS
		li	a0,4
		move	a1,sp
		jal     do_IRQ
		  nop
		mtc0	s0,CP0_STATUS
		DEC_INTR_COUNT(s1,s2)
		j	ret_from_irq
		  nop

		.align	5
		.set	reorder
ll_serial_irq:	li	a0,7
		INC_INTR_COUNT(s1,s2)
		move	a1,sp
		jal     do_IRQ
		  nop
		DEC_INTR_COUNT(s1,s2)
		j	ret_from_irq
		  nop


		.align	5
		.set	reorder
ll_ethernet1_irq:
		INC_INTR_COUNT(s1,s2)
		mfc0	s0,CP0_STATUS		# mask interrupt

		ori	t0,s0, (STATUSF_IP3 | STATUSF_IP4)
		xori	t0,(STATUSF_IP3 | STATUSF_IP4)

		mtc0	t0,CP0_STATUS
		li	a0,13
		move	a1,sp
		jal     do_IRQ
		  nop
		mtc0	s0,CP0_STATUS
		DEC_INTR_COUNT(s1,s2)
		j	ret_from_irq
		  nop

	#
	# This is pretty weird.  The "pci" interrupt on the hardware
	# skematic is from the PCI side of the galileo, so we would
	# only get interrupts here if WE write the control register
	# that normally enables the cpu to send interrupts to the PCI.
	#
	# If you want to interrupt a PCI card, look elsewhere.
	#
		.align	5
		.set	reorder
ll_pci_irq:	li	a0,7
		INC_INTR_COUNT(s1,s2)
		move	a1,sp
		jal     do_IRQ
		  nop
		DEC_INTR_COUNT(s1,s2)
		j	ret_from_irq
		  nop
