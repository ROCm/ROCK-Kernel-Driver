/*
 * net/sched/act_api.c	Packet action API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Jamal Hadi Salim
 *
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#if 1 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

static struct tc_action_ops *act_base = NULL;
static rwlock_t act_mod_lock = RW_LOCK_UNLOCKED;

int tcf_register_action(struct tc_action_ops *act)
{
	struct tc_action_ops *a, **ap;

	write_lock(&act_mod_lock);
	for (ap = &act_base; (a=*ap)!=NULL; ap = &a->next) {
		if (strcmp(act->kind, a->kind) == 0) {
			write_unlock(&act_mod_lock);
			return -EEXIST;
		}
	}
        act->next = NULL;
	*ap = act;

	write_unlock(&act_mod_lock);

	return 0;
}

int tcf_unregister_action(struct tc_action_ops *act)
{
	struct tc_action_ops *a, **ap;
	int err = -ENOENT;

	write_lock(&act_mod_lock);
	for (ap = &act_base; (a=*ap)!=NULL; ap = &a->next) 
		if(a == act)
			break;

	if (a) {
		*ap = a->next;
		a->next = NULL;
		err = 0;
	}
	write_unlock(&act_mod_lock);
	return err;
}

/* lookup by name */
struct tc_action_ops *tc_lookup_action_n(char *kind)
{

	struct tc_action_ops *a = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		for (a = act_base; a; a = a->next) {
			if (strcmp(kind,a->kind) == 0) 
				break;
		}
		read_unlock(&act_mod_lock);
	}

	return a;
}

/* lookup by rtattr */
struct tc_action_ops *tc_lookup_action(struct rtattr *kind)
{

	struct tc_action_ops *a = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		for (a = act_base; a; a = a->next) {

			if (strcmp((char*)RTA_DATA(kind),a->kind) == 0) 
				break;
		}
		read_unlock(&act_mod_lock);
	}

	return a;
}

/* lookup by id */
struct tc_action_ops *tc_lookup_action_id(u32 type)
{
	struct tc_action_ops *a = NULL;

	if (type) {
		read_lock(&act_mod_lock);
		for (a = act_base; a; a = a->next) {
			if (a->type == type) 
				break;
		}
		read_unlock(&act_mod_lock);
	}

	return a;
}

int tcf_action_exec(struct sk_buff *skb,struct tc_action *act)
{

	struct tc_action *a;
	int ret = -1; 

	if (skb->tc_verd & TC_NCLS) {
		skb->tc_verd = CLR_TC_NCLS(skb->tc_verd);
		DPRINTK("(%p)tcf_action_exec: cleared TC_NCLS in %s out %s\n",skb,skb->input_dev?skb->input_dev->name:"xxx",skb->dev->name);
		return TC_ACT_OK;
	}
	while ((a = act) != NULL) {
repeat:
		if (a->ops && a->ops->act) {
			ret = a->ops->act(&skb,a);
				if (TC_MUNGED & skb->tc_verd) {
					/* copied already, allow trampling */
					skb->tc_verd = SET_TC_OK2MUNGE(skb->tc_verd);
					skb->tc_verd = CLR_TC_MUNGED(skb->tc_verd);
				}

			if (ret != TC_ACT_PIPE)
				goto exec_done;
			if (ret == TC_ACT_REPEAT)
				goto repeat;	/* we need a ttl - JHS */

		}
		act = a->next;
	}

exec_done:

	return ret;
}

void tcf_action_destroy(struct tc_action *act, int bind)
{
	struct tc_action *a;

	for (a = act; act; a = act) {
		if (a && a->ops && a->ops->cleanup) {
			DPRINTK("tcf_action_destroy destroying %p next %p\n", a,a->next?a->next:NULL);
			act = act->next;
			a->ops->cleanup(a, bind);
			a->ops = NULL;  
			kfree(a);
		} else { /*FIXME: Remove later - catch insertion bugs*/
			printk("tcf_action_destroy: BUG? destroying NULL ops \n");
			if (a) {
				act = act->next;
				kfree(a);
			} else {
				printk("tcf_action_destroy: BUG? destroying NULL action! \n");
				break;
			}
		}
	}
}

int tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	int err = -EINVAL;


	if ( (NULL == a) || (NULL == a->ops)
	   || (NULL == a->ops->dump) )
		return err;
	return a->ops->dump(skb, a, bind, ref);

}


int tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	int err = -EINVAL;
	unsigned char    *b = skb->tail;
	struct rtattr *r;


	if ( (NULL == a) || (NULL == a->ops)
	   || (NULL == a->ops->dump) || (NULL == a->ops->kind))
		return err;


	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, a->ops->kind);
	if (tcf_action_copy_stats(skb,a))
		goto rtattr_failure;
	r = (struct rtattr*) skb->tail;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);
	if ((err = tcf_action_dump_old(skb, a, bind, ref)) > 0) {
		r->rta_len = skb->tail - (u8*)r;
		return err;
	}


rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;

}

int tcf_action_dump(struct sk_buff *skb, struct tc_action *act, int bind, int ref)
{
	struct tc_action *a;
	int err = -EINVAL;
	unsigned char    *b = skb->tail;
	struct rtattr *r ;

	while ((a = act) != NULL) {
		r = (struct rtattr*) skb->tail;
		act = a->next;
		RTA_PUT(skb, a->order, 0, NULL);
		err = tcf_action_dump_1(skb, a, bind, ref);
		if (0 > err) 
			goto rtattr_failure;

		r->rta_len = skb->tail - (u8*)r;
	}

	return 0;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -err;
	
}

int tcf_action_init_1(struct rtattr *rta, struct rtattr *est, struct tc_action *a, char *name, int ovr, int bind )
{
	struct tc_action_ops *a_o;
	char act_name[4 + IFNAMSIZ + 1];
	struct rtattr *tb[TCA_ACT_MAX+1];
	struct rtattr *kind = NULL;

	int err = -EINVAL;


	if (NULL == name) {
		if (rtattr_parse(tb, TCA_ACT_MAX, RTA_DATA(rta), RTA_PAYLOAD(rta))<0)
			goto err_out;
		kind = tb[TCA_ACT_KIND-1];
		if (NULL != kind) {
			sprintf(act_name, "%s", (char*)RTA_DATA(kind));
			if (RTA_PAYLOAD(kind) >= IFNAMSIZ) {
				printk(" Action %s bad\n", (char*)RTA_DATA(kind));
				goto err_out;
			}

		} else {
			printk("Action bad kind\n");
			goto err_out;
		}
		a_o = tc_lookup_action(kind);
	} else {
		sprintf(act_name, "%s", name);
		DPRINTK("tcf_action_init_1: finding  %s\n",act_name);
		a_o = tc_lookup_action_n(name);
	}
#ifdef CONFIG_KMOD
	if (NULL == a_o) {
		DPRINTK("tcf_action_init_1: trying to load module %s\n",act_name);
		request_module (act_name);
		a_o = tc_lookup_action_n(act_name);
	}

#endif
	if (NULL == a_o) {
		printk("failed to find %s\n",act_name);
		goto err_out;
	}

	if (NULL == a) {
		goto err_out;
	}

	/* backward compatibility for policer */
	if (NULL == name) {
		err = a_o->init(tb[TCA_ACT_OPTIONS-1], est, a, ovr, bind);
		if (0 > err ) {
			return -EINVAL;
		}
	} else {
		err = a_o->init(rta, est, a, ovr, bind);
		if (0 > err ) {
			return -EINVAL;
		}
	}

	DPRINTK("tcf_action_init_1: sucess  %s\n",act_name);

	a->ops = a_o;

	return 0;
err_out:
	return err;
}

int tcf_action_init(struct rtattr *rta, struct rtattr *est, struct tc_action *a, char *name, int ovr , int bind)
{
	struct rtattr *tb[TCA_ACT_MAX_PRIO+1];
	int i;
	struct tc_action *act = a, *a_s = a;

	int err = -EINVAL;

	if (rtattr_parse(tb, TCA_ACT_MAX_PRIO, RTA_DATA(rta), RTA_PAYLOAD(rta))<0)
		return err;

	for (i=0; i < TCA_ACT_MAX_PRIO ; i++) {
		if (tb[i]) {
			if (NULL == act) {
				act = kmalloc(sizeof(*act),GFP_KERNEL);
				if (NULL == act) {
					err = -ENOMEM;
					goto bad_ret;
				}
				 memset(act, 0,sizeof(*act));
			}
			act->next = NULL;
			if (0 > tcf_action_init_1(tb[i],est,act,name,ovr,bind)) {
				printk("Error processing action order %d\n",i);
				return err;
			}

			act->order = i+1;
			if (a_s != act) {
				a_s->next = act;
				a_s = act;
			}
			act = NULL;
		}

	}

	return 0;
bad_ret:
	tcf_action_destroy(a, bind);
	return err;
}

