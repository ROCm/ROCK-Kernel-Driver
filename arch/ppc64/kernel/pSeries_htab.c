/*
 * pSeries hashtable management.
 *
 * SMP scalability work:
 *    Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/smp.h>

#include <asm/abs_addr.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/cputable.h>

#define HPTE_LOCK_BIT 3

static inline void pSeries_lock_hpte(HPTE *hptep)
{
	unsigned long *word = &hptep->dw0.dword0;

	while (1) {
		if (!test_and_set_bit(HPTE_LOCK_BIT, word))
			break;
		while(test_bit(HPTE_LOCK_BIT, word))
			cpu_relax();
	}
}

static inline void pSeries_unlock_hpte(HPTE *hptep)
{
	unsigned long *word = &hptep->dw0.dword0;

	asm volatile("lwsync":::"memory");
	clear_bit(HPTE_LOCK_BIT, word);
}

static spinlock_t pSeries_tlbie_lock = SPIN_LOCK_UNLOCKED;

long pSeries_hpte_insert(unsigned long hpte_group, unsigned long va,
			 unsigned long prpn, int secondary,
			 unsigned long hpteflags, int bolted, int large)
{
	unsigned long arpn = physRpn_to_absRpn(prpn);
	HPTE *hptep = htab_data.htab + hpte_group;
	Hpte_dword0 dw0;
	HPTE lhpte;
	int i;

	for (i = 0; i < HPTES_PER_GROUP; i++) {
		dw0 = hptep->dw0.dw0;

		if (!dw0.v) {
			/* retry with lock held */
			pSeries_lock_hpte(hptep);
			dw0 = hptep->dw0.dw0;
			if (!dw0.v)
				break;
			pSeries_unlock_hpte(hptep);
		}

		hptep++;
	}

	if (i == HPTES_PER_GROUP)
		return -1;

	lhpte.dw1.dword1      = 0;
	lhpte.dw1.dw1.rpn     = arpn;
	lhpte.dw1.flags.flags = hpteflags;

	lhpte.dw0.dword0      = 0;
	lhpte.dw0.dw0.avpn    = va >> 23;
	lhpte.dw0.dw0.h       = secondary;
	lhpte.dw0.dw0.bolted  = bolted;
	lhpte.dw0.dw0.v       = 1;

	if (large) {
		lhpte.dw0.dw0.l = 1;
		lhpte.dw0.dw0.avpn &= ~0x1UL;
	}

	hptep->dw1.dword1 = lhpte.dw1.dword1;

	/* Guarantee the second dword is visible before the valid bit */
	__asm__ __volatile__ ("eieio" : : : "memory");

	/*
	 * Now set the first dword including the valid bit
	 * NOTE: this also unlocks the hpte
	 */
	hptep->dw0.dword0 = lhpte.dw0.dword0;

	__asm__ __volatile__ ("ptesync" : : : "memory");

	return i;
}

static long pSeries_hpte_remove(unsigned long hpte_group)
{
	HPTE *hptep;
	Hpte_dword0 dw0;
	int i;
	int slot_offset;

	/* pick a random entry to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {
		hptep = htab_data.htab + hpte_group + slot_offset;
		dw0 = hptep->dw0.dw0;

		if (dw0.v && !dw0.bolted) {
			/* retry with lock held */
			pSeries_lock_hpte(hptep);
			dw0 = hptep->dw0.dw0;
			if (dw0.v && !dw0.bolted)
				break;
			pSeries_unlock_hpte(hptep);
		}

		slot_offset++;
		slot_offset &= 0x7;
	}

	if (i == HPTES_PER_GROUP)
		return -1;

	/* Invalidate the hpte. NOTE: this also unlocks it */
	hptep->dw0.dword0 = 0;

	return i;
}

static inline void set_pp_bit(unsigned long pp, HPTE *addr)
{
	unsigned long old;
	unsigned long *p = &addr->dw1.dword1;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3\n\
		rldimi	%0,%2,0,61\n\
		stdcx.	%0,0,%3\n\
		bne	1b"
	: "=&r" (old), "=m" (*p)
	: "r" (pp), "r" (p), "m" (*p)
	: "cc");
}

/*
 * Only works on small pages. Yes its ugly to have to check each slot in
 * the group but we only use this during bootup.
 */
static long pSeries_hpte_find(unsigned long vpn)
{
	HPTE *hptep;
	unsigned long hash;
	unsigned long i, j;
	long slot;
	Hpte_dword0 dw0;

	hash = hpt_hash(vpn, 0);

	for (j = 0; j < 2; j++) {
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		for (i = 0; i < HPTES_PER_GROUP; i++) {
			hptep = htab_data.htab + slot;
			dw0 = hptep->dw0.dw0;

			if ((dw0.avpn == (vpn >> 11)) && dw0.v &&
			    (dw0.h == j)) {
				/* HPTE matches */
				if (j)
					slot = -slot;
				return slot;
			}
			++slot;
		}
		hash = ~hash;
	}

	return -1;
}

static long pSeries_hpte_updatepp(unsigned long slot, unsigned long newpp,
				  unsigned long va, int large, int local)
{
	HPTE *hptep = htab_data.htab + slot;
	Hpte_dword0 dw0;
	unsigned long avpn = va >> 23;
	unsigned long flags;
	int ret = 0;

	if (large)
		avpn &= ~0x1UL;

	pSeries_lock_hpte(hptep);

	dw0 = hptep->dw0.dw0;

	/* Even if we miss, we need to invalidate the TLB */
	if ((dw0.avpn != avpn) || !dw0.v) {
		pSeries_unlock_hpte(hptep);
		ret = -1;
	} else {
		set_pp_bit(newpp, hptep);
		pSeries_unlock_hpte(hptep);
	}

	/* Ensure it is out of the tlb too */
	if ((cur_cpu_spec->cpu_features & CPU_FTR_TLBIEL) && !large && local) {
		_tlbiel(va);
	} else {
		spin_lock_irqsave(&pSeries_tlbie_lock, flags);
		_tlbie(va, large);
		spin_unlock_irqrestore(&pSeries_tlbie_lock, flags);
	}

	return ret;
}

