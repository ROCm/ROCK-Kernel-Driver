/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/fddidevice.h>
#include <linux/trdevice.h>
#include <linux/fcdevice.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <net/snmp.h>
#include <net/checksum.h>
#include <linux/etherdevice.h>
#include <net/route.h>
#ifdef CONFIG_HIPPI
#include <linux/hippidevice.h>
#endif
#include <net/pkt_sched.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/random.h>

#ifdef CONFIG_INET
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/inet_common.h>
#include <linux/inet.h>
#include <linux/mroute.h>
#include <linux/igmp.h>
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

#ifdef CONFIG_ATALK_MODULE
#include <net/sock.h>
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
/* Internet layer registration */
EXPORT_SYMBOL(inet_add_protocol);
EXPORT_SYMBOL(inet_del_protocol);
EXPORT_SYMBOL(ip_route_output_key);
EXPORT_SYMBOL(ip_route_input);
EXPORT_SYMBOL(icmp_send);
EXPORT_SYMBOL(icmp_statistics);
EXPORT_SYMBOL(icmp_err_convert);
EXPORT_SYMBOL(ip_options_compile);
EXPORT_SYMBOL(ip_options_undo);
EXPORT_SYMBOL(__ip_select_ident);
EXPORT_SYMBOL(in_aton);
EXPORT_SYMBOL(ip_mc_inc_group);
EXPORT_SYMBOL(ip_mc_dec_group);
EXPORT_SYMBOL(ip_mc_join_group);
EXPORT_SYMBOL(inet_addr_type); 
EXPORT_SYMBOL(ip_dev_find);
EXPORT_SYMBOL(ip_defrag);
EXPORT_SYMBOL(inet_peer_idlock);

/* Route manipulation */
EXPORT_SYMBOL(ip_rt_ioctl);

/* needed for ip_gre -cw */
EXPORT_SYMBOL(ip_statistics);
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
EXPORT_SYMBOL_GPL(skb_cow_data);
EXPORT_SYMBOL_GPL(pskb_put);
EXPORT_SYMBOL_GPL(skb_to_sgvec);
#endif


#if defined (CONFIG_IPV6_MODULE) || defined (CONFIG_IP_SCTP_MODULE)
/* inet functions common to v4 and v6 */

/* UDP/TCP exported functions for TCPv6 */
EXPORT_SYMBOL(net_statistics); 
#endif

/* Used by at least ipip.c.  */
EXPORT_SYMBOL(ipv4_config);

/* Used by other modules */
EXPORT_SYMBOL(xrlim_allow);

EXPORT_SYMBOL(ip_rcv);
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
PSCHED_EXPORTLIST;
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

EXPORT_PER_CPU_SYMBOL(softnet_data);

EXPORT_SYMBOL(linkwatch_fire_event);
#endif  /* CONFIG_NET */