int tcf_action_copy_stats (struct sk_buff *skb,struct tc_action *a)
{
#ifdef CONFIG_KMOD
	/* place holder */
#endif

	if (NULL == a->ops || NULL == a->ops->get_stats)
		return 1;

	return a->ops->get_stats(skb,a);
}


static int
tca_get_fill(struct sk_buff *skb,  struct tc_action *a,
	      u32 pid, u32 seq, unsigned flags, int event, int bind, int ref)
{
	struct tcamsg *t;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;
	struct rtattr *x;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*t));
	nlh->nlmsg_flags = flags;
	t = NLMSG_DATA(nlh);
	t->tca_family = AF_UNSPEC;
	
	x = (struct rtattr*) skb->tail;
	RTA_PUT(skb, TCA_ACT_TAB, 0, NULL);

	if (0 > tcf_action_dump(skb, a, bind, ref)) {
		goto rtattr_failure;
	}

	x->rta_len = skb->tail - (u8*)x;
	
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

rtattr_failure:
nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int act_get_notify(u32 pid, struct nlmsghdr *n,
			   struct tc_action *a, int event)
{
	struct sk_buff *skb;

	int err = 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tca_get_fill(skb, a,  pid, n->nlmsg_seq, 0, event, 0, 0) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	err =  netlink_unicast(rtnl,skb, pid, MSG_DONTWAIT);
	if (err > 0)
		err = 0;
	return err;
}

int tcf_action_get_1(struct rtattr *rta, struct tc_action *a, struct nlmsghdr *n, u32 pid)
{
	struct tc_action_ops *a_o;
	char act_name[4 + IFNAMSIZ + 1];
	struct rtattr *tb[TCA_ACT_MAX+1];
	struct rtattr *kind = NULL;
	int index;

	int err = -EINVAL;

	if (rtattr_parse(tb, TCA_ACT_MAX, RTA_DATA(rta), RTA_PAYLOAD(rta))<0)
		goto err_out;


	kind = tb[TCA_ACT_KIND-1];
	if (NULL != kind) {
		sprintf(act_name, "%s", (char*)RTA_DATA(kind));
		if (RTA_PAYLOAD(kind) >= IFNAMSIZ) {
			printk("tcf_action_get_1: action %s bad\n", (char*)RTA_DATA(kind));
			goto err_out;
		}

	} else {
		printk("tcf_action_get_1: action bad kind\n");
		goto err_out;
	}

	if (tb[TCA_ACT_INDEX - 1]) {
		index = *(int *)RTA_DATA(tb[TCA_ACT_INDEX - 1]);
	} else {
		printk("tcf_action_get_1: index not received\n");
		goto err_out;
	}

	a_o = tc_lookup_action(kind);
#ifdef CONFIG_KMOD
	if (NULL == a_o) {
		request_module (act_name);
		a_o = tc_lookup_action_n(act_name);
	}

#endif
	if (NULL == a_o) {
		printk("failed to find %s\n",act_name);
		goto err_out;
	}

	if (NULL == a) {
		goto err_out;
	}

	a->ops = a_o;

	if (NULL == a_o->lookup || 0 == a_o->lookup(a, index))
		return -EINVAL;

	return 0;
err_out:
	return err;
}

void cleanup_a (struct tc_action *act) 
{
	struct tc_action *a;

	for (a = act; act; a = act) {
		if (a) {
			act = act->next;
			a->ops = NULL;
			a->priv = NULL;
			kfree(a);
		} else {
			printk("cleanup_a: BUG? empty action\n");
		}
	}
}

struct tc_action_ops *get_ao(struct rtattr *kind, struct tc_action *a)
{
	char act_name[4 + IFNAMSIZ + 1];
	struct tc_action_ops *a_o = NULL;

	if (NULL != kind) {
		sprintf(act_name, "%s", (char*)RTA_DATA(kind));
		if (RTA_PAYLOAD(kind) >= IFNAMSIZ) {
			printk("get_ao: action %s bad\n", (char*)RTA_DATA(kind));
			return NULL;
		}

	} else {
		printk("get_ao: action bad kind\n");
		return NULL;
	}

