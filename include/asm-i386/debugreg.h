#ifndef _I386_DEBUGREG_H
#define _I386_DEBUGREG_H


/* Indicate the register numbers for a number of the specific
   debug registers.  Registers 0-3 contain the addresses we wish to trap on */
#define DR_FIRSTADDR 0        /* u_debugreg[DR_FIRSTADDR] */
#define DR_LASTADDR 3         /* u_debugreg[DR_LASTADDR]  */

#define DR_STATUS 6           /* u_debugreg[DR_STATUS]     */
#define DR_CONTROL 7          /* u_debugreg[DR_CONTROL] */

/* Define a few things for the status register.  We can use this to determine
   which debugging register was responsible for the trap.  The other bits
   are either reserved or not of interest to us. */

#define DR_TRAP0	(0x1)		/* db0 */
#define DR_TRAP1	(0x2)		/* db1 */
#define DR_TRAP2	(0x4)		/* db2 */
#define DR_TRAP3	(0x8)		/* db3 */

#define DR_STEP		(0x4000)	/* single-step */
#define DR_SWITCH	(0x8000)	/* task switch */

/* Now define a bunch of things for manipulating the control register.
   The top two bytes of the control register consist of 4 fields of 4
   bits - each field corresponds to one of the four debug registers,
   and indicates what types of access we trap on, and how large the data
   field is that we are looking at */

#define DR_CONTROL_SHIFT 16 /* Skip this many bits in ctl register */
#define DR_CONTROL_SIZE 4   /* 4 control bits per register */

#define DR_RW_EXECUTE (0x0)   /* Settings for the access types to trap on */
#define DR_RW_WRITE (0x1)
#define DR_RW_READ (0x3)

#define DR_LEN_1 (0x0) /* Settings for data length to trap on */
#define DR_LEN_2 (0x4)
#define DR_LEN_4 (0xC)

/* The low byte to the control register determine which registers are
   enabled.  There are 4 fields of two bits.  One bit is "local", meaning
   that the processor will reset the bit after a task switch and the other
   is global meaning that we have to explicitly reset the bit.  With linux,
   you can use either one, since we explicitly zero the register when we enter
   kernel mode. */

#define DR_LOCAL_ENABLE_SHIFT 0    /* Extra shift to the local enable bit */
#define DR_GLOBAL_ENABLE_SHIFT 1   /* Extra shift to the global enable bit */
#define DR_ENABLE_SIZE 2           /* 2 enable bits per register */

#define DR_LOCAL_ENABLE_MASK (0x55)  /* Set  local bits for all 4 regs */
#define DR_GLOBAL_ENABLE_MASK (0xAA) /* Set global bits for all 4 regs */

/* The second byte to the control register has a few special things.
   We can slow the instruction pipeline for instructions coming via the
   gdt or the ldt if we want to.  I am not sure why this is an advantage */

#define DR_CONTROL_RESERVED (0xFC00) /* Reserved by Intel */
#define DR_LOCAL_SLOWDOWN (0x100)   /* Local slow the pipeline */
#define DR_GLOBAL_SLOWDOWN (0x200)  /* Global slow the pipeline */

struct debugreg {
	unsigned long flag;
	unsigned long use_count;
};

/* debugreg flags */
#define DR_UNUSED	0
#define DR_LOCAL	1
#define DR_GLOBAL	2
	
#define DR_MAX	4
#define DR_ANY	DR_MAX + 1

/* global or local allocation requests */
#define DR_ALLOC_GLOBAL		0
#define DR_ALLOC_LOCAL		1

#define DR7_RW_SET(dr, regnum, rw) do {	\
		(dr) &= ~(0x3 << (16 + (4 * (regnum)))); \
		(dr) |= (((rw) & 0x3) << (16 + (4 * (regnum)))); \
	} while (0)

#define DR7_RW_VAL(dr, regnum) \
	(((dr) >> (16 + (4 * (regnum)))) & 0x3)

#define DR7_LEN_SET(dr, regnum, len) do { \
		(dr) &= ~(0x3 << (18 + (4 * (regnum)))); \
		(dr) |= (((len-1) & 0x3) << (18 + (4 * (regnum)))); \
	} while (0)

#define DR7_LEN_VAL(dr, regnum) \
	(((dr) >> (18 + (4 * (regnum)))) & 0x3)

