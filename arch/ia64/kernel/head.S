/*
 * Here is where the ball gets rolling as far as the kernel is concerned.
 * When control is transferred to _start, the bootload has already
 * loaded us to the correct address.  All that's left to do here is
 * to set up the kernel's global pointer and jump to the kernel
 * entry point.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <Don.Dugger@intel.com>
 */

#include <linux/config.h>

#include <asm/asmmacro.h>
#include <asm/fpu.h>
#include <asm/pal.h>
#include <asm/offsets.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>

	.psr abi64
	.psr lsb
	.lsb

	.section __special_page_section,"ax"

	.global empty_zero_page
empty_zero_page:
	.skip PAGE_SIZE

	.global swapper_pg_dir
swapper_pg_dir:
	.skip PAGE_SIZE

	.global empty_bad_page
empty_bad_page:
	.skip PAGE_SIZE

	.global empty_bad_pte_table
empty_bad_pte_table:
	.skip PAGE_SIZE

	.global empty_bad_pmd_table
empty_bad_pmd_table:
	.skip PAGE_SIZE

	.rodata
halt_msg:
	stringz "Halting kernel\n"

	.text

GLOBAL_ENTRY(_start)
	UNW(.prologue)
	UNW(.save rp, r4)		// terminate unwind chain with a NULL rp
	UNW(mov r4=r0)
	UNW(.body)
	// set IVT entry point---can't access I/O ports without it
	movl r3=ia64_ivt
	;;
	mov cr.iva=r3
	movl r2=FPSR_DEFAULT
	;;
	srlz.i
	movl gp=__gp

	mov ar.fpsr=r2
	;;

#ifdef CONFIG_IA64_EARLY_PRINTK
	mov r3=(6<<8) | (28<<2)
	movl r2=6<<61
	;;
	mov rr[r2]=r3
	;;
	srlz.i
	;;
#endif

#define isAP	p2	// are we booting an Application Processor (not the BSP)?

	// Find the init_task for the currently booting CPU.  At poweron, and in
	// UP mode, cpu_now_booting is 0
	movl r3=cpu_now_booting
 	;;
	ld4 r3=[r3]
	movl r2=init_tasks
	;; 
	shladd r2=r3,3,r2
	;;
	ld8 r2=[r2]
	cmp4.ne isAP,p0=r3,r0	// p9 == true if this is an application processor (ap)
	;;			// RAW on r2
	extr r3=r2,0,61		// r3 == phys addr of task struct
	;;

	// load the "current" pointer (r13) and ar.k6 with the current task 
	mov r13=r2
	mov ar.k6=r3		// Physical address
	;;
	/*
	 * Reserve space at the top of the stack for "struct pt_regs".  Kernel threads
	 * don't store interesting values in that structure, but the space still needs
	 * to be there because time-critical stuff such as the context switching can
	 * be implemented more efficiently (for example, __switch_to()
	 * always sets the psr.dfh bit of the task it is switching to).
	 */
	addl r12=IA64_STK_OFFSET-IA64_PT_REGS_SIZE-16,r2
	addl r2=IA64_RBS_OFFSET,r2	// initialize the RSE
	mov ar.rsc=r0		// place RSE in enforced lazy mode
	;;
	mov ar.bspstore=r2	// establish the new RSE stack
	;;
	loadrs			// load zero bytes from the register stack
	;;
	mov ar.rsc=0x3		// place RSE in eager mode
	;;

#ifdef CONFIG_IA64_EARLY_PRINTK
	.rodata
alive_msg:
	stringz "I'm alive and well\n"
	.previous

	alloc r2=ar.pfs,0,0,2,0
	movl out0=alive_msg
	;;
	br.call.sptk.few rp=early_printk
1:	// force new bundle
#endif /* CONFIG_IA64_EARLY_PRINTK */

	alloc r2=ar.pfs,8,0,2,0
	;;
#ifdef CONFIG_SMP
(isAP)	br.call.sptk.few rp=smp_callin
.ret0:
(isAP)	br.cond.sptk.few self
#endif

#undef isAP

	// This is executed by the bootstrap processor (bsp) only:

#ifdef CONFIG_IA64_FW_EMU
	// initialize PAL & SAL emulator:
	br.call.sptk.few rp=sys_fw_init
.ret1:
#endif
	br.call.sptk.few rp=start_kernel