	a_o = tc_lookup_action(kind);
#ifdef CONFIG_KMOD
	if (NULL == a_o) {
		DPRINTK("get_ao: trying to load module %s\n",act_name);
		request_module (act_name);
		a_o = tc_lookup_action_n(act_name);
	}
#endif

	if (NULL == a_o) {
		printk("get_ao: failed to find %s\n",act_name);
		return NULL;
	}

	a->ops = a_o;
	return a_o;
}

struct tc_action *create_a(int i)
{
	struct tc_action *act = NULL;

	act = kmalloc(sizeof(*act),GFP_KERNEL);
	if (NULL == act) { /* grrr .. */
		printk("create_a: failed to alloc! \n");
		return NULL;
	}

	memset(act, 0,sizeof(*act));

	act->order = i;

	return act;
}

int tca_action_flush(struct rtattr *rta, struct nlmsghdr *n, u32 pid)
{
	struct sk_buff *skb;
	unsigned char *b;
	struct nlmsghdr *nlh;
	struct tcamsg *t;
	struct rtattr *x;
	struct rtattr *tb[TCA_ACT_MAX+1];
	struct rtattr *kind = NULL;
	struct tc_action *a = create_a(0);
	int err = -EINVAL;

	if (NULL == a) {
		printk("tca_action_flush: couldnt create tc_action\n");
		return err;
	}

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		printk("tca_action_flush: failed skb alloc\n");
		kfree(a);
		return -ENOBUFS;
	}

	b = (unsigned char *)skb->tail;

	if (rtattr_parse(tb, TCA_ACT_MAX, RTA_DATA(rta), RTA_PAYLOAD(rta))<0) {
		goto err_out;
	}

	kind = tb[TCA_ACT_KIND-1];
	if (NULL == get_ao(kind, a)) {
		goto err_out;
	}

	nlh = NLMSG_PUT(skb, pid,  n->nlmsg_seq, RTM_DELACTION, sizeof (*t));
	t = NLMSG_DATA(nlh);
	t->tca_family = AF_UNSPEC;

	x = (struct rtattr *) skb->tail;
	RTA_PUT(skb, TCA_ACT_TAB, 0, NULL);

	err = a->ops->walk(skb, NULL, RTM_DELACTION, a);
	if (0 > err ) {
		goto rtattr_failure;
	}

	x->rta_len = skb->tail - (u8 *) x;

	nlh->nlmsg_len = skb->tail - b;
	nlh->nlmsg_flags |= NLM_F_ROOT;
	kfree(a);
	err = rtnetlink_send(skb, pid, RTMGRP_TC, n->nlmsg_flags&NLM_F_ECHO);
	if (err > 0)
		return 0;

	return err;


rtattr_failure:
nlmsg_failure:
err_out:
	kfree_skb(skb);
	kfree(a);
	return err;
}

int tca_action_gd(struct rtattr *rta, struct nlmsghdr *n, u32 pid, int event )
{

	int s = 0;
	int i, ret = 0;
	struct tc_action *act = NULL;
	struct rtattr *tb[TCA_ACT_MAX_PRIO+1];
	struct tc_action *a = NULL, *a_s = NULL;

	if (event != RTM_GETACTION  && event != RTM_DELACTION)
		ret = -EINVAL;

	if (rtattr_parse(tb, TCA_ACT_MAX_PRIO, RTA_DATA(rta), RTA_PAYLOAD(rta))<0) {
		ret = -EINVAL;
		goto nlmsg_failure;
	}

	if (event == RTM_DELACTION && n->nlmsg_flags&NLM_F_ROOT) {
		if (NULL != tb[0]  && NULL == tb[1]) {
			return tca_action_flush(tb[0],n,pid);
		}
	}

	for (i=0; i < TCA_ACT_MAX_PRIO ; i++) {

		if (NULL == tb[i])
			break;

		act = create_a(i+1);
		if (NULL != a && a != act) {
			a->next = act;
			a = act;
		} else {
			a = act;
		}

		if (!s) {
			s = 1;
			a_s = a;
		}

		ret = tcf_action_get_1(tb[i],act,n,pid);
		if (ret < 0) {
			printk("tcf_action_get: failed to get! \n");
			ret = -EINVAL;
			goto rtattr_failure;
		}

	}


	if (RTM_GETACTION == event) {
		ret = act_get_notify(pid, n, a_s, event);
	} else { /* delete */

		struct sk_buff *skb;

		skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
		if (!skb) {
			ret = -ENOBUFS;
			goto nlmsg_failure;
		}

		if (tca_get_fill(skb, a_s,  pid, n->nlmsg_seq, 0, event, 0 , 1) <= 0) {
			kfree_skb(skb);
			ret = -EINVAL;
			goto nlmsg_failure;
		}

		/* now do the delete */
		tcf_action_destroy(a_s, 0);

		ret = rtnetlink_send(skb, pid, RTMGRP_TC, n->nlmsg_flags&NLM_F_ECHO);
		if (ret > 0)
			return 0;
		return ret;
	}
rtattr_failure:
nlmsg_failure:
	cleanup_a(a_s);
	return ret;
}


