/*
 *  include/asm-s390/ptrace.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#ifndef _S390_PTRACE_H
#define _S390_PTRACE_H
#include <linux/config.h>
#include <asm/s390-regs-common.h>
#include <asm/current.h>
#include <linux/types.h>
#include <asm/setup.h>
#include <linux/stddef.h>


#define S390_REGS   \
S390_REGS_COMMON    \
__u32 orig_gpr2;

typedef struct
{
	S390_REGS
} s390_regs;

struct pt_regs 
{
	S390_REGS
	__u32 trap;
};

#if CONFIG_REMOTE_DEBUG
typedef struct
{
	S390_REGS
	__u32 trap;
	__u32 crs[16];
	s390_fp_regs fp_regs;
} gdb_pt_regs;
#endif


typedef struct
{
			__u32   cr[3];
} per_cr_words  __attribute__((packed));

#define PER_EM_MASK 0xE8000000
typedef	struct
{
	unsigned    em_branching:1;
	unsigned    em_instruction_fetch:1;
	/* Switching on storage alteration automatically fixes
	   the storage alteration event bit in the users std. */
	unsigned    em_storage_alteration:1;
	unsigned    em_gpr_alt_unused:1;
	unsigned    em_store_real_address:1;
	unsigned    :3;
	unsigned    branch_addr_ctl:1;
	unsigned    :1;
	unsigned    storage_alt_space_ctl:1;
	unsigned    :5;
	unsigned    :16;
	__u32   starting_addr;
	__u32   ending_addr;
} per_cr_bits  __attribute__((packed));

typedef struct
{
	__u16          perc_atmid;          /* 0x096 */
	__u32          address;             /* 0x098 */
	__u8           access_id;           /* 0x0a1 */
} per_lowcore_words  __attribute__((packed));

typedef struct
{
	unsigned       perc_branching:1;               /* 0x096 */
	unsigned       perc_instruction_fetch:1;
	unsigned       perc_storage_alteration:1;
	unsigned       perc_gpr_alt_unused:1;
	unsigned       perc_store_real_address:1;
	unsigned       :3;
	unsigned       :1;
	unsigned       atmid:5;
	unsigned       si:2;
	__u32          address;              /* 0x098 */
	unsigned       :4;                   /* 0x0a1 */
	unsigned       access_id:4;           
} per_lowcore_bits __attribute__((packed));

typedef struct
{
	union
	{
		per_cr_words   words;
		per_cr_bits    bits;
	} control_regs  __attribute__((packed));
	/* Use these flags instead of setting em_instruction_fetch */
	/* directly they are used so that single stepping can be */
	/* switched on & off while not affecting other tracing */
	unsigned  single_step:1;
	unsigned  instruction_fetch:1;
	unsigned  :30;
	/* These addresses are copied into cr10 & cr11 if single stepping
	   is switched off */
	__u32     starting_addr;
	__u32     ending_addr;
	union
	{
		per_lowcore_words words;
		per_lowcore_bits  bits;
	} lowcore; 
} per_struct __attribute__((packed));



/* this struct defines the way the registers are stored on the
   stack during a system call. If you change the pt_regs structure,
   you'll need to change user.h too. 

   N.B. if you modify the pt_regs struct the strace command also has to be
   modified & recompiled  ( just wait till we have gdb going ).

*/

struct user_regs_struct
{
	S390_REGS
	s390_fp_regs fp_regs;
/* These per registers are in here so that gdb can modify them itself
 * as there is no "official" ptrace interface for hardware watchpoints.
 * this is the way intel does it
 */
	per_struct per_info;
};

typedef struct user_regs_struct user_regs_struct;

typedef struct pt_regs pt_regs;

#ifdef __KERNEL__
#define user_mode(regs) (((regs)->psw.mask & PSW_PROBLEM_STATE) != 0)
#define instruction_pointer(regs) ((regs)->psw.addr)

struct thread_struct;
extern int sprintf_regs(int line,char *buff,struct task_struct * task,
			struct thread_struct *tss,struct pt_regs * regs);
