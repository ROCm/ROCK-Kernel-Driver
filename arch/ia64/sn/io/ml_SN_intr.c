/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Alan Mayer
 */

/*
 * intr.c-
 *	This file contains all of the routines necessary to set up and
 *	handle interrupts on an IP27 board.
 */

#ident  "$Revision: 1.167 $"

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/smp.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/nodemask.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/synergy.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/pci/pcibr_private.h>

#if DEBUG_INTR_TSTAMP_DEBUG
#include <sys/debug.h>
#include <sys/idbg.h>
#include <sys/inst.h>
void do_splx_log(int, int);
void spldebug_log_event(int);
#endif

// FIXME - BRINGUP
#ifdef CONFIG_SMP
extern unsigned long cpu_online_map;
#endif
#define cpu_allows_intr(cpu)	(1)
// If I understand what's going on with this, 32 should work.
// physmem_maxradius seems to be the maximum number of router
// hops to get from one end of the system to the other.  With
// a maximally configured machine, with the dumbest possible
// topology, we would make 32 router hops.  For what we're using
// it for, the dumbest possible should suffice.
#define physmem_maxradius()	32

#define SUBNODE_ANY -1

extern int	nmied;
extern int	hub_intr_wakeup_cnt;
extern synergy_da_t	*Synergy_da_indr[];
extern cpuid_t         master_procid;

extern cnodeid_t master_node_get(devfs_handle_t vhdl);


#define INTR_LOCK(vecblk) \
     (s = mutex_spinlock(&(vecblk)->vector_lock))
#define INTR_UNLOCK(vecblk) \
      mutex_spinunlock(&(vecblk)->vector_lock, s)

/*
 * REACT/Pro
 */



/* 
 * Find first bit set 
 * Used outside this file also 
 */
int ms1bit(unsigned long x)
{
    int			b;

    if (x >> 32)	b  = 32, x >>= 32;
    else		b  =  0;
    if (x >> 16)	b += 16, x >>= 16;
    if (x >>  8)	b +=  8, x >>=  8;
    if (x >>  4)	b +=  4, x >>=  4;
    if (x >>  2)	b +=  2, x >>=  2;

    return b + (int) (x >> 1);
}

/* ARGSUSED */
void
intr_stray(void *lvl)
{
    printk("Stray Interrupt - level %ld to cpu %d", (long)lvl, cpuid());
}

#if defined(DEBUG)

/* Infrastructure  to gather the device - target cpu mapping info */
#define MAX_DEVICES	1000	/* Reasonable large number . Need not be 
				 * the exact maximum # devices possible.
				 */
#define MAX_NAME	100	
typedef struct {
	dev_t		dev;	/* device */
	cpuid_t		cpuid;	/* target cpu */
	cnodeid_t	cnodeid;/* node on which the target cpu is present */
	int		bit;	/* intr bit reserved */
	char		intr_name[MAX_NAME]; /* name of the interrupt */
} intr_dev_targ_map_t;

intr_dev_targ_map_t 	intr_dev_targ_map[MAX_DEVICES];
uint64_t		intr_dev_targ_map_size;
lock_t			intr_dev_targ_map_lock;

/* Print out the device - target cpu mapping.
 * This routine is used only in the idbg command
 * "intrmap" 
 */
void
intr_dev_targ_map_print(cnodeid_t cnodeid)
{
	int  i,j,size = 0;
	int  print_flag = 0,verbose = 0;	
	char node_name[10];
	
	if (cnodeid != CNODEID_NONE) {
		nodepda_t 	*npda;

		npda = NODEPDA(cnodeid);
		for (j=0; j<NUM_SUBNODES; j++) {
			qprintf("\n SUBNODE %d\n INT_PEND0: ", j);
			for(i = 0 ; i < N_INTPEND_BITS ; i++)
				qprintf("%d",SNPDA(npda,j)->intr_dispatch0.info[i].ii_flags);
			qprintf("\n INT_PEND1: ");
			for(i = 0 ; i < N_INTPEND_BITS ; i++)
				qprintf("%d",SNPDA(npda,j)->intr_dispatch1.info[i].ii_flags);
		}
		verbose = 1;
	}
	qprintf("\n Device - Target Map [Interrupts: %s Node%s]\n\n",
		(verbose ? "All" : "Non-hardwired"),
		(cnodeid == CNODEID_NONE) ? "s: All" : node_name); 
		
	qprintf("Device\tCpu\tCnode\tIntr_bit\tIntr_name\n");
	for (i = 0 ; i < intr_dev_targ_map_size ; i++) {

		print_flag = 0;
		if (verbose) {
			if (cnodeid != CNODEID_NONE) {
				if (cnodeid == intr_dev_targ_map[i].cnodeid)
					print_flag = 1;
			} else {
				print_flag = 1;
			}
		} else {
			if (intr_dev_targ_map[i].dev != 0) {
				if (cnodeid != CNODEID_NONE) {
					if (cnodeid == 
					    intr_dev_targ_map[i].cnodeid)
						print_flag = 1;
				} else {
					print_flag = 1;
				}
			}
		}
		if (print_flag) {
			size++;
			qprintf("%d\t%d\t%d\t%d\t%s\n",
				intr_dev_targ_map[i].dev,
				intr_dev_targ_map[i].cpuid,
				intr_dev_targ_map[i].cnodeid,
				intr_dev_targ_map[i].bit,
				intr_dev_targ_map[i].intr_name);
		}

	}
	qprintf("\nTotal : %d\n",size);
}
#endif /* DEBUG */

/*
 * The spinlocks have already been initialized.  Now initialize the interrupt
 * vectors.  One processor on each hub does the work.
 */
void
intr_init_vecblk(nodepda_t *npda, cnodeid_t node, int sn)
{
    int			i, ip=0;
    intr_vecblk_t	*vecblk;
    subnode_pda_t	*snpda;


    snpda = SNPDA(npda,sn);
    do {
	if (ip == 0) {
	    vecblk = &snpda->intr_dispatch0;
	} else {
	    vecblk = &snpda->intr_dispatch1;
	}

	/* Initialize this vector. */
	for (i = 0; i < N_INTPEND_BITS; i++) {
		vecblk->vectors[i].iv_func = intr_stray;
		vecblk->vectors[i].iv_prefunc = NULL;
		vecblk->vectors[i].iv_arg = (void *)(__psint_t)(ip * N_INTPEND_BITS + i);

		vecblk->info[i].ii_owner_dev = 0;
		strcpy(vecblk->info[i].ii_name, "Unused");
		vecblk->info[i].ii_flags = 0;	/* No flags */
		vecblk->vectors[i].iv_mustruncpu = -1; /* No CPU yet. */

	    }

	spinlock_init(&vecblk->vector_lock, "ivecb");

	vecblk->vector_count = 0;    
	for (i = 0; i < CPUS_PER_SUBNODE; i++)
		vecblk->cpu_count[i] = 0;

	vecblk->vector_state = VECTOR_UNINITED;

    } while (++ip < 2);

}


/*
 * do_intr_reserve_level(cpuid_t cpu, int bit, int resflags, int reserve, 
 *					devfs_handle_t owner_dev, char *name)
 *	Internal work routine to reserve or unreserve an interrupt level.
 *		cpu is the CPU to which the interrupt will be sent.
 *		bit is the level bit to reserve.  -1 means any level
 *		resflags should include II_ERRORINT if this is an
 *			error interrupt, II_THREADED if the interrupt handler
 *			will be threaded, or 0 otherwise.
 *		reserve should be set to II_RESERVE or II_UNRESERVE
 *			to get or clear a reservation.
 *		owner_dev is the device that "owns" this interrupt, if supplied
 *		name is a human-readable name for this interrupt, if supplied
 *	intr_reserve_level returns the bit reserved or -1 to indicate an error
 */