/*
 * Update the page protection bits. Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT. Assumes pages being operated on will not be stolen.
 * Does not work on large pages.
 *
 * No need to lock here because we should be the only user.
 */
static void pSeries_hpte_updateboltedpp(unsigned long newpp, unsigned long ea)
{
	unsigned long vsid, va, vpn, flags;
	long slot;
	HPTE *hptep;

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;

	slot = pSeries_hpte_find(vpn);
	if (slot == -1)
		panic("could not find page to bolt\n");
	hptep = htab_data.htab + slot;

	set_pp_bit(newpp, hptep);

	/* Ensure it is out of the tlb too */
	spin_lock_irqsave(&pSeries_tlbie_lock, flags);
	_tlbie(va, 0);
	spin_unlock_irqrestore(&pSeries_tlbie_lock, flags);
}

static void pSeries_hpte_invalidate(unsigned long slot, unsigned long va,
				    int large, int local)
{
	HPTE *hptep = htab_data.htab + slot;
	Hpte_dword0 dw0;
	unsigned long avpn = va >> 23;
	unsigned long flags;

	if (large)
		avpn &= ~0x1UL;

	pSeries_lock_hpte(hptep);

	dw0 = hptep->dw0.dw0;

	/* Even if we miss, we need to invalidate the TLB */
	if ((dw0.avpn != avpn) || !dw0.v) {
		pSeries_unlock_hpte(hptep);
	} else {
		/* Invalidate the hpte. NOTE: this also unlocks it */
		hptep->dw0.dword0 = 0;
	}

	/* Invalidate the tlb */
	if ((cur_cpu_spec->cpu_features & CPU_FTR_TLBIEL) && !large && local) {
		_tlbiel(va);
	} else {
		spin_lock_irqsave(&pSeries_tlbie_lock, flags);
		_tlbie(va, large);
		spin_unlock_irqrestore(&pSeries_tlbie_lock, flags);
	}
}

static void pSeries_flush_hash_range(unsigned long context,
				     unsigned long number, int local)
{
	unsigned long vsid, vpn, va, hash, secondary, slot, flags, avpn;
	int i, j;
	HPTE *hptep;
	Hpte_dword0 dw0;
	struct ppc64_tlb_batch *batch = &ppc64_tlb_batch[smp_processor_id()];

	/* XXX fix for large ptes */
	unsigned long large = 0;

	j = 0;
	for (i = 0; i < number; i++) {
		if ((batch->addr[i] >= USER_START) &&
		    (batch->addr[i] <= USER_END))
			vsid = get_vsid(context, batch->addr[i]);
		else
			vsid = get_kernel_vsid(batch->addr[i]);

		va = (vsid << 28) | (batch->addr[i] & 0x0fffffff);
		batch->vaddr[j] = va;
		if (large)
			vpn = va >> LARGE_PAGE_SHIFT;
		else
			vpn = va >> PAGE_SHIFT;
		hash = hpt_hash(vpn, large);
		secondary = (pte_val(batch->pte[i]) & _PAGE_SECONDARY) >> 15;
		if (secondary)
			hash = ~hash;
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		slot += (pte_val(batch->pte[i]) & _PAGE_GROUP_IX) >> 12;

		hptep = htab_data.htab + slot;

		avpn = va >> 23;
		if (large)
			avpn &= ~0x1UL;

		pSeries_lock_hpte(hptep);

		dw0 = hptep->dw0.dw0;

		/* Even if we miss, we need to invalidate the TLB */
		if ((dw0.avpn != avpn) || !dw0.v) {
			pSeries_unlock_hpte(hptep);
		} else {
			/* Invalidate the hpte. NOTE: this also unlocks it */
			hptep->dw0.dword0 = 0;
		}

		j++;
	}

	if ((cur_cpu_spec->cpu_features & CPU_FTR_TLBIEL) && !large && local) {
		asm volatile("ptesync":::"memory");

		for (i = 0; i < j; i++) {
			asm volatile("\n\
			clrldi  %0,%0,16\n\
			tlbiel   %0"
			: : "r" (batch->vaddr[i]) : "memory" );
		}

		asm volatile("ptesync":::"memory");
	} else {
		/* XXX double check that it is safe to take this late */
		spin_lock_irqsave(&pSeries_tlbie_lock, flags);

		asm volatile("ptesync":::"memory");

		for (i = 0; i < j; i++) {
			asm volatile("\n\
			clrldi  %0,%0,16\n\
			tlbie   %0"
			: : "r" (batch->vaddr[i]) : "memory" );
		}

		asm volatile("eieio; tlbsync; ptesync":::"memory");

		spin_unlock_irqrestore(&pSeries_tlbie_lock, flags);
	}
}

void hpte_init_pSeries(void)
{
	struct device_node *root;
	const char *model;

	ppc_md.hpte_invalidate	= pSeries_hpte_invalidate;
	ppc_md.hpte_updatepp	= pSeries_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = pSeries_hpte_updateboltedpp;
	ppc_md.hpte_insert	= pSeries_hpte_insert;
	ppc_md.hpte_remove     	= pSeries_hpte_remove;

	/* Disable TLB batching on nighthawk */
	root = find_path_device("/");
	if (root) {
		model = get_property(root, "model", NULL);
		if (strcmp(model, "CHRP IBM,9076-N81"))
			ppc_md.flush_hash_range = pSeries_flush_hash_range;
	}
}
