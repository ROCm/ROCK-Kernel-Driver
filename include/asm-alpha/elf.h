#ifndef __ASM_ALPHA_ELF_H
#define __ASM_ALPHA_ELF_H

/*
 * ELF register definitions..
 */

/*
 * The OSF/1 version of <sys/procfs.h> makes gregset_t 46 entries long.
 * I have no idea why that is so.  For now, we just leave it at 33
 * (32 general regs + processor status word). 
 */
#define ELF_NGREG	33
#define ELF_NFPREG	32

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_ALPHA)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_ALPHA

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	8192

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE		(TASK_UNMAPPED_BASE + 0x1000000)

/* $0 is set by ld.so to a pointer to a function which might be 
   registered using atexit.  This provides a mean for the dynamic
   linker to call DT_FINI functions for shared libraries that have
   been loaded before the code runs.

   So that we can use the same startup file with static executables,
   we start programs with a value of 0 to indicate that there is no
   such function.  */

#define ELF_PLAT_INIT(_r)       _r->r0 = 0

/* Use the same format as the OSF/1 procfs interface.  The register
   layout is sane.  However, since dump_thread() creates the funky
   layout that ECOFF coredumps want, we need to undo that layout here.
   Eventually, it would be nice if the ECOFF core-dump had to do the
   translation, then ELF_CORE_COPY_REGS() would become trivial and
   faster.  */
#define ELF_CORE_COPY_REGS(_dest,_regs)				\
{								\
	struct user _dump;					\
								\
	dump_thread(_regs, &_dump);				\
	_dest[ 0] = _dump.regs[EF_V0];				\
	_dest[ 1] = _dump.regs[EF_T0];				\
	_dest[ 2] = _dump.regs[EF_T1];				\
	_dest[ 3] = _dump.regs[EF_T2];				\
	_dest[ 4] = _dump.regs[EF_T3];				\
	_dest[ 5] = _dump.regs[EF_T4];				\
	_dest[ 6] = _dump.regs[EF_T5];				\
	_dest[ 7] = _dump.regs[EF_T6];				\
	_dest[ 8] = _dump.regs[EF_T7];				\
	_dest[ 9] = _dump.regs[EF_S0];				\
	_dest[10] = _dump.regs[EF_S1];				\
	_dest[11] = _dump.regs[EF_S2];				\
	_dest[12] = _dump.regs[EF_S3];				\
	_dest[13] = _dump.regs[EF_S4];				\
	_dest[14] = _dump.regs[EF_S5];				\
	_dest[15] = _dump.regs[EF_S6];				\
	_dest[16] = _dump.regs[EF_A0];				\
	_dest[17] = _dump.regs[EF_A1];				\
	_dest[18] = _dump.regs[EF_A2];				\
	_dest[19] = _dump.regs[EF_A3];				\
	_dest[20] = _dump.regs[EF_A4];				\
	_dest[21] = _dump.regs[EF_A5];				\
	_dest[22] = _dump.regs[EF_T8];				\
	_dest[23] = _dump.regs[EF_T9];				\
	_dest[24] = _dump.regs[EF_T10];				\
	_dest[25] = _dump.regs[EF_T11];				\
	_dest[26] = _dump.regs[EF_RA];				\
	_dest[27] = _dump.regs[EF_T12];				\
	_dest[28] = _dump.regs[EF_AT];				\
	_dest[29] = _dump.regs[EF_GP];				\
	_dest[30] = _dump.regs[EF_SP];				\
	_dest[31] = _dump.regs[EF_PC];	/* store PC here */	\
	_dest[32] = _dump.regs[EF_PS];				\
}

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This is trivial on Alpha, 
   but not so on other machines. */

#define ELF_HWCAP							\
({									\
	/* Sadly, most folks don't yet have assemblers that know about	\
	   amask.  This is "amask v0, v0" */				\
	register long _v0 __asm("$0") = -1;				\
	__asm(".long 0x47e00c20" : "=r"(_v0) : "0"(_v0));		\
	~_v0;								\
})

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  

   This might do with checking bwx simultaneously...  */

#define ELF_PLATFORM				\
({						\
	/* Or "implver v0" ... */		\
	register long _v0 __asm("$0");		\
	__asm(".long 0x47e03d80" : "=r"(_v0));	\
	_v0 == 0 ? "ev4" : "ev5";		\
})

#ifdef __KERNEL__
#define SET_PERSONALITY(EX, IBCS2)				\
	set_personality(((EX).e_flags & EF_ALPHA_32BIT)		\
	   ? PER_LINUX_32BIT : (IBCS2) ? PER_SVR4 : PER_LINUX)
#endif

#endif
