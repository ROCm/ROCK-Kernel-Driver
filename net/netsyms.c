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
#include <linux/ethtool.h>
#include <net/neighbour.h>
#include <net/snmp.h>
#include <net/dst.h>
#include <net/checksum.h>
#include <linux/etherdevice.h>
#include <net/route.h>
#ifdef CONFIG_HIPPI
#include <linux/hippidevice.h>
#endif
#include <net/pkt_sched.h>
#include <net/scm.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/random.h>
#ifdef CONFIG_NET_DIVERT
#include <linux/divert.h>
#endif /* CONFIG_NET_DIVERT */

#ifdef CONFIG_INET
#include <linux/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)
#include <net/atmclip.h>
#endif
#include <net/ip.h>
#include <net/udp.h>
#include <net/tcp.h>
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

#ifdef CONFIG_SYSCTL
extern int sysctl_max_syn_backlog;
#endif

/* Socket layer support routines */
EXPORT_SYMBOL(memcpy_fromiovec);
EXPORT_SYMBOL(memcpy_tokerneliovec);
EXPORT_SYMBOL(skb_recv_datagram);
EXPORT_SYMBOL(skb_free_datagram);
EXPORT_SYMBOL(skb_copy_datagram);
EXPORT_SYMBOL(skb_copy_datagram_iovec);
EXPORT_SYMBOL(skb_copy_and_csum_datagram_iovec);
EXPORT_SYMBOL(datagram_poll);
EXPORT_SYMBOL(put_cmsg);

EXPORT_SYMBOL(sk_run_filter);
EXPORT_SYMBOL(sk_chk_filter);

/*	dst_entry	*/
EXPORT_SYMBOL(dst_alloc);
EXPORT_SYMBOL(__dst_free);
EXPORT_SYMBOL(dst_destroy);

/*	misc. support routines */
EXPORT_SYMBOL(net_ratelimit);
EXPORT_SYMBOL(net_random);
EXPORT_SYMBOL(net_srandom);

/* Needed by smbfs.o */
EXPORT_SYMBOL(__scm_destroy);
EXPORT_SYMBOL(__scm_send);

/* Needed by unix.o */
EXPORT_SYMBOL(scm_fp_dup);
EXPORT_SYMBOL(files_stat);
EXPORT_SYMBOL(memcpy_toiovec);

#ifdef CONFIG_IPX_MODULE
EXPORT_SYMBOL(make_8023_client);
EXPORT_SYMBOL(destroy_8023_client);
EXPORT_SYMBOL(make_EII_client);
EXPORT_SYMBOL(destroy_EII_client);
#endif

EXPORT_SYMBOL(scm_detach_fds);

#ifdef CONFIG_NET_DIVERT
EXPORT_SYMBOL(alloc_divert_blk);
EXPORT_SYMBOL(free_divert_blk);
#endif /* CONFIG_NET_DIVERT */

#ifdef CONFIG_INET
/* Internet layer registration */
EXPORT_SYMBOL(inetdev_lock);
EXPORT_SYMBOL(inet_add_protocol);
EXPORT_SYMBOL(inet_del_protocol);
EXPORT_SYMBOL(ip_route_output_key);
EXPORT_SYMBOL(ip_route_input);
EXPORT_SYMBOL(icmp_send);
EXPORT_SYMBOL(icmp_statistics);
EXPORT_SYMBOL(icmp_err_convert);
EXPORT_SYMBOL(ip_options_compile);
EXPORT_SYMBOL(ip_options_undo);
EXPORT_SYMBOL(arp_send);
EXPORT_SYMBOL(arp_broken_ops);
EXPORT_SYMBOL(__ip_select_ident);
EXPORT_SYMBOL(ip_send_check);
EXPORT_SYMBOL(ip_fragment);
EXPORT_SYMBOL(in_aton);
EXPORT_SYMBOL(ip_mc_inc_group);
EXPORT_SYMBOL(ip_mc_dec_group);
EXPORT_SYMBOL(ip_mc_join_group);
EXPORT_SYMBOL(ip_finish_output);
EXPORT_SYMBOL(ip_cmsg_recv);
EXPORT_SYMBOL(inet_addr_type); 
EXPORT_SYMBOL(inet_select_addr);
EXPORT_SYMBOL(ip_dev_find);
EXPORT_SYMBOL(inetdev_by_index);
EXPORT_SYMBOL(in_dev_finish_destroy);
EXPORT_SYMBOL(ip_defrag);
EXPORT_SYMBOL(inet_peer_idlock);

/* Route manipulation */
EXPORT_SYMBOL(ip_rt_ioctl);
EXPORT_SYMBOL(devinet_ioctl);
EXPORT_SYMBOL(register_inetaddr_notifier);
EXPORT_SYMBOL(unregister_inetaddr_notifier);

/* needed for ip_gre -cw */
EXPORT_SYMBOL(ip_statistics);
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
EXPORT_SYMBOL_GPL(skb_cow_data);
EXPORT_SYMBOL_GPL(pskb_put);
EXPORT_SYMBOL_GPL(skb_to_sgvec);
#endif

EXPORT_SYMBOL(flow_cache_lookup);
EXPORT_SYMBOL(flow_cache_genid);

#if defined (CONFIG_IPV6_MODULE) || defined (CONFIG_IP_SCTP_MODULE)
/* inet functions common to v4 and v6 */

/* Socket demultiplexing. */

