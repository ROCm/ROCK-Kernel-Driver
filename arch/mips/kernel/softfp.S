/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 2000 by Ralf Baechle
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

	subu	sp, 16
	.cprestore 20
	sw	ra, 16(sp)

	/* For now we assume that we get the opcode to simulate passed in as
	   an argument.  */
	move	t0, a0

	/*
	 * First table lookup using insn[5:0]
	 */
	la	t1, lowtab
	andi	t2, t0, 0x3f
	sll	t2, t2, 2
	addu	t1, t2
	lw	t1, (t1)
	jr	t1
	END(simfp)

/*
 * We only decode the lower 3 of the 5 bit in the fmt field.  That way we
 * can keep the jump table significantly shorter.
 */
#define FMT_switch(insn,opc,temp0,temp1)				\
insn:	srl	temp0, opc, 19;						\
	andi	temp0, 0x1c;						\
	la	temp1, insn##.tab;					\
	addu	temp0, temp1;						\
	lw	temp0, (temp0);						\
	jr	temp0;							\
									\
	.data;								\
insn##.tab:								\
	.word	insn##.s, insn##.d, unimp, unimp;			\
	.word	insn##.w, insn##.l, unimp, unimp;			\
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

FMT_switch(cvt.w,t0,t1,t2)

/* Convert a single fp to a fixed point integer.  */
cvt.w.s:
	srl	t1, t0, fs_shift	# Get source register
	andi	t1, 31
	jal	s_get_fpreg

	S_is_E_max(t1,t2,t3)
	beqz	t2, 3f
	/* Might be a NaN or Inf.  */
	S_get_F(t1,t2)
	beqz	t2, 2f

	/* It's a NaN.  IEEE says undefined.  */
	/* Is it a QNaN?  Then the result is a QNaN as well.  */
	S_is_QNaN(t1,t2)
	bltz	t2, 1f

	/* XXX Ok, it's a SNaN.  Signal invalid exception, if enabled.
	   For now we don't signal and supply a QNaN for result.  */

1:	li	t2, W_QNaN
	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg
	j	done
2:

	S_is_Inf(t1,t2)
	bnez	t2, 2f

	/* It's +/- Inf.  Set register to +/- max. integer.  */
	/* XXX Send invalid operation exception instead, if enabled.  */
	srl	t1, t1, 31		# Extract sign bit
	li	t2, 0x7fffffff
	addu	t2, t1

	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg
	j	done
2:
3:	

	/* But then it might be a denorm or zero?  */
	S_is_E_min(t1,t2)
	bnez	t2, 2f

	/* Ok, it's a denorm or zero.  */
	S_get_F(t1,t2)
	beqz	t2, 1f

	/* It's a denorm.  */
	/* XXX Should be signaling inexact exception, if enabled.  */
	/* Fall through.  */
1:
	/* Yes, it is a denorm or zero.  Supply a zero as result.  */
	move	t2, zero
	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg
	j	done
2:

	/* XXX Ok, it's a normal number.  We don't handle that case yet.
	   If we have fp hardware this case is unreached.  Add this for
	   full fp simulation.  */

	/* Done, return.  */
	lw	ra, 16(sp)
	addu	sp, 16
	jr	ra

/* Convert a double fp to a fixed point integer.  */
cvt.w.d:
	srl	t1, t0, fs_shift	# Get source register
	andi	t1, 31
	jal	d_get_fpreg

	D_is_E_max(t1,t2,t3,t4)
	beqz	t3, 3f

	/* Might be a NaN or Inf.  */
	D_get_F(t1,t2,t3)
	or	t3, t2
	beqz	t3, 2f

	/* It's a NaN.  IEEE says undefined.  */
	/* Is it a QNaN?  Then the result is a QNaN as well.  */
	D_is_QNaN(t1,t2,t3)
	bltz	t3, 1f

	/* XXX Ok, it's a SNaN.  Signal invalid exception, if enabled.
	   For now we don't signal and supply a QNaN for result.  */

1:	li	t2, W_QNaN
	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg
	j	done
