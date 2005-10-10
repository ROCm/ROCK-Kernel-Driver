/*
 * This program is used to generate definitions needed by
 * some assembly functions.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#ifdef __KERNEL__
#include "archinclude.h"
#include "kernel_vars.h"
#include "mmu.h"
#else
#include "mol_config.h"
#include <stddef.h>
#include "mac_registers.h"
#endif

#include "processor.h"

#define DEFINE(sym, val) \
	asm volatile("\n#define\t" #sym "\t%0" : : "i" (val))

#define K_DEF(sym, val ) \
	DEFINE(sym, offsetof(kernel_vars_t, val ))

#define ST_DEF(sym, val ) \
	DEFINE(sym, offsetof(session_table_t, val ))

#define M_DEF(sym, val ) \
	DEFINE(sym, XOFFS + offsetof(mac_regs_t, val ))

#define IO_DEF(sym, val) \
	DEFINE(sym, offsetof(struct io_page, val ))

int main( void )
{
#ifdef __KERNEL__
	#define XOFFS	offsetof(kernel_vars_t, mregs)
#else
	#define XOFFS	0
#endif
	/* --- mac_regs offsets --- */

	M_DEF( xVEC_BASE, vec[0] );
	M_DEF( xVEC0, vec[0] );
	M_DEF( xVEC1, vec[1] );
	M_DEF( xVEC2, vec[2] );
	M_DEF( xVSCR, vscr );
	M_DEF( xVRSAVE, spr[S_VRSAVE] );

	M_DEF( xGPR_BASE, gpr[0] );
	M_DEF( xGPR0, gpr[0] );
	M_DEF( xGPR1, gpr[1] );
	M_DEF( xGPR2, gpr[2] );
	M_DEF( xGPR3, gpr[3] );
	M_DEF( xGPR4, gpr[4] );
	M_DEF( xGPR5, gpr[5] );
	M_DEF( xGPR6, gpr[6] );
	M_DEF( xGPR7, gpr[7] );
	M_DEF( xGPR8, gpr[8] );
	M_DEF( xGPR9, gpr[9] );
	M_DEF( xGPR10, gpr[10] );
	M_DEF( xGPR11, gpr[11] );
	M_DEF( xGPR12, gpr[12] );
	M_DEF( xGPR13, gpr[13] );
	M_DEF( xGPR14, gpr[14] );
	M_DEF( xGPR15, gpr[15] );
	M_DEF( xGPR16, gpr[16] );
	M_DEF( xGPR17, gpr[17] );
	M_DEF( xGPR18, gpr[18] );
	M_DEF( xGPR19, gpr[19] );
	M_DEF( xGPR20, gpr[20] );
	M_DEF( xGPR21, gpr[21] );
	M_DEF( xGPR22, gpr[22] );
	M_DEF( xGPR23, gpr[23] );
	M_DEF( xGPR24, gpr[24] );
	M_DEF( xGPR25, gpr[25] );
	M_DEF( xGPR26, gpr[26] );
	M_DEF( xGPR27, gpr[27] );
	M_DEF( xGPR28, gpr[28] );
	M_DEF( xGPR29, gpr[29] );
	M_DEF( xGPR30, gpr[30] );
	M_DEF( xGPR31, gpr[31] );

	M_DEF( xNIP, nip);
	M_DEF( xCR, cr);
	M_DEF( xFPR_BASE, fpr[0]);
	M_DEF( xFPR13, fpr[13]);
	M_DEF( xFPSCR, fpscr );
	M_DEF( xEMULATOR_FPSCR, emulator_fpscr );
	M_DEF( xFPU_STATE, fpu_state );

	M_DEF( xLINK, link);
	M_DEF( xXER, xer);
	M_DEF( xCTR, ctr);
	M_DEF( xFLAG_BITS, flag_bits );
	M_DEF( xDEC, spr[S_DEC]);
	M_DEF( xDEC_STAMP, dec_stamp);
	M_DEF( xTIMER_STAMP, timer_stamp);
	M_DEF( xMSR, msr);	
	M_DEF( xSPR_BASE, spr[0]);

	M_DEF( xHID0, spr[S_HID0]);

	M_DEF( xSRR0, spr[S_SRR0]);
	M_DEF( xSRR1, spr[S_SRR1]);

	M_DEF( xSPRG0, spr[S_SPRG0]);
	M_DEF( xSPRG1, spr[S_SPRG1]);
	M_DEF( xSPRG2, spr[S_SPRG2]);
	M_DEF( xSPRG3, spr[S_SPRG3]);

	M_DEF( xSEGR_BASE, segr[0]);
	M_DEF( xIBAT_BASE, spr[S_IBAT0U] );
	M_DEF( xSDR1, spr[S_SDR1] );

	M_DEF( xINST_OPCODE, inst_opcode );
	M_DEF( xALTIVEC_USED, altivec_used );
	M_DEF( xNO_ALTIVEC, no_altivec );

	M_DEF( xINTERRUPT, interrupt );
	M_DEF( xIN_VIRTUAL_MODE, in_virtual_mode );

	M_DEF( xRVEC_PARAM0, rvec_param[0] );
	M_DEF( xRVEC_PARAM1, rvec_param[1] );
	M_DEF( xRVEC_PARAM2, rvec_param[2] );

#ifdef EMULATE_603
	M_DEF( xGPRSAVE0_603, gprsave_603[0] );
	M_DEF( xGPRSAVE1_603, gprsave_603[1] );
	M_DEF( xGPRSAVE2_603, gprsave_603[2] );
	M_DEF( xGPRSAVE3_603, gprsave_603[3] );
#endif

	M_DEF( xDEBUG0, debug[0] );
	M_DEF( xDEBUG1, debug[1] );
	M_DEF( xDEBUG2, debug[2] );
	M_DEF( xDEBUG3, debug[3] );
	M_DEF( xDEBUG4, debug[4] );
	M_DEF( xDEBUG5, debug[5] );
	M_DEF( xDEBUG6, debug[6] );
	M_DEF( xDEBUG7, debug[7] );
	M_DEF( xDEBUG8, debug[8] );
	M_DEF( xDEBUG9, debug[9] );

	M_DEF( xDEBUG_SCR1, debug_scr1 );
	M_DEF( xDEBUG_SCR2, debug_scr2 );
	M_DEF( xDEBUG_TRACE, debug_trace );
	M_DEF( xDBG_TRACE_SPACE, dbg_trace_space[0] );
	M_DEF( xDBG_LAST_RVEC, dbg_last_rvec );

	M_DEF( xKERNEL_DBG_STOP, kernel_dbg_stop );

	return 0;
}

