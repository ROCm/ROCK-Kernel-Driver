#include <linux/config.h>
#include <linux/types.h>
#include <linux/binfmts.h>
#include <asm/param.h>
#include <asm/signal.h>

#include <asm/ilp32.h>

/* Override some function names */
#undef	start_thread
#define	start_thread			ilp32_elf_start_thread
#define	elf_format			ilp32_elf_format
#define	init_elf_binfmt			init_ilp32_elf_binfmt
#define	exit_elf_binfmt			exit_ilp32_elf_binfmt

struct linux_binprm; struct elf32_hdr;
static int ilp32_elf_setup_arg_pages (struct linux_binprm *);
static void ilp32_elf_set_personality (struct elf32_hdr *, unsigned char);
void ilp32_init_addr_space(void);

#undef	ELF_PLAT_INIT
#define ELF_PLAT_INIT(_r,d) ilp32_init_addr_space()
#define setup_arg_pages(bprm)	ilp32_elf_setup_arg_pages(bprm)
#undef SET_PERSONALITY
#define SET_PERSONALITY(ex, ibcs2)	ilp32_elf_set_personality(&(ex), ibcs2)
#undef ELF_CORE_WRITE_EXTRA_DATA
#undef ELF_CORE_WRITE_EXTRA_PHDRS
#undef ARCH_DLINFO
#undef ELF_CORE_EXTRA_PHDRS
#include "../../../fs/binfmt_elf.c"

static int ilp32_elf_setup_arg_pages (struct linux_binprm *bprm)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	struct mm_struct *mm = current->mm;
	int i;

	stack_base = ILP32_STACK_TOP - MAX_ARG_PAGES*PAGE_SIZE;

	bprm->p += stack_base;
	mm->arg_start = bprm->p;

	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!mpnt)
		return -ENOMEM;

	down_write(&current->mm->mmap_sem);
	{
		mpnt->vm_mm = current->mm;
		mpnt->vm_start = PAGE_MASK & (unsigned long) bprm->p;
		mpnt->vm_end = ILP32_STACK_TOP;
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
		struct page *page = bprm->page[i];
		if (page) {
			bprm->page[i] = NULL;
			put_dirty_page(current, page, stack_base, PAGE_COPY, mpnt);
		}
		stack_base += PAGE_SIZE;
	}
	up_write(&current->mm->mmap_sem);

	return 0;
}

static void ilp32_elf_set_personality (struct elfhdr *elf_ex, unsigned char exec_stack)
{
	set_personality(PER_LINUX_32BIT);
	current->thread.map_base  = ILP32_MMAP_BASE;
	current->thread.task_size = ILP32_PAGE_OFFSET;
	if (elf_ex->e_flags & EF_IA_64_LINUX_EXECUTABLE_STACK)
		current->thread.flags |= IA64_THREAD_XSTACK;
	else
		current->thread.flags &= ~IA64_THREAD_XSTACK;
	set_fs(USER_DS);
}

void ilp32_init_addr_space()
{
	struct vm_area_struct *vma;

	/*
	 * If we're out of memory and kmem_cache_alloc() returns NULL, we simply ignore
	 * the problem.  When the process attempts to write to the register backing store
	 * for the first time, it will get a SEGFAULT in this case.
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = ILP32_RBS_BOT;
		vma->vm_end = vma->vm_start + PAGE_SIZE;
		vma->vm_page_prot = PAGE_COPY;
		vma->vm_flags = VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE|VM_GROWSUP;
		vma->vm_ops = NULL;
		vma->vm_pgoff = 0;
		vma->vm_file = NULL;
		vma->vm_private_data = NULL;
		insert_vm_struct(current->mm, vma);
	}

	/* map NaT-page at address zero to speed up speculative dereferencing of NULL: */
	if (!(current->personality & MMAP_PAGE_ZERO)) {
		vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (vma) {
			memset(vma, 0, sizeof(*vma));
			vma->vm_mm = current->mm;
			vma->vm_end = PAGE_SIZE;
			vma->vm_page_prot = __pgprot(pgprot_val(PAGE_READONLY) | _PAGE_MA_NAT);
			vma->vm_flags = VM_READ | VM_MAYREAD | VM_IO | VM_RESERVED;
			insert_vm_struct(current->mm, vma);
		}
	}
}

