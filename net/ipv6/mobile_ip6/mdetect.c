/*
 *      Movement Detection Module
 *
 *      Authors:
 *      Henrik Petander                <lpetande@cc.hut.fi>
 *
 *      $Id: s.mdetect.c 1.135 03/10/02 15:26:27+03:00 henkku@mart10.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      Handles the L3 movement detection of mobile node and also
 *      changing of its routes.
 *  
 */

/*
 *	Changes:
 *
 *	Nanno Langstraat	:	Locking fixes
 *      Venkata Jagana          :       Locking fix
 */

#include <linux/autoconf.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/route.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/mipglue.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif /* CONFIG_SYSCTL */

#include "util.h"
#include "mdetect.h"
#include "mn.h"
#include "debug.h"
#include "multiaccess_ctl.h"

#define START 0
#define CONTINUE 1
#define OK 2
#define DEBUG_MDETECT 7

#define DEF_RTR_POLL_IVAL RTR_SOLICITATION_INTERVAL /* In seconds */

/* States */
#define CURR_RTR_PRESENT	0
#define CURR_RTR_NOT_PRESENT	1
#define STOP_RTR_PROBE		2

/* Events */
#define RA_RCVD			0
#define RA_TIMEOUT		1

#define MIPV6_MDF_NONE 0x0
#define MIPV6_MDF_HAS_RTR_PREV 0x1

#define ROUTER_REACHABLE 1
#define RADV_MISSED 2
#define NOT_REACHABLE 3

/* R_TIME_OUT paramater is used to make the decision when to change the 
 * default  router, if the current one is unreachable. 2s is pretty aggressive 
 * and may result in hopping between two routers. OTOH a small value enhances 
 * the  performance
 */
#define R_TIME_OUT 30*HZ

/* maximum RA interval for router unreachability detection */
#define MAX_RADV_INTERVAL 6000*HZ  /* 6000 ms... */

/* Threshold for exponential resending of router solicitations */
#define RS_RESEND_LINEAR 10*HZ

#define EAGER_CELL_SWITCHING 1
#define LAZY_CELL_SWITCHING 0
#define RESPECT_DAD 1

#define ROUTER_ADDRESS 0x20

/* RA flags */
#define ND_RA_FLAG_MANAGED  0x80
#define ND_RA_FLAG_OTHER    0x40
#define ND_RA_FLAG_HA       0x20
/* Care-of address */
#define COA_TENTATIVE       0x10

struct router {
	struct list_head list;
	struct in6_addr ll_addr;
	struct in6_addr raddr; /* Also contains prefix */
	__u8 link_addr[MAX_ADDR_LEN]; /* link layer address */
	__u8 link_addr_len;
	__u8 state;
	__u8 is_current;
	int ifindex;
	int pfix_len; /* Length of the network prefix */
	unsigned long lifetime; /* from ra */
	__u32 last_ns_sent; 
	__u32 last_ra_rcvd;
	__u32 interval; /* ra interval in milliseconds, 0 if not set */ 
	int glob_addr; /*Whether raddr contains also routers global address*/
	__u8 flags; /* RA flags, for example ha */
        struct in6_addr CoA;     /* care-off address used with this router */
	int extra_addr_route;
};

/* dad could also be RESPECT_DAD for duplicate address detection of
   new care-of addresses */
static int dad = 0;

/* Only one choice, nothing else implemented */
int max_rtr_reach_time = DEF_RTR_POLL_IVAL;
int mdet_mech = EAGER_CELL_SWITCHING; 

int eager_cell_switching = 1;  /* This can't be set from anywhere for now */
static spinlock_t router_lock; 
static spinlock_t ho_lock;

static void coa_timer_handler(unsigned long arg);
static void timer_handler(unsigned long foo);
static struct router *curr_router = NULL, *next_router = NULL;
static struct timer_list r_timer = { function: timer_handler };
static struct timer_list coa_timer = { function: coa_timer_handler };
#define MAX_ROUTERS 1000
static LIST_HEAD(rtr_list);
static int num_routers = 0;
static struct handoff *_ho = NULL;
/*
 * Functions for handling the default router list, which movement
 * detection uses for avoiding loops etc.
 */

