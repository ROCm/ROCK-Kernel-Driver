/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_I386_PTRACE_H
#define __SYSDEP_I386_PTRACE_H

#include "sysdep/sc.h"

struct uml_pt_regs {
	unsigned long args[6];
	long syscall;
	int is_user;
	void *sc;
};

#define EMPTY_UML_PT_REGS { \
	syscall : 	-1, \
	args : 		{ [0 ... 5] = 0 }, \
	is_user :	0, \
	sc : 		NULL }

#define UPT_IP(regs) SC_IP((regs)->sc)
#define UPT_SP(regs) SC_SP((regs)->sc)
#define UPT_EFLAGS(regs) SC_EFLAGS((regs)->sc)
#define UPT_EAX(regs) SC_EAX((regs)->sc)
#define UPT_EBX(regs) SC_EBX((regs)->sc)
#define UPT_ECX(regs) SC_ECX((regs)->sc)
#define UPT_EDX(regs) SC_EDX((regs)->sc)
#define UPT_ESI(regs) SC_ESI((regs)->sc)
#define UPT_EDI(regs) SC_EDI((regs)->sc)
#define UPT_EBP(regs) SC_EBP((regs)->sc)
#define UPT_ORIG_EAX(regs) ((regs)->syscall)
#define UPT_CS(regs) SC_CS((regs)->sc)
#define UPT_SS(regs) SC_SS((regs)->sc)
#define UPT_DS(regs) SC_DS((regs)->sc)
#define UPT_ES(regs) SC_ES((regs)->sc)
#define UPT_FS(regs) SC_FS((regs)->sc)
#define UPT_GS(regs) SC_GS((regs)->sc)
#define UPT_SC(regs) ((regs)->sc)

#define UPT_REG(regs, reg) \
	({	unsigned long val; \
		switch(reg){ \
		case EIP: val = UPT_IP(regs); break; \
		case UESP: val = UPT_SP(regs); break; \
		case EAX: val = UPT_EAX(regs); break; \
		case EBX: val = UPT_EBX(regs); break; \
		case ECX: val = UPT_ECX(regs); break; \
		case EDX: val = UPT_EDX(regs); break; \
		case ESI: val = UPT_ESI(regs); break; \
		case EDI: val = UPT_EDI(regs); break; \
		case EBP: val = UPT_EBP(regs); break; \
		case ORIG_EAX: val = UPT_ORIG_EAX(regs); break; \
		case CS: val = UPT_CS(regs); break; \
		case SS: val = UPT_SS(regs); break; \
		case DS: val = UPT_DS(regs); break; \
		case ES: val = UPT_ES(regs); break; \
		case FS: val = UPT_FS(regs); break; \
		case GS: val = UPT_GS(regs); break; \
		case EFL: val = UPT_EFLAGS(regs); break; \
		default :  \
			panic("Bad register in UPT_REG : %d\n", reg);  \
			val = -1; \
		} \
	        val; \
	})
	

#define UPT_SET(regs, reg, val) \
	do { \
		switch(reg){ \
		case EIP: UPT_IP(regs) = val; break; \
		case UESP: UPT_SP(regs) = val; break; \
		case EAX: UPT_EAX(regs) = val; break; \
		case EBX: UPT_EBX(regs) = val; break; \
		case ECX: UPT_ECX(regs) = val; break; \
		case EDX: UPT_EDX(regs) = val; break; \
		case ESI: UPT_ESI(regs) = val; break; \
		case EDI: UPT_EDI(regs) = val; break; \
		case EBP: UPT_EBP(regs) = val; break; \
		case ORIG_EAX: UPT_ORIG_EAX(regs) = val; break; \
		case CS: UPT_CS(regs) = val; break; \
		case SS: UPT_SS(regs) = val; break; \
		case DS: UPT_DS(regs) = val; break; \
		case ES: UPT_ES(regs) = val; break; \
		case FS: UPT_FS(regs) = val; break; \
		case GS: UPT_GS(regs) = val; break; \
		case EFL: UPT_EFLAGS(regs) = val; break; \
		default :  \
			panic("Bad register in UPT_SET : %d\n", reg);  \
			break; \
		} \
	} while (0)

#define UPT_SET_SYSCALL_RETURN(regs, res) \
	SC_SET_SYSCALL_RETURN((regs)->sc, (res))
#define UPT_RESTART_SYSCALL(regs) SC_RESTART_SYSCALL((regs)->sc)
#define UPT_ORIG_SYSCALL(regs) UPT_EAX(regs)
#define UPT_SYSCALL_NR(regs) ((regs)->syscall)
#define UPT_SYSCALL_RET(regs) UPT_EAX(regs)

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
