/*
 * IA-32 ELF support.
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2001 Hewlett-Packard Co
 * Copyright (C) 2001 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 06/16/00	A. Mallick	initialize csd/ssd/tssd/cflg for ia32_load_state
 * 04/13/01	D. Mosberger	dropped saving tssd in ar.k1---it's not needed
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

extern void ia64_elf32_init (struct pt_regs *regs);
extern void put_dirty_page (struct task_struct * tsk, struct page *page, unsigned long address);

#define ELF_PLAT_INIT(_r)		ia64_elf32_init(_r)
#define setup_arg_pages(bprm)		ia32_setup_arg_pages(bprm)
#define elf_map				elf_map32

/* Ugly but avoids duplication */
#include "../../../fs/binfmt_elf.c"

/* Global descriptor table */
unsigned long *ia32_gdt_table, *ia32_tss;

struct page *
put_shared_page (struct task_struct * tsk, struct page *page, unsigned long address)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte;

	if (page_count(page) != 1)
		printk("mem_map disagrees with %p at %08lx\n", (void *) page, address);

	pgd = pgd_offset(tsk->mm, address);

	spin_lock(&tsk->mm->page_table_lock);
	{
		pmd = pmd_alloc(tsk->mm, pgd, address);
		if (!pmd)
			goto out;
		pte = pte_alloc(tsk->mm, pmd, address);
		if (!pte)
			goto out;
		if (!pte_none(*pte))
			goto out;
		flush_page_to_ram(page);
		set_pte(pte, pte_mkwrite(mk_pte(page, PAGE_SHARED)));
	}
	spin_unlock(&tsk->mm->page_table_lock);
	/* no need for flush_tlb */
	return page;

  out:
	spin_unlock(&tsk->mm->page_table_lock);
	__free_page(page);
	return 0;
}

void
ia64_elf32_init (struct pt_regs *regs)
{
	struct vm_area_struct *vma;
	int nr;

	/*
	 * Map GDT and TSS below 4GB, where the processor can find them.  We need to map
	 * it with privilege level 3 because the IVE uses non-privileged accesses to these
	 * tables.  IA-32 segmentation is used to protect against IA-32 accesses to them.
	 */
	put_shared_page(current, virt_to_page(ia32_gdt_table), IA32_GDT_OFFSET);
	if (PAGE_SHIFT <= IA32_PAGE_SHIFT)
		put_shared_page(current, virt_to_page(ia32_tss), IA32_TSS_OFFSET);

	/*
	 * Install LDT as anonymous memory.  This gives us all-zero segment descriptors
	 * until a task modifies them via modify_ldt().
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = IA32_LDT_OFFSET;
		vma->vm_end = vma->vm_start + PAGE_ALIGN(IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE);
		vma->vm_page_prot = PAGE_SHARED;
		vma->vm_flags = VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE;
		vma->vm_ops = NULL;
		vma->vm_pgoff = 0;
		vma->vm_file = NULL;
		vma->vm_private_data = NULL;
		insert_vm_struct(current->mm, vma);
	}

	nr = smp_processor_id();

	current->thread.map_base  = IA32_PAGE_OFFSET/3;
	current->thread.task_size = IA32_PAGE_OFFSET;	/* use what Linux/x86 uses... */
	set_fs(USER_DS);				/* set addr limit for new TASK_SIZE */

	/* Setup the segment selectors */
	regs->r16 = (__USER_DS << 16) | __USER_DS; /* ES == DS, GS, FS are zero */
	regs->r17 = (__USER_DS << 16) | __USER_CS; /* SS, CS; ia32_load_state() sets TSS and LDT */

	/* Setup the segment descriptors */
	regs->r24 = IA32_SEG_UNSCRAMBLE(ia32_gdt_table[__USER_DS >> 3]);	/* ESD */
	regs->r27 = IA32_SEG_UNSCRAMBLE(ia32_gdt_table[__USER_DS >> 3]);	/* DSD */
	regs->r28 = 0;								/* FSD (null) */
	regs->r29 = 0;								/* GSD (null) */
	regs->r30 = IA32_SEG_UNSCRAMBLE(ia32_gdt_table[_LDT(nr)]);		/* LDTD */

	/*
	 * Setup GDTD.  Note: GDTD is the descrambled version of the pseudo-descriptor
	 * format defined by Figure 3-11 "Pseudo-Descriptor Format" in the IA-32
	 * architecture manual.
	 */
	regs->r31 = IA32_SEG_UNSCRAMBLE(IA32_SEG_DESCRIPTOR(IA32_GDT_OFFSET, IA32_PAGE_SIZE - 1, 0,
							    0, 0, 0, 0, 0, 0));

	ia64_psr(regs)->ac = 0;		/* turn off alignment checking */
	regs->loadrs = 0;
	/*
	 *  According to the ABI %edx points to an `atexit' handler.  Since we don't have
	 *  one we'll set it to 0 and initialize all the other registers just to make
	 *  things more deterministic, ala the i386 implementation.
	 */
	regs->r8 = 0;	/* %eax */
	regs->r11 = 0;	/* %ebx */
	regs->r9 = 0;	/* %ecx */
	regs->r10 = 0;	/* %edx */
	regs->r13 = 0;	/* %ebp */
	regs->r14 = 0;	/* %esi */
	regs->r15 = 0;	/* %edi */

	current->thread.eflag = IA32_EFLAG;
	current->thread.fsr = IA32_FSR_DEFAULT;
	current->thread.fcr = IA32_FCR_DEFAULT;
	current->thread.fir = 0;
	current->thread.fdr = 0;
	current->thread.csd = IA32_SEG_UNSCRAMBLE(ia32_gdt_table[__USER_CS >> 3]);
	current->thread.ssd = IA32_SEG_UNSCRAMBLE(ia32_gdt_table[__USER_DS >> 3]);
	current->thread.tssd = IA32_SEG_UNSCRAMBLE(ia32_gdt_table[_TSS(nr)]);

	ia32_load_state(current);
}

int
ia32_setup_arg_pages (struct linux_binprm *bprm)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	int i;

	stack_base = IA32_STACK_TOP - MAX_ARG_PAGES*PAGE_SIZE;

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
		mpnt->vm_end = IA32_STACK_TOP;
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
			put_dirty_page(current,bprm->page[i],stack_base);
		}
		stack_base += PAGE_SIZE;
	}

	return 0;
}

static unsigned long
ia32_mm_addr (unsigned long addr)
{
	struct vm_area_struct *vma;

	if ((vma = find_vma(current->mm, addr)) == NULL)
		return ELF_PAGESTART(addr);
	if (vma->vm_start > addr)
		return ELF_PAGESTART(addr);
	return ELF_PAGEALIGN(addr);
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
	set_brk(ia32_mm_addr(addr), addr + eppnt->p_memsz);
	memset((char *) addr + eppnt->p_filesz, 0, eppnt->p_memsz - eppnt->p_filesz);
	kernel_read(filep, eppnt->p_offset, (char *) addr, eppnt->p_filesz);
	retval = (unsigned long) addr;
	return retval;
}
