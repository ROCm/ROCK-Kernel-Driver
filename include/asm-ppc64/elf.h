#ifndef __PPC64_ELF_H
#define __PPC64_ELF_H

/* PowerPC relocations defined by the ABIs */
#define R_PPC_NONE		0
#define R_PPC_ADDR32		1	/* 32bit absolute address */
#define R_PPC_ADDR24		2	/* 26bit address, 2 bits ignored.  */
#define R_PPC_ADDR16		3	/* 16bit absolute address */
#define R_PPC_ADDR16_LO		4	/* lower 16bit of absolute address */
#define R_PPC_ADDR16_HI		5	/* high 16bit of absolute address */
#define R_PPC_ADDR16_HA		6	/* adjusted high 16bit */
#define R_PPC_ADDR14		7	/* 16bit address, 2 bits ignored */
#define R_PPC_ADDR14_BRTAKEN	8
#define R_PPC_ADDR14_BRNTAKEN	9
#define R_PPC_REL24		10	/* PC relative 26 bit */
#define R_PPC_REL14		11	/* PC relative 16 bit */
#define R_PPC_REL14_BRTAKEN	12
#define R_PPC_REL14_BRNTAKEN	13
#define R_PPC_GOT16		14
#define R_PPC_GOT16_LO		15
#define R_PPC_GOT16_HI		16
#define R_PPC_GOT16_HA		17
#define R_PPC_PLTREL24		18
#define R_PPC_COPY		19
#define R_PPC_GLOB_DAT		20
#define R_PPC_JMP_SLOT		21
#define R_PPC_RELATIVE		22
#define R_PPC_LOCAL24PC		23
#define R_PPC_UADDR32		24
#define R_PPC_UADDR16		25
#define R_PPC_REL32		26
#define R_PPC_PLT32		27
#define R_PPC_PLTREL32		28
#define R_PPC_PLT16_LO		29
#define R_PPC_PLT16_HI		30
#define R_PPC_PLT16_HA		31
#define R_PPC_SDAREL16		32
#define R_PPC_SECTOFF		33
#define R_PPC_SECTOFF_LO	34
#define R_PPC_SECTOFF_HI	35
#define R_PPC_SECTOFF_HA	36
/* Keep this the last entry.  */
#define R_PPC_NUM		37

/*
 * ELF register definitions..
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ptrace.h>

#define ELF_NGREG	48	/* includes nip, msr, lr, etc. */
#define ELF_NFPREG	33	/* includes fpscr */
#define ELF_NVRREG	33	/* includes vscr */

typedef unsigned long elf_greg_t64;
typedef elf_greg_t64 elf_gregset_t64[ELF_NGREG];

typedef unsigned int elf_greg_t32;
typedef elf_greg_t32 elf_gregset_t32[ELF_NGREG];

/*
 * These are used to set parameters in the core dumps.
 */
#ifndef ELF_ARCH
# define ELF_ARCH	EM_PPC64
# define ELF_CLASS	ELFCLASS64
# define ELF_DATA	ELFDATA2MSB
  typedef elf_greg_t64 elf_greg_t;
  typedef elf_gregset_t64 elf_gregset_t;
# define elf_addr_t unsigned long
#else
  /* Assumption: ELF_ARCH == EM_PPC and ELF_CLASS == ELFCLASS32 */
  typedef elf_greg_t32 elf_greg_t;
  typedef elf_gregset_t32 elf_gregset_t;
# define elf_addr_t u32
#endif

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == ELF_ARCH)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (0x08000000)

#ifdef __KERNEL__

/* Common routine for both 32-bit and 64-bit processes */
static inline void ppc64_elf_core_copy_regs(elf_gregset_t elf_regs,
					    struct pt_regs *regs)
{
	int i;
	int gprs = sizeof(struct pt_regs)/sizeof(elf_greg_t64);

	if (gprs > ELF_NGREG)
		gprs = ELF_NGREG;

	for (i=0; i < gprs; i++)
		elf_regs[i] = (elf_greg_t)((elf_greg_t64 *)regs)[i];
}
#define ELF_CORE_COPY_REGS(gregs, regs) ppc64_elf_core_copy_regs(gregs, regs);

static inline int dump_task_regs(struct task_struct *tsk,
				 elf_gregset_t *elf_regs)
{
	struct pt_regs *regs = tsk->thread.regs;
	if (regs)
		ppc64_elf_core_copy_regs(*elf_regs, regs);

	return 1;
}
#define ELF_CORE_COPY_TASK_REGS(tsk, elf_regs) dump_task_regs(tsk, elf_regs)

