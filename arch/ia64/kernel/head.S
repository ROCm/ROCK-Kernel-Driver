/*
 * Here is where the ball gets rolling as far as the kernel is concerned.
 * When control is transferred to _start, the bootload has already
 * loaded us to the correct address.  All that's left to do here is
 * to set up the kernel's global pointer and jump to the kernel
 * entry point.
 *
 * Copyright (C) 1998-2001, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <Don.Dugger@intel.com>
 * Copyright (C) 2002 Fenghua Yu <fenghua.yu@intel.com>
 *   -Optimize __ia64_save_fpu() and __ia64_load_fpu() for Itanium 2.
 */

#include <linux/config.h>

#include <asm/asmmacro.h>
#include <asm/fpu.h>
#include <asm/kregs.h>
#include <asm/mmu_context.h>
#include <asm/offsets.h>
#include <asm/pal.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>

	.section __special_page_section,"ax"

	.global empty_zero_page
empty_zero_page:
	.skip PAGE_SIZE

	.global swapper_pg_dir
swapper_pg_dir:
	.skip PAGE_SIZE

	.rodata
halt_msg:
	stringz "Halting kernel\n"

	.text

	.global start_ap

	/*
	 * Start the kernel.  When the bootloader passes control to _start(), r28
	 * points to the address of the boot parameter area.  Execution reaches
	 * here in physical mode.
	 */
GLOBAL_ENTRY(_start)
start_ap:
	.prologue
	.save rp, r4		// terminate unwind chain with a NULL rp
	mov r4=r0
	.body

	/*
	 * Initialize the region register for region 7 and install a translation register
	 * that maps the kernel's text and data:
	 */
	rsm psr.i | psr.ic
	mov r16=((ia64_rid(IA64_REGION_ID_KERNEL, PAGE_OFFSET) << 8) | (IA64_GRANULE_SHIFT << 2))
	;;
	srlz.i
	mov r18=KERNEL_TR_PAGE_SHIFT<<2
	movl r17=KERNEL_START
	;;
	mov rr[r17]=r16
	mov cr.itir=r18
	mov cr.ifa=r17
	mov r16=IA64_TR_KERNEL
	movl r18=((1 << KERNEL_TR_PAGE_SHIFT) | PAGE_KERNEL)
	;;
	srlz.i
	;;
	itr.i itr[r16]=r18
	;;
	itr.d dtr[r16]=r18
	;;
	srlz.i

	/*
	 * Switch into virtual mode:
	 */
	movl r16=(IA64_PSR_IT|IA64_PSR_IC|IA64_PSR_DT|IA64_PSR_RT|IA64_PSR_DFH|IA64_PSR_BN \
		  |IA64_PSR_DI)
	;;
	mov cr.ipsr=r16
	movl r17=1f
	;;
	mov cr.iip=r17
	mov cr.ifs=r0
	;;
	rfi
	;;
1:	// now we are in virtual mode

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
	mov r3=(6<<8) | (IA64_GRANULE_SHIFT<<2)
	movl r2=6<<61
	;;
	mov rr[r2]=r3
	;;
	srlz.i
	;;
#endif

#define isAP	p2	// are we an Application Processor?
#define isBP	p3	// are we the Bootstrap Processor?

#ifdef CONFIG_SMP
	/*
	 * Find the init_task for the currently booting CPU.  At poweron, and in
	 * UP mode, task_for_booting_cpu is NULL.
	 */
	movl r3=task_for_booting_cpu
 	;;
	ld8 r3=[r3]
	movl r2=init_thread_union
	;;
	cmp.eq isBP,isAP=r3,r0
	;;
(isAP)	mov r2=r3
#else
	movl r2=init_thread_union
	cmp.eq isBP,isAP=r0,r0
