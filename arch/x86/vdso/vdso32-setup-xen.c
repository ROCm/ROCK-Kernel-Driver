/*
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
#include <linux/err.h>
#include <linux/module.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/elf.h>
#include <asm/tlbflush.h>
#include <asm/vdso.h>
#include <asm/proto.h>

#include <xen/interface/callback.h>

enum {
	VDSO_DISABLED = 0,
	VDSO_ENABLED = 1,
	VDSO_COMPAT = 2,
};

#ifdef CONFIG_COMPAT_VDSO
#define VDSO_DEFAULT	VDSO_COMPAT
#else
#define VDSO_DEFAULT	VDSO_ENABLED
#endif

#ifdef CONFIG_X86_64
#define vdso_enabled			sysctl_vsyscall32
#define arch_setup_additional_pages	syscall32_setup_pages
#endif

/*
 * This is the difference between the prelinked addresses in the vDSO images
 * and the VDSO_HIGH_BASE address where CONFIG_COMPAT_VDSO places the vDSO
 * in the user address space.
 */
#define VDSO_ADDR_ADJUST	(VDSO_HIGH_BASE - (unsigned long)VDSO32_PRELINK)

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = VDSO_DEFAULT;

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);

	return 1;
}

/*
 * For consistency, the argument vdso32=[012] affects the 32-bit vDSO
 * behavior on both 64-bit and 32-bit kernels.
 * On 32-bit kernels, vdso=[012] means the same thing.
 */
__setup("vdso32=", vdso_setup);

#ifdef CONFIG_X86_32
__setup_param("vdso=", vdso32_setup, vdso_setup, 0);

EXPORT_SYMBOL_GPL(vdso_enabled);
#endif

static __init void reloc_symtab(Elf32_Ehdr *ehdr,
				unsigned offset, unsigned size)
{
	Elf32_Sym *sym = (void *)ehdr + offset;
	unsigned nsym = size / sizeof(*sym);
	unsigned i;

	for(i = 0; i < nsym; i++, sym++) {
		if (sym->st_shndx == SHN_UNDEF ||
		    sym->st_shndx == SHN_ABS)
			continue;  /* skip */

		if (sym->st_shndx > SHN_LORESERVE) {
			printk(KERN_INFO "VDSO: unexpected st_shndx %x\n",
			       sym->st_shndx);
			continue;
		}

		switch(ELF_ST_TYPE(sym->st_info)) {
		case STT_OBJECT:
		case STT_FUNC:
		case STT_SECTION:
		case STT_FILE:
			sym->st_value += VDSO_ADDR_ADJUST;
		}
	}
}

static __init void reloc_dyn(Elf32_Ehdr *ehdr, unsigned offset)
{
	Elf32_Dyn *dyn = (void *)ehdr + offset;

	for(; dyn->d_tag != DT_NULL; dyn++)
		switch(dyn->d_tag) {
		case DT_PLTGOT:
		case DT_HASH:
		case DT_STRTAB:
		case DT_SYMTAB:
		case DT_RELA:
		case DT_INIT:
		case DT_FINI:
		case DT_REL:
		case DT_DEBUG:
		case DT_JMPREL:
		case DT_VERSYM:
		case DT_VERDEF:
		case DT_VERNEED:
		case DT_ADDRRNGLO ... DT_ADDRRNGHI:
			/* definitely pointers needing relocation */
			dyn->d_un.d_ptr += VDSO_ADDR_ADJUST;
			break;

		case DT_ENCODING ... OLD_DT_LOOS-1:
		case DT_LOOS ... DT_HIOS-1:
			/* Tags above DT_ENCODING are pointers if
			   they're even */
			if (dyn->d_tag >= DT_ENCODING &&
			    (dyn->d_tag & 1) == 0)
				dyn->d_un.d_ptr += VDSO_ADDR_ADJUST;
			break;

		case DT_VERDEFNUM:
		case DT_VERNEEDNUM:
		case DT_FLAGS_1:
		case DT_RELACOUNT:
		case DT_RELCOUNT:
		case DT_VALRNGLO ... DT_VALRNGHI:
			/* definitely not pointers */
			break;

		case OLD_DT_LOOS ... DT_LOOS-1:
		case DT_HIOS ... DT_VALRNGLO-1:
		default:
			if (dyn->d_tag > DT_ENCODING)
				printk(KERN_INFO "VDSO: unexpected DT_tag %x\n",
				       dyn->d_tag);
			break;
		}
}