extern int dump_task_fpu(struct task_struct *, elf_fpregset_t *); 
#define ELF_CORE_COPY_FPREGS(tsk, elf_fpregs) dump_task_fpu(tsk, elf_fpregs)

#endif

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM	(NULL)

#define ELF_PLAT_INIT(_r, load_addr)	do { \
	memset(_r->gpr, 0, sizeof(_r->gpr)); \
	_r->ctr = _r->link = _r->xer = _r->ccr = 0; \
	_r->gpr[2] = load_addr; \
} while (0)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2)				\
do {								\
	unsigned long new_flags = 0;				\
	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)		\
		new_flags = _TIF_32BIT;				\
	if ((current_thread_info()->flags & _TIF_32BIT)		\
	    != new_flags)					\
		set_thread_flag(TIF_ABI_PENDING);		\
	else							\
		clear_thread_flag(TIF_ABI_PENDING);		\
	if (ibcs2)						\
		set_personality(PER_SVR4);			\
	else if (current->personality != PER_LINUX32)		\
		set_personality(PER_LINUX);			\
} while (0)
#endif

/*
 * We need to put in some extra aux table entries to tell glibc what
 * the cache block size is, so it can use the dcbz instruction safely.
 */
#define AT_DCACHEBSIZE		19
#define AT_ICACHEBSIZE		20
#define AT_UCACHEBSIZE		21
/* A special ignored type value for PPC, for glibc compatibility.  */
#define AT_IGNOREPPC		22

extern int dcache_bsize;
extern int icache_bsize;
extern int ucache_bsize;

/*
 * The requirements here are:
 * - keep the final alignment of sp (sp & 0xf)
 * - make sure the 32-bit value at the first 16 byte aligned position of
 *   AUXV is greater than 16 for glibc compatibility.
 *   AT_IGNOREPPC is used for that.
 * - for compatibility with glibc ARCH_DLINFO must always be defined on PPC,
 *   even if DLINFO_ARCH_ITEMS goes to zero or is undefined.
 */
#define ARCH_DLINFO							\
do {									\
	/* Handle glibc compatibility. */				\
	NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);			\
	NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);			\
	/* Cache size items */						\
	NEW_AUX_ENT(AT_DCACHEBSIZE, dcache_bsize);			\
	NEW_AUX_ENT(AT_ICACHEBSIZE, icache_bsize);			\
	NEW_AUX_ENT(AT_UCACHEBSIZE, ucache_bsize);			\
 } while (0)

/* PowerPC64 relocations defined by the ABIs */
#define R_PPC64_NONE    R_PPC_NONE
#define R_PPC64_ADDR32  R_PPC_ADDR32  /* 32bit absolute address.  */
#define R_PPC64_ADDR24  R_PPC_ADDR24  /* 26bit address, word aligned.  */
#define R_PPC64_ADDR16  R_PPC_ADDR16  /* 16bit absolute address. */
#define R_PPC64_ADDR16_LO R_PPC_ADDR16_LO /* lower 16bits of abs. address.  */
#define R_PPC64_ADDR16_HI R_PPC_ADDR16_HI /* high 16bits of abs. address. */
#define R_PPC64_ADDR16_HA R_PPC_ADDR16_HA /* adjusted high 16bits.  */
#define R_PPC64_ADDR14 R_PPC_ADDR14   /* 16bit address, word aligned.  */
#define R_PPC64_ADDR14_BRTAKEN  R_PPC_ADDR14_BRTAKEN
#define R_PPC64_ADDR14_BRNTAKEN R_PPC_ADDR14_BRNTAKEN
#define R_PPC64_REL24   R_PPC_REL24 /* PC relative 26 bit, word aligned.  */
#define R_PPC64_REL14   R_PPC_REL14 /* PC relative 16 bit. */
#define R_PPC64_REL14_BRTAKEN   R_PPC_REL14_BRTAKEN
#define R_PPC64_REL14_BRNTAKEN  R_PPC_REL14_BRNTAKEN
#define R_PPC64_GOT16     R_PPC_GOT16
#define R_PPC64_GOT16_LO  R_PPC_GOT16_LO
#define R_PPC64_GOT16_HI  R_PPC_GOT16_HI
#define R_PPC64_GOT16_HA  R_PPC_GOT16_HA

