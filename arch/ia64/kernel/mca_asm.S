// 
// assembly portion of the IA64 MCA handling
//
// Mods by cfleck to integrate into kernel build
// 00/03/15 davidm Added various stop bits to get a clean compile
//
// 00/03/29 cfleck Added code to save INIT handoff state in pt_regs format, switch to temp
//		   kstack, switch modes, jump to C INIT handler
//
#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/mca_asm.h>
#include <asm/mca.h>

/*
 * When we get an machine check, the kernel stack pointer is no longer
 * valid, so we need to set a new stack pointer.
 */
#define	MINSTATE_PHYS	/* Make sure stack access is physical for MINSTATE */ 

#include "minstate.h"
	
	.psr abi64
	.psr lsb
	.lsb

/*
 *	SAL_TO_OS_MCA_HANDOFF_STATE
 *		1. GR1 = OS GP
 *		2. GR8 = PAL_PROC physical address
 *		3. GR9 = SAL_PROC physical address
 *		4. GR10 = SAL GP (physical)
 *		5. GR11 = Rendez state
 *		6. GR12 = Return address to location within SAL_CHECK
 */
#define SAL_TO_OS_MCA_HANDOFF_STATE_SAVE(_tmp)		\
        movl	_tmp=ia64_sal_to_os_handoff_state;;	\
        st8	[_tmp]=r1,0x08;;			\
        st8	[_tmp]=r8,0x08;;			\
        st8	[_tmp]=r9,0x08;;			\
        st8	[_tmp]=r10,0x08;;			\
        st8	[_tmp]=r11,0x08;;			\
        st8	[_tmp]=r12,0x08;;

/*
 *	OS_MCA_TO_SAL_HANDOFF_STATE
 *		1. GR8 = OS_MCA status
 *		2. GR9 = SAL GP (physical)
 *		3. GR22 = New min state save area pointer
 */	
#define OS_MCA_TO_SAL_HANDOFF_STATE_RESTORE(_tmp)	\
        movl	_tmp=ia64_os_to_sal_handoff_state;;	\
	DATA_VA_TO_PA(_tmp);;				\
        ld8	r8=[_tmp],0x08;;			\
        ld8	r9=[_tmp],0x08;;			\
        ld8	r22=[_tmp],0x08;;

/*
 *	BRANCH
 *		Jump to the instruction referenced by
 *	"to_label".
 *		Branch is taken only if the predicate
 *	register "p" is true.
 *		"ip" is the address of the instruction
 *	located at "from_label".
 *		"temp" is a scratch register like r2
 *		"adjust" needed for HP compiler. 
 *	A screwup somewhere with constant arithmetic.
 */	
#define BRANCH(to_label, temp, p, adjust)		\
100:	(p)	mov		temp=ip;		\
		;;					\
	(p)	adds		temp=to_label-100b,temp;\
		;;					\
	(p)	adds		temp=adjust,temp;	\
		;;					\
	(p)	mov		b1=temp	;		\
	(p)	br		b1	

	.global ia64_os_mca_dispatch
	.global ia64_os_mca_dispatch_end
	.global ia64_sal_to_os_handoff_state
	.global	ia64_os_to_sal_handoff_state
	.global	ia64_os_mca_ucmc_handler
	.global	ia64_mca_proc_state_dump
	.global ia64_mca_proc_state_restore
	.global	ia64_mca_stack
	.global	ia64_mca_stackframe
	.global	ia64_mca_bspstore
	.global ia64_init_stack	
			
	.text
	.align 16
	
ia64_os_mca_dispatch:

#if defined(MCA_TEST)
	// Pretend that we are in interrupt context
	mov		r2=psr
	dep		r2=0, r2, PSR_IC, 2;
	mov		psr.l = r2
#endif	/* #if defined(MCA_TEST) */

	// Save the SAL to OS MCA handoff state as defined
	// by SAL SPEC 2.5
	// NOTE : The order in which the state gets saved
	//	  is dependent on the way the C-structure
	//	  for ia64_mca_sal_to_os_state_t has been
	//	  defined in include/asm/mca.h
	SAL_TO_OS_MCA_HANDOFF_STATE_SAVE(r2)

	// LOG PROCESSOR STATE INFO FROM HERE ON..
	;;
