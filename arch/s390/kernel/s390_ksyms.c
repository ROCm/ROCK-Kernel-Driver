/*
 *  arch/s390/kernel/s390_ksyms.c
 *
 *  S390 version
 */
#include <linux/config.h>
#include <linux/module.h>
#include <asm/irq.h>
#include <asm/string.h>
#include <asm/checksum.h>

/*
 * I/O subsystem
 */
EXPORT_SYMBOL(halt_IO);
EXPORT_SYMBOL(do_IO);
EXPORT_SYMBOL(resume_IO);
EXPORT_SYMBOL(ioinfo);
EXPORT_SYMBOL(get_dev_info_by_irq);
EXPORT_SYMBOL(get_dev_info_by_devno);
EXPORT_SYMBOL(get_irq_by_devno);
EXPORT_SYMBOL(get_devno_by_irq);
EXPORT_SYMBOL(get_irq_first);
EXPORT_SYMBOL(get_irq_next);

/*
 * memory management
 */
EXPORT_SYMBOL(_oi_bitmap);
EXPORT_SYMBOL(_ni_bitmap);
EXPORT_SYMBOL(_zb_findmap);

/*
 * string functions
 */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strtok);
EXPORT_SYMBOL_NOVERS(strpbrk);

/*
 * misc.
 */
#ifdef CONFIG_SMP
#include <asm/smplock.h>
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
EXPORT_SYMBOL(global_bh_lock);
EXPORT_SYMBOL(kernel_flag);
#endif
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(csum_fold);


