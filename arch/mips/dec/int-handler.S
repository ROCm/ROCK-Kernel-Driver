/*
 * arch/mips/dec/int-handler.S
 *
 * Copyright (C) 1995, 1996, 1997 Paul M. Antoine and Harald Koerfgen
 *
 * Written by Ralf Baechle and Andreas Busse, modified for DECStation
 * support by Paul Antoine and Harald Koerfgen.
 *
 * completly rewritten:
 * Copyright (C) 1998 Harald Koerfgen
 *
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/addrspace.h>

#include <asm/dec/interrupts.h>


		.text
		.set	noreorder
/*
 * decstation_handle_int: Interrupt handler for DECStations
 *
 * FIXME: Detection of spurious interrupts not yet implemented!
 *
 * We follow the model in the Indy interrupt code by David Miller, where he
 * says: a lot of complication here is taken away because:
 *
 * 1) We handle one interrupt and return, sitting in a loop
 *    and moving across all the pending IRQ bits in the cause
 *    register is _NOT_ the answer, the common case is one
 *    pending IRQ so optimize in that direction.
 *
 * 2) We need not check against bits in the status register
 *    IRQ mask, that would make this routine slow as hell.
 *
 * 3) Linux only thinks in terms of all IRQs on or all IRQs
 *    off, nothing in between like BSD spl() brain-damage.
 *
 * Furthermore, the IRQs on the DECStations look basically (barring
 * software IRQs which we don't use at all) like...
 *
 * DS2100/3100's, aka kn01, aka Pmax:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        SCSI
 *             3        Lance Ethernet
 *             4        DZ11 serial
 *             5        RTC
 *             6        Memory Controller
 *             7        FPU
 *
 * DS5000/200, aka kn02, aka 3max:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        TurboChannel
 *             3        RTC
 *             4        Reserved
 *             5        Memory Controller
 *             6        Reserved
 *             7        FPU
 *
 * DS5000/1xx's, aka kn02ba, aka 3min:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        TurboChannel Slot 0
 *             3        TurboChannel Slot 1
 *             4        TurboChannel Slot 2
 *             5        TurboChannel Slot 3 (ASIC)
 *             6        Halt button
 *             7        FPU
 *
 * DS5000/2x's, aka kn02ca, aka maxine:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        Periodic Interrupt (100usec)
 *             3        RTC
 *             4        I/O write timeout
 *             5        TurboChannel (ASIC)
 *             6        Halt Keycode from Access.Bus keyboard (CTRL-ALT-ENTER)
 *             7        FPU
 *
 * DS5000/2xx's, aka kn03, aka 3maxplus:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        System Board (ASIC)
 *             3        RTC
 *             4        Reserved
 *             5        Memory
 *             6        Halt Button
 *             7        FPU
 *
 * We handle the IRQ according to _our_ priority.
 * Priority is:
 *
 * Highest ----     RTC
 *                  SCSI (if separate from TC)
 *                  Ethernet (if separate from TC)
 *		    Serial (if separate from TC)
 *                  TurboChannel (if there is one!)
 *                  Memory Controller (execept kmin)
 * Lowest  ----     Halt (if there is one!)
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 *
 */
		.align	5
		NESTED(decstation_handle_int, PT_SIZE, ra)
		.set	noat
		SAVE_ALL
		CLI				# TEST: interrupts should be off
		.set	at
		.set	noreorder

		/*
		 * Get pending Interrupts
		 */
		mfc0	t0,CP0_CAUSE		# get pending interrupts
		mfc0	t2,CP0_STATUS
		la	t1,cpu_mask_tbl
		and	t0,t2			# isolate allowed ones

		/* insert detection of spurious interrupts here */

		/*
		 * Find irq with highest priority
		 */
1:		lw	t2,(t1)
		move	t3,t0
		and	t3,t2
		beq	t3,zero,1b
		 addu	t1,PTRSIZE		# delay slot

		/*
		 * Do the low-level stuff
		 */
		lw	a0,%lo(cpu_irq_nr-cpu_mask_tbl-PTRSIZE)(t1)
		lw	t0,%lo(cpu_ivec_tbl-cpu_mask_tbl-PTRSIZE)(t1)
		bgez	a0, handle_it		# irq_nr >= 0?
						# irq_nr < 0: a0 contains an address
		nop
		jr	t0
		 nop				# delay slot

