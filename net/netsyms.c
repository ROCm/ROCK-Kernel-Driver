/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/netdevice.h>

/* Needed by unix.o */
EXPORT_SYMBOL(files_stat);

/* support for loadable net drivers */
#ifdef CONFIG_NET
EXPORT_SYMBOL(loopback_dev);
EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_base_lock);
EXPORT_SYMBOL(__kill_fasync);
#endif  /* CONFIG_NET */