static int
do_intr_reserve_level(cpuid_t cpu, int bit, int resflags, int reserve, 
					devfs_handle_t owner_dev, char *name)
{
    intr_vecblk_t	*vecblk;
    hub_intmasks_t 	*hub_intmasks;
    int s;
    int rv = 0;
    int ip;
    synergy_da_t	*sda;
    int		which_synergy;
    cnodeid_t	cnode;

    ASSERT(bit < N_INTPEND_BITS * 2);

    cnode = cpuid_to_cnodeid(cpu);
    which_synergy = cpuid_to_synergy(cpu);
    sda = Synergy_da_indr[(cnode * 2) + which_synergy];
    hub_intmasks = &sda->s_intmasks;
    // hub_intmasks = &pdaindr[cpu].pda->p_intmasks;

    // if (pdaindr[cpu].pda == NULL) return -1;
    if ((bit < N_INTPEND_BITS) && !(resflags & II_ERRORINT)) {
	vecblk = hub_intmasks->dispatch0;
	ip = 0;
    } else {
	ASSERT((bit >= N_INTPEND_BITS) || (bit == -1));
	bit -= N_INTPEND_BITS;	/* Get position relative to INT_PEND1 reg. */
	vecblk = hub_intmasks->dispatch1;
	ip = 1;
    }

    INTR_LOCK(vecblk);

    if (bit <= -1) {
	// bit = 0;
	bit = 7;  /* First available on SNIA */
	ASSERT(reserve == II_RESERVE);
	/* Choose any available level */
	for (; bit < N_INTPEND_BITS; bit++) {
	    if (!(vecblk->info[bit].ii_flags & II_RESERVE)) {
		rv = bit;
		break;
	    }
	}

	/* Return -1 if all interrupt levels int this register are taken. */
	if (bit == N_INTPEND_BITS)
	    rv = -1;

    } else {
	/* Reserve a particular level if it's available. */
	if ((vecblk->info[bit].ii_flags & II_RESERVE) == reserve) {
	    /* Can't (un)reserve a level that's already (un)reserved. */
	    rv = -1;
	} else {
	    rv = bit;
	}
    }

    /* Reserve the level and bump the count. */
    if (rv != -1) {
	if (reserve) {
	    int maxlen = sizeof(vecblk->info[bit].ii_name) - 1;
	    int namelen;
	    vecblk->info[bit].ii_flags |= (II_RESERVE | resflags);
	    vecblk->info[bit].ii_owner_dev = owner_dev;
	    /* Copy in the name. */
	    namelen = name ? strlen(name) : 0;
	    strncpy(vecblk->info[bit].ii_name, name, MIN(namelen, maxlen)); 
	    vecblk->info[bit].ii_name[maxlen] = '\0';
	    vecblk->vector_count++;
	} else {
	    vecblk->info[bit].ii_flags = 0;	/* Clear all the flags */
	    vecblk->info[bit].ii_owner_dev = 0;
	    /* Clear the name. */
	    vecblk->info[bit].ii_name[0] = '\0';
	    vecblk->vector_count--;
	}
    }

    INTR_UNLOCK(vecblk);

#if defined(DEBUG)
    if (rv >= 0) {
	    int namelen = name ? strlen(name) : 0;
	    /* Gather this device - target cpu mapping information
	     * in a table which can be used later by the idbg "intrmap"
	     * command
	     */
	    s = mutex_spinlock(&intr_dev_targ_map_lock);
	    if (intr_dev_targ_map_size < MAX_DEVICES) {
		    intr_dev_targ_map_t	*p;

		    p 		= &intr_dev_targ_map[intr_dev_targ_map_size];
		    p->dev  	= owner_dev;
		    p->cpuid 	= cpu; 
		    p->cnodeid 	= cputocnode(cpu); 
		    p->bit 	= ip * N_INTPEND_BITS + rv;
		    strncpy(p->intr_name,
			    name,
			    MIN(MAX_NAME,namelen));
		    intr_dev_targ_map_size++;
	    }
	    mutex_spinunlock(&intr_dev_targ_map_lock,s);
    }
#endif /* DEBUG */

    return (((rv == -1) ? rv : (ip * N_INTPEND_BITS) + rv)) ;
}


/*
 * WARNING:  This routine should only be called from within ml/SN.
 *	Reserve an interrupt level.
 */
int
intr_reserve_level(cpuid_t cpu, int bit, int resflags, devfs_handle_t owner_dev, char *name)
{
	return(do_intr_reserve_level(cpu, bit, resflags, II_RESERVE, owner_dev, name));
}


/*
 * WARNING:  This routine should only be called from within ml/SN.
 *	Unreserve an interrupt level.
 */
void
intr_unreserve_level(cpuid_t cpu, int bit)
{
	(void)do_intr_reserve_level(cpu, bit, 0, II_UNRESERVE, 0, NULL);
}

/*
 * Get values that vary depending on which CPU and bit we're operating on
 */
static hub_intmasks_t *
intr_get_ptrs(cpuid_t cpu, int bit,
	      int *new_bit,		/* Bit relative to the register */
	      hubreg_t **intpend_masks, /* Masks for this register */
	      intr_vecblk_t **vecblk,	/* Vecblock for this interrupt */
	      int *ip)			/* Which intpend register */
{
	hub_intmasks_t *hub_intmasks;
	synergy_da_t	*sda;
	int		which_synergy;
	cnodeid_t	cnode;

	ASSERT(bit < N_INTPEND_BITS * 2);

	cnode = cpuid_to_cnodeid(cpu);
	which_synergy = cpuid_to_synergy(cpu);
	sda = Synergy_da_indr[(cnode * 2) + which_synergy];
	hub_intmasks = &sda->s_intmasks;

	// hub_intmasks = &pdaindr[cpu].pda->p_intmasks;

	if (bit < N_INTPEND_BITS) {
		*intpend_masks = hub_intmasks->intpend0_masks;
		*vecblk = hub_intmasks->dispatch0;
		*ip = 0;
		*new_bit = bit;
	} else {
		*intpend_masks = hub_intmasks->intpend1_masks;
		*vecblk = hub_intmasks->dispatch1;
		*ip = 1;
		*new_bit = bit - N_INTPEND_BITS;
	}

	return hub_intmasks;
}


/*
 * intr_connect_level(cpuid_t cpu, int bit, ilvl_t intr_swlevel, 
 *		intr_func_t intr_func, void *intr_arg);
 *	This is the lowest-level interface to the interrupt code.  It shouldn't
 *	be called from outside the ml/SN directory.
 *	intr_connect_level hooks up an interrupt to a particular bit in
 *	the INT_PEND0/1 masks.  Returns 0 on success.
 *		cpu is the CPU to which the interrupt will be sent.
 *		bit is the level bit to connect to
 *		intr_swlevel tells which software level to use
 *		intr_func is the interrupt handler
 *		intr_arg is an arbitrary argument interpreted by the handler
 *		intr_prefunc is a prologue function, to be called
 *			with interrupts disabled, to disable
 *			the interrupt at source.  It is called
 *			with the same argument.  Should be NULL for
 *			typical interrupts, which can be masked
 *			by the infrastructure at the level bit.
 *	intr_connect_level returns 0 on success or nonzero on an error
 */