.ret2:	addl r2=@ltoff(halt_msg),gp
	;;
	ld8 out0=[r2]
	br.call.sptk.few b0=console_print
self:	br.sptk.few self		// endless loop
END(_start)

GLOBAL_ENTRY(ia64_save_debug_regs)
	alloc r16=ar.pfs,1,0,0,0
	mov r20=ar.lc			// preserve ar.lc
	mov ar.lc=IA64_NUM_DBG_REGS-1
	mov r18=0
	add r19=IA64_NUM_DBG_REGS*8,in0
	;;
1:	mov r16=dbr[r18]
#if defined(CONFIG_ITANIUM_ASTEP_SPECIFIC) || defined(CONFIG_ITANIUM_BSTEP_SPECIFIC) \
    || defined(CONFIG_ITANIUM_C0_SPECIFIC)
	;;
	srlz.d
#endif
	mov r17=ibr[r18]
	add r18=1,r18
	;;
	st8.nta [in0]=r16,8
	st8.nta [r19]=r17,8
	br.cloop.sptk.few 1b
	;;
	mov ar.lc=r20			// restore ar.lc
	br.ret.sptk.few rp
END(ia64_save_debug_regs)

GLOBAL_ENTRY(ia64_load_debug_regs)
	alloc r16=ar.pfs,1,0,0,0
#if !(defined(CONFIG_ITANIUM_ASTEP_SPECIFIC) \
   || defined(CONFIG_ITANIUM_B0_SPECIFIC) || defined(CONFIG_ITANIUM_B1_SPECIFIC))
	lfetch.nta [in0]
#endif
	mov r20=ar.lc			// preserve ar.lc
	add r19=IA64_NUM_DBG_REGS*8,in0
	mov ar.lc=IA64_NUM_DBG_REGS-1
	mov r18=-1
	;;
1:	ld8.nta r16=[in0],8
	ld8.nta r17=[r19],8
	add r18=1,r18
	;;
	mov dbr[r18]=r16
#if defined(CONFIG_ITANIUM_ASTEP_SPECIFIC) || defined(CONFIG_ITANIUM_BSTEP_SPECIFIC) \
    || defined(CONFIG_ITANIUM_C0_SPECIFIC)
	;;
	srlz.d
#endif
	mov ibr[r18]=r17
	br.cloop.sptk.few 1b
	;;
	mov ar.lc=r20			// restore ar.lc
	br.ret.sptk.few rp
END(ia64_load_debug_regs)