begin_os_mca_dump:
	BRANCH(ia64_os_mca_proc_state_dump, r2, p0, 0x0)
        ;;
ia64_os_mca_done_dump:

	// Setup new stack frame for OS_MCA handling 
        movl        r2=ia64_mca_bspstore		// local bspstore area location in r2
        movl        r3=ia64_mca_stackframe		// save stack frame to memory in r3
        rse_switch_context(r6,r3,r2);;                  // RSC management in this new context
        movl        r12=ia64_mca_stack;;

	// Enter virtual mode from physical mode
	VIRTUAL_MODE_ENTER(r2, r3, ia64_os_mca_virtual_begin, r4)
ia64_os_mca_virtual_begin:

	// call our handler
        movl		r2=ia64_mca_ucmc_handler;;
        mov		b6=r2;;
        br.call.sptk.few	b0=b6
.ret0:
	// Revert back to physical mode before going back to SAL
	PHYSICAL_MODE_ENTER(r2, r3, ia64_os_mca_virtual_end, r4)
ia64_os_mca_virtual_end:

#if defined(MCA_TEST)
	// Pretend that we are in interrupt context
	mov		r2=psr
	dep		r2=0, r2, PSR_IC, 2;
	mov		psr.l = r2
#endif	/* #if defined(MCA_TEST) */

	// restore the original stack frame here
        movl    r2=ia64_mca_stackframe               // restore stack frame from memory at r2
	;;
	DATA_VA_TO_PA(r2)
        movl    r4=IA64_PSR_MC
	;;
        rse_return_context(r4,r3,r2)                 // switch from interrupt context for RSE
	
	// let us restore all the registers from our PSI structure
        mov		r8=gp
	;;
begin_os_mca_restore:
	BRANCH(ia64_os_mca_proc_state_restore, r2, p0, 0x0)
        ;;

ia64_os_mca_done_restore:
	;;
#ifdef SOFTSDV
	VIRTUAL_MODE_ENTER(r2,r3, vmode_enter, r4)
vmode_enter:	
	br.ret.sptk.few		b0	
#else
	// branch back to SALE_CHECK
	OS_MCA_TO_SAL_HANDOFF_STATE_RESTORE(r2)
        ld8		r3=[r2];;              
        mov		b0=r3                       // SAL_CHECK return address
        br		b0
        ;;
#endif /* #ifdef SOFTSDV */
ia64_os_mca_dispatch_end:	
//EndMain//////////////////////////////////////////////////////////////////////


//++
// Name:
//      ia64_os_mca_proc_state_dump()
// 
// Stub Description:
//
//       This stub dumps the processor state during MCHK to a data area
//
//--

ia64_os_mca_proc_state_dump:
// Get and save GR0-31 from Proc. Min. State Save Area to SAL PSI
        movl		r2=ia64_mca_proc_state_dump;;           // Os state dump area

// save ar.NaT 
        mov		r5=ar.unat                  // ar.unat

// save banked GRs 16-31 along with NaT bits
        bsw.1;;
        st8.spill	[r2]=r16,8;;
        st8.spill	[r2]=r17,8;;
        st8.spill	[r2]=r18,8;;
        st8.spill	[r2]=r19,8;;
        st8.spill	[r2]=r20,8;;
        st8.spill	[r2]=r21,8;;
        st8.spill	[r2]=r22,8;;
        st8.spill	[r2]=r23,8;;
        st8.spill	[r2]=r24,8;;
        st8.spill	[r2]=r25,8;;
        st8.spill	[r2]=r26,8;;
        st8.spill	[r2]=r27,8;;
        st8.spill	[r2]=r28,8;;
        st8.spill	[r2]=r29,8;;
        st8.spill	[r2]=r30,8;;
        st8.spill	[r2]=r31,8;;

        mov		r4=ar.unat;;
        st8		[r2]=r4,8                // save User NaT bits for r16-r31
        mov		ar.unat=r5                  // restore original unat
        bsw.0;;

//save BRs
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2                // duplicate r2 in r4

        mov		r3=b0
        mov		r5=b1
        mov		r7=b2;;
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;  

        mov		r3=b3
        mov		r5=b4
        mov		r7=b5;;
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;  

        mov		r3=b6
        mov		r5=b7;;
        st8		[r2]=r3,2*8
        st8		[r4]=r5,2*8;;

