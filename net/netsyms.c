/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/fddidevice.h>
#include <linux/trdevice.h>
#include <linux/fcdevice.h>
#include <linux/etherdevice.h>
#ifdef CONFIG_HIPPI
#include <linux/hippidevice.h>
#endif
#include <net/pkt_sched.h>

#ifdef CONFIG_INET
#if defined(CONFIG_INET_AH) || defined(CONFIG_INET_AH_MODULE) || defined(CONFIG_INET6_AH) || defined(CONFIG_INET6_AH_MODULE)
#include <net/ah.h>
#endif
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
#include <net/esp.h>
#endif
#endif

#ifdef CONFIG_IPX_MODULE
extern struct datalink_proto   *make_EII_client(void);
extern struct datalink_proto   *make_8023_client(void);
extern void destroy_EII_client(struct datalink_proto *);
extern void destroy_8023_client(struct datalink_proto *);
#endif

/* Needed by unix.o */
EXPORT_SYMBOL(files_stat);

#ifdef CONFIG_IPX_MODULE
EXPORT_SYMBOL(make_8023_client);
EXPORT_SYMBOL(destroy_8023_client);
EXPORT_SYMBOL(make_EII_client);
EXPORT_SYMBOL(destroy_EII_client);
#endif

#ifdef CONFIG_INET
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
EXPORT_SYMBOL_GPL(skb_cow_data);
EXPORT_SYMBOL_GPL(pskb_put);
EXPORT_SYMBOL_GPL(skb_to_sgvec);
#endif
#endif  /* CONFIG_INET */

#ifdef CONFIG_TR
EXPORT_SYMBOL(tr_source_route);
EXPORT_SYMBOL(tr_type_trans);
#endif

/* support for loadable net drivers */
#ifdef CONFIG_NET
EXPORT_SYMBOL(loopback_dev);
EXPORT_SYMBOL(eth_type_trans);
#ifdef CONFIG_FDDI
EXPORT_SYMBOL(fddi_type_trans);
#endif /* CONFIG_FDDI */
EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_base_lock);
EXPORT_SYMBOL(__kill_fasync);

#ifdef CONFIG_HIPPI
EXPORT_SYMBOL(hippi_type_trans);
#endif

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
