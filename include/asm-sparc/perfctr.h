/*----------------------------------------
  PERFORMANCE INSTRUMENTATION  
  Guillaume Thouvenin           08/10/98
  David S. Miller               10/06/98
  ---------------------------------------*/
#ifndef PERF_COUNTER_API
#define PERF_COUNTER_API

/* sys_perfctr() interface.  First arg is operation code
 * from enumeration below.  The meaning of further arguments
 * are determined by the operation code.
 *
 * int sys_perfctr(int opcode, unsigned long arg0,
 *                 unsigned long arg1, unsigned long arg2)
 *
 * Pointers which are passed by the user are pointers to 64-bit
 * integers.
 *
 * Once enabled, performance counter state is retained until the
 * process either exits or performs an exec.  That is, performance
 * counters remain enabled for fork/clone children.
 */
enum perfctr_opcode {
	/* Enable UltraSparc performance counters, ARG0 is pointer
	 * to 64-bit accumulator for D0 counter in PIC, ARG1 is pointer
	 * to 64-bit accumulator for D1 counter.  ARG2 is a pointer to
	 * the initial PCR register value to use.
	 */
	PERFCTR_ON,

	/* Disable UltraSparc performance counters.  The PCR is written
	 * with zero and the user counter accumulator pointers and
	 * working PCR register value are forgotten.
	 */
	PERFCTR_OFF,

	/* Add current D0 and D1 PIC values into user pointers given
	 * in PERFCTR_ON operation.  The PIC is cleared before returning.
	 */
	PERFCTR_READ,

	/* Clear the PIC register. */
	PERFCTR_CLRPIC,

	/* Begin using a new PCR value, the pointer to which is passed
	 * in ARG0.  The PIC is also cleared after the new PCR value is
	 * written.
	 */
	PERFCTR_SETPCR,

	/* Store in pointer given in ARG0 the current PCR register value
	 * being used.
	 */
	PERFCTR_GETPCR
};

/* I don't want the kernel's namespace to be polluted with this
 * stuff when this file is included.  --DaveM
 */
#ifndef __KERNEL__

#define  PRIV 0x00000001
#define  USR  0x00000002
#define  SYS  0x00000004

/* Pic.S0 Selection Bit Field Encoding  */
#define  CYCLE_CNT            0x00000000
#define  INSTR_CNT            0x00000010
#define  DISPATCH0_IC_MISS    0x00000020
#define  DISPATCH0_STOREBUF   0x00000030
#define  IC_REF               0x00000080
#define  DC_RD                0x00000090
#define  DC_WR                0x000000A0
#define  LOAD_USE             0x000000B0
#define  EC_REF               0x000000C0
#define  EC_WRITE_HIT_RDO     0x000000D0
#define  EC_SNOOP_INV         0x000000E0
#define  EC_RD_HIT            0x000000F0

/* Pic.S1 Selection Bit Field Encoding  */
#define  CYCLE_CNT_D1         0x00000000
#define  INSTR_CNT_D1         0x00000800
#define  DISPATCH0_IC_MISPRED 0x00001000
#define  DISPATCH0_FP_USE     0x00001800
#define  IC_HIT               0x00004000
#define  DC_RD_HIT            0x00004800
#define  DC_WR_HIT            0x00005000
#define  LOAD_USE_RAW         0x00005800
#define  EC_HIT               0x00006000
#define  EC_WB                0x00006800
#define  EC_SNOOP_CB          0x00007000
#define  EC_IT_HIT            0x00007800

struct vcounter_struct {
  unsigned long long vcnt0;
  unsigned long long vcnt1;
};

#endif /* !(__KERNEL__) */

#endif /* !(PERF_COUNTER_API) */
