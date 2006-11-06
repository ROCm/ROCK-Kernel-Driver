/*
 * linux/arch/i386/kernel/sysenter.c
 *
 * (C) Copyright 2002 Linus Torvalds
 * Portions based on the vdso-randomization code from exec-shield:
 * Copyright(C) 2005-2006, Red Hat, Inc., Ingo Molnar
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = 1;

EXPORT_SYMBOL_GPL(vdso_enabled);

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);

	return 1;
}

__setup("vdso=", vdso_setup);

extern asmlinkage void sysenter_entry(void);

#ifdef CONFIG_XEN
#include <xen/interface/callback.h>

static struct callback_register __initdata sysenter_cb = {
	.type = CALLBACKTYPE_sysenter,
	.address = { __KERNEL_CS, (unsigned long)sysenter_entry },
};
#endif

void enable_sep_cpu(void)
{
#ifndef CONFIG_X86_NO_TSS
	int cpu = get_cpu();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		put_cpu();
		return;
	}

	tss->ss1 = __KERNEL_CS;
	tss->esp1 = sizeof(struct tss_struct) + (unsigned long) tss;
	wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, tss->esp1, 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long) sysenter_entry, 0);
	put_cpu();	
#endif
}

#if defined(CONFIG_XEN) && defined(CONFIG_COMPAT_VDSO)
static void __init relocate_vdso(Elf32_Ehdr *ehdr, unsigned long old_base, unsigned long new_base,
                                 const unsigned long *reloc_start, const unsigned long *reloc_end)
{
#if 1
	const unsigned long *reloc;

	for (reloc = reloc_start; reloc < reloc_end; ++reloc) {
		unsigned long *ptr = (void *)((unsigned long)ehdr + *reloc);

		*ptr += new_base - old_base;
	}
#else
	unsigned i, ndynsym = 0, szdynsym = 0;
	unsigned long dynsym = 0;

	BUG_ON(ehdr->e_ident[EI_MAG0] != ELFMAG0);
	BUG_ON(ehdr->e_ident[EI_MAG1] != ELFMAG1);
	BUG_ON(ehdr->e_ident[EI_MAG2] != ELFMAG2);
	BUG_ON(ehdr->e_ident[EI_MAG3] != ELFMAG3);
	BUG_ON(ehdr->e_ident[EI_CLASS] != ELFCLASS32);
	BUG_ON(ehdr->e_ident[EI_DATA] != ELFDATA2LSB);
	BUG_ON(ehdr->e_ehsize < sizeof(*ehdr));
	ehdr->e_entry += new_base - old_base;
	BUG_ON(ehdr->e_phentsize < sizeof(Elf32_Phdr));
	for (i = 0; i < ehdr->e_phnum; ++i) {
		Elf32_Phdr *phdr = (void *)((unsigned long)ehdr + ehdr->e_phoff + i * ehdr->e_phentsize);

		phdr->p_vaddr += new_base - old_base;
		switch(phdr->p_type) {
		case PT_LOAD:
		case PT_NOTE:
			break;
		case PT_DYNAMIC: {
				Elf32_Dyn *dyn = (void *)(phdr->p_vaddr - new_base + (unsigned long)ehdr);
				unsigned j;

				for(j = 0; dyn[j].d_tag != DT_NULL; ++j) {
					switch(dyn[j].d_tag) {
					case DT_HASH:
					case DT_STRTAB:
					case DT_SYMTAB:
					case 0x6ffffff0: /* DT_VERSYM */
					case 0x6ffffffc: /* DT_VERDEF */
						break;
					case DT_SONAME:
					case DT_STRSZ:
					case 0x6ffffffd: /* DT_VERDEFNUM */
						continue;
					case DT_SYMENT:
						szdynsym = dyn[j].d_un.d_val;
						continue;
					default:
						if (dyn[j].d_tag >= 0x60000000 /* OLD_DT_LOOS */
						    || dyn[j].d_tag < 31 /* DT_ENCODING */
						    || !(dyn[j].d_tag & 1)) {
							printk(KERN_WARNING "vDSO dynamic info %u has unsupported tag %08X\n", j, dyn[j].d_tag);
							WARN_ON(1);
							continue;
						}
						break;
					}
					dyn[j].d_un.d_ptr += new_base - old_base;
					switch(dyn[j].d_tag) {
					case DT_HASH:
						ndynsym = ((Elf32_Word *)dyn[j].d_un.d_ptr)[1];
						break;
					case DT_SYMTAB:
						dynsym = dyn[j].d_un.d_ptr;
						break;
					}
				}
			}
			break;
		case PT_GNU_EH_FRAME:
			/* XXX */
			break;
		default:
			printk(KERN_WARNING "vDSO program header %u has unsupported type %08X\n", i, phdr->p_type);
			WARN_ON(1);
			break;
		}
	}
	BUG_ON(ehdr->e_shentsize < sizeof(Elf32_Shdr));
	BUG_ON(ehdr->e_shnum >= SHN_LORESERVE);
	for (i = 1; i < ehdr->e_shnum; ++i) {
		Elf32_Shdr *shdr = (void *)((unsigned long)ehdr + ehdr->e_shoff + i * ehdr->e_shentsize);

		if (!(shdr->sh_flags & SHF_ALLOC))
			continue;
		shdr->sh_addr += new_base - old_base;
		switch(shdr->sh_type) {
		case SHT_DYNAMIC:
		case SHT_HASH:
		case SHT_NOBITS:
		case SHT_NOTE:
		case SHT_PROGBITS:
		case SHT_STRTAB:
		case 0x6ffffffd: /* SHT_GNU_verdef */
		case 0x6fffffff: /* SHT_GNU_versym */
			break;
		case SHT_DYNSYM:
			BUG_ON(shdr->sh_entsize < sizeof(Elf32_Sym));
			if (!szdynsym)
				szdynsym = shdr->sh_entsize;
			else
				WARN_ON(szdynsym != shdr->sh_entsize);
			if (!ndynsym)
				ndynsym = shdr->sh_size / szdynsym;
			else
				WARN_ON(ndynsym != shdr->sh_size / szdynsym);
			if (!dynsym)
				dynsym = shdr->sh_addr;
			else
				WARN_ON(dynsym != shdr->sh_addr);
			break;
		default:
			printk(KERN_WARNING "vDSO section %u has unsupported type %08X\n", i, shdr->sh_type);
			WARN_ON(shdr->sh_size);
			break;
		}
	}
	dynsym += (unsigned long)ehdr - new_base;
	for(i = 1; i < ndynsym; ++i) {
		Elf32_Sym *sym = (void *)(dynsym + i * szdynsym);

		if (sym->st_shndx == SHN_ABS)
			continue;
		sym->st_value += new_base - old_base;
	}