static __init void relocate_vdso(Elf32_Ehdr *ehdr)
{
	Elf32_Phdr *phdr;
	Elf32_Shdr *shdr;
	int i;

	BUG_ON(memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
	       !elf_check_arch_ia32(ehdr) ||
	       ehdr->e_type != ET_DYN);

	ehdr->e_entry += VDSO_ADDR_ADJUST;

	/* rebase phdrs */
	phdr = (void *)ehdr + ehdr->e_phoff;
	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr[i].p_vaddr += VDSO_ADDR_ADJUST;

		/* relocate dynamic stuff */
		if (phdr[i].p_type == PT_DYNAMIC)
			reloc_dyn(ehdr, phdr[i].p_offset);
	}

	/* rebase sections */
	shdr = (void *)ehdr + ehdr->e_shoff;
	for(i = 0; i < ehdr->e_shnum; i++) {
		if (!(shdr[i].sh_flags & SHF_ALLOC))
			continue;

		shdr[i].sh_addr += VDSO_ADDR_ADJUST;

		if (shdr[i].sh_type == SHT_SYMTAB ||
		    shdr[i].sh_type == SHT_DYNSYM)
			reloc_symtab(ehdr, shdr[i].sh_offset,
				     shdr[i].sh_size);
	}
}

static struct page *vdso32_pages[1];

#ifdef CONFIG_X86_64

#define	vdso32_sysenter()	(boot_cpu_has(X86_FEATURE_SYSENTER32))
#define	vdso32_syscall()	(boot_cpu_has(X86_FEATURE_SYSCALL32))

void __cpuinit syscall32_cpu_init(void)
{
	static const struct callback_register __cpuinitconst cstar = {
		.type = CALLBACKTYPE_syscall32,
		.address = (unsigned long)ia32_cstar_target
	};
	static const struct callback_register __cpuinitconst sysenter = {
		.type = CALLBACKTYPE_sysenter,
		.address = (unsigned long)ia32_sysenter_target
	};

	if (HYPERVISOR_callback_op(CALLBACKOP_register, &sysenter) < 0)
		setup_clear_cpu_cap(X86_FEATURE_SYSENTER32);
	if (HYPERVISOR_callback_op(CALLBACKOP_register, &cstar) < 0)
		setup_clear_cpu_cap(X86_FEATURE_SYSCALL32);
}

#define compat_uses_vma		1

static inline void map_compat_vdso(int map)
{
}

#else  /* CONFIG_X86_32 */

#define vdso32_sysenter()	(boot_cpu_has(X86_FEATURE_SEP))
#define vdso32_syscall()	(boot_cpu_has(X86_FEATURE_SYSCALL32))

extern asmlinkage void ia32pv_cstar_target(void);
static const struct callback_register __cpuinitconst cstar = {
	.type = CALLBACKTYPE_syscall32,
	.address = { __KERNEL_CS, (unsigned long)ia32pv_cstar_target },
};

void __cpuinit enable_sep_cpu(void)
{
	extern asmlinkage void ia32pv_sysenter_target(void);
	static struct callback_register __cpuinitdata sysenter = {
		.type = CALLBACKTYPE_sysenter,
		.address = { __KERNEL_CS, (unsigned long)ia32pv_sysenter_target },
	};

	if (vdso32_syscall()) {
		if (HYPERVISOR_callback_op(CALLBACKOP_register, &cstar) != 0)
			BUG();
		return;
	}

	if (!vdso32_sysenter())
		return;

	if (xen_feature(XENFEAT_supervisor_mode_kernel))
		sysenter.address.eip = (unsigned long)ia32_sysenter_target;

	switch (HYPERVISOR_callback_op(CALLBACKOP_register, &sysenter)) {
	case 0:
		break;
#if CONFIG_XEN_COMPAT < 0x030200
	case -ENOSYS:
		sysenter.type = CALLBACKTYPE_sysenter_deprecated;
		if (HYPERVISOR_callback_op(CALLBACKOP_register, &sysenter) == 0)
			break;
#endif
	default:
		setup_clear_cpu_cap(X86_FEATURE_SEP);
		break;
	}
}

static struct vm_area_struct gate_vma;

static int __init gate_vma_init(void)
{
	gate_vma.vm_mm = NULL;
	gate_vma.vm_start = FIXADDR_USER_START;
	gate_vma.vm_end = FIXADDR_USER_END;
	gate_vma.vm_flags = VM_READ | VM_MAYREAD | VM_EXEC | VM_MAYEXEC;
	gate_vma.vm_page_prot = __P101;
	/*
	 * Make sure the vDSO gets into every core dump.
	 * Dumping its contents makes post-mortem fully interpretable later
	 * without matching up the same kernel and hardware config to see
	 * what PC values meant.
	 */
	gate_vma.vm_flags |= VM_ALWAYSDUMP;
	return 0;
}

#define compat_uses_vma		0