/* ARGSUSED */
int
intr_connect_level(cpuid_t cpu, int bit, ilvl_t intr_swlevel, 
		intr_func_t intr_func, void *intr_arg,
		intr_func_t intr_prefunc)
{
    intr_vecblk_t	*vecblk;
    hubreg_t		*intpend_masks;
    int s;
    int rv = 0;
    int ip;

    ASSERT(bit < N_INTPEND_BITS * 2);

    (void)intr_get_ptrs(cpu, bit, &bit, &intpend_masks,
				 &vecblk, &ip);

    INTR_LOCK(vecblk);

    if ((vecblk->info[bit].ii_flags & II_INUSE) ||
	(!(vecblk->info[bit].ii_flags & II_RESERVE))) {
	/* Can't assign to a level that's in use or isn't reserved. */
	rv = -1;
    } else {
	/* Stuff parameters into vector and info */
	vecblk->vectors[bit].iv_func = intr_func;
	vecblk->vectors[bit].iv_prefunc = intr_prefunc;
	vecblk->vectors[bit].iv_arg = intr_arg;
	vecblk->info[bit].ii_flags |= II_INUSE;
    }

    /* Now stuff the masks if everything's okay. */
    if (!rv) {
	int lslice;
	volatile hubreg_t *mask_reg;
	// nasid_t nasid = COMPACT_TO_NASID_NODEID(cputocnode(cpu));
	nasid_t nasid = cpuid_to_nasid(cpu);
	int	subnode = cpuid_to_subnode(cpu);

	/* Make sure it's not already pending when we connect it. */
	REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit + ip * N_INTPEND_BITS);

	intpend_masks[0] |= (1ULL << (uint64_t)bit);

	lslice = cputolocalslice(cpu);
	vecblk->cpu_count[lslice]++;
#if SN1
	/*
	 * On SN1, there are 8 interrupt mask registers per node:
	 * 	PI_0 MASK_0 A
	 * 	PI_0 MASK_1 A
	 * 	PI_0 MASK_0 B
	 * 	PI_0 MASK_1 B
	 * 	PI_1 MASK_0 A
	 * 	PI_1 MASK_1 A
	 * 	PI_1 MASK_0 B
	 * 	PI_1 MASK_1 B
	 */
#endif
	if (ip == 0) {
		mask_reg = REMOTE_HUB_PI_ADDR(nasid, subnode, 
		        PI_INT_MASK0_A + PI_INT_MASK_OFFSET * lslice);
	} else {
		mask_reg = REMOTE_HUB_PI_ADDR(nasid, subnode,
			PI_INT_MASK1_A + PI_INT_MASK_OFFSET * lslice);
	}

	HUB_S(mask_reg, intpend_masks[0]);
    }

    INTR_UNLOCK(vecblk);

    return rv;
}


/*
 * intr_disconnect_level(cpuid_t cpu, int bit)
 *
 *	This is the lowest-level interface to the interrupt code.  It should
 *	not be called from outside the ml/SN directory.
 *	intr_disconnect_level removes a particular bit from an interrupt in
 * 	the INT_PEND0/1 masks.  Returns 0 on success or nonzero on failure.
 */
int
intr_disconnect_level(cpuid_t cpu, int bit)
{
    intr_vecblk_t	*vecblk;
    hubreg_t		*intpend_masks;
    int s;
    int rv = 0;
    int ip;

    (void)intr_get_ptrs(cpu, bit, &bit, &intpend_masks,
				 &vecblk, &ip);

    INTR_LOCK(vecblk);

    if ((vecblk->info[bit].ii_flags & (II_RESERVE | II_INUSE)) !=
	((II_RESERVE | II_INUSE))) {
	/* Can't remove a level that's not in use or isn't reserved. */
	rv = -1;
    } else {
	/* Stuff parameters into vector and info */
	vecblk->vectors[bit].iv_func = (intr_func_t)NULL;
	vecblk->vectors[bit].iv_prefunc = (intr_func_t)NULL;
	vecblk->vectors[bit].iv_arg = 0;
	vecblk->info[bit].ii_flags &= ~II_INUSE;
#ifdef BASE_ITHRTEAD
	vecblk->vectors[bit].iv_mustruncpu = -1; /* No mustrun CPU any more. */
#endif
    }

    /* Now clear the masks if everything's okay. */
    if (!rv) {
	int lslice;
	volatile hubreg_t *mask_reg;

	intpend_masks[0] &= ~(1ULL << (uint64_t)bit);
	lslice = cputolocalslice(cpu);
	vecblk->cpu_count[lslice]--;
	mask_reg = REMOTE_HUB_PI_ADDR(COMPACT_TO_NASID_NODEID(cputocnode(cpu)), 
				   cpuid_to_subnode(cpu),
				   ip == 0 ? PI_INT_MASK0_A : PI_INT_MASK1_A);
	mask_reg = (volatile hubreg_t *)((__psunsigned_t)mask_reg +
					(PI_INT_MASK_OFFSET * lslice));
	*mask_reg = intpend_masks[0];
    }

    INTR_UNLOCK(vecblk);

    return rv;
}

/*
 * Actually block or unblock an interrupt
 */
void
do_intr_block_bit(cpuid_t cpu, int bit, int block)
{
	intr_vecblk_t *vecblk;
	int s;
	int ip;
	hubreg_t *intpend_masks;
	volatile hubreg_t mask_value;
	volatile hubreg_t *mask_reg;

	intr_get_ptrs(cpu, bit, &bit, &intpend_masks, &vecblk, &ip);

	INTR_LOCK(vecblk);

	if (block)
		/* Block */
		intpend_masks[0] &= ~(1ULL << (uint64_t)bit);
	else
		/* Unblock */
		intpend_masks[0] |= (1ULL << (uint64_t)bit);

	if (ip == 0) {
		mask_reg = REMOTE_HUB_PI_ADDR(COMPACT_TO_NASID_NODEID(cputocnode(cpu)), 
		        cpuid_to_subnode(cpu), PI_INT_MASK0_A);
	} else {
		mask_reg = REMOTE_HUB_PI_ADDR(COMPACT_TO_NASID_NODEID(cputocnode(cpu)), 
			cpuid_to_subnode(cpu), PI_INT_MASK1_A);
	}

	HUB_S(mask_reg, intpend_masks[0]);

	/*
	 * Wait for it to take effect.  (One read should suffice.)
	 * This is only necessary when blocking an interrupt
	 */
	if (block)
		while ((mask_value = HUB_L(mask_reg)) != intpend_masks[0])
			;

	INTR_UNLOCK(vecblk);
}


/*
 * Block a particular interrupt (cpu/bit pair).
 */
/* ARGSUSED */
void
intr_block_bit(cpuid_t cpu, int bit)
{
	do_intr_block_bit(cpu, bit, 1);
}


/*
 * Unblock a particular interrupt (cpu/bit pair).
 */
/* ARGSUSED */
void
intr_unblock_bit(cpuid_t cpu, int bit)
{
	do_intr_block_bit(cpu, bit, 0);
}


/* verifies that the specified CPUID is on the specified SUBNODE (if any) */
#define cpu_on_subnode(cpuid, which_subnode) \
	   (((which_subnode) == SUBNODE_ANY) || (cpuid_to_subnode(cpuid) == (which_subnode)))


/*
 * Choose one of the CPUs on a specified node or subnode to receive
 * interrupts. Don't pick a cpu which has been specified as a NOINTR cpu.
 *
 * Among all acceptable CPUs, the CPU that has the fewest total number
 * of interrupts targetted towards it is chosen.  Note that we never
 * consider how frequent each of these interrupts might occur, so a rare
 * hardware error interrupt is weighted equally with a disk interrupt.
 */
