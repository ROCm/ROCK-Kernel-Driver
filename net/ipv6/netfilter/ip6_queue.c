/*
 * This is a module which is used for queueing IPv6 packets and
 * communicating with userspace via netlink.
 *
 * (C) 2001 Fernando Anton, this code is GPL.
 *     IPv64 Project - Work based in IPv64 draft by Arturo Azcorra.
 *     Universidad Carlos III de Madrid - Leganes (Madrid) - Spain
 *     Universidad Politecnica de Alcala de Henares - Alcala de H. (Madrid) - Spain
 *     email: fanton@it.uc3m.es
 *
 * 2001-11-06: First try. Working with ip_queue.c for IPv4 and trying
 *             to adapt it to IPv6
 *             HEAVILY based in ipqueue.c by James Morris. It's just
 *             a little modified version of it, so he's nearly the
 *             real coder of this.
 *             Few changes needed, mainly the hard_routing code and
 *             the netlink socket protocol (we're NETLINK_IP6_FW).
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/ipv6.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>

/* We're still usign the following structs. No need to change them: */
/*   ipq_packet_msg                                                 */
/*   ipq_mode_msg                                                   */
/*   ipq_verdict_msg                                                */
/*   ipq_peer_msg                                                   */
#include <linux/netfilter_ipv4/ip_queue.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

#define IPQ_QMAX_DEFAULT 1024
#define IPQ_PROC_FS_NAME "ip6_queue"
#define NET_IPQ_QMAX 2088
#define NET_IPQ_QMAX_NAME "ip6_queue_maxlen"

typedef struct ip6q_rt_info {
	struct in6_addr daddr;
	struct in6_addr saddr;
} ip6q_rt_info_t;

typedef struct ip6q_queue_element {
	struct list_head list;		/* Links element into queue */
	int verdict;			/* Current verdict */
	struct nf_info *info;		/* Extra info from netfilter */
	struct sk_buff *skb;		/* Packet inside */
	ip6q_rt_info_t rt_info;		/* May need post-mangle routing */
} ip6q_queue_element_t;

typedef int (*ip6q_send_cb_t)(ip6q_queue_element_t *e);

typedef struct ip6q_peer {
	pid_t pid;			/* PID of userland peer */
	unsigned char died;		/* We think the peer died */
	unsigned char copy_mode;	/* Copy packet as well as metadata? */
	size_t copy_range;		/* Range past metadata to copy */
	ip6q_send_cb_t send;		/* Callback for sending data to peer */
} ip6q_peer_t;

typedef struct ip6q_queue {
 	int len;			/* Current queue len */
 	int *maxlen;			/* Maximum queue len, via sysctl */
 	unsigned char flushing;		/* If queue is being flushed */
 	unsigned char terminate;	/* If the queue is being terminated */
 	struct list_head list;		/* Head of packet queue */
 	spinlock_t lock;		/* Queue spinlock */
 	ip6q_peer_t peer;		/* Userland peer */
} ip6q_queue_t;

/****************************************************************************
 *
 * Packet queue
 *
 ****************************************************************************/
/* Dequeue a packet if matched by cmp, or the next available if cmp is NULL */
static ip6q_queue_element_t *
ip6q_dequeue(ip6q_queue_t *q,
            int (*cmp)(ip6q_queue_element_t *, unsigned long),
            unsigned long data)
{
	struct list_head *i;

	spin_lock_bh(&q->lock);
	for (i = q->list.prev; i != &q->list; i = i->prev) {
		ip6q_queue_element_t *e = (ip6q_queue_element_t *)i;
		
		if (!cmp || cmp(e, data)) {
			list_del(&e->list);
			q->len--;
			spin_unlock_bh(&q->lock);
			return e;
		}
	}
	spin_unlock_bh(&q->lock);
	return NULL;
}

