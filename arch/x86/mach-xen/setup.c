/*
 *	Machine specific setup for generic
 */

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/acpi.h>
#include <asm/arch_hooks.h>
#include <asm/e820.h>
#include <asm/setup.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>

#include <xen/interface/callback.h>
#include <xen/interface/memory.h>

#ifdef CONFIG_X86_32

#ifdef CONFIG_HOTPLUG_CPU
#define DEFAULT_SEND_IPI	(1)
#else
#define DEFAULT_SEND_IPI	(0)
#endif

int no_broadcast=DEFAULT_SEND_IPI;

static __init int no_ipi_broadcast(char *str)
{
	get_option(&str, &no_broadcast);
	printk ("Using %s mode\n", no_broadcast ? "No IPI Broadcast" :
											"IPI Broadcast");
	return 1;
}

__setup("no_ipi_broadcast", no_ipi_broadcast);

static int __init print_ipi_mode(void)
{
	printk ("Using IPI %s mode\n", no_broadcast ? "No-Shortcut" :
											"Shortcut");
	return 0;
}

late_initcall(print_ipi_mode);

unsigned long *machine_to_phys_mapping = (void *)MACH2PHYS_VIRT_START;
EXPORT_SYMBOL(machine_to_phys_mapping);
unsigned int machine_to_phys_order;
EXPORT_SYMBOL(machine_to_phys_order);

void __init pre_setup_arch_hook(void)
{
	struct xen_machphys_mapping mapping;
	unsigned long machine_to_phys_nr_ents;
	struct xen_platform_parameters pp;

	init_mm.pgd = swapper_pg_dir = (pgd_t *)xen_start_info->pt_base;

	xen_setup_features();

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0) {
		hypervisor_virt_start = pp.virt_start;
		reserve_top_address(0UL - pp.virt_start);
	}

	if (HYPERVISOR_memory_op(XENMEM_machphys_mapping, &mapping) == 0) {
		machine_to_phys_mapping = (unsigned long *)mapping.v_start;
		machine_to_phys_nr_ents = mapping.max_mfn + 1;
	} else
		machine_to_phys_nr_ents = MACH2PHYS_NR_ENTRIES;
	machine_to_phys_order = fls(machine_to_phys_nr_ents - 1);

	if (!xen_feature(XENFEAT_auto_translated_physmap))
		phys_to_machine_mapping =
			(unsigned long *)xen_start_info->mfn_list;
}

#endif /* CONFIG_X86_32 */

extern void hypervisor_callback(void);
extern void failsafe_callback(void);
extern void nmi(void);

#ifdef CONFIG_X86_64
#include <asm/proto.h>
#define CALLBACK_ADDR(fn) ((unsigned long)(fn))
#else
#define CALLBACK_ADDR(fn) { __KERNEL_CS, (unsigned long)(fn) }
#endif

void __init machine_specific_arch_setup(void)
{
	int ret;
	static struct callback_register __initdata event = {
		.type = CALLBACKTYPE_event,
		.address = CALLBACK_ADDR(hypervisor_callback)
	};
	static struct callback_register __initdata failsafe = {
		.type = CALLBACKTYPE_failsafe,
		.address = CALLBACK_ADDR(failsafe_callback)
	};
#ifdef CONFIG_X86_64
	static struct callback_register __initdata syscall = {
		.type = CALLBACKTYPE_syscall,
		.address = CALLBACK_ADDR(system_call)
	};
#endif
	static struct callback_register __initdata nmi_cb = {
		.type = CALLBACKTYPE_nmi,
		.address = CALLBACK_ADDR(nmi)
	};

	ret = HYPERVISOR_callback_op(CALLBACKOP_register, &event);
	if (ret == 0)
		ret = HYPERVISOR_callback_op(CALLBACKOP_register, &failsafe);
#ifdef CONFIG_X86_64
	if (ret == 0)
		ret = HYPERVISOR_callback_op(CALLBACKOP_register, &syscall);
#endif
#if CONFIG_XEN_COMPAT <= 0x030002
#ifdef CONFIG_X86_32
	if (ret == -ENOSYS)
		ret = HYPERVISOR_set_callbacks(
			event.address.cs, event.address.eip,
			failsafe.address.cs, failsafe.address.eip);
#else
		ret = HYPERVISOR_set_callbacks(
			event.address,
			failsafe.address,
			syscall.address);
#endif
#endif
	BUG_ON(ret);

	ret = HYPERVISOR_callback_op(CALLBACKOP_register, &nmi_cb);
#if CONFIG_XEN_COMPAT <= 0x030002
	if (ret == -ENOSYS) {
		static struct xennmi_callback __initdata cb = {
			.handler_address = (unsigned long)nmi
		};

		HYPERVISOR_nmi_op(XENNMI_register_callback, &cb);
	}
#endif

#ifdef CONFIG_X86_32
	/* Do an early initialization of the fixmap area */
	{
		extern pte_t swapper_pg_fixmap[PTRS_PER_PTE];
		unsigned long addr = __fix_to_virt(FIX_EARLYCON_MEM_BASE);
		pud_t *pud = pud_offset(swapper_pg_dir + pgd_index(addr), addr);
		pmd_t *pmd = pmd_offset(pud, addr);
		unsigned int i;

		make_lowmem_page_readonly(swapper_pg_fixmap, XENFEAT_writable_page_tables);
		set_pmd(pmd, __pmd(__pa_symbol(swapper_pg_fixmap) | _PAGE_TABLE));

#define __FIXADDR_TOP (-PAGE_SIZE)
#define FIX_BUG_ON(fix) BUILD_BUG_ON(pmd_index(__fix_to_virt(FIX_##fix)) \
			!= pmd_index(__fix_to_virt(FIX_EARLYCON_MEM_BASE)))
		FIX_BUG_ON(SHARED_INFO);
		FIX_BUG_ON(ISAMAP_BEGIN);
		FIX_BUG_ON(ISAMAP_END);
#undef __FIXADDR_TOP
		BUG_ON(pte_index(hypervisor_virt_start));

		/* Switch to the real shared_info page, and clear the
		 * dummy page. */
		set_fixmap(FIX_SHARED_INFO, xen_start_info->shared_info);
		HYPERVISOR_shared_info = (shared_info_t *)fix_to_virt(FIX_SHARED_INFO);
		memset(empty_zero_page, 0, sizeof(empty_zero_page));

		/* Setup mapping of lower 1st MB */
		for (i = 0; i < NR_FIX_ISAMAPS; i++)
			if (is_initial_xendomain())
				set_fixmap(FIX_ISAMAP_BEGIN - i, i * PAGE_SIZE);
			else
				__set_fixmap(FIX_ISAMAP_BEGIN - i,
					     virt_to_machine(empty_zero_page),
					     PAGE_KERNEL_RO);
	}
#endif
}