static cpuid_t
do_intr_cpu_choose(cnodeid_t cnode, int which_subnode)
{
	cpuid_t 	cpu, best_cpu = CPU_NONE;
	int		slice, min_count=1000;

	min_count = 1000;
	for (slice=0; slice < CPUS_PER_NODE; slice++) {
		intr_vecblk_t 	*vecblk0, *vecblk1;
		int total_intrs_to_slice;
		subnode_pda_t *snpda;
		int local_cpu_num;

		cpu = cnode_slice_to_cpuid(cnode, slice);
		cpu = cpu_logical_id(cpu);
		if (cpu == CPU_NONE)
			continue;

		/* If this cpu isn't enabled for interrupts, skip it */
		if (!cpu_enabled(cpu) || !cpu_allows_intr(cpu))
			continue;

		/* If this isn't the right subnode, skip it */
		if (!cpu_on_subnode(cpu, which_subnode))
			continue;

		/* OK, this one's a potential CPU for interrupts */
		snpda = SUBNODEPDA(cnode,SUBNODE(slice));
		vecblk0 = &snpda->intr_dispatch0;
		vecblk1 = &snpda->intr_dispatch1;
		local_cpu_num = LOCALCPU(slice);
		total_intrs_to_slice = vecblk0->cpu_count[local_cpu_num] +
		              vecblk1->cpu_count[local_cpu_num];

		if (min_count > total_intrs_to_slice) {
			min_count = total_intrs_to_slice;
			best_cpu = cpu;
		}
	}
	return best_cpu;
}

/*
 * Choose an appropriate interrupt target CPU on a specified node.
 * If which_subnode is SUBNODE_ANY, then subnode is not considered.
 * Otherwise, the chosen CPU must be on the specified subnode.
 */
static cpuid_t
intr_cpu_choose_from_node(cnodeid_t cnode, int which_subnode)
{
	return(do_intr_cpu_choose(cnode, which_subnode));
}


#ifndef CONFIG_IA64_SGI_IO
/*
 * Convert a subnode vertex into a (cnodeid, which_subnode) pair.
 * Return 0 on success, non-zero on failure.
 */
static int
subnodevertex_to_subnode(devfs_handle_t vhdl, cnodeid_t *cnodeidp, int *which_subnodep)
{
	arbitrary_info_t which_subnode;
	cnodeid_t cnodeid;

	/* Try to grab subnode information */
	if (hwgraph_info_get_LBL(vhdl, INFO_LBL_CPUBUS, &which_subnode) != GRAPH_SUCCESS)
		return(-1);

	/* On which node? */
	cnodeid = master_node_get(vhdl);
	if (cnodeid == CNODEID_NONE)
		return(-1);

	*which_subnodep = (int)which_subnode;
	*cnodeidp = cnodeid;
	return(0); /* success */
}

#endif /* CONFIG_IA64_SGI_IO */

/* Make it easy to identify subnode vertices in the hwgraph */
void
mark_subnodevertex_as_subnode(devfs_handle_t vhdl, int which_subnode)
{
	graph_error_t rv;

	ASSERT(0 <= which_subnode);
	ASSERT(which_subnode < NUM_SUBNODES);

	rv = hwgraph_info_add_LBL(vhdl, INFO_LBL_CPUBUS, (arbitrary_info_t)which_subnode);
	ASSERT_ALWAYS(rv == GRAPH_SUCCESS);

	rv = hwgraph_info_export_LBL(vhdl, INFO_LBL_CPUBUS, sizeof(arbitrary_info_t));
	ASSERT_ALWAYS(rv == GRAPH_SUCCESS);
}


#ifndef CONFIG_IA64_SGI_IO
/*
 * Given a device descriptor, extract interrupt target information and
 * choose an appropriate CPU.  Return CPU_NONE if we can't make sense
 * out of the target information.
 * TBD: Should this be considered platform-independent code?
 */
static cpuid_t
intr_target_from_desc(device_desc_t dev_desc, int favor_subnode)
{
	cpuid_t cpuid = CPU_NONE;
	cnodeid_t cnodeid;
	int which_subnode;
	devfs_handle_t intr_target_dev;

	if ((intr_target_dev = device_desc_intr_target_get(dev_desc)) != GRAPH_VERTEX_NONE) {
		/* 
		 * A valid device was specified.  If it's a particular
		 * CPU, then use that CPU as target.  
		 */
		cpuid = cpuvertex_to_cpuid(intr_target_dev);
		if (cpuid != CPU_NONE)
			goto cpuchosen;

		/* If a subnode vertex was specified, pick a CPU on that subnode. */
		if (subnodevertex_to_subnode(intr_target_dev, &cnodeid, &which_subnode) == 0) {
			cpuid = intr_cpu_choose_from_node(cnodeid, which_subnode);
			goto cpuchosen;
		}

		/*
		 * Otherwise, pick a CPU on the node that owns the 
		 * specified target.  Favor "favor_subnode", if specified.
		 */
		cnodeid = master_node_get(intr_target_dev);
		if (cnodeid != CNODEID_NONE) {
			cpuid = intr_cpu_choose_from_node(cnodeid, favor_subnode);
			goto cpuchosen;
		}
	}

cpuchosen:
	return(cpuid);
}
#endif /* CONFIG_IA64_SGI_IO */


#ifndef CONFIG_IA64_SGI_IO
/*
 * Check if we had already visited this candidate cnode
 */
static void *
intr_cnode_seen(cnodeid_t 	candidate,
		void 		*arg1,
		void 		*arg2)
{
	int		i;
	cnodeid_t	*visited_cnodes = (cnodeid_t *)arg1;
	int		*num_visited_cnodes = (int *)arg2;

	ASSERT(visited_cnodes);
	ASSERT(*num_visited_cnodes <= numnodes);
	for(i = 0 ; i < *num_visited_cnodes; i++) {
		if (candidate == visited_cnodes[i])
			return(NULL);
	}
	return(visited_cnodes);
}

#endif /* CONFIG_IA64_SGI_IO */



/*
 * intr_bit_reserve_test(cpuid,which_subnode,cnode,req_bit,intr_resflags,
 *		owner_dev,intr_name,*resp_bit)
 *	Either cpuid is not CPU_NONE or cnodeid not CNODE_NONE but
 * 	not both.
 * 1. 	If cpuid is specified, this routine tests if this cpu can be a valid
 * 	interrupt target candidate.
 * 2. 	If cnodeid is specified, this routine tests if there is a cpu on 
 *	this node which can be a valid interrupt target candidate.
 * 3.	If a valid interrupt target cpu candidate is found then an attempt at 
 * 	reserving an interrupt bit on the corresponding cnode is made.
 *
 * If steps 1 & 2 both fail or step 3 fails then we are not able to get a valid
 * interrupt target cpu then routine returns CPU_NONE (failure)
 * Otherwise routine returns cpuid of interrupt target (success)
 */
