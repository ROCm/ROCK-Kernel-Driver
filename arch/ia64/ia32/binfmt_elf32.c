/*
 * IA-32 ELF support.
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 *
 * 06/16/00	A. Mallick	initialize csd/ssd/tssd/cflg for ia32_load_state
 */
#include <linux/config.h>

#include <linux/types.h>

#include <asm/param.h>
#include <asm/signal.h>
#include <asm/ia32.h>

#define CONFIG_BINFMT_ELF32

/* Override some function names */
#undef start_thread
#define start_thread			ia32_start_thread
#define elf_format			elf32_format
#define init_elf_binfmt			init_elf32_binfmt
#define exit_elf_binfmt			exit_elf32_binfmt

#undef CONFIG_BINFMT_ELF
#ifdef CONFIG_BINFMT_ELF32
# define CONFIG_BINFMT_ELF		CONFIG_BINFMT_ELF32
#endif

#undef CONFIG_BINFMT_ELF_MODULE
#ifdef CONFIG_BINFMT_ELF32_MODULE
# define CONFIG_BINFMT_ELF_MODULE	CONFIG_BINFMT_ELF32_MODULE
#endif

#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC	IA32_CLOCKS_PER_SEC

extern void ia64_elf32_init(struct pt_regs *regs);
extern void put_dirty_page(struct task_struct * tsk, struct page *page, unsigned long address);

#define ELF_PLAT_INIT(_r)		ia64_elf32_init(_r)
#define setup_arg_pages(bprm)		ia32_setup_arg_pages(bprm)
#define elf_map				elf_map32

/* Ugly but avoids duplication */
#include "../../../fs/binfmt_elf.c"

/* Global descriptor table */
unsigned long *ia32_gdt_table, *ia32_tss;

struct page *
put_shared_page(struct task_struct * tsk, struct page *page, unsigned long address)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte;

	if (page_count(page) != 1)
		printk("mem_map disagrees with %p at %08lx\n", (void *) page, address);
	pgd = pgd_offset(tsk->mm, address);
	pmd = pmd_alloc(pgd, address);
	if (!pmd) {
		__free_page(page);
		force_sig(SIGKILL, tsk);
		return 0;
	}
	pte = pte_alloc(pmd, address);
	if (!pte) {
		__free_page(page);
		force_sig(SIGKILL, tsk);
		return 0;
	}
	if (!pte_none(*pte)) {
		pte_ERROR(*pte);
		__free_page(page);
		return 0;
	}
	flush_page_to_ram(page);
	set_pte(pte, pte_mkwrite(mk_pte(page, PAGE_SHARED)));
	/* no need for flush_tlb */
	return page;
}

void ia64_elf32_init(struct pt_regs *regs)
{
	int nr;

	put_shared_page(current, virt_to_page(ia32_gdt_table), IA32_PAGE_OFFSET);
	if (PAGE_SHIFT <= IA32_PAGE_SHIFT)
		put_shared_page(current, virt_to_page(ia32_tss), IA32_PAGE_OFFSET + PAGE_SIZE);

	nr = smp_processor_id();
	
	/* Do all the IA-32 setup here */

	current->thread.map_base  =  0x40000000;
	current->thread.task_size =  0xc0000000;	/* use what Linux/x86 uses... */
 
	/* setup ia32 state for ia32_load_state */

	current->thread.eflag = IA32_EFLAG;
	current->thread.csd = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0xBL, 1L, 3L, 1L, 1L, 1L);
	current->thread.ssd = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	current->thread.tssd = IA64_SEG_DESCRIPTOR(IA32_PAGE_OFFSET + PAGE_SIZE, 0x1FFFL, 0xBL,
						   1L, 3L, 1L, 1L, 1L);

	/* CS descriptor */
	__asm__("mov ar.csd = %0" : /* no outputs */
		: "r" IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0xBL, 1L,
					  3L, 1L, 1L, 1L));
	/* SS descriptor */
	__asm__("mov ar.ssd = %0" : /* no outputs */
		: "r" IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L,
					  3L, 1L, 1L, 1L));
	/* EFLAGS */
	__asm__("mov ar.eflag = %0" : /* no outputs */ : "r" (IA32_EFLAG));

	/* Control registers */
	__asm__("mov ar.fsr = %0"
		: /* no outputs */
		: "r" ((ulong)IA32_FSR_DEFAULT));
	__asm__("mov ar.fcr = %0"
		: /* no outputs */
		: "r" ((ulong)IA32_FCR_DEFAULT));
	__asm__("mov ar.fir = r0");
	__asm__("mov ar.fdr = r0");
	__asm__("mov %0=ar.k0 ;;" : "=r" (current->thread.old_iob));
	__asm__("mov ar.k0=%0 ;;" :: "r"(IA32_IOBASE));
	/* TSS */
	__asm__("mov ar.k1 = %0"
		: /* no outputs */
		: "r" IA64_SEG_DESCRIPTOR(IA32_PAGE_OFFSET + PAGE_SIZE,
					  0x1FFFL, 0xBL, 1L,
					  3L, 1L, 1L, 1L));

	/* Get the segment selectors right */
	regs->r16 = (__USER_DS << 16) |  (__USER_DS); /* ES == DS, GS, FS are zero */
	regs->r17 = (_TSS(nr) << 48) | (_LDT(nr) << 32)
		    | (__USER_DS << 16) | __USER_CS;

	/* Setup other segment descriptors - ESD, DSD, FSD, GSD */
	regs->r24 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	regs->r27 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	regs->r28 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	regs->r29 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);

	/* Setup the LDT and GDT */
	regs->r30 = ia32_gdt_table[_LDT(nr)];
	regs->r31 = IA64_SEG_DESCRIPTOR(0xc0000000L, 0x400L, 0x3L, 1L, 3L,
					1L, 1L, 1L);

       	/* Clear psr.ac */
	regs->cr_ipsr &= ~IA64_PSR_AC;

	regs->loadrs = 0;
	/*
	 *  According to the ABI %edx points to an `atexit' handler.
	 *  Since we don't have one we'll set it to 0 and initialize
	 *  all the other registers just to make things more deterministic,
	 *  ala the i386 implementation.
	 */
	regs->r8 = 0;	/* %eax */
	regs->r11 = 0;	/* %ebx */
	regs->r9 = 0;	/* %ecx */
	regs->r10 = 0;	/* %edx */
	regs->r13 = 0;	/* %ebp */
	regs->r14 = 0;	/* %esi */
	regs->r15 = 0;	/* %edi */
}

