#ifndef ASM_IA64_SN_SYNERGY_H
#define ASM_IA64_SN_SYNERGY_H

#include "asm/io.h"
#include "asm/sn/intr_public.h"


/*
 * Definitions for the synergy asic driver
 * 
 * These are for SGI platforms only.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc
 * Copyright (C) 2000 Alan Mayer (ajm@sgi.com)
 */


#define SSPEC_BASE	(0xe0000000000)
#define LB_REG_BASE	(SSPEC_BASE + 0x0)

#define VEC_MASK3A_ADDR	(0x2a0 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK3B_ADDR	(0x2a8 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK3A	(0x2a0)
#define VEC_MASK3B	(0x2a8)

#define VEC_MASK2A_ADDR	(0x2b0 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK2B_ADDR	(0x2b8 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK2A	(0x2b0)
#define VEC_MASK2B	(0x2b8)

#define VEC_MASK1A_ADDR	(0x2c0 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK1B_ADDR	(0x2c8 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK1A	(0x2c0)
#define VEC_MASK1B	(0x2c8)

#define VEC_MASK0A_ADDR	(0x2d0 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK0B_ADDR	(0x2d8 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define VEC_MASK0A	(0x2d0)
#define VEC_MASK0B	(0x2d8)

#define WRITE_LOCAL_SYNERGY_REG(addr, value)	__synergy_out(addr, value)

#define HUBREG_CAST             (volatile hubreg_t *)
#define NODE_OFFSET(_n)         (UINT64_CAST (_n) << NODE_SIZE_BITS)
#define SYN_UNCACHED_SPACE      0xc000000000000000
#define NODE_HSPEC_BASE(_n)     (HSPEC_BASE + NODE_OFFSET(_n))
#define NODE_LREG_BASE(_n)      (NODE_HSPEC_BASE(_n) + 0x30000000)
#define RREG_BASE(_n)           (NODE_LREG_BASE(_n))
#define REMOTE_HSPEC(_n, _x)    (HUBREG_CAST (RREG_BASE(_n) + (_x)))
#define    HSPEC_SYNERGY0_0          0x04000000    /* Synergy0 Registers     */
#define    HSPEC_SYNERGY1_0          0x05000000    /* Synergy1 Registers     */
#define HS_SYNERGY_STRIDE               (HSPEC_SYNERGY1_0 - HSPEC_SYNERGY0_0)


#define HUB_L(_a)                       *(_a)
#define HUB_S(_a, _d)                   *(_a) = (_d)


#define REMOTE_SYNERGY_LOAD(nasid, fsb, reg)  __remote_synergy_in(nasid, fsb, reg)
#define REMOTE_SYNERGY_STORE(nasid, fsb, reg, val) __remote_synergy_out(nasid, fsb, reg, val)

extern inline void
__remote_synergy_out(int nasid, int fsb, unsigned long reg, unsigned long val) {
	unsigned long addr = ((RREG_BASE(nasid)) + 
		((HSPEC_SYNERGY0_0 | (fsb)*HS_SYNERGY_STRIDE) | ((reg) << 2)));

	HUB_S((unsigned long *)(addr),      (val) >> 48);
	HUB_S((unsigned long *)(addr+0x08), (val) >> 32);
	HUB_S((unsigned long *)(addr+0x10), (val) >> 16);
	HUB_S((unsigned long *)(addr+0x18), (val)      );
	__ia64_mf_a();
}

extern inline unsigned long
__remote_synergy_in(int nasid, int fsb, unsigned long reg) {
	volatile unsigned long *addr = (unsigned long *) ((RREG_BASE(nasid)) + 
		((HSPEC_SYNERGY0_0 | (fsb)*HS_SYNERGY_STRIDE) | (reg)));
	unsigned long ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

extern inline void
__synergy_out(unsigned long addr, unsigned long value)
{
	volatile unsigned long *adr = (unsigned long *)
			(addr | __IA64_UNCACHED_OFFSET);

	*adr = value;
	__ia64_mf_a();
}

#define READ_LOCAL_SYNERGY_REG(addr)	__synergy_in(addr)

extern inline unsigned long
__synergy_in(unsigned long addr)
{
	unsigned long ret, *adr = (unsigned long *)
			(addr | __IA64_UNCACHED_OFFSET);

	ret = *adr;
	__ia64_mf_a();
	return ret;
}

struct sn1_intr_action {
	void (*handler)(int, void *, struct pt_regs *);
	void *intr_arg;
	unsigned long flags;
	struct sn1_intr_action * next;
};

typedef struct synergy_da_s {
	hub_intmasks_t	s_intmasks;
}synergy_da_t;

struct sn1_cnode_action_list {
	spinlock_t action_list_lock;
	struct sn1_intr_action *action_list;
};
	

/* Temporary defintions for testing: */

#endif ASM_IA64_SN_SYNERGY_H
