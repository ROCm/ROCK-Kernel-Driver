/*
 * Architecture-specific unaligned trap handling.
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 Stephane Eranian <eranian@hpl.hp.com>
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <asm/rse.h>
#include <asm/processor.h>
#include <asm/unaligned.h>

extern void die_if_kernel(char *str, struct pt_regs *regs, long err) __attribute__ ((noreturn));

#undef DEBUG_UNALIGNED_TRAP

#ifdef DEBUG_UNALIGNED_TRAP
#define DPRINT(a) { printk("%s, line %d: ", __FUNCTION__, __LINE__); printk a;}
#else
#define DPRINT(a)
#endif

#define IA64_FIRST_STACKED_GR	32
#define IA64_FIRST_ROTATING_FR	32
#define SIGN_EXT9		__IA64_UL(0xffffffffffffff00)

/*
 * For M-unit:
 *
 *  opcode |   m  |   x6    |
 * --------|------|---------|
 * [40-37] | [36] | [35:30] |
 * --------|------|---------|
 *     4   |   1  |    6    | = 11 bits
 * --------------------------
 * However bits [31:30] are not directly useful to distinguish between 
 * load/store so we can use [35:32] instead, which gives the following 
 * mask ([40:32]) using 9 bits. The 'e' comes from the fact that we defer
 * checking the m-bit until later in the load/store emulation.
 */
#define IA64_OPCODE_MASK	0x1ef00000000

/*
 * Table C-28 Integer Load/Store
 * 
 * We ignore [35:32]= 0x6, 0x7, 0xE, 0xF
 *
 * ld8.fill, st8.fill  MUST be aligned because the RNATs are based on 
 * the address (bits [8:3]), so we must failed.
 */
#define LD_OP            0x08000000000
#define LDS_OP           0x08100000000
#define LDA_OP           0x08200000000
#define LDSA_OP          0x08300000000
#define LDBIAS_OP        0x08400000000
#define LDACQ_OP         0x08500000000
/* 0x086, 0x087 are not relevant */
#define LDCCLR_OP        0x08800000000
#define LDCNC_OP         0x08900000000
#define LDCCLRACQ_OP     0x08a00000000
#define ST_OP            0x08c00000000
#define STREL_OP         0x08d00000000
/* 0x08e,0x8f are not relevant */

/*
 * Table C-29 Integer Load +Reg
 *
 * we use the ld->m (bit [36:36]) field to determine whether or not we have
 * a load/store of this form.
 */

/*
 * Table C-30 Integer Load/Store +Imm
 * 
 * We ignore [35:32]= 0x6, 0x7, 0xE, 0xF
 *
 * ld8.fill, st8.fill  must be aligned because the Nat register are based on 
 * the address, so we must fail and the program must be fixed.
 */
#define LD_IMM_OP            0x0a000000000
#define LDS_IMM_OP           0x0a100000000
#define LDA_IMM_OP           0x0a200000000
#define LDSA_IMM_OP          0x0a300000000
#define LDBIAS_IMM_OP        0x0a400000000
#define LDACQ_IMM_OP         0x0a500000000
/* 0x0a6, 0xa7 are not relevant */
#define LDCCLR_IMM_OP        0x0a800000000
#define LDCNC_IMM_OP         0x0a900000000
#define LDCCLRACQ_IMM_OP     0x0aa00000000
#define ST_IMM_OP            0x0ac00000000
#define STREL_IMM_OP         0x0ad00000000
/* 0x0ae,0xaf are not relevant */

/*
 * Table C-32 Floating-point Load/Store
 */
#define LDF_OP           0x0c000000000
#define LDFS_OP          0x0c100000000
#define LDFA_OP          0x0c200000000
#define LDFSA_OP         0x0c300000000
/* 0x0c6 is irrelevant */
#define LDFCCLR_OP       0x0c800000000
#define LDFCNC_OP        0x0c900000000
/* 0x0cb is irrelevant  */
#define STF_OP           0x0cc00000000

/*
 * Table C-33 Floating-point Load +Reg
 *
 * we use the ld->m (bit [36:36]) field to determine whether or not we have
 * a load/store of this form.
 */

/*
 * Table C-34 Floating-point Load/Store +Imm
 */
#define LDF_IMM_OP       0x0e000000000
#define LDFS_IMM_OP      0x0e100000000
#define LDFA_IMM_OP      0x0e200000000
#define LDFSA_IMM_OP     0x0e300000000
/* 0x0e6 is irrelevant */
#define LDFCCLR_IMM_OP   0x0e800000000
#define LDFCNC_IMM_OP    0x0e900000000
#define STF_IMM_OP       0x0ec00000000

typedef struct {
	unsigned long  	 qp:6;	/* [0:5]   */
	unsigned long    r1:7;	/* [6:12]  */
	unsigned long   imm:7;	/* [13:19] */
	unsigned long    r3:7;	/* [20:26] */
	unsigned long     x:1;  /* [27:27] */
	unsigned long  hint:2;	/* [28:29] */
	unsigned long x6_sz:2;	/* [30:31] */
	unsigned long x6_op:4;	/* [32:35], x6 = x6_sz|x6_op */
	unsigned long     m:1;	/* [36:36] */
	unsigned long    op:4;	/* [37:40] */
	unsigned long   pad:23; /* [41:63] */
} load_store_t;


typedef enum {
	UPD_IMMEDIATE,	/* ldXZ r1=[r3],imm(9) */
	UPD_REG		/* ldXZ r1=[r3],r2     */
} update_t;

/*
 * We use tables to keep track of the offsets of registers in the saved state.
 * This way we save having big switch/case statements.
 *
 * We use bit 0 to indicate switch_stack or pt_regs.
 * The offset is simply shifted by 1 bit.
 * A 2-byte value should be enough to hold any kind of offset
 *
 * In case the calling convention changes (and thus pt_regs/switch_stack)
 * simply use RSW instead of RPT or vice-versa.
 */

#define RPO(x)	((size_t) &((struct pt_regs *)0)->x)
#define RSO(x)	((size_t) &((struct switch_stack *)0)->x)

#define RPT(x)		(RPO(x) << 1)
#define RSW(x)		(1| RSO(x)<<1)

#define GR_OFFS(x)	(gr_info[x]>>1)
#define GR_IN_SW(x)	(gr_info[x] & 0x1)

#define FR_OFFS(x)	(fr_info[x]>>1)
#define FR_IN_SW(x)	(fr_info[x] & 0x1)

static u16 gr_info[32]={
	0, 			/* r0 is read-only : WE SHOULD NEVER GET THIS */

	RPT(r1), RPT(r2), RPT(r3),

	RSW(r4), RSW(r5), RSW(r6), RSW(r7),

	RPT(r8), RPT(r9), RPT(r10), RPT(r11),
	RPT(r12), RPT(r13), RPT(r14), RPT(r15),

	RPT(r16), RPT(r17), RPT(r18), RPT(r19),
	RPT(r20), RPT(r21), RPT(r22), RPT(r23),
	RPT(r24), RPT(r25), RPT(r26), RPT(r27),
	RPT(r28), RPT(r29), RPT(r30), RPT(r31)
};

