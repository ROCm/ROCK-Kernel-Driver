/*      $Id: kcompat.h,v 5.9 2004/09/05 16:48:48 lirc Exp $      */

#ifndef _KCOMPAT_H
#define _KCOMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define LIRC_HAVE_DEVFS
#define LIRC_HAVE_DEVFS_26
#define LIRC_HAVE_SYSFS
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#define LIRC_HAVE_DEVFS
#define LIRC_HAVE_DEVFS_24
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
#include <linux/timer.h>
#include <linux/interrupt.h>
static inline void del_timer_sync(struct timer_list * timerlist)
{
	start_bh_atomic();
	del_timer(timerlist);
	end_bh_atomic();
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
#ifdef daemonize
#undef daemonize
#endif
#define daemonize(name) do {                                           \
                                                                       \
	lock_kernel();                                                 \
	                                                               \
	exit_mm(current);                                              \
	exit_files(current);                                           \
	exit_fs(current);                                              \
	current->session = 1;                                          \
	current->pgrp = 1;                                             \
	current->euid = 0;                                             \
	current->tty = NULL;                                           \
	sigfillset(&current->blocked);                                 \
	                                                               \
	strcpy(current->comm, name);                                   \
	                                                               \
	unlock_kernel();                                               \
                                                                       \
} while (0)

/* Not sure when this was introduced, sometime during 2.5.X */
#define MODULE_PARM_int(x) MODULE_PARM(x, "i")
#define MODULE_PARM_bool(x) MODULE_PARM(x, "i")
#define MODULE_PARM_long(x) MODULE_PARM(x, "l")
#define module_param(x,y,z) MODULE_PARM_##y(x)
#else
#include <linux/moduleparam.h>
#endif /* Linux < 2.6.0 */

#ifdef LIRC_HAVE_DEVFS_24
#ifdef register_chrdev
#undef register_chrdev
#endif
#define register_chrdev devfs_register_chrdev
#ifdef unregister_chrdev
#undef unregister_chrdev
#endif
#define unregister_chrdev devfs_unregister_chrdev
#endif /* DEVFS 2.4 */

#ifndef LIRC_HAVE_SYSFS
#define class_simple_destroy(x) do { } while(0)
#define class_simple_create(x,y) NULL
#define class_simple_device_remove(x) do { } while(0)
#define class_simple_device_add(x, y, z, xx, yy) 0
#define IS_ERR(x) 0
struct class_simple 
{
	int notused;
};	
#endif /* No SYSFS */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#define KERNEL_2_5

/*
 * Recent kernels should handle some of this autmatically by 
 * increasing/decreasing use count when a dependant module is 
 * loaded/unloaded but we need to keep track when a chardev is 
 * opened/closed.
 */
#ifdef MOD_INC_USE_COUNT
#undef MOD_INC_USE_COUNT
#endif
#define MOD_INC_USE_COUNT try_module_get(THIS_MODULE)

#ifdef MOD_DEC_USE_COUNT
#undef MOD_DEC_USE_COUNT
#endif
#define MOD_DEC_USE_COUNT module_put(THIS_MODULE)

#ifdef EXPORT_NO_SYMBOLS
#undef EXPORT_NO_SYMBOLS
#endif
#define EXPORT_NO_SYMBOLS

#endif /* Kernel >= 2.5.0 */

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(x)
#endif

#ifndef MODULE_PARM_DESC
#define MODULE_PARM_DESC(x,y)
#endif

#ifndef MODULE_ALIAS_CHARDEV_MAJOR
#define MODULE_ALIAS_CHARDEV_MAJOR(x)
#endif

#ifndef MODULE_DEVICE_TABLE
#define MODULE_DEVICE_TABLE(x,y)
#endif

#ifndef IRQ_RETVAL
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif

#ifndef MOD_IN_USE
#ifdef CONFIG_MODULE_UNLOAD
#define MOD_IN_USE module_refcount(THIS_MODULE)
#else
#error "LIRC modules currently require"
#error "  'Loadable module support  --->  Module unloading'"
#error "to be enabled in the kernel"
#endif
#endif

#if !defined(local_irq_save)
#define local_irq_save(flags) do{ save_flags(flags);cli(); } while(0)
#endif
#if !defined(local_irq_restore)
#define local_irq_restore(flags) do{ restore_flags(flags); } while(0)
#endif

#if !defined(pci_pretty_name)
#define pci_pretty_name(dev) ((dev)->name)
#endif

#endif /* _KCOMPAT_H */
