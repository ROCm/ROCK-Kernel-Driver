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


#endif