/* Flush all packets */
static void ip6q_flush(ip6q_queue_t *q)
{
	ip6q_queue_element_t *e;
	
	spin_lock_bh(&q->lock);
	q->flushing = 1;
	spin_unlock_bh(&q->lock);
	while ((e = ip6q_dequeue(q, NULL, 0))) {
		e->verdict = NF_DROP;
		nf_reinject(e->skb, e->info, e->verdict);
		kfree(e);
	}
	spin_lock_bh(&q->lock);
	q->flushing = 0;
	spin_unlock_bh(&q->lock);
}

static ip6q_queue_t *ip6q_create_queue(nf_queue_outfn_t outfn,
                                     ip6q_send_cb_t send_cb,
                                     int *errp, int *sysctl_qmax)
{
	int status;
	ip6q_queue_t *q;

	*errp = 0;
	q = kmalloc(sizeof(ip6q_queue_t), GFP_KERNEL);
	if (q == NULL) {
		*errp = -ENOMEM;
		return NULL;
	}
	q->peer.pid = 0;
	q->peer.died = 0;
	q->peer.copy_mode = IPQ_COPY_NONE;
	q->peer.copy_range = 0;
	q->peer.send = send_cb;
	q->len = 0;
	q->maxlen = sysctl_qmax;
	q->flushing = 0;
	q->terminate = 0;
	INIT_LIST_HEAD(&q->list);
	spin_lock_init(&q->lock);
	status = nf_register_queue_handler(PF_INET6, outfn, q);
	if (status < 0) {
		*errp = -EBUSY;
		kfree(q);
		return NULL;
	}
	return q;
}

static int ip6q_enqueue(ip6q_queue_t *q,
                       struct sk_buff *skb, struct nf_info *info)
{
	ip6q_queue_element_t *e;
	int status;
	
	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (e == NULL) {
		printk(KERN_ERR "ip6_queue: OOM in enqueue\n");
		return -ENOMEM;
	}

	e->verdict = NF_DROP;
	e->info = info;
	e->skb = skb;

	if (e->info->hook == NF_IP_LOCAL_OUT) {
		struct ipv6hdr *iph = skb->nh.ipv6h;

		e->rt_info.daddr = iph->daddr;
		e->rt_info.saddr = iph->saddr;
	}

	spin_lock_bh(&q->lock);
	if (q->len >= *q->maxlen) {
		spin_unlock_bh(&q->lock);
		if (net_ratelimit()) 
			printk(KERN_WARNING "ip6_queue: full at %d entries, "
			       "dropping packet(s).\n", q->len);
		goto free_drop;
	}
	if (q->flushing || q->peer.copy_mode == IPQ_COPY_NONE
	    || q->peer.pid == 0 || q->peer.died || q->terminate) {
		spin_unlock_bh(&q->lock);
		goto free_drop;
	}
	status = q->peer.send(e);
	if (status > 0) {
		list_add(&e->list, &q->list);
		q->len++;
		spin_unlock_bh(&q->lock);
		return status;
	}
	spin_unlock_bh(&q->lock);
	if (status == -ECONNREFUSED) {
		printk(KERN_INFO "ip6_queue: peer %d died, "
		       "resetting state and flushing queue\n", q->peer.pid);
			q->peer.died = 1;
			q->peer.pid = 0;
			q->peer.copy_mode = IPQ_COPY_NONE;
			q->peer.copy_range = 0;
			ip6q_flush(q);
	}
free_drop:
	kfree(e);
	return -EBUSY;
}

static void ip6q_destroy_queue(ip6q_queue_t *q)
{
	nf_unregister_queue_handler(PF_INET6);
	spin_lock_bh(&q->lock);
	q->terminate = 1;
	spin_unlock_bh(&q->lock);
	ip6q_flush(q);
	kfree(q);
}

/*
 * Taken from net/ipv6/ip6_output.c
 *
 * We should use the one there, but is defined static
 * so we put this just here and let the things as
 * they are now.
 *
 * If that one is modified, this one should be modified too.
 */