2:

	D_is_Inf(t1,t2,t3)
	bnez	t3, 2f

	/* It's +/- Inf.  Set register to +/- max. integer.  */
	/* XXX Send invalid operation exception instead, if enabled.  */
	srl	t1, t1, 31		# Extract sign bit
	li	t2, 0x7fffffff
	addu	t2, t1

	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg
	j	done
2:
3:	

	/* But then it might be a denorm or zero?  */
	D_is_E_min(t1,t2,t3)
	bnez	t3, 2f

	/* Ok, it's a denorm or zero.  */
	D_get_F(t1,t2,t3)
	or	t3, t2
	beqz	t3, 1f

	/* It's a denorm.  */
	/* XXX Should be signaling inexact exception, if enabled.  */
	/* Fall through.  */
1:
	/* Yes, it is a denorm or zero.  Supply a zero as result.  */
	move	t2, zero
	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg
	j	done
2:

	/* XXX Ok, it's a normal number.  We don't handle that case yet.
	   If we have fp hardware this case is only reached if the value
	   of the source register exceeds the range which is representable
	   in a single precission register.  For now we kludge by returning
	   +/- maxint and don't signal overflow. */

	srl	t1, t1, 31		# Extract sign bit
	li	t2, 0x7fffffff
	addu	t2, t1

	srl	t1, t0, fd_shift	# Put result register
	andi	t1, 31
	jal	s_put_fpreg

	/* Done, return.  */
	lw	ra, 16(sp)
	addu	sp, 16
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

/* Get the single precission register which's number is in t1.  */
s_get_fpreg:
	.set	noat
	sll	AT, t1, 2
	sll	t1, 3
	addu	t1, AT
	la	AT, 1f
	addu	AT, t1
	jr	AT
	.set	at

1:	mfc1	t1, $0
	jr	ra
	mfc1	t1, $1
	jr	ra
	mfc1	t1, $2
	jr	ra
	mfc1	t1, $3
	jr	ra
	mfc1	t1, $4
	jr	ra
	mfc1	t1, $5
	jr	ra
	mfc1	t1, $6
	jr	ra
	mfc1	t1, $7
	jr	ra
	mfc1	t1, $8
	jr	ra
	mfc1	t1, $9
	jr	ra
	mfc1	t1, $10
	jr	ra
	mfc1	t1, $11
	jr	ra
	mfc1	t1, $12
	jr	ra
	mfc1	t1, $13
	jr	ra
	mfc1	t1, $14
	jr	ra
	mfc1	t1, $15
	jr	ra
	mfc1	t1, $16
	jr	ra
	mfc1	t1, $17
	jr	ra
	mfc1	t1, $18
	jr	ra
	mfc1	t1, $19
	jr	ra
	mfc1	t1, $20
	jr	ra
	mfc1	t1, $21
	jr	ra
	mfc1	t1, $22
	jr	ra
	mfc1	t1, $23
	jr	ra
	mfc1	t1, $24
	jr	ra
	mfc1	t1, $25
	jr	ra
	mfc1	t1, $26
	jr	ra
	mfc1	t1, $27
	jr	ra
	mfc1	t1, $28
	jr	ra
	mfc1	t1, $29
	jr	ra
	mfc1	t1, $30
	jr	ra
	mfc1	t1, $31
	jr	ra

/*
 * Put the value in t2 into the single precission register which's number
 * is in t1.
 */
s_put_fpreg:
	.set	noat
	sll	AT, t1, 2
	sll	t1, 3
	addu	t1, AT
	la	AT, 1f
	addu	AT, t1
	jr	AT
	.set	at
	