GLOBAL_ENTRY(__ia64_save_fpu)
	alloc r2=ar.pfs,1,0,0,0
	adds r3=16,in0
	;;
	stf.spill.nta [in0]=f32,32
	stf.spill.nta [ r3]=f33,32
	;;
	stf.spill.nta [in0]=f34,32
	stf.spill.nta [ r3]=f35,32
	;;
	stf.spill.nta [in0]=f36,32
	stf.spill.nta [ r3]=f37,32
	;;
	stf.spill.nta [in0]=f38,32
	stf.spill.nta [ r3]=f39,32
	;;
	stf.spill.nta [in0]=f40,32
	stf.spill.nta [ r3]=f41,32
	;;
	stf.spill.nta [in0]=f42,32
	stf.spill.nta [ r3]=f43,32
	;;
	stf.spill.nta [in0]=f44,32
	stf.spill.nta [ r3]=f45,32
	;;
	stf.spill.nta [in0]=f46,32
	stf.spill.nta [ r3]=f47,32
	;;
	stf.spill.nta [in0]=f48,32
	stf.spill.nta [ r3]=f49,32
	;;
	stf.spill.nta [in0]=f50,32
	stf.spill.nta [ r3]=f51,32
	;;
	stf.spill.nta [in0]=f52,32
	stf.spill.nta [ r3]=f53,32
	;;
	stf.spill.nta [in0]=f54,32
	stf.spill.nta [ r3]=f55,32
	;;
	stf.spill.nta [in0]=f56,32
	stf.spill.nta [ r3]=f57,32
	;;
	stf.spill.nta [in0]=f58,32
	stf.spill.nta [ r3]=f59,32
	;;
	stf.spill.nta [in0]=f60,32
	stf.spill.nta [ r3]=f61,32
	;;
	stf.spill.nta [in0]=f62,32
	stf.spill.nta [ r3]=f63,32
	;;
	stf.spill.nta [in0]=f64,32
	stf.spill.nta [ r3]=f65,32
	;;
	stf.spill.nta [in0]=f66,32
	stf.spill.nta [ r3]=f67,32
	;;
	stf.spill.nta [in0]=f68,32
	stf.spill.nta [ r3]=f69,32
	;;
	stf.spill.nta [in0]=f70,32
	stf.spill.nta [ r3]=f71,32
	;;
	stf.spill.nta [in0]=f72,32
	stf.spill.nta [ r3]=f73,32
	;;
	stf.spill.nta [in0]=f74,32
	stf.spill.nta [ r3]=f75,32
	;;
	stf.spill.nta [in0]=f76,32
	stf.spill.nta [ r3]=f77,32
	;;
	stf.spill.nta [in0]=f78,32
	stf.spill.nta [ r3]=f79,32
	;;
	stf.spill.nta [in0]=f80,32
	stf.spill.nta [ r3]=f81,32
	;;
	stf.spill.nta [in0]=f82,32
	stf.spill.nta [ r3]=f83,32
	;;
	stf.spill.nta [in0]=f84,32
	stf.spill.nta [ r3]=f85,32
	;;
	stf.spill.nta [in0]=f86,32
	stf.spill.nta [ r3]=f87,32
	;;
	stf.spill.nta [in0]=f88,32
	stf.spill.nta [ r3]=f89,32
	;;
	stf.spill.nta [in0]=f90,32
	stf.spill.nta [ r3]=f91,32
	;;
	stf.spill.nta [in0]=f92,32
	stf.spill.nta [ r3]=f93,32
	;;
	stf.spill.nta [in0]=f94,32
	stf.spill.nta [ r3]=f95,32
	;;
	stf.spill.nta [in0]=f96,32
	stf.spill.nta [ r3]=f97,32
	;;
	stf.spill.nta [in0]=f98,32
	stf.spill.nta [ r3]=f99,32
	;;
	stf.spill.nta [in0]=f100,32
	stf.spill.nta [ r3]=f101,32
	;;
	stf.spill.nta [in0]=f102,32
	stf.spill.nta [ r3]=f103,32
	;;
	stf.spill.nta [in0]=f104,32
	stf.spill.nta [ r3]=f105,32
	;;
	stf.spill.nta [in0]=f106,32
	stf.spill.nta [ r3]=f107,32
	;;
	stf.spill.nta [in0]=f108,32
	stf.spill.nta [ r3]=f109,32
	;;
	stf.spill.nta [in0]=f110,32
	stf.spill.nta [ r3]=f111,32
	;;
	stf.spill.nta [in0]=f112,32
	stf.spill.nta [ r3]=f113,32
	;;
	stf.spill.nta [in0]=f114,32
	stf.spill.nta [ r3]=f115,32
	;;
	stf.spill.nta [in0]=f116,32
	stf.spill.nta [ r3]=f117,32
	;;
	stf.spill.nta [in0]=f118,32
	stf.spill.nta [ r3]=f119,32
	;;
	stf.spill.nta [in0]=f120,32
	stf.spill.nta [ r3]=f121,32
	;;
	stf.spill.nta [in0]=f122,32
	stf.spill.nta [ r3]=f123,32
	;;
	stf.spill.nta [in0]=f124,32
	stf.spill.nta [ r3]=f125,32
	;;
	stf.spill.nta [in0]=f126,32
	stf.spill.nta [ r3]=f127,32
	br.ret.sptk.few rp
END(__ia64_save_fpu)