static int route6_me_harder(struct sk_buff *skb)
{
	struct ipv6hdr *iph = skb->nh.ipv6h;
	struct dst_entry *dst;
	struct flowi fl;

	fl.proto = iph->nexthdr;
	fl.fl6_dst = &iph->daddr;
	fl.fl6_src = &iph->saddr;
	fl.oif = skb->sk ? skb->sk->bound_dev_if : 0;
	fl.fl6_flowlabel = 0;
	fl.uli_u.ports.dport = 0;
	fl.uli_u.ports.sport = 0;

	dst = ip6_route_output(skb->sk, &fl);

	if (dst->error) {
		if (net_ratelimit())
			printk(KERN_DEBUG "route6_me_harder: No more route.\n");
		return -EINVAL;
	}

	/* Drop old route. */
	dst_release(skb->dst);

	skb->dst = dst;
	return 0;
}
static int ip6q_mangle_ipv6(ipq_verdict_msg_t *v, ip6q_queue_element_t *e)
{
	int diff;
	struct ipv6hdr *user_iph = (struct ipv6hdr *)v->payload;

	if (v->data_len < sizeof(*user_iph))
		return 0;
	diff = v->data_len - e->skb->len;
	if (diff < 0)
		skb_trim(e->skb, v->data_len);
	else if (diff > 0) {
		if (v->data_len > 0xFFFF)
			return -EINVAL;
		if (diff > skb_tailroom(e->skb)) {
			struct sk_buff *newskb;
			
			newskb = skb_copy_expand(e->skb,
			                         skb_headroom(e->skb),
			                         diff,
			                         GFP_ATOMIC);
			if (newskb == NULL) {
				printk(KERN_WARNING "ip6_queue: OOM "
				      "in mangle, dropping packet\n");
				return -ENOMEM;
			}
			if (e->skb->sk)
				skb_set_owner_w(newskb, e->skb->sk);
			kfree_skb(e->skb);
			e->skb = newskb;
		}
		skb_put(e->skb, diff);
	}
	memcpy(e->skb->data, v->payload, v->data_len);
	e->skb->nfcache |= NFC_ALTERED;

	/*
	 * Extra routing may needed on local out, as the QUEUE target never
	 * returns control to the table.
         * Not a nice way to cmp, but works
	 */
	if (e->info->hook == NF_IP_LOCAL_OUT) {
		struct ipv6hdr *iph = e->skb->nh.ipv6h;
		if (!(   iph->daddr.in6_u.u6_addr32[0] == e->rt_info.daddr.in6_u.u6_addr32[0]
                      && iph->daddr.in6_u.u6_addr32[1] == e->rt_info.daddr.in6_u.u6_addr32[1]
                      && iph->daddr.in6_u.u6_addr32[2] == e->rt_info.daddr.in6_u.u6_addr32[2]
                      && iph->daddr.in6_u.u6_addr32[3] == e->rt_info.daddr.in6_u.u6_addr32[3]
		      && iph->saddr.in6_u.u6_addr32[0] == e->rt_info.saddr.in6_u.u6_addr32[0]
		      && iph->saddr.in6_u.u6_addr32[1] == e->rt_info.saddr.in6_u.u6_addr32[1]
		      && iph->saddr.in6_u.u6_addr32[2] == e->rt_info.saddr.in6_u.u6_addr32[2]
		      && iph->saddr.in6_u.u6_addr32[3] == e->rt_info.saddr.in6_u.u6_addr32[3]))
			return route6_me_harder(e->skb);
	}
	return 0;
}

static inline int id_cmp(ip6q_queue_element_t *e, unsigned long id)
{
	return (id == (unsigned long )e);
}

static int ip6q_set_verdict(ip6q_queue_t *q,
                           ipq_verdict_msg_t *v, unsigned int len)
{
	ip6q_queue_element_t *e;

	if (v->value > NF_MAX_VERDICT)
		return -EINVAL;
	e = ip6q_dequeue(q, id_cmp, v->id);
	if (e == NULL)
		return -ENOENT;
	else {
		e->verdict = v->value;
		if (v->data_len && v->data_len == len)
			if (ip6q_mangle_ipv6(v, e) < 0)
				e->verdict = NF_DROP;
		nf_reinject(e->skb, e->info, e->verdict);
		kfree(e);
		return 0;
	}
}

