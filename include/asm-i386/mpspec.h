#ifndef __ASM_MPSPEC_H
#define __ASM_MPSPEC_H

#include <asm/mpspec_def.h>
#include <mach_mpspec.h>

extern int mp_bus_id_to_type [MAX_MP_BUSSES];
extern int mp_bus_id_to_node [MAX_MP_BUSSES];
extern int mp_bus_id_to_local [MAX_MP_BUSSES];
extern int quad_local_to_mp_bus_id [NR_CPUS/4][4];
extern int mp_bus_id_to_pci_bus [MAX_MP_BUSSES];

extern unsigned int boot_cpu_physical_apicid;
extern unsigned long phys_cpu_present_map;
extern int smp_found_config;
extern void find_smp_config (void);
extern void get_smp_config (void);
extern int nr_ioapics;
extern int apic_version [MAX_APICS];
extern int mp_bus_id_to_type [MAX_MP_BUSSES];
extern int mp_irq_entries;
extern struct mpc_config_intsrc mp_irqs [MAX_IRQ_SOURCES];
extern int mpc_default_type;
extern int mp_bus_id_to_pci_bus [MAX_MP_BUSSES];
extern int mp_current_pci_id;
extern unsigned long mp_lapic_addr;
extern int pic_mode;
extern int using_apic_timer;

#ifdef CONFIG_X86_SUMMIT
extern void setup_summit (void);
#endif

#ifdef CONFIG_ACPI_BOOT
extern void mp_register_lapic (u8 id, u8 enabled);
extern void mp_register_lapic_address (u64 address);
extern void mp_register_ioapic (u8 id, u32 address, u32 irq_base);
extern void mp_override_legacy_irq (u8 bus_irq, u8 polarity, u8 trigger, u32 global_irq);
extern void mp_config_acpi_legacy_irqs (void);
extern void mp_config_ioapic_for_sci(int irq);
extern void mp_parse_prt (void);
#endif /*CONFIG_ACPI_BOOT*/

#endif

