#
#  vtxsys32.S
#
#  Copyright (C) 2002-2004 Intel Corporation
#  Maintainer - Juan Villacis <juan.villacis@intel.com>
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# ===========================================================================
#
#	File: vtxsys32.S
#
#	Description: routines for sampling on IA32 platforms
#
#	Author(s): George Artz, Intel Corp.
#
#	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
#
# ===========================================================================
#

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/desc.h>

#define RDMSR   .byte   0x0f,0x32
#define WRMSR   .byte   0x0f,0x30

MASK_APIC_BASE          = 0xfffff000

APIC_BASE_MSR           = 0x1b


# ------ local APIC equates
                                        # local apic registers
APIC_LCL_ID             = 0x20          # ..ID reg
APIPC_LCL_VER           = 0x30          # ..version
APIC_LCL_TASKPRI        = 0x80          # ..version
APIC_LCL_LDEST          = 0xD0          # ..local destination reg
APIC_LCL_EOI            = 0xB0          # ..eoi register
APIC_LCL_DESTFMT        = 0xE0          # ..destination format reg
APIC_LVT_SPURIOUS       = 0xF0          # ..spurious vector reg
APIC_LVT_TIMER          = 0x320         # ..local timer
APIC_LVT_PERF           = 0x340         # ..performance monitor lvt
APIC_LVT_LINT0          = 0x350         # ..local int 0 lvt
APIC_LVT_LINT1          = 0x360         # ..local int 0 lvt

# ------ various values for apic registers

AV_MASK                 = 0x10000       #
AV_TOALL                = 0x7fffffff    #
AV_IM_OFF               = 0x80000000    # logical destination if OFF *
AV_FIXED                = 0x0           #
AV_LOPRI                = 0x100         #
AV_NMI                  = 0x400         #
AV_RESET                = 0x500         #
AV_STARTUP              = 0x600         #
AV_EXTINT               = 0x700         #
AV_PENDING              = 0x1000        #
AV_PDEST                = 0x0           #
AV_LDEST                = 0x800         #
AV_POLOW                = 0x2000        #
AV_POHIGH               = 0x0           #
AV_ASSERT               = 0x4000        #
AV_DEASSERT             = 0x0           #
AV_EDGE                 = 0x0           #
AV_LEVEL                = 0x8000        #
AV_POLARITY             = 0x2000        #
AV_XTOSELF              = 0x40000       #
AV_XTOALL               = 0x80000       #

AV_EXTINT_EDGE          = 0x00000700    #
AV_NMI_LEVEL            = 0x00008400    #


# ------ APIC Task priority values
TPRIHI                  = 0xf0          #
TPRILO                  = 0x0           #

P6_APIC_ENABLE          = 0x800         # Enable bit in P6 APIC_BASE_MSR


# ----------------------------------------------------------------------------
# name:         get_CSD
# description:  get the CS descriptor
# Input:        code segment selector
# Output:       code segment descriptor
# ----------------------------------------------------------------------------
        .text
        .align  4
        .globl  get_CSD

get_CSD:
        pushl   %ebp
        movl    %esp, %ebp
        pushal                                  # save regs

        subl    $8,%esp
        xorl    %eax, %eax
        movw    8(%ebp), %ax                    # eax.lo = cs
        sgdt    (%esp)                          # store gdt reg
        leal    (%esp), %ebx                    # ebx = gdt reg ptr
        movl    2(%ebx), %ecx                   # ecx = gdt base
        xorl    %edx, %edx
        movw    %ax, %dx
        andl    $4, %edx
        cmpl    $0, %edx                        # test ti. GDT?
        jz      .bsr_10                         # ..yes
        xorl    %edx, %edx
        sldt    %dx                             # ..no dx=ldtsel
        andb    $0xf8, %dl                      # clear ti,rpl
        addl    2(%ebx), %edx                   # add gdt base
        movb    7(%edx), %cl                    # ecx = ldt base
        shll    $8, %ecx                        # ..
        movb    4(%edx), %cl                    # ..
        shll    $16, %ecx                       # ..
        movw    2(%edx), %cx                    # ..
.bsr_10:
        andb    $0xf8, %al                      # clear ti & rpl
        addl    %eax, %ecx                      # add to gdt/ldt
        movl    (%ecx), %eax                    # copy code seg
        movl    12(%ebp), %edx                  # ..descriptor (csdlo)
        movl    %eax, (%edx)                    # ..descriptor (csdlo)
        movl    4(%ecx), %eax                   # ..from gdt or
        movl    16(%ebp), %edx                  # ..ldt to sample (csdhi)
        movl    %eax, (%edx)                    # ..ldt to sample (csdhi)
        addl    $8,%esp
        popal                                   # restore regs
        leave
        ret