int tcf_add_notify(struct tc_action *a, u32 pid, u32 seq, int event, unsigned flags) 
{
	struct tcamsg *t;
	struct nlmsghdr  *nlh;
	struct sk_buff *skb;
	struct rtattr *x;
	unsigned char *b;


	int err = 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	b = (unsigned char *)skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*t));
	nlh->nlmsg_flags = flags;
	t = NLMSG_DATA(nlh);
	t->tca_family = AF_UNSPEC;
	
	x = (struct rtattr*) skb->tail;
	RTA_PUT(skb, TCA_ACT_TAB, 0, NULL);

	if (0 > tcf_action_dump(skb, a, 0, 0)) {
		goto rtattr_failure;
	}

	x->rta_len = skb->tail - (u8*)x;
	
	nlh->nlmsg_len = skb->tail - b;
	NETLINK_CB(skb).dst_groups = RTMGRP_TC;
	
	err = rtnetlink_send(skb, pid, RTMGRP_TC, flags&NLM_F_ECHO);
	if (err > 0)
		err = 0;

	return err;

rtattr_failure:
nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

	
int tcf_action_add(struct rtattr *rta, struct nlmsghdr *n, u32 pid, int ovr ) 
{
	int ret = 0;
	struct tc_action *act = NULL;
	struct tc_action *a = NULL;
	u32 seq = n->nlmsg_seq;

	act = kmalloc(sizeof(*act),GFP_KERNEL);
	if (NULL == act)
		return -ENOMEM;

	ret = tcf_action_init(rta, NULL,act,NULL,ovr,0);
	/* NOTE: We have an all-or-none model
	 * This means that of any of the actions fail
	 * to update then all are undone.
	 * */
	if (0 > ret) {
		tcf_action_destroy(act, 0);
		goto done;
	}

	/* dump then free all the actions after update; inserted policy
	 * stays intact
	 * */
	ret = tcf_add_notify(act, pid, seq, RTM_NEWACTION, n->nlmsg_flags); 
	for (a = act; act; a = act) {
		if (a) {
			act = act->next;
			a->ops = NULL;
			a->priv = NULL;
			kfree(a);
		} else {
			printk("tcf_action_add: BUG? empty action\n");
		}
	}
done:

	return ret;
}

static int tc_ctl_action(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct rtattr **tca = arg;
	u32 pid = skb ? NETLINK_CB(skb).pid : 0;

	int ret = 0, ovr = 0;

	if (NULL == tca[TCA_ACT_TAB-1]) {
			printk("tc_ctl_action: received NO action attribs\n");
			return -EINVAL;
	}

	/* n->nlmsg_flags&NLM_F_CREATE
	 * */
	switch (n->nlmsg_type) {
	case RTM_NEWACTION:    
		/* we are going to assume all other flags
		 * imply create only if it doesnt exist
		 * Note that CREATE | EXCL implies that
		 * but since we want avoid ambiguity (eg when flags
		 * is zero) then just set this
		 */
		if (n->nlmsg_flags&NLM_F_REPLACE) {
			ovr = 1;
		}
		ret =  tcf_action_add(tca[TCA_ACT_TAB-1], n, pid, ovr);
		break;
	case RTM_DELACTION:
		ret = tca_action_gd(tca[TCA_ACT_TAB-1], n, pid,RTM_DELACTION);
		break;
	case RTM_GETACTION:
		ret = tca_action_gd(tca[TCA_ACT_TAB-1], n, pid,RTM_GETACTION);
		break;
	default:
		printk(" Unknown cmd was detected\n");
		break;
	}

	return ret;
}

