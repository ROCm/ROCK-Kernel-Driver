
#define H_Success	0
#define H_Busy		1	/* Hardware busy -- retry later */
#define H_Hardware	-1	/* Hardware error */
#define H_Function	-2	/* Function not supported */
#define H_Privilege	-3	/* Caller not privileged */
#define H_Parameter	-4	/* Parameter invalid, out-of-range or conflicting */
#define H_Bad_Mode	-5	/* Illegal msr value */
#define H_PTEG_Full	-6	/* PTEG is full */
#define H_Not_Found	-7	/* PTE was not found" */
#define H_Reserved_DABR	-8	/* DABR address is reserved by the hypervisor on this processor" */

/* Flags */
#define H_LARGE_PAGE		(1UL<<(63-16))
#define H_EXACT		    (1UL<<(63-24))	/* Use exact PTE or return H_PTEG_FULL */
#define H_R_XLATE		(1UL<<(63-25))	/* include a valid logical page num in the pte if the valid bit is set */
#define H_READ_4		(1UL<<(63-26))	/* Return 4 PTEs */
#define H_AVPN			(1UL<<(63-32))	/* An avpn is provided as a sanity test */
#define H_ANDCOND		(1UL<<(63-33))
#define H_ICACHE_INVALIDATE	(1UL<<(63-40))	/* icbi, etc.  (ignored for IO pages) */
#define H_ICACHE_SYNCHRONIZE	(1UL<<(63-41))	/* dcbst, icbi, etc (ignored for IO pages */
#define H_ZERO_PAGE		(1UL<<(63-48))	/* zero the page before mapping (ignored for IO pages) */
#define H_COPY_PAGE		(1UL<<(63-49))
#define H_N			(1UL<<(63-61))
#define H_PP1			(1UL<<(63-62))
#define H_PP2			(1UL<<(63-63))



/* pSeries hypervisor opcodes */
#define H_REMOVE		0x04
#define H_ENTER			0x08
#define H_READ			0x0c
#define H_CLEAR_MOD		0x10
#define H_CLEAR_REF		0x14
#define H_PROTECT		0x18
#define H_GET_TCE		0x1c
#define H_PUT_TCE		0x20
#define H_SET_SPRG0		0x24
#define H_SET_DABR		0x28
#define H_PAGE_INIT		0x2c
#define H_SET_ASR		0x30
#define H_ASR_ON		0x34
#define H_ASR_OFF		0x38
#define H_LOGICAL_CI_LOAD	0x3c
#define H_LOGICAL_CI_STORE	0x40
#define H_LOGICAL_CACHE_LOAD	0x44
#define H_LOGICAL_CACHE_STORE	0x48
#define H_LOGICAL_ICBI		0x4c
#define H_LOGICAL_DCBF		0x50
#define H_GET_TERM_CHAR		0x54
#define H_PUT_TERM_CHAR		0x58
#define H_REAL_TO_LOGICAL	0x5c
#define H_HYPERVISOR_DATA	0x60
#define H_EOI			0x64
#define H_CPPR			0x68
#define H_IPI			0x6c
#define H_IPOLL			0x70
#define H_XIRR			0x74
#define H_PERFMON		0x7c

/* plpar_hcall() -- Generic call interface using above opcodes
 *
 * The actual call interface is a hypervisor call instruction with
 * the opcode in R3 and input args in R4-R7.
 * Status is returned in R3 with variable output values in R4-R11.
 * Only H_PTE_READ with H_READ_4 uses R6-R11 so we ignore it for now
 * and return only two out args which MUST ALWAYS BE PROVIDED.
 */
long plpar_hcall(unsigned long opcode,
		 unsigned long arg1,
		 unsigned long arg2,
		 unsigned long arg3,
		 unsigned long arg4,
		 unsigned long *out1,
		 unsigned long *out2,
		 unsigned long *out3);

/* Same as plpar_hcall but for those opcodes that return no values
 * other than status.  Slightly more efficient.
 */
long plpar_hcall_norets(unsigned long opcode, ...);
