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

/* 32bit SYSCALL stub mapped into user space. */ 
asm("	.code32\n"
    "\nsyscall32:\n"
    "	pushl %ebp\n"
    "	movl  %ecx,%ebp\n"
    "	syscall\n"
    "	popl  %ebp\n"
    "	ret\n"
    "syscall32_end:\n"

    /* signal trampolines */

    "sig32_rt_tramp:\n"
    "	movl $"  __stringify(__NR_ia32_rt_sigreturn) ",%eax\n"
    "   syscall\n"
    "sig32_rt_tramp_end:\n"

    "sig32_tramp:\n"
    "	popl %eax\n"
    "	movl $"  __stringify(__NR_ia32_sigreturn) ",%eax\n"
    "	syscall\n"
    "sig32_tramp_end:\n"
    "	.code64\n"); 

extern unsigned char syscall32[], syscall32_end[];
extern unsigned char sig32_rt_tramp[], sig32_rt_tramp_end[];
extern unsigned char sig32_tramp[], sig32_tramp_end[];

char *syscall32_page; 

/* RED-PEN: This knows too much about high level VM */ 
/* Alternative would be to generate a vma with appropriate backing options
   and let it be handled by generic VM */ 
int map_syscall32(struct mm_struct *mm, unsigned long address) 
{ 
	pte_t *pte;
	int err = 0;
	down_read(&mm->mmap_sem);
	spin_lock(&mm->page_table_lock); 
	pmd_t *pmd = pmd_alloc(mm, pgd_offset(mm, address), address); 
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
	memcpy(syscall32_page, syscall32, syscall32_end - syscall32);
	memcpy(syscall32_page + 32, sig32_rt_tramp, 
	       sig32_rt_tramp_end - sig32_rt_tramp);
	memcpy(syscall32_page + 64, sig32_tramp, 
	       sig32_tramp_end - sig32_tramp);	
	return 0;
} 
	
__initcall(init_syscall32); 