char *
find_dump_kind(struct nlmsghdr *n)
{
	struct rtattr *tb1, *tb2[TCA_ACT_MAX_PRIO+1];
	struct rtattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct rtattr *rta[TCAA_MAX + 1];
	struct rtattr *kind = NULL;
	int min_len = NLMSG_LENGTH(sizeof (struct tcamsg));

	int attrlen = n->nlmsg_len - NLMSG_ALIGN(min_len);
	struct rtattr *attr = (void *) n + NLMSG_ALIGN(min_len);

	if (rtattr_parse(rta, TCAA_MAX, attr, attrlen) < 0)
		return NULL;
	tb1 = rta[TCA_ACT_TAB - 1];
	if (NULL == tb1) {
		return NULL;
	}

	if (rtattr_parse(tb, TCA_ACT_MAX_PRIO, RTA_DATA(tb1), NLMSG_ALIGN(RTA_PAYLOAD(tb1))) < 0)
		return NULL;
	if (NULL == tb[0]) 
		return NULL;

	if (rtattr_parse(tb2, TCA_ACT_MAX, RTA_DATA(tb[0]), RTA_PAYLOAD(tb[0]))<0)
		return NULL;
	kind = tb2[TCA_ACT_KIND-1];

	return (char *) RTA_DATA(kind);
}

static int
tc_dump_action(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;
	struct rtattr *x;
	struct tc_action_ops *a_o;
	struct tc_action a;
	int ret = 0;

	struct tcamsg *t = (struct tcamsg *) NLMSG_DATA(cb->nlh);
	char *kind = find_dump_kind(cb->nlh);
	if (NULL == kind) {
		printk("tc_dump_action: action bad kind\n");
		return 0;
	}

	a_o = tc_lookup_action_n(kind);

#ifdef CONFIG_KMOD
	if (NULL == a_o) {
		DPRINTK("tc_dump_action: trying to load module %s\n", kind);
		request_module(kind);
		a_o = tc_lookup_action_n(kind);
	}
#endif
	if (NULL == a_o) {
		printk("failed to find %s\n", kind);
		return 0;
	}

	memset(&a,0,sizeof(struct tc_action));
	a.ops = a_o;

	if (NULL == a_o->walk) {
		printk("tc_dump_action: %s !capable of dumping table\n",kind);
		goto rtattr_failure;
	}

	nlh = NLMSG_PUT(skb, NETLINK_CB(cb->skb).pid,  cb->nlh->nlmsg_seq, cb->nlh->nlmsg_type, sizeof (*t));
	t = NLMSG_DATA(nlh);
	t->tca_family = AF_UNSPEC;

	x = (struct rtattr *) skb->tail;
	RTA_PUT(skb, TCA_ACT_TAB, 0, NULL);

	ret = a_o->walk(skb, cb, RTM_GETACTION, &a);
	if (0 > ret ) {
		goto rtattr_failure;
	}

	if (ret > 0) {
		x->rta_len = skb->tail - (u8 *) x;
		ret = skb->len;
	} else {
		skb_trim(skb, (u8*)x - skb->data);
	}

	nlh->nlmsg_len = skb->tail - b;
	if (NETLINK_CB(cb->skb).pid && ret) 
		nlh->nlmsg_flags |= NLM_F_MULTI;
	return skb->len;

rtattr_failure:
nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return skb->len;
}

static int __init tc_action_init(void)
{
	struct rtnetlink_link *link_p = rtnetlink_links[PF_UNSPEC];

	if (link_p) {
		link_p[RTM_NEWACTION-RTM_BASE].doit = tc_ctl_action;
		link_p[RTM_DELACTION-RTM_BASE].doit = tc_ctl_action;
		link_p[RTM_GETACTION-RTM_BASE].doit = tc_ctl_action;
		link_p[RTM_GETACTION-RTM_BASE].dumpit = tc_dump_action;
	}

	printk("TC classifier action (bugs to netdev@oss.sgi.com cc hadi@cyberus.ca)\n");
	return 0;
}

subsys_initcall(tc_action_init);

EXPORT_SYMBOL(tcf_register_action);
EXPORT_SYMBOL(tcf_unregister_action);
EXPORT_SYMBOL(tcf_action_init_1);
EXPORT_SYMBOL(tcf_action_init);
EXPORT_SYMBOL(tcf_action_destroy);
EXPORT_SYMBOL(tcf_action_exec);
EXPORT_SYMBOL(tcf_action_copy_stats);
EXPORT_SYMBOL(tcf_action_dump);
EXPORT_SYMBOL(tcf_action_dump_1);
EXPORT_SYMBOL(tcf_action_dump_old);
