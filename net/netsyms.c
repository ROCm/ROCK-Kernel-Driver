/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <net/pkt_sched.h>

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

#ifdef CONFIG_NET_SCHED
EXPORT_SYMBOL(pfifo_qdisc_ops);
EXPORT_SYMBOL(bfifo_qdisc_ops);
#ifdef CONFIG_NET_ESTIMATOR
EXPORT_SYMBOL(qdisc_new_estimator);
EXPORT_SYMBOL(qdisc_kill_estimator);
#endif
#endif
#ifdef CONFIG_NET_CLS
EXPORT_SYMBOL(register_tcf_proto_ops);
EXPORT_SYMBOL(unregister_tcf_proto_ops);
#endif

#endif  /* CONFIG_NET */
