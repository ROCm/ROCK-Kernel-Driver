/*
 * iSeries hashtable management.
 * 	Derived from pSeries_htab.c
 *
 * SMP scalability work:
 *    Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/abs_addr.h>

#if 0
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/smp.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/cputable.h>
#endif

static long iSeries_hpte_insert(unsigned long hpte_group, unsigned long va,
			 unsigned long prpn, int secondary,
			 unsigned long hpteflags, int bolted, int large)
{
	long slot;
	HPTE lhpte;

	/*
	 * The hypervisor tries both primary and secondary.
	 * If we are being called to insert in the secondary,
	 * it means we have already tried both primary and secondary,
	 * so we return failure immediately.
	 */
	if (secondary)
		return -1;

	slot = HvCallHpt_findValid(&lhpte, va >> PAGE_SHIFT);
	if (lhpte.dw0.dw0.v)
		panic("select_hpte_slot found entry already valid\n");

	if (slot == -1)	/* No available entry found in either group */
		return -1;

	if (slot < 0) {		/* MSB set means secondary group */
		secondary = 1;
		slot &= 0x7fffffffffffffff;
	}

	lhpte.dw1.dword1      = 0;
	lhpte.dw1.dw1.rpn     = physRpn_to_absRpn(prpn);
	lhpte.dw1.flags.flags = hpteflags;

	lhpte.dw0.dword0      = 0;
	lhpte.dw0.dw0.avpn    = va >> 23;
	lhpte.dw0.dw0.h       = secondary;
	lhpte.dw0.dw0.bolted  = bolted;
	lhpte.dw0.dw0.v       = 1;

	/* Now fill in the actual HPTE */
	HvCallHpt_addValidate(slot, secondary, &lhpte);

	return (secondary << 3) | (slot & 7);
}

static unsigned long iSeries_hpte_getword0(unsigned long slot)
{
	unsigned long dword0;
	HPTE hpte;

	HvCallHpt_get(&hpte, slot);
	dword0 = hpte.dw0.dword0;

	return dword0;
}

static long iSeries_hpte_remove(unsigned long hpte_group)
{
	unsigned long slot_offset;
	int i;
	HPTE lhpte;

	/* Pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {
		lhpte.dw0.dword0 = 
			iSeries_hpte_getword0(hpte_group + slot_offset);

		if (!lhpte.dw0.dw0.bolted) {
			HvCallHpt_invalidateSetSwBitsGet(hpte_group + 
							 slot_offset, 0, 0);
			return i;
		}

		slot_offset++;
		slot_offset &= 0x7;
	}

	return -1;
}

/*
 * The HyperVisor expects the "flags" argument in this form:
 * 	bits  0..59 : reserved
 * 	bit      60 : N
 * 	bits 61..63 : PP2,PP1,PP0
 */
static long iSeries_hpte_updatepp(unsigned long slot, unsigned long newpp,
				  unsigned long va, int large, int local)
{
	HPTE hpte;
	unsigned long avpn = va >> 23;

	HvCallHpt_get(&hpte, slot);
	if ((hpte.dw0.dw0.avpn == avpn) && (hpte.dw0.dw0.v)) {
		HvCallHpt_setPp(slot, (newpp & 0x3) | ((newpp & 0x4) << 1));
		return 0;
	}
	return -1;
}

/*
 * Functions used to find the PTE for a particular virtual address. 
 * Only used during boot when bolting pages.
 *
 * Input : vpn      : virtual page number
 * Output: PTE index within the page table of the entry
 *         -1 on failure
 */
static long iSeries_hpte_find(unsigned long vpn)
{
	HPTE hpte;
	long slot;

	/*
	 * The HvCallHpt_findValid interface is as follows:
	 * 0xffffffffffffffff : No entry found.
	 * 0x00000000xxxxxxxx : Entry found in primary group, slot x
	 * 0x80000000xxxxxxxx : Entry found in secondary group, slot x
	 */
	slot = HvCallHpt_findValid(&hpte, vpn); 
	if (hpte.dw0.dw0.v) {
		if (slot < 0) {
			slot &= 0x7fffffffffffffff;
			slot = -slot;
		}
	} else
		slot = -1;
	return slot;
}

/*
 * Update the page protection bits. Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT. Assumes pages being operated on will not be stolen.
 * Does not work on large pages.
 *
 * No need to lock here because we should be the only user.
 */
static void iSeries_hpte_updateboltedpp(unsigned long newpp, unsigned long ea)
{
	unsigned long vsid,va,vpn;
	long slot;

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;
	slot = iSeries_hpte_find(vpn); 
	if (slot == -1)
		panic("updateboltedpp: Could not find page to bolt\n");
	HvCallHpt_setPp(slot, newpp);
}

static void iSeries_hpte_invalidate(unsigned long slot, unsigned long va,
				    int large, int local)
{
	HPTE lhpte;
	unsigned long avpn = va >> 23;

	lhpte.dw0.dword0 = iSeries_hpte_getword0(slot);
	
	if ((lhpte.dw0.dw0.avpn == avpn) && lhpte.dw0.dw0.v)
		HvCallHpt_invalidateSetSwBitsGet(slot, 0, 0);
}

void hpte_init_iSeries(void)
{
	ppc_md.hpte_invalidate	= iSeries_hpte_invalidate;
	ppc_md.hpte_updatepp	= iSeries_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = iSeries_hpte_updateboltedpp;
	ppc_md.hpte_insert	= iSeries_hpte_insert;
	ppc_md.hpte_remove     	= iSeries_hpte_remove;
}
