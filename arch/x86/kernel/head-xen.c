#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#ifndef CONFIG_XEN
#include <asm/bios_ebda.h>

#define BIOS_LOWMEM_KILOBYTES 0x413

/*
 * The BIOS places the EBDA/XBDA at the top of conventional
 * memory, and usually decreases the reported amount of
 * conventional memory (int 0x12) too. This also contains a
 * workaround for Dell systems that neglect to reserve EBDA.
 * The same workaround also avoids a problem with the AMD768MPX
 * chipset: reserve a page before VGA to prevent PCI prefetch
 * into it (errata #56). Usually the page is reserved anyways,
 * unless you have no PS/2 mouse plugged in.
 */
void __init reserve_ebda_region(void)
{
	unsigned int lowmem, ebda_addr;

	/* To determine the position of the EBDA and the */
	/* end of conventional memory, we need to look at */
	/* the BIOS data area. In a paravirtual environment */
	/* that area is absent. We'll just have to assume */
	/* that the paravirt case can handle memory setup */
	/* correctly, without our help. */
	if (paravirt_enabled())
		return;

	/* end of low (conventional) memory */
	lowmem = *(unsigned short *)__va(BIOS_LOWMEM_KILOBYTES);
	lowmem <<= 10;

	/* start of EBDA area */
	ebda_addr = get_bios_ebda();

	/* Fixup: bios puts an EBDA in the top 64K segment */
	/* of conventional memory, but does not adjust lowmem. */
	if ((lowmem - ebda_addr) <= 0x10000)
		lowmem = ebda_addr;

	/* Fixup: bios does not report an EBDA at all. */
	/* Some old Dells seem to need 4k anyhow (bugzilla 2990) */
	if ((ebda_addr == 0) && (lowmem >= 0x9f000))
		lowmem = 0x9f000;

	/* Paranoia: should never happen, but... */
	if ((lowmem == 0) || (lowmem >= 0x100000))
		lowmem = 0x9f000;

	/* reserve all memory between lowmem and the 1MB mark */
	reserve_early_overlap_ok(lowmem, 0x100000, "BIOS reserved");
}
#else /* CONFIG_XEN */
#include <linux/module.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <xen/interface/callback.h>
#include <xen/interface/memory.h>

extern void hypervisor_callback(void);
extern void failsafe_callback(void);
extern void nmi(void);

#ifdef CONFIG_X86_64
#include <asm/proto.h>
#define CALLBACK_ADDR(fn) ((unsigned long)(fn))
#else
#define CALLBACK_ADDR(fn) { __KERNEL_CS, (unsigned long)(fn) }
#endif

unsigned long *machine_to_phys_mapping = (void *)MACH2PHYS_VIRT_START;
EXPORT_SYMBOL(machine_to_phys_mapping);
unsigned int machine_to_phys_order;
EXPORT_SYMBOL(machine_to_phys_order);

void __init xen_start_kernel(void)
{
	unsigned int i;
	struct xen_machphys_mapping mapping;
	unsigned long machine_to_phys_nr_ents;
#ifdef CONFIG_X86_32
	struct xen_platform_parameters pp;
	extern pte_t swapper_pg_fixmap[PTRS_PER_PTE];
	unsigned long addr;
#endif

	xen_setup_features();

	if (HYPERVISOR_memory_op(XENMEM_machphys_mapping, &mapping) == 0) {
		machine_to_phys_mapping = (unsigned long *)mapping.v_start;
		machine_to_phys_nr_ents = mapping.max_mfn + 1;
	} else
		machine_to_phys_nr_ents = MACH2PHYS_NR_ENTRIES;
	while ((1UL << machine_to_phys_order) < machine_to_phys_nr_ents )
		machine_to_phys_order++;

	if (!xen_feature(XENFEAT_auto_translated_physmap))
		phys_to_machine_mapping =
			(unsigned long *)xen_start_info->mfn_list;

	WARN_ON(HYPERVISOR_vm_assist(VMASST_CMD_enable,
				     VMASST_TYPE_writable_pagetables));

	reserve_early(ALIGN(__pa_symbol(&_end), PAGE_SIZE),
		      __pa(xen_start_info->pt_base)
		      + (xen_start_info->nr_pt_frames << PAGE_SHIFT),
		      "Xen provided");

#ifdef CONFIG_X86_32
	WARN_ON(HYPERVISOR_vm_assist(VMASST_CMD_enable,
				     VMASST_TYPE_4gb_segments));

	init_mm.pgd = swapper_pg_dir = (pgd_t *)xen_start_info->pt_base;

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0) {
		hypervisor_virt_start = pp.virt_start;
		reserve_top_address(0UL - pp.virt_start);
	}

	BUG_ON(pte_index(hypervisor_virt_start));

	/* Do an early initialization of the fixmap area */
	make_lowmem_page_readonly(swapper_pg_fixmap, XENFEAT_writable_page_tables);
	addr = __fix_to_virt(FIX_EARLYCON_MEM_BASE);
	set_pmd(pmd_offset(pud_offset(swapper_pg_dir + pgd_index(addr),
				      addr),
			   addr),
		__pmd(__pa_symbol(swapper_pg_fixmap) | _PAGE_TABLE));
#else
	check_efer();
	xen_init_pt();
#endif

#define __FIXADDR_TOP (-PAGE_SIZE)
#define pmd_index(addr) (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
#define FIX_BUG_ON(fix) BUILD_BUG_ON(pmd_index(__fix_to_virt(FIX_##fix)) \
			!= pmd_index(__fix_to_virt(FIX_EARLYCON_MEM_BASE)))
	FIX_BUG_ON(SHARED_INFO);
	FIX_BUG_ON(ISAMAP_BEGIN);
	FIX_BUG_ON(ISAMAP_END);
#undef pmd_index
#undef __FIXADDR_TOP

	/* Switch to the real shared_info page, and clear the dummy page. */
	set_fixmap(FIX_SHARED_INFO, xen_start_info->shared_info);
	HYPERVISOR_shared_info = (shared_info_t *)fix_to_virt(FIX_SHARED_INFO);
	memset(empty_zero_page, 0, sizeof(empty_zero_page));

	setup_vcpu_info(0);

	/* Set up mapping of lowest 1MB of physical memory. */
	for (i = 0; i < NR_FIX_ISAMAPS; i++)
		if (is_initial_xendomain())
			set_fixmap(FIX_ISAMAP_BEGIN - i, i * PAGE_SIZE);
		else
			__set_fixmap(FIX_ISAMAP_BEGIN - i,
				     virt_to_machine(empty_zero_page),
				     PAGE_KERNEL_RO);

}

void __init xen_arch_setup(void)
{
	int ret;
	static const struct callback_register __initconst event = {
		.type = CALLBACKTYPE_event,
		.address = CALLBACK_ADDR(hypervisor_callback)
	};
	static const struct callback_register __initconst failsafe = {
		.type = CALLBACKTYPE_failsafe,
		.address = CALLBACK_ADDR(failsafe_callback)
	};
#ifdef CONFIG_X86_64
	static const struct callback_register __initconst syscall = {
		.type = CALLBACKTYPE_syscall,
		.address = CALLBACK_ADDR(system_call)
	};
#endif
	static const struct callback_register __initconst nmi_cb = {
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
}
#endif /* CONFIG_XEN */