#undef STACK_TOP
#define STACK_TOP ((IA32_PAGE_OFFSET/3) * 2)

int ia32_setup_arg_pages(struct linux_binprm *bprm)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	int i;

	stack_base = STACK_TOP - MAX_ARG_PAGES*PAGE_SIZE;

	bprm->p += stack_base;
	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!mpnt) 
		return -ENOMEM; 
	
	{
		mpnt->vm_mm = current->mm;
		mpnt->vm_start = PAGE_MASK & (unsigned long) bprm->p;
		mpnt->vm_end = STACK_TOP;
		mpnt->vm_page_prot = PAGE_COPY;
		mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_ops = NULL;
		mpnt->vm_pgoff = 0;
		mpnt->vm_file = NULL;
		mpnt->vm_private_data = 0;
		insert_vm_struct(current->mm, mpnt);
		current->mm->total_vm = (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT;
	} 

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		if (bprm->page[i]) {
			current->mm->rss++;
			put_dirty_page(current,bprm->page[i],stack_base);
		}
		stack_base += PAGE_SIZE;
	}
	
	return 0;
}

static unsigned long
ia32_mm_addr(unsigned long addr)
{
	struct vm_area_struct *vma;

	if ((vma = find_vma(current->mm, addr)) == NULL)
		return(ELF_PAGESTART(addr));
	if (vma->vm_start > addr)
		return(ELF_PAGESTART(addr));
	return(ELF_PAGEALIGN(addr));
}

/*
 *  Normally we would do an `mmap' to map in the process's text section.
 *  This doesn't work with IA32 processes as the ELF file might specify
 *  a non page size aligned address.  Instead we will just allocate
 *  memory and read the data in from the file.  Slightly less efficient
 *  but it works.
 */
extern long ia32_do_mmap (struct file *filep, unsigned int len, unsigned int prot,
			  unsigned int flags, unsigned int fd, unsigned int offset);

static unsigned long
elf_map32 (struct file *filep, unsigned long addr, struct elf_phdr *eppnt, int prot, int type)
{
	unsigned long retval;

	if (eppnt->p_memsz >= (1UL<<32) || addr > (1UL<<32) - eppnt->p_memsz)
		return -EINVAL;

	/*
	 *  Make sure the elf interpreter doesn't get loaded at location 0
	 *    so that NULL pointers correctly cause segfaults.
	 */
	if (addr == 0)
		addr += PAGE_SIZE;
#if 1
	set_brk(ia32_mm_addr(addr), addr + eppnt->p_memsz);
	memset((char *) addr + eppnt->p_filesz, 0, eppnt->p_memsz - eppnt->p_filesz);
	kernel_read(filep, eppnt->p_offset, (char *) addr, eppnt->p_filesz);
	retval = (unsigned long) addr;
#else
	/* doesn't work yet... */
#	define IA32_PAGESTART(_v) ((_v) & ~(unsigned long)(ELF_EXEC_PAGESIZE-1))
#	define IA32_PAGEOFFSET(_v) ((_v) & (ELF_EXEC_PAGESIZE-1))
#	define IA32_PAGEALIGN(_v) (((_v) + ELF_EXEC_PAGESIZE - 1) & ~(ELF_EXEC_PAGESIZE - 1))

	down(&current->mm->mmap_sem);
	retval = ia32_do_mmap(filep, IA32_PAGESTART(addr),
			      eppnt->p_filesz + IA32_PAGEOFFSET(eppnt->p_vaddr), prot, type,
			      eppnt->p_offset - IA32_PAGEOFFSET(eppnt->p_vaddr));
	up(&current->mm->mmap_sem);
#endif
	return retval;
}
