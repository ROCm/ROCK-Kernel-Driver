/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_I386_PTRACE_H
#define __SYSDEP_I386_PTRACE_H

#include "uml-config.h"

#ifdef CONFIG_MODE_TT
#include "ptrace-tt.h"
#endif

#ifdef CONFIG_MODE_SKAS
#include "ptrace-skas.h"
#endif

#include "choose-mode.h"

struct uml_pt_regs {
	unsigned long args[6];
	long syscall;
	int is_user;
	union {
#ifdef CONFIG_MODE_TT
		void *tt;
#endif
#ifdef CONFIG_MODE_SKAS
		struct {
			unsigned long regs[HOST_FRAME_SIZE];
			unsigned long fp[HOST_FP_SIZE];
			unsigned long xfp[HOST_XFP_SIZE];
			unsigned long fault_addr;
			unsigned long fault_type;
			unsigned long trap_type;
		} skas;
#endif
	} mode;
};

#define EMPTY_UML_PT_REGS { \
	syscall : 	-1, \
	args : 		{ [0 ... 5] = 0 }, \
	is_user :	0 }

extern int mode_tt;

#define UPT_IP(r) \
	CHOOSE_MODE(SC_IP((r)->mode.tt), REGS_IP((r)->mode.skas.regs))
#define UPT_SP(r) \
	CHOOSE_MODE(SC_SP((r)->mode.tt), REGS_SP((r)->mode.skas.regs))
#define UPT_EFLAGS(r) \
	CHOOSE_MODE(SC_EFLAGS((r)->mode.tt), REGS_EFLAGS((r)->mode.skas.regs))
#define UPT_EAX(r) \
	CHOOSE_MODE(SC_EAX((r)->mode.tt), REGS_EAX((r)->mode.skas.regs))
#define UPT_EBX(r) \
	CHOOSE_MODE(SC_EBX((r)->mode.tt), REGS_EBX((r)->mode.skas.regs))
#define UPT_ECX(r) \
	CHOOSE_MODE(SC_ECX((r)->mode.tt), REGS_ECX((r)->mode.skas.regs))
#define UPT_EDX(r) \
	CHOOSE_MODE(SC_EDX((r)->mode.tt), REGS_EDX((r)->mode.skas.regs))
#define UPT_ESI(r) \
	CHOOSE_MODE(SC_ESI((r)->mode.tt), REGS_ESI((r)->mode.skas.regs))
#define UPT_EDI(r) \
	CHOOSE_MODE(SC_EDI((r)->mode.tt), REGS_EDI((r)->mode.skas.regs))
#define UPT_EBP(r) \
	CHOOSE_MODE(SC_EBP((r)->mode.tt), REGS_EBP((r)->mode.skas.regs))
#define UPT_ORIG_EAX(r) ((r)->syscall)
#define UPT_CS(r) \
	CHOOSE_MODE(SC_CS((r)->mode.tt), REGS_CS((r)->mode.skas.regs))
#define UPT_SS(r) \
	CHOOSE_MODE(SC_SS((r)->mode.tt), REGS_SS((r)->mode.skas.regs))
#define UPT_DS(r) \
	CHOOSE_MODE(SC_DS((r)->mode.tt), REGS_DS((r)->mode.skas.regs))
#define UPT_ES(r) \
	CHOOSE_MODE(SC_ES((r)->mode.tt), REGS_ES((r)->mode.skas.regs))
#define UPT_FS(r) \
	CHOOSE_MODE(SC_FS((r)->mode.tt), REGS_FS((r)->mode.skas.regs))
#define UPT_GS(r) \
	CHOOSE_MODE(SC_GS((r)->mode.tt), REGS_GS((r)->mode.skas.regs))
#define UPT_SC(r) ((r)->mode.tt)

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

#define UPT_SET_SYSCALL_RETURN(r, res) \
	CHOOSE_MODE(SC_SET_SYSCALL_RETURN((r)->mode.tt, (res)), \
                    REGS_SET_SYSCALL_RETURN((r)->mode.skas.regs, (res)))

#define UPT_RESTART_SYSCALL(r) \
	CHOOSE_MODE(SC_RESTART_SYSCALL((r)->mode.tt), \
		    REGS_RESTART_SYSCALL((r)->mode.skas.regs))

#define UPT_ORIG_SYSCALL(r) UPT_EAX(r)
#define UPT_SYSCALL_NR(r) ((r)->syscall)
#define UPT_SYSCALL_RET(r) UPT_EAX(r)

#define UPT_SEGV_IS_FIXABLE(r) \
	CHOOSE_MODE(SC_SEGV_IS_FIXABLE(r->mode.tt), \
		    REGS_SEGV_IS_FIXABLE(&r->mode.skas))

#define UPT_FAULT_ADDR(r) \
	CHOOSE_MODE(SC_FAULT_ADDR(r->mode.tt), \
		    REGS_FAULT_ADDR(&r->mode.skas))

#define UPT_FAULT_WRITE(r) \
	CHOOSE_MODE(SC_FAULT_WRITE(r->mode.tt), \
		    REGS_FAULT_WRITE(&r->mode.skas))

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
