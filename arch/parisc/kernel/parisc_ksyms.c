/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/string.h>
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(memset);
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
EXPORT_SYMBOL(strpbrk);

#include <asm/hardware.h>	/* struct parisc_device for asm/pci.h */
#include <linux/pci.h>
EXPORT_SYMBOL(hppa_dma_ops);
#if defined(CONFIG_PCI) || defined(CONFIG_ISA)
EXPORT_SYMBOL(get_pci_node_path);
#endif

#include <linux/sched.h>
#include <asm/irq.h>
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);

#include <asm/processor.h>
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(boot_cpu_data);
#ifdef CONFIG_EISA
EXPORT_SYMBOL(EISA_bus);
#endif

#include <linux/pm.h>
EXPORT_SYMBOL(pm_power_off);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(synchronize_irq);
#endif /* CONFIG_SMP */

#include <asm/atomic.h>
EXPORT_SYMBOL(__xchg8);
EXPORT_SYMBOL(__xchg32);
EXPORT_SYMBOL(__cmpxchg_u32);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(__atomic_hash);
#endif
#ifdef __LP64__
EXPORT_SYMBOL(__xchg64);
EXPORT_SYMBOL(__cmpxchg_u64);
#endif

#include <asm/uaccess.h>
EXPORT_SYMBOL(lcopy_to_user);
EXPORT_SYMBOL(lcopy_from_user);
EXPORT_SYMBOL(lstrnlen_user);
EXPORT_SYMBOL(lclear_user);

#ifndef __LP64__
/* Needed so insmod can set dp value */
extern int $global$;
EXPORT_SYMBOL($global$);
#endif

EXPORT_SYMBOL(register_parisc_driver);
EXPORT_SYMBOL(unregister_parisc_driver);
EXPORT_SYMBOL(print_pci_hwpath);
EXPORT_SYMBOL(print_pa_hwpath);
EXPORT_SYMBOL(pdc_iodc_read);
EXPORT_SYMBOL(pdc_tod_read);
EXPORT_SYMBOL(pdc_tod_set);

#include <asm/io.h>
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(__memcpy_toio);
EXPORT_SYMBOL(__memcpy_fromio);
EXPORT_SYMBOL(__memset_io);

#if defined(CONFIG_PCI) || defined(CONFIG_ISA)
EXPORT_SYMBOL(inb);
EXPORT_SYMBOL(inw);
EXPORT_SYMBOL(inl);
EXPORT_SYMBOL(outb);
EXPORT_SYMBOL(outw);
EXPORT_SYMBOL(outl);

EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(insl);
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(outsl);
#endif

#include <asm/cache.h>
EXPORT_SYMBOL(flush_kernel_dcache_range_asm);
EXPORT_SYMBOL(flush_kernel_dcache_page);
EXPORT_SYMBOL(flush_data_cache_local);
EXPORT_SYMBOL(flush_kernel_icache_range_asm);
EXPORT_SYMBOL(__flush_dcache_page);
EXPORT_SYMBOL(flush_all_caches);
EXPORT_SYMBOL(dcache_stride);
EXPORT_SYMBOL(flush_cache_all_local);

#include <asm/unistd.h>
extern long sys_open(const char *, int, int);
extern off_t sys_lseek(int, off_t, int);
extern int sys_read(int, char *, int);
extern int sys_write(int, const char *, int);
EXPORT_SYMBOL(sys_open);
EXPORT_SYMBOL(sys_lseek);
EXPORT_SYMBOL(sys_read);
EXPORT_SYMBOL(sys_write);

#include <asm/semaphore.h>
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__down);

#include <linux/in6.h>
#include <asm/checksum.h>
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(csum_partial_copy_from_user);

#include <asm/pdc.h>
EXPORT_SYMBOL(pdc_add_valid);
EXPORT_SYMBOL(pdc_lan_station_id);
EXPORT_SYMBOL(pdc_get_initiator);
EXPORT_SYMBOL(pdc_sti_call);

extern void $$divI(void);
extern void $$divU(void);
extern void $$remI(void);
extern void $$remU(void);
extern void $$mulI(void);
extern void $$divU_3(void);
extern void $$divU_5(void);
extern void $$divU_6(void);
extern void $$divU_9(void);
extern void $$divU_10(void);
extern void $$divU_12(void);
extern void $$divU_7(void);
extern void $$divU_14(void);
extern void $$divU_15(void);
extern void $$divI_3(void);
extern void $$divI_5(void);
extern void $$divI_6(void);
extern void $$divI_7(void);
extern void $$divI_9(void);
extern void $$divI_10(void);
extern void $$divI_12(void);
extern void $$divI_14(void);
extern void $$divI_15(void);

EXPORT_SYMBOL($$divI);
EXPORT_SYMBOL($$divU);
EXPORT_SYMBOL($$remI);
EXPORT_SYMBOL($$remU);
EXPORT_SYMBOL($$mulI);
EXPORT_SYMBOL($$divU_3);
EXPORT_SYMBOL($$divU_5);
EXPORT_SYMBOL($$divU_6);
EXPORT_SYMBOL($$divU_9);
EXPORT_SYMBOL($$divU_10);
EXPORT_SYMBOL($$divU_12);
EXPORT_SYMBOL($$divU_7);
EXPORT_SYMBOL($$divU_14);
EXPORT_SYMBOL($$divU_15);
EXPORT_SYMBOL($$divI_3);
EXPORT_SYMBOL($$divI_5);
EXPORT_SYMBOL($$divI_6);
EXPORT_SYMBOL($$divI_7);
EXPORT_SYMBOL($$divI_9);
EXPORT_SYMBOL($$divI_10);
EXPORT_SYMBOL($$divI_12);
EXPORT_SYMBOL($$divI_14);
EXPORT_SYMBOL($$divI_15);

extern void __ashrdi3(void);
extern void __ashldi3(void);
extern void __lshrdi3(void);
extern void __muldi3(void);

EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__muldi3);

#ifdef __LP64__
extern void __divdi3(void);
extern void __udivdi3(void);
extern void __umoddi3(void);
extern void __moddi3(void);

EXPORT_SYMBOL(__divdi3);
EXPORT_SYMBOL(__udivdi3);
EXPORT_SYMBOL(__umoddi3);
EXPORT_SYMBOL(__moddi3);
#endif

#ifndef __LP64__
extern void $$dyncall(void);
EXPORT_SYMBOL($$dyncall);
#endif

#include <asm/pgtable.h>
EXPORT_SYMBOL(vmalloc_start);