# ----------------------------------------------------------------------------
# name:         samp_get_set_idt_entry
# description:  get/Set the IDT entry for the interrupt vector 
# Input:        vector number, handler, address of the original entry,
#               address of original handler
# Output:       original entry and handler
# ----------------------------------------------------------------------------

        .text
        .align  4
        .globl  samp_get_set_idt_entry

samp_get_set_idt_entry:

        pushl   %ebp
        movl    %esp, %ebp

        pushal                          # save regs
        cli                             # disable on this cpu

        # get address of idt entry for caller's interrupt #
        subl    $8, %esp
        sidt    2(%esp)                         # get idt base address
        movl    4(%esp),%esi            # esi = addr of idt
        movl    8(%ebp),%eax            # eax = idt index
        leal    (%esi,%eax,8),%esi      # esi = addr of idt entry

        # Copy current idt entry in caller's save area

        movl    16(%ebp),%edi           # edi = addr to save idt entry
        or      %edi,%edi               # save addr set?
        jz      set_idt_10              # ..no
        movl    (%esi),%eax             # ..yes  copy current
        movl    %eax,(%edi)             # ..idt
        movl    4(%esi),%eax            # ..entry
        movl    %eax,4(%edi)            # ..to caller's save area

set_idt_10:
        #
        # Set new idt entry. int32 gate with caller's handler
        #

        movl    12(%ebp),%ebx           # ebx = new int handler addr
        or      %ebx,%ebx               # handler supplied?
        jz      set_idt_20              # ..no
        movw    %cs,%ax                 # set cs in IDT gate
        movw    %ax,2(%esi)             # ..
        movw    %bx,(%esi)              # set offset.lo in IDT gate
        movw    $0x8e00,%bx             # bx = P,DPL=0,TYPE=int_gate32
        mov     %ebx,4(%esi)            # ebx.hi = hi half of offset

set_idt_20:
        sti
        addl    $8,%esp
        popal                           # restore regs
        leave
        ret

# ----------------------------------------------------------------------------
# name:         samp_restore_idt_entry
# description:  restore the IDT entry for the interrupt vector 
# Input:        vector number, original handler
# Output:       none
# ----------------------------------------------------------------------------
        .text
        .align  4
        .globl  samp_restore_idt_entry

 samp_restore_idt_entry:

        pushl   %ebp
        movl    %esp, %ebp

        pushal                          # save regs
        cli                             # disable on this cpu

        # get address of idt entry for caller's interrupt #
        subl    $8, %esp
        sidt    2(%esp)                 # get idt base address
        movl    4(%esp),%edi            # edi = addr of idt
        movl    8(%ebp),%eax            # eax = idt index
        leal    (%edi,%eax,8),%edi      # edi = addr of idt entry

        # restore idt entry

        movl    12(%ebp),%esi           # esi = addr to save idt entry
        or      %esi,%esi               # save addr set?
        jz      restore_idt_10          # ..no
        movl    (%esi),%eax             # ..yes
        movl    4(%esi),%ebx            # ..is original entry set?
        mov     %eax,(%edi)             # ..yes restore original
        mov     %ebx,4(%edi)            # ..idt entry

restore_idt_10:
        sti
        addl    $8,%esp
        popal                           # restore regs
        leave
        ret

        .globl  apic_local_addr

        .text
        .align  4
        .globl  samp_apic_set_perf_lvt_int_mask
#***********************************************************************
#    Set interrupt mask bit in APIC perf LVT
#
#
#    On entry: 
#
#***********************************************************************
samp_apic_set_perf_lvt_int_mask:
        pushl   %eax
        pushl   %edx

        movl    apic_local_addr,%edx            # edx = mapped local apic addr
        orl     %edx,%edx     
        jz      apic_set_perf_LVT_exit    
        movl    APIC_LVT_PERF(%edx),%eax        # set int mask in perf lvt
        orl     $0x10000,%eax                   # ..
        movl    %eax,APIC_LVT_PERF(%edx)        # ..
apic_set_perf_LVT_exit:
        popl    %edx
        popl    %eax
        ret


        .text
        .align  4
        .globl  samp_apic_clear_perf_lvt_int_mask
#***********************************************************************
#    Clear interrupt mask bit in APIC perf LVT
#
#
#    On entry: 
#
#***********************************************************************
samp_apic_clear_perf_lvt_int_mask:
        pushl   %eax
        pushl   %edx

        movl    apic_local_addr,%edx            # edx = mapped local apic addr
        orl     %edx,%edx     
        jz      apic_set_perf_LVT_exit    
        movl    APIC_LVT_PERF(%edx),%eax        # clear int mask in perf lvt
        andl    $0xFFFEFFFF,%eax                # ..
        movl    %eax,APIC_LVT_PERF(%edx)        # ..