static u16 fr_info[32]={
	0, 			/* constant : WE SHOULD NEVER GET THIS */
	0,			/* constant : WE SHOULD NEVER GET THIS */

	RSW(f2), RSW(f3), RSW(f4), RSW(f5),

	RPT(f6), RPT(f7), RPT(f8), RPT(f9),

	RSW(f10), RSW(f11), RSW(f12), RSW(f13), RSW(f14),
	RSW(f15), RSW(f16), RSW(f17), RSW(f18), RSW(f19),
	RSW(f20), RSW(f21), RSW(f22), RSW(f23), RSW(f24),
	RSW(f25), RSW(f26), RSW(f27), RSW(f28), RSW(f29),
	RSW(f30), RSW(f31)
};

/* Invalidate ALAT entry for integer register REGNO.  */
static void
invala_gr (int regno)
{
#	define F(reg)	case reg: __asm__ __volatile__ ("invala.e r%0" :: "i"(reg)); break

	switch (regno) {
		F(  0); F(  1); F(  2); F(  3); F(  4); F(  5); F(  6); F(  7);
		F(  8); F(  9); F( 10); F( 11); F( 12); F( 13); F( 14); F( 15);
		F( 16); F( 17); F( 18); F( 19); F( 20); F( 21); F( 22); F( 23);
		F( 24); F( 25); F( 26); F( 27); F( 28); F( 29); F( 30); F( 31);
		F( 32); F( 33); F( 34); F( 35); F( 36); F( 37); F( 38); F( 39);
		F( 40); F( 41); F( 42); F( 43); F( 44); F( 45); F( 46); F( 47);
		F( 48); F( 49); F( 50); F( 51); F( 52); F( 53); F( 54); F( 55);
		F( 56); F( 57); F( 58); F( 59); F( 60); F( 61); F( 62); F( 63);
		F( 64); F( 65); F( 66); F( 67); F( 68); F( 69); F( 70); F( 71);
		F( 72); F( 73); F( 74); F( 75); F( 76); F( 77); F( 78); F( 79);
		F( 80); F( 81); F( 82); F( 83); F( 84); F( 85); F( 86); F( 87);
		F( 88); F( 89); F( 90); F( 91); F( 92); F( 93); F( 94); F( 95);
		F( 96); F( 97); F( 98); F( 99); F(100); F(101); F(102); F(103);
		F(104); F(105); F(106); F(107); F(108); F(109); F(110); F(111);
		F(112); F(113); F(114); F(115); F(116); F(117); F(118); F(119);
		F(120); F(121); F(122); F(123); F(124); F(125); F(126); F(127);
	}
#	undef F
}

/* Invalidate ALAT entry for floating-point register REGNO.  */
static void
invala_fr (int regno)
{
#	define F(reg)	case reg: __asm__ __volatile__ ("invala.e f%0" :: "i"(reg)); break

	switch (regno) {
		F(  0); F(  1); F(  2); F(  3); F(  4); F(  5); F(  6); F(  7);
		F(  8); F(  9); F( 10); F( 11); F( 12); F( 13); F( 14); F( 15);
		F( 16); F( 17); F( 18); F( 19); F( 20); F( 21); F( 22); F( 23);
		F( 24); F( 25); F( 26); F( 27); F( 28); F( 29); F( 30); F( 31);
		F( 32); F( 33); F( 34); F( 35); F( 36); F( 37); F( 38); F( 39);
		F( 40); F( 41); F( 42); F( 43); F( 44); F( 45); F( 46); F( 47);
		F( 48); F( 49); F( 50); F( 51); F( 52); F( 53); F( 54); F( 55);
		F( 56); F( 57); F( 58); F( 59); F( 60); F( 61); F( 62); F( 63);
		F( 64); F( 65); F( 66); F( 67); F( 68); F( 69); F( 70); F( 71);
		F( 72); F( 73); F( 74); F( 75); F( 76); F( 77); F( 78); F( 79);
		F( 80); F( 81); F( 82); F( 83); F( 84); F( 85); F( 86); F( 87);
		F( 88); F( 89); F( 90); F( 91); F( 92); F( 93); F( 94); F( 95);
		F( 96); F( 97); F( 98); F( 99); F(100); F(101); F(102); F(103);
		F(104); F(105); F(106); F(107); F(108); F(109); F(110); F(111);
		F(112); F(113); F(114); F(115); F(116); F(117); F(118); F(119);
		F(120); F(121); F(122); F(123); F(124); F(125); F(126); F(127);
	}
#	undef F
}

static void
set_rse_reg(struct pt_regs *regs, unsigned long r1, unsigned long val, int nat)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	unsigned long *kbs	= ((unsigned long *)current) + IA64_RBS_OFFSET/8;
	unsigned long on_kbs;
	unsigned long *bsp, *bspstore, *addr, *ubs_end, *slot;
	unsigned long rnats;
	long nlocals;

	/*
	 * cr_ifs=[rv:ifm], ifm=[....:sof(6)]
	 * nlocal=number of locals (in+loc) register of the faulting function
	 */
	nlocals = (regs->cr_ifs) & 0x7f;

	DPRINT(("sw.bsptore=%lx pt.bspstore=%lx\n", sw->ar_bspstore, regs->ar_bspstore));
	DPRINT(("cr.ifs=%lx sof=%ld sol=%ld\n",
		regs->cr_ifs, regs->cr_ifs &0x7f, (regs->cr_ifs>>7)&0x7f));

	on_kbs   = ia64_rse_num_regs(kbs, (unsigned long *)sw->ar_bspstore);
	bspstore = (unsigned long *)regs->ar_bspstore;

	DPRINT(("rse_slot_num=0x%lx\n",ia64_rse_slot_num((unsigned long *)sw->ar_bspstore)));
	DPRINT(("kbs=%p nlocals=%ld\n", (void *) kbs, nlocals));
	DPRINT(("bspstore next rnat slot %p\n",
		(void *) ia64_rse_rnat_addr((unsigned long *)sw->ar_bspstore)));
	DPRINT(("on_kbs=%ld rnats=%ld\n",
		on_kbs, ((sw->ar_bspstore-(unsigned long)kbs)>>3) - on_kbs));

	/*
	 * See get_rse_reg() for an explanation on the following instructions
	 */
	ubs_end = ia64_rse_skip_regs(bspstore, on_kbs);
	bsp     = ia64_rse_skip_regs(ubs_end, -nlocals);
	addr    = slot = ia64_rse_skip_regs(bsp, r1 - 32);

	DPRINT(("ubs_end=%p bsp=%p addr=%p slot=0x%lx\n",
		(void *) ubs_end, (void *) bsp, (void *) addr, ia64_rse_slot_num(addr)));

	ia64_poke(regs, current, (unsigned long)addr, val);

	/*
	 * addr will now contain the address of the RNAT for the register
	 */
	addr = ia64_rse_rnat_addr(addr);

	ia64_peek(regs, current, (unsigned long)addr, &rnats);
	DPRINT(("rnat @%p = 0x%lx nat=%d rnatval=%lx\n",
		(void *) addr, rnats, nat, rnats &ia64_rse_slot_num(slot)));
	
	if (nat) {
		rnats |= __IA64_UL(1) << ia64_rse_slot_num(slot);
	} else {
		rnats &= ~(__IA64_UL(1) << ia64_rse_slot_num(slot));
	}
	ia64_poke(regs, current, (unsigned long)addr, rnats);

	DPRINT(("rnat changed to @%p = 0x%lx\n", (void *) addr, rnats));
}