static cpuid_t
intr_bit_reserve_test(cpuid_t 		cpuid,
		      int		favor_subnode,
		      cnodeid_t 	cnodeid,
		      int		req_bit,
		      int 		intr_resflags,
		      devfs_handle_t 	owner_dev,
		      char		*intr_name,
		      int		*resp_bit)
{

	ASSERT((cpuid==CPU_NONE) || (cnodeid==CNODEID_NONE));

	if (cnodeid != CNODEID_NONE) {
		/* Try to choose a interrupt cpu candidate */
		cpuid = intr_cpu_choose_from_node(cnodeid, favor_subnode);
	}

	if (cpuid != CPU_NONE) {
		/* Try to reserve an interrupt bit on the hub 
		 * corresponding to the canidate cnode. If we
		 * are successful then we got a cpu which can
		 * act as an interrupt target for the io device.
		 * Otherwise we need to continue the search
		 * further.
		 */
		*resp_bit = do_intr_reserve_level(cpuid, 
						  req_bit,
						  intr_resflags,
						  II_RESERVE,
						  owner_dev, 
						  intr_name);

		if (*resp_bit >= 0)
			/* The interrupt target  specified was fine */
			return(cpuid);
	}
	return(CPU_NONE);
}
/*
 * intr_heuristic(dev_t dev,device_desc_t dev_desc,
 *		  int req_bit,int intr_resflags,dev_t owner_dev,
 *		  char *intr_name,int *resp_bit)
 *
 * Choose an interrupt destination for an interrupt.
 *	dev is the device for which the interrupt is being set up
 *	dev_desc is a description of hardware and policy that could
 *		help determine where this interrupt should go
 *	req_bit is the interrupt bit requested 
 *		(can be INTRCONNECT_ANY_BIT in which the first available
 * 		 interrupt bit is used)
 *	intr_resflags indicates whether we want to (un)reserve bit
 *	owner_dev is the owner device
 *	intr_name is the readable interrupt name	
 * 	resp_bit indicates whether we succeeded in getting the required
 *		 action  { (un)reservation} done	
 *		 negative value indicates failure
 *
 */
/* ARGSUSED */
cpuid_t
intr_heuristic(devfs_handle_t 		dev,
	       device_desc_t 	dev_desc,
	       int		req_bit,
	       int 		intr_resflags,
	       devfs_handle_t 		owner_dev,
	       char		*intr_name,
	       int		*resp_bit)
{
	cpuid_t		cpuid;				/* possible intr targ*/
	cnodeid_t 	candidate;			/* possible canidate */
#ifndef BRINGUP
	cnodeid_t	visited_cnodes[MAX_NASIDS], 	/* nodes seen so far */
		        center,				/* node we are on */
		        candidate;			/* possible canidate */
	int		num_visited_cnodes = 0;		/* # nodes seen */

	int		radius = 1,			/* start looking at the
							 * current node
							 */
		        maxradius = physmem_maxradius();
	void		*rv;
#endif /* BRINGUP */
	int		which_subnode = SUBNODE_ANY;

#if CONFIG_IA64_SGI_IO /* SN1 + pcibr Addressing Limitation */
	{
	devfs_handle_t pconn_vhdl;
	pcibr_soft_t pcibr_soft;

	/*
	 * This combination of SN1 and Bridge hardware has an odd "limitation".
	 * Due to the choice of addresses for PI0 and PI1 registers on SN1
	 * and historical limitations in Bridge, Bridge is unable to
	 * send interrupts to both PI0 CPUs and PI1 CPUs -- we have
	 * to choose one set or the other.  That choice is implicitly
	 * made when Bridge first attaches its error interrupt.  After
	 * that point, all subsequent interrupts are restricted to the
	 * same PI number (though it's possible to send interrupts to
	 * the same PI number on a different node).
	 *
	 * Since neither SN1 nor Bridge designers are willing to admit a
	 * bug, we can't really call this a "workaround".  It's a permanent
	 * solution for an SN1-specific and Bridge-specific hardware
	 * limitation that won't ever be lifted.
	 */
        if ((hwgraph_edge_get(dev, EDGE_LBL_PCI, &pconn_vhdl) == GRAPH_SUCCESS) &&
	   ((pcibr_soft = pcibr_soft_get(pconn_vhdl)) != NULL)) {
		/*
		 * We "know" that the error interrupt is the first
		 * interrupt set up by pcibr_attach.  Send all interrupts
		 * on this bridge to the same subnode number.
		 */
		if (pcibr_soft->bsi_err_intr) {
			which_subnode = cpuid_to_subnode(((hub_intr_t) pcibr_soft->bsi_err_intr)->i_cpuid);
		}
	}
	}
#endif /* CONFIG_IA64_SGI_IO */

#ifndef CONFIG_IA64_SGI_IO
	/* 
	 * If an interrupt target was specified for this
	 * interrupt allocation, try to use it.
	 */
	if (dev_desc) {

		/* Try to see if the interrupt target specified in the
		 * device descriptor is a legal candidate.
		 */
		cpuid = intr_bit_reserve_test(intr_target_from_desc(dev_desc, which_subnode),
					      which_subnode,
					      CNODEID_NONE,
					      req_bit,
					      intr_resflags,
					      owner_dev,
					      intr_name,
					      resp_bit);

		if (cpuid != CPU_NONE) {
			if (cpu_on_subnode(cpuid, which_subnode))
				return(cpuid);	/* got a valid interrupt target */

			printk("Override explicit interrupt targetting: %v (0x%x)\n",
				owner_dev, owner_dev);

			intr_unreserve_level(cpuid, *resp_bit);
		}

		/* Fall through on to the next step in the search for
		 * the interrupt candidate.
		 */

	}
#endif  /* CONFIG_IA64_SGI_IO */
	
	/* Check if we can find a valid interrupt target candidate on
	 * the master node for the device.
	 */
	cpuid = intr_bit_reserve_test(CPU_NONE,
				      which_subnode,	
				      master_node_get(dev),
				      req_bit,
				      intr_resflags,
				      owner_dev,
				      intr_name,
				      resp_bit);

	if (cpuid != CPU_NONE) {
		if (cpu_on_subnode(cpuid, which_subnode))
			return(cpuid);	/* got a valid interrupt target */
		else
			intr_unreserve_level(cpuid, *resp_bit);
	}

	printk("Cannot target interrupts to closest node(%d): %ld (0x%lx)\n",
		master_node_get(dev),(long) owner_dev, (unsigned long)owner_dev);

	/* Fall through into the default algorithm
	 * (exhaustive-search-for-the-nearest-possible-interrupt-target)
	 * for finding the interrupt target
	 */

#ifndef BRINGUP
	// Use of this algorithm is deferred until the supporting
	// code has been implemented.
	/* 
	 * No valid interrupt specification exists.
	 * Try to find a node which is closest to the current node
	 * which can process interrupts from a device
	 */

	center = cpuid_to_cnodeid(smp_processor_id());
	while (radius <= maxradius) {

		/* Try to find a node at the given radius and which
		 * we haven't seen already.
		 */
		rv = physmem_select_neighbor_node(center,radius,&candidate,
						  intr_cnode_seen,
						  (void *)visited_cnodes,
						  (void *)&num_visited_cnodes);
		if (!rv) {
			/* We have seen all the nodes at this particular radius
			 * Go on to the next radius level.
			 */
			radius++;
			continue;
		}			      
		/* We are seeing this candidate  cnode for the first time
		 */
		visited_cnodes[num_visited_cnodes++] = candidate;

		cpuid = intr_bit_reserve_test(CPU_NONE,
					      which_subnode,
					      candidate,
					      req_bit,
					      intr_resflags,
					      owner_dev,
					      intr_name,
					      resp_bit);

		if (cpuid != CPU_NONE) {
			if (cpu_on_subnode(cpuid, which_subnode))
				return(cpuid);	/* got a valid interrupt target */
			else
				intr_unreserve_level(cpuid, *resp_bit);
		}
	}
#else  /* BRINGUP */
	{
	// Do a stupid round-robin assignment of the node.
		static cnodeid_t last_node = 0;

		if (last_node > numnodes) last_node = 0;
		for (candidate = last_node; candidate <= numnodes; candidate++) {
			cpuid = intr_bit_reserve_test(CPU_NONE,
					      which_subnode,
					      candidate,
					      req_bit,
					      intr_resflags,
					      owner_dev,
					      intr_name,
					      resp_bit);

			if (cpuid != CPU_NONE) {
				if (cpu_on_subnode(cpuid, which_subnode)) {
					last_node++;
					return(cpuid);	/* got a valid interrupt target */
				}
				else
					intr_unreserve_level(cpuid, *resp_bit);
			}
			last_node++;
		}
	}
#endif

	printk("Cannot target interrupts to any close node: %ld (0x%lx)\n",
		(long)owner_dev, (unsigned long)owner_dev);

	/* In the worst case try to allocate interrupt bits on the
	 * master processor's node. We may get here during error interrupt
	 * allocation phase when the topology matrix is not yet setup
	 * and hence cannot do an exhaustive search.
	 */
	ASSERT(cpu_allows_intr(master_procid));
	cpuid = intr_bit_reserve_test(master_procid,
				      which_subnode,
				      CNODEID_NONE,
				      req_bit,
				      intr_resflags,
				      owner_dev,
				      intr_name,
				      resp_bit);

	if (cpuid != CPU_NONE) {
		if (cpu_on_subnode(cpuid, which_subnode))
			return(cpuid);
		else
			intr_unreserve_level(cpuid, *resp_bit);
	}

	printk("Cannot target interrupts: %ld (0x%lx)\n",
		(long)owner_dev, (unsigned long)owner_dev);

	return(CPU_NONE);	/* Should never get here */
}




