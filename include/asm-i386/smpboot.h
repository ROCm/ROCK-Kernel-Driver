#ifndef __ASM_SMPBOOT_H
#define __ASM_SMPBOOT_H

#ifndef clustered_apic_mode
 #ifdef CONFIG_CLUSTERED_APIC
  #define clustered_apic_mode (1)
 #else /* !CONFIG_CLUSTERED_APIC */
  #define clustered_apic_mode (0)
 #endif /* CONFIG_CLUSTERED_APIC */
#endif 
 
#endif
