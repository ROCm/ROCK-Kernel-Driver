/* Copyright 2002,2003 Andi Kleen, SuSE Labs */

/* vsyscall handling for 32bit processes. Map a stub page into it 
   on demand because 32bit cannot reach the kernel's fixmaps */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/stringify.h>
#include <asm/proto.h>
#include <asm/tlbflush.h>
#include <asm/ia32_unistd.h>

/* 32bit VDSOs mapped into user space. */ 
asm(".section \".init.data\",\"aw\"\n"
    "syscall32_syscall:\n"
    ".incbin \"arch/x86_64/ia32/vsyscall-syscall.so\"\n"
    "syscall32_syscall_end:\n"
    "syscall32_sysenter:\n"
    ".incbin \"arch/x86_64/ia32/vsyscall-sysenter.so\"\n"
    "syscall32_sysenter_end:\n"
    ".previous");

extern unsigned char syscall32_syscall[], syscall32_syscall_end[];
extern unsigned char syscall32_sysenter[], syscall32_sysenter_end[];
extern int sysctl_vsyscall32;

char *syscall32_page; 
static int use_sysenter __initdata = -1;

/* RED-PEN: This knows too much about high level VM */ 
/* Alternative would be to generate a vma with appropriate backing options
   and let it be handled by generic VM */ 
int map_syscall32(struct mm_struct *mm, unsigned long address) 
{ 
	pte_t *pte;
	pmd_t *pmd;
	int err = 0;

	down_read(&mm->mmap_sem);
	spin_lock(&mm->page_table_lock); 
	pmd = pmd_alloc(mm, pgd_offset(mm, address), address); 
	if (pmd && (pte = pte_alloc_map(mm, pmd, address)) != NULL) { 
		if (pte_none(*pte)) { 
			set_pte(pte, 
				mk_pte(virt_to_page(syscall32_page), 
				       PAGE_KERNEL_VSYSCALL)); 
		}
		/* Flush only the local CPU. Other CPUs taking a fault
		   will just end up here again */
		__flush_tlb_one(address); 
	} else
		err = -ENOMEM; 
	spin_unlock(&mm->page_table_lock);
	up_read(&mm->mmap_sem);
	return err;
}

static int __init init_syscall32(void)
{ 
	syscall32_page = (void *)get_zeroed_page(GFP_KERNEL); 
	if (!syscall32_page) 
		panic("Cannot allocate syscall32 page"); 
	SetPageReserved(virt_to_page(syscall32_page));
 	if (use_sysenter > 0) {
 		memcpy(syscall32_page, syscall32_sysenter,
 		       syscall32_sysenter_end - syscall32_sysenter);
 	} else {
  		memcpy(syscall32_page, syscall32_syscall,
  		       syscall32_syscall_end - syscall32_syscall);
  	}	
	return 0;
} 
	
__initcall(init_syscall32); 

void __init syscall32_cpu_init(void)
{
	if (use_sysenter < 0)
 		use_sysenter = (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL);

	/* Load these always in case some future AMD CPU supports
	   SYSENTER from compat mode too. */
	checking_wrmsrl(MSR_IA32_SYSENTER_CS, (u64)__KERNEL_CS);
	checking_wrmsrl(MSR_IA32_SYSENTER_ESP, 0ULL);
	checking_wrmsrl(MSR_IA32_SYSENTER_EIP, (u64)ia32_sysenter_target);

	wrmsrl(MSR_CSTAR, ia32_cstar_target);
}
