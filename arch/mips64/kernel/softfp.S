/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics, Inc.
 *
 * For now it's just a crude hack good enough to run certain fp programs like
 * Mozilla.
 * XXX: Handle MIPS II/III/IV/V enhancements, exceptions, ...
 */
#include <asm/regdef.h>
#include <asm/asm.h>

#ifndef __KERNEL__
#define printk printf
#endif

#define LOCK_KERNEL
#define UNLOCK_KERNEL

/*
 * This duplicates definitions from <linux/kernel.h>.
 */
#define KERN_EMERG      "<0>"   /* system is unusable                   */
#define KERN_ALERT      "<1>"   /* action must be taken immediately     */
#define KERN_CRIT       "<2>"   /* critical conditions                  */
#define KERN_ERR        "<3>"   /* error conditions                     */
#define KERN_WARNING    "<4>"   /* warning conditions                   */
#define KERN_NOTICE     "<5>"   /* normal but significant condition     */
#define KERN_INFO       "<6>"   /* informational                        */
#define KERN_DEBUG      "<7>"   /* debug-level messages                 */

/*
 * This duplicates definitions from <asm/signal.h>
 */
#define SIGILL           4      /* Illegal instruction (ANSI).  */

/*
 * Definitions about the instruction format
 */
#define fd_shift	6
#define fr_shift	21
#define fs_shift	11
#define ft_shift	16

/*
 * NaNs as use by the MIPS architecture
 */
#define S_QNaN		0x7fbfffff
#define D_QNaN		0x7ff7ffffffffffff
#define W_QNaN		0x7fffffff
#define L_QNaN		0x7fffffffffffffff

/*
 * Checking for NaNs
 */
#define S_is_QNaN(reg,res)						\
	sll	res, reg, S_F_size - S_F_bits
#define D_is_QNaN(reg1,reg2,res)					\
	sll	res, reg1, (D_F_size - 32) - (D_F_bits - 32);		\
	or	res, reg2

/*
 * Checking for Denorms
 */
#define S_is_Denorm(reg,res)						\
	li	res, 1 << (S_F_bits - 1);				\
	and	reg, res

/*
 * Some constants that define the properties of single precission numbers.
 */
#define S_M_prec	24
#define S_E_max		127
#define S_E_min		-126
#define S_E_bias	127
#define S_E_bits	8
#define S_F_bits	23
#define S_F_size	32

/* Set temp0, if exponent of reg is S_E_max + 1.  */
#define S_is_E_max(reg,temp0,temp1)					\
	li	temp0, (S_E_max + 1 + S_E_bias) << S_F_bits;		\
	and	temp1, temp0, reg;					\
	seq	temp0, temp1			/* temp0 != 0 if NaN */

/* Clear temp0, if exponent of reg is S_E_min - 1.  */
#define S_is_E_min(reg,temp0)						\
	li	temp0, (S_E_min - 1 + S_E_bias) << S_F_bits;		\
	and	temp0, reg	/* temp0 == 0 if denorm or zero */

/* Set temp0 if reg is a NaN assuming S_is_E_max is true  */
#define S_get_F(reg,temp0)						\
	li	temp0, (1 << S_F_bits) - 1;				\
	and	temp0, reg			/* temp0 != 0 if NaN */

/* Set res if fraction of reg is != 0.  */
#define S_is_Inf(reg,res)						\
	li	res, (1 << S_F_bits) - 1;				\
	and	res, reg			/* temp0 == 0 if Inf */


/*
 * Some constants that define the properties of double precission numbers.
 */
#define D_M_prec	53
#define D_E_max		1023
#define D_E_min		-1022
#define D_E_bias	1023
#define D_E_bits	8
#define D_F_bits	52
#define D_F_size	64

/* Set temp0, if exponent of reg1/reg2 is D_E_max.  */
#define D_is_E_max(reg1,reg2,temp0,temp1)				\
	li	temp0, (D_E_max + 1 + D_E_bias) << (D_F_bits - 32);	\
	and	temp1, temp0, reg1;					\
	seq	temp0, temp1			/* temp0 != 0 if NaN */

