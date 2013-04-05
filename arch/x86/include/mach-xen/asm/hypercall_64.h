#define HYPERCALL_arg1 "rdi"
#define HYPERCALL_arg2 "rsi"
#define HYPERCALL_arg3 "rdx"
#define HYPERCALL_arg4 "r10"
#define HYPERCALL_arg5 "r8"

#if CONFIG_XEN_COMPAT <= 0x030002
static inline int __must_check
HYPERVISOR_set_callbacks(
	unsigned long event_address, unsigned long failsafe_address, 
	unsigned long syscall_address)
{
	return _hypercall3(int, set_callbacks,
			   event_address, failsafe_address, syscall_address);
}
#endif

static inline long __must_check
HYPERVISOR_set_timer_op(
	u64 timeout)
{
	return _hypercall1(long, set_timer_op, timeout);
}

static inline int __must_check
HYPERVISOR_update_descriptor(
	unsigned long ma, unsigned long word)
{
	return _hypercall2(int, update_descriptor, ma, word);
}

static inline int __must_check
HYPERVISOR_update_va_mapping(
	unsigned long va, pte_t new_val, unsigned long flags)
{
	if (arch_use_lazy_mmu_mode())
		return xen_multi_update_va_mapping(va, new_val, flags);
	return _hypercall3(int, update_va_mapping, va, new_val.pte, flags);
}

static inline int __must_check
HYPERVISOR_update_va_mapping_otherdomain(
	unsigned long va, pte_t new_val, unsigned long flags, domid_t domid)
{
	return _hypercall4(int, update_va_mapping_otherdomain, va,
			   new_val.pte, flags, domid);
}

static inline int __must_check
HYPERVISOR_set_segment_base(
	int reg, unsigned long value)
{
	return _hypercall2(int, set_segment_base, reg, value);
}
