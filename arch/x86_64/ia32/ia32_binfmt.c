/* 
 * Written 2000 by Andi Kleen. 
 * 
 * Losely based on the sparc64 and IA64 32bit emulation loaders.
 */ 
#include <linux/types.h>
#include <linux/config.h> 
#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <asm/segment.h> 
#include <asm/ptrace.h>
#include <asm/processor.h>

struct file;
struct elf_phdr; 

#define IA32_EMULATOR 1

#define IA32_PAGE_OFFSET 0xE0000000
#define IA32_STACK_TOP IA32_PAGE_OFFSET
#define ELF_ET_DYN_BASE		(IA32_PAGE_OFFSET/3 + 0x1000000)

#undef ELF_ARCH
#define ELF_ARCH EM_386

#undef ELF_CLASS
#define ELF_CLASS ELFCLASS32

#define ELF_DATA	ELFDATA2LSB
//#define USE_ELF_CORE_DUMP

#define __ASM_X86_64_ELF_H 1
#include <asm/ia32.h>
#include <linux/elf.h>

typedef __u32  elf_greg_t;

typedef elf_greg_t elf_gregset_t[8];

/* FIXME -- wrong */
typedef struct user_i387_ia32_struct elf_fpregset_t;
typedef struct user_i387_struct elf_fpxregset_t;

#undef elf_check_arch
#define elf_check_arch(x) \
	((x)->e_machine == EM_386)

#define ELF_EXEC_PAGESIZE PAGE_SIZE
#define ELF_HWCAP (boot_cpu_data.x86_capability[0])
#define ELF_PLATFORM  ("i686")
#define SET_PERSONALITY(ex, ibcs2)			\
do {							\
	set_personality((ibcs2)?PER_SVR4:current->personality);	\
} while (0)

/* Override some function names */
#define elf_format			elf32_format

#define init_elf_binfmt			init_elf32_binfmt
#define exit_elf_binfmt			exit_elf32_binfmt

#define load_elf_binary load_elf32_binary

#undef CONFIG_BINFMT_ELF
#ifdef CONFIG_BINFMT_ELF32
# define CONFIG_BINFMT_ELF		CONFIG_BINFMT_ELF32
#endif

#undef CONFIG_BINFMT_ELF_MODULE
#ifdef CONFIG_BINFMT_ELF32_MODULE
# define CONFIG_BINFMT_ELF_MODULE	CONFIG_BINFMT_ELF32_MODULE
#endif

#define ELF_PLAT_INIT(r)		elf32_init(r)
#define setup_arg_pages(bprm)		ia32_setup_arg_pages(bprm)

#undef start_thread
#define start_thread(regs,new_rip,new_rsp) do { \
	__asm__("movl %0,%%fs": :"r" (0)); \
	__asm__("movl %0,%%es; movl %0,%%ds": :"r" (__USER32_DS)); \
	wrmsrl(MSR_KERNEL_GS_BASE, 0); \
	(regs)->rip = (new_rip); \
	(regs)->rsp = (new_rsp); \
	(regs)->eflags = 0x200; \
	(regs)->cs = __USER32_CS; \
	(regs)->ss = __USER32_DS; \
	set_fs(USER_DS); \
} while(0) 


#define elf_map elf32_map

MODULE_DESCRIPTION("Binary format loader for compatibility with IA32 ELF binaries."); 
MODULE_AUTHOR("Eric Youngdale, Andi Kleen");

#undef MODULE_DESCRIPTION
#undef MODULE_AUTHOR

#define elf_addr_t __u32
#define elf_caddr_t __u32

static void elf32_init(struct pt_regs *);

#include "../../../fs/binfmt_elf.c" 

static void elf32_init(struct pt_regs *regs)
{
	struct task_struct *me = current; 
	regs->rdi = 0;
	regs->rsi = 0;
	regs->rdx = 0;
	regs->rcx = 0;
	regs->rax = 0;
	regs->rbx = 0; 
	regs->rbp = 0; 
    me->thread.fs = 0; 
	me->thread.gs = 0;
	me->thread.fsindex = 0; 
	me->thread.gsindex = 0;
    me->thread.ds = __USER_DS; 
	me->thread.es = __USER_DS;
	set_thread_flag(TIF_IA32); 
}

extern void put_dirty_page(struct task_struct * tsk, struct page *page, unsigned long address);
 

int ia32_setup_arg_pages(struct linux_binprm *bprm)
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
	
	down_write(&current->mm->mmap_sem);
	{
		mpnt->vm_mm = current->mm;
		mpnt->vm_start = PAGE_MASK & (unsigned long) bprm->p;
		mpnt->vm_end = IA32_STACK_TOP;
		mpnt->vm_page_prot = PAGE_COPY;
		mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_ops = NULL;
		mpnt->vm_pgoff = 0;
		mpnt->vm_file = NULL;
		mpnt->vm_private_data = (void *) 0;
		insert_vm_struct(current->mm, mpnt);
		current->mm->total_vm = (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT;
	} 

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page *page = bprm->page[i];
		if (page) {
			bprm->page[i] = NULL;
			current->mm->rss++;
			put_dirty_page(current,page,stack_base);
		}
		stack_base += PAGE_SIZE;
	}
	up_write(&current->mm->mmap_sem);
	
	return 0;
}
static unsigned long
elf32_map (struct file *filep, unsigned long addr, struct elf_phdr *eppnt, int prot, int type)
{
	unsigned long map_addr;
	struct task_struct *me = current; 

	down_write(&me->mm->mmap_sem);
	map_addr = do_mmap(filep, ELF_PAGESTART(addr),
			   eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr), prot, type|MAP_32BIT,
			   eppnt->p_offset - ELF_PAGEOFFSET(eppnt->p_vaddr));
	up_write(&me->mm->mmap_sem);
	return(map_addr);
}

