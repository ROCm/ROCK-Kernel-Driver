/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_INTR_H
#define _ASM_SN_INTR_H

/* Number of interrupt levels associated with each interrupt register. */
#define N_INTPEND_BITS		64

#define INT_PEND0_BASELVL	0
#define INT_PEND1_BASELVL	64

#define	N_INTPENDJUNK_BITS	8
#define	INTPENDJUNK_CLRBIT	0x80

#include <linux/config.h>
#include <asm/sn/intr_public.h>

#if LANGUAGE_C

#if defined(CONFIG_IA64_SGI_IO)

#define II_NAMELEN	24

/*
 * Dispatch table entry - contains information needed to call an interrupt
 * routine.
 */
typedef struct intr_vector_s {
	intr_func_t	iv_func;	/* Interrupt handler function */
	intr_func_t	iv_prefunc;	/* Interrupt handler prologue func */
	void		*iv_arg;	/* Argument to pass to handler */
#ifdef IRIX
	thd_int_t		iv_tinfo;	/* Thread info */
#endif
	cpuid_t			iv_mustruncpu;	/* Where we must run. */
} intr_vector_t;

/* Interrupt information table. */
typedef struct intr_info_s {
	xtalk_intr_setfunc_t	ii_setfunc;	/* Function to set the interrupt
						 * destination and level register.
						 * It returns 0 (success) or an
						 * error code.
						 */
	void			*ii_cookie;	/* arg passed to setfunc */
	devfs_handle_t		ii_owner_dev;	/* device that owns this intr */
	char			ii_name[II_NAMELEN];	/* Name of this intr. */
	int			ii_flags;	/* informational flags */
} intr_info_t;

#define iv_tflags	iv_tinfo.thd_flags
#define iv_isync	iv_tinfo.thd_isync
#define iv_lat		iv_tinfo.thd_latstats
#define iv_thread	iv_tinfo.thd_ithread
#define iv_pri		iv_tinfo.thd_pri

#define THD_CREATED	0x00000001	/*
					 * We've created a thread for this
					 * interrupt.
					 */

/*
 * Bits for ii_flags:
 */
#define II_UNRESERVE	0
#define II_RESERVE	1		/* Interrupt reserved. 			*/
#define II_INUSE	2		/* Interrupt connected 			*/
#define II_ERRORINT	4		/* INterrupt is an error condition 	*/
#define II_THREADED	8		/* Interrupt handler is threaded.	*/

/*
 * Interrupt level wildcard
 */
#define INTRCONNECT_ANYBIT	-1

/*
 * This structure holds information needed both to call and to maintain
 * interrupts.  The two are in separate arrays for the locality benefits.
 * Since there's only one set of vectors per hub chip (but more than one
 * CPU, the lock to change the vector tables must be here rather than in
 * the PDA.
 */

typedef struct intr_vecblk_s {
	intr_vector_t	vectors[N_INTPEND_BITS];  /* information needed to
						     call an intr routine. */
	intr_info_t	info[N_INTPEND_BITS];	  /* information needed only
						     to maintain interrupts. */
	lock_t		vector_lock;		  /* Lock for this and the
						     masks in the PDA. */
	splfunc_t	vector_spl;		  /* vector_lock req'd spl */
	int		vector_state;		  /* Initialized to zero.
						     Set to INTR_INITED
						     by hubintr_init.
						   */
	int		vector_count;		  /* Number of vectors
						   * reserved.
						   */
	int		cpu_count[CPUS_PER_SUBNODE]; /* How many interrupts are
						   * connected to each CPU
						   */
	int		ithreads_enabled;	  /* Are interrupt threads
						   * initialized on this node.
						   * and block?
						   */
} intr_vecblk_t;

/* Possible values for vector_state: */
#define VECTOR_UNINITED	0
#define VECTOR_INITED	1
#define VECTOR_SET	2

#define hub_intrvect0	private.p_intmasks.dispatch0->vectors
#define hub_intrvect1	private.p_intmasks.dispatch1->vectors
#define hub_intrinfo0	private.p_intmasks.dispatch0->info
#define hub_intrinfo1	private.p_intmasks.dispatch1->info

#endif	/* CONFIG_IA64_SGI_IO */

/*
 * Macros to manipulate the interrupt register on the calling hub chip.
 */

#define LOCAL_HUB_SEND_INTR(_level)	LOCAL_HUB_S(PI_INT_PEND_MOD, \
						    (0x100|(_level)))