static void
get_rse_reg(struct pt_regs *regs, unsigned long r1, unsigned long *val, int *nat)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	unsigned long *kbs	= (unsigned long *)current + IA64_RBS_OFFSET/8;
	unsigned long on_kbs;
	long nlocals;
	unsigned long *bsp, *addr, *ubs_end, *slot, *bspstore;
	unsigned long rnats;

	/*
	 * cr_ifs=[rv:ifm], ifm=[....:sof(6)]
	 * nlocals=number of local registers in the faulting function
	 */
	nlocals = (regs->cr_ifs) & 0x7f;

	/*
	 * save_switch_stack does a flushrs and saves bspstore.
	 * on_kbs = actual number of registers saved on kernel backing store
	 *          (taking into accound potential RNATs)
	 *
	 * Note that this number can be greater than nlocals if the dirty
	 * parititions included more than one stack frame at the time we
	 * switched to KBS
	 */
	on_kbs   = ia64_rse_num_regs(kbs, (unsigned long *)sw->ar_bspstore);
	bspstore = (unsigned long *)regs->ar_bspstore;

	/*
	 * To simplify the logic, we calculate everything as if there was only
	 * one backing store i.e., the user one (UBS). We let it to peek/poke
	 * to figure out whether the register we're looking for really is
	 * on the UBS or on KBS.
	 *
	 * regs->ar_bsptore = address of last register saved on UBS (before switch)
	 *
	 * ubs_end = virtual end of the UBS (if everything had been spilled there)
	 *
	 * We know that ubs_end is the point where the last register on the
	 * stack frame we're interested in as been saved. So we need to walk
	 * our way backward to figure out what the BSP "was" for that frame,
	 * this will give us the location of r32. 
	 *
	 * bsp = "virtual UBS" address of r32 for our frame
	 *
	 * Finally, get compute the address of the register we're looking for
	 * using bsp as our base (move up again).
	 *
	 * Please note that in our case, we know that the register is necessarily
	 * on the KBS because we are only interested in the current frame at the moment
	 * we got the exception i.e., bsp is not changed until we switch to KBS.
	 */
	ubs_end = ia64_rse_skip_regs(bspstore, on_kbs);
	bsp     = ia64_rse_skip_regs(ubs_end, -nlocals);
	addr    = slot = ia64_rse_skip_regs(bsp, r1 - 32);

	DPRINT(("ubs_end=%p bsp=%p addr=%p slot=0x%lx\n",
		(void *) ubs_end, (void *) bsp, (void *) addr, ia64_rse_slot_num(addr)));
	
	ia64_peek(regs, current, (unsigned long)addr, val);

	/*
	 * addr will now contain the address of the RNAT for the register
	 */
	addr = ia64_rse_rnat_addr(addr);

	ia64_peek(regs, current, (unsigned long)addr, &rnats);
	DPRINT(("rnat @%p = 0x%lx\n", (void *) addr, rnats));
	
	if (nat)
		*nat = rnats >> ia64_rse_slot_num(slot) & 0x1;
}


static void
setreg(unsigned long regnum, unsigned long val, int nat, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs -1;
	unsigned long addr;
	unsigned long bitmask;
	unsigned long *unat;


	/*
	 * First takes care of stacked registers
	 */
 	if (regnum >= IA64_FIRST_STACKED_GR) {
		set_rse_reg(regs, regnum, val, nat);
		return;
	}

	/*
	 * Using r0 as a target raises a General Exception fault which has 
	 * higher priority than the Unaligned Reference fault.
	 */ 

	/*
	 * Now look at registers in [0-31] range and init correct UNAT
	 */
	if (GR_IN_SW(regnum)) {
		addr = (unsigned long)sw;
		unat = &sw->ar_unat;
	} else {
		addr = (unsigned long)regs;
		unat = &sw->caller_unat;
	}
	DPRINT(("tmp_base=%lx switch_stack=%s offset=%d\n",
		addr, unat==&sw->ar_unat ? "yes":"no", GR_OFFS(regnum)));
	/*
	 * add offset from base of struct
	 * and do it !
	 */
	addr += GR_OFFS(regnum);

	*(unsigned long *)addr = val;

	/*
	 * We need to clear the corresponding UNAT bit to fully emulate the load
	 * UNAT bit_pos = GR[r3]{8:3} form EAS-2.4
	 */
	bitmask   = __IA64_UL(1) << (addr >> 3 & 0x3f);
	DPRINT(("*0x%lx=0x%lx NaT=%d prev_unat @%p=%lx\n", addr, val, nat, (void *) unat, *unat));
	if (nat) {
		*unat |= bitmask;
	} else {
		*unat &= ~bitmask;
	}
	DPRINT(("*0x%lx=0x%lx NaT=%d new unat: %p=%lx\n", addr, val, nat, (void *) unat,*unat));
}

#define IA64_FPH_OFFS(r) (r - IA64_FIRST_ROTATING_FR)

static void
setfpreg(unsigned long regnum, struct ia64_fpreg *fpval, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	unsigned long addr;

	/*
	 * From EAS-2.5: FPDisableFault has higher priority than Unaligned
	 * Fault. Thus, when we get here, we know the partition is enabled.
	 * To update f32-f127, there are three choices:
	 *
	 *	(1) save f32-f127 to thread.fph and update the values there
	 *	(2) use a gigantic switch statement to directly access the registers
	 *	(3) generate code on the fly to update the desired register
	 *
	 * For now, we are using approach (1).
	 */
 	if (regnum >= IA64_FIRST_ROTATING_FR) {
		ia64_sync_fph(current);
		current->thread.fph[IA64_FPH_OFFS(regnum)] = *fpval;
	} else {
		/*
		 * pt_regs or switch_stack ?
		 */
		if (FR_IN_SW(regnum)) {
			addr = (unsigned long)sw;
		} else {
			addr = (unsigned long)regs;
		}
		
		DPRINT(("tmp_base=%lx offset=%d\n", addr, FR_OFFS(regnum)));

		addr += FR_OFFS(regnum);
		*(struct ia64_fpreg *)addr = *fpval;

		/*
	 	 * mark the low partition as being used now
		 *
		 * It is highly unlikely that this bit is not already set, but
		 * let's do it for safety.
	 	 */
		regs->cr_ipsr |= IA64_PSR_MFL;
	}
}