extern void show_regs(struct task_struct * task,struct thread_struct *tss,
		      struct pt_regs * regs);
#endif





#define FIX_PSW(addr) ((unsigned long)(addr)|0x80000000UL)

#define MULT_PROCPTR_TYPES    ((CONFIG_BINFMT_ELF)&&(CONFIG_BINFMT_TOC))

typedef struct
{
  long addr;
  long toc;
} routine_descriptor;
extern void fix_routine_descriptor_regs(routine_descriptor *rdes,pt_regs *regs);
extern __inline__ void 
fix_routine_descriptor_regs(routine_descriptor *rdes,pt_regs *regs)
{
  regs->psw.addr=FIX_PSW(rdes->addr);
  regs->gprs[12]=rdes->toc;
}

/*
 * Compiler optimisation should save this stuff from being non optimal
 * & remove uneccessary code ( isnt gcc great DJB. )
 */

/*I'm just using this an indicator of what binformat we are using
 * (DJB) N.B. this needs to stay a macro unfortunately as I am otherwise
 * dereferencing incomplete pointer types in with load_toc_binary
 */
#if MULT_PROCPTR_TYPES
#define uses_routine_descriptors() \
(current->binfmt->load_binary==load_toc_binary)
#else
#if CONFIG_BINFMT_TOC
#define uses_routine_descriptors() 1
#else
#define uses_routine_descriptors() 0
#endif
#endif

