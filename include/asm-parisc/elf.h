#ifndef __ASMPARISC_ELF_H
#define __ASMPARISC_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>

#define EM_PARISC 15

#define ELF_NGREG 32
#define ELF_NFPREG 32

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#define ELF_CORE_COPY_REGS(gregs, regs) \
	memcpy(gregs, regs, \
	       sizeof(struct pt_regs) < sizeof(elf_gregset_t)? \
	       sizeof(struct pt_regs): sizeof(elf_gregset_t));

/*
 * This is used to ensure we don't load something for the wrong architecture.
 *
 * Note that this header file is used by default in fs/binfmt_elf.c. So
 * the following macros are for the default case. However, for the 64
 * bit kernel we also support 32 bit parisc binaries. To do that
 * arch/parisc64/kernel/binfmt_elf32.c defines its own set of these
 * macros, and then if includes fs/binfmt_elf.c to provide an alternate
 * elf binary handler for 32 bit binaries (on the 64 bit kernel).
 */

#ifdef __LP64__
#define ELF_CLASS       ELFCLASS64
#else
#define ELF_CLASS	ELFCLASS32
#endif

#define elf_check_arch(x) ((x)->e_machine == EM_PARISC && (x)->e_ident[EI_CLASS] == ELF_CLASS)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_DATA	ELFDATA2MSB
#define ELF_ARCH	EM_PARISC

/* %r23 is set by ld.so to a pointer to a function which might be 
   registered using atexit.  This provides a mean for the dynamic
   linker to call DT_FINI functions for shared libraries that have
   been loaded before the code runs.

   So that we can use the same startup file with static executables,
   we start programs with a value of 0 to indicate that there is no
   such function.  */
#define ELF_PLAT_INIT(_r)       _r->gr[23] = 0

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.

   (2 * TASK_SIZE / 3) turns into something undefined when run through a
   32 bit preprocessor and in some cases results in the kernel trying to map
   ld.so to the kernel virtual base. Use a sane value instead. /Jes 
  */

#define ELF_ET_DYN_BASE         (TASK_UNMAPPED_BASE + 0x01000000)

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP	0
/* (boot_cpu_data.x86_capability) */

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM  ("PARISC\0" /*+((boot_cpu_data.x86-3)*5) */)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2) \
	current->personality = PER_LINUX
#endif

#endif
