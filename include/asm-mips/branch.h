/*
 * Branch and jump emulation.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 *
 * $Id: branch.h,v 1.2 1998/04/28 19:37:46 ralf Exp $
 */
#include <asm/ptrace.h>

extern inline int delay_slot(struct pt_regs *regs)
{
	return regs->cp0_cause & CAUSEF_BD;
}

extern int __compute_return_epc(struct pt_regs *regs);

extern inline int compute_return_epc(struct pt_regs *regs)
{
	if (!delay_slot(regs)) {
		regs->cp0_epc += 4;
		return 0;
	}

	return __compute_return_epc(regs);
}
