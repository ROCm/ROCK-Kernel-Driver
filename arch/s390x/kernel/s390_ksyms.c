/*
 *  arch/s390/kernel/s390_ksyms.c
 *
 *  S390 version
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/highuid.h>
#include <asm/ccwcache.h>
#include <asm/debug.h>
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/s390dyn.h>
#include <asm/ebcdic.h>
#include <asm/checksum.h>
#include <asm/delay.h>
#include <asm/pgalloc.h>
#include <asm/idals.h>
#if CONFIG_CHANDEV
#include <asm/chandev.h>
#endif
#if CONFIG_IP_MULTICAST
#include <net/arp.h>
#endif

/*
 * I/O subsystem
 */
EXPORT_SYMBOL(halt_IO);
EXPORT_SYMBOL(clear_IO);
EXPORT_SYMBOL(do_IO);
EXPORT_SYMBOL(resume_IO);
EXPORT_SYMBOL(ioinfo);
EXPORT_SYMBOL(get_dev_info_by_irq);
EXPORT_SYMBOL(get_dev_info_by_devno);
EXPORT_SYMBOL(get_irq_by_devno);
EXPORT_SYMBOL(get_devno_by_irq);
EXPORT_SYMBOL(get_irq_first);
EXPORT_SYMBOL(get_irq_next);
EXPORT_SYMBOL(read_conf_data);
EXPORT_SYMBOL(read_dev_chars);
EXPORT_SYMBOL(s390_request_irq_special);
EXPORT_SYMBOL(s390_device_register);
EXPORT_SYMBOL(s390_device_unregister);

EXPORT_SYMBOL(ccw_alloc_request);
EXPORT_SYMBOL(ccw_free_request);

EXPORT_SYMBOL(register_external_interrupt);
EXPORT_SYMBOL(unregister_external_interrupt);

/* 
 * debug feature
 */
EXPORT_SYMBOL(debug_register);
EXPORT_SYMBOL(debug_unregister);
EXPORT_SYMBOL(debug_set_level);
EXPORT_SYMBOL(debug_register_view);
EXPORT_SYMBOL(debug_unregister_view);
EXPORT_SYMBOL(debug_event);
EXPORT_SYMBOL(debug_int_event);
EXPORT_SYMBOL(debug_text_event);
EXPORT_SYMBOL(debug_exception);
EXPORT_SYMBOL(debug_int_exception);
EXPORT_SYMBOL(debug_text_exception);
EXPORT_SYMBOL(debug_hex_ascii_view);
EXPORT_SYMBOL(debug_raw_view);
EXPORT_SYMBOL(debug_dflt_header_fn);

/*
 * memory management
 */
EXPORT_SYMBOL(_oi_bitmap);
EXPORT_SYMBOL(_ni_bitmap);
EXPORT_SYMBOL(_zb_findmap);
EXPORT_SYMBOL(__copy_from_user_fixup);
EXPORT_SYMBOL(__copy_to_user_fixup);

/*
 * semaphore ops
 */
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__down_trylock);

/*
 * string functions
 */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strtok);
EXPORT_SYMBOL_NOVERS(strpbrk);

EXPORT_SYMBOL_NOVERS(_ascebc_500);
EXPORT_SYMBOL_NOVERS(_ebcasc_500);
EXPORT_SYMBOL_NOVERS(_ascebc);
EXPORT_SYMBOL_NOVERS(_ebcasc);
EXPORT_SYMBOL_NOVERS(_ebc_tolower);
EXPORT_SYMBOL_NOVERS(_ebc_toupper);

/*
 * binfmt_elf loader 
 */
EXPORT_SYMBOL(get_pte_slow);
EXPORT_SYMBOL(get_pmd_slow);
extern int dump_fpu (struct pt_regs * regs, s390_fp_regs *fpregs);
EXPORT_SYMBOL(dump_fpu);
#ifdef CONFIG_S390_SUPPORT
extern int setup_arg_pages32(struct linux_binprm *bprm);
EXPORT_SYMBOL(setup_arg_pages32);
#endif
EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);

/*
 * misc.
 */
EXPORT_SYMBOL(module_list);
EXPORT_SYMBOL(__udelay);
#ifdef CONFIG_SMP
#include <asm/smplock.h>
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
EXPORT_SYMBOL(lowcore_ptr);
EXPORT_SYMBOL(global_bh_lock);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(smp_ctl_set_bit);
EXPORT_SYMBOL(smp_ctl_clear_bit);
#endif
EXPORT_SYMBOL(kernel_thread);
#if CONFIG_CHANDEV
EXPORT_SYMBOL(chandev_register_and_probe);
EXPORT_SYMBOL(chandev_request_irq);
EXPORT_SYMBOL(chandev_unregister);
EXPORT_SYMBOL(chandev_initdevice);
EXPORT_SYMBOL(chandev_initnetdevice);
#endif
#if CONFIG_IP_MULTICAST
/* Required for lcs gigibit ethernet multicast support */
EXPORT_SYMBOL(arp_mc_map);
#endif
EXPORT_SYMBOL(s390_daemonize);
EXPORT_SYMBOL (set_normalized_cda);