#endif
	mov r16=KERNEL_TR_PAGE_NUM
	;;

	// load the "current" pointer (r13) and ar.k6 with the current task
	mov IA64_KR(CURRENT)=r2		// virtual address
	// initialize k4 to a safe value (64-128MB is mapped by TR_KERNEL)
	mov IA64_KR(CURRENT_STACK)=r16
	mov r13=r2
	/*
	 * Reserve space at the top of the stack for "struct pt_regs".  Kernel threads
	 * don't store interesting values in that structure, but the space still needs
	 * to be there because time-critical stuff such as the context switching can
	 * be implemented more efficiently (for example, __switch_to()
	 * always sets the psr.dfh bit of the task it is switching to).
	 */
	addl r12=IA64_STK_OFFSET-IA64_PT_REGS_SIZE-16,r2
	addl r2=IA64_RBS_OFFSET,r2	// initialize the RSE
	mov ar.rsc=0		// place RSE in enforced lazy mode
	;;
	loadrs			// clear the dirty partition
	;;
	mov ar.bspstore=r2	// establish the new RSE stack
	;;
	mov ar.rsc=0x3		// place RSE in eager mode

(isBP)	dep r28=-1,r28,61,3	// make address virtual
(isBP)	movl r2=ia64_boot_param
	;;
(isBP)	st8 [r2]=r28		// save the address of the boot param area passed by the bootloader

#ifdef CONFIG_IA64_EARLY_PRINTK
	.rodata
alive_msg:
	stringz "I'm alive and well\n"
alive_msg_end:
	.previous

	alloc r2=ar.pfs,0,0,2,0
	movl out0=alive_msg
	movl out1=alive_msg_end-alive_msg-1
	;;
	br.call.sptk.many rp=early_printk
1:	// force new bundle
#endif /* CONFIG_IA64_EARLY_PRINTK */

#ifdef CONFIG_SMP
(isAP)	br.call.sptk.many rp=start_secondary
.ret0:
(isAP)	br.cond.sptk self
#endif

	// This is executed by the bootstrap processor (bsp) only:

#ifdef CONFIG_IA64_FW_EMU
	// initialize PAL & SAL emulator:
	br.call.sptk.many rp=sys_fw_init
.ret1:
#endif
	br.call.sptk.many rp=start_kernel
.ret2:	addl r3=@ltoff(halt_msg),gp
	;;
	alloc r2=ar.pfs,8,0,2,0
	;;
	ld8 out0=[r3]
	br.call.sptk.many b0=console_print
self:	br.sptk.many self		// endless loop
END(_start)

GLOBAL_ENTRY(ia64_save_debug_regs)
	alloc r16=ar.pfs,1,0,0,0
	mov r20=ar.lc			// preserve ar.lc
	mov ar.lc=IA64_NUM_DBG_REGS-1
	mov r18=0
	add r19=IA64_NUM_DBG_REGS*8,in0
	;;
1:	mov r16=dbr[r18]
#ifdef CONFIG_ITANIUM
	;;
	srlz.d
#endif
	mov r17=ibr[r18]
	add r18=1,r18
	;;
	st8.nta [in0]=r16,8
	st8.nta [r19]=r17,8
	br.cloop.sptk.many 1b
	;;
	mov ar.lc=r20			// restore ar.lc
	br.ret.sptk.many rp
END(ia64_save_debug_regs)

GLOBAL_ENTRY(ia64_load_debug_regs)
	alloc r16=ar.pfs,1,0,0,0
	lfetch.nta [in0]
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
#ifdef CONFIG_ITANIUM
	;;
	srlz.d				// Errata 132 (NoFix status)
#endif
	mov ibr[r18]=r17
	br.cloop.sptk.many 1b
	;;
	mov ar.lc=r20			// restore ar.lc
	br.ret.sptk.many rp
END(ia64_load_debug_regs)