apic_clear_perf_LVT_exit:
        popl    %edx
        popl    %eax
        ret

#***********************************************************************
#    EOI the local APIC
#
#
#    On entry: 
#
#;***********************************************************************
samp_apic_EOI:
        pushl   %edx

        movl    apic_local_addr,%edx            # edx = mapped local apic addr
        orl     %edx,%edx                       #
        jz      apic_eoi_10                     #
	xor	%eax, %eax                      # Pentium(R) 4 processor 
						# likes to see a zero
        movl    %eax,APIC_LCL_EOI(%edx)         # eoi local apic
apic_eoi_10:

        popl    %edx                            # restore regs
        ret



# ----------------------------------------------------------------------------
# name:         SAMP_Set_Apic_Virtual_Wire_Mode
# description:  get performance vector in local APIC
# Input:        apic_local_addr = mapped virt addr of local apic
# Output:       local apic placed in virtual wire mode
#
# ----------------------------------------------------------------------------


       
        .text
        .align  4
        .globl  SAMP_Set_Apic_Virtual_Wire_Mode

SAMP_Set_Apic_Virtual_Wire_Mode:

        pushal                                  #save regs
        pushfl                                  #save flags (if state)
        
# ------ If apic is disabled, switch apic to virtual wire mode


        movl    $(APIC_BASE_MSR),%ecx           # APIC base MSR
        xorl    %eax, %eax                      # ..
        xorl    %edx, %edx                      # ..
        RDMSR                                   # eax = apic physical base

        test    $(P6_APIC_ENABLE),%eax          # is apic enabled?
        jnz     set_vwm_exit                    # ..yes all done

        movl    apic_local_addr,%edx            # edx = mapped local apic addr
        orl     %edx,%edx     
        jz      set_vwm_exit    

        
# ------ mask both 8259's to prevent interrupts from
# ------ being trapped while switching to virtual wire mode

        xorl    %eax,%eax                       #mask both 8259's
        xorl    %ebx,%ebx                       #ebx = 0
        cli                                     #..
        inb     $0xa1,%al                       #..
        
        nop                                     # IO delay..
        nop                                     # ..
        
        movb    %al,%bh                         #..bh = original pic2 mask
        movl    $0x0ff,%eax                     #..
        outb    %al,$0xa1                       #..
        inb     $0x21,%al                       #..

        nop                                     # IO delay..
        nop                                     # ..

        movb    %al,%bl                         #..bl = original pic1 mask
        movl    $0xff,%eax                      #..
        outb    %al,$0x21                       #..

        sti                                     #enable ints to make sure ints
        nop                                     #..are not trapped in 8259
        nop                                     #..
        cli                                     #..



# ------ write P6 APIC_BASE_MSR to enable local apic memory mapped io

        movl    $(APIC_BASE_MSR),%ecx           # APIC base MSR
        xorl    %eax, %eax                      #
        xorl    %edx, %edx                      #
        RDMSR                                   # RDMSR
        
        orl             $(P6_APIC_ENABLE),%eax  # eax = set apic enable bit
        WRMSR                                   # WRMSR write P6 APIC_BASE_MSR

# ------ Set apic registers for virtual wire mode.

        movl    apic_local_addr,%edx            # edx = mapped local apic addr

        movl    $0x000001ff,APIC_LVT_SPURIOUS(%edx)     # enable virtual wire mode
        movl    $(TPRIHI),APIC_LCL_TASKPRI(%edx)        # set task priority hi
        movl    $0,APIC_LCL_ID(%edx)                    # set local unit id = 0
        movl    $0,APIC_LCL_LDEST(%edx)                 # set logical unit id = 0
        movl    $-1,APIC_LCL_DESTFMT(%edx)              # set destination format
        movl    $(AV_MASK),APIC_LVT_TIMER(%edx)         # mask local timer
        movl    $(AV_EXTINT_EDGE),APIC_LVT_LINT0(%edx)  # 8259->lint0
        movl    $(AV_NMI_LEVEL),APIC_LVT_LINT1(%edx)    # nmi ->lint1
        movl    $(TPRILO),APIC_LCL_TASKPRI(%edx)        # set task priority lo

# ------ restore mask state for pic1 & pic2

        movb    %al,%bh                         # restore pic2 mask
        outb    %al,$0xa1                       # ..

        nop                                     # IO delay..
        nop                                     # ..

        movb    %bl,%al                         # restore pic1 mask
        outb    %al,$0x21                       # ..


set_vwm_exit:
        popfl                                   # restore flags
        popal                                   #
        ret                                     #

# ----------------------------------------------------------------------------
# name:         vtune_sys_init
# description:  low level initialization. determines local APIC phys addr.
# Input:        none
# Output:       none
# ----------------------------------------------------------------------------
        .text
        .align  4
        .globl  vtune_sys_init