#define DR7_L0(dr)    (((dr))&0x1)
#define DR7_L1(dr)    (((dr)>>2)&0x1)
#define DR7_L2(dr)    (((dr)>>4)&0x1)
#define DR7_L3(dr)    (((dr)>>6)&0x1)

#define DR_IS_LOCAL(dr, num) ((dr) & (1UL << (num <<1)))

/* Set the rw, len and global flag in dr7 for a debug register */
#define SET_DR7(dr, regnum, access, len) do { \
		DR7_RW_SET(dr, regnum, access); \
		DR7_LEN_SET(dr, regnum, len); \
		dr |= (2UL << regnum*2); \
	} while (0)

/* Disable a debug register by clearing the global/local flag in dr7 */
#define RESET_DR7(dr, regnum) dr &= ~(3UL << regnum*2)

#define DR7_DR0_BITS		0x000F0003
#define DR7_DR1_BITS		0x00F0000C
#define DR7_DR2_BITS		0x0F000030
#define DR7_DR3_BITS		0xF00000C0

#define DR_TRAP_MASK 		0xF

#define DR_TYPE_EXECUTE		0x0
#define DR_TYPE_WRITE		0x1
#define DR_TYPE_IO		0x2
#define DR_TYPE_RW		0x3

#define get_dr(regnum, val) \
		__asm__("movl %%db" #regnum ", %0"  \
			:"=r" (val))
static inline unsigned long read_dr(int regnum)
{
	unsigned long val = 0;
	switch (regnum) {
		case 0: get_dr(0, val); break;
		case 1: get_dr(1, val); break;
		case 2: get_dr(2, val); break;
		case 3: get_dr(3, val); break;
		case 6: get_dr(6, val); break;
		case 7: get_dr(7, val); break;
	}
	return val;
}
#undef get_dr
	
#define set_dr(regnum, val) \
		__asm__("movl %0,%%db" #regnum  \
			: /* no output */ \
			:"r" (val))
static inline void write_dr(int regnum, unsigned long val)
{
	switch (regnum) {
		case 0: set_dr(0, val); break;
		case 1: set_dr(1, val); break;
		case 2: set_dr(2, val); break;
		case 3: set_dr(3, val); break;
		case 7: set_dr(7, val); break;
	}
	return;
}
#undef set_dr

#ifdef CONFIG_DEBUGREG
/*
 * Given the debug status register, returns the debug register number
 * which caused the debug trap.
 */
static inline int dr_trap(unsigned int condition)
{
	int i, reg_shift = 1UL;
	for (i = 0; i < DR_MAX; i++, reg_shift <<= 1)
		if ((condition & reg_shift))
			return i;	
	return -1;
}

/*
 * Given the debug status register, returns the address due to which
 * the debug trap occured.
 */
static inline unsigned long dr_trap_addr(unsigned int condition)
{
	int regnum = dr_trap(condition);

	if (regnum == -1)
		return -1;
	return read_dr(regnum);
}

/*
 * Given the debug status register, returns the type of debug trap:
 * execute, read/write, write or io.
 */
static inline int dr_trap_type(unsigned int condition)
{
	int regnum = dr_trap(condition);

	if (regnum == -1)
		return -1;
	return DR7_RW_VAL(read_dr(7), regnum);
}

/* Function declarations */

extern int dr_alloc(int regnum, int flag);
extern int dr_free(int regnum);
extern void dr_inc_use_count(unsigned long mask);
extern void dr_dec_use_count(unsigned long mask);
extern struct debugreg dr_list[DR_MAX];
extern unsigned long dr7_global_mask;
extern int enable_debugreg(unsigned long old_dr7, unsigned long new_dr7);

static inline void load_process_dr7(unsigned long curr_dr7)
{
	write_dr(7, (read_dr(7) & dr7_global_mask) | curr_dr7);
}
#else
static inline int enable_debugreg(unsigned long old_dr7, unsigned long new_dr7) { return 0; }
static inline void load_process_dr7(unsigned long curr_dr7)
{
	write_dr(7, curr_dr7);
}

#define dr_inc_use_count(mask) do { } while (0)
#define dr_dec_use_count(mask) do { } while (0)

#endif /* CONFIG_DEBUGREG */
#endif
