#ifndef __ASM_I386_DPROBES_EXCLUDE_H__
#define __ASM_I386_DPROBES_EXCLUDE_H__

/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

/*
 * Exclude regions markers: We do not allow probes to be placed in these
 * code areas to _prevent_ recursion into dp_trap3. Note that this does
 * not eliminate the possibility of recursion completely. dp_trap3 handles
 * recursion by silently and permanently removing the recursed probe point.
 */
#include <linux/kprobes.h>
#include <asm/kwatch.h>

extern void kprobes_code_start(void);
extern void kprobes_code_end(void);
extern void kprobes_asm_code_start(void);
extern void kprobes_asm_code_end(void);
extern void dprobes_code_start(void);
extern void dprobes_code_end(void);
extern void dprobes_asm_code_start(void);
extern void dprobes_asm_code_end(void);
extern void dprobes_interpreter_code_start(void);
extern void dprobes_interpreter_code_end(void);

#ifdef CONFIG_DR_ALLOC
extern void kwatch_asm_start(void);
extern void kwatch_asm_end(void);
#endif

struct region {
	unsigned long start;
	unsigned long end;
};

#define NR_EXCLUDED_REGIONS 6
static struct region exclude[] = {
#ifdef CONFIG_DR_ALLOC
	{(unsigned long)kwatch_asm_start,
		(unsigned long)kwatch_asm_end},
#else	
	{0, 0},
#endif
	{(unsigned long)kprobes_code_start,
		(unsigned long)kprobes_code_end},
	{(unsigned long)kprobes_asm_code_start,
		(unsigned long)kprobes_asm_code_end},
	{(unsigned long)dprobes_code_start,
		(unsigned long)dprobes_code_end},
	{(unsigned long)dprobes_asm_code_start,
		(unsigned long)dprobes_asm_code_end},
	{(unsigned long)dprobes_interpreter_code_start,
		(unsigned long)dprobes_interpreter_code_end}
};

static inline int dprobes_excluded(unsigned long addr)
{
	int i;
	for (i = 0; i < NR_EXCLUDED_REGIONS; i++) {
		if (addr >= exclude[i].start && addr <= exclude[i].end) {
			return 1;
		}
	}
	return 0;
}

#endif /* __ASM_I386_DPROBES_EXCLUDE_H__ */