/* Clear temp0, if exponent of reg is D_E_min.  */
#define D_is_E_min(reg1,reg2,res)					\
	li	res, (D_E_min + 1 + D_E_bias) << (D_F_bits - 32);	\
	and	res, reg1	/* temp0 == 0 if NaN or zero */

/* Set res if reg is a NaN assuming S_is_E_max is true  */
#define D_get_F(reg1,reg2,res)						\
	li	res, (1 << (D_F_bits - 32)) - 1;			\
	and	res, reg1			/* temp0 != 0 if NaN */

/* Set temp0 if reg1/reg2 is a NaN  */
#define D_is_NAN(reg1,reg2,temp0,temp1)					\
	li	temp0, (1 << (D_F_bits - 32) - 1;			\
	and	temp0, reg1;						\
	or	temp0, reg2;						\
	sne	temp0, zero, temp0		/* temp0 != 0 if NaN */

/* Set res if fraction of reg1/reg2 is != 0.  */
#define D_is_Inf(reg1,reg2,res)						\
	li	res, (1 << (D_F_bits - 32)) - 1;			\
	and	res, reg1;						\
	or	res, reg2			/* temp0 == 0 if Inf */

/* Complain about yet unhandled instruction.  */
#define BITCH(insn)							\
insn:	LOCK_KERNEL;							\
	la	a1, 8f;							\
	TEXT(#insn);							\
	la	a0, nosim;						\
	jal	printk;							\
	UNLOCK_KERNEL;							\
	j	done

	.data
nosim: .asciz	KERN_DEBUG "Don't know how to simulate %s instruction\n"
	.previous

/*
 * When we come here, we've saved some of the integer registers and
 * reenabled interrupts.
 */
LEAF(simfp)
	.set	noreorder
	.cpload	$25
	.set	reorder

	dsubu	sp, 16
	.cprestore 20
	sd	ra, 0(sp)

	/* For now we assume that we get the opcode to simulate passed in as
	   an argument.  */
	move	ta0, a0

	/*
	 * First table lookup using insn[5:0]
	 */
	la	ta1, lowtab
	andi	ta2, ta0, 0x3f
	sll	ta2, ta2, 3
	daddu	ta1, ta2
	ld	ta1, (ta1)
	jr	ta1
	END(simfp)

/*
 * We only decode the lower 3 of the 5 bit in the fmt field.  That way we
 * can keep the jump table significantly shorter.
 */
#define FMT_switch(insn,opc,temp0,temp1)				\
insn:	srl	temp0, opc, 18;						\
	andi	temp0, 0x1c;						\
	la	temp1, insn##.tab;					\
	daddu	temp0, temp1;						\
	ld	temp0, (temp0);						\
	jr	temp0;							\
									\
	.data;								\
insn##.tab:								\
	.dword	insn##.s, insn##.d, unimp, unimp;			\
	.dword	insn##.w, insn##.l, unimp, unimp;			\
	.previous

	BITCH(add)
	BITCH(sub)
	BITCH(mul)
	BITCH(div)
	BITCH(sqrt)
	BITCH(abs)
	BITCH(mov)
	BITCH(neg)
	BITCH(round.l)
	BITCH(trunc.l)
	BITCH(ceil.l)
	BITCH(floor.l)
	BITCH(round.w)
	BITCH(trunc.w)
	BITCH(ceil.w)
	BITCH(floor.w)
	BITCH(cvt.s)
	BITCH(cvt.d)

/* ------------------------------------------------------------------------ */

FMT_switch(cvt.w,ta0,ta1,ta2)

/* Convert a single fp to a fixed point integer.  */
cvt.w.s:
	srl	ta1, ta0, fs_shift	# Get source register
	andi	ta1, 31
	jal	s_get_fpreg

	S_is_E_max(ta1,ta2,ta3)
	beqz	ta2, 3f
	/* Might be a NaN or Inf.  */
	S_get_F(ta1,ta2)
	beqz	ta2, 2f

	/* It's a NaN.  IEEE says undefined.  */
	/* Is it a QNaN?  Then the result is a QNaN as well.  */
	S_is_QNaN(ta1,ta2)
	bltz	ta2, 1f

	/* XXX Ok, it's a SNaN.  Signal invalid exception, if enabled.
	   For now we don't signal and supply a QNaN for result.  */

1:	li	ta2, W_QNaN
	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg
	j	done
2:

	S_is_Inf(ta1,ta2)
	bnez	ta2, 2f

	/* It's +/- Inf.  Set register to +/- max. integer.  */
	/* XXX Send invalid operation exception instead, if enabled.  */
	srl	ta1, ta1, 31		# Extract sign bit
	li	ta2, 0x7fffffff
	addu	ta2, ta1

	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg
	j	done
2:
3:	

	/* But then it might be a denorm or zero?  */
	S_is_E_min(ta1,ta2)
	bnez	ta2, 2f

	/* Ok, it's a denorm or zero.  */
	S_get_F(ta1,ta2)
	beqz	ta2, 1f

	/* It's a denorm.  */
	/* XXX Should be signaling inexact exception, if enabled.  */
	/* Fall through.  */
1:
	/* Yes, it is a denorm or zero.  Supply a zero as result.  */
	move	ta2, zero
	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg
	j	done
2:

	/* XXX Ok, it's a normal number.  We don't handle that case yet.
	   If we have fp hardware this case is unreached.  Add this for
	   full fp simulation.  */

	/* Done, return.  */
	ld	ra, 0(sp)
	daddu	sp, 16
	jr	ra

/* Convert a double fp to a fixed point integer.  */
cvt.w.d:
	srl	ta1, ta0, fs_shift	# Get source register
	andi	ta1, 31
	jal	d_get_fpreg

	D_is_E_max(ta1,ta2,ta3,t0)
	beqz	ta3, 3f

	/* Might be a NaN or Inf.  */
	D_get_F(ta1,ta2,ta3)
	or	ta3, ta2
	beqz	ta3, 2f

	/* It's a NaN.  IEEE says undefined.  */
	/* Is it a QNaN?  Then the result is a QNaN as well.  */
	D_is_QNaN(ta1,ta2,ta3)
	bltz	ta3, 1f

	/* XXX Ok, it's a SNaN.  Signal invalid exception, if enabled.
	   For now we don't signal and supply a QNaN for result.  */

1:	li	ta2, W_QNaN
	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg
	j	done
2:

	D_is_Inf(ta1,ta2,ta3)
	bnez	ta3, 2f

	/* It's +/- Inf.  Set register to +/- max. integer.  */
	/* XXX Send invalid operation exception instead, if enabled.  */
	srl	ta1, ta1, 31		# Extract sign bit
	li	ta2, 0x7fffffff
	addu	ta2, ta1

	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg
	j	done
2:
3:	

	/* But then it might be a denorm or zero?  */
	D_is_E_min(ta1,ta2,ta3)
	bnez	ta3, 2f

	/* Ok, it's a denorm or zero.  */
	D_get_F(ta1,ta2,ta3)
	or	ta3, ta2
	beqz	ta3, 1f

	/* It's a denorm.  */
	/* XXX Should be signaling inexact exception, if enabled.  */
	/* Fall through.  */
1:
	/* Yes, it is a denorm or zero.  Supply a zero as result.  */
	move	ta2, zero
	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg
	j	done
2:

	/* XXX Ok, it's a normal number.  We don't handle that case yet.
	   If we have fp hardware this case is only reached if the value
	   of the source register exceeds the range which is representable
	   in a single precission register.  For now we kludge by returning
	   +/- maxint and don't signal overflow. */

	srl	ta1, ta1, 31		# Extract sign bit
	li	ta2, 0x7fffffff
	addu	ta2, ta1

	srl	ta1, ta0, fd_shift	# Put result register
	andi	ta1, 31
	jal	s_put_fpreg

	/* Done, return.  */
	ld	ra, 0(sp)
	daddu	sp, 16
	jr	ra

cvt.w.w = unimp				# undefined result
cvt.w.l = unimp				# undefined result

/* MIPS III extension, no need to handle for 32bit OS.  */
cvt.l = unimp

/* ------------------------------------------------------------------------ */

	BITCH(c.f)
	BITCH(c.un)
	BITCH(c.eq)
	BITCH(c.ueq)
	BITCH(c.olt)
	BITCH(c.ult)
	BITCH(c.ole)
	BITCH(c.ule)
	BITCH(c.sf)
	BITCH(c.ngle)
	BITCH(c.seq)
	BITCH(c.ngl)
	BITCH(c.lt)
	BITCH(c.nge)
	BITCH(c.le)
	BITCH(c.ngt)

/* Get the single precission register which's number is in ta1.  */
s_get_fpreg:
	.set	noat
	sll	ta1, 3
	la	AT, 1f
	daddu	AT, ta1
	jr	AT
	.set	at

1:	mfc1	ta1, $0
	jr	ra
	mfc1	ta1, $1
	jr	ra
	mfc1	ta1, $2
	jr	ra
	mfc1	ta1, $3
	jr	ra
	mfc1	ta1, $4
	jr	ra
	mfc1	ta1, $5
	jr	ra
	mfc1	ta1, $6
	jr	ra
	mfc1	ta1, $7
	jr	ra
	mfc1	ta1, $8
	jr	ra
	mfc1	ta1, $9
	jr	ra
	mfc1	ta1, $10
	jr	ra
	mfc1	ta1, $11
	jr	ra
	mfc1	ta1, $12
	jr	ra
	mfc1	ta1, $13
	jr	ra
	mfc1	ta1, $14
	jr	ra
	mfc1	ta1, $15
	jr	ra
	mfc1	ta1, $16
	jr	ra
	mfc1	ta1, $17
	jr	ra
	mfc1	ta1, $18
	jr	ra
	mfc1	ta1, $19
	jr	ra
	mfc1	ta1, $20
	jr	ra
	mfc1	ta1, $21
	jr	ra
	mfc1	ta1, $22
	jr	ra
	mfc1	ta1, $23
	jr	ra
	mfc1	ta1, $24
	jr	ra
	mfc1	ta1, $25
	jr	ra
	mfc1	ta1, $26
	jr	ra
	mfc1	ta1, $27
	jr	ra
	mfc1	ta1, $28
	jr	ra
	mfc1	ta1, $29
	jr	ra
	mfc1	ta1, $30
	jr	ra
	mfc1	ta1, $31
	jr	ra

/*
 * Put the value in ta2 into the single precission register which's number
 * is in ta1.
 */
s_put_fpreg:
	.set	noat
	sll	ta1, 3
	la	AT, 1f
	daddu	AT, ta1
	jr	AT
	.set	at
	
1:	mtc1	ta2, $0
	jr	ra
	mtc1	ta2, $1
	jr	ra
	mtc1	ta2, $2
	jr	ra
	mtc1	ta2, $3
	jr	ra
	mtc1	ta2, $4
	jr	ra
	mtc1	ta2, $5
	jr	ra
	mtc1	ta2, $6
	jr	ra
	mtc1	ta2, $7
	jr	ra
	mtc1	ta2, $8
	jr	ra
	mtc1	ta2, $9
	jr	ra
	mtc1	ta2, $10
	jr	ra
	mtc1	ta2, $11
	jr	ra
	mtc1	ta2, $12
	jr	ra
	mtc1	ta2, $13
	jr	ra
	mtc1	ta2, $14
	jr	ra
	mtc1	ta2, $15
	jr	ra
	mtc1	ta2, $16
	jr	ra
	mtc1	ta2, $17
	jr	ra
	mtc1	ta2, $18
	jr	ra
	mtc1	ta2, $19
	jr	ra
	mtc1	ta2, $20
	jr	ra
	mtc1	ta2, $21
	jr	ra
	mtc1	ta2, $22
	jr	ra
	mtc1	ta2, $23
	jr	ra
	mtc1	ta2, $24
	jr	ra
	mtc1	ta2, $25
	jr	ra
	mtc1	ta2, $26
	jr	ra
	mtc1	ta2, $27
	jr	ra
	mtc1	ta2, $28
	jr	ra
	mtc1	ta2, $29
	jr	ra
	mtc1	ta2, $30
	jr	ra
	mtc1	ta2, $31
	jr	ra

/* Get the double precission register which's number is in ta1 into ta1/ta2.  */
d_get_fpreg:
	.set	noat
	sll	AT, ta1, 1
	sll	ta1, 2
	daddu	ta1, AT
	la	AT, 1f
	daddu	AT, ta1
	jr	AT
	.set	at

1:	mfc1	ta1, $0
	mfc1	ta2, $1
	jr	ra
	mfc1	ta1, $2
	mfc1	ta2, $3
	jr	ra
	mfc1	ta1, $4
	mfc1	ta2, $5
	jr	ra
	mfc1	ta1, $6
	mfc1	ta2, $7
	jr	ra
	mfc1	ta1, $8
	mfc1	ta2, $9
	jr	ra
	mfc1	ta1, $10
	mfc1	ta2, $11
	jr	ra
	mfc1	ta1, $12
	mfc1	ta2, $13
	jr	ra
	mfc1	ta1, $14
	mfc1	ta2, $15
	jr	ra
	mfc1	ta1, $16
	mfc1	ta2, $17
	jr	ra
	mfc1	ta1, $18
	mfc1	ta2, $19
	jr	ra
	mfc1	ta1, $20
	mfc1	ta2, $21
	jr	ra
	mfc1	ta1, $22
	mfc1	ta2, $23
	jr	ra
	mfc1	ta1, $24
	mfc1	ta2, $25
	jr	ra
	mfc1	ta1, $26
	mfc1	ta2, $27
	jr	ra
	mfc1	ta1, $28
	mfc1	ta2, $29
	jr	ra
	mfc1	ta1, $30
	mfc1	ta2, $31
	jr	ra

/*
 * Send an invalid operation exception.
 */
invalid:
	ld	ra, 0(sp)
	daddu	sp, 16
	jr	ra

/*
 * Done, just skip over the current instruction
 */
done:
	ld	ra, 0(sp)
	daddu	sp, 16
	jr	ra

unimp:
	/* We've run into an yet unknown instruction.  This happens either
	   on new, yet unsupported CPU types or when the faulting instruction
	   is being executed for cache but has been overwritten in memory.  */
	LOCK_KERNEL
	move	a1, ta0
	PRINT(KERN_DEBUG "FP support: unknown fp op %08lx, ")
	PRINT("please mail to ralf@gnu.org.\n")
	UNLOCK_KERNEL

	li	a0, SIGILL			# Die, sucker ...
	move	a1, $28
	jal	force_sig

	ld	ra, 0(sp)
	daddu	sp, 16
	jr	ra

/*
 * Jump table for the lowest 6 bits of a cp1 instruction.
 */
	.data
lowtab:	.dword	add,   sub,   mul,   div,   sqrt,  abs,   mov,   neg
	.dword	round.l,trunc.l,ceil.l,floor.l,round.w,trunc.w,ceil.w,floor.w
	.dword	unimp, unimp, unimp, unimp, unimp, unimp, unimp, unimp
	.dword	unimp, unimp, unimp, unimp, unimp, unimp, unimp, unimp
	.dword	cvt.s, cvt.d, unimp, unimp, cvt.w, cvt.l, unimp, unimp
	.dword	unimp, unimp, unimp, unimp, unimp, unimp, unimp, unimp
	.dword	c.f,   c.un,  c.eq,  c.ueq, c.olt, c.ult, c.ole, c.ule
	.dword	c.sf,  c.ngle,c.seq, c.ngl, c.lt,  c.nge, c.le, c.ngt