static void map_compat_vdso(int map)
{
	static int vdso_mapped;

	if (map == vdso_mapped)
		return;

	vdso_mapped = map;

	__set_fixmap(FIX_VDSO, page_to_pfn(vdso32_pages[0]) << PAGE_SHIFT,
		     map ? PAGE_READONLY_EXEC : PAGE_NONE);

	/* flush stray tlbs */
	flush_tlb_all();
}

#endif	/* CONFIG_X86_64 */

int __init sysenter_setup(void)
{
	void *syscall_page = (void *)get_zeroed_page(GFP_ATOMIC);
	const void *vsyscall;
	size_t vsyscall_len;

	vdso32_pages[0] = virt_to_page(syscall_page);

#ifdef CONFIG_X86_32
	gate_vma_init();

	if (boot_cpu_has(X86_FEATURE_SYSCALL)) {
		if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD
		    && HYPERVISOR_callback_op(CALLBACKOP_register, &cstar) == 0)
			setup_force_cpu_cap(X86_FEATURE_SYSCALL32);
		else {
			setup_clear_cpu_cap(X86_FEATURE_SYSCALL);
			setup_clear_cpu_cap(X86_FEATURE_SYSCALL32);
		}
	}
#endif
	if (vdso32_syscall()) {
		vsyscall = &vdso32_syscall_start;
		vsyscall_len = &vdso32_syscall_end - &vdso32_syscall_start;
	} else if (vdso32_sysenter()){
		vsyscall = &vdso32_sysenter_start;
		vsyscall_len = &vdso32_sysenter_end - &vdso32_sysenter_start;
	} else {
		vsyscall = &vdso32_int80_start;
		vsyscall_len = &vdso32_int80_end - &vdso32_int80_start;
	}

	memcpy(syscall_page, vsyscall, vsyscall_len);
	relocate_vdso(syscall_page);

	return 0;
}

/* Setup a VMA at program startup for the vsyscall page */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;
	bool compat;

	if (vdso_enabled == VDSO_DISABLED)
		return 0;

	down_write(&mm->mmap_sem);

	/* Test compat mode once here, in case someone
	   changes it via sysctl */
	compat = (vdso_enabled == VDSO_COMPAT);

	map_compat_vdso(compat);

	if (compat)
		addr = VDSO_HIGH_BASE;
	else {
		addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
		if (IS_ERR_VALUE(addr)) {
			ret = addr;
			goto up_fail;
		}
	}

	current->mm->context.vdso = (void *)addr;

	if (compat_uses_vma || !compat) {
		/*
		 * MAYWRITE to allow gdb to COW and set breakpoints
		 *
		 * Make sure the vDSO gets into every core dump.
		 * Dumping its contents makes post-mortem fully
		 * interpretable later without matching up the same
		 * kernel and hardware config to see what PC values
		 * meant.
		 */
		ret = install_special_mapping(mm, addr, PAGE_SIZE,
					      VM_READ|VM_EXEC|
					      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
					      VM_ALWAYSDUMP,
					      vdso32_pages);

		if (ret)
			goto up_fail;
	}

	current_thread_info()->sysenter_return =
		VDSO32_SYMBOL(addr, SYSENTER_RETURN);

  up_fail:
	if (ret)
		current->mm->context.vdso = NULL;

	up_write(&mm->mmap_sem);

	return ret;
}

#ifdef CONFIG_X86_64

/*
 * This must be done early in case we have an initrd containing 32-bit
 * binaries (e.g., hotplug). This could be pushed upstream.
 */
core_initcall(sysenter_setup);

#ifdef CONFIG_SYSCTL
/* Register vsyscall32 into the ABI table */
#include <linux/sysctl.h>

static ctl_table abi_table2[] = {
	{
		.procname	= "vsyscall32",
		.data		= &sysctl_vsyscall32,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{}
};

static ctl_table abi_root_table2[] = {
	{
		.ctl_name = CTL_ABI,
		.procname = "abi",
		.mode = 0555,
		.child = abi_table2
	},
	{}
};

static __init int ia32_binfmt_init(void)
{
	register_sysctl_table(abi_root_table2);
	return 0;
}
__initcall(ia32_binfmt_init);
#endif

#else  /* CONFIG_X86_32 */

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	return NULL;
}

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;

	/* Check to see if this task was created in compat vdso mode */
	if (mm && mm->context.vdso == (void *)VDSO_HIGH_BASE)
		return &gate_vma;
	return NULL;
}

int in_gate_area(struct task_struct *task, unsigned long addr)
{
	const struct vm_area_struct *vma = get_gate_vma(task);

	return vma && addr >= vma->vm_start && addr < vma->vm_end;
}

int in_gate_area_no_task(unsigned long addr)
{
	return 0;
}

#endif	/* CONFIG_X86_64 */