GLOBAL_ENTRY(__ia64_save_fpu)
	alloc r2=ar.pfs,1,4,0,0
	adds loc0=96*16-16,in0
	adds loc1=96*16-16-128,in0
	;;
	stf.spill.nta [loc0]=f127,-256
	stf.spill.nta [loc1]=f119,-256
	;;
	stf.spill.nta [loc0]=f111,-256
	stf.spill.nta [loc1]=f103,-256
	;;
	stf.spill.nta [loc0]=f95,-256
	stf.spill.nta [loc1]=f87,-256
	;;
	stf.spill.nta [loc0]=f79,-256
	stf.spill.nta [loc1]=f71,-256
	;;
	stf.spill.nta [loc0]=f63,-256
	stf.spill.nta [loc1]=f55,-256
	adds loc2=96*16-32,in0
	;;
	stf.spill.nta [loc0]=f47,-256
	stf.spill.nta [loc1]=f39,-256
	adds loc3=96*16-32-128,in0
	;;
	stf.spill.nta [loc2]=f126,-256
	stf.spill.nta [loc3]=f118,-256
	;;
	stf.spill.nta [loc2]=f110,-256
	stf.spill.nta [loc3]=f102,-256
	;;
	stf.spill.nta [loc2]=f94,-256
	stf.spill.nta [loc3]=f86,-256
	;;
	stf.spill.nta [loc2]=f78,-256
	stf.spill.nta [loc3]=f70,-256
	;;
	stf.spill.nta [loc2]=f62,-256
	stf.spill.nta [loc3]=f54,-256
	adds loc0=96*16-48,in0
	;;
	stf.spill.nta [loc2]=f46,-256
	stf.spill.nta [loc3]=f38,-256
	adds loc1=96*16-48-128,in0
	;;
	stf.spill.nta [loc0]=f125,-256
	stf.spill.nta [loc1]=f117,-256
	;;
	stf.spill.nta [loc0]=f109,-256
	stf.spill.nta [loc1]=f101,-256
	;;
	stf.spill.nta [loc0]=f93,-256
	stf.spill.nta [loc1]=f85,-256
	;;
	stf.spill.nta [loc0]=f77,-256
	stf.spill.nta [loc1]=f69,-256
	;;
	stf.spill.nta [loc0]=f61,-256
	stf.spill.nta [loc1]=f53,-256
	adds loc2=96*16-64,in0
	;;
	stf.spill.nta [loc0]=f45,-256
	stf.spill.nta [loc1]=f37,-256
	adds loc3=96*16-64-128,in0
	;;
	stf.spill.nta [loc2]=f124,-256
	stf.spill.nta [loc3]=f116,-256
	;;
	stf.spill.nta [loc2]=f108,-256
	stf.spill.nta [loc3]=f100,-256
	;;
	stf.spill.nta [loc2]=f92,-256
	stf.spill.nta [loc3]=f84,-256
	;;
	stf.spill.nta [loc2]=f76,-256
	stf.spill.nta [loc3]=f68,-256
	;;
	stf.spill.nta [loc2]=f60,-256
	stf.spill.nta [loc3]=f52,-256
	adds loc0=96*16-80,in0
	;;
	stf.spill.nta [loc2]=f44,-256
	stf.spill.nta [loc3]=f36,-256
	adds loc1=96*16-80-128,in0
	;;
	stf.spill.nta [loc0]=f123,-256
	stf.spill.nta [loc1]=f115,-256
	;;
	stf.spill.nta [loc0]=f107,-256
	stf.spill.nta [loc1]=f99,-256
	;;
	stf.spill.nta [loc0]=f91,-256
	stf.spill.nta [loc1]=f83,-256
	;;
	stf.spill.nta [loc0]=f75,-256
	stf.spill.nta [loc1]=f67,-256
	;;
	stf.spill.nta [loc0]=f59,-256
	stf.spill.nta [loc1]=f51,-256
	adds loc2=96*16-96,in0
	;;
	stf.spill.nta [loc0]=f43,-256
	stf.spill.nta [loc1]=f35,-256
	adds loc3=96*16-96-128,in0
	;;
	stf.spill.nta [loc2]=f122,-256
	stf.spill.nta [loc3]=f114,-256
	;;
	stf.spill.nta [loc2]=f106,-256
	stf.spill.nta [loc3]=f98,-256
	;;
	stf.spill.nta [loc2]=f90,-256
	stf.spill.nta [loc3]=f82,-256
	;;
	stf.spill.nta [loc2]=f74,-256
	stf.spill.nta [loc3]=f66,-256
	;;
	stf.spill.nta [loc2]=f58,-256
	stf.spill.nta [loc3]=f50,-256
	adds loc0=96*16-112,in0
	;;
	stf.spill.nta [loc2]=f42,-256
	stf.spill.nta [loc3]=f34,-256
	adds loc1=96*16-112-128,in0
	;;
	stf.spill.nta [loc0]=f121,-256
	stf.spill.nta [loc1]=f113,-256
	;;
	stf.spill.nta [loc0]=f105,-256
	stf.spill.nta [loc1]=f97,-256
	;;
	stf.spill.nta [loc0]=f89,-256
	stf.spill.nta [loc1]=f81,-256
	;;
	stf.spill.nta [loc0]=f73,-256
	stf.spill.nta [loc1]=f65,-256
	;;
	stf.spill.nta [loc0]=f57,-256
	stf.spill.nta [loc1]=f49,-256
	adds loc2=96*16-128,in0
	;;
	stf.spill.nta [loc0]=f41,-256
	stf.spill.nta [loc1]=f33,-256
	adds loc3=96*16-128-128,in0
	;;
	stf.spill.nta [loc2]=f120,-256
	stf.spill.nta [loc3]=f112,-256
	;;
	stf.spill.nta [loc2]=f104,-256
	stf.spill.nta [loc3]=f96,-256
	;;
	stf.spill.nta [loc2]=f88,-256
	stf.spill.nta [loc3]=f80,-256
	;;
	stf.spill.nta [loc2]=f72,-256
	stf.spill.nta [loc3]=f64,-256
	;;
	stf.spill.nta [loc2]=f56,-256
	stf.spill.nta [loc3]=f48,-256
	;;
	stf.spill.nta [loc2]=f40
	stf.spill.nta [loc3]=f32
	br.ret.sptk.many rp