cSaveCRs:
// save CRs
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2                // duplicate r2 in r4

        mov		r3=cr0                      // cr.dcr
        mov		r5=cr1                      // cr.itm
        mov		r7=cr2;;                    // cr.iva

        st8		[r2]=r3,8*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;            // 48 byte rements

        mov		r3=cr8;;                    // cr.pta
        st8		[r2]=r3,8*8;;            // 64 byte rements

// if PSR.ic=0, reading interruption registers causes an illegal operation fault
        mov		r3=psr;;
        tbit.nz.unc	p2,p0=r3,PSR_IC;;           // PSI Valid Log bit pos. test
(p2)    st8		[r2]=r0,9*8+160             // increment by 168 byte inc.
begin_skip_intr_regs:	
	BRANCH(SkipIntrRegs,  r9, p2, 0x0)
	;; 
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2                // duplicate r2 in r6
        
        mov		r3=cr16                     // cr.ipsr
        mov		r5=cr17                     // cr.isr
        mov		r7=r0;;                     // cr.ida => cr18
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;                                      

        mov		r3=cr19                     // cr.iip
        mov		r5=cr20                     // cr.idtr
        mov		r7=cr21;;                   // cr.iitr
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;                                      

        mov		r3=cr22                     // cr.iipa
        mov		r5=cr23                     // cr.ifs
        mov		r7=cr24;;                   // cr.iim
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;    
                                          
        mov		r3=cr25;;                   // cr.iha
        st8		[r2]=r3,160;;               // 160 byte rement

SkipIntrRegs:
        st8		[r2]=r0,168                 // another 168 byte .

        mov		r3=cr66;;                   // cr.lid
        st8		[r2]=r3,40                  // 40 byte rement

        mov		r3=cr71;;                   // cr.ivr
        st8		[r2]=r3,8

        mov		r3=cr72;;                   // cr.tpr
        st8		[r2]=r3,24                  // 24 byte increment
    
        mov		r3=r0;;                     // cr.eoi => cr75
        st8		[r2]=r3,168                 // 168 byte inc.
    
        mov		r3=r0;;                     // cr.irr0 => cr96
        st8		[r2]=r3,16               // 16 byte inc.

        mov		r3=r0;;                     // cr.irr1 => cr98
        st8		[r2]=r3,16               // 16 byte inc.

        mov		r3=r0;;                     // cr.irr2 => cr100
        st8		[r2]=r3,16               // 16 byte inc

        mov		r3=r0;;                     // cr.irr3 => cr100
        st8		[r2]=r3,16               // 16b inc.

        mov		r3=r0;;                     // cr.itv => cr114
        st8		[r2]=r3,16               // 16 byte inc.

        mov		r3=r0;;                     // cr.pmv => cr116
        st8		[r2]=r3,8

        mov		r3=r0;;                     // cr.lrr0 => cr117
        st8		[r2]=r3,8

        mov		r3=r0;;                     // cr.lrr1 => cr118
        st8		[r2]=r3,8

        mov		r3=r0;;                     // cr.cmcv => cr119
        st8		[r2]=r3,8*10;;

cSaveARs:
// save ARs
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2                // duplicate r2 in r6

        mov		r3=ar0                      // ar.kro
        mov		r5=ar1                      // ar.kr1
        mov		r7=ar2;;                    // ar.kr2
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;

        mov		r3=ar3                      // ar.kr3                               
        mov		r5=ar4                      // ar.kr4
        mov		r7=ar5;;                    // ar.kr5
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;

        mov		r3=ar6                      // ar.kr6
        mov		r5=ar7                      // ar.kr7
        mov		r7=r0;;                     // ar.kr8
        st8		[r2]=r3,10*8
        st8		[r4]=r5,10*8
        st8		[r6]=r7,10*8;;           // rement by 72 bytes

        mov		r3=ar16                     // ar.rsc
	mov		ar16=r0			    // put RSE in enforced lazy mode
        mov		r5=ar17                     // ar.bsp
	;;
        mov		r7=ar18;;                   // ar.bspstore
        st8		[r2]=r3,3*8
        st8		[r4]=r5,3*8
        st8		[r6]=r7,3*8;;

        mov		r3=ar19;;                   // ar.rnat
        st8		[r2]=r3,8*13             // increment by 13x8 bytes

        mov		r3=ar32;;                   // ar.ccv
        st8		[r2]=r3,8*4

        mov		r3=ar36;;                   // ar.unat
        st8		[r2]=r3,8*4

        mov		r3=ar40;;                   // ar.fpsr
        st8		[r2]=r3,8*4

        mov		r3=ar44;;                   // ar.itc
        st8		[r2]=r3,160                 // 160

        mov		r3=ar64;;                   // ar.pfs
        st8		[r2]=r3,8

        mov		r3=ar65;;                   // ar.lc
        st8		[r2]=r3,8

        mov		r3=ar66;;                   // ar.ec
        st8		[r2]=r3
        add		r2=8*62,r2               //padding
    