static int ip6q_receive_peer(ip6q_queue_t* q, ipq_peer_msg_t *m,
                            unsigned char type, unsigned int len)
{

	int status = 0;
	int busy;
		
	spin_lock_bh(&q->lock);
	busy = (q->terminate || q->flushing);
	spin_unlock_bh(&q->lock);
	if (busy)
		return -EBUSY;
	if (len < sizeof(ipq_peer_msg_t))
		return -EINVAL;
	switch (type) {
		case IPQM_MODE:
			switch (m->msg.mode.value) {
				case IPQ_COPY_META:
					q->peer.copy_mode = IPQ_COPY_META;
					q->peer.copy_range = 0;
					break;
				case IPQ_COPY_PACKET:
					q->peer.copy_mode = IPQ_COPY_PACKET;
					q->peer.copy_range = m->msg.mode.range;
					if (q->peer.copy_range > 0xFFFF)
						q->peer.copy_range = 0xFFFF;
					break;
				default:
					status = -EINVAL;
			}
			break;
		case IPQM_VERDICT:
			if (m->msg.verdict.value > NF_MAX_VERDICT)
				status = -EINVAL;
			else
				status = ip6q_set_verdict(q,
				                         &m->msg.verdict,
				                         len - sizeof(*m));
			break;
		default:
			 status = -EINVAL;
	}
	return status;
}

static inline int dev_cmp(ip6q_queue_element_t *e, unsigned long ifindex)
{
	if (e->info->indev)
		if (e->info->indev->ifindex == ifindex)
			return 1;
	if (e->info->outdev)
		if (e->info->outdev->ifindex == ifindex)
			return 1;
	return 0;
}

/* Drop any queued packets associated with device ifindex */
static void ip6q_dev_drop(ip6q_queue_t *q, int ifindex)
{
	ip6q_queue_element_t *e;
	
	while ((e = ip6q_dequeue(q, dev_cmp, ifindex))) {
		e->verdict = NF_DROP;
		nf_reinject(e->skb, e->info, e->verdict);
		kfree(e);
	}
}

/****************************************************************************
 *
 * Netfilter interface
 *
 ****************************************************************************/

/*
 * Packets arrive here from netfilter for queuing to userspace.
 * All of them must be fed back via nf_reinject() or Alexey will kill Rusty.
 */
static int netfilter6_receive(struct sk_buff *skb,
                             struct nf_info *info, void *data)
{
	return ip6q_enqueue((ip6q_queue_t *)data, skb, info);
}

/****************************************************************************
 *
 * Netlink interface.
 *
 ****************************************************************************/

static struct sock *nfnl = NULL;
/* This is not a static one, so we should not repeat its name */
ip6q_queue_t *nlq6 = NULL;

static struct sk_buff *netlink_build_message(ip6q_queue_element_t *e, int *errp)
{
	unsigned char *old_tail;
	size_t size = 0;
	size_t data_len = 0;
	struct sk_buff *skb;
	ipq_packet_msg_t *pm;
	struct nlmsghdr *nlh;

