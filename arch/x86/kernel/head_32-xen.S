

.text
#include <linux/elfnote.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/boot.h>
#include <asm/dwarf2.h>
#include <asm/percpu.h>
#include <xen/interface/xen.h>
#include <xen/interface/elfnote.h>

/*
 * References to members of the new_cpu_data structure.
 */

#define X86		new_cpu_data+CPUINFO_x86
#define X86_VENDOR	new_cpu_data+CPUINFO_x86_vendor
#define X86_MODEL	new_cpu_data+CPUINFO_x86_model
#define X86_MASK	new_cpu_data+CPUINFO_x86_mask
#define X86_HARD_MATH	new_cpu_data+CPUINFO_hard_math
#define X86_CPUID	new_cpu_data+CPUINFO_cpuid_level
#define X86_CAPABILITY	new_cpu_data+CPUINFO_x86_capability
#define X86_VENDOR_ID	new_cpu_data+CPUINFO_x86_vendor_id

__HEAD
#define VIRT_ENTRY_OFFSET 0x0
.org VIRT_ENTRY_OFFSET
ENTRY(startup_32)
	movl %esi,xen_start_info
	cld

	/* Set up the stack pointer */
	movl $(init_thread_union+THREAD_SIZE),%esp

	/* get vendor info */
	xorl %eax,%eax			# call CPUID with 0 -> return vendor ID
	XEN_CPUID
	movl %eax,X86_CPUID		# save CPUID level
	movl %ebx,X86_VENDOR_ID		# lo 4 chars
	movl %edx,X86_VENDOR_ID+4	# next 4 chars
	movl %ecx,X86_VENDOR_ID+8	# last 4 chars

	movl $1,%eax		# Use the CPUID instruction to get CPU type
	XEN_CPUID
	movb %al,%cl		# save reg for future use
	andb $0x0f,%ah		# mask processor family
	movb %ah,X86
	andb $0xf0,%al		# mask model
	shrb $4,%al
	movb %al,X86_MODEL
	andb $0x0f,%cl		# mask mask revision
	movb %cl,X86_MASK
	movl %edx,X86_CAPABILITY

	movb $1,X86_HARD_MATH

#ifdef CONFIG_CC_STACKPROTECTOR
	/*
	 * The linker can't handle this by relocation.  Manually set
	 * base address in stack canary segment descriptor.
	 */
	movl $per_cpu__gdt_page,%eax
	movl $per_cpu__stack_canary,%ecx
	movw %cx, 8 * GDT_ENTRY_STACK_CANARY + 2(%eax)
	shrl $16, %ecx
	movb %cl, 8 * GDT_ENTRY_STACK_CANARY + 4(%eax)
	movb %ch, 8 * GDT_ENTRY_STACK_CANARY + 7(%eax)
#endif

	# %esi still points to start_info, and no registers
	# need to be preserved.

	movl XEN_START_mfn_list(%esi), %ebx
	movl $(per_cpu__gdt_page - __PAGE_OFFSET), %eax
	shrl $PAGE_SHIFT, %eax
	movl (%ebx,%eax,4), %ecx
	pushl %ecx			# frame number for set_gdt below

	xorl %esi, %esi
	xorl %edx, %edx
	shldl $PAGE_SHIFT, %ecx, %edx
	shll $PAGE_SHIFT, %ecx
	orl $_PAGE_PRESENT|_PAGE_ACCESSED|_PAGE_DIRTY, %ecx
	movl $per_cpu__gdt_page, %ebx
	movl $__HYPERVISOR_update_va_mapping, %eax
	int $0x82

	movl $(PAGE_SIZE_asm / 8), %ecx
	movl %esp, %ebx
	movl $__HYPERVISOR_set_gdt, %eax
	int $0x82

	popl %ecx

	movl $(__KERNEL_PERCPU), %eax
	movl %eax,%fs			# set this cpu's percpu

	movl $(__KERNEL_STACK_CANARY),%eax
	movl %eax,%gs

	cld			# gcc2 wants the direction flag cleared at all times

	pushl $0		# fake return address for unwinder
	jmp i386_start_kernel

#define HYPERCALL_PAGE_OFFSET 0x1000
.org HYPERCALL_PAGE_OFFSET
ENTRY(hypercall_page)
	CFI_STARTPROC
.skip 0x1000
	CFI_ENDPROC

/*
 * BSS section
 */
__PAGE_ALIGNED_BSS
	.align PAGE_SIZE_asm
ENTRY(swapper_pg_fixmap)
	.fill 1024,4,0
ENTRY(empty_zero_page)
	.fill 4096,1,0

/*
 * This starts the data section.
 */
.data

#if CONFIG_XEN_COMPAT <= 0x030002
/*
 * __xen_guest information
 */
.macro utoa value
 .if (\value) < 0 || (\value) >= 0x10
	utoa (((\value)>>4)&0x0fffffff)
 .endif
 .if ((\value) & 0xf) < 10
  .byte '0' + ((\value) & 0xf)
 .else
  .byte 'A' + ((\value) & 0xf) - 10
 .endif
.endm

.section __xen_guest
	.ascii	"GUEST_OS=linux,GUEST_VER=2.6"
	.ascii	",XEN_VER=xen-3.0"
	.ascii	",VIRT_BASE=0x"
		utoa __PAGE_OFFSET
	.ascii	",ELF_PADDR_OFFSET=0x"
		utoa __PAGE_OFFSET
	.ascii	",VIRT_ENTRY=0x"
		utoa (__PAGE_OFFSET + LOAD_PHYSICAL_ADDR + VIRT_ENTRY_OFFSET)
	.ascii	",HYPERCALL_PAGE=0x"
		utoa ((LOAD_PHYSICAL_ADDR+HYPERCALL_PAGE_OFFSET)>>PAGE_SHIFT)
	.ascii  ",FEATURES=writable_page_tables"
	.ascii	         "|writable_descriptor_tables"
	.ascii	         "|auto_translated_physmap"
	.ascii	         "|pae_pgdir_above_4gb"
	.ascii	         "|supervisor_mode_kernel"
#ifdef CONFIG_X86_PAE
	.ascii	",PAE=yes[extended-cr3]"
#else
	.ascii	",PAE=no"
#endif
	.ascii	",LOADER=generic"
	.byte	0
#endif /* CONFIG_XEN_COMPAT <= 0x030002 */


	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      .long __PAGE_OFFSET)
#if CONFIG_XEN_COMPAT <= 0x030002
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   .long __PAGE_OFFSET)
#else
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   .long 0)
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          .long startup_32)
	ELFNOTE(Xen, XEN_ELFNOTE_HYPERCALL_PAGE, .long hypercall_page)
	ELFNOTE(Xen, XEN_ELFNOTE_HV_START_LOW,   .long HYPERVISOR_VIRT_START)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,       .asciz "writable_page_tables|writable_descriptor_tables|auto_translated_physmap|pae_pgdir_above_4gb|supervisor_mode_kernel")
#ifdef CONFIG_X86_PAE
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,   .quad _PAGE_PRESENT, _PAGE_PRESENT)
#else
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "no")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,   .long _PAGE_PRESENT, _PAGE_PRESENT)
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
