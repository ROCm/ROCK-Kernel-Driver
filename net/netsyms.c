/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/module.h>

#ifdef CONFIG_INET
#if defined(CONFIG_INET_AH) || defined(CONFIG_INET_AH_MODULE) || defined(CONFIG_INET6_AH) || defined(CONFIG_INET6_AH_MODULE)
#include <net/ah.h>
#endif
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
#include <net/esp.h>
#endif
#endif

/* Needed by unix.o */
EXPORT_SYMBOL(files_stat);

#ifdef CONFIG_INET
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
EXPORT_SYMBOL_GPL(skb_cow_data);
EXPORT_SYMBOL_GPL(pskb_put);
EXPORT_SYMBOL_GPL(skb_to_sgvec);
#endif
#endif  /* CONFIG_INET */

/* support for loadable net drivers */
#ifdef CONFIG_NET
EXPORT_SYMBOL(loopback_dev);
EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_base_lock);
EXPORT_SYMBOL(__kill_fasync);
#endif  /* CONFIG_NET */