// save RRs
        mov		ar.lc=0x08-1
        movl		r4=0x00;;

cStRR:
        mov		r3=rr[r4];;
        st8		[r2]=r3,8
        add		r4=1,r4
        br.cloop.sptk.few	cStRR
        ;;
end_os_mca_dump:
	BRANCH(ia64_os_mca_done_dump, r2, p0, -0x10)
        ;;

//EndStub//////////////////////////////////////////////////////////////////////


//++
// Name:
//       ia64_os_mca_proc_state_restore()
// 
// Stub Description:
//
//       This is a stub to restore the saved processor state during MCHK
//
//--

ia64_os_mca_proc_state_restore:

// Restore bank1 GR16-31 
	movl		r2=ia64_mca_proc_state_dump	// Convert virtual address
	;;						// of OS state dump area
	DATA_VA_TO_PA(r2)				// to physical address
	;;
restore_GRs:                                    // restore bank-1 GRs 16-31
        bsw.1;;
        add		r3=16*8,r2;;                // to get to NaT of GR 16-31
        ld8		r3=[r3];;
        mov		ar.unat=r3;;                // first restore NaT

        ld8.fill	r16=[r2],8;;
        ld8.fill	r17=[r2],8;;
        ld8.fill	r18=[r2],8;;
        ld8.fill	r19=[r2],8;;
        ld8.fill	r20=[r2],8;;
        ld8.fill	r21=[r2],8;;
        ld8.fill	r22=[r2],8;;
        ld8.fill	r23=[r2],8;;
        ld8.fill	r24=[r2],8;;
        ld8.fill	r25=[r2],8;;
        ld8.fill	r26=[r2],8;;
        ld8.fill	r27=[r2],8;;
        ld8.fill	r28=[r2],8;;
        ld8.fill	r29=[r2],8;;
        ld8.fill	r30=[r2],8;;
        ld8.fill	r31=[r2],8;;

        ld8		r3=[r2],8;;              // increment to skip NaT
        bsw.0;;

restore_BRs:
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2;;              // duplicate r2 in r4

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;  
        mov		b0=r3
        mov		b1=r5
        mov		b2=r7;;

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;  
        mov		b3=r3
        mov		b4=r5
        mov		b5=r7;;

        ld8		r3=[r2],2*8
        ld8		r5=[r4],2*8;;  
        mov		b6=r3
        mov		b7=r5;;

restore_CRs:
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2;;              // duplicate r2 in r4

        ld8		r3=[r2],8*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;            // 48 byte increments
        mov		cr0=r3                      // cr.dcr
        mov		cr1=r5                      // cr.itm
        mov		cr2=r7;;                    // cr.iva

        ld8		r3=[r2],8*8;;            // 64 byte increments
//      mov		cr8=r3                      // cr.pta


// if PSR.ic=1, reading interruption registers causes an illegal operation fault
        mov		r3=psr;;
        tbit.nz.unc	p2,p0=r3,PSR_IC;;           // PSI Valid Log bit pos. test
(p2)    st8		[r2]=r0,9*8+160             // increment by 160 byte inc.

begin_rskip_intr_regs:	
	BRANCH(rSkipIntrRegs, r9, p2, 0x0)
	;; 

        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2;;              // duplicate r2 in r4

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;
        mov		cr16=r3                     // cr.ipsr
        mov		cr17=r5                     // cr.isr is read only
