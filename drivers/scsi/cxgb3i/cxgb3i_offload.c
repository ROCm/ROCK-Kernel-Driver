/*
 * Copyright (C) 2003-2008 Chelsio Communications.  All rights reserved.
 *
 * Written by Dimitris Michailidis (dm@chelsio.com)
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#include <linux/if_vlan.h>
#include <linux/version.h>

#include "cxgb3_defs.h"
#include "cxgb3_ctl_defs.h"
#include "firmware_exports.h"
#include "cxgb3i_offload.h"
#include "cxgb3i_ulp2.h"

static int cxgb3_rcv_win = 256 * 1024;
module_param(cxgb3_rcv_win, int, 0644);
MODULE_PARM_DESC(cxgb3_rcv_win, "TCP receive window in bytes (default=256KB)");

static int cxgb3_snd_win = 64 * 1024;
module_param(cxgb3_snd_win, int, 0644);
MODULE_PARM_DESC(cxgb3_snd_win, "TCP send window in bytes (default=64KB)");

static int cxgb3_rx_credit_thres = 10 * 1024;
module_param(cxgb3_rx_credit_thres, int, 0644);
MODULE_PARM_DESC(rx_credit_thres,
		 "RX credits return threshold in bytes (default=10KB)");

static unsigned int cxgb3_max_connect = 8 * 1024;
module_param(cxgb3_max_connect, uint, 0644);
MODULE_PARM_DESC(cxgb3_max_connect, "Max. # of connections (default=8092)");

static unsigned int cxgb3_sport_base = 20000;
module_param(cxgb3_sport_base, uint, 0644);
MODULE_PARM_DESC(cxgb3_sport_base, "starting port number (default=20000)");

#ifdef __DEBUG_C3CN_TX__
#define c3cn_tx_debug         cxgb3i_log_debug
#else
#define c3cn_tx_debug(fmt...)
#endif

/* connection flags */
static inline void c3cn_set_flag(struct s3_conn *c3cn, enum c3cn_flags flag)
{
	__set_bit(flag, &c3cn->flags);
	c3cn_conn_debug("c3cn 0x%p, set %d, s 0x%x, f 0x%lx.\n",
			c3cn, flag, c3cn->state, c3cn->flags);
}

static inline void c3cn_reset_flag(struct s3_conn *c3cn, enum c3cn_flags flag)
{
	__clear_bit(flag, &c3cn->flags);
	c3cn_conn_debug("c3cn 0x%p, clear %d, s 0x%x, f 0x%lx.\n",
			c3cn, flag, c3cn->state, c3cn->flags);
}

static inline int c3cn_flag(struct s3_conn *c3cn, enum c3cn_flags flag)
{
	if (c3cn == NULL)
		return 0;
	return test_bit(flag, &c3cn->flags);
}

/* connection state */
static void c3cn_set_state(struct s3_conn *c3cn, int state)
{
	c3cn_conn_debug("c3cn 0x%p state -> 0x%x.\n", c3cn, state);
	c3cn->state = state;
}

/* connection reference count */
static inline void c3cn_hold(struct s3_conn *c3cn)
{
	atomic_inc(&c3cn->refcnt);
}

static inline void c3cn_put(struct s3_conn *c3cn)
{
	if (atomic_dec_and_test(&c3cn->refcnt)) {
		c3cn_conn_debug("free c3cn 0x%p, 0x%x, 0x%lx.\n",
				c3cn, c3cn->state, c3cn->flags);
		kfree(c3cn);
	}
}

/* minimal port allocation management scheme */
static spinlock_t sport_map_lock;
static unsigned int sport_map_next;
static unsigned long *sport_map;

/*
 * Find a free source port in our allocation map.  We use a very simple rotor
 * scheme to look for the next free port.
 *
 * If a source port has been specified make sure that it doesn't collide with
 * our normal source port allocation map.  If it's outside the range of our
 * allocation scheme just let them use it.
 */
static int c3cn_get_port(struct s3_conn *c3cn)
{
	unsigned int start;

	if (!sport_map)
		goto error_out;

	if (c3cn->saddr.sin_port != 0) {
		int sport = ntohs(c3cn->saddr.sin_port) - cxgb3_sport_base;
		int err = 0;

		if (sport < 0 || sport >= cxgb3_max_connect)
			return 0;
		spin_lock(&sport_map_lock);
		err = __test_and_set_bit(sport, sport_map);
		spin_unlock(&sport_map_lock);
		return err ? -EADDRINUSE : 0;
	}

	spin_lock(&sport_map_lock);
	start = sport_map_next;
	do {
		unsigned int new = sport_map_next;
		if (++sport_map_next >= cxgb3_max_connect)
			sport_map_next = 0;
		if (!(__test_and_set_bit(new, sport_map))) {
			spin_unlock(&sport_map_lock);
			c3cn_conn_debug("reserve port %u.\n",
					cxgb3_sport_base + new);
			c3cn->saddr.sin_port = htons(cxgb3_sport_base + new);
			return 0;
		}
	} while (sport_map_next != start);
	spin_unlock(&sport_map_lock);

error_out:
	return -EADDRNOTAVAIL;
}

/*
 * Deallocate a source port from the allocation map.  If the source port is
 * outside our allocation range just return -- the caller is responsible for
 * keeping track of their port usage outside of our allocation map.
 */
static void c3cn_put_port(struct s3_conn *c3cn)
{
	if (c3cn->saddr.sin_port) {
		int old = ntohs(c3cn->saddr.sin_port) - cxgb3_sport_base;
		c3cn->saddr.sin_port = 0;

		if (old < 0 || old >= cxgb3_max_connect)
			return;

		c3cn_conn_debug("release port %u.\n", cxgb3_sport_base + old);
		spin_lock(&sport_map_lock);
		__clear_bit(old, sport_map);
		spin_unlock(&sport_map_lock);
	}
}

static void c3cn_reset_timer(struct s3_conn *c3cn, struct timer_list *timer,
		      unsigned long expires)
{
	if (!mod_timer(timer, expires))
		c3cn_hold(c3cn);
}

typedef int (cxgb3_cpl_handler_decl) (struct t3cdev *,
				      struct sk_buff *, void *);

static cxgb3_cpl_handler_decl do_act_establish;
static cxgb3_cpl_handler_decl do_act_open_rpl;
static cxgb3_cpl_handler_decl do_wr_ack;
static cxgb3_cpl_handler_decl do_peer_close;
static cxgb3_cpl_handler_decl do_abort_req;
static cxgb3_cpl_handler_decl do_abort_rpl;
static cxgb3_cpl_handler_decl do_close_con_rpl;
static cxgb3_cpl_handler_decl do_iscsi_hdr;

static LIST_HEAD(cxgb3_list);
static DEFINE_MUTEX(cxgb3_list_lock);

/*
 * For ULP connections HW may inserts digest bytes into the pdu. This array
 * contains the compensating extra lengths for ULP packets.  It is indexed by
 * a packet's ULP submode.
 */
static const unsigned int cxgb3_ulp_extra_len[] = { 0, 4, 4, 8 };

/*
 * Return the length of any HW additions that will be made to a Tx packet.
 * Such additions can happen for some types of ULP packets.
 */
static inline unsigned int ulp_extra_len(const struct sk_buff *skb)
{
	return cxgb3_ulp_extra_len[skb_ulp_mode(skb) & 3];
}

/*
 * Size of WRs in bytes.  Note that we assume all devices we are handling have
 * the same WR size.
 */
static unsigned int wrlen __read_mostly;

/*
 * The number of WRs needed for an skb depends on the number of page fragments
 * in the skb and whether it has any payload in its main body.  This maps the
 * length of the gather list represented by an skb into the # of necessary WRs.
 */
static unsigned int skb_wrs[MAX_SKB_FRAGS + 2] __read_mostly;

static void s3_init_wr_tab(unsigned int wr_len)
{
	int i;

	if (skb_wrs[1])		/* already initialized */
		return;

	for (i = 1; i < ARRAY_SIZE(skb_wrs); i++) {
		int sgl_len = (3 * i) / 2 + (i & 1);

		sgl_len += 3;
		skb_wrs[i] = (sgl_len <= wr_len
			      ? 1 : 1 + (sgl_len - 2) / (wr_len - 1));
	}

	wrlen = wr_len * 8;
}

/*
 * cxgb3i API operations.
 */
/*
 * large memory chunk allocation/release
 */
void *cxgb3i_alloc_big_mem(unsigned int size)
{
	void *p = kmalloc(size, GFP_KERNEL);
	if (!p)
		p = vmalloc(size);
	if (p)
		memset(p, 0, size);
	return p;
}

void cxgb3i_free_big_mem(void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}

