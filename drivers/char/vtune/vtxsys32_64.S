#
#  vtxsys32_64.S
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
#	File: vtxsys32_64.S
#
#	Description: routines for sampling on Pentium(R) 4 processors
#                    with Intel(R) Extended Memory 64 Technology
#
#	Author(s): Charles Spirakis, Intel Corp.
#	           Thomas M Johnson, Intel Corp.
#
#	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
#
# ===========================================================================
#

#define ASSEMBLY    1

#include <asm/calling.h>
#include <asm/msr.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/unistd.h>
#include <asm/ia32_unistd.h>
#include <asm/current.h>
		
#define ORIG_RCX_OFFSET    0xD8
#define VT_ARGS_SKIP       56
#define RET_VAL_PTR      0xA8
#define FINAL_RET_VAL_IA32 RAX+VT_ARGS_SKIP+8*9+8
#define ORIG_STACK_RIP     0x108
#define ORIG_STACK_CS      0x110
#define ORIG_STACK_EFLAGS  0x118
#define ORIG_STACK_RSP     0x120
#define ORIG_STACK_SS      0x128
	
.text

#define PDAREF(field) %gs:field	 		
	
.macro FIXUP_TOP_OF_STACK tmp
	movq	PDAREF(pda_oldrsp),\tmp
	movq  	\tmp,RSP(%rsp)
	movq    $__USER_DS,SS(%rsp)
	movq    $__USER_CS,CS(%rsp)
	movq	$-1,RCX(%rsp)	/* contains return address, already in RIP */
	movq	R11(%rsp),\tmp  /* get eflags */
	movq	\tmp,EFLAGS(%rsp)
.endm

.macro RESTORE_TOP_OF_STACK tmp,offset=0
	movq   RSP-\offset(%rsp),\tmp
	movq   \tmp,PDAREF(pda_oldrsp)
	movq   EFLAGS-\offset(%rsp),\tmp
	movq   \tmp,R11-\offset(%rsp)
.endm

.macro IA32_ARG_FIXUP
	movl %edi,%r8d
	movl %ebp,%r9d
	xchg %ecx,%esi
	movl %ebx,%edi
	movl %edx,%edx
.endm

.macro UNDO_IA32_ARG_FIXUP
	movq R8-ARGOFFSET+8(%rsp),%r8
	movq R9-ARGOFFSET+8(%rsp),%r9
	movq RCX-ARGOFFSET+8(%rsp),%rcx
	movq RSI-ARGOFFSET+8(%rsp),%rsi
	movq RDI-ARGOFFSET+8(%rsp),%rdi
	movq RDX-ARGOFFSET+8(%rsp),%rdx
.endm

.macro COMPLETE_STACK_FRAME
	movq %rax, ORIG_RAX(%rsp)
	movq ORIG_STACK_RIP(%rsp), %rax
	movq %rax, RIP(%rsp)
	movq ORIG_STACK_CS(%rsp), %rax
	movq %rax, CS(%rsp)
	movq ORIG_STACK_EFLAGS(%rsp), %rax
	movq %rax, EFLAGS(%rsp)
	movq ORIG_STACK_RSP(%rsp), %rax
	movq %rax, RSP(%rsp)
	movq ORIG_STACK_SS(%rsp), %rax
	movq %rax, SS(%rsp)
	movq ORIG_RAX(%rsp), %rax
.endm
		
#***********************************************************************
#
#    GetIDTDesc
#            Get the IDT Desc address
#
#    Entry:  pointer to location to store idt Desc
#
#    Exit:  none 
#
# void GetIDTDesc(U64 *pIdtDesc);
#
#***********************************************************************
        .global GetIDTDesc
GetIDTDesc:
        SIDT (%rdi)
        ret

#***********************************************************************
#
#    GetGDTDesc
#            Get the GDT Desc address
#
#    Entry:  pointer to location to store gdt Desc
#
#    Exit:  none 
#
# void GetGDTDesc(U64 *pGdtDesc);
#
#***********************************************************************
        .global GetIDTDesc
GetGDTDesc:
        SGDT (%rdi)
        ret