/*
 * Those 2 inline functions generate the spilled versions of the constant floating point
 * registers which can be used with stfX
 */
static inline void 
float_spill_f0(struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("stf.spill [%0]=f0" :: "r"(final) : "memory");
}

static inline void 
float_spill_f1(struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("stf.spill [%0]=f1" :: "r"(final) : "memory");
}

static void
getfpreg(unsigned long regnum, struct ia64_fpreg *fpval, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs -1;
	unsigned long addr;

	/*
	 * From EAS-2.5: FPDisableFault has higher priority than 
	 * Unaligned Fault. Thus, when we get here, we know the partition is 
	 * enabled.
	 *
	 * When regnum > 31, the register is still live and we need to force a save
	 * to current->thread.fph to get access to it.  See discussion in setfpreg()
	 * for reasons and other ways of doing this.
	 */
 	if (regnum >= IA64_FIRST_ROTATING_FR) {
		ia64_flush_fph(current);
		*fpval = current->thread.fph[IA64_FPH_OFFS(regnum)];
	} else {
		/*
		 * f0 = 0.0, f1= 1.0. Those registers are constant and are thus
	 	 * not saved, we must generate their spilled form on the fly
		 */
		switch(regnum) {
		case 0:
			float_spill_f0(fpval);
			break;
		case 1:
			float_spill_f1(fpval);
			break;
		default:
			/*
			 * pt_regs or switch_stack ?
			 */
			addr =  FR_IN_SW(regnum) ? (unsigned long)sw
						 : (unsigned long)regs;

			DPRINT(("is_sw=%d tmp_base=%lx offset=0x%x\n",
				FR_IN_SW(regnum), addr, FR_OFFS(regnum)));

			addr  += FR_OFFS(regnum);
			*fpval = *(struct ia64_fpreg *)addr;
		}
	}
}


static void
getreg(unsigned long regnum, unsigned long *val, int *nat, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs -1;
	unsigned long addr, *unat;

 	if (regnum >= IA64_FIRST_STACKED_GR) {
		get_rse_reg(regs, regnum, val, nat);
		return;
	}

	/*
	 * take care of r0 (read-only always evaluate to 0)
	 */
	if (regnum == 0) {
		*val = 0;
		if (nat)
			*nat = 0;
		return;
	}

	/*
	 * Now look at registers in [0-31] range and init correct UNAT
	 */
	if (GR_IN_SW(regnum)) {
		addr = (unsigned long)sw;
		unat = &sw->ar_unat;
	} else {
		addr = (unsigned long)regs;
		unat = &sw->caller_unat;
	}

	DPRINT(("addr_base=%lx offset=0x%x\n", addr,  GR_OFFS(regnum)));

	addr += GR_OFFS(regnum);

	*val  = *(unsigned long *)addr;

	/*
	 * do it only when requested
	 */
	if (nat)
		*nat  = (*unat >> (addr >> 3 & 0x3f)) & 0x1UL;
}

static void
emulate_load_updates(update_t type, load_store_t *ld, struct pt_regs *regs, unsigned long ifa)
{		
	/*
	 * IMPORTANT: 
	 * Given the way we handle unaligned speculative loads, we should
	 * not get to this point in the code but we keep this sanity check,
	 * just in case.
	 */
	if (ld->x6_op == 1 || ld->x6_op == 3) {
		printk(KERN_ERR __FUNCTION__": register update on speculative load, error\n");	
		die_if_kernel("unaligned reference on specualtive load with register update\n",
			      regs, 30);
	}


	/*
	 * at this point, we know that the base register to update is valid i.e.,
	 * it's not r0
	 */
	if (type == UPD_IMMEDIATE) {
		unsigned long imm;

		/* 
	 	 * Load +Imm: ldXZ r1=[r3],imm(9)
	 	 *
		 *
	   	 * form imm9: [13:19] contain the first 7 bits
      		 */		 
		imm = ld->x << 7 | ld->imm;

		/*
		 * sign extend (1+8bits) if m set
		 */
		if (ld->m) imm |= SIGN_EXT9;

		/*
		 * ifa == r3 and we know that the NaT bit on r3 was clear so
		 * we can directly use ifa.
		 */
		ifa += imm;

		setreg(ld->r3, ifa, 0, regs);

		DPRINT(("ld.x=%d ld.m=%d imm=%ld r3=0x%lx\n", ld->x, ld->m, imm, ifa));

	} else if (ld->m) {
		unsigned long r2;
		int nat_r2;

		/*
		 * Load +Reg Opcode: ldXZ r1=[r3],r2
		 *
		 * Note: that we update r3 even in the case of ldfX.a 
		 * (where the load does not happen)
		 *
		 * The way the load algorithm works, we know that r3 does not
		 * have its NaT bit set (would have gotten NaT consumption
		 * before getting the unaligned fault). So we can use ifa 
		 * which equals r3 at this point.
		 *
		 * IMPORTANT:
	 	 * The above statement holds ONLY because we know that we
		 * never reach this code when trying to do a ldX.s.
		 * If we ever make it to here on an ldfX.s then 
		 */
		getreg(ld->imm, &r2, &nat_r2, regs);
		
		ifa += r2;
		
		/*
		 * propagate Nat r2 -> r3
		 */
		setreg(ld->r3, ifa, nat_r2, regs);

		DPRINT(("imm=%d r2=%ld r3=0x%lx nat_r2=%d\n",ld->imm, r2, ifa, nat_r2));
	}
}


