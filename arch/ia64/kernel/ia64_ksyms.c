/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/string.h>
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strtok);

#include <asm/hw_irq.h>
EXPORT_SYMBOL(isa_irq_to_vector_map);

#include <linux/in6.h>
#include <asm/checksum.h>
/* not coded yet?? EXPORT_SYMBOL(csum_ipv6_magic); */
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(csum_tcpudp_magic);
EXPORT_SYMBOL(ip_compute_csum);
EXPORT_SYMBOL(ip_fast_csum);

#include <asm/io.h>
EXPORT_SYMBOL(__ia64_memcpy_fromio);
EXPORT_SYMBOL(__ia64_memcpy_toio);
EXPORT_SYMBOL(__ia64_memset_c_io);

#include <asm/irq.h>
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);

#include <asm/page.h>
EXPORT_SYMBOL(clear_page);

#include <asm/processor.h>
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(kernel_thread);

#include <asm/system.h>
#ifdef CONFIG_IA64_DEBUG_IRQ
EXPORT_SYMBOL(last_cli_ip);
#endif

#ifdef CONFIG_SMP

#include <asm/current.h>
#include <asm/hardirq.h>
EXPORT_SYMBOL(synchronize_irq);

#include <asm/smp.h>
EXPORT_SYMBOL(smp_call_function);

#include <linux/smp.h>
EXPORT_SYMBOL(smp_num_cpus);

#include <asm/smplock.h>
EXPORT_SYMBOL(kernel_flag);

/* #include <asm/system.h> */
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);

#endif

#include <asm/uaccess.h>
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__do_clear_user);
EXPORT_SYMBOL(__strlen_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

#include <asm/unistd.h>
EXPORT_SYMBOL(__ia64_syscall);

/* from arch/ia64/lib */
extern void __divsi3(void);
extern void __udivsi3(void);
extern void __modsi3(void);
extern void __umodsi3(void);
extern void __divdi3(void);
extern void __udivdi3(void);
extern void __moddi3(void);
extern void __umoddi3(void);

EXPORT_SYMBOL_NOVERS(__divsi3);
EXPORT_SYMBOL_NOVERS(__udivsi3);
EXPORT_SYMBOL_NOVERS(__modsi3);
EXPORT_SYMBOL_NOVERS(__umodsi3);
EXPORT_SYMBOL_NOVERS(__divdi3);
EXPORT_SYMBOL_NOVERS(__udivdi3);
EXPORT_SYMBOL_NOVERS(__moddi3);
EXPORT_SYMBOL_NOVERS(__umoddi3);

extern unsigned long ia64_iobase;
EXPORT_SYMBOL(ia64_iobase);