/* TODO: Send NS to router after MAX interval has passed from last RA */
static int mipv6_router_state(struct router *rtr)
{
	if (rtr->interval) {
		if (time_before(jiffies, (unsigned long)(rtr->last_ra_rcvd + (rtr->interval * HZ) / 1000)))
			return ROUTER_REACHABLE;
		else
			return NOT_REACHABLE;
	}
	else
		if (time_after(jiffies, (unsigned long)(rtr->last_ra_rcvd + (rtr->lifetime * HZ))))
			return NOT_REACHABLE;
	return ROUTER_REACHABLE;
}

/* searches for a specific router or any router that is reachable, 
 * if address is NULL. Also deletes obsolete routers.
 */
static void mipv6_router_gc(void)
{
	struct router *curr = NULL;
	struct list_head *lh, *lh_tmp;

	DEBUG_FUNC();

	list_for_each_safe(lh, lh_tmp, &rtr_list) {
		curr =  list_entry(lh, struct router, list);
		if (mipv6_router_state(curr) == NOT_REACHABLE && !curr->is_current) {
			num_routers--;
			list_del_init(&curr->list);
			DEBUG(DBG_DATADUMP, "Deleting unreachable router  %x:%x:%x:%x:%x:%x:%x:%x", 
			      NIPV6ADDR(&curr->raddr));
			kfree(curr);
		}
		else {
			DEBUG(DBG_DATADUMP, "NOT Deleting router  %x:%x:%x:%x:%x:%x:%x:%x", 
			      NIPV6ADDR(&curr->raddr));
		}
	}
}

static struct router *mipv6_rtr_get(struct in6_addr *search_addr)
{
	struct router *rtr = NULL;
	struct list_head *lh;

	DEBUG_FUNC();

	if (search_addr == NULL)
		return NULL;
	list_for_each(lh, &rtr_list) {
		rtr = list_entry(lh, struct router, list);
		if(!ipv6_addr_cmp(search_addr, &rtr->raddr)) {
			return rtr;
		}
	}
	return NULL;
}

/*
 * Adds router to list
 */
static struct router *mipv6_rtr_add(struct router *nrt)
{

	struct router *rptr;

	DEBUG_FUNC();

	/* check if someone is trying DoS attack, or we just have some
           memory leaks... */
	if (num_routers > MAX_ROUTERS) {
		DEBUG(DBG_CRITICAL, 
		      "failed to add new router, MAX_ROUTERS exceeded");
		return NULL;
	}
	
	rptr = kmalloc(sizeof(struct router), GFP_ATOMIC);
	if (rptr) {
		memcpy(rptr, nrt, sizeof(struct router));
		list_add(&rptr->list, &rtr_list);
		num_routers++;
	}
	DEBUG(DBG_INFO, "Adding router: %x:%x:%x:%x:%x:%x:%x:%x, "
	      "lifetime : %d sec, adv.interval: %d millisec", 
	      NIPV6ADDR(&rptr->raddr), rptr->lifetime, rptr->interval);

	DEBUG(DBG_INFO, "num_routers after addition: %d", num_routers);
	return rptr;
}

/* Cleans up the list */
static void list_free(struct router **curr_router_p)
{
	struct router *tmp;
	struct list_head *lh, *lh_tmp;

	DEBUG_FUNC();

	DEBUG(DBG_INFO, "Freeing the router list");
	/* set curr_router->prev_router and curr_router NULL */
	*curr_router_p = NULL;
	list_for_each_safe(lh, lh_tmp, &rtr_list) {
		tmp = list_entry(lh, struct router, list);
		DEBUG(DBG_INFO, "%x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(&tmp->ll_addr));
		list_del(&tmp->list);
		kfree(tmp);
		num_routers--;
	}
}

int rs_state = START;

/* Sends router solicitations to all valid devices 
 * source  = link local address (of sending interface)
 * dstaddr = all routers multicast address
 * Solicitations are sent at an exponentially decreasing rate
 *
 * TODO: send solicitation first at a normal rate (from ipv6) and
 *       after that use the exponentially increasing intervals 
 */
static int rs_send(void)
{
	struct net_device *dev;
	struct in6_addr raddr, lladdr;
	struct inet6_dev *in6_dev = NULL;
	static int num_rs;

	if (rs_state == START) {
		num_rs = 0;
		rs_state = CONTINUE;
	} else if (num_rs++ > MAX_RTR_SOLICITATIONS)
		return HZ;

	ipv6_addr_all_routers(&raddr);
	read_lock(&dev_base_lock); 

	/*  Send router solicitations to all interfaces  */
	for (dev = dev_base; dev; dev = dev->next) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ETHER) {
			DEBUG(DBG_DATADUMP, "Sending RS to device %s", 
			      dev->name);
			if (!ipv6_get_lladdr(dev, &lladdr)) {
				ndisc_send_rs(dev, &lladdr, &raddr);
				in6_dev = in6_dev_get(dev);
				in6_dev->if_flags |= IF_RS_SENT;
				in6_dev_put(in6_dev);
			} else {
				DEBUG(DBG_DATADUMP, "%s: device doesn't have link-local address!\n", dev->name);
				continue;
			}
		}
		
	}
	read_unlock(&dev_base_lock);
	return RTR_SOLICITATION_INTERVAL;
}

