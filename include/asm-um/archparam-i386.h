/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __UM_ARCHPARAM_I386_H
#define __UM_ARCHPARAM_I386_H

/********* Bits for asm-um/elf.h ************/

#include "user.h"

#define ELF_PLATFORM "i586"

#define ELF_ET_DYN_BASE (2 * TASK_SIZE / 3)

typedef struct user_i387_struct elf_fpregset_t;
typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

#define ELF_DATA        ELFDATA2LSB
#define ELF_ARCH        EM_386

#define ELF_PLAT_INIT(regs, load_addr) do { \
	PT_REGS_EBX(regs) = 0; \
	PT_REGS_ECX(regs) = 0; \
	PT_REGS_EDX(regs) = 0; \
	PT_REGS_ESI(regs) = 0; \
	PT_REGS_EDI(regs) = 0; \
	PT_REGS_EBP(regs) = 0; \
	PT_REGS_EAX(regs) = 0; \
} while(0)

/* Shamelessly stolen from include/asm-i386/elf.h */

#define ELF_CORE_COPY_REGS(pr_reg, regs) do {	\
	pr_reg[0] = PT_REGS_EBX(regs);		\
	pr_reg[1] = PT_REGS_ECX(regs);		\
	pr_reg[2] = PT_REGS_EDX(regs);		\
	pr_reg[3] = PT_REGS_ESI(regs);		\
	pr_reg[4] = PT_REGS_EDI(regs);		\
	pr_reg[5] = PT_REGS_EBP(regs);		\
	pr_reg[6] = PT_REGS_EAX(regs);		\
	pr_reg[7] = PT_REGS_DS(regs);		\
	pr_reg[8] = PT_REGS_ES(regs);		\
	/* fake once used fs and gs selectors? */	\
	pr_reg[9] = PT_REGS_DS(regs);		\
	pr_reg[10] = PT_REGS_DS(regs);		\
	pr_reg[11] = PT_REGS_SYSCALL_NR(regs);	\
	pr_reg[12] = PT_REGS_IP(regs);		\
	pr_reg[13] = PT_REGS_CS(regs);		\
	pr_reg[14] = PT_REGS_EFLAGS(regs);	\
	pr_reg[15] = PT_REGS_SP(regs);		\
	pr_reg[16] = PT_REGS_SS(regs);		\
} while(0);

#if 0 /* Turn this back on when UML has VSYSCALL working */
#define VSYSCALL_BASE	(__fix_to_virt(FIX_VSYSCALL))
#else
#define VSYSCALL_BASE	0
#endif

#define VSYSCALL_EHDR	((const struct elfhdr *) VSYSCALL_BASE)
#define VSYSCALL_ENTRY	((unsigned long) &__kernel_vsyscall)
extern void *__kernel_vsyscall;

/*
 * Architecture-neutral AT_ values in 0-17, leave some room
 * for more of them, start the x86-specific ones at 32.
 */
#define AT_SYSINFO		32
#define AT_SYSINFO_EHDR		33

#define ARCH_DLINFO						\
do {								\
		NEW_AUX_ENT(AT_SYSINFO,	VSYSCALL_ENTRY);	\
		NEW_AUX_ENT(AT_SYSINFO_EHDR, VSYSCALL_BASE);	\
} while (0)

/*
 * These macros parameterize elf_core_dump in fs/binfmt_elf.c to write out
 * extra segments containing the vsyscall DSO contents.  Dumping its
 * contents makes post-mortem fully interpretable later without matching up
 * the same kernel and hardware config to see what PC values meant.
 * Dumping its extra ELF program headers includes all the other information
 * a debugger needs to easily find how the vsyscall DSO was being used.
 */
#if 0
#define ELF_CORE_EXTRA_PHDRS		(VSYSCALL_EHDR->e_phnum)
#endif

#undef ELF_CORE_EXTRA_PHDRS

#if 0
#define ELF_CORE_WRITE_EXTRA_PHDRS					      \
do {									      \
	const struct elf_phdr *const vsyscall_phdrs =			      \
		(const struct elf_phdr *) (VSYSCALL_BASE		      \
					   + VSYSCALL_EHDR->e_phoff);	      \
	int i;								      \
	Elf32_Off ofs = 0;						      \
	for (i = 0; i < VSYSCALL_EHDR->e_phnum; ++i) {			      \
		struct elf_phdr phdr = vsyscall_phdrs[i];		      \
		if (phdr.p_type == PT_LOAD) {				      \
			ofs = phdr.p_offset = offset;			      \
			offset += phdr.p_filesz;			      \
		}							      \
		else							      \
			phdr.p_offset += ofs;				      \
		phdr.p_paddr = 0; /* match other core phdrs */		      \
		DUMP_WRITE(&phdr, sizeof(phdr));			      \
	}								      \
} while (0)
#define ELF_CORE_WRITE_EXTRA_DATA					      \
do {									      \
	const struct elf_phdr *const vsyscall_phdrs =			      \
		(const struct elf_phdr *) (VSYSCALL_BASE		      \
					   + VSYSCALL_EHDR->e_phoff);	      \
	int i;								      \
	for (i = 0; i < VSYSCALL_EHDR->e_phnum; ++i) {			      \
		if (vsyscall_phdrs[i].p_type == PT_LOAD)		      \
			DUMP_WRITE((void *) vsyscall_phdrs[i].p_vaddr,	      \
				   vsyscall_phdrs[i].p_filesz);		      \
	}								      \
} while (0)
#endif

#undef ELF_CORE_WRITE_EXTRA_PHDRS
#undef ELF_CORE_WRITE_EXTRA_DATA

#define R_386_NONE	0
#define R_386_32	1
#define R_386_PC32	2
#define R_386_GOT32	3
#define R_386_PLT32	4
#define R_386_COPY	5
#define R_386_GLOB_DAT	6
#define R_386_JMP_SLOT	7
#define R_386_RELATIVE	8
#define R_386_GOTOFF	9
#define R_386_GOTPC	10
#define R_386_NUM	11

/********* Bits for asm-um/delay.h **********/

typedef unsigned long um_udelay_t;

/********* Nothing for asm-um/hardirq.h **********/

/********* Nothing for asm-um/hw_irq.h **********/

/********* Nothing for asm-um/string.h **********/

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