vtune_sys_init: 

        pushl   %eax                            # save regs
        pushl   %ecx
        pushl   %edx

        movl    $(APIC_BASE_MSR),%ecx           # APIC base MSR
        xorl    %eax, %eax                      #
        xorl    %edx, %edx                      #
        RDMSR                                   # RDMSR
        
        andl    $(MASK_APIC_BASE),%eax          # eax = local apic physical base
        movl    %eax, apic_paddr                # Save apic phys addr as global

        popl    %edx                            # restore regs
        popl    %ecx
        popl    %eax
        ret


# ----------------------------------------------------------------------------
# name: SAMP_Set_apic_perf_lvt
# description:          Set performance vector in local APIC
# Input:                        new apic perf lvt value
# Output:                       0 if local apic not present
#                                       previous value of local apic perf lvt
# ----------------------------------------------------------------------------
        .text
        .align  4
        .globl  SAMP_Set_apic_perf_lvt

SAMP_Set_apic_perf_lvt:

        pushl   %ebx
        xorl    %eax,%eax                       # eax = 0 (if no local apic)
        cmpl    $0,apic_local_addr              # is local apic mapped?
                                                # pcmp platform-specific
        je      set_perf_exit                   #...no
        movl    apic_local_addr,%ebx            #..yes ebx = local apic addr
        movl    8(%esp),%eax                    # ..eax = new perf lvt value
        xchgl    %eax,APIC_LVT_PERF(%ebx)       #...set new perf lvt.
                                                #..eax = original perf lvt
set_perf_exit:
        popl    %ebx                            # restore regs
        ret


# ----------------------------------------------------------------------------
# name:         SAMP_get_apic_perf_lvt
# description:  get performance vector in local APIC
# Input:        none
# Output:       0 if local apic not present
#                       value of local apic perf lvt
# ----------------------------------------------------------------------------
        .text
        .align  4
        .globl  SAMP_get_apic_perf_lvt

SAMP_get_apic_perf_lvt:

        xorl    %eax,%eax                       # eax = 0 (if no local apic)
        cmpl    $0,apic_local_addr              # is local apic mapped?
        je              get_perf_exit           #..no
        leal    apic_local_addr,%eax            #..yes eax = local apic addr
        movl    APIC_LVT_PERF(%eax),%eax        #..eax = local apic perf lvt

get_perf_exit:
        ret


# ----------------------------------------------------------------------------
# name:         t_ebs
#
# description:  ISR entry for local APIC PERF interrupt vector 
#
# Input:        n/a
#
# Output:       n/a original entry and handler
# ----------------------------------------------------------------------------

        .text
        .align  8
        .globl  t_ebs

t_ebs: 
                                        # This is the same as KERNEL's
        pushl   %eax                    # Filler for Error Code

        cld
        pushl   %es                     # SAVE_ALL macro to access pt_regs
        pushl   %ds                     # inside our ISR.
        pushl   %eax
        pushl   %ebp
        pushl   %edi
        pushl   %esi
        pushl   %edx
        pushl   %ecx
        pushl   %ebx

        xor     %edx, %edx              # Clear DX storage (css why? overwritten below?)
        
        movl    $(__KERNEL_DS), %edx    # Use KERNEL DS selector
        movl    %edx,%ds                # Make sure we set Kernel
        movl    %edx,%es                # DS into local DS and ES

	movl	%esp, %ebx		# get ready to put *pt_regs on stack
        
#if defined(ALLOW_LBRS)

	movl	quick_freeze_msr, %ecx	# see if we need to freeze the LBRs
	cmpl	$0, %ecx		# zero means no freeze
	je	samp_no_lbrs

	RDMSR				# get the old freeze state
	pushl	%eax
	pushl	%edx

	xor 	%eax, %eax
	xor 	%edx, %edx
	WRMSR				# write zeros to cause an LBR freeze

samp_no_lbrs:
	pushl	%ecx			# always save whether we did extra work

#endif

        pushl   %ebx			# put *pt_regs on the stack
        call ebs_intr
        addl $0x4, %esp			# pop to nowhere...

#if defined(ALLOW_LBRS)
	popl	%ecx			# did we do extra work on the way in?
	cmpl	$0, %ecx
	je	samp_skip_msr_restore	# nope, so we done with lbr stuff

	popl	%edx
	popl	%eax
	WRMSR				# restore the freeze register

samp_skip_msr_restore:

#endif

        call    samp_apic_EOI           # send EOI to APIC

        pop     %ebx                    # restore register set
        pop     %ecx
        pop     %edx
        pop     %esi
        pop     %edi
        pop     %ebp
        pop     %eax
        pop     %ds
        pop     %es
        pop     %eax
        
        iret