/*
 * Handle "IRQ Controller" Interrupts
 * Masked Interrupts are still visible and have to be masked "by hand".
 * %hi(KN02_CSR_ADDR) does not work so all addresses are hardcoded :-(.
 */
		EXPORT(kn02_io_int)
kn02_io_int:	lui	t0,0xbff0		# get interrupt status and mask
		lw	t0,(t0)
		la	t1,asic_mask_tbl
		move	t3,t0
		sll	t3,16			# shift interrupt status
		b	find_int
		 and	t0,t3			# mask out allowed ones

		EXPORT(kn03_io_int)
kn03_io_int:	lui	t2,0xbf84		# upper part of IOASIC Address
		lw	t0,0x0110(t2)		# get status: IOASIC isr
		lw	t3,0x0120(t2)		# get mask:   IOASIC isrm
		la	t1,asic_mask_tbl
		b	find_int
		 and	t0,t3			# mask out allowed ones

		EXPORT(kn02ba_io_int)
kn02ba_io_int:	lui	t2,0xbc04		
		lw	t0,0x0110(t2)		# IOASIC isr, works for maxine also
		lw	t3,0x0120(t2)		# IOASIC isrm
		la	t1,asic_mask_tbl
		and	t0,t3

		/*
		 * Find irq with highest priority
		 */
find_int:	lw	t2,(t1)
		move	t3,t0
		and	t3,t2
		beq	zero,t3,find_int
		 addu	t1,PTRSIZE		# delay slot

		/*
		 * Do the low-level stuff
		 */
		lw	a0,%lo(asic_irq_nr-asic_mask_tbl-PTRSIZE)(t1)
                nop

handle_it:	jal	do_IRQ
		 move	a1,sp
		j	ret_from_irq
		 nop
		
		END(decstation_handle_int)
/*
  * Interrupt routines common to all DECStations first.
 */
		EXPORT(dec_intr_fpu)
dec_intr_fpu:	PANIC("Unimplemented FPU interrupt handler")

/*
 * Halt interrupt
 */
		EXPORT(intr_halt)
intr_halt:	la	k0,0xbc000000
		jr	k0
		 nop

/*
 * Generic unimplemented interrupt routines - ivec_tbl is initialised to
 * point all interrupts here.  The table is then filled in by machine-specific
 * initialisation in dec_setup().
 */
		EXPORT(dec_intr_unimplemented)
dec_intr_unimplemented:
		mfc0	a1,CP0_CAUSE		# cheats way of printing an arg!
		nop				# to be sure...
		PANIC("Unimplemented cpu interrupt! CP0_CAUSE: 0x%x");

		EXPORT(asic_intr_unimplemented)
asic_intr_unimplemented:
		move	a1,t0			# cheats way of printing an arg!
		PANIC("Unimplemented asic interrupt! ASIC ISR: 0x%x");

/*
 * FIXME: This interrupt vector table is experimental.  It is initialised with 
 *	  *_intr_unimplemented and filled in with the addresses of
 *	  machine-specific interrupt routines in dec_setup()  Paul 10/5/97.
 *
 *	  The mask_tbls contain the interrupt masks which are used. It is 
 *	  initialised with all possible interrupt status bits set, so that 
 * 	  unused Interrupts are catched.				Harald
 */
		.data
		EXPORT(cpu_mask_tbl)
cpu_mask_tbl:
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000		# these two are unlikely 
		.word	0x00000000		# to be used
		.word	0x0000ff00		# End of list

		EXPORT(cpu_irq_nr)
cpu_irq_nr:
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000
		.word	0x00000000		# these two are unlikely 
		.word	0x00000000		# to be used
		.word	0x00ffffff		# End of list

		EXPORT(cpu_ivec_tbl)
cpu_ivec_tbl:
		PTR	dec_intr_unimplemented
		PTR	dec_intr_unimplemented
		PTR	dec_intr_unimplemented
		PTR	dec_intr_unimplemented
		PTR	dec_intr_unimplemented
		PTR	dec_intr_unimplemented
		PTR	dec_intr_unimplemented	# these two are unlikely
		PTR	dec_intr_unimplemented	# to be used
		PTR	dec_intr_unimplemented	# EOL

		EXPORT(asic_mask_tbl)
asic_mask_tbl:
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0xffffffff			# EOL

		EXPORT(asic_irq_nr)
asic_irq_nr:
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0
		.word	0xffffffff		# EOL