# ----------------------------------------------------------------------------
# name:         t_ebs
#
# description:  ISR entry for local APIC PERF interrupt vector
#
# Input:        n/a
#
# Output:       n/a 
# ----------------------------------------------------------------------------

        .global t_ebs
t_ebs:
        pushq %rax          // fake an error code...
        cld                 // cause the kernel likes it this way...

        SAVE_ALL            // Save the world!

        movl  $MSR_GS_BASE,%ecx     // for the moment, do the safe swapgs check
        rdmsr
        xorl  %ebx,%ebx             // assume no swapgs (ebx == 0)
        testl %edx,%edx
        js    1f
        swapgs
        movl  $1,%ebx               // ebx == 1 means we did a swapgs
1:      movq %rsp,%rdi              // pt_regs is the first argument

        //
        // ebx is zero if no swap, one if swap
        // ebx is preserved in C calling convention...
        //
        // NOTE: the C code is responsible for ACK'ing the APIC!!!
        //
        call ebs_intr

        //
        // Don't want an interrupt while we are doing the swapgs stuff
        //
        cli
        testl %ebx,%ebx
        jz 2f
        swapgs
2:      RESTORE_ALL
        popq    %rax
        iretq

# ----------------------------------------------------------------------------
# name:         vt_clone_prologue
#
# description:  Entry point for intercepting 64-bit clone system call.
#
# Input:        partial stack frame expected by clone system call
#
# Output:       clone result value
# ----------------------------------------------------------------------------

	.global vt_clone_prologue
vt_clone_prologue:
	SAVE_ARGS VT_ARGS_SKIP,0
	movq ORIG_RCX_OFFSET(%rsp),%rcx
	movq %rax,ORIG_RAX-ARGOFFSET(%rsp)
	movq %rcx,RIP-ARGOFFSET(%rsp)
	movq %r10,%rcx
	SAVE_REST
	movq $-ENOSYS,RAX(%rsp)
	FIXUP_TOP_OF_STACK %rdi
	call vt_sys_clone_enter
vt_clone_prologue_done:
	movq %rax,RET_VAL_PTR(%rsp)
	LOAD_ARGS ARGOFFSET
	RESTORE_REST
	movq %r10,%rcx
	movq original_sys_clone,%rax
	call *%rax
vt_clone_epilogue:
	movq %rax,RAX-ARGOFFSET(%rsp)
	SAVE_REST
	movq %rsp,%rdi
	movq ORIG_RAX(%rsp),%rax
	movq RET_VAL_PTR(%rsp),%rsi
	movq RAX(%rsp),%rdi
	call vt_sys_clone_exit
vt_clone_cleanup:	
	RESTORE_TOP_OF_STACK %rbx
	RESTORE_REST
	RESTORE_ARGS 0,VT_ARGS_SKIP,0
	retq

# ----------------------------------------------------------------------------
# name:         vt_fork32_prologue
#
# description:  Entry point for intercepting 32-bit fork system call.
#
# Input:        partial stack frame expected by fork system call
#
# Output:       fork result value
# ----------------------------------------------------------------------------

	.global vt_fork32_prologue
vt_fork32_prologue:
	UNDO_IA32_ARG_FIXUP
	SAVE_ARGS VT_ARGS_SKIP
	SAVE_REST
	COMPLETE_STACK_FRAME
	movq $-ENOSYS,RAX(%rsp)
	call vt_sys32_fork_enter
vt_fork32_prologue_done:
	movq %rax,RET_VAL_PTR(%rsp)
	LOAD_ARGS ARGOFFSET
	addq $ARGOFFSET,%rsp
	IA32_ARG_FIXUP
	movq original_ia32_sys_fork,%rax
	call *%rax
vt_fork32_epilogue:
	movq %rax,RAX-ARGOFFSET(%rsp)
	SAVE_REST
	movq RET_VAL_PTR(%rsp),%rsi
	movq RAX(%rsp),%rdi
	call vt_sys32_fork_exit
vt_fork32_epilogue_done:
	movq RAX(%rsp),%rax
	movq %rax,FINAL_RET_VAL_IA32(%rsp)
	addq $ARGOFFSET,%rsp
	addq $VT_ARGS_SKIP+8*9,%rsp
	retq

