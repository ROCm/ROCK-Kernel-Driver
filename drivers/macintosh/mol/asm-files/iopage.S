/* 
 *   Creation Date: <97/07/26 18:23:02 samuel>
 *   Time-stamp: <2002/07/06 12:12:10 samuel>
 *   
 *	<iopage.S>
 *	
 *	IO low-level support
 *   
 *   Copyright (C) 2002 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

	// NOTE: This function is also called from the splitmode secondary interrupt 
	// handler (splitmode segment registers are resetup at return).

	
	///////////////////////////////////////////////////////////////////
	// check_io_page
	//
	//	xINST_OPCODE is always valid at this point 
	//
	// Examine if the page (which DAR points to) is an I/O page.
	// Safe to modify: r0,r2-r12

check_io_page:
	// Is this is an IO-page? IMPORTANT: we must *never* insert mappings
	// that are unreadable by supervisor (will cause a freeze right here).

	mfdsisr	r10				// r10 = DSISR
	rlwinm.	r3,r10,0,1,1			// BIT 1 set if no PTE (or BAT mapping)
	bnelr
	mfdar	r8

	mfmsr	r7
	ori	r3,r7,MSR_DR			// set MSR_DR
	mtmsr	r3
	isync

	rlwinm	r5,r8,0,0,19			// mask page index

	// XXX: TO BE FIXED. These instruction will _never_ cause an exceptions on a 
	// single processor system. However, on a SMP machine we *could* receive a 
	// tlbie invalidate broadcast. Thus we must implement a secondary 
	// interrupt handler to cover that case.

	lwz	r2,IOP_MAGIC(r5)
	lwz	r3,IOP_MAGIC2(r5)		// These should _never_ cause any exceptions
	lwz	r9,IOP_ME_PHYS(r5)		// r9 = physical addr of iopage

	mtmsr	r7				// restore MSR
	isync

	lis	r6,HI(IO_PAGE_MAGIC_1)		// check MAGIC 1
	ori	r6,r6,LO(IO_PAGE_MAGIC_1)
	cmplw	r6,r2
	bnelr

	lis	r7,HI(IO_PAGE_MAGIC_2)		// check MAGIC 2
	ori	r7,r7,LO(IO_PAGE_MAGIC_2)
	cmplw	r7,r3
	bnelr

	// Obtain translation info from the iopage:
	//
	// r4 = mphys_ioaddr = iop->mphys | (dar & 0xfff);
	// r5 = usr_data = iop->usr_data[ (dar & 0xfff) >> 3 ];
	//
	lwz	r4,IOP_MPHYS(r9)
	rlwimi	r4,r8,0,20,31			// insert page offset

	rlwinm	r7,r8,32-1,21,29		// grain = double word
	addi	r7,r7,IOP_USR_DATA		// usr_data[ (dar&0xfff) ]
	lwzx	r5,r9,r7			// r5 = usr_data
	
	rlwinm.	r2,r10,0,6,6			// was it a write? (r10=DSISR)
	bne	handle_write

	// r4 = mphys_ioaddr
	// r5 = usr_data
	MAC_EXIT( RVEC_IO_READ )

handle_write:
	// r4 = mphys_ioaddr
	// r5 = usr_data
	MAC_EXIT( RVEC_IO_WRITE )
