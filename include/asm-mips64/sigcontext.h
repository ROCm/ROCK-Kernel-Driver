/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIGCONTEXT_H
#define _ASM_SIGCONTEXT_H

/*
 * Keep this struct definition in sync with the sigcontext fragment
 * in arch/mips/tools/offset.c
 */
struct sigcontext {
	unsigned long long sc_regs[32];
	unsigned long long sc_fpregs[32];
	unsigned long long sc_mdhi;
	unsigned long long sc_mdlo;
	unsigned long long sc_pc;
	unsigned int       sc_status;
	unsigned int       sc_ownedfp;
	unsigned int       sc_fpc_csr;
	unsigned int       sc_fpc_eir;

	unsigned int       sc_cause;
	unsigned int       sc_badvaddr;
};

#endif /* _ASM_SIGCONTEXT_H */