#define R_PPC64_COPY      R_PPC_COPY
#define R_PPC64_GLOB_DAT  R_PPC_GLOB_DAT
#define R_PPC64_JMP_SLOT  R_PPC_JMP_SLOT
#define R_PPC64_RELATIVE  R_PPC_RELATIVE

#define R_PPC64_UADDR32   R_PPC_UADDR32
#define R_PPC64_UADDR16   R_PPC_UADDR16
#define R_PPC64_REL32     R_PPC_REL32
#define R_PPC64_PLT32     R_PPC_PLT32
#define R_PPC64_PLTREL32  R_PPC_PLTREL32
#define R_PPC64_PLT16_LO  R_PPC_PLT16_LO
#define R_PPC64_PLT16_HI  R_PPC_PLT16_HI
#define R_PPC64_PLT16_HA  R_PPC_PLT16_HA

#define R_PPC64_SECTOFF     R_PPC_SECTOFF
#define R_PPC64_SECTOFF_LO  R_PPC_SECTOFF_LO
#define R_PPC64_SECTOFF_HI  R_PPC_SECTOFF_HI
#define R_PPC64_SECTOFF_HA  R_PPC_SECTOFF_HA
#define R_PPC64_ADDR30          37  /* word30 (S + A - P) >> 2.  */
#define R_PPC64_ADDR64          38  /* doubleword64 S + A.  */
#define R_PPC64_ADDR16_HIGHER   39  /* half16 #higher(S + A).  */
#define R_PPC64_ADDR16_HIGHERA  40  /* half16 #highera(S + A).  */
#define R_PPC64_ADDR16_HIGHEST  41  /* half16 #highest(S + A).  */
#define R_PPC64_ADDR16_HIGHESTA 42  /* half16 #highesta(S + A). */
#define R_PPC64_UADDR64     43  /* doubleword64 S + A.  */
#define R_PPC64_REL64       44  /* doubleword64 S + A - P.  */
#define R_PPC64_PLT64       45  /* doubleword64 L + A.  */
#define R_PPC64_PLTREL64    46  /* doubleword64 L + A - P.  */
#define R_PPC64_TOC16       47  /* half16* S + A - .TOC.  */
#define R_PPC64_TOC16_LO    48  /* half16 #lo(S + A - .TOC.).  */
#define R_PPC64_TOC16_HI    49  /* half16 #hi(S + A - .TOC.).  */
#define R_PPC64_TOC16_HA    50  /* half16 #ha(S + A - .TOC.).  */
#define R_PPC64_TOC         51  /* doubleword64 .TOC. */
#define R_PPC64_PLTGOT16    52  /* half16* M + A.  */
#define R_PPC64_PLTGOT16_LO 53  /* half16 #lo(M + A).  */
#define R_PPC64_PLTGOT16_HI 54  /* half16 #hi(M + A).  */
#define R_PPC64_PLTGOT16_HA 55  /* half16 #ha(M + A).  */

#define R_PPC64_ADDR16_DS      56 /* half16ds* (S + A) >> 2.  */
#define R_PPC64_ADDR16_LO_DS   57 /* half16ds  #lo(S + A) >> 2.  */
#define R_PPC64_GOT16_DS       58 /* half16ds* (G + A) >> 2.  */
#define R_PPC64_GOT16_LO_DS    59 /* half16ds  #lo(G + A) >> 2.  */
#define R_PPC64_PLT16_LO_DS    60 /* half16ds  #lo(L + A) >> 2.  */
#define R_PPC64_SECTOFF_DS     61 /* half16ds* (R + A) >> 2.  */
#define R_PPC64_SECTOFF_LO_DS  62 /* half16ds  #lo(R + A) >> 2.  */
#define R_PPC64_TOC16_DS       63 /* half16ds* (S + A - .TOC.) >> 2.  */
#define R_PPC64_TOC16_LO_DS    64 /* half16ds  #lo(S + A - .TOC.) >> 2.  */
#define R_PPC64_PLTGOT16_DS    65 /* half16ds* (M + A) >> 2.  */
#define R_PPC64_PLTGOT16_LO_DS 66 /* half16ds  #lo(M + A) >> 2.  */
/* Keep this the last entry.  */
#define R_PPC64_NUM		67

#endif /* __PPC64_ELF_H */
