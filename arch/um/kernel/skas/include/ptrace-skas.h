/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __PTRACE_SKAS_H
#define __PTRACE_SKAS_H

#include "uml-config.h"

#ifdef UML_CONFIG_MODE_SKAS

#include "skas_ptregs.h"

#define HOST_FRAME_SIZE 17

#define REGS_IP(r) ((r)[HOST_IP])
#define REGS_SP(r) ((r)[HOST_SP])
#define REGS_EFLAGS(r) ((r)[HOST_EFLAGS])
#define REGS_EAX(r) ((r)[HOST_EAX])
#define REGS_EBX(r) ((r)[HOST_EBX])
#define REGS_ECX(r) ((r)[HOST_ECX])
#define REGS_EDX(r) ((r)[HOST_EDX])
#define REGS_ESI(r) ((r)[HOST_ESI])
#define REGS_EDI(r) ((r)[HOST_EDI])
#define REGS_EBP(r) ((r)[HOST_EBP])
#define REGS_CS(r) ((r)[HOST_CS])
#define REGS_SS(r) ((r)[HOST_SS])
#define REGS_DS(r) ((r)[HOST_DS])
#define REGS_ES(r) ((r)[HOST_ES])
#define REGS_FS(r) ((r)[HOST_FS])
#define REGS_GS(r) ((r)[HOST_GS])

#define REGS_SET_SYSCALL_RETURN(r, res) REGS_EAX(r) = (res)

#define REGS_RESTART_SYSCALL(r) IP_RESTART_SYSCALL(REGS_IP(r))

#define REGS_SEGV_IS_FIXABLE(r) SEGV_IS_FIXABLE((r)->trap_type)

#define REGS_FAULT_ADDR(r) ((r)->fault_addr)

#define REGS_FAULT_WRITE(r) FAULT_WRITE((r)->fault_type)

#endif

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
