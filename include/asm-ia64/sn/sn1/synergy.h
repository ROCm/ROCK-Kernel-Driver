#ifndef _ASM_IA64_SN_SN1_SYNERGY_H
#define _ASM_IA64_SN_SN1_SYNERGY_H

#include <asm/io.h>
#include <asm/sn/hcl.h>
#include <asm/sn/addrs.h>
#include <asm/sn/intr_public.h>


/*
 * Definitions for the synergy asic driver
 * 
 * These are for SGI platforms only.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */


#define SYNERGY_L4_BYTES		(64UL*1024*1024)
#define SYNERGY_L4_WAYS			8
#define SYNERGY_L4_BYTES_PER_WAY	(SYNERGY_L4_BYTES/SYNERGY_L4_WAYS)
#define SYNERGY_BLOCK_SIZE		512UL


#define SSPEC_BASE	(0xe0000000000UL)
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

#define GBL_PERF_A_ADDR (0x330 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)
#define GBL_PERF_B_ADDR (0x338 + LB_REG_BASE + __IA64_UNCACHED_OFFSET)

#define WRITE_LOCAL_SYNERGY_REG(addr, value)	__synergy_out(addr, value)

#define HUB_L(_a)               *(_a)
#define HUB_S(_a, _d)           *(_a) = (_d)

#define HSPEC_SYNERGY0_0        0x04000000    /* Synergy0 Registers     */
#define HSPEC_SYNERGY1_0        0x05000000    /* Synergy1 Registers     */
#define HS_SYNERGY_STRIDE       (HSPEC_SYNERGY1_0 - HSPEC_SYNERGY0_0)
#define REMOTE_HSPEC(_n, _x)    (HUBREG_CAST (RREG_BASE(_n) + (_x)))

#define RREG_BASE(_n)           (NODE_LREG_BASE(_n))
#define NODE_LREG_BASE(_n)      (NODE_HSPEC_BASE(_n) + 0x30000000)
#define NODE_HSPEC_BASE(_n)     (HSPEC_BASE + NODE_OFFSET(_n))
#ifndef HSPEC_BASE
#define HSPEC_BASE              (SYN_UNCACHED_SPACE | HSPEC_BASE_SYN)
#endif
#define SYN_UNCACHED_SPACE      0xc000000000000000
#define HSPEC_BASE_SYN          0x00000b0000000000
#define NODE_OFFSET(_n)         (UINT64_CAST (_n) << NODE_SIZE_BITS)
#define NODE_SIZE_BITS		33

#define SYN_TAG_DISABLE_WAY	(SSPEC_BASE+0xae0)


#define RSYN_REG_OFFSET(fsb, reg) (((fsb) ? HSPEC_SYNERGY1_0 : HSPEC_SYNERGY0_0) | (reg))

#define REMOTE_SYNERGY_LOAD(nasid, fsb, reg)  __remote_synergy_in(nasid, fsb, reg)
#define REMOTE_SYNERGY_STORE(nasid, fsb, reg, val) __remote_synergy_out(nasid, fsb, reg, val)

static inline uint64_t
__remote_synergy_in(int nasid, int fsb, uint64_t reg) {
	volatile uint64_t *addr;

	addr = (uint64_t *)(RREG_BASE(nasid) + RSYN_REG_OFFSET(fsb, reg));
	return (*addr);
}

static inline void
__remote_synergy_out(int nasid, int fsb, uint64_t reg, uint64_t value) {
	volatile uint64_t *addr;

        addr = (uint64_t *)(RREG_BASE(nasid) + RSYN_REG_OFFSET(fsb, (reg<<2)));
        *(addr+0) = value >> 48;
        *(addr+1) = value >> 32;
        *(addr+2) = value >> 16;
        *(addr+3) = value;
        __ia64_mf_a();
}

/* XX this doesn't make a lot of sense. Which fsb?  */
static inline void
__synergy_out(unsigned long addr, unsigned long value)
{
	volatile unsigned long *adr = (unsigned long *)
			(addr | __IA64_UNCACHED_OFFSET);

	*adr = value;
	__ia64_mf_a();
}

#define READ_LOCAL_SYNERGY_REG(addr)	__synergy_in(addr)

/* XX this doesn't make a lot of sense. Which fsb? */
static inline unsigned long
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

/*
 * ioctl cmds for node/hub/synergy/[01]/mon for synergy
 * perf monitoring are defined in sndrv.h
 */

/* multiplex the counters every 10 timer interrupts */ 
#define SYNERGY_PERF_FREQ_DEFAULT 10

/* macros for synergy "mon" device ioctl handler */
#define SYNERGY_PERF_INFO(_s, _f)	(arbitrary_info_t)(((_s) << 16)|(_f))
#define SYNERGY_PERF_INFO_CNODE(_x)	(cnodeid_t)(((uint64_t)_x) >> 16)
#define SYNERGY_PERF_INFO_FSB(_x)	(((uint64_t)_x) & 1)

/* synergy perf control registers */
#define PERF_CNTL0_A            0xab0UL /* control A on FSB0 */
#define PERF_CNTL0_B            0xab8UL /* control B on FSB0 */
#define PERF_CNTL1_A            0xac0UL /* control A on FSB1 */
#define PERF_CNTL1_B            0xac8UL /* control B on FSB1 */

/* synergy perf counters */
#define PERF_CNTR0_A            0xad0UL /* counter A on FSB0 */
#define PERF_CNTR0_B            0xad8UL /* counter B on FSB0 */
#define PERF_CNTR1_A            0xaf0UL /* counter A on FSB1 */
#define PERF_CNTR1_B            0xaf8UL /* counter B on FSB1 */

/* Synergy perf data. Each nodepda keeps a list of these */
struct synergy_perf_s {
        uint64_t        intervals;      /* count of active intervals for this event */
        uint64_t        total_intervals;/* snapshot of total intervals */
        uint64_t        modesel;        /* mode and sel bits, both A and B registers */
        struct synergy_perf_s *next;	/* next in circular linked list */
        uint64_t        counts[2];      /* [0] is synergy-A counter, [1] synergy-B counter */
};

typedef struct synergy_perf_s synergy_perf_t;

typedef struct synergy_info_s synergy_info_t;

extern void synergy_perf_init(void);
extern void synergy_perf_update(int);
extern struct file_operations synergy_mon_fops;

#endif /* _ASM_IA64_SN_SN1_SYNERGY_H */