#ifndef BRINGUP
/*
 * Should never receive an exception while running on the idle 
 * stack.  It IS possible to handle *interrupts* while on the
 * idle stack, but a non-interrupt *exception* is a problem.
 */
void
idle_err(inst_t *epc, uint cause, void *fep, void *sp)
{
	eframe_t *ep = (eframe_t *)fep;

    if ((cause & CAUSE_EXCMASK) == EXC_IBE ||
	(cause & CAUSE_EXCMASK) == EXC_DBE) {
	(void)dobuserre((eframe_t *)ep, epc, 0);
    }

    /* XXX - This will have to change to deal with various SN errors. */
    panic( "exception on IDLE stack "
	    "ep:0x%x epc:0x%x cause:0x%w32x sp:0x%x badvaddr:0x%x",
	    ep, epc, cause, sp, getbadvaddr());
    /* NOTREACHED */
}


/*
 * earlynofault - handle very early global faults - usually just while
 *      sizing memory
 * Returns: 1 if should do nofault
 *          0 if not
 */
/* ARGSUSED */
int
earlynofault(eframe_t *ep, uint code)
{
	switch(code) {
	case EXC_DBE:
		return(1);
	default:
		return(0);
	}
}



/* ARGSUSED */
static void
cpuintr(void *arg1, void *arg2)
{
#if RTE
	static int rte_intrdebug = 1;
#endif
	/*
	 * Frame Scheduler
	 */
	LOG_TSTAMP_EVENT(RTMON_INTR, TSTAMP_EV_CPUINTR, NULL, NULL,
			 NULL, NULL);

	/*
	 * Hardware clears the IO interrupts, but we need to clear software-
	 * generated interrupts.
	 */
	LOCAL_HUB_CLR_INTR(CPU_ACTION_A + cputolocalslice(cpuid()));

#if 0
	/* XXX - Handle error interrupts. */
	if (error_intr_reason)
		error_intr();
#endif /* 0 */

	/*
	 * If we're headed for panicspin and it is due to a NMI, save the
	 * eframe in the NMI area
	 */
	if (private.p_va_panicspin && nmied) {
		caddr_t	nmi_save_area;

		nmi_save_area = (caddr_t) (TO_UNCAC(TO_NODE(
			cputonasid(cpuid()), IP27_NMI_EFRAME_OFFSET)) + 
			cputoslice(cpuid()) * IP27_NMI_EFRAME_SIZE);
		bcopy((caddr_t) arg2, nmi_save_area, sizeof(eframe_t));
	}

	doacvec();
#if RTE
	if (private.p_flags & PDAF_ISOLATED && !rte_intrdebug)
		goto end_cpuintr;
#endif
	doactions();
#if RTE
end_cpuintr:
#endif
	LOG_TSTAMP_EVENT(RTMON_INTR, TSTAMP_EV_INTREXIT, TSTAMP_EV_CPUINTR, NULL, NULL, NULL);
}

void
install_cpuintr(cpuid_t cpu)
{
	int		intr_bit = CPU_ACTION_A + cputolocalslice(cpu);

	if (intr_connect_level(cpu, intr_bit, INTPEND0_MAXMASK,
				(intr_func_t) cpuintr, NULL, NULL))
		panic("install_cpuintr: Can't connect interrupt.");
}
#endif /* BRINGUP */

#ifdef DEBUG_INTR_TSTAMP
/* We allocate an array, but only use element number 64.  This guarantees that
 * the entry is in a cacheline by itself.
 */
#define DINTR_CNTIDX	32
#define DINTR_TSTAMP1	48
#define	DINTR_TSTAMP2	64
volatile long long dintr_tstamp_cnt[128];
int dintr_debug_output=0;
extern void idbg_tstamp_debug(void);
#ifdef SPLDEBUG
extern void idbg_splx_log(int);
#endif
#if DEBUG_INTR_TSTAMP_DEBUG
int dintr_enter_symmon=1000;	/* 1000 microseconds is 1 millisecond */
#endif

#ifndef BRINGUP
/* ARGSUSED */
static void
cpulatintr(void *arg)
{
	/*
	 * Hardware only clears IO interrupts so we have to clear our level
	 * here.
	 */
	LOCAL_HUB_CLR_INTR(CPU_INTRLAT_A + cputolocalslice(cpuid()));

#if DEBUG_INTR_TSTAMP_DEBUG
	dintr_tstamp_cnt[DINTR_TSTAMP2] =  GET_LOCAL_RTC;
	if ((dintr_tstamp_cnt[DINTR_TSTAMP2] - dintr_tstamp_cnt[DINTR_TSTAMP1])
	    > dintr_enter_symmon) {
#ifdef SPLDEBUG
		extern int spldebug_log_off;

		spldebug_log_off = 1;
#endif /* SPLDEBUG */
		debug("ring");
#ifdef SPLDEBUG
		spldebug_log_off = 0;
#endif /* SPLDEBUG */
	}
#endif
	dintr_tstamp_cnt[DINTR_CNTIDX]++;

	return;
}

static int install_cpulat_first=0;

void
install_cpulatintr(cpuid_t cpu)
{
	int		intr_bit;
	devfs_handle_t	cpuv = cpuid_to_vertex(cpu);

	intr_bit = CPU_INTRLAT_A + cputolocalslice(cpu);
	if (intr_bit != intr_reserve_level(cpu, intr_bit, II_THREADED,
					   cpuv, "intrlat"))
		panic( "install_cpulatintr: Can't reserve interrupt.");

	if (intr_connect_level(cpu, intr_bit, INTPEND0_MAXMASK,
				cpulatintr, NULL, NULL))
		panic( "install_cpulatintr: Can't connect interrupt.");

	if (!install_cpulat_first) {
		install_cpulat_first++;
		idbg_addfunc("tstamp_debug", (void (*)())idbg_tstamp_debug);
#if defined(SPLDEBUG) || defined(SPLDEBUG_CPU_EVENTS)
		idbg_addfunc("splx_log", (void (*)())idbg_splx_log);
#endif /* SPLDEBUG || SPLDEBUG_CPU_EVENTS */
	}
}
#endif /* BRINGUP */

#endif /* DEBUG_INTR_TSTAMP */

#ifndef BRINGUP
/* ARGSUSED */
static void
dbgintr(void *arg)
{
	/*
	 * Hardware only clears IO interrupts so we have to clear our level
	 * here.
	 */
	LOCAL_HUB_CLR_INTR(N_INTPEND_BITS + DEBUG_INTR_A + cputolocalslice(cpuid()));

	debug("zing");
	return;
}