/* Create a new CoA for MN and also add a route to it if it is still tentative 
   to allow MN to get packets to the address immediately
 */
static int form_coa(struct in6_addr *coa, struct in6_addr *pfix, 
		    int plen, int ifindex)
{
	struct net_device *dev;
	int ret = 0;

	if ((dev = dev_get_by_index(ifindex)) == NULL) {
		DEBUG(DBG_WARNING, "Device is not present");
		return -1;
	}
	if (ipv6_get_lladdr(dev, coa) != 0) /* Link local address still tentative */
		ret = -1;
	coa->s6_addr32[0] = pfix->s6_addr32[0];
	coa->s6_addr32[1] = pfix->s6_addr32[1];
	
	if (ipv6_chk_addr(coa, dev, 0) == 0) { 
		DEBUG(DBG_WARNING, "care-of address still tentative");
		ret = 1;
	} 

	dev_put(dev);
	DEBUG(DBG_INFO, "Formed new CoA:  %x:%x:%x:%x:%x:%x:%x:%x",
	      NIPV6ADDR(coa));
	return ret;
}

static inline int rtr_is_gw(struct router *rtr, struct rt6_info *rt) 
{
	return ((rt->rt6i_flags & RTF_GATEWAY) && 
		!ipv6_addr_cmp(&rt->rt6i_gateway, &rtr->ll_addr));
}

static inline int is_prefix_route(struct router *rtr, struct rt6_info *rt) 
{
	return (!(rt->rt6i_flags & RTF_GATEWAY) &&
		mipv6_prefix_compare(&rt->rt6i_dst.addr, &rtr->raddr, 
				     rtr->pfix_len));
}

/*
 * Function that determines whether given rt6_info should be destroyed
 * (negative => destroy rt6_info, zero or positive => do nothing) 
 */
static int mn_route_cleaner(struct rt6_info *rt, void *arg)
{
	int type;

	struct router *rtr = (struct router *)arg;

	int ret = -1;

	DEBUG_FUNC();
	
	if (!rt || !rtr) {
		DEBUG(DBG_ERROR, "mn_route_cleaner: rt or rtr NULL");
		return 0;
	}

	/* Do not delete routes to local addresses or to multicast
	 * addresses, since we need them to get router advertisements
	 * etc. Multicast addresses are more tricky, but we don't
	 * delete them in any case. The routing mechanism is not optimal for 
	 * multihoming.   
	 *
	 * Also keep all new prefix routes, gateway routes through rtr and
	 * all remaining default routes (including those used for reverse
	 * tunneling)
	 */
	type = ipv6_addr_type(&rt->rt6i_dst.addr);
	
	if ((type & (IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL)) ||
	    rt->rt6i_dev == &loopback_dev || rtr_is_gw(rtr, rt) ||
	    is_prefix_route(rtr, rt) || (rt->rt6i_flags & RTF_DEFAULT))  
		ret = 0;
	
	/*   delete all others */

	if (rt->rt6i_dev != &loopback_dev) {
		DEBUG(DEBUG_MDETECT, 
		      "%s route:\n"
		      "dev: %s,\n"
		      "gw: %x:%x:%x:%x:%x:%x:%x:%x,\n"
		      "flags: %x,\n"
		      "metric: %d,\n"
		      "src: %x:%x:%x:%x:%x:%x:%x:%x,\n"
		      "dst: %x:%x:%x:%x:%x:%x:%x:%x,\n"
		      "plen: %d\n",
		      (ret ? "Deleting" : "Keeping"),
		      rt->rt6i_dev->name,	       
		      NIPV6ADDR(&rt->rt6i_gateway),	       
		      rt->rt6i_flags,
		      rt->rt6i_metric,
		      NIPV6ADDR(&rt->rt6i_src.addr),
		      NIPV6ADDR(&rt->rt6i_dst.addr),
		      rt->rt6i_dst.plen);
	}
	return ret;
}