END(__ia64_save_fpu)

GLOBAL_ENTRY(__ia64_load_fpu)
	alloc r2=ar.pfs,1,2,0,0
	adds r3=128,in0
	adds r14=256,in0
	adds r15=384,in0
	mov loc0=512
	mov loc1=-1024+16
	;;
	ldf.fill.nta f32=[in0],loc0
	ldf.fill.nta f40=[ r3],loc0
	ldf.fill.nta f48=[r14],loc0
	ldf.fill.nta f56=[r15],loc0
	;;
	ldf.fill.nta f64=[in0],loc0
	ldf.fill.nta f72=[ r3],loc0
	ldf.fill.nta f80=[r14],loc0
	ldf.fill.nta f88=[r15],loc0
	;;
	ldf.fill.nta f96=[in0],loc1
	ldf.fill.nta f104=[ r3],loc1
	ldf.fill.nta f112=[r14],loc1
	ldf.fill.nta f120=[r15],loc1
	;;
	ldf.fill.nta f33=[in0],loc0
	ldf.fill.nta f41=[ r3],loc0
	ldf.fill.nta f49=[r14],loc0
	ldf.fill.nta f57=[r15],loc0
	;;
	ldf.fill.nta f65=[in0],loc0
	ldf.fill.nta f73=[ r3],loc0
	ldf.fill.nta f81=[r14],loc0
	ldf.fill.nta f89=[r15],loc0
	;;
	ldf.fill.nta f97=[in0],loc1
	ldf.fill.nta f105=[ r3],loc1
	ldf.fill.nta f113=[r14],loc1
	ldf.fill.nta f121=[r15],loc1
	;;
	ldf.fill.nta f34=[in0],loc0
	ldf.fill.nta f42=[ r3],loc0
	ldf.fill.nta f50=[r14],loc0
	ldf.fill.nta f58=[r15],loc0
	;;
	ldf.fill.nta f66=[in0],loc0
	ldf.fill.nta f74=[ r3],loc0
	ldf.fill.nta f82=[r14],loc0
	ldf.fill.nta f90=[r15],loc0
	;;
	ldf.fill.nta f98=[in0],loc1
	ldf.fill.nta f106=[ r3],loc1
	ldf.fill.nta f114=[r14],loc1
	ldf.fill.nta f122=[r15],loc1
	;;
	ldf.fill.nta f35=[in0],loc0
	ldf.fill.nta f43=[ r3],loc0
	ldf.fill.nta f51=[r14],loc0
	ldf.fill.nta f59=[r15],loc0
	;;
	ldf.fill.nta f67=[in0],loc0
	ldf.fill.nta f75=[ r3],loc0
	ldf.fill.nta f83=[r14],loc0
	ldf.fill.nta f91=[r15],loc0
	;;
	ldf.fill.nta f99=[in0],loc1
	ldf.fill.nta f107=[ r3],loc1
	ldf.fill.nta f115=[r14],loc1
	ldf.fill.nta f123=[r15],loc1
	;;
	ldf.fill.nta f36=[in0],loc0
	ldf.fill.nta f44=[ r3],loc0
	ldf.fill.nta f52=[r14],loc0
	ldf.fill.nta f60=[r15],loc0
	;;
	ldf.fill.nta f68=[in0],loc0
	ldf.fill.nta f76=[ r3],loc0
	ldf.fill.nta f84=[r14],loc0
	ldf.fill.nta f92=[r15],loc0
	;;
	ldf.fill.nta f100=[in0],loc1
	ldf.fill.nta f108=[ r3],loc1
	ldf.fill.nta f116=[r14],loc1
	ldf.fill.nta f124=[r15],loc1
	;;
	ldf.fill.nta f37=[in0],loc0
	ldf.fill.nta f45=[ r3],loc0
	ldf.fill.nta f53=[r14],loc0
	ldf.fill.nta f61=[r15],loc0
	;;
	ldf.fill.nta f69=[in0],loc0
	ldf.fill.nta f77=[ r3],loc0
	ldf.fill.nta f85=[r14],loc0
	ldf.fill.nta f93=[r15],loc0
	;;
	ldf.fill.nta f101=[in0],loc1
	ldf.fill.nta f109=[ r3],loc1
	ldf.fill.nta f117=[r14],loc1
	ldf.fill.nta f125=[r15],loc1
	;;
	ldf.fill.nta f38 =[in0],loc0
	ldf.fill.nta f46 =[ r3],loc0
	ldf.fill.nta f54 =[r14],loc0
	ldf.fill.nta f62 =[r15],loc0
	;;
	ldf.fill.nta f70 =[in0],loc0
	ldf.fill.nta f78 =[ r3],loc0
	ldf.fill.nta f86 =[r14],loc0
	ldf.fill.nta f94 =[r15],loc0
	;;
	ldf.fill.nta f102=[in0],loc1
	ldf.fill.nta f110=[ r3],loc1
	ldf.fill.nta f118=[r14],loc1
	ldf.fill.nta f126=[r15],loc1
	;;
	ldf.fill.nta f39 =[in0],loc0
	ldf.fill.nta f47 =[ r3],loc0
	ldf.fill.nta f55 =[r14],loc0
	ldf.fill.nta f63 =[r15],loc0
	;;
	ldf.fill.nta f71 =[in0],loc0
	ldf.fill.nta f79 =[ r3],loc0
	ldf.fill.nta f87 =[r14],loc0
	ldf.fill.nta f95 =[r15],loc0
	;;
	ldf.fill.nta f103=[in0]
	ldf.fill.nta f111=[ r3]
	ldf.fill.nta f119=[r14]
	ldf.fill.nta f127=[r15]
	br.ret.sptk.many rp