EXPORT_SYMBOL(ip_queue_xmit);
EXPORT_SYMBOL(memcpy_fromiovecend);
EXPORT_SYMBOL(csum_partial_copy_fromiovecend);
/* UDP/TCP exported functions for TCPv6 */
EXPORT_SYMBOL(tcp_check_req);
EXPORT_SYMBOL(tcp_child_process);
EXPORT_SYMBOL(tcp_parse_options);
EXPORT_SYMBOL(tcp_rcv_established);
EXPORT_SYMBOL(tcp_init_xmit_timers);
EXPORT_SYMBOL(tcp_clear_xmit_timers);
EXPORT_SYMBOL(tcp_statistics);
EXPORT_SYMBOL(tcp_rcv_state_process);
EXPORT_SYMBOL(tcp_timewait_state_process);
EXPORT_SYMBOL(tcp_create_openreq_child);
EXPORT_SYMBOL(tcp_tw_deschedule);
EXPORT_SYMBOL(tcp_delete_keepalive_timer);
EXPORT_SYMBOL(tcp_reset_keepalive_timer);
EXPORT_SYMBOL(net_statistics); 
EXPORT_SYMBOL(sysctl_tcp_reordering);
EXPORT_SYMBOL(sysctl_tcp_ecn);
EXPORT_SYMBOL(tcp_cwnd_application_limited);



extern int sysctl_tcp_tw_recycle;

#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(sysctl_tcp_tw_recycle); 
#endif

EXPORT_SYMBOL(ip_generic_getfrag);

#endif


#ifdef CONFIG_IP_SCTP_MODULE
EXPORT_SYMBOL(ip_setsockopt);
EXPORT_SYMBOL(ip_getsockopt);
#endif /* CONFIG_IP_SCTP_MODULE */



/* Used by at least ipip.c.  */
EXPORT_SYMBOL(ipv4_config);

/* Used by other modules */
EXPORT_SYMBOL(xrlim_allow);

EXPORT_SYMBOL(ip_rcv);
EXPORT_SYMBOL(arp_rcv);
EXPORT_SYMBOL(arp_tbl);
#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)
EXPORT_SYMBOL(clip_tbl_hook);
#endif
EXPORT_SYMBOL(arp_find);

#endif  /* CONFIG_INET */

#ifdef CONFIG_TR
EXPORT_SYMBOL(tr_source_route);
EXPORT_SYMBOL(tr_type_trans);
#endif

/* Device callback registration */

/* support for loadable net drivers */
#ifdef CONFIG_NET
EXPORT_SYMBOL(loopback_dev);
EXPORT_SYMBOL(eth_type_trans);
#ifdef CONFIG_FDDI
EXPORT_SYMBOL(fddi_type_trans);
#endif /* CONFIG_FDDI */
#if 0
EXPORT_SYMBOL(eth_copy_and_sum);
#endif
EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_base_lock);
EXPORT_SYMBOL(dev_mc_add);
EXPORT_SYMBOL(dev_mc_delete);
EXPORT_SYMBOL(dev_mc_upload);
EXPORT_SYMBOL(__kill_fasync);


#ifdef CONFIG_HIPPI
EXPORT_SYMBOL(hippi_type_trans);
#endif

#ifdef CONFIG_SYSCTL
#ifdef CONFIG_INET
EXPORT_SYMBOL(sysctl_ip_default_ttl);
#endif
#endif

#ifdef CONFIG_NET_SCHED
PSCHED_EXPORTLIST;
EXPORT_SYMBOL(pfifo_qdisc_ops);
EXPORT_SYMBOL(bfifo_qdisc_ops);
EXPORT_SYMBOL(register_qdisc);
EXPORT_SYMBOL(unregister_qdisc);
EXPORT_SYMBOL(qdisc_get_rtab);
EXPORT_SYMBOL(qdisc_put_rtab);
EXPORT_SYMBOL(qdisc_copy_stats);
#ifdef CONFIG_NET_ESTIMATOR
EXPORT_SYMBOL(qdisc_new_estimator);
EXPORT_SYMBOL(qdisc_kill_estimator);
#endif
#ifdef CONFIG_NET_CLS_POLICE
EXPORT_SYMBOL(tcf_police);
EXPORT_SYMBOL(tcf_police_locate);
EXPORT_SYMBOL(tcf_police_destroy);
EXPORT_SYMBOL(tcf_police_dump);
#endif
#endif
#ifdef CONFIG_NET_CLS
EXPORT_SYMBOL(register_tcf_proto_ops);
EXPORT_SYMBOL(unregister_tcf_proto_ops);
#endif

EXPORT_PER_CPU_SYMBOL(softnet_data);

#ifdef CONFIG_NET_RADIO
#include <net/iw_handler.h>		/* Wireless Extensions driver API */
EXPORT_SYMBOL(wireless_send_event);
EXPORT_SYMBOL(iw_handler_set_spy);
EXPORT_SYMBOL(iw_handler_get_spy);
EXPORT_SYMBOL(iw_handler_set_thrspy);
EXPORT_SYMBOL(iw_handler_get_thrspy);
EXPORT_SYMBOL(wireless_spy_update);
#endif	/* CONFIG_NET_RADIO */

EXPORT_SYMBOL(linkwatch_fire_event);

/* ethtool.c */
EXPORT_SYMBOL(ethtool_op_get_link);
EXPORT_SYMBOL(ethtool_op_get_tx_csum);
EXPORT_SYMBOL(ethtool_op_set_tx_csum);
EXPORT_SYMBOL(ethtool_op_get_sg);
EXPORT_SYMBOL(ethtool_op_set_sg);
EXPORT_SYMBOL(ethtool_op_get_tso);
EXPORT_SYMBOL(ethtool_op_set_tso);

#endif  /* CONFIG_NET */