GLOBAL_ENTRY(__ia64_load_fpu)
	alloc r2=ar.pfs,1,0,0,0
	adds r3=16,in0
	;;
	ldf.fill.nta f32=[in0],32
	ldf.fill.nta f33=[ r3],32
	;;
	ldf.fill.nta f34=[in0],32
	ldf.fill.nta f35=[ r3],32
	;;
	ldf.fill.nta f36=[in0],32
	ldf.fill.nta f37=[ r3],32
	;;
	ldf.fill.nta f38=[in0],32
	ldf.fill.nta f39=[ r3],32
	;;
	ldf.fill.nta f40=[in0],32
	ldf.fill.nta f41=[ r3],32
	;;
	ldf.fill.nta f42=[in0],32
	ldf.fill.nta f43=[ r3],32
	;;
	ldf.fill.nta f44=[in0],32
	ldf.fill.nta f45=[ r3],32
	;;
	ldf.fill.nta f46=[in0],32
	ldf.fill.nta f47=[ r3],32
	;;
	ldf.fill.nta f48=[in0],32
	ldf.fill.nta f49=[ r3],32
	;;
	ldf.fill.nta f50=[in0],32
	ldf.fill.nta f51=[ r3],32
	;;
	ldf.fill.nta f52=[in0],32
	ldf.fill.nta f53=[ r3],32
	;;
	ldf.fill.nta f54=[in0],32
	ldf.fill.nta f55=[ r3],32
	;;
	ldf.fill.nta f56=[in0],32
	ldf.fill.nta f57=[ r3],32
	;;
	ldf.fill.nta f58=[in0],32
	ldf.fill.nta f59=[ r3],32
	;;
	ldf.fill.nta f60=[in0],32
	ldf.fill.nta f61=[ r3],32
	;;
	ldf.fill.nta f62=[in0],32
	ldf.fill.nta f63=[ r3],32
	;;
	ldf.fill.nta f64=[in0],32
	ldf.fill.nta f65=[ r3],32
	;;
	ldf.fill.nta f66=[in0],32
	ldf.fill.nta f67=[ r3],32
	;;
	ldf.fill.nta f68=[in0],32
	ldf.fill.nta f69=[ r3],32
	;;
	ldf.fill.nta f70=[in0],32
	ldf.fill.nta f71=[ r3],32
	;;
	ldf.fill.nta f72=[in0],32
	ldf.fill.nta f73=[ r3],32
	;;
	ldf.fill.nta f74=[in0],32
	ldf.fill.nta f75=[ r3],32
	;;
	ldf.fill.nta f76=[in0],32
	ldf.fill.nta f77=[ r3],32
	;;
	ldf.fill.nta f78=[in0],32
	ldf.fill.nta f79=[ r3],32
	;;
	ldf.fill.nta f80=[in0],32
	ldf.fill.nta f81=[ r3],32
	;;
	ldf.fill.nta f82=[in0],32
	ldf.fill.nta f83=[ r3],32
	;;
	ldf.fill.nta f84=[in0],32
	ldf.fill.nta f85=[ r3],32
	;;
	ldf.fill.nta f86=[in0],32
	ldf.fill.nta f87=[ r3],32
	;;
	ldf.fill.nta f88=[in0],32
	ldf.fill.nta f89=[ r3],32
	;;
	ldf.fill.nta f90=[in0],32
	ldf.fill.nta f91=[ r3],32
	;;
	ldf.fill.nta f92=[in0],32
	ldf.fill.nta f93=[ r3],32
	;;
	ldf.fill.nta f94=[in0],32
	ldf.fill.nta f95=[ r3],32
	;;
	ldf.fill.nta f96=[in0],32
	ldf.fill.nta f97=[ r3],32
	;;
	ldf.fill.nta f98=[in0],32
	ldf.fill.nta f99=[ r3],32
	;;
	ldf.fill.nta f100=[in0],32
	ldf.fill.nta f101=[ r3],32
	;;
	ldf.fill.nta f102=[in0],32
	ldf.fill.nta f103=[ r3],32
	;;
	ldf.fill.nta f104=[in0],32
	ldf.fill.nta f105=[ r3],32
	;;
	ldf.fill.nta f106=[in0],32
	ldf.fill.nta f107=[ r3],32
	;;
	ldf.fill.nta f108=[in0],32
	ldf.fill.nta f109=[ r3],32
	;;
	ldf.fill.nta f110=[in0],32
	ldf.fill.nta f111=[ r3],32
	;;
	ldf.fill.nta f112=[in0],32
	ldf.fill.nta f113=[ r3],32
	;;
	ldf.fill.nta f114=[in0],32
	ldf.fill.nta f115=[ r3],32
	;;
	ldf.fill.nta f116=[in0],32
	ldf.fill.nta f117=[ r3],32
	;;
	ldf.fill.nta f118=[in0],32
	ldf.fill.nta f119=[ r3],32
	;;
	ldf.fill.nta f120=[in0],32
	ldf.fill.nta f121=[ r3],32
	;;
	ldf.fill.nta f122=[in0],32
	ldf.fill.nta f123=[ r3],32
	;;
	ldf.fill.nta f124=[in0],32
	ldf.fill.nta f125=[ r3],32
	;;
	ldf.fill.nta f126=[in0],32
	ldf.fill.nta f127=[ r3],32
	br.ret.sptk.few rp