END(__ia64_load_fpu)

GLOBAL_ENTRY(__ia64_init_fpu)
	stf.spill [sp]=f0		// M3
	mov	 f32=f0			// F
	nop.b	 0

	ldfps	 f33,f34=[sp]		// M0
	ldfps	 f35,f36=[sp]		// M1
	mov      f37=f0			// F
	;;

	setf.s	 f38=r0			// M2
	setf.s	 f39=r0			// M3
	mov      f40=f0			// F

	ldfps	 f41,f42=[sp]		// M0
	ldfps	 f43,f44=[sp]		// M1
	mov      f45=f0			// F

	setf.s	 f46=r0			// M2
	setf.s	 f47=r0			// M3
	mov      f48=f0			// F

	ldfps	 f49,f50=[sp]		// M0
	ldfps	 f51,f52=[sp]		// M1
	mov      f53=f0			// F

	setf.s	 f54=r0			// M2
	setf.s	 f55=r0			// M3
	mov      f56=f0			// F

	ldfps	 f57,f58=[sp]		// M0
	ldfps	 f59,f60=[sp]		// M1
	mov      f61=f0			// F

	setf.s	 f62=r0			// M2
	setf.s	 f63=r0			// M3
	mov      f64=f0			// F

	ldfps	 f65,f66=[sp]		// M0
	ldfps	 f67,f68=[sp]		// M1
	mov      f69=f0			// F

	setf.s	 f70=r0			// M2
	setf.s	 f71=r0			// M3
	mov      f72=f0			// F

	ldfps	 f73,f74=[sp]		// M0
	ldfps	 f75,f76=[sp]		// M1
	mov      f77=f0			// F

	setf.s	 f78=r0			// M2
	setf.s	 f79=r0			// M3
	mov      f80=f0			// F

	ldfps	 f81,f82=[sp]		// M0
	ldfps	 f83,f84=[sp]		// M1
	mov      f85=f0			// F

	setf.s	 f86=r0			// M2
	setf.s	 f87=r0			// M3
	mov      f88=f0			// F

	/*
	 * When the instructions are cached, it would be faster to initialize
	 * the remaining registers with simply mov instructions (F-unit).
	 * This gets the time down to ~29 cycles.  However, this would use up
	 * 33 bundles, whereas continuing with the above pattern yields
	 * 10 bundles and ~30 cycles.
	 */

	ldfps	 f89,f90=[sp]		// M0
	ldfps	 f91,f92=[sp]		// M1
	mov      f93=f0			// F

	setf.s	 f94=r0			// M2
	setf.s	 f95=r0			// M3
	mov      f96=f0			// F

	ldfps	 f97,f98=[sp]		// M0
	ldfps	 f99,f100=[sp]		// M1
	mov      f101=f0		// F

	setf.s	 f102=r0		// M2
	setf.s	 f103=r0		// M3
	mov      f104=f0		// F

	ldfps	 f105,f106=[sp]		// M0
	ldfps	 f107,f108=[sp]		// M1
	mov      f109=f0		// F

	setf.s	 f110=r0		// M2
	setf.s	 f111=r0		// M3
	mov      f112=f0		// F

	ldfps	 f113,f114=[sp]		// M0
	ldfps	 f115,f116=[sp]		// M1
	mov      f117=f0		// F

	setf.s	 f118=r0		// M2
	setf.s	 f119=r0		// M3
	mov      f120=f0		// F

	ldfps	 f121,f122=[sp]		// M0
	ldfps	 f123,f124=[sp]		// M1
	mov      f125=f0		// F

	setf.s	 f126=r0		// M2
	setf.s	 f127=r0		// M3
	br.ret.sptk.many rp		// F
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
	br.ret.sptk.many rp
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
	br.ret.sptk.many rp;			\
 END(ia64_set_##reg)

SET_REG(b1);
SET_REG(b2);
SET_REG(b3);
SET_REG(b4);
SET_REG(b5);

#endif /* CONFIG_IA64_BRL_EMU */