# ----------------------------------------------------------------------------
# name:         vt_vfork32_prologue
#
# description:  Entry point for intercepting 32-bit vfork system call.
#
# Input:        partial stack frame expected by vfork system call
#
# Output:       vfork result value
# ----------------------------------------------------------------------------

	.global vt_vfork32_prologue
vt_vfork32_prologue:
	UNDO_IA32_ARG_FIXUP
	SAVE_ARGS VT_ARGS_SKIP
	SAVE_REST
	COMPLETE_STACK_FRAME
	movq $-ENOSYS,RAX(%rsp)
	call vt_sys32_vfork_enter
vt_vfork32_prologue_done:
	movq %rax,RET_VAL_PTR(%rsp)
	LOAD_ARGS ARGOFFSET
	addq $ARGOFFSET,%rsp
	IA32_ARG_FIXUP
	movq original_ia32_sys_vfork,%rax
	call *%rax
vt_vfork32_epilogue:
	movq %rax,RAX-ARGOFFSET(%rsp)
	SAVE_REST
	movq RET_VAL_PTR(%rsp),%rsi
	movq RAX(%rsp),%rdi
	call vt_sys32_vfork_exit
vt_vfork32_epilogue_done:
	movq RAX(%rsp),%rax
	movq %rax,FINAL_RET_VAL_IA32(%rsp)
	addq $ARGOFFSET,%rsp
	addq $VT_ARGS_SKIP+8*9,%rsp
	retq

# ----------------------------------------------------------------------------
# name:         vt_clone32_prologue
#
# description:  Entry point for intercepting 32-bit clone system call.
#
# Input:        partial stack frame expected by clone system call
#
# Output:       clone result value
# ----------------------------------------------------------------------------

	.global vt_clone32_prologue
vt_clone32_prologue:
	UNDO_IA32_ARG_FIXUP
	SAVE_ARGS VT_ARGS_SKIP
	SAVE_REST
	COMPLETE_STACK_FRAME
	movq $-ENOSYS,RAX(%rsp)
	call vt_sys32_clone_enter
vt_clone32_prologue_done:
	movq %rax,RET_VAL_PTR(%rsp)
	LOAD_ARGS ARGOFFSET
	addq $ARGOFFSET,%rsp
	IA32_ARG_FIXUP
	movq original_ia32_sys_clone,%rax
	call *%rax
vt_clone32_epilogue:
	movq %rax,RAX-ARGOFFSET(%rsp)
	SAVE_REST
	movq RET_VAL_PTR(%rsp),%rsi
	movq RAX(%rsp),%rdi
	call vt_sys32_clone_exit
vt_clone32_epilogue_done:
	movq RAX(%rsp),%rax
	movq %rax,FINAL_RET_VAL_IA32(%rsp)
	addq $ARGOFFSET,%rsp
	addq $VT_ARGS_SKIP+8*9,%rsp
	retq

# ----------------------------------------------------------------------------
# name:         vt_execve32_prologue
#
# description:  Entry point for intercepting 32-bit execve system call.
#
# Input:        partial stack frame expected by execve system call
#
# Output:       execve result value
# ----------------------------------------------------------------------------

	.global vt_execve32_prologue
vt_execve32_prologue:
	UNDO_IA32_ARG_FIXUP
	SAVE_ARGS VT_ARGS_SKIP
	SAVE_REST
	COMPLETE_STACK_FRAME
	movq $-ENOSYS,RAX(%rsp)
	call vt_sys32_execve_enter
vt_execve32_prologue_done:
	movq %rax,RET_VAL_PTR(%rsp)
	LOAD_ARGS ARGOFFSET
	addq $ARGOFFSET,%rsp
	IA32_ARG_FIXUP
	movq original_ia32_sys_execve,%rax
	call *%rax
vt_execve32_epilogue:
	movq %rax,RAX-ARGOFFSET(%rsp)
	SAVE_REST
	movq RET_VAL_PTR(%rsp),%rsi
	movq RAX(%rsp),%rdi
	call vt_sys32_execve_exit
vt_execve32_epilogue_done:
	movq RAX(%rsp),%rax
	movq %rax,FINAL_RET_VAL_IA32(%rsp)
	addq $ARGOFFSET,%rsp
	addq $VT_ARGS_SKIP+8*9,%rsp
	retq