#if defined(CONFIG_IA64_SGI_IO)
#define REMOTE_HUB_PI_SEND_INTR(_hub, _sn, _level) \
		REMOTE_HUB_PI_S((_hub), _sn, PI_INT_PEND_MOD, (0x100|(_level)))

#define REMOTE_CPU_SEND_INTR(_cpuid, _level) 					\
		REMOTE_HUB_PI_S(cputonasid(_cpuid),				\
			SUBNODE(cputoslice(_cpuid)),				\
			PI_INT_PEND_MOD, (0x100|(_level)))
#endif	/* CONFIG_IA64_SGI_IO*/

/*
 * When clearing the interrupt, make sure this clear does make it 
 * to the hub. Otherwise we could end up losing interrupts.
 * We do an uncached load of the int_pend0 register to ensure this.
 */

#define LOCAL_HUB_CLR_INTR(_level)	  \
                LOCAL_HUB_S(PI_INT_PEND_MOD, (_level)),	\
                LOCAL_HUB_L(PI_INT_PEND0)
#define REMOTE_HUB_PI_CLR_INTR(_hub, _sn, _level) \
		REMOTE_HUB_PI_S((_hub), (_sn), PI_INT_PEND_MOD, (_level)),	\
                REMOTE_HUB_PI_L((_hub), (_sn), PI_INT_PEND0)

#if defined(CONFIG_IA64_SGI_IO)
/* Special support for use by gfx driver only.  Supports special gfx hub interrupt. */
extern void install_gfxintr(cpuid_t cpu, ilvl_t swlevel, intr_func_t intr_func, void *intr_arg);

void setrtvector(intr_func_t func);

/*
 * Interrupt blocking
 */
extern void intr_block_bit(cpuid_t cpu, int bit);
extern void intr_unblock_bit(cpuid_t cpu, int bit);
#endif	/* CONFIG_IA64_SGI_IO */

#endif /* LANGUAGE_C */

/*
 * Hard-coded interrupt levels:
 */

/*
 *	L0 = SW1
 *	L1 = SW2
 *	L2 = INT_PEND0
 *	L3 = INT_PEND1
 *	L4 = RTC
 *	L5 = Profiling Timer
 *	L6 = Hub Errors
 *	L7 = Count/Compare (T5 counters)
 */


/* INT_PEND0 hard-coded bits. */
#ifdef DEBUG_INTR_TSTAMP
/* hard coded interrupt level for interrupt latency test interrupt */
#define	CPU_INTRLAT_B	62
#define	CPU_INTRLAT_A	61
#endif

/* Hardcoded bits required by software. */
#define MSC_MESG_INTR	9
#define CPU_ACTION_B	8
#define CPU_ACTION_A	7

/* These are determined by hardware: */
#define CC_PEND_B	6
#define CC_PEND_A	5
#define UART_INTR	4
#define PG_MIG_INTR	3
#define GFX_INTR_B	2
#define GFX_INTR_A	1
#define RESERVED_INTR	0

/* INT_PEND1 hard-coded bits: */
#define MSC_PANIC_INTR	63
#define NI_ERROR_INTR	62
#define MD_COR_ERR_INTR	61
#define COR_ERR_INTR_B	60
#define COR_ERR_INTR_A	59
#define CLK_ERR_INTR	58

#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
# define NACK_INT_B	57
# define NACK_INT_A	56
# define LB_ERROR	55
# define XB_ERROR	54
#else
	<< BOMB! >>  Must define IP27 or IP35 or IP37
#endif

#define BRIDGE_ERROR_INTR 53	/* Setup by PROM to catch Bridge Errors */

#define IP27_INTR_0	52	/* Reserved for PROM use */
#define IP27_INTR_1	51	/*   (do not use in Kernel) */
#define IP27_INTR_2	50
#define IP27_INTR_3	49
#define IP27_INTR_4	48
#define IP27_INTR_5	47
#define IP27_INTR_6	46
#define IP27_INTR_7	45

#define	TLB_INTR_B	44	/* used for tlb flush random */
#define	TLB_INTR_A	43

#define LLP_PFAIL_INTR_B 42	/* see ml/SN/SN0/sysctlr.c */
#define LLP_PFAIL_INTR_A 41

#define NI_BRDCAST_ERR_B 40
#define NI_BRDCAST_ERR_A 39

#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
# define IO_ERROR_INTR	38	/* set up by prom */
# define DEBUG_INTR_B	37	/* used by symmon to stop all cpus */
# define DEBUG_INTR_A	36
#endif

#endif /* _ASM_SN_INTR_H */
