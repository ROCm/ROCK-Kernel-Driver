#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/string.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/tlbflush.h>

extern void dump_thread(struct pt_regs *, struct user *);

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_HD) || defined(CONFIG_BLK_DEV_IDE_MODULE) || defined(CONFIG_BLK_DEV_HD_MODULE)
extern struct drive_info_struct drive_info;
EXPORT_SYMBOL(drive_info);
#endif

/* platform dependent support */
EXPORT_SYMBOL(boot_cpu_data);
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down_trylock);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);
/* Delay loops */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);

EXPORT_SYMBOL_NOVERS(__get_user_1);
EXPORT_SYMBOL_NOVERS(__get_user_2);
EXPORT_SYMBOL_NOVERS(__get_user_4);

EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__generic_copy_from_user);
EXPORT_SYMBOL(__generic_copy_to_user);
EXPORT_SYMBOL(strnlen_user);

#ifdef CONFIG_SMP
#ifdef CONFIG_CHIP_M32700_TS1
extern void *dcache_dummy;
EXPORT_SYMBOL(dcache_dummy);
#endif
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_callout_map);

/* Global SMP stuff */
EXPORT_SYMBOL(synchronize_irq);
EXPORT_SYMBOL(smp_call_function);

/* TLB flushing */
EXPORT_SYMBOL(smp_flush_tlb_page);
EXPORT_SYMBOL_GPL(smp_flush_tlb_all);
#endif

/* compiler generated symbol */
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __lshldi3(void);
extern void __lshrdi3(void);
extern void __muldi3(void);
EXPORT_SYMBOL_NOVERS(__ashldi3);
EXPORT_SYMBOL_NOVERS(__ashrdi3);
EXPORT_SYMBOL_NOVERS(__lshldi3);
EXPORT_SYMBOL_NOVERS(__lshrdi3);
EXPORT_SYMBOL_NOVERS(__muldi3);

/* memory and string operations */
EXPORT_SYMBOL_NOVERS(memchr);
EXPORT_SYMBOL_NOVERS(memcpy);
/* EXPORT_SYMBOL_NOVERS(memcpy_fromio); // not implement yet */
/* EXPORT_SYMBOL_NOVERS(memcpy_toio); // not implement yet */
EXPORT_SYMBOL_NOVERS(memset);
/* EXPORT_SYMBOL_NOVERS(memset_io); // not implement yet */
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(copy_page);
EXPORT_SYMBOL_NOVERS(clear_page);

EXPORT_SYMBOL_NOVERS(strcat);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strcpy);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strncpy);

EXPORT_SYMBOL_NOVERS(_inb);
EXPORT_SYMBOL_NOVERS(_inw);
EXPORT_SYMBOL_NOVERS(_inl);
EXPORT_SYMBOL_NOVERS(_outb);
EXPORT_SYMBOL_NOVERS(_outw);
EXPORT_SYMBOL_NOVERS(_outl);
EXPORT_SYMBOL_NOVERS(_inb_p);
EXPORT_SYMBOL_NOVERS(_inw_p);
EXPORT_SYMBOL_NOVERS(_inl_p);
EXPORT_SYMBOL_NOVERS(_outb_p);
EXPORT_SYMBOL_NOVERS(_outw_p);
EXPORT_SYMBOL_NOVERS(_outl_p);
EXPORT_SYMBOL_NOVERS(_insb);
EXPORT_SYMBOL_NOVERS(_insw);
EXPORT_SYMBOL_NOVERS(_insl);
EXPORT_SYMBOL_NOVERS(_outsb);
EXPORT_SYMBOL_NOVERS(_outsw);
EXPORT_SYMBOL_NOVERS(_outsl);
EXPORT_SYMBOL_NOVERS(_readb);
EXPORT_SYMBOL_NOVERS(_readw);
EXPORT_SYMBOL_NOVERS(_readl);
EXPORT_SYMBOL_NOVERS(_writeb);
EXPORT_SYMBOL_NOVERS(_writew);
EXPORT_SYMBOL_NOVERS(_writel);

