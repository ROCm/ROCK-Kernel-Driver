#ifndef _ASM_IA64_ILP32_H
#define _ASM_IA64_ILP32_H

#include <linux/config.h>

/*
 * Data types and macros for providing ILP32 support for native IA-64 instruction set.
 */

#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

#include <asm/elf.h>
#undef	ELF_CLASS
#define	ELF_CLASS	ELFCLASS32
#undef	elf_check_arch
#define elf_check_arch(x) ((x)->e_machine == EM_IA_64 && (x)->e_ident[EI_CLASS] == ELFCLASS32)
#undef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE 0x80000000UL

#define elf_addr_t      u32
#define elf_caddr_t     u32

#define ILP32_PAGE_OFFSET	0x100000000UL /* 4G */
#define ILP32_STACK_TOP		(ILP32_PAGE_OFFSET - PAGE_SIZE)
#define ILP32_RBS_BOT		0xF0000000UL	/* bottom of reg. backing store */
#define ILP32_MMAP_BASE		0x40000000UL

#define ROUND_UP(x,a)	((__typeof__(x))(((unsigned long)(x) + ((a) - 1)) & ~((a) - 1)))
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))

static inline void ilp32_elf_start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
{
	start_thread(regs,new_ip,new_sp);
	regs->ar_bspstore = ILP32_RBS_BOT;
}

#endif
