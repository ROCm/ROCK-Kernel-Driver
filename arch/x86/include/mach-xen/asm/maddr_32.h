#ifndef _I386_MADDR_H
#define _I386_MADDR_H

#ifdef CONFIG_X86_PAE
static inline paddr_t pte_phys_to_machine(paddr_t phys)
{
	/*
	 * In PAE mode, the NX bit needs to be dealt with in the value
	 * passed to pfn_to_mfn(). On x86_64, we need to mask it off,
	 * but for i386 the conversion to ulong for the argument will
	 * clip it off.
	 */
	maddr_t machine = pfn_to_mfn(phys >> PAGE_SHIFT);
	machine = (machine << PAGE_SHIFT) | (phys & ~PHYSICAL_PAGE_MASK);
	return machine;
}

static inline paddr_t pte_machine_to_phys(maddr_t machine)
{
	/*
	 * In PAE mode, the NX bit needs to be dealt with in the value
	 * passed to mfn_to_pfn(). On x86_64, we need to mask it off,
	 * but for i386 the conversion to ulong for the argument will
	 * clip it off.
	 */
	paddr_t phys = mfn_to_pfn(machine >> PAGE_SHIFT);
	phys = (phys << PAGE_SHIFT) | (machine & ~PHYSICAL_PAGE_MASK);
	return phys;
}
#else
#define pte_phys_to_machine phys_to_machine
#define pte_machine_to_phys machine_to_phys
#endif

#endif /* _I386_MADDR_H */
