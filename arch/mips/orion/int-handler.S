#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set    mips1
	.set    reorder
	.set    macro
	.set    noat
	.align	5

NESTED(orionIRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI				# Important: mark KERNEL mode !
	/*
	 * Get pending interrupts
	 */
	mfc0	t0,CP0_CAUSE		# get pending interrupts
	mfc0	t1,CP0_STATUS		# get enabled interrupts
	and	t0,t1			# isolate allowed ones
	andi	t0,0xff00		# isolate pending bits
	sll	t0,16			# shift the pending bits down
	beqz	t0,3f			# no pending intrs, then spurious
	nop				# delay slot
	
	/*
	 * Find irq with highest priority
	 * FIXME: This is slow - use binary search
	 */
	la	a0,7
1:	bltz	t0,2f			# found pending irq
	subu	a0,1
	sll	t0,1
	b	1b
	nop				# delay slot

call_do_IRQ:
2:	move	a1,sp
	jal	do_IRQ
	nop				# delay slot

	mfc0	t0,CP0_STATUS		# disable interrupts
	ori	t0,1
	xori	t0,1
	mtc0	t0,CP0_STATUS
	
	la      a1, ret_from_irq
	jr	a1
	
3:	j	spurious_interrupt
END(orionIRQ)

