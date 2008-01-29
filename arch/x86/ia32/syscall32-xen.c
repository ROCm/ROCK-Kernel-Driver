/* Copyright 2002,2003 Andi Kleen, SuSE Labs */

/* vsyscall handling for 32bit processes. Map a stub page into it 
   on demand because 32bit cannot reach the kernel's fixmaps */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/security.h>
#include <asm/proto.h>
#include <asm/tlbflush.h>
#include <asm/ia32_unistd.h>
#include <asm/vsyscall32.h>
#include <xen/interface/callback.h>

extern unsigned char syscall32_syscall[], syscall32_syscall_end[];
extern unsigned char syscall32_sysenter[], syscall32_sysenter_end[];
extern int sysctl_vsyscall32;

static struct page *syscall32_pages[1];
static int use_sysenter = -1;

#if CONFIG_XEN_COMPAT < 0x030200
extern unsigned char syscall32_int80[], syscall32_int80_end[];
static int use_int80 = 1;
#endif

struct linux_binprm;

/* Setup a VMA at program startup for the vsyscall page */
int syscall32_setup_pages(struct linux_binprm *bprm, int exstack)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_write(&mm->mmap_sem);
	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 *
	 * Make sure the vDSO gets into every core dump.
	 * Dumping its contents makes post-mortem fully interpretable later
	 * without matching up the same kernel and hardware config to see
	 * what PC values meant.
	 */
	/* Could randomize here */
	ret = install_special_mapping(mm, VSYSCALL32_BASE, PAGE_SIZE,
				      VM_READ|VM_EXEC|
				      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
				      VM_ALWAYSDUMP,
				      syscall32_pages);
	up_write(&mm->mmap_sem);
	return ret;
}

static int __init init_syscall32(void)
{ 
	char *syscall32_page = (void *)get_zeroed_page(GFP_KERNEL);
	if (!syscall32_page) 
		panic("Cannot allocate syscall32 page"); 

	syscall32_pages[0] = virt_to_page(syscall32_page);
#if CONFIG_XEN_COMPAT < 0x030200
	if (use_int80) {
		memcpy(syscall32_page, syscall32_int80,
		       syscall32_int80_end - syscall32_int80);
	} else
#endif
 	if (use_sysenter > 0) {
 		memcpy(syscall32_page, syscall32_sysenter,
 		       syscall32_sysenter_end - syscall32_sysenter);
 	} else {
  		memcpy(syscall32_page, syscall32_syscall,
  		       syscall32_syscall_end - syscall32_syscall);
  	}	
	return 0;
} 

/*
 * This must be done early in case we have an initrd containing 32-bit
 * binaries (e.g., hotplug). This could be pushed upstream to arch/x86_64.
 */	
core_initcall(init_syscall32); 

/* May not be __init: called during resume */
void syscall32_cpu_init(void)
{
	static const struct callback_register cstar = {
		.type = CALLBACKTYPE_syscall32,
		.address = (unsigned long)ia32_cstar_target
	};
	static const struct callback_register sysenter = {
		.type = CALLBACKTYPE_sysenter,
		.address = (unsigned long)ia32_sysenter_target
	};

	/* Load these always in case some future AMD CPU supports
	   SYSENTER from compat mode too. */
	if ((HYPERVISOR_callback_op(CALLBACKOP_register, &sysenter) < 0) ||
	    (HYPERVISOR_callback_op(CALLBACKOP_register, &cstar) < 0))
#if CONFIG_XEN_COMPAT < 0x030200
		return;
	use_int80 = 0;
#else
		BUG();
#endif

	if (use_sysenter < 0)
		use_sysenter = (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL);
}
