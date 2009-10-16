/*
 * Copyright (C) 2009 Thomas Gleixner <tglx@linutronix.de>
 *
 *  For licencing details see kernel-base/COPYING
 */
#include <linux/bitmap.h>
#include <linux/init.h>
#include <linux/threads.h>

#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820.h>
#include <asm/time.h>
#include <asm/irq.h>

void __cpuinit x86_init_noop(void) { }
void __init x86_init_uint_noop(unsigned int unused) { }
void __init x86_init_pgd_noop(pgd_t *unused) { }

/*
 * The platform setup functions are preset with the default functions
 * for standard PC hardware.
 */
struct x86_init_ops x86_init __initdata = {

	.resources = {
		.probe_roms		= x86_init_noop,
		.reserve_resources	= reserve_standard_io_resources,
		.memory_setup		= default_machine_specific_memory_setup,
	},

	.mpparse = {
		.mpc_record		= x86_init_uint_noop,
		.setup_ioapic_ids	= x86_init_noop,
		.mpc_apic_id		= NULL,
		.smp_read_mpc_oem	= default_smp_read_mpc_oem,
		.mpc_oem_bus_info	= default_mpc_oem_bus_info,
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= NULL,
		.intr_init		= NULL,
		.trap_init		= x86_init_noop,
	},

	.oem = {
		.arch_setup		= xen_arch_setup,
		.banner			= x86_init_noop,
	},

	.paging = {
		.pagetable_setup_start	= x86_init_pgd_noop,
		.pagetable_setup_done	= x86_init_pgd_noop,
	},

	.timers = {
		.setup_percpu_clockev	= NULL,
		.tsc_pre_init		= x86_init_noop,
		.timer_init		= x86_init_noop,
	},
};

struct x86_platform_ops x86_platform = {
	.calibrate_tsc			= NULL,
	.get_wallclock			= mach_get_cmos_time,
	.set_wallclock			= mach_set_rtc_mmss,
};