void
install_dbgintr(cpuid_t cpu)
{
	int		intr_bit;
	devfs_handle_t	cpuv = cpuid_to_vertex(cpu);

	intr_bit = N_INTPEND_BITS + DEBUG_INTR_A + cputolocalslice(cpu);
	if (intr_bit != intr_reserve_level(cpu, intr_bit, 1, cpuv, "DEBUG"))
		panic("install_dbgintr: Can't reserve interrupt. "
			" intr_bit %d" ,intr_bit);

	if (intr_connect_level(cpu, intr_bit, INTPEND1_MAXMASK,
				dbgintr, NULL, NULL))
		panic("install_dbgintr: Can't connect interrupt.");

#ifdef DEBUG_INTR_TSTAMP
	/* Set up my interrupt latency test interrupt */
	install_cpulatintr(cpu);
#endif
}

/* ARGSUSED */
static void
tlbintr(void *arg)
{
	extern void tlbflush_rand(void);

	/*
	 * Hardware only clears IO interrupts so we have to clear our level
	 * here.
	 */
	LOCAL_HUB_CLR_INTR(N_INTPEND_BITS + TLB_INTR_A + cputolocalslice(cpuid()));

	tlbflush_rand();
	return;
}


void
install_tlbintr(cpuid_t cpu)
{
	int		intr_bit;
	devfs_handle_t	cpuv = cpuid_to_vertex(cpu);

	intr_bit = N_INTPEND_BITS + TLB_INTR_A + cputolocalslice(cpu);
	if (intr_bit != intr_reserve_level(cpu, intr_bit, 1, cpuv, "DEBUG"))
		panic("install_tlbintr: Can't reserve interrupt. "
			" intr_bit %d" ,intr_bit);

	if (intr_connect_level(cpu, intr_bit, INTPEND1_MAXMASK,
				tlbintr, NULL, NULL))
		panic("install_tlbintr: Can't connect interrupt.");

}


/*
 * Send an interrupt to all nodes.  Don't panic if we get an error.
 * Returns 1 if any exceptions occurred.
 */
int
protected_broadcast(hubreg_t intrbit)
{
	nodepda_t *npdap = private.p_nodepda;
	int byte, bit, sn;
	int error = 0;

	extern int _wbadaddr_val(volatile void *, int, volatile int *);

	/* Send rather than clear an interrupt. */
	intrbit |= 0x100;
	
	for (byte = 0; byte < NASID_MASK_BYTES; byte++) {
		for (bit = 0; bit < 8; bit++) {
			if (npdap->nasid_mask[byte] & (1 << bit)) {
				nasid_t nasid = byte * 8 + bit;
				for (sn=0; sn<NUM_SUBNODES; sn++) {
					error += _wbadaddr_val(REMOTE_HUB_PI_ADDR(nasid,
					      sn, PI_INT_PEND_MOD),
					      sizeof(hubreg_t),
					      (volatile int *)&intrbit);
				}
			}
		}
	}

	return error;
}


/*
 * Poll the interrupt register to see if another cpu has asked us
 * to drop into the debugger (without lowering spl).
 */
void
chkdebug(void)
{
	if (LOCAL_HUB_L(PI_INT_PEND1) & (1L << (DEBUG_INTR_A + cputolocalslice(cpuid()))))
		dbgintr((void *)NULL);
}


/*
 * Install special graphics interrupt.
 */
void
install_gfxintr(cpuid_t cpu, ilvl_t swlevel, intr_func_t intr_func, void *intr_arg)
{
	int intr_bit = GFX_INTR_A + cputolocalslice(cpu);

	if (intr_connect_level(cpu, intr_bit, swlevel,
				intr_func, intr_arg, NULL))
		panic("install_gfxintr: Can't connect interrupt.");
}


/*
 * Install page migration interrupt handler.
 */
void
hub_migrintr_init(cnodeid_t cnode)
{
	cpuid_t cpu = cnodetocpu(cnode);
	int intr_bit = INT_PEND0_BASELVL + PG_MIG_INTR;

	if (numnodes == 1){
		/* 
		 * No migration with just one node..
		 */
		return;
	}
	
	if (cpu != -1) {
		if (intr_connect_level(cpu, intr_bit, 0,
			       (intr_func_t) migr_intr_handler, 0, (intr_func_t) migr_intr_prologue_handler))
			panic( "hub_migrintr_init: Can't connect interrupt.");
	}
}


/*
 * Cause all CPUs to stop by sending them each a DEBUG interrupt.
 * Parameter is actually a (cpumask_t *).
 */
void
debug_stop_all_cpus(void *stoplist)
{
	int cpu;
	ulong level;

	for (cpu=0; cpu<maxcpus; cpu++) {
		if (cpu == cpuid())
			continue;
		if (!cpu_enabled(cpu))
		        continue;
		/* "-1" is the old style parameter OR could be the new style
		 * if no-one is currently stopped.  We only stop the
		 * requested cpus, the others are already stopped (probably
		 * at a breakpoint).
		 */

		if (((cpumask_t *)stoplist != (cpumask_t *)-1LL) &&
		    (!CPUMASK_TSTB(*(cpumask_t*)stoplist, cpu)))
			continue;

		/*
		 * CPU lslice A gets level DEBUG_INTR_A
		 * CPU lslice B gets level DEBUG_INTR_B
		 */
		level = DEBUG_INTR_A + LOCALCPU(get_cpu_slice(cpu));
		/*
		 * Convert the compact hub number to the NASID to get the
		 * correct part of the address space.  Then set the interrupt
		 * bit associated with the CPU we want to send the interrupt
		 * to.
		 */
		REMOTE_CPU_SEND_INTR(cpu, N_INTPEND_BITS + level);

	}
}


struct hardwired_intr_s {
	signed char level;
	int flags;
	char *name;
} const hardwired_intr[] = {
	{ INT_PEND0_BASELVL + RESERVED_INTR,	0,	"Reserved" },
	{ INT_PEND0_BASELVL + GFX_INTR_A,	0, 	"Gfx A" },
	{ INT_PEND0_BASELVL + GFX_INTR_B,	0, 	"Gfx B" },
	{ INT_PEND0_BASELVL + PG_MIG_INTR,	II_THREADED, "Migration" },
#if defined(SN1) && !defined(DIRECT_L1_CONSOLE)
	{ INT_PEND0_BASELVL + UART_INTR,	II_THREADED, "Bedrock/L1" },
#else
	{ INT_PEND0_BASELVL + UART_INTR,	0,	"Hub I2C" },
#endif
	{ INT_PEND0_BASELVL + CC_PEND_A,	0,	"Crosscall A" },
	{ INT_PEND0_BASELVL + CC_PEND_B,	0,	"Crosscall B" },
	{ INT_PEND0_BASELVL + MSC_MESG_INTR,	II_THREADED, "MSC Message" },
	{ INT_PEND0_BASELVL + CPU_ACTION_A,	0,	"CPU Action A" },
	{ INT_PEND0_BASELVL + CPU_ACTION_B,	0,	"CPU Action B" },
	{ INT_PEND1_BASELVL + IO_ERROR_INTR,	II_ERRORINT, "IO Error" },
	{ INT_PEND1_BASELVL + CLK_ERR_INTR,	II_ERRORINT, "Clock Error" },
	{ INT_PEND1_BASELVL + COR_ERR_INTR_A,	II_ERRORINT, "Correctable Error A" },
	{ INT_PEND1_BASELVL + COR_ERR_INTR_B,	II_ERRORINT, "Correctable Error B" },
	{ INT_PEND1_BASELVL + MD_COR_ERR_INTR,	II_ERRORINT, "MD Correct. Error" },
	{ INT_PEND1_BASELVL + NI_ERROR_INTR,	II_ERRORINT, "NI Error" },
	{ INT_PEND1_BASELVL + NI_BRDCAST_ERR_A,	II_ERRORINT, "Remote NI Error"},
	{ INT_PEND1_BASELVL + NI_BRDCAST_ERR_B,	II_ERRORINT, "Remote NI Error"},
	{ INT_PEND1_BASELVL + MSC_PANIC_INTR,	II_ERRORINT, "MSC Panic" },
	{ INT_PEND1_BASELVL + LLP_PFAIL_INTR_A,	II_ERRORINT, "LLP Pfail WAR" },
	{ INT_PEND1_BASELVL + LLP_PFAIL_INTR_B,	II_ERRORINT, "LLP Pfail WAR" },
#ifdef SN1
	{ INT_PEND1_BASELVL + NACK_INT_A,	0, "CPU A Nack count == NACK_CMP" },
	{ INT_PEND1_BASELVL + NACK_INT_B,	0, "CPU B Nack count == NACK_CMP" },
	{ INT_PEND1_BASELVL + LB_ERROR,		0, "Local Block Error" },
	{ INT_PEND1_BASELVL + XB_ERROR,		0, "Local XBar Error" },
#endif /* SN1 */	
	{ -1, 0, (char *)NULL}
};