void cxgb3i_sdev_cleanup(void)
{
	if (sport_map)
		cxgb3i_free_big_mem(sport_map);
}

int cxgb3i_sdev_init(cxgb3_cpl_handler_func *cpl_handlers)
{
	cpl_handlers[CPL_ACT_ESTABLISH] = do_act_establish;
	cpl_handlers[CPL_ACT_OPEN_RPL] = do_act_open_rpl;
	cpl_handlers[CPL_PEER_CLOSE] = do_peer_close;
	cpl_handlers[CPL_ABORT_REQ_RSS] = do_abort_req;
	cpl_handlers[CPL_ABORT_RPL_RSS] = do_abort_rpl;
	cpl_handlers[CPL_CLOSE_CON_RPL] = do_close_con_rpl;
	cpl_handlers[CPL_TX_DMA_ACK] = do_wr_ack;
	cpl_handlers[CPL_ISCSI_HDR] = do_iscsi_hdr;

	if (cxgb3_max_connect > CXGB3I_MAX_CONN)
		cxgb3_max_connect = CXGB3I_MAX_CONN;
	sport_map = cxgb3i_alloc_big_mem(DIV_ROUND_UP(cxgb3_max_connect,
						      8 *
						      sizeof(unsigned long)));
	if (!sport_map)
		return -ENOMEM;
	return 0;
}

void cxgb3i_sdev_add(struct t3cdev *cdev, struct cxgb3_client *client)
{
	struct cxgb3i_sdev_data *cdata;
	struct adap_ports *ports;
	struct ofld_page_info rx_page_info;
	unsigned int wr_len;
	int i;

	cdata = kzalloc(sizeof *cdata, GFP_KERNEL);
	if (!cdata)
		return;
	ports = kzalloc(sizeof *ports, GFP_KERNEL);
	if (!ports)
		goto free_ports;
	cdata->ports = ports;

	if (cdev->ctl(cdev, GET_WR_LEN, &wr_len) < 0 ||
	    cdev->ctl(cdev, GET_PORTS, cdata->ports) < 0 ||
	    cdev->ctl(cdev, GET_RX_PAGE_INFO, &rx_page_info) < 0)
		goto free_ports;

	s3_init_wr_tab(wr_len);

	INIT_LIST_HEAD(&cdata->list);
	cdata->cdev = cdev;
	cdata->client = client;
	cdata->rx_page_size = rx_page_info.page_size;
	skb_queue_head_init(&cdata->deferq);

	for (i = 0; i < ports->nports; i++)
		NDEV2CDATA(ports->lldevs[i]) = cdata;

	mutex_lock(&cxgb3_list_lock);
	list_add_tail(&cdata->list, &cxgb3_list);
	mutex_unlock(&cxgb3_list_lock);

	return;

free_ports:
	kfree(ports);
	kfree(cdata);
}

void cxgb3i_sdev_remove(struct t3cdev *cdev)
{
	struct cxgb3i_sdev_data *cdata = CXGB3_SDEV_DATA(cdev);
	struct adap_ports *ports = cdata->ports;
	int i;

	for (i = 0; i < ports->nports; i++)
		NDEV2CDATA(ports->lldevs[i]) = NULL;

	mutex_lock(&cxgb3_list_lock);
	list_del(&cdata->list);
	mutex_unlock(&cxgb3_list_lock);

	kfree(ports);
	kfree(cdata);
}

/*
 * Return TRUE if the specified net device is for a port on one of our
 * registered adapters.
 */
static int is_cxgb3_dev(struct net_device *dev)
{
	struct cxgb3i_sdev_data *cdata;

	mutex_lock(&cxgb3_list_lock);
	list_for_each_entry(cdata, &cxgb3_list, list) {
		struct adap_ports *ports = cdata->ports;
		int i;

		for (i = 0; i < ports->nports; i++)
			if (dev == ports->lldevs[i]) {
				mutex_unlock(&cxgb3_list_lock);
				return 1;
			}
	}
	mutex_unlock(&cxgb3_list_lock);
	return 0;
}

/*
 * Primary cxgb3 API operations.
 * =============================
 */

static int s3_push_frames(struct s3_conn *, int);
static int s3_send_reset(struct s3_conn *, int, struct sk_buff *);
static void t3_release_offload_resources(struct s3_conn *);
static void mk_close_req(struct s3_conn *);

struct s3_conn *cxgb3i_c3cn_create(void)
{
	struct s3_conn *c3cn;

	c3cn = kzalloc(sizeof(*c3cn), GFP_KERNEL);
	if (c3cn == NULL)
		return NULL;

	c3cn_conn_debug("alloc c3cn 0x%p.\n", c3cn);

	c3cn->flags = 0;
	spin_lock_init(&c3cn->lock);
	atomic_set(&c3cn->refcnt, 1);
	skb_queue_head_init(&c3cn->receive_queue);
	skb_queue_head_init(&c3cn->write_queue);
	setup_timer(&c3cn->retry_timer, NULL, (unsigned long)c3cn);
	rwlock_init(&c3cn->callback_lock);

	return c3cn;
}

static inline void s3_purge_write_queue(struct s3_conn *c3cn)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&c3cn->write_queue)))
		__kfree_skb(skb);
}

static void c3cn_done(struct s3_conn *c3cn)
{
	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			 c3cn, c3cn->state, c3cn->flags);

	c3cn_put_port(c3cn);
	t3_release_offload_resources(c3cn);
	c3cn_set_state(c3cn, C3CN_STATE_CLOSE);
	c3cn->shutdown = C3CN_SHUTDOWN_MASK;
	cxgb3i_conn_closing(c3cn);
}

static void c3cn_close(struct s3_conn *c3cn)
{
	int data_lost, old_state;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			 c3cn, c3cn->state, c3cn->flags);

	dst_confirm(c3cn->dst_cache);

	spin_lock_bh(&c3cn->lock);
	c3cn->shutdown |= C3CN_SHUTDOWN_MASK;

	/*
	 * We need to flush the receive buffs.  We do this only on the
	 * descriptor close, not protocol-sourced closes, because the
	 * reader process may not have drained the data yet!  Make a note
	 * of whether any received data will be lost so we can decide whether
	 * to FIN or RST.
	 */
	data_lost = skb_queue_len(&c3cn->receive_queue);
	__skb_queue_purge(&c3cn->receive_queue);

	if (c3cn->state == C3CN_STATE_CLOSE)
		/* Nothing if we are already closed */
		c3cn_conn_debug("c3cn 0x%p, 0x%x, already closed.\n",
				c3cn, c3cn->state);
	else if (data_lost || c3cn->state == C3CN_STATE_SYN_SENT) {
		c3cn_conn_debug("c3cn 0x%p, 0x%x -> closing, send reset.\n",
				c3cn, c3cn->state);
		/* Unread data was tossed, zap the connection. */
		s3_send_reset(c3cn, CPL_ABORT_SEND_RST, NULL);
		goto unlock;
	} else if (c3cn->state == C3CN_STATE_ESTABLISHED) {
		c3cn_conn_debug("c3cn 0x%p, est. -> closing, send close_req.\n",
				c3cn);
		c3cn_set_state(c3cn, C3CN_STATE_CLOSING);
		mk_close_req(c3cn);
	}

unlock:
	old_state = c3cn->state;
	c3cn_hold(c3cn); /* must last past the potential destroy() */

	spin_unlock_bh(&c3cn->lock);

	/*
	 * There are no more user references at this point.  Grab the
	 * connection lock and finish the close.
	 */
	local_bh_disable();
	spin_lock(&c3cn->lock);

	/*
	 * Because the connection was orphaned before the spin_lock()
	 * either the backlog or a BH may have already destroyed it.
	 * Bail out if so.
	 */
	if (old_state != C3CN_STATE_CLOSE && c3cn->state == C3CN_STATE_CLOSE)
		goto out;

	if (c3cn->state == C3CN_STATE_CLOSE)
		s3_purge_write_queue(c3cn);

out:
	spin_unlock(&c3cn->lock);
	local_bh_enable();
	c3cn_put(c3cn);
}