//      mov		cr18=r7;;                   // cr.ida

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;
        mov		cr19=r3                     // cr.iip
        mov		cr20=r5                     // cr.idtr
        mov		cr21=r7;;                   // cr.iitr

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;
        mov		cr22=r3                     // cr.iipa
        mov		cr23=r5                     // cr.ifs
        mov		cr24=r7                     // cr.iim

        ld8		r3=[r2],160;;               // 160 byte increment
        mov		cr25=r3                     // cr.iha 

rSkipIntrRegs:
        ld8		r3=[r2],168;;               // another 168 byte inc.

        ld8		r3=[r2],40;;                // 40 byte increment
        mov		cr66=r3                     // cr.lid

        ld8		r3=[r2],8;;
//      mov		cr71=r3                     // cr.ivr is read only
        ld8		r3=[r2],24;;                // 24 byte increment
        mov		cr72=r3                     // cr.tpr
   
        ld8		r3=[r2],168;;               // 168 byte inc.
//      mov		cr75=r3                     // cr.eoi
   
        ld8		r3=[r2],16;;             // 16 byte inc.
//      mov		cr96=r3                     // cr.irr0 is read only

        ld8		r3=[r2],16;;             // 16 byte inc.
//      mov		cr98=r3                     // cr.irr1 is read only

        ld8		r3=[r2],16;;             // 16 byte inc
//      mov		cr100=r3                    // cr.irr2 is read only

        ld8		r3=[r2],16;;             // 16b inc.
//      mov		cr102=r3                    // cr.irr3 is read only

        ld8		r3=[r2],16;;             // 16 byte inc.
//      mov		cr114=r3                    // cr.itv

        ld8		r3=[r2],8;;
//      mov		cr116=r3                    // cr.pmv
        ld8		r3=[r2],8;;
//      mov		cr117=r3                    // cr.lrr0
        ld8		r3=[r2],8;;
//      mov		cr118=r3                    // cr.lrr1
        ld8		r3=[r2],8*10;;
//      mov		cr119=r3                    // cr.cmcv

restore_ARs:
        add		r4=8,r2                  // duplicate r2 in r4
        add		r6=2*8,r2;;              // duplicate r2 in r4

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;
        mov		ar0=r3                      // ar.kro
        mov		ar1=r5                      // ar.kr1
        mov		ar2=r7;;                    // ar.kr2

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;
        mov		ar3=r3                      // ar.kr3                               
        mov		ar4=r5                      // ar.kr4
        mov		ar5=r7;;                    // ar.kr5

        ld8		r3=[r2],10*8
        ld8		r5=[r4],10*8
        ld8		r7=[r6],10*8;;
        mov		ar6=r3                      // ar.kr6
        mov		ar7=r5                      // ar.kr7
//      mov		ar8=r6                      // ar.kr8
        ;;

        ld8		r3=[r2],3*8
        ld8		r5=[r4],3*8
        ld8		r7=[r6],3*8;;
//      mov		ar16=r3                     // ar.rsc
//      mov		ar17=r5                     // ar.bsp is read only
	mov		ar16=r0			    // make sure that RSE is in enforced lazy mode
	;;
        mov		ar18=r7;;                   // ar.bspstore

        ld8		r9=[r2],8*13;;
        mov		ar19=r9                     // ar.rnat

	mov		ar16=r3			    // ar.rsc
        ld8		r3=[r2],8*4;;
        mov		ar32=r3                     // ar.ccv

        ld8		r3=[r2],8*4;;
        mov		ar36=r3                     // ar.unat

        ld8		r3=[r2],8*4;;
        mov		ar40=r3                     // ar.fpsr

        ld8		r3=[r2],160;;               // 160
//      mov		ar44=r3                     // ar.itc

        ld8		r3=[r2],8;;
        mov		ar64=r3                     // ar.pfs

        ld8		r3=[r2],8;;
        mov		ar65=r3                     // ar.lc

        ld8		r3=[r2];;
        mov		ar66=r3                     // ar.ec
        add		r2=8*62,r2;;             // padding 
    
restore_RRs:
        mov		r5=ar.lc
        mov		ar.lc=0x08-1
        movl		r4=0x00
cStRRr:
        ld8		r3=[r2],8;;
//      mov		rr[r4]=r3                   // what are its access previledges?
        add		r4=1,r4
        br.cloop.sptk.few	cStRRr
        ;;
        mov		ar.lc=r5
	;;