#define pt_off(ptreg)   offsetof(user_regs_struct,ptreg)
enum
{
	PT_PSWMASK=pt_off(psw.mask),
	PT_PSWADDR=pt_off(psw.addr),
	PT_GPR0=pt_off(gprs[0]),
	PT_GPR1=pt_off(gprs[1]),
	PT_GPR2=pt_off(gprs[2]),
	PT_GPR3=pt_off(gprs[3]),
	PT_GPR4=pt_off(gprs[4]),
	PT_GPR5=pt_off(gprs[5]),
	PT_GPR6=pt_off(gprs[6]),
	PT_GPR7=pt_off(gprs[7]),
	PT_GPR8=pt_off(gprs[8]),
	PT_GPR9=pt_off(gprs[9]),
	PT_GPR10=pt_off(gprs[10]),
	PT_GPR11=pt_off(gprs[11]),
	PT_GPR12=pt_off(gprs[12]),
	PT_GPR13=pt_off(gprs[13]),
	PT_GPR14=pt_off(gprs[14]),
	PT_GPR15=pt_off(gprs[15]),
        PT_ACR0=pt_off(acrs[0]),
        PT_ACR1=pt_off(acrs[1]),
        PT_ACR2=pt_off(acrs[2]),
        PT_ACR3=pt_off(acrs[3]),
        PT_ACR4=pt_off(acrs[4]),
        PT_ACR5=pt_off(acrs[5]),
        PT_ACR6=pt_off(acrs[6]),
        PT_ACR7=pt_off(acrs[7]),
        PT_ACR8=pt_off(acrs[8]),
        PT_ACR9=pt_off(acrs[9]),
        PT_ACR10=pt_off(acrs[10]),
        PT_ACR11=pt_off(acrs[11]),
        PT_ACR12=pt_off(acrs[12]),
        PT_ACR13=pt_off(acrs[13]),
        PT_ACR14=pt_off(acrs[14]),
        PT_ACR15=pt_off(acrs[15]),
	PT_ORIGGPR2=pt_off(orig_gpr2),
	PT_FPC=pt_off(fp_regs.fpc),
/*
 *      A nasty fact of life that the ptrace api
 *      only supports passing of longs.
 */
	PT_FPR0_HI=pt_off(fp_regs.fprs[0].fp.hi),
	PT_FPR0_LO=pt_off(fp_regs.fprs[0].fp.lo),
	PT_FPR1_HI=pt_off(fp_regs.fprs[1].fp.hi),
	PT_FPR1_LO=pt_off(fp_regs.fprs[1].fp.lo),
	PT_FPR2_HI=pt_off(fp_regs.fprs[2].fp.hi),
	PT_FPR2_LO=pt_off(fp_regs.fprs[2].fp.lo),
	PT_FPR3_HI=pt_off(fp_regs.fprs[3].fp.hi),
	PT_FPR3_LO=pt_off(fp_regs.fprs[3].fp.lo),
	PT_FPR4_HI=pt_off(fp_regs.fprs[4].fp.hi),
	PT_FPR4_LO=pt_off(fp_regs.fprs[4].fp.lo),
	PT_FPR5_HI=pt_off(fp_regs.fprs[5].fp.hi),
	PT_FPR5_LO=pt_off(fp_regs.fprs[5].fp.lo),
	PT_FPR6_HI=pt_off(fp_regs.fprs[6].fp.hi),
	PT_FPR6_LO=pt_off(fp_regs.fprs[6].fp.lo),
	PT_FPR7_HI=pt_off(fp_regs.fprs[7].fp.hi),
	PT_FPR7_LO=pt_off(fp_regs.fprs[7].fp.lo),
	PT_FPR8_HI=pt_off(fp_regs.fprs[8].fp.hi),
	PT_FPR8_LO=pt_off(fp_regs.fprs[8].fp.lo),
	PT_FPR9_HI=pt_off(fp_regs.fprs[9].fp.hi),
	PT_FPR9_LO=pt_off(fp_regs.fprs[9].fp.lo),
	PT_FPR10_HI=pt_off(fp_regs.fprs[10].fp.hi),
	PT_FPR10_LO=pt_off(fp_regs.fprs[10].fp.lo),
	PT_FPR11_HI=pt_off(fp_regs.fprs[11].fp.hi),
	PT_FPR11_LO=pt_off(fp_regs.fprs[11].fp.lo),
	PT_FPR12_HI=pt_off(fp_regs.fprs[12].fp.hi),
	PT_FPR12_LO=pt_off(fp_regs.fprs[12].fp.lo),
	PT_FPR13_HI=pt_off(fp_regs.fprs[13].fp.hi),
	PT_FPR13_LO=pt_off(fp_regs.fprs[13].fp.lo),
	PT_FPR14_HI=pt_off(fp_regs.fprs[14].fp.hi),
	PT_FPR14_LO=pt_off(fp_regs.fprs[14].fp.lo),
	PT_FPR15_HI=pt_off(fp_regs.fprs[15].fp.hi),
	PT_FPR15_LO=pt_off(fp_regs.fprs[15].fp.lo),
	PT_CR_9=pt_off(per_info.control_regs.words.cr[0]),
	PT_CR_10=pt_off(per_info.control_regs.words.cr[1]),
	PT_CR_11=pt_off(per_info.control_regs.words.cr[2]),
	PT_LASTOFF=PT_CR_11,
	PT_ENDREGS=offsetof(user_regs_struct,per_info.lowcore.words.perc_atmid)
};

#define PTRACE_AREA \
__u32 len;          \
addr_t  kernel_addr; \
addr_t  process_addr;

typedef struct
{
	 PTRACE_AREA
} ptrace_area;

/*
  390 specific non posix ptrace requests
  I chose unusual values so they are unlikely to clash with future ptrace definitions.
 */
#define PTRACE_PEEKUSR_AREA           0x5000
#define PTRACE_POKEUSR_AREA           0x5001
#define PTRACE_PEEKTEXT_AREA	      0x5002
#define PTRACE_PEEKDATA_AREA	      0x5003
#define PTRACE_POKETEXT_AREA	      0x5004
#define PTRACE_POKEDATA_AREA 	      0x5005
/* PT_PROT definition is loosely based on hppa bsd definition in gdb/hppab-nat.c */
#define PTRACE_PROT                       21

typedef enum
{
	ptprot_set_access_watchpoint,
	ptprot_set_write_watchpoint,
	ptprot_disable_watchpoint
} ptprot_flags;

typedef struct
{
	addr_t           lowaddr;
	addr_t           hiaddr;
	ptprot_flags     prot;
} ptprot_area;                     
#endif














