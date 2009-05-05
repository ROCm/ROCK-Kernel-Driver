#define HYPERCALL_arg1 "ebx"
#define HYPERCALL_arg2 "ecx"
#define HYPERCALL_arg3 "edx"
#define HYPERCALL_arg4 "esi"
#define HYPERCALL_arg5 "edi"

#if CONFIG_XEN_COMPAT <= 0x030002
static inline int __must_check
HYPERVISOR_set_callbacks(
	unsigned long event_selector, unsigned long event_address,
	unsigned long failsafe_selector, unsigned long failsafe_address)
{
	return _hypercall4(int, set_callbacks,
			   event_selector, event_address,
			   failsafe_selector, failsafe_address);
}
#endif

static inline long __must_check
HYPERVISOR_set_timer_op(
	u64 timeout)
{
	return _hypercall2(long, set_timer_op,
			   (unsigned long)timeout,
			   (unsigned long)(timeout>>32));
}

static inline int __must_check
HYPERVISOR_update_descriptor(
	u64 ma, u64 desc)
{
	return _hypercall4(int, update_descriptor,
			   (unsigned long)ma, (unsigned long)(ma>>32),
			   (unsigned long)desc, (unsigned long)(desc>>32));
}

static inline int __must_check
HYPERVISOR_update_va_mapping(
	unsigned long va, pte_t new_val, unsigned long flags)
{
	unsigned long pte_hi = 0;

	if (arch_use_lazy_mmu_mode())
		return xen_multi_update_va_mapping(va, new_val, flags);
#ifdef CONFIG_X86_PAE
	pte_hi = new_val.pte_high;
#endif
	return _hypercall4(int, update_va_mapping, va,
			   new_val.pte_low, pte_hi, flags);
}

static inline int __must_check
HYPERVISOR_update_va_mapping_otherdomain(
	unsigned long va, pte_t new_val, unsigned long flags, domid_t domid)
{
	unsigned long pte_hi = 0;
#ifdef CONFIG_X86_PAE
	pte_hi = new_val.pte_high;
#endif
	return _hypercall5(int, update_va_mapping_otherdomain, va,
			   new_val.pte_low, pte_hi, flags, domid);
}