end_os_mca_restore:
	BRANCH(ia64_os_mca_done_restore, r2, p0, -0x20)
	;; 
//EndStub//////////////////////////////////////////////////////////////////////

// ok, the issue here is that we need to save state information so
// it can be useable by the kernel debugger and show regs routines.
// In order to do this, our best bet is save the current state (plus
// the state information obtain from the MIN_STATE_AREA) into a pt_regs
// format.  This way we can pass it on in a useable format.
//				

//
// SAL to OS entry point for INIT on the monarch processor
// This has been defined for registration purposes with SAL 
// as a part of ia64_mca_init.
//
// When we get here, the follow registers have been
// set by the SAL for our use
//		
//		1. GR1 = OS INIT GP
//		2. GR8 = PAL_PROC physical address
//		3. GR9 = SAL_PROC physical address
//		4. GR10 = SAL GP (physical)
//		5. GR11 = Init Reason
//			0 = Received INIT for event other than crash dump switch
//			1 = Received wakeup at the end of an OS_MCA corrected machine check
//			2 = Received INIT dude to CrashDump switch assertion
//	
//		6. GR12 = Return address to location within SAL_INIT procedure

				
	.text
	.align 16
.global ia64_monarch_init_handler       
.proc ia64_monarch_init_handler       
ia64_monarch_init_handler:

#if defined(CONFIG_SMP) && defined(SAL_MPINIT_WORKAROUND)
	//
	// work around SAL bug that sends all processors to monarch entry
	//
	mov	r17=cr.lid
	movl	r18=__cpu_physical_id
	;;
	dep	r18=0,r18,61,3		// convert to physical address
	;;
	shr.u	r17=r17,16
	ld4	r18=[r18]		// get the BSP ID
	;;
	dep	r17=0,r17,16,48
	;;
	cmp4.ne	p6,p0=r17,r18		// Am I the BSP ?
(p6)	br.cond.spnt slave_init_spin_me
	;;
#endif

	
//
// ok, the first thing we do is stash the information
// the SAL passed to os
//
_tmp = r2
	movl	_tmp=ia64_sal_to_os_handoff_state
	;;
	dep	_tmp=0,_tmp, 61, 3	// get physical address
	;;
	st8	[_tmp]=r1,0x08;;
	st8	[_tmp]=r8,0x08;;
	st8	[_tmp]=r9,0x08;;			
	st8	[_tmp]=r10,0x08;;			
	st8	[_tmp]=r11,0x08;;
	st8	[_tmp]=r12,0x08;;

// now we want to save information so we can dump registers
	SAVE_MIN_WITH_COVER
	;;
	mov r8=cr.ifa
	mov r9=cr.isr
	adds r3=8,r2				// set up second base pointer
	;;
	SAVE_REST
	
// ok, enough should be saved at this point to be dangerous, and  supply
// information for a dump
// We need to switch to Virtual mode before hitting the C functions.
//
// 
//
	movl	r2=IA64_PSR_IT|IA64_PSR_IC|IA64_PSR_DT|IA64_PSR_RT|IA64_PSR_DFH|IA64_PSR_BN
	mov	r3=psr	// get the current psr, minimum enabled at this point
	;;
	or	r2=r2,r3
	;;
	movl	r3=IVirtual_Switch
	;;
	mov	cr.iip=r3		// short return to set the appropriate bits
	mov	cr.ipsr=r2		// need to do an rfi to set appropriate bits
	;;
	rfi
	;;
IVirtual_Switch:
	//	
	// We should now be running virtual
	//
	// Lets call the C handler to get the rest of the state info
	//
	alloc r14=ar.pfs,0,0,1,0		// now it's safe (must be first in insn group!)
	;;					//
	adds out0=16,sp				// out0 = pointer to pt_regs
	;;

	br.call.sptk.few rp=ia64_init_handler
.ret1:

return_from_init:
	br.sptk return_from_init

	.endp

//
// SAL to OS entry point for INIT on the slave processor
// This has been defined for registration purposes with SAL 
// as a part of ia64_mca_init.
//

	.text
	.align 16
.global ia64_slave_init_handler
.proc ia64_slave_init_handler
ia64_slave_init_handler:		


slave_init_spin_me:	
	br.sptk slave_init_spin_me
	;;
	.endp