static int
emulate_load_int(unsigned long ifa, load_store_t *ld, struct pt_regs *regs)
{
	unsigned long val;
	unsigned int len = 1<< ld->x6_sz;

	/*
	 * the macro supposes sequential access (which is the case)
	 * if the first byte is an invalid address we return here. Otherwise
	 * there is a guard page at the top of the user's address page and 
	 * the first access would generate a NaT consumption fault and return
	 * with a SIGSEGV, which is what we want.
	 *
	 * Note: the first argument is ignored 
	 */
	if (access_ok(VERIFY_READ, (void *)ifa, len) < 0) {
		DPRINT(("verify area failed on %lx\n", ifa));
		return -1;
	}

	/*
	 * r0, as target, doesn't need to be checked because Illegal Instruction
	 * faults have higher priority than unaligned faults.
	 *
	 * r0 cannot be found as the base as it would never generate an 
	 * unaligned reference.
	 */

	/*
	 * ldX.a we don't try to emulate anything but we must
	 * invalidate the ALAT entry.
	 * See comment below for explanation on how we handle ldX.a
	 */
	if (ld->x6_op != 0x2) {
		/*
		 * we rely on the macros in unaligned.h for now i.e.,
		 * we let the compiler figure out how to read memory gracefully.
		 *
		 * We need this switch/case because the way the inline function
		 * works. The code is optimized by the compiler and looks like
		 * a single switch/case.
		 */
		switch(len) {
			case 2:
				val = ia64_get_unaligned((void *)ifa, 2);
				break;
			case 4:
				val = ia64_get_unaligned((void *)ifa, 4);
				break;
			case 8:
				val = ia64_get_unaligned((void *)ifa, 8);
				break;
			default:
				DPRINT(("unknown size: x6=%d\n", ld->x6_sz));
				return -1;
		}

		setreg(ld->r1, val, 0, regs);
	}

	/*
	 * check for updates on any kind of loads
	 */
	if (ld->op == 0x5 || ld->m)
		emulate_load_updates(ld->op == 0x5 ? UPD_IMMEDIATE: UPD_REG, ld, regs, ifa);

	/*
	 * handling of various loads (based on EAS2.4):
	 *
	 * ldX.acq (ordered load):
	 *	- acquire semantics would have been used, so force fence instead.
	 *
	 *
	 * ldX.c.clr (check load and clear):
	 *	- if we get to this handler, it's because the entry was not in the ALAT.
	 *	  Therefore the operation reverts to a normal load
	 *
	 * ldX.c.nc (check load no clear):
	 *	- same as previous one
	 *
	 * ldX.c.clr.acq (ordered check load and clear):
	 *	- same as above for c.clr part. The load needs to have acquire semantics. So
	 *	  we use the fence semantics which is stronger and thus ensures correctness.
	 *	
	 * ldX.a (advanced load):
	 *	- suppose ldX.a r1=[r3]. If we get to the unaligned trap it's because the 
	 * 	  address doesn't match requested size alignement. This means that we would 
	 *	  possibly need more than one load to get the result.
	 *
	 *	  The load part can be handled just like a normal load, however the difficult
	 *	  part is to get the right thing into the ALAT. The critical piece of information
	 * 	  in the base address of the load & size. To do that, a ld.a must be executed,
	 *	  clearly any address can be pushed into the table by using ld1.a r1=[r3]. Now
	 *	  if we use the same target register, we will be okay for the check.a instruction.
	 *	  If we look at the store, basically a stX [r3]=r1 checks the ALAT  for any entry
	 *	  which would overlap within [r3,r3+X] (the size of the load was store in the
	 *	  ALAT). If such an entry is found the entry is invalidated. But this is not good
	 *	  enough, take the following example:
	 *		r3=3
	 *		ld4.a r1=[r3]
	 *
	 *	  Could be emulated by doing:
	 *		ld1.a r1=[r3],1
	 *		store to temporary;
	 *		ld1.a r1=[r3],1
	 *		store & shift to temporary;
	 *		ld1.a r1=[r3],1
	 *		store & shift to temporary;
	 *		ld1.a r1=[r3]
	 *		store & shift to temporary;
	 * 		r1=temporary
	 *
	 *	  So int this case, you would get the right value is r1 but the wrong info in
	 *	  the ALAT.  Notice that you could do it in reverse to finish with address 3
	 *	  but you would still get the size wrong.  To get the size right, one needs to
	 *	  execute exactly the same kind of load. You could do it from a aligned
	 *	  temporary location, but you would get the address wrong.
	 *
	 *	  So no matter what, it is not possible to emulate an advanced load
	 *	  correctly. But is that really critical ?
	 *
	 *
	 *	  Now one has to look at how ld.a is used, one must either do a ld.c.* or
	 *	  chck.a.* to reuse the value stored in the ALAT. Both can "fail" (meaning no
	 *	  entry found in ALAT), and that's perfectly ok because:
	 *
	 *		- ld.c.*, if the entry is not present a  normal load is executed
	 *		- chk.a.*, if the entry is not present, execution jumps to recovery code
	 *
	 *	  In either case, the load can be potentially retried in another form.
	 *
	 *	  So it's okay NOT to do any actual load on an unaligned ld.a. However the ALAT
	 *	  must be invalidated for the register (so that's chck.a.*,ld.c.* don't pick up
	 *	  a stale entry later) The register base update MUST also be performed.
	 *	  
	 *	  Now what is the content of the register and its NaT bit in the case we don't
	 *	  do the load ?  EAS2.4, says (in case an actual load is needed)
	 *
	 *		- r1 = [r3], Nat = 0 if succeeds
	 *		- r1 = 0 Nat = 0 if trying to access non-speculative memory
	 *
	 *	  For us, there is nothing to do, because both ld.c.* and chk.a.* are going to
	 *	  retry and thus eventually reload the register thereby changing Nat and
	 *	  register content.
	 */

	/*
	 * when the load has the .acq completer then 
	 * use ordering fence.
	 */
	if (ld->x6_op == 0x5 || ld->x6_op == 0xa)
		mb();

	/*
	 * invalidate ALAT entry in case of advanced load
	 */
	if (ld->x6_op == 0x2)
		invala_gr(ld->r1);

	return 0;
}

static int
emulate_store_int(unsigned long ifa, load_store_t *ld, struct pt_regs *regs)
{
	unsigned long r2;
	unsigned int len = 1<< ld->x6_sz;
	
	/*
	 * the macro supposes sequential access (which is the case)
	 * if the first byte is an invalid address we return here. Otherwise
	 * there is a guard page at the top of the user's address page and 
	 * the first access would generate a NaT consumption fault and return
	 * with a SIGSEGV, which is what we want.
	 *
	 * Note: the first argument is ignored 
	 */
	if (access_ok(VERIFY_WRITE, (void *)ifa, len) < 0) {
		DPRINT(("verify area failed on %lx\n",ifa));
		return -1;
	}

	/*
	 * if we get to this handler, Nat bits on both r3 and r2 have already
	 * been checked. so we don't need to do it
	 *
	 * extract the value to be stored
	 */
	getreg(ld->imm, &r2, 0, regs);

	/*
	 * we rely on the macros in unaligned.h for now i.e.,
	 * we let the compiler figure out how to read memory gracefully.
	 *
	 * We need this switch/case because the way the inline function
	 * works. The code is optimized by the compiler and looks like
	 * a single switch/case.
	 */
	DPRINT(("st%d [%lx]=%lx\n", len, ifa, r2));

	switch(len) {
		case 2:
			ia64_put_unaligned(r2, (void *)ifa, 2);
			break;
		case 4:
			ia64_put_unaligned(r2, (void *)ifa, 4);
			break;
		case 8:
			ia64_put_unaligned(r2, (void *)ifa, 8);
			break;
		default:
			DPRINT(("unknown size: x6=%d\n", ld->x6_sz));
			return -1;
	}
	/*
	 * stX [r3]=r2,imm(9)
	 *
	 * NOTE:
	 * ld->r3 can never be r0, because r0 would not generate an 
	 * unaligned access.
	 */
	if (ld->op == 0x5) {
		unsigned long imm;

		/*
		 * form imm9: [12:6] contain first 7bits
		 */
		imm = ld->x << 7 | ld->r1;
		/*
		 * sign extend (8bits) if m set
		 */
		if (ld->m) imm |= SIGN_EXT9; 
		/*
		 * ifa == r3 (NaT is necessarily cleared)
		 */
		ifa += imm;

		DPRINT(("imm=%lx r3=%lx\n", imm, ifa));
	
		setreg(ld->r3, ifa, 0, regs);
	}
	/*
	 * we don't have alat_invalidate_multiple() so we need
	 * to do the complete flush :-<<
	 */
	ia64_invala();

	/*
	 * stX.rel: use fence instead of release
	 */
	if (ld->x6_op == 0xd)
		mb();

	return 0;
}

