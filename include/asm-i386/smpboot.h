#ifndef __ASM_SMPBOOT_H
#define __ASM_SMPBOOT_H

#ifndef clustered_apic_mode
 #ifdef CONFIG_CLUSTERED_APIC
  #define clustered_apic_mode (1)
 #else /* !CONFIG_CLUSTERED_APIC */
  #define clustered_apic_mode (0)
 #endif /* CONFIG_CLUSTERED_APIC */
#endif 
 
#ifdef CONFIG_CLUSTERED_APIC
 #define TRAMPOLINE_LOW phys_to_virt(0x8)
 #define TRAMPOLINE_HIGH phys_to_virt(0xa)
#else /* !CONFIG_CLUSTERED_APIC */
 #define TRAMPOLINE_LOW phys_to_virt(0x467)
 #define TRAMPOLINE_HIGH phys_to_virt(0x469)
#endif /* CONFIG_CLUSTERED_APIC */

#ifdef CONFIG_CLUSTERED_APIC
 #define boot_cpu_apicid boot_cpu_logical_apicid
#else /* !CONFIG_CLUSTERED_APIC */
 #define boot_cpu_apicid boot_cpu_physical_apicid
#endif /* CONFIG_CLUSTERED_APIC */

/*
 * How to map from the cpu_present_map
 */
#ifdef CONFIG_CLUSTERED_APIC
 #define cpu_present_to_apicid(mps_cpu) ( ((mps_cpu/4)*16) + (1<<(mps_cpu%4)) )
#else /* !CONFIG_CLUSTERED_APIC */
 #define cpu_present_to_apicid(apicid) (apicid)
#endif /* CONFIG_CLUSTERED_APIC */

/*
 * Mappings between logical cpu number and logical / physical apicid
 * The first four macros are trivial, but it keeps the abstraction consistent
 */
extern volatile int logical_apicid_2_cpu[];
extern volatile int cpu_2_logical_apicid[];
extern volatile int physical_apicid_2_cpu[];
extern volatile int cpu_2_physical_apicid[];

#define logical_apicid_to_cpu(apicid) logical_apicid_2_cpu[apicid]
#define cpu_to_logical_apicid(cpu) cpu_2_logical_apicid[cpu]
#define physical_apicid_to_cpu(apicid) physical_apicid_2_cpu[apicid]
#define cpu_to_physical_apicid(cpu) cpu_2_physical_apicid[cpu]
#ifdef CONFIG_CLUSTERED_APIC			/* use logical IDs to bootstrap */
#define boot_apicid_to_cpu(apicid) logical_apicid_2_cpu[apicid]
#define cpu_to_boot_apicid(cpu) cpu_2_logical_apicid[cpu]
#else /* !CONFIG_CLUSTERED_APIC */		/* use physical IDs to bootstrap */
#define boot_apicid_to_cpu(apicid) physical_apicid_2_cpu[apicid]
#define cpu_to_boot_apicid(cpu) cpu_2_physical_apicid[cpu]
#endif /* CONFIG_CLUSTERED_APIC */


#endif