/* 
 * Deletes old routes 
 */
static __inline__ void delete_routes(struct router *rtr)
{
	DEBUG_FUNC();

	/* Routing table is locked to ensure that nobody uses its */  
	write_lock_bh(&rt6_lock);
	DEBUG(DBG_INFO, "mipv6: Purging routes");
	/*  TODO: Does not prune, should it?  */
	fib6_clean_tree(&ip6_routing_table, 
			mn_route_cleaner, 0, rtr);
	write_unlock_bh(&rt6_lock);

}

#if 0
int next_mdet_state[3][3] = {{CURR_RTR_OK, NO_RTR, NO_RTR},
			     {CURR_RTR_OK, CURR_RTR_OK, NO_RTR},
			     {CURR_RTR_OK, CURR_RTR_OK, RTR_SUSPECT}};
 
char *states[3] = {"NO_RTR", "RTR_SUSPECT", "CURR_RTR_OK"};
char *events[3] = {"RA_RCVD", "NA_RCVD", "TIMEOUT"};

/* State transitions
 * NO_RTR, RA_RCVD -> CURR_RTR_OK
 * NO_RTR, NA_RCVD -> NO_RTR
 * NO_RTR, TIMEOUT -> NO_RTR

 * RTR_SUSPECT, RA_RCVD -> CURR_RTR_OK
 * RTR_SUSPECT, NA_RCVD -> CURR_RTR_OK
 * RTR_SUSPECT, TIMEOUT -> NO_RTR

 * CURR_RTR_OK, RA_RCVD -> CURR_RTR_OK
 * CURR_RTR_OK, NA_RCVD -> CURR_RTR_OK
 * CURR_RTR_OK, TIMEOUT -> RTR_SUSPECT
 */
#else
/*
 * State diagram has been changed to :
 * STOP_RTR_PROBE, RA_RCVD -> CURR_RTR_PRESENT
 *
 * CURR_RTR_NOT_PRESENT, RA_RCVD -> CURR_RTR_PRESENT
 * CURR_RTR_NOT_PRESENT, RA_TIMEOUT -> CURR_RTR_NOT_PRESENT
 * CURR_RTR_NOT_PRESENT, RA_TIMEOUT (if _curr_count == 0) -> STOP_RTR_PROBE
 *
 * CURR_RTR_PRESENT, RA_RCVD -> CURR_RTR_PRESENT
 * CURR_RTR_PRESENT, RA_TIMEOUT -> CURR_RTR_NOT_PRESENT
 */
char *states[3] = {"CURR_RTR_PRESENT", "CURR_RTR_NOT_PRESENT", "STOP_RTR_PROBE"};
char *events[3] = {"RA_RCVD", "RA_TIMEOUT"};

static int _curr_state = CURR_RTR_NOT_PRESENT;
static int _curr_count = MAX_RTR_SOLICITATIONS;
#endif

/* Needs to be called with router_lock locked */
static int mdet_statemachine(int event)
{
	if (event > RA_TIMEOUT || _curr_state > STOP_RTR_PROBE) {
	       DEBUG(DBG_ERROR, "Got illegal event or curr_state");
	       return -1;
	}

	DEBUG(DBG_DATADUMP, "Got event %s and curr_state is %s", 
	      events[event], states[_curr_state]); 
	
	switch (event) {
		case RA_RCVD:
			_curr_state = CURR_RTR_PRESENT;
			_curr_count = MAX_RTR_SOLICITATIONS;
			break;
		case RA_TIMEOUT:
			/* Try for _curr_count before stopping probe */
			if (_curr_count-- <= 0)
				_curr_state = STOP_RTR_PROBE;
			else _curr_state = CURR_RTR_NOT_PRESENT;
			break;
	}

	DEBUG(DBG_DATADUMP, "Next state is %s", states[_curr_state]);

	return _curr_state;
}

