/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/string.h>
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strtok);

#include <linux/pci.h>
EXPORT_SYMBOL(hppa_dma_ops);

#include <asm/irq.h>
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);

#include <asm/processor.h>
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(boot_cpu_data);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(synchronize_irq);

#include <asm/smplock.h>
EXPORT_SYMBOL(kernel_flag);

#include <asm/system.h>
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);

#endif

#include <asm/uaccess.h>
EXPORT_SYMBOL(lcopy_to_user);
EXPORT_SYMBOL(lcopy_from_user);

/* Needed so insmod can set dp value */

extern int data_start;

EXPORT_SYMBOL_NOVERS(data_start);

#include <asm/gsc.h>
EXPORT_SYMBOL(_gsc_writeb);
EXPORT_SYMBOL(_gsc_writew);
EXPORT_SYMBOL(_gsc_writel);
EXPORT_SYMBOL(_gsc_readb);
EXPORT_SYMBOL(_gsc_readw);
EXPORT_SYMBOL(_gsc_readl);
EXPORT_SYMBOL(busdevice_alloc_irq);
EXPORT_SYMBOL(register_driver);
EXPORT_SYMBOL(gsc_alloc_irq);
EXPORT_SYMBOL(pdc_iodc_read);

extern void $$divI(void);
extern void $$divU(void);
extern void $$remI(void);
extern void $$remU(void);
extern void $$mulI(void);
extern void $$mulU(void);
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

EXPORT_SYMBOL_NOVERS($$divI);
EXPORT_SYMBOL_NOVERS($$divU);
EXPORT_SYMBOL_NOVERS($$remI);
EXPORT_SYMBOL_NOVERS($$remU);
EXPORT_SYMBOL_NOVERS($$mulI);
EXPORT_SYMBOL_NOVERS($$mulU);
EXPORT_SYMBOL_NOVERS($$divU_3);
EXPORT_SYMBOL_NOVERS($$divU_5);
EXPORT_SYMBOL_NOVERS($$divU_6);
EXPORT_SYMBOL_NOVERS($$divU_9);
EXPORT_SYMBOL_NOVERS($$divU_10);
EXPORT_SYMBOL_NOVERS($$divU_12);
EXPORT_SYMBOL_NOVERS($$divU_7);
EXPORT_SYMBOL_NOVERS($$divU_14);
EXPORT_SYMBOL_NOVERS($$divU_15);
EXPORT_SYMBOL_NOVERS($$divI_3);
EXPORT_SYMBOL_NOVERS($$divI_5);
EXPORT_SYMBOL_NOVERS($$divI_6);
EXPORT_SYMBOL_NOVERS($$divI_7);
EXPORT_SYMBOL_NOVERS($$divI_9);
EXPORT_SYMBOL_NOVERS($$divI_10);
EXPORT_SYMBOL_NOVERS($$divI_12);
EXPORT_SYMBOL_NOVERS($$divI_14);
EXPORT_SYMBOL_NOVERS($$divI_15);

extern void __ashrdi3(void);

EXPORT_SYMBOL_NOVERS(__ashrdi3);

#ifdef __LP64__
extern void __divdi3(void);
extern void __udivdi3(void);

EXPORT_SYMBOL_NOVERS(__divdi3);
EXPORT_SYMBOL_NOVERS(__udivdi3);
#endif

#ifndef __LP64__
extern void $$dyncall(void);
EXPORT_SYMBOL_NOVERS($$dyncall);
#endif