#endif
}
#else
#define relocate_vdso(ehdr, old, new, start, end) ((void)0)
#endif

/*
 * These symbols are defined by vsyscall.o to mark the bounds
 * of the ELF DSO images included therein.
 */
extern const char vsyscall_int80_start, vsyscall_int80_end;
extern const char vsyscall_sysenter_start, vsyscall_sysenter_end;
static void *syscall_page;

#ifndef CONFIG_XEN
#define virt_to_machine(x) __pa(x)
#elif defined(CONFIG_COMPAT_VDSO)
extern const unsigned long vdso_rel_int80_start[], vdso_rel_int80_end[];
extern const unsigned long vdso_rel_sysenter_start[], vdso_rel_sysenter_end[];
#endif

int __init sysenter_setup(void)
{
	syscall_page = (void *)get_zeroed_page(GFP_ATOMIC);

#ifdef CONFIG_COMPAT_VDSO
	__set_fixmap(FIX_VDSO, virt_to_machine(syscall_page), PAGE_READONLY);
	printk("Compat vDSO mapped to %08lx.\n", __fix_to_virt(FIX_VDSO));
#else
	/*
	 * In the non-compat case the ELF coredumping code needs the fixmap:
	 */
	__set_fixmap(FIX_VDSO, virt_to_machine(syscall_page), PAGE_KERNEL_RO);
#endif

#ifdef CONFIG_XEN
	if (boot_cpu_has(X86_FEATURE_SEP) &&
	    HYPERVISOR_callback_op(CALLBACKOP_register, &sysenter_cb) < 0)
		clear_bit(X86_FEATURE_SEP, boot_cpu_data.x86_capability);
#endif

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		memcpy(syscall_page,
		       &vsyscall_int80_start,
		       &vsyscall_int80_end - &vsyscall_int80_start);
		relocate_vdso(syscall_page, VDSO_PRELINK, __fix_to_virt(FIX_VDSO),
		              vdso_rel_int80_start, vdso_rel_int80_end);
		return 0;
	}

	memcpy(syscall_page,
	       &vsyscall_sysenter_start,
	       &vsyscall_sysenter_end - &vsyscall_sysenter_start);
	relocate_vdso(syscall_page, VDSO_PRELINK, __fix_to_virt(FIX_VDSO),
	              vdso_rel_sysenter_start, vdso_rel_sysenter_end);

	return 0;
}

static struct page *syscall_nopage(struct vm_area_struct *vma,
				unsigned long adr, int *type)
{
	struct page *p = virt_to_page(adr - vma->vm_start + syscall_page);
	get_page(p);
	return p;
}

/* Prevent VMA merging */
static void syscall_vma_close(struct vm_area_struct *vma)
{
}

static struct vm_operations_struct syscall_vm_ops = {
	.close = syscall_vma_close,
	.nopage = syscall_nopage,
};

/* Defined in vsyscall-sysenter.S */
extern void SYSENTER_RETURN;

/* Setup a VMA at program startup for the vsyscall page */
int arch_setup_additional_pages(struct linux_binprm *bprm, int exstack)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret;

	down_write(&mm->mmap_sem);
	addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	vma = kmem_cache_zalloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma) {
		ret = -ENOMEM;
		goto up_fail;
	}

	vma->vm_start = addr;
	vma->vm_end = addr + PAGE_SIZE;
	/* MAYWRITE to allow gdb to COW and set breakpoints */
	vma->vm_flags = VM_READ|VM_EXEC|VM_MAYREAD|VM_MAYEXEC|VM_MAYWRITE;
	vma->vm_flags |= mm->def_flags;
	vma->vm_page_prot = protection_map[vma->vm_flags & 7];
	vma->vm_ops = &syscall_vm_ops;
	vma->vm_mm = mm;

	ret = insert_vm_struct(mm, vma);
	if (unlikely(ret)) {
		kmem_cache_free(vm_area_cachep, vma);
		goto up_fail;
	}

	current->mm->context.vdso = (void *)addr;
	current_thread_info()->sysenter_return =
				    (void *)VDSO_SYM(&SYSENTER_RETURN);
	mm->total_vm++;
up_fail:
	up_write(&mm->mmap_sem);
	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	return NULL;
}

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
	return NULL;
}

int in_gate_area(struct task_struct *task, unsigned long addr)
{
	return 0;
}

int in_gate_area_no_task(unsigned long addr)
{
	return 0;
}