/* 
 * Changes the router, called from ndisc.c if mipv6_router_event 
 * returns true.
 */

static void mipv6_change_router(void)
{

	struct in6_addr coa;
	int ret, ifindex;
	
	DEBUG_FUNC(); 

	
	if (next_router == NULL) 
		return;
	
	spin_lock(&router_lock);


	if (curr_router != NULL && 
	    !ipv6_addr_cmp(&curr_router->ll_addr, &next_router->ll_addr)) {
		DEBUG(DBG_INFO,"Trying to handoff from: "
		      "%x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(&curr_router->ll_addr));
		DEBUG(DBG_INFO,"Trying to handoff to: "
		      "%x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(&next_router->ll_addr));
		next_router = NULL; /* Let's not leave dangling pointers */
		spin_unlock(&router_lock);
		return;
        }

	ret = form_coa(&next_router->CoA, &next_router->raddr, 64, 
		       next_router->ifindex);
	if (ret < 0) {
		DEBUG(DBG_ERROR, "handoff: Creation of coa failed");
		spin_unlock(&router_lock);
		return;
	} else if (ret > 0)
		next_router->flags |= COA_TENTATIVE;

	mdet_statemachine(RA_RCVD); /* TODO: What if DAD fails... */
	if (next_router->interval)
		mod_timer(&r_timer, jiffies + 
			  (next_router->interval * HZ)/1000);
	else
		mod_timer(&r_timer, jiffies + max_rtr_reach_time);

	if (ret == 0) {
		ipv6_addr_copy(&coa, &next_router->CoA);
		ifindex = next_router->ifindex;
		spin_unlock(&router_lock);
		mipv6_mdet_finalize_ho(&coa, ifindex);
		return;
	}
	spin_unlock(&router_lock);

}

static void coa_timer_handler(unsigned long dummy)
{
	spin_lock_bh(&ho_lock);
	if (_ho) {
		DEBUG(DBG_INFO, "Starting handoff after DAD");
		mipv6_mobile_node_moved(_ho);
		kfree(_ho);
		_ho = NULL;
	}
	spin_unlock_bh(&ho_lock);
}
static void timer_handler(unsigned long foo)
{
	unsigned long timeout = 0;
	int state;

	spin_lock_bh(&router_lock);
	state = mdet_statemachine(RA_TIMEOUT);
	if (state == CURR_RTR_NOT_PRESENT)
		(void) rs_send();
	if (state != STOP_RTR_PROBE) {
		if (curr_router) {
			timeout = curr_router->interval ?
			curr_router->interval * HZ / 1000 :
			max_rtr_reach_time;
			if (timeout < 2) {
				DEBUG(DBG_ERROR, "mdetect timeout < 0.02s");
				timeout = 10;
			}
		} else
			timeout = max_rtr_reach_time;
	}

	mipv6_router_gc();
	if (timeout)
		mod_timer(&r_timer, jiffies + timeout);
	spin_unlock_bh(&router_lock);
}

/**
 * mipv6_get_care_of_address - get node's care-of primary address
 * @homeaddr: one of node's home addresses
 * @coaddr: buffer to store care-of address
 *
 * Stores the current care-of address in the @coaddr, assumes
 * addresses in EUI-64 format.  Since node might have several home
 * addresses caller MUST supply @homeaddr.  If node is at home
 * @homeaddr is stored in @coaddr.  Returns 0 on success, otherwise a
 * negative value.
 **/
int mipv6_get_care_of_address(
	struct in6_addr *homeaddr, struct in6_addr *coaddr)
{
	
	DEBUG_FUNC();

	if (homeaddr == NULL)
		return -1;
	spin_lock_bh(&router_lock);
	if (curr_router == NULL || mipv6_mn_is_at_home(homeaddr) || 
	    mipv6_prefix_compare(homeaddr, &curr_router->raddr, 64) || 
	    curr_router->flags&COA_TENTATIVE) {
		DEBUG(DBG_INFO,
		      "mipv6_get_care_of_address: returning home address");
		ipv6_addr_copy(coaddr, homeaddr);
		spin_unlock_bh(&router_lock);
		return 0;

	}

	/* At home or address check failure probably due to dad wait */
	if (mipv6_prefix_compare(&curr_router->raddr, homeaddr, 
				 curr_router->pfix_len) 
				 || (dad == RESPECT_DAD && 
				     (ipv6_chk_addr(coaddr, NULL, 0) == 0))) { 
		ipv6_addr_copy(coaddr, homeaddr);
	} else { 
		ipv6_addr_copy(coaddr, &curr_router->CoA);
	}

	spin_unlock_bh(&router_lock);
	return 0;
}
int mipv6_mdet_del_if(int ifindex)
{
	struct router *curr = NULL;
	struct list_head *lh, *lh_tmp;

	DEBUG_FUNC();

	spin_lock_bh(&router_lock);
	list_for_each_safe(lh, lh_tmp, &rtr_list) {
		curr =  list_entry(lh, struct router, list);
		if (curr->ifindex == ifindex) {
			num_routers--;
			list_del_init(&curr->list);
			DEBUG(DBG_DATADUMP, "Deleting router  %x:%x:%x:%x:%x:%x:%x:%x on interface %d", 
			      NIPV6ADDR(&curr->raddr), ifindex);
			if (curr_router == curr)
				curr_router = NULL;
			kfree(curr);
		}
	}
	spin_unlock_bh(&router_lock);
	return 0;
}
int mipv6_mdet_finalize_ho(const struct in6_addr *coa, const int ifindex)
{
	int dummy;
	struct handoff ho;
	struct router *tmp;

	spin_lock_bh(&router_lock);
	if (next_router && mipv6_prefix_compare(coa, &next_router->CoA, 
						next_router->pfix_len)) {
	
		tmp = curr_router;
		curr_router = next_router;
		curr_router->is_current = 1;
		next_router = NULL; 
		curr_router->flags &= ~COA_TENTATIVE; 
		delete_routes(curr_router);
		if (tmp) {
			struct net_device *dev = dev_get_by_index(tmp->ifindex);
			struct rt6_info *rt = NULL;
			if (dev) {
				rt = rt6_get_dflt_router(&tmp->ll_addr, dev);
				dev_put(dev);
			}
			if (rt)
				ip6_del_rt(rt, NULL, NULL);
			tmp->is_current = 0;
		}

		ma_ctl_upd_iface(curr_router->ifindex, MA_IFACE_CURRENT, &dummy);
		ma_ctl_upd_iface(curr_router->ifindex, MA_IFACE_CURRENT, &dummy);


		memcpy(&ho.rtr_addr, &curr_router->raddr, sizeof(ho.rtr_addr));
		ho.coa = &curr_router->CoA;
		ho.plen = curr_router->pfix_len;
		ho.ifindex = curr_router->ifindex;
		ipv6_addr_copy(&ho.rtr_addr, &curr_router->raddr);
		ho.home_address = (curr_router->glob_addr && 
				    curr_router->flags&ND_RA_FLAG_HA);
		
		spin_unlock_bh(&router_lock);
		mipv6_mobile_node_moved(&ho);
	} else 
		spin_unlock_bh(&router_lock);
	return 0;
}
/* Decides whether router candidate is the same router as current rtr
 * based on prefix / global addresses of the routers and their link local 
 * addresses 
 */
static int is_current_rtr(struct router *nrt, struct router *crt)
{
	DEBUG_FUNC();
	
	DEBUG(DEBUG_MDETECT, "Current router: "
	      "%x:%x:%x:%x:%x:%x:%x:%x and", NIPV6ADDR(&crt->raddr));
	DEBUG(DEBUG_MDETECT, "Candidate router: "
	      "%x:%x:%x:%x:%x:%x:%x:%x", NIPV6ADDR(&nrt->raddr));

	return (!ipv6_addr_cmp(&nrt->raddr,&crt->raddr) && 
		!ipv6_addr_cmp(&nrt->ll_addr, &crt->ll_addr));
}

/* 
 * Set next router to nrtr
 * TODO: Locking to ensure nothing happens to next router
 * before handoff is done
 */ 
static void set_next_rtr(struct router *nrtr, struct router *ortr)
{
	DEBUG_FUNC();
	next_router = nrtr;
}

static int clean_ncache(struct router *nrt, struct router *ort, int same_if)
{
	struct net_device *ortdev;
	DEBUG_FUNC();

	/* Always call ifdown after a handoff to ensure proper routing */
	
	if (!ort) 
		return 0;
	if ((ortdev = dev_get_by_index(ort->ifindex)) == NULL) {
		DEBUG(DBG_WARNING, "Device is not present");
		return -1;
	}
	neigh_ifdown(&nd_tbl, ortdev);
	dev_put(ortdev);	
	return 0;
}

static int mdet_get_if_preference(int ifi)
{
	int pref = 0;

	DEBUG_FUNC();

	pref = ma_ctl_get_preference(ifi);

	DEBUG(DEBUG_MDETECT, "ifi: %d preference %d", ifi, pref);

	return pref;
}

/*
 * Called from mipv6_mn_ra_rcv to determine whether to do a handoff. 
 */
static int mipv6_router_event(struct router *rptr)
{
	struct router *nrt = NULL;
	int new_router = 0, same_if = 1;
	int ret = MIPV6_IGN_RTR;
	int oldstate = _curr_state;
	int addrtype = ipv6_addr_type(&rptr->raddr);

	DEBUG_FUNC();

	if (rptr->lifetime == 0)
		return ret;
	DEBUG(DEBUG_MDETECT, "Received a RA from router: "
	      "%x:%x:%x:%x:%x:%x:%x:%x", NIPV6ADDR(&rptr->raddr));
	spin_lock(&router_lock);
	
	/* Add or update router entry */
	if ((nrt = mipv6_rtr_get(&rptr->raddr)) == NULL) {
		if (addrtype == IPV6_ADDR_ANY || (nrt = mipv6_rtr_add(rptr)) == NULL) {
				spin_unlock(&router_lock);
				return ret;
		}
		DEBUG(DBG_INFO, "Router not on list,adding it to the list"); 
		new_router = 1;
	}
	nrt->last_ra_rcvd = jiffies;
	nrt->state = ROUTER_REACHABLE;
	nrt->interval = rptr->interval;
	nrt->lifetime = rptr->lifetime;
	nrt->ifindex = rptr->ifindex;
	nrt->flags = rptr->flags;
	nrt->glob_addr = rptr->glob_addr;

	/* Whether from current router */
	if (curr_router && is_current_rtr(nrt, curr_router)) {
		if (nrt->interval)
			mod_timer(&r_timer, jiffies + (nrt->interval * HZ)/1000);
		else
			mod_timer(&r_timer, jiffies + max_rtr_reach_time);
		mdet_statemachine(RA_RCVD);
		spin_unlock(&router_lock);
		return MIPV6_ADD_RTR;
	} else if (oldstate == STOP_RTR_PROBE) {
		rt6_purge_dflt_routers(0); /* For multiple interface case */
		DEBUG(DBG_INFO, "No router or router not reachable, switching to new one");   
		goto handoff;
	}
	if (!curr_router) { 
	        /* Startup */
	        goto handoff;
	}
	/* Router behind same interface as current one ?*/
	same_if = (nrt->ifindex == curr_router->ifindex);
	/* Switch to new router behind same interface if eager cell 
	 *  switching is used or if the interface is preferred
	 */
	if ((new_router && eager_cell_switching && same_if) ||
	    (mdet_get_if_preference(nrt->ifindex) > 
	     mdet_get_if_preference(curr_router->ifindex))) {
		DEBUG(DBG_INFO, "Switching to new router.");
		goto handoff;
	}
	
	/* No handoff, don't add default route */
	DEBUG(DEBUG_MDETECT, "Ignoring RA");
	spin_unlock(&router_lock);
	return ret;
handoff:
	clean_ncache(nrt, curr_router, same_if);
	set_next_rtr(nrt, curr_router);
	spin_unlock(&router_lock);

	return MIPV6_CHG_RTR;
}	

/* 
 * Called from ndisc.c's router_discovery.
 */

static int mipv6_mn_ra_rcv(struct sk_buff *skb)
{
	int optlen;
	int ifi = ((struct inet6_skb_parm *)skb->cb)->iif;
	struct ra_msg *ra = (struct ra_msg *) skb->h.raw;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct router nrt;
	__u8 * opt = (__u8 *)(ra + 1);

	DEBUG_FUNC();

	memset(&nrt, 0, sizeof(struct router));

	if (ra->icmph.icmp6_home_agent) {
		nrt.flags |= ND_RA_FLAG_HA;
		DEBUG(DBG_DATADUMP, "RA has ND_RA_FLAG_HA up");
	}

	if (ra->icmph.icmp6_addrconf_managed) {
		nrt.flags |= ND_RA_FLAG_MANAGED;
		DEBUG(DBG_DATADUMP, "RA has ND_RA_FLAG_MANAGED up");
	}

	if (ra->icmph.icmp6_addrconf_other) {
		nrt.flags |= ND_RA_FLAG_OTHER;
		DEBUG(DBG_DATADUMP, "RA has ND_RA_FLAG_OTHER up");
	}

	ipv6_addr_copy(&nrt.ll_addr, saddr);
	nrt.ifindex = ifi;
	nrt.lifetime = ntohs(ra->icmph.icmp6_rt_lifetime);

	optlen = (skb->tail - (unsigned char *)ra) - sizeof(struct ra_msg);

	while (optlen > 0) {
		int len = (opt[1] << 3);
		if (len == 0) 
			return MIPV6_IGN_RTR;


		
		if (opt[0] == ND_OPT_PREFIX_INFO) {
			struct prefix_info *pinfo;

			if (len < sizeof(struct prefix_info)) 
				return MIPV6_IGN_RTR;

			pinfo = (struct prefix_info *) opt;

			if (!pinfo->autoconf) {
				/* Autonomous not set according to
                                 * 2462 5.5.3 (a)
				 */
				goto nextopt;
			}

			/* use first prefix with widest scope */
			if (ipv6_addr_any(&nrt.raddr) || 
			    ((ipv6_addr_type(&nrt.raddr) != IPV6_ADDR_UNICAST) &&
			    (ipv6_addr_type(&pinfo->prefix) == IPV6_ADDR_UNICAST))) {
				ipv6_addr_copy(&nrt.raddr, &pinfo->prefix);
				nrt.pfix_len = pinfo->prefix_len;
				if (pinfo->router_address)
					nrt.glob_addr = 1;
				else
					nrt.glob_addr = 0;
				DEBUG(DBG_DATADUMP, "Address of the received "
				      "prefix info option: %x:%x:%x:%x:%x:%x:%x:%x", 
				      NIPV6ADDR(&nrt.raddr));
				DEBUG(DBG_DATADUMP, "the length of the prefix is %d", 
				      nrt.pfix_len);
			}
		}
		if (opt[0] == ND_OPT_SOURCE_LL_ADDR) {
			nrt.link_addr_len = skb->dev->addr_len;
			memcpy(nrt.link_addr, opt + 2, nrt.link_addr_len);
		}
		if (opt[0] == ND_OPT_RTR_ADV_INTERVAL) {			
			nrt.interval = ntohl(*(__u32 *)(opt+4));
			DEBUG(DBG_DATADUMP, 
			      "received router interval option with interval : %d ",
			      nrt.interval / HZ);
			
			if (nrt.interval > MAX_RADV_INTERVAL) {
				nrt.interval = 0;
				DEBUG(DBG_DATADUMP, "but we are using: %d, "
				      "because interval>MAX_RADV_INTERVAL",
				      nrt.interval / HZ);
			}
		}
	nextopt:
		optlen -= len;
		opt += len;
	}

	return mipv6_router_event(&nrt);
}

int __init mipv6_initialize_mdetect(void)
{

	DEBUG_FUNC();

	spin_lock_init(&router_lock);
	spin_lock_init(&ho_lock);
	init_timer(&coa_timer);
	init_timer(&r_timer);
	r_timer.expires = jiffies + HZ;
	add_timer(&r_timer);

	/* Actual HO, also deletes old routes after the addition of new ones 
	   in ndisc */
	MIPV6_SETCALL(mipv6_change_router, mipv6_change_router);

	MIPV6_SETCALL(mipv6_ra_rcv, mipv6_mn_ra_rcv);

	return 0;
}

int __exit mipv6_shutdown_mdetect()
{

	DEBUG_FUNC();

	MIPV6_RESETCALL(mipv6_ra_rcv);
	MIPV6_RESETCALL(mipv6_change_router);
	spin_lock_bh(&router_lock);
	spin_lock(&ho_lock);
	del_timer(&coa_timer);
	del_timer(&r_timer);
	/* Free the memory allocated by router list */
	list_free(&curr_router);
	if (_ho)
		kfree(_ho);
	spin_unlock(&ho_lock);
	spin_unlock_bh(&router_lock);
	return 0;
}