void cxgb3i_c3cn_release(struct s3_conn *c3cn)
{
	c3cn_conn_debug("c3cn 0x%p, s 0x%x, f 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	if (likely(c3cn->state != C3CN_STATE_SYN_SENT))
		c3cn_close(c3cn);
	else
		c3cn_set_flag(c3cn, C3CN_CLOSE_NEEDED);
	c3cn_put(c3cn);
}


/*
 * Local utility routines used to implement primary cxgb3 API operations.
 * ======================================================================
 */

static u32 s3_send_rx_credits(struct s3_conn *, u32, u32, int);
static int act_open(struct s3_conn *, struct net_device *);
static void mk_act_open_req(struct s3_conn *, struct sk_buff *,
			    unsigned int, const struct l2t_entry *);
static void skb_entail(struct s3_conn *, struct sk_buff *, int);

static inline void reset_wr_list(struct s3_conn *c3cn)
{
	c3cn->wr_pending_head = NULL;
}

/*
 * Add a WR to a connections's list of pending WRs.  This is a singly-linked
 * list of sk_buffs operating as a FIFO.  The head is kept in wr_pending_head
 * and the tail in wr_pending_tail.
 */
static inline void enqueue_wr(struct s3_conn *c3cn,
			      struct sk_buff *skb)
{
	skb->sp = NULL;

	/*
	 * We want to take an extra reference since both us and the driver
	 * need to free the packet before it's really freed.  We know there's
	 * just one user currently so we use atomic_set rather than skb_get
	 * to avoid the atomic op.
	 */
	atomic_set(&skb->users, 2);

	if (!c3cn->wr_pending_head)
		c3cn->wr_pending_head = skb;
	else
		c3cn->wr_pending_tail->sp = (void *)skb;
	c3cn->wr_pending_tail = skb;
}

/*
 * The next two functions calculate the option 0 value for a connection.
 */
static inline int compute_wscale(int win)
{
	int wscale = 0;
	while (wscale < 14 && (65535<<wscale) < win)
		wscale++;
	return wscale;
}

static inline unsigned int calc_opt0h(struct s3_conn *c3cn)
{
	int wscale = compute_wscale(cxgb3_rcv_win);
	return  V_KEEP_ALIVE(1) |
		F_TCAM_BYPASS |
		V_WND_SCALE(wscale) |
		V_MSS_IDX(c3cn->mss_idx);
}

static inline unsigned int calc_opt0l(struct s3_conn *c3cn)
{
	return  V_ULP_MODE(ULP_MODE_ISCSI) |
		V_RCV_BUFSIZ(cxgb3_rcv_win>>10);
}

static inline void make_tx_data_wr(struct s3_conn *c3cn,
				   struct sk_buff *skb, int len)
{
	struct tx_data_wr *req;

	skb_reset_transport_header(skb);
	req = (struct tx_data_wr *)__skb_push(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_TX_DATA));
	req->wr_lo = htonl(V_WR_TID(c3cn->tid));
	req->sndseq = htonl(c3cn->snd_nxt);
	/* len includes the length of any HW ULP additions */
	req->len = htonl(len);
	req->param = htonl(V_TX_PORT(c3cn->l2t->smt_idx));
	/* V_TX_ULP_SUBMODE sets both the mode and submode */
	req->flags = htonl(V_TX_ULP_SUBMODE(skb_ulp_mode(skb)) |
			   V_TX_SHOVE((skb_peek(&c3cn->write_queue) ? 0 : 1)));

	if (!c3cn_flag(c3cn, C3CN_TX_DATA_SENT)) {

		req->flags |= htonl(V_TX_ACK_PAGES(2) | F_TX_INIT |
				    V_TX_CPU_IDX(c3cn->qset));

		/* Sendbuffer is in units of 32KB.
		 */
		req->param |= htonl(V_TX_SNDBUF(cxgb3_snd_win >> 15));
		c3cn_set_flag(c3cn, C3CN_TX_DATA_SENT);
	}
}

/**
 * cxgb3_egress_dev - return the cxgb3 egress device
 * @root_dev: the root device anchoring the search
 * @c3cn: the connection used to determine egress port in bonding mode
 * @context: in bonding mode, indicates a connection set up or failover
 *
 * Return egress device or NULL if the egress device isn't one of our ports.
 *
 * Given a root network device it returns the physical egress device that is a
 * descendant of the root device.  The root device may be either a physical
 * device, in which case it is the device returned, or a virtual device, such
 * as a VLAN or bonding device.  In case of a bonding device the search
 * considers the decisions of the bonding device given its mode to locate the
 * correct egress device.
 */
static struct net_device *cxgb3_egress_dev(struct net_device *root_dev,
					   struct s3_conn *c3cn,
					   int context)
{
	while (root_dev) {
		if (root_dev->priv_flags & IFF_802_1Q_VLAN)
			root_dev = vlan_dev_real_dev(root_dev);
		else if (is_cxgb3_dev(root_dev))
			return root_dev;
		else
			return NULL;
	}
	return NULL;
}

static struct rtable *find_route(__be32 saddr, __be32 daddr,
				 __be16 sport, __be16 dport)
{
	struct rtable *rt;
	struct flowi fl = {
		.oif = 0,
		.nl_u = {
			 .ip4_u = {
				   .daddr = daddr,
				   .saddr = saddr,
				   .tos = 0 } },
		.proto = IPPROTO_TCP,
		.uli_u = {
			  .ports = {
				    .sport = sport,
				    .dport = dport } } };

	if (ip_route_output_flow(&init_net, &rt, &fl, NULL, 0))
		return NULL;
	return rt;
}