/*
 * Reserve all of the hardwired interrupt levels so they're not used as
 * general purpose bits later.
 */
void
intr_reserve_hardwired(cnodeid_t cnode)
{
	cpuid_t cpu;
	int level;
	int i;
	char subnode_done[NUM_SUBNODES];

	cpu = cnodetocpu(cnode);
	if (cpu == CPU_NONE) {
		printk("Node %d has no CPUs", cnode);
		return;
	}

	for (i=0; i<NUM_SUBNODES; i++)
		subnode_done[i] = 0;

	for (; cpu<maxcpus && cpu_enabled(cpu) && cputocnode(cpu) == cnode; cpu++) {
		int which_subnode = cpuid_to_subnode(cpu);
		if (subnode_done[which_subnode])
			continue;
		subnode_done[which_subnode] = 1;

		for (i = 0; hardwired_intr[i].level != -1; i++) {
			level = hardwired_intr[i].level;

			if (level != intr_reserve_level(cpu, level,
						hardwired_intr[i].flags,
						(devfs_handle_t) NULL,
						hardwired_intr[i].name))
				panic("intr_reserve_hardwired: Can't reserve level %d.", level);
		}
	}
}

#endif /* BRINGUP */

/*
 * Check and clear interrupts.
 */
/*ARGSUSED*/
static void
intr_clear_bits(nasid_t nasid, volatile hubreg_t *pend, int base_level,
		char *name)
{
	volatile hubreg_t bits;
	int i;

	/* Check pending interrupts */
	if ((bits = HUB_L(pend)) != 0) {
		for (i = 0; i < N_INTPEND_BITS; i++) {
			if (bits & (1 << i)) {
#ifdef INTRDEBUG
				printk( "Nasid %d interrupt bit %d set in %s",
					nasid, i, name);
#endif
				LOCAL_HUB_CLR_INTR(base_level + i);
			}
		}
	}
}

/*
 * Clear out our interrupt registers.
 */
void
intr_clear_all(nasid_t nasid)
{
	int	sn;

	for(sn=0; sn<NUM_SUBNODES; sn++) {
		REMOTE_HUB_PI_S(nasid, sn, PI_INT_MASK0_A, 0);
		REMOTE_HUB_PI_S(nasid, sn, PI_INT_MASK0_B, 0);
		REMOTE_HUB_PI_S(nasid, sn, PI_INT_MASK1_A, 0);
		REMOTE_HUB_PI_S(nasid, sn, PI_INT_MASK1_B, 0);
	
		intr_clear_bits(nasid, REMOTE_HUB_PI_ADDR(nasid, sn, PI_INT_PEND0),
				INT_PEND0_BASELVL, "INT_PEND0");
		intr_clear_bits(nasid, REMOTE_HUB_PI_ADDR(nasid, sn, PI_INT_PEND1),
				INT_PEND1_BASELVL, "INT_PEND1");
	}
}

/* 
 * Dump information about a particular interrupt vector.
 */
static void
dump_vector(intr_info_t *info, intr_vector_t *vector, int bit, hubreg_t ip,
		hubreg_t ima, hubreg_t imb, void (*pf)(char *, ...))
{
	hubreg_t value = 1LL << bit;

	pf("  Bit %02d: %s: func 0x%x arg 0x%x prefunc 0x%x\n",
		bit, info->ii_name,
		vector->iv_func, vector->iv_arg, vector->iv_prefunc);
	pf("   vertex 0x%x %s%s",
		info->ii_owner_dev,
		((info->ii_flags) & II_RESERVE) ? "R" : "U",
		((info->ii_flags) & II_INUSE) ? "C" : "-");
	pf("%s%s%s%s",
		ip & value ? "P" : "-",
		ima & value ? "A" : "-",
		imb & value ? "B" : "-",
		((info->ii_flags) & II_ERRORINT) ? "E" : "-");
	pf("\n");
}


/*
 * Dump information about interrupt vector assignment.
 */
void
intr_dumpvec(cnodeid_t cnode, void (*pf)(char *, ...))
{
	nodepda_t *npda;
	int ip, sn, bit;
	intr_vecblk_t *dispatch;
	hubreg_t ipr, ima, imb;
	nasid_t nasid;

	if ((cnode < 0) || (cnode >= numnodes)) {
		pf("intr_dumpvec: cnodeid out of range: %d\n", cnode);
		return ;
	}

	nasid = COMPACT_TO_NASID_NODEID(cnode);

	if (nasid == INVALID_NASID) {
		pf("intr_dumpvec: Bad cnodeid: %d\n", cnode);
		return ;
	}
		

	npda = NODEPDA(cnode);

	for (sn = 0; sn < NUM_SUBNODES; sn++) {
		for (ip = 0; ip < 2; ip++) {
			dispatch = ip ? &(SNPDA(npda,sn)->intr_dispatch1) : &(SNPDA(npda,sn)->intr_dispatch0);
			ipr = REMOTE_HUB_PI_L(nasid, sn, ip ? PI_INT_PEND1 : PI_INT_PEND0);
			ima = REMOTE_HUB_PI_L(nasid, sn, ip ? PI_INT_MASK1_A : PI_INT_MASK0_A);
			imb = REMOTE_HUB_PI_L(nasid, sn, ip ? PI_INT_MASK1_B : PI_INT_MASK0_B);
	
			pf("Node %d INT_PEND%d:\n", cnode, ip);
	
			if (dispatch->ithreads_enabled)
				pf(" Ithreads enabled\n");
			else
				pf(" Ithreads disabled\n");
			pf(" vector_count = %d, vector_state = %d\n",
				dispatch->vector_count,
				dispatch->vector_state);
			pf(" CPU A count %d, CPU B count %d\n",
 		   	dispatch->cpu_count[0],
 		   	dispatch->cpu_count[1]);
			pf(" &vector_lock = 0x%x\n",
				&(dispatch->vector_lock));
			for (bit = 0; bit < N_INTPEND_BITS; bit++) {
				if ((dispatch->info[bit].ii_flags & II_RESERVE) ||
			    	(ipr & (1L << bit))) {
					dump_vector(&(dispatch->info[bit]),
					    	&(dispatch->vectors[bit]),
					    	bit, ipr, ima, imb, pf);
				}
			}
			pf("\n");
		}
	}
}

