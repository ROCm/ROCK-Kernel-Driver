#ifndef __PPC_ELF_H
#define __PPC_ELF_H

/*
 * ELF register definitions..
 */
#include <asm/ptrace.h>

#define ELF_NGREG	48	/* includes nip, msr, lr, etc. */
#define ELF_NFPREG	33	/* includes fpscr */
#define ELF_NVRREG	33	/* includes vscr */

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_PPC
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB

/* General registers */
typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* Floating point registers */
typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/* Altivec registers */
typedef __vector128 elf_vrreg_t;
typedef elf_vrreg_t elf_vrregset_t[ELF_NVRREG];

#ifdef __KERNEL__

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */

#define elf_check_arch(x) ((x)->e_machine == EM_PPC)

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (0x08000000)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#define ELF_CORE_COPY_REGS(gregs, regs) \
	memcpy(gregs, regs, \
	       sizeof(struct pt_regs) < sizeof(elf_gregset_t)? \
	       sizeof(struct pt_regs): sizeof(elf_gregset_t));


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

#define SET_PERSONALITY(ex, ibcs2) set_personality((ibcs2)?PER_SVR4:PER_LINUX)

#endif /* __KERNEL__ */
#endif