int cxgb3i_c3cn_connect(struct s3_conn *c3cn, struct sockaddr_in *usin)
{
	struct rtable *rt;
	struct net_device *dev;
	struct cxgb3i_sdev_data *cdata;
	struct t3cdev *cdev;
	__be32 sipv4;
	int err;

	if (usin->sin_family != AF_INET)
		return -EAFNOSUPPORT;

	/* get a source port if one hasn't been provided */
	err = c3cn_get_port(c3cn);
	if (err)
		return err;

	c3cn_conn_debug("c3cn 0x%p get port %u.\n",
			c3cn, ntohs(c3cn->saddr.sin_port));

	c3cn->daddr.sin_port = usin->sin_port;
	c3cn->daddr.sin_addr.s_addr = usin->sin_addr.s_addr;

	rt = find_route(c3cn->saddr.sin_addr.s_addr,
			c3cn->daddr.sin_addr.s_addr,
			c3cn->saddr.sin_port,
			c3cn->daddr.sin_port);
	if (rt == NULL) {
		c3cn_conn_debug("NO route to 0x%x, port %u.\n",
				c3cn->daddr.sin_addr.s_addr,
				ntohs(c3cn->daddr.sin_port));
		return -ENETUNREACH;
	}

	if (rt->rt_flags & (RTCF_MULTICAST | RTCF_BROADCAST)) {
		c3cn_conn_debug("multi-cast route to 0x%x, port %u.\n",
				c3cn->daddr.sin_addr.s_addr,
				ntohs(c3cn->daddr.sin_port));
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	if (!c3cn->saddr.sin_addr.s_addr)
		c3cn->saddr.sin_addr.s_addr = rt->rt_src;

	/* now commit destination to connection */
	c3cn->dst_cache = &rt->u.dst;

	/* try to establish an offloaded connection */
	dev = cxgb3_egress_dev(c3cn->dst_cache->dev, c3cn, 0);
	if (dev == NULL) {
		c3cn_conn_debug("c3cn 0x%p, egress dev NULL.\n", c3cn);
		return -ENETUNREACH;
	}
	cdata = NDEV2CDATA(dev);
	cdev = cdata->cdev;

	sipv4 = cxgb3i_get_private_ipv4addr(dev);
	if (!sipv4) {
		c3cn_conn_debug("c3cn 0x%p, iscsi ip not configured.\n", c3cn);
		sipv4 = c3cn->saddr.sin_addr.s_addr;
		cxgb3i_set_private_ipv4addr(dev, sipv4);
	} else
		c3cn->saddr.sin_addr.s_addr = sipv4;

	c3cn_conn_debug("c3cn 0x%p, %u.%u.%u.%u,%u-%u.%u.%u.%u,%u SYN_SENT.\n",
			c3cn, NIPQUAD(c3cn->saddr.sin_addr.s_addr),
			ntohs(c3cn->saddr.sin_port),
			NIPQUAD(c3cn->daddr.sin_addr.s_addr),
			ntohs(c3cn->daddr.sin_port));

	c3cn_set_state(c3cn, C3CN_STATE_SYN_SENT);

	if (!act_open(c3cn, dev))
		return 0;

	/*
	 * If we get here, we don't have an offload connection so simply
	 * return a failure.
	 */
	err = -ENOTSUPP;

	/*
	 * This trashes the connection and releases the local port,
	 * if necessary.
	 */
	c3cn_conn_debug("c3cn 0x%p -> CLOSE.\n", c3cn);
	c3cn_set_state(c3cn, C3CN_STATE_CLOSE);
	ip_rt_put(rt);
	c3cn_put_port(c3cn);
	c3cn->daddr.sin_port = 0;
	return err;
}

/*
 * Set of states for which we should return RX credits.
 */
#define CREDIT_RETURN_STATE (C3CN_STATE_ESTABLISHED)

/*
 * Called after some received data has been read.  It returns RX credits
 * to the HW for the amount of data processed.
 */
void cxgb3i_c3cn_rx_credits(struct s3_conn *c3cn, int copied)
{
	struct t3cdev *cdev;
	int must_send;
	u32 credits, dack = 0;

	if (!c3cn_in_state(c3cn, CREDIT_RETURN_STATE))
		return;

	credits = c3cn->copied_seq - c3cn->rcv_wup;
	if (unlikely(!credits))
		return;

	cdev = c3cn->cdev;

	if (unlikely(cxgb3_rx_credit_thres == 0))
		return;

	dack = F_RX_DACK_CHANGE | V_RX_DACK_MODE(1);

	/*
	 * For coalescing to work effectively ensure the receive window has
	 * at least 16KB left.
	 */
	must_send = credits + 16384 >= cxgb3_rcv_win;

	if (must_send || credits >= cxgb3_rx_credit_thres)
		c3cn->rcv_wup += s3_send_rx_credits(c3cn, credits, dack,
						    must_send);
}

/*
 * Generic ARP failure handler that discards the buffer.
 */
static void arp_failure_discard(struct t3cdev *cdev, struct sk_buff *skb)
{
	kfree_skb(skb);
}

/*
 * Prepends TX_DATA_WR or CPL_CLOSE_CON_REQ headers to buffers waiting in a
 * connection's send queue and sends them on to T3.  Must be called with the
 * connection's lock held.  Returns the amount of send buffer space that was
 * freed as a result of sending queued data to T3.
 */
static int s3_push_frames(struct s3_conn *c3cn, int req_completion)
{
	int total_size = 0;
	struct sk_buff *skb;
	struct t3cdev *cdev;
	struct cxgb3i_sdev_data *cdata;

	if (unlikely(c3cn_in_state(c3cn,
				   C3CN_STATE_SYN_SENT | C3CN_STATE_CLOSE)))
		return 0;

	/*
	 * We shouldn't really be called at all after an abort but check just
	 * in case.
	 */
	if (unlikely(c3cn_flag(c3cn, C3CN_ABORT_SHUTDOWN)))
		return 0;

	cdev = c3cn->cdev;
	cdata = CXGB3_SDEV_DATA(cdev);

	while (c3cn->wr_avail
	       && (skb = skb_peek(&c3cn->write_queue)) != NULL
	       && !c3cn_flag(c3cn, C3CN_TX_WAIT_IDLE)) {

		int len = skb->len;	/* length before skb_push */
		int frags = skb_shinfo(skb)->nr_frags + (len != skb->data_len);
		int wrs_needed = skb_wrs[frags];

		if (wrs_needed > 1 && len + sizeof(struct tx_data_wr) <= wrlen)
			wrs_needed = 1;

		WARN_ON(frags >= ARRAY_SIZE(skb_wrs) || wrs_needed < 1);

		if (c3cn->wr_avail < wrs_needed)
			break;

		__skb_unlink(skb, &c3cn->write_queue);
		skb->priority = CPL_PRIORITY_DATA;
		skb->csum = wrs_needed;	/* remember this until the WR_ACK */
		c3cn->wr_avail -= wrs_needed;
		c3cn->wr_unacked += wrs_needed;
		enqueue_wr(c3cn, skb);

		if (likely(CXGB3_SKB_CB(skb)->flags & C3CB_FLAG_NEED_HDR)) {
			len += ulp_extra_len(skb);
			make_tx_data_wr(c3cn, skb, len);
			c3cn->snd_nxt += len;
			if ((req_completion
			     && c3cn->wr_unacked == wrs_needed)
			    || (CXGB3_SKB_CB(skb)->flags & C3CB_FLAG_COMPL)
			    || c3cn->wr_unacked >= c3cn->wr_max / 2) {
				struct work_request_hdr *wr = cplhdr(skb);

				wr->wr_hi |= htonl(F_WR_COMPL);
				c3cn->wr_unacked = 0;
			}
			CXGB3_SKB_CB(skb)->flags &= ~C3CB_FLAG_NEED_HDR;
		} else if (skb->data[0] == FW_WROPCODE_OFLD_CLOSE_CON)
			c3cn_set_flag(c3cn, C3CN_CLOSE_CON_REQUESTED);

		total_size += skb->truesize;
		set_arp_failure_handler(skb, arp_failure_discard);
		l2t_send(cdev, skb, c3cn->l2t);
	}
	return total_size;
}

/*
 * Handle an ARP failure for a CPL_ABORT_REQ.  Change it into a no RST variant
 * and send it along.
 */
static void abort_arp_failure(struct t3cdev *cdev, struct sk_buff *skb)
{
	struct cpl_abort_req *req = cplhdr(skb);

	c3cn_conn_debug("tdev 0x%p.\n", cdev);

	req->cmd = CPL_ABORT_NO_RST;
	cxgb3_ofld_send(cdev, skb);
}

/*
 * Send an ABORT_REQ message.  Cannot fail.  This routine makes sure we do
 * not send multiple ABORT_REQs for the same connection and also that we do
 * not try to send a message after the connection has closed.  Returns 1 if
 * an ABORT_REQ wasn't generated after all, 0 otherwise.
 */
static int s3_send_reset(struct s3_conn *c3cn, int mode,
			 struct sk_buff *skb)
{
	struct cpl_abort_req *req;
	unsigned int tid = c3cn->tid;

	if (unlikely(c3cn_flag(c3cn, C3CN_ABORT_SHUTDOWN) || !c3cn->cdev)) {
		if (skb)
			__kfree_skb(skb);
		return 1;
	}

	c3cn_conn_debug("c3cn 0x%p, mode %d, flag ABORT_RPL + ABORT_SHUT.\n",
			c3cn, mode);

	c3cn_set_flag(c3cn, C3CN_ABORT_RPL_PENDING);
	c3cn_set_flag(c3cn, C3CN_ABORT_SHUTDOWN);

	/* Purge the send queue so we don't send anything after an abort. */
	s3_purge_write_queue(c3cn);

	if (!skb)
		skb = alloc_skb(sizeof(*req), GFP_KERNEL | __GFP_NOFAIL);
	skb->priority = CPL_PRIORITY_DATA;
	set_arp_failure_handler(skb, abort_arp_failure);

	req = (struct cpl_abort_req *)skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_REQ));
	req->wr.wr_lo = htonl(V_WR_TID(tid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ABORT_REQ, tid));
	req->rsvd0 = htonl(c3cn->snd_nxt);
	req->rsvd1 = !c3cn_flag(c3cn, C3CN_TX_DATA_SENT);
	req->cmd = mode;

	l2t_send(c3cn->cdev, skb, c3cn->l2t);
	return 0;
}

/*
 * Add a list of skbs to a connection send queue.  This interface is intended
 * for use by in-kernel ULPs.  The skbs must comply with the max size limit of
 * the device and have a headroom of at least TX_HEADER_LEN bytes.
 */
int cxgb3i_c3cn_send_pdus(struct s3_conn *c3cn, struct sk_buff *skb, int flags)
{
	struct sk_buff *next;
	int err, copied = 0;

	spin_lock_bh(&c3cn->lock);

	if (!c3cn_in_state(c3cn, C3CN_STATE_ESTABLISHED)) {
		err = -EAGAIN;
		goto out_err;
	}

	err = -EPIPE;
	if (c3cn->err || (c3cn->shutdown & C3CN_SEND_SHUTDOWN))
		goto out_err;

	while (skb) {
		if (unlikely(skb_headroom(skb) < TX_HEADER_LEN)) {
			c3cn_tx_debug("c3cn 0x%p, skb head.\n", c3cn);
			err = -EINVAL;
			goto out_err;
		}

		next = skb->next;
		skb->next = NULL;
		skb_entail(c3cn, skb, C3CB_FLAG_NO_APPEND | C3CB_FLAG_NEED_HDR);
		copied += skb->len;
		c3cn->write_seq += skb->len + ulp_extra_len(skb);
		skb = next;
	}
done:
	if (likely(skb_queue_len(&c3cn->write_queue)))
		s3_push_frames(c3cn, 1);
	spin_unlock_bh(&c3cn->lock);
	return copied;

out_err:
	if (copied == 0 && err == -EPIPE)
		copied = c3cn->err ? c3cn->err : -EPIPE;
	goto done;
}

/*
 * Low-level utility routines for primary API functions.
 * =====================================================
 */