END(__ia64_load_fpu)

GLOBAL_ENTRY(__ia64_init_fpu)
	alloc r2=ar.pfs,0,0,0,0
	stf.spill [sp]=f0
	mov      f32=f0
	;;
	ldf.fill f33=[sp]
	ldf.fill f34=[sp]
	mov      f35=f0
	;;
	ldf.fill f36=[sp]
	ldf.fill f37=[sp]
	mov      f38=f0
	;;
	ldf.fill f39=[sp]
	ldf.fill f40=[sp]
	mov      f41=f0
	;;
	ldf.fill f42=[sp]
	ldf.fill f43=[sp]
	mov      f44=f0
	;;
	ldf.fill f45=[sp]
	ldf.fill f46=[sp]
	mov      f47=f0
	;;
	ldf.fill f48=[sp]
	ldf.fill f49=[sp]
	mov      f50=f0
	;;
	ldf.fill f51=[sp]
	ldf.fill f52=[sp]
	mov      f53=f0
	;;
	ldf.fill f54=[sp]
	ldf.fill f55=[sp]
	mov      f56=f0
	;;
	ldf.fill f57=[sp]
	ldf.fill f58=[sp]
	mov      f59=f0
	;;
	ldf.fill f60=[sp]
	ldf.fill f61=[sp]
	mov      f62=f0
	;;
	ldf.fill f63=[sp]
	ldf.fill f64=[sp]
	mov      f65=f0
	;;
	ldf.fill f66=[sp]
	ldf.fill f67=[sp]
	mov      f68=f0
	;;
	ldf.fill f69=[sp]
	ldf.fill f70=[sp]
	mov      f71=f0
	;;
	ldf.fill f72=[sp]
	ldf.fill f73=[sp]
	mov      f74=f0
	;;
	ldf.fill f75=[sp]
	ldf.fill f76=[sp]
	mov      f77=f0
	;;
	ldf.fill f78=[sp]
	ldf.fill f79=[sp]
	mov      f80=f0
	;;
	ldf.fill f81=[sp]
	ldf.fill f82=[sp]
	mov      f83=f0
	;;
	ldf.fill f84=[sp]
	ldf.fill f85=[sp]
	mov      f86=f0
	;;
	ldf.fill f87=[sp]
	ldf.fill f88=[sp]
	mov      f89=f0
	;;
	ldf.fill f90=[sp]
	ldf.fill f91=[sp]
	mov      f92=f0
	;;
	ldf.fill f93=[sp]
	ldf.fill f94=[sp]
	mov      f95=f0
	;;
	ldf.fill f96=[sp]
	ldf.fill f97=[sp]
	mov      f98=f0
	;;
	ldf.fill f99=[sp]
	ldf.fill f100=[sp]
	mov      f101=f0
	;;
	ldf.fill f102=[sp]
	ldf.fill f103=[sp]
	mov      f104=f0
	;;
	ldf.fill f105=[sp]
	ldf.fill f106=[sp]
	mov      f107=f0
	;;
	ldf.fill f108=[sp]
	ldf.fill f109=[sp]
	mov      f110=f0
	;;
	ldf.fill f111=[sp]
	ldf.fill f112=[sp]
	mov      f113=f0
	;;
	ldf.fill f114=[sp]
	ldf.fill f115=[sp]
	mov      f116=f0
	;;
	ldf.fill f117=[sp]
	ldf.fill f118=[sp]
	mov      f119=f0
	;;
	ldf.fill f120=[sp]
	ldf.fill f121=[sp]
	mov      f122=f0
	;;
	ldf.fill f123=[sp]
	ldf.fill f124=[sp]
	mov      f125=f0
	;;
	ldf.fill f126=[sp]
	mov      f127=f0
	br.ret.sptk.few rp
END(__ia64_init_fpu)

/*
 * Switch execution mode from virtual to physical or vice versa.
 *
 * Inputs:
 *	r16 = new psr to establish
 *
 * Note: RSE must already be in enforced lazy mode
 */
