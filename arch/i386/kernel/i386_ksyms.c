#include <linux/config.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/irq.h>
#include <asm/checksum.h>
#include <asm/desc.h>

EXPORT_SYMBOL_GPL(cpu_gdt_descr);

EXPORT_SYMBOL(__down_failed);
EXPORT_SYMBOL(__down_failed_interruptible);
EXPORT_SYMBOL(__down_failed_trylock);
EXPORT_SYMBOL(__up_wakeup);
/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy_generic);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);

#ifdef CONFIG_SMP
extern void FASTCALL( __write_lock_failed(rwlock_t *rw));
extern void FASTCALL( __read_lock_failed(rwlock_t *rw));
EXPORT_SYMBOL(__write_lock_failed);
EXPORT_SYMBOL(__read_lock_failed);
#endif

EXPORT_SYMBOL(csum_partial);

#ifdef CONFIG_LKCD_DUMP_MODULE
#ifdef CONFIG_SMP
extern irq_desc_t irq_desc[NR_IRQS];
extern cpumask_t irq_affinity[NR_IRQS];
extern void stop_this_cpu(void *);
extern void dump_send_ipi(void);
EXPORT_SYMBOL_GPL(irq_desc);
EXPORT_SYMBOL_GPL(irq_affinity);
EXPORT_SYMBOL_GPL(stop_this_cpu);
EXPORT_SYMBOL_GPL(dump_send_ipi);
#endif
extern int page_is_ram(unsigned long);
EXPORT_SYMBOL_GPL(page_is_ram);
#ifdef ARCH_HAS_NMI_WATCHDOG
EXPORT_SYMBOL_GPL(touch_nmi_watchdog);
#endif
#endif