/* routines to implement CPL message processing */
static void c3cn_act_establish(struct s3_conn *, struct sk_buff *);
static void active_open_failed(struct s3_conn *, struct sk_buff *);
static void wr_ack(struct s3_conn *, struct sk_buff *);
static void do_peer_fin(struct s3_conn *, struct sk_buff *);
static void process_abort_req(struct s3_conn *, struct sk_buff *);
static void process_abort_rpl(struct s3_conn *, struct sk_buff *);
static void process_close_con_rpl(struct s3_conn *, struct sk_buff *);
static void process_rx_iscsi_hdr(struct s3_conn *, struct sk_buff *);

static struct sk_buff *__get_cpl_reply_skb(struct sk_buff *, size_t, gfp_t);

static void fail_act_open(struct s3_conn *, int);
static void init_offload_conn(struct s3_conn *, struct t3cdev *,
			      struct dst_entry *);

/*
 * Insert a connection into the TID table and take an extra reference.
 */
static inline void c3cn_insert_tid(struct cxgb3i_sdev_data *cdata,
				   struct s3_conn *c3cn,
				   unsigned int tid)
{
	c3cn_hold(c3cn);
	cxgb3_insert_tid(cdata->cdev, cdata->client, c3cn, tid);
}

static inline void free_atid(struct t3cdev *cdev, unsigned int tid)
{
	struct s3_conn *c3cn = cxgb3_free_atid(cdev, tid);
	if (c3cn)
		c3cn_put(c3cn);
}

/*
 * This function is intended for allocations of small control messages.
 * Such messages go as immediate data and usually the pakets are freed
 * immediately.  We maintain a cache of one small sk_buff and use it whenever
 * it is available (has a user count of 1).  Otherwise we get a fresh buffer.
 */
#define CTRL_SKB_LEN 120

static struct sk_buff *alloc_ctrl_skb(const struct s3_conn *c3cn,
				      int len)
{
	struct sk_buff *skb = c3cn->ctrl_skb_cache;

	if (likely(skb && !skb_shared(skb) && !skb_cloned(skb))) {
		__skb_trim(skb, 0);
		atomic_set(&skb->users, 2);
	} else if (likely(!in_atomic()))
		skb = alloc_skb(len, GFP_ATOMIC | __GFP_NOFAIL);
	else
		skb = alloc_skb(len, GFP_ATOMIC);
	return skb;
}

/*
 * Handle an ARP failure for an active open.
 */
static void act_open_req_arp_failure(struct t3cdev *dev, struct sk_buff *skb)
{
	struct s3_conn *c3cn = (struct s3_conn *)skb->sk;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x.\n", c3cn, c3cn->state);

	c3cn_hold(c3cn);
	spin_lock(&c3cn->lock);
	if (c3cn->state == C3CN_STATE_SYN_SENT) {
		fail_act_open(c3cn, EHOSTUNREACH);
		__kfree_skb(skb);
	}
	spin_unlock(&c3cn->lock);
	c3cn_put(c3cn);
}

/*
 * Send an active open request.
 */
static int act_open(struct s3_conn *c3cn, struct net_device *dev)
{
	struct cxgb3i_sdev_data *cdata = NDEV2CDATA(dev);
	struct t3cdev *cdev = cdata->cdev;
	struct dst_entry *dst = c3cn->dst_cache;
	struct sk_buff *skb;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	/*
	 * Initialize connection data.  Note that the flags and ULP mode are
	 * initialized higher up ...
	 */
	c3cn->dev = dev;
	c3cn->cdev = cdev;
	c3cn->tid = cxgb3_alloc_atid(cdev, cdata->client, c3cn);
	if (c3cn->tid < 0)
		goto out_err;

	c3cn->qset = 0;
	c3cn->l2t = t3_l2t_get(cdev, dst->neighbour, dev);
	if (!c3cn->l2t)
		goto free_tid;

	skb = alloc_skb(sizeof(struct cpl_act_open_req), GFP_KERNEL);
	if (!skb)
		goto free_l2t;

	skb->sk = (struct sock *)c3cn;
	set_arp_failure_handler(skb, act_open_req_arp_failure);

	c3cn_hold(c3cn);

	init_offload_conn(c3cn, cdev, dst);
	c3cn->err = 0;
	c3cn_reset_flag(c3cn, C3CN_DONE);

	mk_act_open_req(c3cn, skb, c3cn->tid, c3cn->l2t);
	l2t_send(cdev, skb, c3cn->l2t);
	return 0;

free_l2t:
	l2t_release(L2DATA(cdev), c3cn->l2t);
free_tid:
	free_atid(cdev, c3cn->tid);
	c3cn->tid = 0;
out_err:
	return -1;
}

/*
 * Close a connection by sending a CPL_CLOSE_CON_REQ message.  Cannot fail
 * under any circumstances.  We take the easy way out and always queue the
 * message to the write_queue.  We can optimize the case where the queue is
 * already empty though the optimization is probably not worth it.
 */
static void mk_close_req(struct s3_conn *c3cn)
{
	struct sk_buff *skb;
	struct cpl_close_con_req *req;
	unsigned int tid = c3cn->tid;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	skb = alloc_skb(sizeof(struct cpl_close_con_req),
			GFP_KERNEL | __GFP_NOFAIL);
	req = (struct cpl_close_con_req *)__skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_CLOSE_CON));
	req->wr.wr_lo = htonl(V_WR_TID(tid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, tid));
	req->rsvd = htonl(c3cn->write_seq);

	skb_entail(c3cn, skb, C3CB_FLAG_NO_APPEND);
	if (c3cn->state != C3CN_STATE_SYN_SENT)
		s3_push_frames(c3cn, 1);
}

static void skb_entail(struct s3_conn *c3cn, struct sk_buff *skb,
		       int flags)
{
	CXGB3_SKB_CB(skb)->seq = c3cn->write_seq;
	CXGB3_SKB_CB(skb)->flags = flags;
	__skb_queue_tail(&c3cn->write_queue, skb);
}

/*
 * Send RX credits through an RX_DATA_ACK CPL message.  If nofail is 0 we are
 * permitted to return without sending the message in case we cannot allocate
 * an sk_buff.  Returns the number of credits sent.
 */
static u32 s3_send_rx_credits(struct s3_conn *c3cn, u32 credits, u32 dack,
			      int nofail)
{
	struct sk_buff *skb;
	struct cpl_rx_data_ack *req;

	skb = (nofail ? alloc_ctrl_skb(c3cn, sizeof(*req))
	       : alloc_skb(sizeof(*req), GFP_ATOMIC));
	if (!skb)
		return 0;

	req = (struct cpl_rx_data_ack *)__skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_RX_DATA_ACK, c3cn->tid));
	req->credit_dack = htonl(dack | V_RX_CREDITS(credits));
	skb->priority = CPL_PRIORITY_ACK;
	cxgb3_ofld_send(c3cn->cdev, skb);
	return credits;
}

static void mk_act_open_req(struct s3_conn *c3cn, struct sk_buff *skb,
			    unsigned int atid, const struct l2t_entry *e)
{
	struct cpl_act_open_req *req;

	c3cn_conn_debug("c3cn 0x%p, atid 0x%x.\n", c3cn, atid);

	skb->priority = CPL_PRIORITY_SETUP;
	req = (struct cpl_act_open_req *)__skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ, atid));
	req->local_port = c3cn->saddr.sin_port;
	req->peer_port = c3cn->daddr.sin_port;
	req->local_ip = c3cn->saddr.sin_addr.s_addr;
	req->peer_ip = c3cn->daddr.sin_addr.s_addr;
	req->opt0h = htonl(calc_opt0h(c3cn) | V_L2T_IDX(e->idx) |
			   V_TX_CHANNEL(e->smt_idx));
	req->opt0l = htonl(calc_opt0l(c3cn));
	req->params = 0;
}

/*
 * Definitions and declarations for CPL handler functions.
 * =======================================================
 */

/*
 * Similar to process_cpl_msg() but takes an extra connection reference around
 * the call to the handler.  Should be used if the handler may drop a
 * connection reference.
 */
static inline void process_cpl_msg_ref(void (*fn) (struct s3_conn *,
						   struct sk_buff *),
				       struct s3_conn *c3cn,
				       struct sk_buff *skb)
{
	c3cn_hold(c3cn);
	process_cpl_msg(fn, c3cn, skb);
	c3cn_put(c3cn);
}

/*
 * Return whether a failed active open has allocated a TID
 */
static inline int act_open_has_tid(int status)
{
	return status != CPL_ERR_TCAM_FULL && status != CPL_ERR_CONN_EXIST &&
	    status != CPL_ERR_ARP_MISS;
}

/*
 * Returns true if a connection cannot accept new Rx data.
 */
static inline int c3cn_no_receive(const struct s3_conn *c3cn)
{
	return c3cn->shutdown & C3CN_RCV_SHUTDOWN;
}