1:	mtc1	t2, $0
	jr	ra
	mtc1	t2, $1
	jr	ra
	mtc1	t2, $2
	jr	ra
	mtc1	t2, $3
	jr	ra
	mtc1	t2, $4
	jr	ra
	mtc1	t2, $5
	jr	ra
	mtc1	t2, $6
	jr	ra
	mtc1	t2, $7
	jr	ra
	mtc1	t2, $8
	jr	ra
	mtc1	t2, $9
	jr	ra
	mtc1	t2, $10
	jr	ra
	mtc1	t2, $11
	jr	ra
	mtc1	t2, $12
	jr	ra
	mtc1	t2, $13
	jr	ra
	mtc1	t2, $14
	jr	ra
	mtc1	t2, $15
	jr	ra
	mtc1	t2, $16
	jr	ra
	mtc1	t2, $17
	jr	ra
	mtc1	t2, $18
	jr	ra
	mtc1	t2, $19
	jr	ra
	mtc1	t2, $20
	jr	ra
	mtc1	t2, $21
	jr	ra
	mtc1	t2, $22
	jr	ra
	mtc1	t2, $23
	jr	ra
	mtc1	t2, $24
	jr	ra
	mtc1	t2, $25
	jr	ra
	mtc1	t2, $26
	jr	ra
	mtc1	t2, $27
	jr	ra
	mtc1	t2, $28
	jr	ra
	mtc1	t2, $29
	jr	ra
	mtc1	t2, $30
	jr	ra
	mtc1	t2, $31
	jr	ra

/* Get the double precission register which's number is in t1 into t1/t2.  */
d_get_fpreg:
	.set	noat
	sll	t1, 3
	la	AT, 1f
	addu	AT, t1
	jr	AT
	.set	at

1:	mfc1	t1, $0
	mfc1	t2, $1
	jr	ra
	mfc1	t1, $2
	mfc1	t2, $3
	jr	ra
	mfc1	t1, $4
	mfc1	t2, $5
	jr	ra
	mfc1	t1, $6
	mfc1	t2, $7
	jr	ra
	mfc1	t1, $8
	mfc1	t2, $9
	jr	ra
	mfc1	t1, $10
	mfc1	t2, $11
	jr	ra
	mfc1	t1, $12
	mfc1	t2, $13
	jr	ra
	mfc1	t1, $14
	mfc1	t2, $15
	jr	ra
	mfc1	t1, $16
	mfc1	t2, $17
	jr	ra
	mfc1	t1, $18
	mfc1	t2, $19
	jr	ra
	mfc1	t1, $20
	mfc1	t2, $21
	jr	ra
	mfc1	t1, $22
	mfc1	t2, $23
	jr	ra
	mfc1	t1, $24
	mfc1	t2, $25
	jr	ra
	mfc1	t1, $26
	mfc1	t2, $27
	jr	ra
	mfc1	t1, $28
	mfc1	t2, $29
	jr	ra
	mfc1	t1, $30
	mfc1	t2, $31
	jr	ra

/*
 * Send an invalid operation exception.
 */
invalid:
	lw	ra, 16(sp)
	addu	sp, 16
	jr	ra

/*
 * Done, just skip over the current instruction
 */
done:
	lw	ra, 16(sp)
	addu	sp, 16
	jr	ra

unimp:
	/* We've run into an yet unknown instruction.  This happens either
	   on new, yet unsupported CPU types or when the faulting instruction
	   is being executed for cache but has been overwritten in memory.  */
	LOCK_KERNEL
	move	a0, t0
	PRINT(KERN_DEBUG "FP support: unknown fp op %08lx, ")
	PRINT("please mail to ralf@gnu.org.\n")

	li	a0, SIGILL			# Die, sucker ...
	move	a1, $28
	jal	force_sig
	UNLOCK_KERNEL

	lw	ra, 16(sp)
	addu	sp, 16
	jr	ra

/*
 * Jump table for the lowest 6 bits of a cp1 instruction.
 */
	.data
lowtab:	.word	add,   sub,   mul,   div,   sqrt,  abs,   mov,   neg
	.word	round.l,trunc.l,ceil.l,floor.l,round.w,trunc.w,ceil.w,floor.w
	.word	unimp, unimp, unimp, unimp, unimp, unimp, unimp, unimp
	.word	unimp, unimp, unimp, unimp, unimp, unimp, unimp, unimp
	.word	cvt.s, cvt.d, unimp, unimp, cvt.w, cvt.l, unimp, unimp
	.word	unimp, unimp, unimp, unimp, unimp, unimp, unimp, unimp
	.word	c.f,   c.un,  c.eq,  c.ueq, c.olt, c.ult, c.ole, c.ule
	.word	c.sf,  c.ngle,c.seq, c.ngl, c.lt,  c.nge, c.le, c.ngt
