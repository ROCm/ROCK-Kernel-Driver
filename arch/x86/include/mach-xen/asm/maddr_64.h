#ifndef _X86_64_MADDR_H
#define _X86_64_MADDR_H

static inline paddr_t pte_phys_to_machine(paddr_t phys)
{
	maddr_t machine;
	machine = pfn_to_mfn((phys & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT);
	machine = (machine << PAGE_SHIFT) | (phys & ~PHYSICAL_PAGE_MASK);
	return machine;
}

static inline paddr_t pte_machine_to_phys(maddr_t machine)
{
	paddr_t phys;
	phys = mfn_to_pfn((machine & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT);
	phys = (phys << PAGE_SHIFT) | (machine & ~PHYSICAL_PAGE_MASK);
	return phys;
}

#endif /* _X86_64_MADDR_H */