/*
 * A helper function that aborts a connection and increments the given MIB
 * counter.  The supplied skb is used to generate the ABORT_REQ message if
 * possible.  Must be called with softirqs disabled.
 */
static inline void abort_conn(struct s3_conn *c3cn,
			      struct sk_buff *skb)
{
	struct sk_buff *abort_skb;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	abort_skb = __get_cpl_reply_skb(skb, sizeof(struct cpl_abort_req),
					GFP_ATOMIC);
	if (abort_skb)
		s3_send_reset(c3cn, CPL_ABORT_SEND_RST, abort_skb);
}

/*
 * Returns whether an ABORT_REQ_RSS message is a negative advice.
 */
static inline int is_neg_adv_abort(unsigned int status)
{
	return  status == CPL_ERR_RTX_NEG_ADVICE ||
		status == CPL_ERR_PERSIST_NEG_ADVICE;
}

/*
 * CPL handler functions.
 * ======================
 */

/*
 * Process a CPL_ACT_ESTABLISH message.
 */
static int do_act_establish(struct t3cdev *cdev, struct sk_buff *skb,
			    void *ctx)
{
	struct cpl_act_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);
	unsigned int atid = G_PASS_OPEN_TID(ntohl(req->tos_tid));
	struct s3_conn *c3cn = ctx;
	struct cxgb3i_sdev_data *cdata = CXGB3_SDEV_DATA(cdev);

	c3cn_conn_debug("rcv, tid 0x%x, c3cn 0x%p, 0x%x, 0x%lx.\n",
			tid, c3cn, c3cn->state, c3cn->flags);
	/*
	 * It's OK if the TID is currently in use, the owning connection may
	 * have backlogged its last CPL message(s).  Just take it away.
	 */
	c3cn->tid = tid;
	c3cn_insert_tid(cdata, c3cn, tid);
	free_atid(cdev, atid);

	c3cn->qset = G_QNUM(ntohl(skb->csum));

	process_cpl_msg(c3cn_act_establish, c3cn, skb);
	return 0;
}

/*
 * Process an ACT_OPEN_RPL CPL message.
 */
static int do_act_open_rpl(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;
	struct cpl_act_open_rpl *rpl = cplhdr(skb);

	c3cn_conn_debug("rcv, status 0x%x, c3cn 0x%p, 0x%x, 0x%lx.\n",
			rpl->status, c3cn, c3cn->state, c3cn->flags);

	if (act_open_has_tid(rpl->status))
		cxgb3_queue_tid_release(cdev, GET_TID(rpl));

	process_cpl_msg_ref(active_open_failed, c3cn, skb);
	return 0;
}

/*
 * Handler RX_ISCSI_HDR CPL messages.
 */
static int do_iscsi_hdr(struct t3cdev *t3dev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;
	process_cpl_msg(process_rx_iscsi_hdr, c3cn, skb);
	return 0;
}

/*
 * Handler for TX_DATA_ACK CPL messages.
 */
static int do_wr_ack(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;

	process_cpl_msg(wr_ack, c3cn, skb);
	return 0;
}

/*
 * Handler for PEER_CLOSE CPL messages.
 */
static int do_peer_close(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, c3cn 0x%p, 0x%x, 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	process_cpl_msg_ref(do_peer_fin, c3cn, skb);
	return 0;
}

/*
 * Handle an ABORT_REQ_RSS CPL message.
 */
static int do_abort_req(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	const struct cpl_abort_req_rss *req = cplhdr(skb);
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, c3cn 0x%p, 0x%x, 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (is_neg_adv_abort(req->status)) {
		__kfree_skb(skb);
		return 0;
	}

	process_cpl_msg_ref(process_abort_req, c3cn, skb);
	return 0;
}

/*
 * Handle an ABORT_RPL_RSS CPL message.
 */
static int do_abort_rpl(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct cpl_abort_rpl_rss *rpl = cplhdr(skb);
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, status 0x%x, c3cn 0x%p, 0x%x, 0x%lx.\n",
			rpl->status, c3cn, c3cn ? c3cn->state : 0,
			c3cn ? c3cn->flags : 0UL);

	/*
	 * Ignore replies to post-close aborts indicating that the abort was
	 * requested too late.  These connections are terminated when we get
	 * PEER_CLOSE or CLOSE_CON_RPL and by the time the abort_rpl_rss
	 * arrives the TID is either no longer used or it has been recycled.
	 */
	if (rpl->status == CPL_ERR_ABORT_FAILED)
		goto discard;

	/*
	 * Sometimes we've already closed the connection, e.g., a post-close
	 * abort races with ABORT_REQ_RSS, the latter frees the connection
	 * expecting the ABORT_REQ will fail with CPL_ERR_ABORT_FAILED,
	 * but FW turns the ABORT_REQ into a regular one and so we get
	 * ABORT_RPL_RSS with status 0 and no connection.  Only on T3A.
	 */
	if (!c3cn)
		goto discard;

	process_cpl_msg_ref(process_abort_rpl, c3cn, skb);
	return 0;

discard:
	__kfree_skb(skb);
	return 0;
}

/*
 * Handler for CLOSE_CON_RPL CPL messages.
 */
static int do_close_con_rpl(struct t3cdev *cdev, struct sk_buff *skb,
			    void *ctx)
{
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, c3cn 0x%p, 0x%x, 0x%lx.\n",
			 c3cn, c3cn->state, c3cn->flags);

	process_cpl_msg_ref(process_close_con_rpl, c3cn, skb);
	return 0;
}

/*
 * Definitions and declarations for CPL message processing.
 * ========================================================
 */

static void make_established(struct s3_conn *, u32, unsigned int);
static void act_open_retry_timer(unsigned long);
static void mk_act_open_req(struct s3_conn *, struct sk_buff *,
			    unsigned int, const struct l2t_entry *);
static int act_open_rpl_status_to_errno(int);
static void handle_excess_rx(struct s3_conn *, struct sk_buff *);
static int abort_status_to_errno(struct s3_conn *, int, int *);
static void send_abort_rpl(struct sk_buff *, struct t3cdev *, int);
static struct sk_buff *get_cpl_reply_skb(struct sk_buff *, size_t, gfp_t);

/*
 * Dequeue and return the first unacknowledged's WR on a connections's pending
 * list.
 */
static inline struct sk_buff *dequeue_wr(struct s3_conn *c3cn)
{
	struct sk_buff *skb = c3cn->wr_pending_head;

	if (likely(skb)) {
		/* Don't bother clearing the tail */
		c3cn->wr_pending_head = (struct sk_buff *)skb->sp;
		skb->sp = NULL;
	}
	return skb;
}

/*
 * Return the first pending WR without removing it from the list.
 */
static inline struct sk_buff *peek_wr(const struct s3_conn *c3cn)
{
	return c3cn->wr_pending_head;
}

static inline void free_wr_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

static void purge_wr_queue(struct s3_conn *c3cn)
{
	struct sk_buff *skb;
	while ((skb = dequeue_wr(c3cn)) != NULL)
		free_wr_skb(skb);
}

static inline void set_abort_rpl_wr(struct sk_buff *skb, unsigned int tid,
				    int cmd)
{
	struct cpl_abort_rpl *rpl = cplhdr(skb);

	rpl->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_RPL));
	rpl->wr.wr_lo = htonl(V_WR_TID(tid));
	OPCODE_TID(rpl) = htonl(MK_OPCODE_TID(CPL_ABORT_RPL, tid));
	rpl->cmd = cmd;
}

/*
 * CPL message processing ...
 * ==========================
 */

/*
 * Updates connection state from an active establish CPL message.  Runs with
 * the connection lock held.
 */
static void c3cn_act_establish(struct s3_conn *c3cn,
			       struct sk_buff *skb)
{
	struct cpl_act_establish *req = cplhdr(skb);
	u32 rcv_isn = ntohl(req->rcv_isn);	/* real RCV_ISN + 1 */

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (unlikely(c3cn->state != C3CN_STATE_SYN_SENT))
		printk(KERN_ERR "TID %u expected SYN_SENT, found %d\n",
		       c3cn->tid, c3cn->state);

	c3cn->copied_seq = c3cn->rcv_wup = c3cn->rcv_nxt = rcv_isn;
	make_established(c3cn, ntohl(req->snd_isn), ntohs(req->tcp_opt));

	if (unlikely(c3cn_flag(c3cn, C3CN_CLOSE_NEEDED))) {
		/* upper layer has requested closing */
		abort_conn(c3cn, skb);
		return;
	}

	__kfree_skb(skb);
	if (s3_push_frames(c3cn, 1))
		cxgb3i_conn_tx_open(c3cn);
}

/*
 * Handle active open failures.
 */
