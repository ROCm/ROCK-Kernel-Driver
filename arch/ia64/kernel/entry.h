/*
 * Preserved registers that are shared between code in ivt.S and entry.S.  Be
 * careful not to step on these!
 */
#define pKern		p2	/* will leave_kernel return to kernel-mode? */
#define pSys		p4	/* are we processing a (synchronous) system call? */
#define pNonSys		p5	/* complement of pSys */

#define PT(f)		(IA64_PT_REGS_##f##_OFFSET + 16)
#define SW(f)		(IA64_SWITCH_STACK_##f##_OFFSET + 16)

#define PT_REGS_SAVES(off)				\
	UNW(.unwabi @svr4, 'i');			\
	UNW(.fframe IA64_PT_REGS_SIZE+16+(off));	\
	UNW(.spillsp rp, PT(CR_IIP)+(off));		\
	UNW(.spillsp ar.pfs, PT(CR_IFS)+(off));		\
	UNW(.spillsp ar.unat, PT(AR_UNAT)+(off));	\
	UNW(.spillsp ar.fpsr, PT(AR_FPSR)+(off));	\
	UNW(.spillsp pr, PT(PR)+(off));

#define PT_REGS_UNWIND_INFO(off)		\
	UNW(.prologue);				\
	PT_REGS_SAVES(off);			\
	UNW(.body)

#define SWITCH_STACK_SAVES(off)									  \
	UNW(.savesp ar.unat,SW(CALLER_UNAT)+(off)); UNW(.savesp ar.fpsr,SW(AR_FPSR)+(off));	  \
	UNW(.spillsp f2,SW(F2)+(off)); UNW(.spillsp f3,SW(F3)+(off));				  \
	UNW(.spillsp f4,SW(F4)+(off)); UNW(.spillsp f5,SW(F5)+(off));				  \
	UNW(.spillsp f16,SW(F16)+(off)); UNW(.spillsp f17,SW(F17)+(off));			  \
	UNW(.spillsp f18,SW(F18)+(off)); UNW(.spillsp f19,SW(F19)+(off));			  \
	UNW(.spillsp f20,SW(F20)+(off)); UNW(.spillsp f21,SW(F21)+(off));			  \
	UNW(.spillsp f22,SW(F22)+(off)); UNW(.spillsp f23,SW(F23)+(off));			  \
	UNW(.spillsp f24,SW(F24)+(off)); UNW(.spillsp f25,SW(F25)+(off));			  \
	UNW(.spillsp f26,SW(F26)+(off)); UNW(.spillsp f27,SW(F27)+(off));			  \
	UNW(.spillsp f28,SW(F28)+(off)); UNW(.spillsp f29,SW(F29)+(off));			  \
	UNW(.spillsp f30,SW(F30)+(off)); UNW(.spillsp f31,SW(F31)+(off));			  \
	UNW(.spillsp r4,SW(R4)+(off)); UNW(.spillsp r5,SW(R5)+(off));				  \
	UNW(.spillsp r6,SW(R6)+(off)); UNW(.spillsp r7,SW(R7)+(off));				  \
	UNW(.spillsp b0,SW(B0)+(off)); UNW(.spillsp b1,SW(B1)+(off));				  \
	UNW(.spillsp b2,SW(B2)+(off)); UNW(.spillsp b3,SW(B3)+(off));				  \
	UNW(.spillsp b4,SW(B4)+(off)); UNW(.spillsp b5,SW(B5)+(off));				  \
	UNW(.spillsp ar.pfs,SW(AR_PFS)+(off)); UNW(.spillsp ar.lc,SW(AR_LC)+(off));		  \
	UNW(.spillsp @priunat,SW(AR_UNAT)+(off));						  \
	UNW(.spillsp ar.rnat,SW(AR_RNAT)+(off)); UNW(.spillsp ar.bspstore,SW(AR_BSPSTORE)+(off)); \
	UNW(.spillsp pr,SW(PR)+(off))

#define DO_SAVE_SWITCH_STACK			\
	movl r28=1f;				\
	;;					\
	.fframe IA64_SWITCH_STACK_SIZE;		\
	adds sp=-IA64_SWITCH_STACK_SIZE,sp;	\
	mov b7=r28;				\
	SWITCH_STACK_SAVES(0);			\
	br.cond.sptk.many save_switch_stack;	\
1:

#define DO_LOAD_SWITCH_STACK(extra)		\
	movl r28=1f;				\
	;;					\
	mov b7=r28;				\
	br.cond.sptk.many load_switch_stack;	\
1:	UNW(.restore sp);			\
	extra;					\
	adds sp=IA64_SWITCH_STACK_SIZE,sp