GLOBAL_ENTRY(ia64_switch_mode)
 {
	alloc r2=ar.pfs,0,0,0,0
	rsm psr.i | psr.ic		// disable interrupts and interrupt collection
	mov r15=ip
 }
	;;
 {
	flushrs				// must be first insn in group
	srlz.i
	shr.u r19=r15,61		// r19 <- top 3 bits of current IP
 }
	;;
	mov cr.ipsr=r16			// set new PSR
	add r3=1f-ia64_switch_mode,r15
	xor r15=0x7,r19			// flip the region bits

	mov r17=ar.bsp
	mov r14=rp			// get return address into a general register

	// switch RSE backing store:
	;;
	dep r17=r15,r17,61,3		// make ar.bsp physical or virtual
	mov r18=ar.rnat			// save ar.rnat
	;;
	mov ar.bspstore=r17		// this steps on ar.rnat
	dep r3=r15,r3,61,3		// make rfi return address physical or virtual
	;;
	mov cr.iip=r3
	mov cr.ifs=r0
	dep sp=r15,sp,61,3		// make stack pointer physical or virtual
	;;
	mov ar.rnat=r18			// restore ar.rnat
	dep r14=r15,r14,61,3		// make function return address physical or virtual
	rfi				// must be last insn in group
	;;
1:	mov rp=r14
	br.ret.sptk.few rp
END(ia64_switch_mode)

#ifdef CONFIG_IA64_BRL_EMU

/*
 *  Assembly routines used by brl_emu.c to set preserved register state.
 */

#define SET_REG(reg)				\
 GLOBAL_ENTRY(ia64_set_##reg);			\
	alloc r16=ar.pfs,1,0,0,0;		\
	mov reg=r32;				\
	;;					\
	br.ret.sptk rp;				\
 END(ia64_set_##reg)

SET_REG(b1);
SET_REG(b2);
SET_REG(b3);
SET_REG(b4);
SET_REG(b5);

#endif /* CONFIG_IA64_BRL_EMU */

#ifdef CONFIG_SMP

	/*
	 * This routine handles spinlock contention.  It uses a simple exponential backoff
	 * algorithm to reduce unnecessary bus traffic.  The initial delay is selected from
	 * the low-order bits of the cycle counter (a cheap "randomizer").  I'm sure this
	 * could use additional tuning, especially on systems with a large number of CPUs.
	 * Also, I think the maximum delay should be made a function of the number of CPUs in
	 * the system. --davidm 00/08/05
	 *
	 * WARNING: This is not a normal procedure.  It gets called from C code without
	 * the compiler knowing about it.  Thus, we must not use any scratch registers
	 * beyond those that were declared "clobbered" at the call-site (see spin_lock()
	 * macro).  We may not even use the stacked registers, because that could overwrite
	 * output registers.  Similarly, we can't use the scratch stack area as it may be
	 * in use, too.
	 *
	 * Inputs:
	 *	ar.ccv = 0 (and available for use)
	 *	r28 = available for use
	 *	r29 = available for use
	 *	r30 = non-zero (and available for use)
	 *	r31 = address of lock we're trying to acquire
	 *	p15 = available for use
	 */

#	define delay	r28
#	define timeout	r29
#	define tmp	r30

GLOBAL_ENTRY(ia64_spinlock_contention)
	mov tmp=ar.itc
	;;
	and delay=0x3f,tmp
	;;

.retry:	add timeout=tmp,delay
	shl delay=delay,1
	;;
	dep delay=delay,r0,0,13	// limit delay to 8192 cycles
	;;
	// delay a little...
.wait:	sub tmp=tmp,timeout
	or delay=0xf,delay	// make sure delay is non-zero (otherwise we get stuck with 0)
	;;
	cmp.lt p15,p0=tmp,r0
	mov tmp=ar.itc
(p15)	br.cond.sptk .wait
	;;
	ld4 tmp=[r31]
	;;
	cmp.ne p15,p0=tmp,r0
	mov tmp=ar.itc
(p15)	br.cond.sptk.few .retry	// lock is still busy
	;;
	// try acquiring lock (we know ar.ccv is still zero!):
	mov tmp=1
	;;
	IA64_SEMFIX_INSN
	cmpxchg4.acq tmp=[r31],tmp,ar.ccv
	;;
	cmp.eq p15,p0=tmp,r0

	mov tmp=ar.itc
(p15)	br.ret.sptk.many b7	// got lock -> return
	br .retry		// still no luck, retry

END(ia64_spinlock_contention)

#endif