	switch (nlq6->peer.copy_mode) {
		size_t copy_range;

		case IPQ_COPY_META:
			size = NLMSG_SPACE(sizeof(*pm));
			data_len = 0;
			break;
		case IPQ_COPY_PACKET:
			copy_range = nlq6->peer.copy_range;
			if (copy_range == 0 || copy_range > e->skb->len)
				data_len = e->skb->len;
			else
				data_len = copy_range;
			size = NLMSG_SPACE(sizeof(*pm) + data_len);
			
			break;
		case IPQ_COPY_NONE:
		default:
			*errp = -EINVAL;
			return NULL;
	}
	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		goto nlmsg_failure;
	old_tail = skb->tail;
	nlh = NLMSG_PUT(skb, 0, 0, IPQM_PACKET, size - sizeof(*nlh));
	pm = NLMSG_DATA(nlh);
	memset(pm, 0, sizeof(*pm));
	pm->packet_id = (unsigned long )e;
	pm->data_len = data_len;
	pm->timestamp_sec = e->skb->stamp.tv_sec;
	pm->timestamp_usec = e->skb->stamp.tv_usec;
	pm->mark = e->skb->nfmark;
	pm->hook = e->info->hook;
	if (e->info->indev) strcpy(pm->indev_name, e->info->indev->name);
	else pm->indev_name[0] = '\0';
	if (e->info->outdev) strcpy(pm->outdev_name, e->info->outdev->name);
	else pm->outdev_name[0] = '\0';
	pm->hw_protocol = e->skb->protocol;
	if (e->info->indev && e->skb->dev) {
		pm->hw_type = e->skb->dev->type;
		if (e->skb->dev->hard_header_parse)
			pm->hw_addrlen =
				e->skb->dev->hard_header_parse(e->skb,
				                               pm->hw_addr);
	}
	if (data_len)
		memcpy(pm->payload, e->skb->data, data_len);
	nlh->nlmsg_len = skb->tail - old_tail;
	NETLINK_CB(skb).dst_groups = 0;
	return skb;
nlmsg_failure:
	if (skb)
		kfree_skb(skb);
	*errp = 0;
	printk(KERN_ERR "ip6_queue: error creating netlink message\n");
	return NULL;
}

static int netlink_send_peer(ip6q_queue_element_t *e)
{
	int status = 0;
	struct sk_buff *skb;

	skb = netlink_build_message(e, &status);
	if (skb == NULL)
		return status;
	return netlink_unicast(nfnl, skb, nlq6->peer.pid, MSG_DONTWAIT);
}

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)

static __inline__ void netlink_receive_user_skb(struct sk_buff *skb)
{
	int status, type;
	struct nlmsghdr *nlh;

	if (skb->len < sizeof(struct nlmsghdr))
		return;

	nlh = (struct nlmsghdr *)skb->data;
	if (nlh->nlmsg_len < sizeof(struct nlmsghdr)
	    || skb->len < nlh->nlmsg_len)
	    	return;

	if(nlh->nlmsg_pid <= 0
	    || !(nlh->nlmsg_flags & NLM_F_REQUEST)
	    || nlh->nlmsg_flags & NLM_F_MULTI)
		RCV_SKB_FAIL(-EINVAL);
	if (nlh->nlmsg_flags & MSG_TRUNC)
		RCV_SKB_FAIL(-ECOMM);
	type = nlh->nlmsg_type;
	if (type < NLMSG_NOOP || type >= IPQM_MAX)
		RCV_SKB_FAIL(-EINVAL);
	if (type <= IPQM_BASE)
		return;
	if(!cap_raised(NETLINK_CB(skb).eff_cap, CAP_NET_ADMIN))
		RCV_SKB_FAIL(-EPERM);
	if (nlq6->peer.pid && !nlq6->peer.died
	    && (nlq6->peer.pid != nlh->nlmsg_pid)) {
	    	printk(KERN_WARNING "ip6_queue: peer pid changed from %d to "
	    	      "%d, flushing queue\n", nlq6->peer.pid, nlh->nlmsg_pid);
		ip6q_flush(nlq6);
	}	
	nlq6->peer.pid = nlh->nlmsg_pid;
	nlq6->peer.died = 0;
	status = ip6q_receive_peer(nlq6, NLMSG_DATA(nlh),
	                          type, skb->len - NLMSG_LENGTH(0));
	if (status < 0)
		RCV_SKB_FAIL(status);
	if (nlh->nlmsg_flags & NLM_F_ACK)
		netlink_ack(skb, nlh, 0);
        return;
}

/* Note: we are only dealing with single part messages at the moment. */
static void netlink_receive_user_sk(struct sock *sk, int len)
{
	do {
		struct sk_buff *skb;

		if (rtnl_shlock_nowait())
			return;
		while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
			netlink_receive_user_skb(skb);
			kfree_skb(skb);
		}
		up(&rtnl_sem);
	} while (nfnl && nfnl->receive_queue.qlen);
}

