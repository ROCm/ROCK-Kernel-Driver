/*
 *  linux/arch/x86_64/kernel/head.S -- start in 32bit and switch to 64bit
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2000 Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2000 Karsten Keil <kkeil@suse.de>
 *  Copyright (C) 2001,2002 Andi Kleen <ak@suse.de>
 *  Copyright (C) 2005 Eric Biederman <ebiederm@xmission.com>
 *  Jun Nakajima <jun.nakajima@intel.com>
 *    Modified for Xen                                
 */


#include <linux/linkage.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/elfnote.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/msr.h>
#include <asm/cache.h>
#include <asm/dwarf2.h>
#include <asm/percpu.h>
#include <xen/interface/elfnote.h>

	__HEAD
	.code64
	.globl startup_64
startup_64:
	movq $(init_thread_union+THREAD_SIZE-8),%rsp

	/* rsi is pointer to startup info structure.
	   pass it to C */
	movq %rsi,%rdi

	/* Set up %gs.
	 *
	 * The base of %gs always points to the bottom of the irqstack
	 * union.  If the stack protector canary is enabled, it is
	 * located at %gs:40.  Note that, on SMP, the boot cpu uses
	 * init data section till per cpu areas are set up.
	 */
	movl	$MSR_GS_BASE,%ecx
	movq	$INIT_PER_CPU_VAR(irq_stack_union),%rax
	movq    %rax,%rdx
	shrq	$32,%rdx
	wrmsr

	pushq $0		# fake return address
	jmp x86_64_start_kernel

#define NEXT_PAGE(name) \
	.balign	PAGE_SIZE; \
	phys_##name = . - .head.text; \
ENTRY(name)

NEXT_PAGE(init_level4_pgt)
	.fill	512,8,0

NEXT_PAGE(level3_kernel_pgt)
	.fill	512,8,0

        /*
         * This is used for vsyscall area mapping as we have a different
         * level4 page table for user.
         */
NEXT_PAGE(level3_user_pgt)
        .fill	512,8,0

NEXT_PAGE(level2_fixmap_pgt)
	.fill	512,8,0

NEXT_PAGE(level1_fixmap_pgt)
	.fill	512,8,0

NEXT_PAGE(hypercall_page)
	CFI_STARTPROC
	.rept 0x1000 / 0x20
	.skip 1 /* push %rcx */
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET	rcx,0
	.skip 2 /* push %r11 */
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET	rcx,0
	.skip 5 /* mov $#,%eax */
	.skip 2 /* syscall */
	.skip 2 /* pop %r11 */
	CFI_ADJUST_CFA_OFFSET -8
	CFI_RESTORE r11
	.skip 1 /* pop %rcx */
	CFI_ADJUST_CFA_OFFSET -8
	CFI_RESTORE rcx
	.align 0x20,0 /* ret */
	.endr
	CFI_ENDPROC

#undef NEXT_PAGE

	__PAGE_ALIGNED_BSS
	.align PAGE_SIZE
ENTRY(empty_zero_page)
	.skip PAGE_SIZE

#if CONFIG_XEN_COMPAT <= 0x030002
/*
 * __xen_guest information
 */
.macro utoh value
 i = 64
 .rept 16
  i = i - 4
  .byte '0' + ((((\value) >> i) & 0xf) > 9) * ('0' - 'A' + 10) + (((\value) >> i) & 0xf)
 .endr
.endm

.section __xen_guest
	.ascii	"GUEST_OS=linux,GUEST_VER=2.6"
	.ascii	",XEN_VER=xen-3.0"
	.ascii	",VIRT_BASE=0x"
		utoh __START_KERNEL_map
	.ascii	",ELF_PADDR_OFFSET=0x"
		utoh __START_KERNEL_map
	.ascii	",VIRT_ENTRY=0x"
		utoh (__START_KERNEL_map + __PHYSICAL_START)
	.ascii	",HYPERCALL_PAGE=0x"
		utoh (phys_hypercall_page >> PAGE_SHIFT)
	.ascii  ",FEATURES=writable_page_tables"
	.ascii		 "|writable_descriptor_tables"
	.ascii		 "|auto_translated_physmap"
	.ascii	         "|supervisor_mode_kernel"
	.ascii	",LOADER=generic"
	.byte	0
#endif /* CONFIG_XEN_COMPAT <= 0x030002 */
	
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      .quad __START_KERNEL_map)
#if CONFIG_XEN_COMPAT <= 0x030002
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   .quad __START_KERNEL_map)
#else
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   .quad 0)
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          .quad startup_64)
	ELFNOTE(Xen, XEN_ELFNOTE_HYPERCALL_PAGE, .quad hypercall_page)
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,   .quad _PAGE_PRESENT, _PAGE_PRESENT)
	ELFNOTE(Xen, XEN_ELFNOTE_INIT_P2M,       .quad VMEMMAP_START)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,       .asciz "writable_page_tables|writable_descriptor_tables|auto_translated_physmap|pae_pgdir_above_4gb|supervisor_mode_kernel")
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
