/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_ELF_H
#define __ASM_ELF_H

#define PT_MIPS_REGINFO		0x70000000

/* Flags in the e_flags field of the header */
#define EF_MIPS_NOREORDER 0x00000001
#define EF_MIPS_PIC       0x00000002
#define EF_MIPS_CPIC      0x00000004
#define EF_MIPS_ARCH      0xf0000000

#define DT_MIPS_RLD_VERSION	0x70000001
#define DT_MIPS_TIME_STAMP	0x70000002
#define DT_MIPS_ICHECKSUM	0x70000003
#define DT_MIPS_IVERSION	0x70000004
#define DT_MIPS_FLAGS		0x70000005
  #define RHF_NONE		  0
  #define RHF_HARDWAY		  1
  #define RHF_NOTPOT		  2
#define DT_MIPS_BASE_ADDRESS	0x70000006
#define DT_MIPS_CONFLICT	0x70000008
#define DT_MIPS_LIBLIST		0x70000009
#define DT_MIPS_LOCAL_GOTNO	0x7000000a
#define DT_MIPS_CONFLICTNO	0x7000000b
#define DT_MIPS_LIBLISTNO	0x70000010
#define DT_MIPS_SYMTABNO	0x70000011
#define DT_MIPS_UNREFEXTNO	0x70000012
#define DT_MIPS_GOTSYM		0x70000013
#define DT_MIPS_HIPAGENO	0x70000014
#define DT_MIPS_RLD_MAP		0x70000016

#define R_MIPS_NONE		0
#define R_MIPS_16		1
#define R_MIPS_32		2
#define R_MIPS_REL32		3
#define R_MIPS_26		4
#define R_MIPS_HI16		5
#define R_MIPS_LO16		6
#define R_MIPS_GPREL16		7
#define R_MIPS_LITERAL		8
#define R_MIPS_GOT16		9
#define R_MIPS_PC16		10
#define R_MIPS_CALL16		11
#define R_MIPS_GPREL32		12
/* The remaining relocs are defined on Irix, although they are not
   in the MIPS ELF ABI.  */
#define R_MIPS_UNUSED1		13
#define R_MIPS_UNUSED2		14
#define R_MIPS_UNUSED3		15
#define R_MIPS_SHIFT5		16
#define R_MIPS_SHIFT6		17
#define R_MIPS_64		18
#define R_MIPS_GOT_DISP		19
#define R_MIPS_GOT_PAGE		20
#define R_MIPS_GOT_OFST		21
/*
 * The following two relocation types are specified in the MIPS ABI
 * conformance guide version 1.2 but not yet in the psABI.
 */
#define R_MIPS_GOTHI16		22
#define R_MIPS_GOTLO16		23
#define R_MIPS_SUB		24
#define R_MIPS_INSERT_A		25
#define R_MIPS_INSERT_B		26
#define R_MIPS_DELETE		27
#define R_MIPS_HIGHER		28
#define R_MIPS_HIGHEST		29
/*
 * The following two relocation types are specified in the MIPS ABI
 * conformance guide version 1.2 but not yet in the psABI.
 */
#define R_MIPS_CALLHI16		30
#define R_MIPS_CALLLO16		31
/*
 * This range is reserved for vendor specific relocations.
 */
#define R_MIPS_LOVENDOR		100
#define R_MIPS_HIVENDOR		127

#define SHN_MIPS_ACCOMON	0xff00

#define SHT_MIPS_LIST		0x70000000
#define SHT_MIPS_CONFLICT	0x70000002
#define SHT_MIPS_GPTAB		0x70000003
#define SHT_MIPS_UCODE		0x70000004

#define SHF_MIPS_GPREL	0x10000000

/* ELF register definitions */
#define ELF_NGREG	45
#define ELF_NFPREG	33

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture
 * and also rejects IRIX binaries.
 */
#define elf_check_arch(hdr)						\
({									\
	int __res = 1;							\
	struct elfhdr *__h = (hdr);					\
									\
	if (__h->e_machine != EM_MIPS)					\
		__res = 0;						\
	if (__h->e_flags & EF_MIPS_ARCH)				\
		__res = 0;						\
									\
	__res;								\
})

/* This one accepts IRIX binaries.  */
#define irix_elf_check_arch(hdr)	((hdr)->e_machine == EM_MIPS)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#ifdef __MIPSEB__
#define ELF_DATA	ELFDATA2MSB
#elif __MIPSEL__
#define ELF_DATA	ELFDATA2LSB
#endif
#define ELF_ARCH	EM_MIPS

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#define ELF_CORE_COPY_REGS(_dest,_regs)				\
	memcpy((char *) &_dest, (char *) _regs,			\
	       sizeof(struct pt_regs));

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP       (0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM  (NULL)

/*
 * See comments in asm-alpha/elf.h, this is the same thing
 * on the MIPS.
 */
#define ELF_PLAT_INIT(_r, load_addr)	do { \
	_r->regs[1] = _r->regs[2] = _r->regs[3] = _r->regs[4] = 0;	\
	_r->regs[5] = _r->regs[6] = _r->regs[7] = _r->regs[8] = 0;	\
	_r->regs[9] = _r->regs[10] = _r->regs[11] = _r->regs[12] = 0;	\
	_r->regs[13] = _r->regs[14] = _r->regs[15] = _r->regs[16] = 0;	\
	_r->regs[17] = _r->regs[18] = _r->regs[19] = _r->regs[20] = 0;	\
	_r->regs[21] = _r->regs[22] = _r->regs[23] = _r->regs[24] = 0;	\
	_r->regs[25] = _r->regs[26] = _r->regs[27] = _r->regs[28] = 0;	\
	_r->regs[30] = _r->regs[31] = 0;				\
} while (0)

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (TASK_SIZE / 3 * 2)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2) set_personality((ibcs2)?PER_SVR4:PER_LINUX)
#endif

#endif /* __ASM_ELF_H */