/****************************************************************************
 *
 * System events
 *
 ****************************************************************************/

static int receive_event(struct notifier_block *this,
                         unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	/* Drop any packets associated with the downed device */
	if (event == NETDEV_DOWN)
		ip6q_dev_drop(nlq6, dev->ifindex);
	return NOTIFY_DONE;
}

struct notifier_block ip6q_dev_notifier = {
	receive_event,
	NULL,
	0
};

/****************************************************************************
 *
 * Sysctl - queue tuning.
 *
 ****************************************************************************/

static int sysctl_maxlen = IPQ_QMAX_DEFAULT;

static struct ctl_table_header *ip6q_sysctl_header;

static ctl_table ip6q_table[] = {
	{ NET_IPQ_QMAX, NET_IPQ_QMAX_NAME, &sysctl_maxlen,
	  sizeof(sysctl_maxlen), 0644,  NULL, proc_dointvec },
 	{ 0 }
};

static ctl_table ip6q_dir_table[] = {
	{NET_IPV6, "ipv6", NULL, 0, 0555, ip6q_table, 0, 0, 0, 0, 0},
	{ 0 }
};

static ctl_table ip6q_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, ip6q_dir_table, 0, 0, 0, 0, 0},
	{ 0 }
};

/****************************************************************************
 *
 * Procfs - debugging info.
 *
 ****************************************************************************/

static int ip6q_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	spin_lock_bh(&nlq6->lock);
	len = sprintf(buffer,
	              "Peer pid            : %d\n"
	              "Peer died           : %d\n"
	              "Peer copy mode      : %d\n"
	              "Peer copy range     : %Zu\n"
	              "Queue length        : %d\n"
	              "Queue max. length   : %d\n"
	              "Queue flushing      : %d\n"
	              "Queue terminate     : %d\n",
	              nlq6->peer.pid,
	              nlq6->peer.died,
	              nlq6->peer.copy_mode,
	              nlq6->peer.copy_range,
	              nlq6->len,
	              *nlq6->maxlen,
	              nlq6->flushing,
	              nlq6->terminate);
	spin_unlock_bh(&nlq6->lock);
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

/****************************************************************************
 *
 * Module stuff.
 *
 ****************************************************************************/

static int __init init(void)
{
	int status = 0;
	struct proc_dir_entry *proc;
	
        /* We must create the NETLINK_IP6_FW protocol service */
	nfnl = netlink_kernel_create(NETLINK_IP6_FW, netlink_receive_user_sk);
	if (nfnl == NULL) {
		printk(KERN_ERR "ip6_queue: initialisation failed: unable to "
		       "create kernel netlink socket\n");
		return -ENOMEM;
	}
	nlq6 = ip6q_create_queue(netfilter6_receive,
	                       netlink_send_peer, &status, &sysctl_maxlen);
	if (nlq6 == NULL) {
		printk(KERN_ERR "ip6_queue: initialisation failed: unable to "
		       "create queue\n");
		sock_release(nfnl->socket);
		return status;
	}
        /* The file will be /proc/net/ip6_queue */
	proc = proc_net_create(IPQ_PROC_FS_NAME, 0, ip6q_get_info);
	if (proc) proc->owner = THIS_MODULE;
	else {
		ip6q_destroy_queue(nlq6);
		sock_release(nfnl->socket);
		return -ENOMEM;
	}
	register_netdevice_notifier(&ip6q_dev_notifier);
	ip6q_sysctl_header = register_sysctl_table(ip6q_root_table, 0);
	return status;
}

static void __exit fini(void)
{
	unregister_sysctl_table(ip6q_sysctl_header);
	proc_net_remove(IPQ_PROC_FS_NAME);
	unregister_netdevice_notifier(&ip6q_dev_notifier);
	ip6q_destroy_queue(nlq6);
	sock_release(nfnl->socket);
}

MODULE_DESCRIPTION("IPv6 packet queue handler");
MODULE_LICENSE("GPL");

module_init(init);
module_exit(fini);
