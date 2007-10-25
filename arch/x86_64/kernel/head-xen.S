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
#include <asm/desc.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/cache.h>
#include <asm/dwarf2.h>
#include <xen/interface/elfnote.h>

	.section .text.head, "ax", @progbits
	.code64
	.globl startup_64
startup_64:
	movq $(init_thread_union+THREAD_SIZE-8),%rsp

	/* rsi is pointer to startup info structure.
	   pass it to C */
	movq %rsi,%rdi
	pushq $0		# fake return address
	jmp x86_64_start_kernel

#ifdef CONFIG_ACPI_SLEEP
.org 0xf00
	.globl pGDT32
pGDT32:
	.word	gdt_end-cpu_gdt_table-1
	.long	cpu_gdt_table-__START_KERNEL_map
#endif

.balign PAGE_SIZE

#define NEXT_PAGE(name) \
	.balign	PAGE_SIZE; \
	phys_##name = . - .text.head; \
ENTRY(name)

NEXT_PAGE(init_level4_pgt)
	.fill	512,8,0
        /*
         * We update two pgd entries to make kernel and user pgd consistent
         * at pgd_populate(). It can be used for kernel modules. So we place 
         * this page here for those cases to avoid memory corruption.
         * We also use this page to establish the initial mapping for the
         * vsyscall area.
         */
	.fill	512,8,0

NEXT_PAGE(level3_kernel_pgt)
	.fill	512,8,0

        /*
         * This is used for vsyscall area mapping as we have a different
         * level4 page table for user.
         */
NEXT_PAGE(level3_user_pgt)
        .fill	512,8,0

NEXT_PAGE(level2_kernel_pgt)
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

	.data
/* Just dummy symbol to allow compilation. Not used in sleep path */
#ifdef CONFIG_ACPI_SLEEP
	.align PAGE_SIZE
ENTRY(wakeup_level4_pgt)
	.fill	512,8,0
#endif

	.data

	.align 16
	.globl cpu_gdt_descr
cpu_gdt_descr:
	.word	gdt_end-cpu_gdt_table-1
gdt:
	.quad	cpu_gdt_table
#ifdef CONFIG_SMP
	.rept	NR_CPUS-1
	.word	0
	.quad	0
	.endr
#endif

/* We need valid kernel segments for data and code in long mode too
 * IRET will check the segment types  kkeil 2000/10/28
 * Also sysret mandates a special GDT layout 
 */
		 		
	.section .data.page_aligned, "aw"
	.align PAGE_SIZE

/* The TLS descriptors are currently at a different place compared to i386.
   Hopefully nobody expects them at a fixed place (Wine?) */

ENTRY(cpu_gdt_table)
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00cf9b000000ffff	/* __KERNEL32_CS */
	.quad	0x00af9b000000ffff	/* __KERNEL_CS */
	.quad	0x00cf93000000ffff	/* __KERNEL_DS */
	.quad	0x00cffb000000ffff	/* __USER32_CS */
	.quad	0x00cff3000000ffff	/* __USER_DS, __USER32_DS  */
	.quad	0x00affb000000ffff	/* __USER_CS */
	.quad	0x0			/* unused */
	.quad	0,0			/* TSS */
	.quad	0,0			/* LDT */
	.quad   0,0,0			/* three TLS descriptors */
	.quad	0x0000f40000000000	/* node/CPU stored in limit */
gdt_end:
	/* asm/segment.h:GDT_ENTRIES must match this */
	/* This should be a multiple of the cache line size */
	/* GDTs of other CPUs are now dynamically allocated */

	/* zero the remaining page */
	.fill PAGE_SIZE / 8 - GDT_ENTRIES,8,0

	.section .bss.page_aligned, "aw", @nobits
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
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,       .asciz "writable_page_tables|writable_descriptor_tables|auto_translated_physmap|pae_pgdir_above_4gb|supervisor_mode_kernel")
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