/*
 * floating point operations sizes in bytes
 */
static const unsigned short float_fsz[4]={
	16, /* extended precision (e) */
	8,  /* integer (8)            */
	4,  /* single precision (s)   */
	8   /* double precision (d)   */
};

static inline void 
mem2float_extended(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldfe f6=[%0];; stf.spill [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
mem2float_integer(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldf8 f6=[%0];; stf.spill [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
mem2float_single(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldfs f6=[%0];; stf.spill [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
mem2float_double(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldfd f6=[%0];; stf.spill [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
float2mem_extended(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldf.fill f6=[%0];; stfe [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
float2mem_integer(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldf.fill f6=[%0];; stf8 [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
float2mem_single(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldf.fill f6=[%0];; stfs [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static inline void 
float2mem_double(struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	__asm__ __volatile__ ("ldf.fill f6=[%0];; stfd [%1]=f6"
			      :: "r"(init), "r"(final) : "f6","memory");
}

static int
emulate_load_floatpair(unsigned long ifa, load_store_t *ld, struct pt_regs *regs)
{
	struct ia64_fpreg fpr_init[2];
	struct ia64_fpreg fpr_final[2];
	unsigned long len = float_fsz[ld->x6_sz];

	if (access_ok(VERIFY_READ, (void *)ifa, len<<1) < 0) {
		DPRINT(("verify area failed on %lx\n", ifa));
		return -1;
	}
	/*
	 * fr0 & fr1 don't need to be checked because Illegal Instruction
	 * faults have higher priority than unaligned faults.
	 *
	 * r0 cannot be found as the base as it would never generate an 
	 * unaligned reference.
	 */

	/* 
	 * make sure we get clean buffers
	 */
	memset(&fpr_init,0, sizeof(fpr_init));
	memset(&fpr_final,0, sizeof(fpr_final));

	/*
	 * ldfpX.a: we don't try to emulate anything but we must
	 * invalidate the ALAT entry and execute updates, if any.
	 */
	if (ld->x6_op != 0x2) {
		/*
		 * does the unaligned access
		 */
		memcpy(&fpr_init[0], (void *)ifa, len);
		memcpy(&fpr_init[1], (void *)(ifa+len), len);

		DPRINT(("ld.r1=%d ld.imm=%d x6_sz=%d\n", ld->r1, ld->imm, ld->x6_sz));
#ifdef DEBUG_UNALIGNED_TRAP
		{ int i; char *c = (char *)&fpr_init;
			printk("fpr_init= ");
			for(i=0; i < len<<1; i++ ) {
				printk("%02x ", c[i]&0xff);
			}
			printk("\n");
		}
#endif
		/*
		 * XXX fixme
		 * Could optimize inlines by using ldfpX & 2 spills 
		 */
		switch( ld->x6_sz ) {
			case 0:
				mem2float_extended(&fpr_init[0], &fpr_final[0]);
				mem2float_extended(&fpr_init[1], &fpr_final[1]);
				break;
			case 1:
				mem2float_integer(&fpr_init[0], &fpr_final[0]);
				mem2float_integer(&fpr_init[1], &fpr_final[1]);
				break;
			case 2:
				mem2float_single(&fpr_init[0], &fpr_final[0]);
				mem2float_single(&fpr_init[1], &fpr_final[1]);
				break;
			case 3:
				mem2float_double(&fpr_init[0], &fpr_final[0]);
				mem2float_double(&fpr_init[1], &fpr_final[1]);
				break;
		}
#ifdef DEBUG_UNALIGNED_TRAP
		{ int i; char *c = (char *)&fpr_final;
			printk("fpr_final= ");
			for(i=0; i < len<<1; i++ ) {
				printk("%02x ", c[i]&0xff);
			}
			printk("\n");
		}
#endif
		/*
		 * XXX fixme
		 *
		 * A possible optimization would be to drop fpr_final and directly
		 * use the storage from the saved context i.e., the actual final
		 * destination (pt_regs, switch_stack or thread structure).
		 */
		setfpreg(ld->r1, &fpr_final[0], regs);
		setfpreg(ld->imm, &fpr_final[1], regs);
	}

	/*
	 * Check for updates: only immediate updates are available for this
	 * instruction.
	 */
	if (ld->m) {

		/*
		 * the immediate is implicit given the ldsz of the operation:
		 * single: 8 (2x4) and for  all others it's 16 (2x8)
		 */
		ifa += len<<1;

		/*
		 * IMPORTANT: 
		 * the fact that we force the NaT of r3 to zero is ONLY valid
		 * as long as we don't come here with a ldfpX.s.
		 * For this reason we keep this sanity check
		 */
		if (ld->x6_op == 1 || ld->x6_op == 3) {
			printk(KERN_ERR "%s: register update on speculative load pair, error\n",
			       __FUNCTION__);	
		}


		setreg(ld->r3, ifa, 0, regs);
	}

	/*
	 * Invalidate ALAT entries, if any, for both registers.
	 */
	if (ld->x6_op == 0x2) {
		invala_fr(ld->r1);
		invala_fr(ld->imm);
	}
	return 0;
}


static int
emulate_load_float(unsigned long ifa, load_store_t *ld, struct pt_regs *regs)
{
	struct ia64_fpreg fpr_init;
	struct ia64_fpreg fpr_final;
	unsigned long len = float_fsz[ld->x6_sz];

	/*
	 * check for load pair because our masking scheme is not fine grain enough
	if (ld->x == 1) return emulate_load_floatpair(ifa,ld,regs);
	 */

	if (access_ok(VERIFY_READ, (void *)ifa, len) < 0) {
		DPRINT(("verify area failed on %lx\n", ifa));
		return -1;
	}
	/*
	 * fr0 & fr1 don't need to be checked because Illegal Instruction
	 * faults have higher priority than unaligned faults.
	 *
	 * r0 cannot be found as the base as it would never generate an 
	 * unaligned reference.
	 */


	/* 
	 * make sure we get clean buffers
	 */
	memset(&fpr_init,0, sizeof(fpr_init));
	memset(&fpr_final,0, sizeof(fpr_final));

	/*
	 * ldfX.a we don't try to emulate anything but we must
	 * invalidate the ALAT entry.
	 * See comments in ldX for descriptions on how the various loads are handled.
	 */
	if (ld->x6_op != 0x2) {

		/*
		 * does the unaligned access
		 */
		memcpy(&fpr_init, (void *)ifa, len);

		DPRINT(("ld.r1=%d x6_sz=%d\n", ld->r1, ld->x6_sz));
#ifdef DEBUG_UNALIGNED_TRAP
		{ int i; char *c = (char *)&fpr_init;
			printk("fpr_init= ");
			for(i=0; i < len; i++ ) {
				printk("%02x ", c[i]&0xff);
			}
			printk("\n");
		}
#endif
		/*
		 * we only do something for x6_op={0,8,9}
		 */
		switch( ld->x6_sz ) {
			case 0:
				mem2float_extended(&fpr_init, &fpr_final);
				break;
			case 1:
				mem2float_integer(&fpr_init, &fpr_final);
				break;
			case 2:
				mem2float_single(&fpr_init, &fpr_final);
				break;
			case 3:
				mem2float_double(&fpr_init, &fpr_final);
				break;
		}
#ifdef DEBUG_UNALIGNED_TRAP
		{ int i; char *c = (char *)&fpr_final;
			printk("fpr_final= ");
			for(i=0; i < len; i++ ) {
				printk("%02x ", c[i]&0xff);
			}
			printk("\n");
		}
#endif
		/*
		 * XXX fixme
		 *
		 * A possible optimization would be to drop fpr_final and directly
		 * use the storage from the saved context i.e., the actual final
		 * destination (pt_regs, switch_stack or thread structure).
		 */
		setfpreg(ld->r1, &fpr_final, regs);
	}

	/*
	 * check for updates on any loads
	 */
	if (ld->op == 0x7 || ld->m)
		emulate_load_updates(ld->op == 0x7 ? UPD_IMMEDIATE: UPD_REG, ld, regs, ifa);

	/*
	 * invalidate ALAT entry in case of advanced floating point loads
	 */
	if (ld->x6_op == 0x2)
		invala_fr(ld->r1);

	return 0;
}


static int
emulate_store_float(unsigned long ifa, load_store_t *ld, struct pt_regs *regs)
{
	struct ia64_fpreg fpr_init;
	struct ia64_fpreg fpr_final;
	unsigned long len = float_fsz[ld->x6_sz];
	
	/*
	 * the macro supposes sequential access (which is the case)
	 * if the first byte is an invalid address we return here. Otherwise
	 * there is a guard page at the top of the user's address page and 
	 * the first access would generate a NaT consumption fault and return
	 * with a SIGSEGV, which is what we want.
	 *
	 * Note: the first argument is ignored 
	 */
	if (access_ok(VERIFY_WRITE, (void *)ifa, len) < 0) {
		DPRINT(("verify area failed on %lx\n",ifa));
		return -1;
	}

	/* 
	 * make sure we get clean buffers
	 */
	memset(&fpr_init,0, sizeof(fpr_init));
	memset(&fpr_final,0, sizeof(fpr_final));


	/*
	 * if we get to this handler, Nat bits on both r3 and r2 have already
	 * been checked. so we don't need to do it
	 *
	 * extract the value to be stored
	 */
	getfpreg(ld->imm, &fpr_init, regs);
	/*
	 * during this step, we extract the spilled registers from the saved
	 * context i.e., we refill. Then we store (no spill) to temporary
	 * aligned location
	 */
	switch( ld->x6_sz ) {
		case 0:
			float2mem_extended(&fpr_init, &fpr_final);
			break;
		case 1:
			float2mem_integer(&fpr_init, &fpr_final);
			break;
		case 2:
			float2mem_single(&fpr_init, &fpr_final);
			break;
		case 3:
			float2mem_double(&fpr_init, &fpr_final);
			break;
	}
	DPRINT(("ld.r1=%d x6_sz=%d\n", ld->r1, ld->x6_sz));
#ifdef DEBUG_UNALIGNED_TRAP
		{ int i; char *c = (char *)&fpr_init;
			printk("fpr_init= ");
			for(i=0; i < len; i++ ) {
				printk("%02x ", c[i]&0xff);
			}
			printk("\n");
		}
		{ int i; char *c = (char *)&fpr_final;
			printk("fpr_final= ");
			for(i=0; i < len; i++ ) {
				printk("%02x ", c[i]&0xff);
			}
			printk("\n");
		}
#endif

	/*
	 * does the unaligned store
	 */
	memcpy((void *)ifa, &fpr_final, len);

	/*
	 * stfX [r3]=r2,imm(9)
	 *
	 * NOTE:
	 * ld->r3 can never be r0, because r0 would not generate an 
	 * unaligned access.
	 */
	if (ld->op == 0x7) {
		unsigned long imm;

		/*
		 * form imm9: [12:6] contain first 7bits
		 */
		imm = ld->x << 7 | ld->r1;
		/*
		 * sign extend (8bits) if m set
		 */
		if (ld->m)
			imm |= SIGN_EXT9; 
		/*
		 * ifa == r3 (NaT is necessarily cleared)
		 */
		ifa += imm;

		DPRINT(("imm=%lx r3=%lx\n", imm, ifa));
	
		setreg(ld->r3, ifa, 0, regs);
	}
	/*
	 * we don't have alat_invalidate_multiple() so we need
	 * to do the complete flush :-<<
	 */
	ia64_invala();

	return 0;
}

void
ia64_handle_unaligned(unsigned long ifa, struct pt_regs *regs)
{
	static unsigned long unalign_count;
	static long last_time;

	struct ia64_psr *ipsr = ia64_psr(regs);
	unsigned long *bundle_addr;
	unsigned long opcode;
	unsigned long op;
	load_store_t *insn;
	int ret = -1;

	/*
	 * Unaligned references in the kernel could come from unaligned
	 *   arguments to system calls.  We fault the user process in
	 *   these cases and panic the kernel otherwise (the kernel should
	 *   be fixed to not make unaligned accesses).
	 */
	if (!user_mode(regs)) {
		const struct exception_table_entry *fix;

		fix = search_exception_table(regs->cr_iip);
		if (fix) {
			regs->r8 = -EFAULT;
			if (fix->skip & 1) {
				regs->r9 = 0;
			}
			regs->cr_iip += ((long) fix->skip) & ~15;
			regs->cr_ipsr &= ~IA64_PSR_RI;	/* clear exception slot number */
			return;
		}
		die_if_kernel("Unaligned reference while in kernel\n", regs, 30);
		/* NOT_REACHED */
	}
	/*
	 * For now, we don't support user processes running big-endian
	 * which do unaligned accesses
	 */
	if (ia64_psr(regs)->be) {
		struct siginfo si;

		printk(KERN_ERR "%s(%d): big-endian unaligned access %016lx (ip=%016lx) not "
		       "yet supported\n",
		       current->comm, current->pid, ifa, regs->cr_iip + ipsr->ri);

		si.si_signo = SIGBUS;
		si.si_errno = 0;
		si.si_code = BUS_ADRALN;
		si.si_addr = (void *) ifa;
		force_sig_info(SIGBUS, &si, current);
		return;
	}

	if (current->thread.flags & IA64_THREAD_UAC_SIGBUS) {
		struct siginfo si;

		si.si_signo = SIGBUS;
		si.si_errno = 0;
		si.si_code = BUS_ADRALN;
		si.si_addr = (void *) ifa;
		force_sig_info(SIGBUS, &si, current);
		return;
	}

	if (!(current->thread.flags & IA64_THREAD_UAC_NOPRINT)) {
		/*
		 * Make sure we log the unaligned access, so that
		 * user/sysadmin can notice it and eventually fix the
		 * program.
		 *
		 * We don't want to do that for every access so we
		 * pace it with jiffies.
		 */
		if (unalign_count > 5 && jiffies - last_time > 5*HZ)
			unalign_count = 0;
		if (++unalign_count < 5) {
			char buf[200];	/* comm[] is at most 16 bytes... */
			size_t len;

			last_time = jiffies;
			len = sprintf(buf, "%s(%d): unaligned access to 0x%016lx, ip=0x%016lx\n\r",
				      current->comm, current->pid, ifa, regs->cr_iip + ipsr->ri);
			tty_write_message(current->tty, buf);
			buf[len-1] = '\0';	/* drop '\r' */
			printk("%s", buf);	/* guard against command names containing %s!! */
		}
	}

	DPRINT(("iip=%lx ifa=%lx isr=%lx\n", regs->cr_iip, ifa, regs->cr_ipsr));
	DPRINT(("ISR.ei=%d ISR.sp=%d\n", ipsr->ri, ipsr->it));

	bundle_addr = (unsigned long *)(regs->cr_iip);

	/*
	 * extract the instruction from the bundle given the slot number
	 */
	switch ( ipsr->ri ) {
		case 0: op = *bundle_addr >> 5;
			break;

		case 1: op = *bundle_addr >> 46 | (*(bundle_addr+1) & 0x7fffff)<<18;
			break;

		case 2: op = *(bundle_addr+1) >> 23;
		 	break;
	}

	insn   = (load_store_t *)&op;
	opcode = op & IA64_OPCODE_MASK;

	DPRINT(("opcode=%lx ld.qp=%d ld.r1=%d ld.imm=%d ld.r3=%d ld.x=%d ld.hint=%d "
		"ld.x6=0x%x ld.m=%d ld.op=%d\n",
		opcode,
		insn->qp,
		insn->r1,
		insn->imm,
		insn->r3,
		insn->x,
		insn->hint,
		insn->x6_sz,
		insn->m,
		insn->op));

	/*
  	 * IMPORTANT:
	 * Notice that the swictch statement DOES not cover all possible instructions
	 * that DO generate unaligned references. This is made on purpose because for some
	 * instructions it DOES NOT make sense to try and emulate the access. Sometimes it
	 * is WRONG to try and emulate. Here is a list of instruction we don't emulate i.e.,
	 * the program will get a signal and die:
	 *
	 *	load/store:
	 *		- ldX.spill
	 *		- stX.spill
	 * 	Reason: RNATs are based on addresses
	 *
	 *	synchronization:
	 *		- cmpxchg
	 *		- fetchadd
	 *		- xchg
	 * 	Reason: ATOMIC operations cannot be emulated properly using multiple 
	 * 	        instructions.
	 *
	 *	speculative loads:
	 *		- ldX.sZ
	 *	Reason: side effects, code must be ready to deal with failure so simpler 
	 * 		to let the load fail.
	 * ---------------------------------------------------------------------------------
	 * XXX fixme
	 *
	 * I would like to get rid of this switch case and do something
	 * more elegant.
	 */
	switch(opcode) {
		case LDS_OP:
		case LDSA_OP:
		case LDS_IMM_OP:
		case LDSA_IMM_OP:
		case LDFS_OP:
		case LDFSA_OP:
		case LDFS_IMM_OP:
			/*
			 * The instruction will be retried with defered exceptions
			 * turned on, and we should get Nat bit installed
			 *
			 * IMPORTANT:
			 * When PSR_ED is set, the register & immediate update
			 * forms are actually executed even though the operation
			 * failed. So we don't need to take care of this.
			 */
			DPRINT(("forcing PSR_ED\n"));
			regs->cr_ipsr |= IA64_PSR_ED;
			return;

		case LD_OP:
		case LDA_OP:
		case LDBIAS_OP:
		case LDACQ_OP:
		case LDCCLR_OP:
		case LDCNC_OP:
		case LDCCLRACQ_OP:
		case LD_IMM_OP:
		case LDA_IMM_OP:
		case LDBIAS_IMM_OP:
		case LDACQ_IMM_OP:
		case LDCCLR_IMM_OP:
		case LDCNC_IMM_OP:
		case LDCCLRACQ_IMM_OP:
			ret = emulate_load_int(ifa, insn, regs);
			break;
		case ST_OP:
		case STREL_OP:
		case ST_IMM_OP:
		case STREL_IMM_OP:
			ret = emulate_store_int(ifa, insn, regs);
			break;
		case LDF_OP:
		case LDFA_OP:
		case LDFCCLR_OP:
		case LDFCNC_OP:
		case LDF_IMM_OP:
		case LDFA_IMM_OP:
		case LDFCCLR_IMM_OP:
		case LDFCNC_IMM_OP:
			ret = insn->x ? 
			      emulate_load_floatpair(ifa, insn, regs):
			      emulate_load_float(ifa, insn, regs);
			break;
		case STF_OP:
		case STF_IMM_OP:
			ret = emulate_store_float(ifa, insn, regs);
	}

	DPRINT(("ret=%d\n", ret));
	if (ret) {
		struct siginfo si;

		si.si_signo = SIGBUS;
		si.si_errno = 0;
		si.si_code = BUS_ADRALN;
		si.si_addr = (void *) ifa;
	        force_sig_info(SIGBUS, &si, current);
	} else {
		/*
	 	 * given today's architecture this case is not likely to happen
	 	 * because a memory access instruction (M) can never be in the 
	 	 * last slot of a bundle. But let's keep it for  now.
	 	 */
		if (ipsr->ri == 2)
			regs->cr_iip += 16;
		ipsr->ri = ++ipsr->ri & 3;
	}

	DPRINT(("ipsr->ri=%d iip=%lx\n", ipsr->ri, regs->cr_iip));
}
