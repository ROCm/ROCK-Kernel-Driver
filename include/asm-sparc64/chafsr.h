/* $Id: chafsr.h,v 1.1 2001/03/28 10:56:34 davem Exp $ */
#ifndef _SPARC64_CHAFSR_H
#define _SPARC64_CHAFSR_H

/* Cheetah Asynchronous Fault Status register, ASI=0x4C VA<63:0>=0x0 */

/* All bits of this register except M_SYNDROME and E_SYNDROME are
 * read, write 1 to clear.  M_SYNDROME and E_SYNDROME are read-only.
 */

/* Multiple errors of the same type have occurred.  This bit is set when
 * an uncorrectable error or a SW correctable error occurs and the status
 * bit to report that error is already set.  When multiple errors of
 * different types are indicated by setting multiple status bits.
 *
 * This bit is not set if multiple HW corrected errors with the same
 * status bit occur, only uncorrectable and SW correctable ones have
 * this behavior.
 *
 * This bit is not set when multiple ECC errors happen within a single
 * 64-byte system bus transaction.  Only the first ECC error in a 16-byte
 * subunit will be logged.  All errors in subsequent 16-byte subunits
 * from the same 64-byte transaction are ignored.
 */
#define CHAFSR_ME		0x0020000000000000

/* Privileged state error has occurred.  This is a capture of PSTATE.PRIV
 * at the time the error is detected.
 */
#define CHAFSR_PRIV		0x0010000000000000

/* The following bits 51 (CHAFSR_PERR) to 33 (CHAFSR_CE) are sticky error
 * bits and record the most recently detected errors.  Bits accumulate
 * errors that have been detected since the last write to clear the bit.
 */

/* System interface protocol error.  The processor asserts its' ERROR
 * pin when this event occurs and it also logs a specific cause code
 * into a JTAG scannable flop.
 */
#define CHAFSR_PERR		0x0008000000000000

/* Internal processor error.  The processor asserts its' ERROR
 * pin when this event occurs and it also logs a specific cause code
 * into a JTAG scannable flop.
 */
#define CHAFSR_IERR		0x0004000000000000

/* System request parity error on incoming address */
#define CHAFSR_ISAP		0x0002000000000000

/* HW Corrected system bus MTAG ECC error */
#define CHAFSR_EMC		0x0001000000000000

/* Uncorrectable system bus MTAG ECC error */
#define CHAFSR_EMU		0x0000800000000000

/* HW Corrected system bus data ECC error for read of interrupt vector */
#define CHAFSR_IVC		0x0000400000000000

/* Uncorrectable system bus data ECC error for read of interrupt vector */
#define CHAFSR_IVU		0x0000200000000000

/* Unmappeed error from system bus */
#define CHAFSR_TO		0x0000100000000000

/* Bus error response from system bus */
#define CHAFSR_BERR		0x0000080000000000

/* SW Correctable E-cache ECC error for instruction fetch or data access
 * other than block load.
 */
#define CHAFSR_UCC		0x0000040000000000

/* Uncorrectable E-cache ECC error for instruction fetch or data access
 * other than block load.
 */
#define CHAFSR_UCU		0x0000020000000000

/* Copyout HW Corrected ECC error */
#define CHAFSR_CPC		0x0000010000000000

/* Copyout Uncorrectable ECC error */
#define CHAFSR_CPU		0x0000008000000000

/* HW Corrected ECC error from E-cache for writeback */
#define CHAFSR_WDC		0x0000004000000000

/* Uncorrectable ECC error from E-cache for writeback */
#define CHAFSR_WDU		0x0000002000000000

/* HW Corrected ECC error from E-cache for store merge or block load */
#define CHAFSR_EDC		0x0000001000000000

/* Uncorrectable ECC error from E-cache for store merge or block load */
#define CHAFSR_EDU		0x0000000800000000

/* Uncorrectable system bus data ECC error for read of memory or I/O */
#define CHAFSR_UE		0x0000000400000000

/* HW Corrected system bus data ECC error for read of memory or I/O */
#define CHAFSR_CE		0x0000000200000000

#define CHAFSR_ERRORS		(CHAFSR_PERR | CHAFSR_IERR | CHAFSR_ISAP | CHAFSR_EMC | \
				 CHAFSR_EMU | CHAFSR_IVC | CHAFSR_IVU | CHAFSR_TO | \
				 CHAFSR_BERR | CHAFSR_UCC | CHAFSR_UCU | CHAFSR_CPC | \
				 CHAFSR_CPU | CHAFSR_WDC | CHAFSR_WDU | CHAFSR_EDC | \
				 CHAFSR_EDU | CHAFSR_UE | CHAFSR_CE)

/* System bus MTAG ECC syndrome.  This field captures the status of the
 * first occurrence of the highest-priority error according to the M_SYND
 * overwrite policy.  After the AFSR sticky bit, corresponding to the error
 * for which the M_SYND is reported, is cleared, the contents of the M_SYND
 * field will be unchanged by will be unfrozen for further error capture.
 */
#define CHAFSR_M_SYNDROME	0x00000000000f0000
#define CHAFSR_M_SYNDROME_SHIFT	16

/* System bus or E-cache data ECC syndrome.  This field captures the status
 * of the first occurrence of the highest-priority error according to the
 * E_SYND overwrite policy.  After the AFSR sticky bit, corresponding to the
 * error for which the E_SYND is reported, is cleare, the contents of the E_SYND
 * field will be unchanged but will be unfrozen for further error capture.
 */
#define CHAFSR_E_SYNDROME	0x00000000000001ff
#define CHAFSR_E_SYNDROME_SHIFT	0

/* The AFSR must be explicitly cleared by software, it is not cleared automatically
 * by a read.  Writes to bits <51:33> with bits set will clear the corresponding
 * bits in the AFSR.  Bits assosciated with disrupting traps must be cleared before
 * interrupts are re-enabled to prevent multiple traps for the same error.  I.e.
 * PSTATE.IE and AFSR bits control delivery of disrupting traps.
 *
 * Since there is only one AFAR, when multiple events have been logged by the
 * bits in the AFSR, at most one of these events will have its status captured
 * in the AFAR.  The highest priority of those event bits will get AFAR logging.
 * The AFAR will be unlocked and available to capture the address of another event
 * as soon as the one bit in AFSR that corresponds to the event logged in AFAR is
 * cleared.  For example, if AFSR.CE is detected, then AFSR.UE (which overwrites
 * the AFAR), and AFSR.UE is cleared by not AFSR.CE, then the AFAR will be unlocked
 * and ready for another event, even though AFSR.CE is still set.  The same rules
 * also apply to the M_SYNDROME and E_SYNDROME fields of the AFSR.
 */

/* Software bit set by linux trap handlers to indicate that the trap was
 * signalled at %tl >= 1.
 */
#define CHAFSR_TL1		0x8000000000000000

#endif /* _SPARC64_CHAFSR_H */