static void active_open_failed(struct s3_conn *c3cn,
			       struct sk_buff *skb)
{
	struct cpl_act_open_rpl *rpl = cplhdr(skb);

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (rpl->status == CPL_ERR_CONN_EXIST &&
	    c3cn->retry_timer.function != act_open_retry_timer) {
		c3cn->retry_timer.function = act_open_retry_timer;
		c3cn_reset_timer(c3cn, &c3cn->retry_timer,
				 jiffies + HZ / 2);
	} else
		fail_act_open(c3cn, act_open_rpl_status_to_errno(rpl->status));
	__kfree_skb(skb);
}

/*
 * Process received pdu for a connection.
 */
static void process_rx_iscsi_hdr(struct s3_conn *c3cn,
				 struct sk_buff *skb)
{
	struct cpl_iscsi_hdr *hdr_cpl = cplhdr(skb);
	struct cpl_iscsi_hdr_norss data_cpl;
	struct cpl_rx_data_ddp_norss ddp_cpl;
	unsigned int hdr_len, data_len, status;
	unsigned int len;
	int err;

	if (unlikely(c3cn_no_receive(c3cn))) {
		handle_excess_rx(c3cn, skb);
		return;
	}

	CXGB3_SKB_CB(skb)->seq = ntohl(hdr_cpl->seq);
	CXGB3_SKB_CB(skb)->flags = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(struct cpl_iscsi_hdr));

	len = hdr_len = ntohs(hdr_cpl->len);
	/* msg coalesce is off or not enough data received */
	if (skb->len <= hdr_len) {
		printk(KERN_ERR "%s: TID %u, ISCSI_HDR, skb len %u < %u.\n",
		       c3cn->cdev->name, c3cn->tid, skb->len, hdr_len);
		goto abort_conn;
	}

	err = skb_copy_bits(skb, skb->len - sizeof(ddp_cpl), &ddp_cpl,
			    sizeof(ddp_cpl));
	if (err < 0)
		goto abort_conn;

	skb_ulp_mode(skb) = ULP2_FLAG_DATA_READY;
	skb_ulp_pdulen(skb) = ntohs(ddp_cpl.len);
	skb_ulp_ddigest(skb) = ntohl(ddp_cpl.ulp_crc);
	status = ntohl(ddp_cpl.ddp_status);

	if (status & (1 << RX_DDP_STATUS_HCRC_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_HCRC_ERROR;
	if (status & (1 << RX_DDP_STATUS_DCRC_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_DCRC_ERROR;
	if (status & (1 << RX_DDP_STATUS_PAD_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_PAD_ERROR;

	if (skb->len > (hdr_len + sizeof(ddp_cpl))) {
		err = skb_copy_bits(skb, hdr_len, &data_cpl, sizeof(data_cpl));
		if (err < 0)
			goto abort_conn;
		data_len = ntohs(data_cpl.len);
		len += sizeof(data_cpl) + data_len;
	} else if (status & (1 << RX_DDP_STATUS_DDP_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_DATA_DDPED;

	c3cn->rcv_nxt = ntohl(ddp_cpl.seq) + skb_ulp_pdulen(skb);
	__pskb_trim(skb, len);
	__skb_queue_tail(&c3cn->receive_queue, skb);
	cxgb3i_conn_pdu_ready(c3cn);

	return;

abort_conn:
	s3_send_reset(c3cn, CPL_ABORT_SEND_RST, NULL);
	__kfree_skb(skb);
}

/*
 * Process an acknowledgment of WR completion.  Advance snd_una and send the
 * next batch of work requests from the write queue.
 */
static void wr_ack(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct cpl_wr_ack *hdr = cplhdr(skb);
	unsigned int credits = ntohs(hdr->credits);
	u32 snd_una = ntohl(hdr->snd_una);

	c3cn->wr_avail += credits;
	if (c3cn->wr_unacked > c3cn->wr_max - c3cn->wr_avail)
		c3cn->wr_unacked = c3cn->wr_max - c3cn->wr_avail;

	while (credits) {
		struct sk_buff *p = peek_wr(c3cn);

		if (unlikely(!p)) {
			printk(KERN_ERR "%u WR_ACK credits for TID %u with "
			       "nothing pending, state %u\n",
			       credits, c3cn->tid, c3cn->state);
			break;
		}
		if (unlikely(credits < p->csum)) {
			p->csum -= credits;
			break;
		} else {
			dequeue_wr(c3cn);
			credits -= p->csum;
			free_wr_skb(p);
		}
	}

	if (unlikely(before(snd_una, c3cn->snd_una)))
		goto out_free;

	if (c3cn->snd_una != snd_una) {
		c3cn->snd_una = snd_una;
		dst_confirm(c3cn->dst_cache);
		if (c3cn->snd_una == c3cn->snd_nxt)
			c3cn_reset_flag(c3cn, C3CN_TX_WAIT_IDLE);
	}

	if (skb_queue_len(&c3cn->write_queue) && s3_push_frames(c3cn, 0))
		cxgb3i_conn_tx_open(c3cn);
out_free:
	__kfree_skb(skb);
}

/*
 * Handle a peer FIN.
 */
static void do_peer_fin(struct s3_conn *c3cn, struct sk_buff *skb)
{
	int keep = 0;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING))
		goto out;

	c3cn->shutdown |= C3CN_RCV_SHUTDOWN;
	c3cn_set_flag(c3cn, C3CN_DONE);

	switch (c3cn->state) {
	case C3CN_STATE_ESTABLISHED:
		break;
	case C3CN_STATE_CLOSING:
		c3cn_done(c3cn);
		break;
	default:
		printk(KERN_ERR
		       "%s: TID %u received PEER_CLOSE in bad state %d\n",
		       c3cn->cdev->name, c3cn->tid, c3cn->state);
	}

	cxgb3i_conn_closing(c3cn);
out:
	if (!keep)
		__kfree_skb(skb);
}

/*
 * Process abort requests.  If we are waiting for an ABORT_RPL we ignore this
 * request except that we need to reply to it.
 */
static void process_abort_req(struct s3_conn *c3cn,
			      struct sk_buff *skb)
{
	int rst_status = CPL_ABORT_NO_RST;
	const struct cpl_abort_req_rss *req = cplhdr(skb);

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (!c3cn_flag(c3cn, C3CN_ABORT_REQ_RCVD)) {
		c3cn_set_flag(c3cn, C3CN_ABORT_REQ_RCVD);
		c3cn_set_flag(c3cn, C3CN_ABORT_SHUTDOWN);
		__kfree_skb(skb);
		return;
	}
	c3cn_reset_flag(c3cn, C3CN_ABORT_REQ_RCVD);

	/*
	 * Three cases to consider:
	 * a) We haven't sent an abort_req; close the connection.
	 * b) We have sent a post-close abort_req that will get to TP too late
	 *    and will generate a CPL_ERR_ABORT_FAILED reply.  The reply will
	 *    be ignored and the connection should be closed now.
	 * c) We have sent a regular abort_req that will get to TP too late.
	 *    That will generate an abort_rpl with status 0, wait for it.
	 */
	send_abort_rpl(skb, c3cn->cdev, rst_status);

	if (!c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING)) {
		c3cn->err =
		    abort_status_to_errno(c3cn, req->status, &rst_status);

		c3cn_done(c3cn);
	}
}

/*
 * Process abort replies.  We only process these messages if we anticipate
 * them as the coordination between SW and HW in this area is somewhat lacking
 * and sometimes we get ABORT_RPLs after we are done with the connection that
 * originated the ABORT_REQ.
 */
static void process_abort_rpl(struct s3_conn *c3cn,
			      struct sk_buff *skb)
{
	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING)) {
		if (!c3cn_flag(c3cn, C3CN_ABORT_RPL_RCVD))
			c3cn_set_flag(c3cn, C3CN_ABORT_RPL_RCVD);
		else {
			c3cn_reset_flag(c3cn, C3CN_ABORT_RPL_RCVD);
			c3cn_reset_flag(c3cn, C3CN_ABORT_RPL_PENDING);
			BUG_ON(c3cn_flag(c3cn, C3CN_ABORT_REQ_RCVD));
			c3cn_done(c3cn);
		}
	}
	__kfree_skb(skb);
}

/*
 * Process a peer ACK to our FIN.
 */
static void process_close_con_rpl(struct s3_conn *c3cn,
				  struct sk_buff *skb)
{
	struct cpl_close_con_rpl *rpl = cplhdr(skb);

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	c3cn->snd_una = ntohl(rpl->snd_nxt) - 1;	/* exclude FIN */

	if (c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING))
		goto out;

	if (c3cn->state == C3CN_STATE_CLOSING) {
		c3cn_done(c3cn);
	} else
		printk(KERN_ERR
		       "%s: TID %u received CLOSE_CON_RPL in bad state %d\n",
		       c3cn->cdev->name, c3cn->tid, c3cn->state);
out:
	kfree_skb(skb);
}

/*
 * Random utility functions for CPL message processing ...
 * =======================================================
 */

/**
 *	find_best_mtu - find the entry in the MTU table closest to an MTU
 *	@d: TOM state
 *	@mtu: the target MTU
 *
 *	Returns the index of the value in the MTU table that is closest to but
 *	does not exceed the target MTU.
 */
static unsigned int find_best_mtu(const struct t3c_data *d, unsigned short mtu)
{
	int i = 0;

	while (i < d->nmtus - 1 && d->mtus[i + 1] <= mtu)
		++i;
	return i;
}

static unsigned int select_mss(struct s3_conn *c3cn, unsigned int pmtu)
{
	unsigned int idx;
	struct dst_entry *dst = c3cn->dst_cache;
	struct t3cdev *cdev = c3cn->cdev;
	const struct t3c_data *td = T3C_DATA(cdev);
	u16 advmss = dst_metric(dst, RTAX_ADVMSS);

	if (advmss > pmtu - 40)
		advmss = pmtu - 40;
	if (advmss < td->mtus[0] - 40)
		advmss = td->mtus[0] - 40;
	idx = find_best_mtu(td, advmss + 40);
	return idx;
}

static void fail_act_open(struct s3_conn *c3cn, int errno)
{
	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	c3cn->err = errno;
	c3cn_done(c3cn);
}

/*
 * Assign offload parameters to some connection fields.
 */
static void init_offload_conn(struct s3_conn *c3cn,
			      struct t3cdev *cdev,
			      struct dst_entry *dst)
{
	BUG_ON(c3cn->cdev != cdev);
	c3cn->wr_max = c3cn->wr_avail = T3C_DATA(cdev)->max_wrs;
	c3cn->wr_unacked = 0;
	c3cn->mss_idx = select_mss(c3cn, dst_mtu(dst));

	c3cn->ctrl_skb_cache = alloc_skb(CTRL_SKB_LEN, gfp_any());
	reset_wr_list(c3cn);
}

static void act_open_retry_timer(unsigned long data)
{
	struct sk_buff *skb;
	struct s3_conn *c3cn = (struct s3_conn *)data;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x.\n", c3cn, c3cn->state);

	spin_lock(&c3cn->lock);
	skb = alloc_skb(sizeof(struct cpl_act_open_req), GFP_ATOMIC);
	if (!skb)
		fail_act_open(c3cn, ENOMEM);
	else {
		skb->sk = (struct sock *)c3cn;
		set_arp_failure_handler(skb, act_open_req_arp_failure);
		mk_act_open_req(c3cn, skb, c3cn->tid, c3cn->l2t);
		l2t_send(c3cn->cdev, skb, c3cn->l2t);
	}
	spin_unlock(&c3cn->lock);
	c3cn_put(c3cn);
}

/*
 * Convert an ACT_OPEN_RPL status to a Linux errno.
 */
static int act_open_rpl_status_to_errno(int status)
{
	switch (status) {
	case CPL_ERR_CONN_RESET:
		return ECONNREFUSED;
	case CPL_ERR_ARP_MISS:
		return EHOSTUNREACH;
	case CPL_ERR_CONN_TIMEDOUT:
		return ETIMEDOUT;
	case CPL_ERR_TCAM_FULL:
		return ENOMEM;
	case CPL_ERR_CONN_EXIST:
		printk(KERN_ERR "ACTIVE_OPEN_RPL: 4-tuple in use\n");
		return EADDRINUSE;
	default:
		return EIO;
	}
}

/*
 * Convert the status code of an ABORT_REQ into a Linux error code.  Also
 * indicate whether RST should be sent in response.
 */
static int abort_status_to_errno(struct s3_conn *c3cn,
				 int abort_reason, int *need_rst)
{
	switch (abort_reason) {
	case CPL_ERR_BAD_SYN: /* fall through */
	case CPL_ERR_CONN_RESET:
		return c3cn->state == C3CN_STATE_CLOSING ? EPIPE : ECONNRESET;
	case CPL_ERR_XMIT_TIMEDOUT:
	case CPL_ERR_PERSIST_TIMEDOUT:
	case CPL_ERR_FINWAIT2_TIMEDOUT:
	case CPL_ERR_KEEPALIVE_TIMEDOUT:
		return ETIMEDOUT;
	default:
		return EIO;
	}
}

static void send_abort_rpl(struct sk_buff *skb, struct t3cdev *cdev,
			   int rst_status)
{
	struct sk_buff *reply_skb;
	struct cpl_abort_req_rss *req = cplhdr(skb);

	reply_skb = get_cpl_reply_skb(skb, sizeof(struct cpl_abort_rpl),
				      gfp_any());

	reply_skb->priority = CPL_PRIORITY_DATA;
	set_abort_rpl_wr(reply_skb, GET_TID(req), rst_status);
	kfree_skb(skb);
	cxgb3_ofld_send(cdev, reply_skb);
}

/*
 * Returns an sk_buff for a reply CPL message of size len.  If the input
 * sk_buff has no other users it is trimmed and reused, otherwise a new buffer
 * is allocated.  The input skb must be of size at least len.  Note that this
 * operation does not destroy the original skb data even if it decides to reuse
 * the buffer.
 */
static struct sk_buff *get_cpl_reply_skb(struct sk_buff *skb, size_t len,
					 gfp_t gfp)
{
	if (likely(!skb_cloned(skb))) {
		BUG_ON(skb->len < len);
		__skb_trim(skb, len);
		skb_get(skb);
	} else {
		skb = alloc_skb(len, gfp);
		if (skb)
			__skb_put(skb, len);
	}
	return skb;
}

/*
 * Release resources held by an offload connection (TID, L2T entry, etc.)
 */
static void t3_release_offload_resources(struct s3_conn *c3cn)
{
	struct t3cdev *cdev = c3cn->cdev;
	unsigned int tid = c3cn->tid;

	if (!cdev)
		return;

	c3cn->qset = 0;

	kfree_skb(c3cn->ctrl_skb_cache);
	c3cn->ctrl_skb_cache = NULL;

	if (c3cn->wr_avail != c3cn->wr_max) {
		purge_wr_queue(c3cn);
		reset_wr_list(c3cn);
	}

	if (c3cn->l2t) {
		l2t_release(L2DATA(cdev), c3cn->l2t);
		c3cn->l2t = NULL;
	}

	if (c3cn->state == C3CN_STATE_SYN_SENT) /* we have ATID */
		free_atid(cdev, tid);
	else {		/* we have TID */
		cxgb3_remove_tid(cdev, (void *)c3cn, tid);
		c3cn_put(c3cn);
	}

	c3cn->cdev = NULL;
}

/*
 * Handles Rx data that arrives in a state where the connection isn't
 * accepting new data.
 */
static void handle_excess_rx(struct s3_conn *c3cn, struct sk_buff *skb)
{
	if (!c3cn_flag(c3cn, C3CN_ABORT_SHUTDOWN))
		abort_conn(c3cn, skb);

	kfree_skb(skb);
}

/*
 * Like get_cpl_reply_skb() but the returned buffer starts out empty.
 */
static struct sk_buff *__get_cpl_reply_skb(struct sk_buff *skb, size_t len,
					   gfp_t gfp)
{
	if (likely(!skb_cloned(skb) && !skb->data_len)) {
		__skb_trim(skb, 0);
		skb_get(skb);
	} else
		skb = alloc_skb(len, gfp);
	return skb;
}

/*
 * Completes some final bits of initialization for just established connections
 * and changes their state to C3CN_STATE_ESTABLISHED.
 *
 * snd_isn here is the ISN after the SYN, i.e., the true ISN + 1.
 */
static void make_established(struct s3_conn *c3cn, u32 snd_isn,
			     unsigned int opt)
{
	c3cn_conn_debug("c3cn 0x%p, state 0x%x.\n", c3cn, c3cn->state);

	c3cn->write_seq = c3cn->snd_nxt = c3cn->snd_una = snd_isn;

	/*
	 * Causes the first RX_DATA_ACK to supply any Rx credits we couldn't
	 * pass through opt0.
	 */
	if (cxgb3_rcv_win > (M_RCV_BUFSIZ << 10))
		c3cn->rcv_wup -= cxgb3_rcv_win - (M_RCV_BUFSIZ << 10);

	dst_confirm(c3cn->dst_cache);

	smp_mb();
	c3cn_set_state(c3cn, C3CN_STATE_ESTABLISHED);
}
