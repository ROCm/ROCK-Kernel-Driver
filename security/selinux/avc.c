/*
 * Implementation of the kernel access vector cache (AVC).
 *
 * Authors:  Stephen Smalley, <sds@epoch.ncsc.mil>
 *           James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/un.h>
#include <net/af_unix.h>
#include <linux/ip.h>
#include <linux/audit.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include "avc.h"
#include "avc_ss.h"
#ifdef CONFIG_AUDIT
#include "class_to_string.h"
#endif
#include "common_perm_to_string.h"
#include "av_inherit.h"
#include "av_perm_to_string.h"
#include "objsec.h"

#define AVC_CACHE_SLOTS		512
#define AVC_CACHE_MAXNODES	410

struct avc_entry {
	u32			ssid;
	u32			tsid;
	u16			tclass;
	struct av_decision	avd;
	int			used;	/* used recently */
};

struct avc_node {
	struct avc_entry	ae;
	struct avc_node		*next;
};

struct avc_cache {
	struct avc_node	*slots[AVC_CACHE_SLOTS];
	u32		lru_hint;	/* LRU hint for reclaim scan */
	u32		active_nodes;
	u32		latest_notif;	/* latest revocation notification */
};

struct avc_callback_node {
	int (*callback) (u32 event, u32 ssid, u32 tsid,
	                 u16 tclass, u32 perms,
	                 u32 *out_retained);
	u32 events;
	u32 ssid;
	u32 tsid;
	u16 tclass;
	u32 perms;
	struct avc_callback_node *next;
};

static spinlock_t avc_lock = SPIN_LOCK_UNLOCKED;
static struct avc_node *avc_node_freelist = NULL;
static struct avc_cache avc_cache;
static unsigned avc_cache_stats[AVC_NSTATS];
static struct avc_callback_node *avc_callbacks = NULL;

static inline int avc_hash(u32 ssid, u32 tsid, u16 tclass)
{
	return (ssid ^ (tsid<<2) ^ (tclass<<4)) & (AVC_CACHE_SLOTS - 1);
}

/**
 * avc_dump_av - Display an access vector in human-readable form.
 * @tclass: target security class
 * @av: access vector
 */
void avc_dump_av(struct audit_buffer *ab, u16 tclass, u32 av)
{
	char **common_pts = 0;
	u32 common_base = 0;
	int i, i2, perm;

	if (av == 0) {
		audit_log_format(ab, " null");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(av_inherit); i++) {
		if (av_inherit[i].tclass == tclass) {
			common_pts = av_inherit[i].common_pts;
			common_base = av_inherit[i].common_base;
			break;
		}
	}

	audit_log_format(ab, " {");
	i = 0;
	perm = 1;
	while (perm < common_base) {
		if (perm & av)
			audit_log_format(ab, " %s", common_pts[i]);
		i++;
		perm <<= 1;
	}

	while (i < sizeof(av) * 8) {
		if (perm & av) {
			for (i2 = 0; i2 < ARRAY_SIZE(av_perm_to_string); i2++) {
				if ((av_perm_to_string[i2].tclass == tclass) &&
				    (av_perm_to_string[i2].value == perm))
					break;
			}
			if (i2 < ARRAY_SIZE(av_perm_to_string))
				audit_log_format(ab, " %s",
						 av_perm_to_string[i2].name);
		}
		i++;
		perm <<= 1;
	}

	audit_log_format(ab, " }");
}

/**
 * avc_dump_query - Display a SID pair and a class in human-readable form.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 */
void avc_dump_query(struct audit_buffer *ab, u32 ssid, u32 tsid, u16 tclass)
{
	int rc;
	char *scontext;
	u32 scontext_len;

 	rc = security_sid_to_context(ssid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, "ssid=%d", ssid);
	else {
		audit_log_format(ab, "scontext=%s", scontext);
		kfree(scontext);
	}

	rc = security_sid_to_context(tsid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, " tsid=%d", tsid);
	else {
		audit_log_format(ab, " tcontext=%s", scontext);
		kfree(scontext);
	}
	audit_log_format(ab, " tclass=%s", class_to_string[tclass]);
}

/**
 * avc_init - Initialize the AVC.
 *
 * Initialize the access vector cache.
 */
void __init avc_init(void)
{
	struct avc_node	*new;
	int i;

	for (i = 0; i < AVC_NSTATS; i++)
		avc_cache_stats[i] = 0;

	for (i = 0; i < AVC_CACHE_SLOTS; i++)
		avc_cache.slots[i] = 0;
	avc_cache.lru_hint = 0;
	avc_cache.active_nodes = 0;
	avc_cache.latest_notif = 0;

	for (i = 0; i < AVC_CACHE_MAXNODES; i++) {
		new = kmalloc(sizeof(*new), GFP_ATOMIC);
		if (!new) {
			printk(KERN_WARNING "avc:  only able to allocate "
			       "%d entries\n", i);
			break;
		}
		memset(new, 0, sizeof(*new));
		new->next = avc_node_freelist;
		avc_node_freelist = new;
	}

	audit_log(current->audit_context, "AVC INITIALIZED\n");
}

#if 0
static void avc_hash_eval(char *tag)
{
	int i, chain_len, max_chain_len, slots_used;
	struct avc_node *node;
	unsigned long flags;

	spin_lock_irqsave(&avc_lock,flags);

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		node = avc_cache.slots[i];
		if (node) {
			slots_used++;
			chain_len = 0;
			while (node) {
				chain_len++;
				node = node->next;
			}
			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
		}
	}

	spin_unlock_irqrestore(&avc_lock,flags);

	printk(KERN_INFO "\n");
	printk(KERN_INFO "%s avc:  %d entries and %d/%d buckets used, longest "
	       "chain length %d\n", tag, avc_cache.active_nodes, slots_used,
	       AVC_CACHE_SLOTS, max_chain_len);
}
#else
static inline void avc_hash_eval(char *tag)
{ }
#endif

static inline struct avc_node *avc_reclaim_node(void)
{
	struct avc_node *prev, *cur;
	int hvalue, try;

	hvalue = avc_cache.lru_hint;
	for (try = 0; try < 2; try++) {
		do {
			prev = NULL;
			cur = avc_cache.slots[hvalue];
			while (cur) {
				if (!cur->ae.used)
					goto found;

				cur->ae.used = 0;

				prev = cur;
				cur = cur->next;
			}
			hvalue = (hvalue + 1) & (AVC_CACHE_SLOTS - 1);
		} while (hvalue != avc_cache.lru_hint);
	}

	panic("avc_reclaim_node");

found:
	avc_cache.lru_hint = hvalue;

	if (prev == NULL)
		avc_cache.slots[hvalue] = cur->next;
	else
		prev->next = cur->next;

	return cur;
}

static inline struct avc_node *avc_claim_node(u32 ssid,
                                              u32 tsid, u16 tclass)
{
	struct avc_node *new;
	int hvalue;

	hvalue = avc_hash(ssid, tsid, tclass);
	if (avc_node_freelist) {
		new = avc_node_freelist;
		avc_node_freelist = avc_node_freelist->next;
		avc_cache.active_nodes++;
	} else {
		new = avc_reclaim_node();
		if (!new)
			goto out;
	}

	new->ae.used = 1;
	new->ae.ssid = ssid;
	new->ae.tsid = tsid;
	new->ae.tclass = tclass;
	new->next = avc_cache.slots[hvalue];
	avc_cache.slots[hvalue] = new;

out:
	return new;
}

static inline struct avc_node *avc_search_node(u32 ssid, u32 tsid,
                                               u16 tclass, int *probes)
{
	struct avc_node *cur;
	int hvalue;
	int tprobes = 1;

	hvalue = avc_hash(ssid, tsid, tclass);
	cur = avc_cache.slots[hvalue];
	while (cur != NULL &&
	       (ssid != cur->ae.ssid ||
		tclass != cur->ae.tclass ||
		tsid != cur->ae.tsid)) {
		tprobes++;
		cur = cur->next;
	}

	if (cur == NULL) {
		/* cache miss */
		goto out;
	}

	/* cache hit */
	if (probes)
		*probes = tprobes;

	cur->ae.used = 1;

out:
	return cur;
}

/**
 * avc_lookup - Look up an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @aeref:  AVC entry reference
 *
 * Look up an AVC entry that is valid for the
 * @requested permissions between the SID pair
 * (@ssid, @tsid), interpreting the permissions
 * based on @tclass.  If a valid AVC entry exists,
 * then this function updates @aeref to refer to the
 * entry and returns %0. Otherwise, this function
 * returns -%ENOENT.
 */
int avc_lookup(u32 ssid, u32 tsid, u16 tclass,
               u32 requested, struct avc_entry_ref *aeref)
{
	struct avc_node *node;
	int probes, rc = 0;

	avc_cache_stats_incr(AVC_CAV_LOOKUPS);
	node = avc_search_node(ssid, tsid, tclass,&probes);

	if (node && ((node->ae.avd.decided & requested) == requested)) {
		avc_cache_stats_incr(AVC_CAV_HITS);
		avc_cache_stats_add(AVC_CAV_PROBES,probes);
		aeref->ae = &node->ae;
		goto out;
	}

	avc_cache_stats_incr(AVC_CAV_MISSES);
	rc = -ENOENT;
out:
	return rc;
}

/**
 * avc_insert - Insert an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @ae: AVC entry
 * @aeref:  AVC entry reference
 *
 * Insert an AVC entry for the SID pair
 * (@ssid, @tsid) and class @tclass.
 * The access vectors and the sequence number are
 * normally provided by the security server in
 * response to a security_compute_av() call.  If the
 * sequence number @ae->avd.seqno is not less than the latest
 * revocation notification, then the function copies
 * the access vectors into a cache entry, updates
 * @aeref to refer to the entry, and returns %0.
 * Otherwise, this function returns -%EAGAIN.
 */
int avc_insert(u32 ssid, u32 tsid, u16 tclass,
               struct avc_entry *ae, struct avc_entry_ref *aeref)
{
	struct avc_node *node;
	int rc = 0;

	if (ae->avd.seqno < avc_cache.latest_notif) {
		printk(KERN_WARNING "avc:  seqno %d < latest_notif %d\n",
		       ae->avd.seqno, avc_cache.latest_notif);
		rc = -EAGAIN;
		goto out;
	}

	node = avc_claim_node(ssid, tsid, tclass);
	if (!node) {
		rc = -ENOMEM;
		goto out;
	}

	node->ae.avd.allowed = ae->avd.allowed;
	node->ae.avd.decided = ae->avd.decided;
	node->ae.avd.auditallow = ae->avd.auditallow;
	node->ae.avd.auditdeny = ae->avd.auditdeny;
	node->ae.avd.seqno = ae->avd.seqno;
	aeref->ae = &node->ae;
out:
	return rc;
}

static inline void avc_print_ipv6_addr(struct audit_buffer *ab,
				       struct in6_addr *addr, u16 port,
				       char *name1, char *name2)
{
	if (!ipv6_addr_any(addr))
		audit_log_format(ab, " %s=%04x:%04x:%04x:%04x:%04x:"
				 "%04x:%04x:%04x", name1, NIP6(*addr));
	if (port)
		audit_log_format(ab, " %s=%d", name2, ntohs(port));
}

static inline void avc_print_ipv4_addr(struct audit_buffer *ab, u32 addr,
				       u16 port, char *name1, char *name2)
{
	if (addr)
		audit_log_format(ab, " %s=%d.%d.%d.%d", name1, NIPQUAD(addr));
	if (port)
		audit_log_format(ab, " %s=%d", name2, ntohs(port));
}

/**
 * avc_audit - Audit the granting or denial of permissions.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions
 * @avd: access vector decisions
 * @result: result from avc_has_perm_noaudit
 * @a:  auxiliary audit data
 *
 * Audit the granting or denial of permissions in accordance
 * with the policy.  This function is typically called by
 * avc_has_perm() after a permission check, but can also be
 * called directly by callers who use avc_has_perm_noaudit()
 * in order to separate the permission check from the auditing.
 * For example, this separation is useful when the permission check must
 * be performed under a lock, to allow the lock to be released
 * before calling the auditing code.
 */
void avc_audit(u32 ssid, u32 tsid,
               u16 tclass, u32 requested,
               struct av_decision *avd, int result, struct avc_audit_data *a)
{
	struct task_struct *tsk = current;
	struct inode *inode = NULL;
	u32 denied, audited;
	struct audit_buffer *ab;

	denied = requested & ~avd->allowed;
	if (denied) {
		audited = denied;
		if (!(audited & avd->auditdeny))
			return;
	} else if (result) {
		audited = denied = requested;
        } else {
		audited = requested;
		if (!(audited & avd->auditallow))
			return;
	}

	ab = audit_log_start(current->audit_context);
	if (!ab)
		return;		/* audit_panic has been called */
	audit_log_format(ab, "avc:  %s ", denied ? "denied" : "granted");
	avc_dump_av(ab, tclass,audited);
	audit_log_format(ab, " for ");
	if (a && a->tsk)
		tsk = a->tsk;
	if (tsk && tsk->pid) {
		struct mm_struct *mm;
		struct vm_area_struct *vma;
		audit_log_format(ab, " pid=%d", tsk->pid);
		if (tsk == current)
			mm = current->mm;
		else
			mm = get_task_mm(tsk);
		if (mm) {
			if (down_read_trylock(&mm->mmap_sem)) {
				vma = mm->mmap;
				while (vma) {
					if ((vma->vm_flags & VM_EXECUTABLE) &&
					    vma->vm_file) {
						audit_log_d_path(ab, "exe=",
							vma->vm_file->f_dentry,
							vma->vm_file->f_vfsmnt);
						break;
					}
					vma = vma->vm_next;
				}
				up_read(&mm->mmap_sem);
			}
			if (tsk != current)
				mmput(mm);
		} else {
			audit_log_format(ab, " comm=%s", tsk->comm);
		}
	}
	if (a) {
		switch (a->type) {
		case AVC_AUDIT_DATA_IPC:
			audit_log_format(ab, " key=%d", a->u.ipc_id);
			break;
		case AVC_AUDIT_DATA_CAP:
			audit_log_format(ab, " capability=%d", a->u.cap);
			break;
		case AVC_AUDIT_DATA_FS:
			if (a->u.fs.dentry) {
				struct dentry *dentry = a->u.fs.dentry;
				if (a->u.fs.mnt) {
					audit_log_d_path(ab, "path=", dentry,
							a->u.fs.mnt);
				} else {
					audit_log_format(ab, " name=%s",
							 dentry->d_name.name);
				}
				inode = dentry->d_inode;
			} else if (a->u.fs.inode) {
				struct dentry *dentry;
				inode = a->u.fs.inode;
				dentry = d_find_alias(inode);
				if (dentry) {
					audit_log_format(ab, " name=%s",
							 dentry->d_name.name);
					dput(dentry);
				}
			}
			if (inode)
				audit_log_format(ab, " dev=%s ino=%ld",
						 inode->i_sb->s_id,
						 inode->i_ino);
			break;
		case AVC_AUDIT_DATA_NET:
			if (a->u.net.sk) {
				struct sock *sk = a->u.net.sk;
				struct unix_sock *u;
				int len = 0;
				char *p = NULL;

				switch (sk->sk_family) {
				case AF_INET: {
					struct inet_opt *inet = inet_sk(sk);

					avc_print_ipv4_addr(ab, inet->rcv_saddr,
							    inet->sport,
							    "laddr", "lport");
					avc_print_ipv4_addr(ab, inet->daddr,
							    inet->dport,
							    "faddr", "fport");
					break;
				}
				case AF_INET6: {
					struct inet_opt *inet = inet_sk(sk);
					struct ipv6_pinfo *inet6 = inet6_sk(sk);

					avc_print_ipv6_addr(ab, &inet6->rcv_saddr,
							    inet->sport,
							    "laddr", "lport");
					avc_print_ipv6_addr(ab, &inet6->daddr,
							    inet->dport,
							    "faddr", "fport");
					break;
				}
				case AF_UNIX:
					u = unix_sk(sk);
					if (u->dentry) {
						audit_log_d_path(ab, "path=",
							u->dentry, u->mnt);
						break;
					}
					if (!u->addr)
						break;
					len = u->addr->len-sizeof(short);
					p = &u->addr->name->sun_path[0];
					if (*p)
						audit_log_format(ab,
							"path=%*.*s", len,
							len, p);
					else
						audit_log_format(ab,
							"path=@%*.*s", len-1,
							len-1, p+1);
					break;
				}
			}
			
			switch (a->u.net.family) {
			case AF_INET:
				avc_print_ipv4_addr(ab, a->u.net.v4info.saddr,
						    a->u.net.sport,
						    "saddr", "src");
				avc_print_ipv4_addr(ab, a->u.net.v4info.daddr,
						    a->u.net.dport,
						    "daddr", "dest");
				break;
			case AF_INET6:
				avc_print_ipv6_addr(ab, &a->u.net.v6info.saddr,
						    a->u.net.sport,
						    "saddr", "src");
				avc_print_ipv6_addr(ab, &a->u.net.v6info.daddr,
						    a->u.net.dport,
						    "daddr", "dest");
				break;
			}
			if (a->u.net.netif)
				audit_log_format(ab, " netif=%s",
					a->u.net.netif);
			break;
		}
	}
	audit_log_format(ab, " ");
	avc_dump_query(ab, ssid, tsid, tclass);
	audit_log_end(ab);
}

/**
 * avc_add_callback - Register a callback for security events.
 * @callback: callback function
 * @events: security events
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions
 *
 * Register a callback function for events in the set @events
 * related to the SID pair (@ssid, @tsid) and
 * and the permissions @perms, interpreting
 * @perms based on @tclass.  Returns %0 on success or
 * -%ENOMEM if insufficient memory exists to add the callback.
 */
int avc_add_callback(int (*callback)(u32 event, u32 ssid, u32 tsid,
                                     u16 tclass, u32 perms,
                                     u32 *out_retained),
                     u32 events, u32 ssid, u32 tsid,
                     u16 tclass, u32 perms)
{
	struct avc_callback_node *c;
	int rc = 0;

	c = kmalloc(sizeof(*c), GFP_ATOMIC);
	if (!c) {
		rc = -ENOMEM;
		goto out;
	}

	c->callback = callback;
	c->events = events;
	c->ssid = ssid;
	c->tsid = tsid;
	c->perms = perms;
	c->next = avc_callbacks;
	avc_callbacks = c;
out:
	return rc;
}

static inline int avc_sidcmp(u32 x, u32 y)
{
	return (x == y || x == SECSID_WILD || y == SECSID_WILD);
}

static inline void avc_update_node(u32 event, struct avc_node *node, u32 perms)
{
	switch (event) {
	case AVC_CALLBACK_GRANT:
		node->ae.avd.allowed |= perms;
		break;
	case AVC_CALLBACK_TRY_REVOKE:
	case AVC_CALLBACK_REVOKE:
		node->ae.avd.allowed &= ~perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_ENABLE:
		node->ae.avd.auditallow |= perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_DISABLE:
		node->ae.avd.auditallow &= ~perms;
		break;
	case AVC_CALLBACK_AUDITDENY_ENABLE:
		node->ae.avd.auditdeny |= perms;
		break;
	case AVC_CALLBACK_AUDITDENY_DISABLE:
		node->ae.avd.auditdeny &= ~perms;
		break;
	}
}

static int avc_update_cache(u32 event, u32 ssid, u32 tsid,
                            u16 tclass, u32 perms)
{
	struct avc_node *node;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&avc_lock,flags);

	if (ssid == SECSID_WILD || tsid == SECSID_WILD) {
		/* apply to all matching nodes */
		for (i = 0; i < AVC_CACHE_SLOTS; i++) {
			for (node = avc_cache.slots[i]; node;
			     node = node->next) {
				if (avc_sidcmp(ssid, node->ae.ssid) &&
				    avc_sidcmp(tsid, node->ae.tsid) &&
				    tclass == node->ae.tclass) {
					avc_update_node(event,node,perms);
				}
			}
		}
	} else {
		/* apply to one node */
		node = avc_search_node(ssid, tsid, tclass, 0);
		if (node) {
			avc_update_node(event,node,perms);
		}
	}

	spin_unlock_irqrestore(&avc_lock,flags);

	return 0;
}

static int avc_control(u32 event, u32 ssid, u32 tsid,
                       u16 tclass, u32 perms,
                       u32 seqno, u32 *out_retained)
{
	struct avc_callback_node *c;
	u32 tretained = 0, cretained = 0;
	int rc = 0;
	unsigned long flags;

	/*
	 * try_revoke only removes permissions from the cache
	 * state if they are not retained by the object manager.
	 * Hence, try_revoke must wait until after the callbacks have
	 * been invoked to update the cache state.
	 */
	if (event != AVC_CALLBACK_TRY_REVOKE)
		avc_update_cache(event,ssid,tsid,tclass,perms);

	for (c = avc_callbacks; c; c = c->next)
	{
		if ((c->events & event) &&
		    avc_sidcmp(c->ssid, ssid) &&
		    avc_sidcmp(c->tsid, tsid) &&
		    c->tclass == tclass &&
		    (c->perms & perms)) {
			cretained = 0;
			rc = c->callback(event, ssid, tsid, tclass,
					 (c->perms & perms),
					 &cretained);
			if (rc)
				goto out;
			tretained |= cretained;
		}
	}

	if (event == AVC_CALLBACK_TRY_REVOKE) {
		/* revoke any unretained permissions */
		perms &= ~tretained;
		avc_update_cache(event,ssid,tsid,tclass,perms);
		*out_retained = tretained;
	}

	spin_lock_irqsave(&avc_lock,flags);
	if (seqno > avc_cache.latest_notif)
		avc_cache.latest_notif = seqno;
	spin_unlock_irqrestore(&avc_lock,flags);

out:
	return rc;
}

/**
 * avc_ss_grant - Grant previously denied permissions.
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions to grant
 * @seqno: policy sequence number
 */
int avc_ss_grant(u32 ssid, u32 tsid, u16 tclass,
                 u32 perms, u32 seqno)
{
	return avc_control(AVC_CALLBACK_GRANT,
			   ssid, tsid, tclass, perms, seqno, 0);
}

/**
 * avc_ss_try_revoke - Try to revoke previously granted permissions.
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions to grant
 * @seqno: policy sequence number
 * @out_retained: subset of @perms that are retained
 *
 * Try to revoke previously granted permissions, but
 * only if they are not retained as migrated permissions.
 * Return the subset of permissions that are retained via @out_retained.
 */
int avc_ss_try_revoke(u32 ssid, u32 tsid, u16 tclass,
                      u32 perms, u32 seqno, u32 *out_retained)
{
	return avc_control(AVC_CALLBACK_TRY_REVOKE,
			   ssid, tsid, tclass, perms, seqno, out_retained);
}

/**
 * avc_ss_revoke - Revoke previously granted permissions.
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions to grant
 * @seqno: policy sequence number
 *
 * Revoke previously granted permissions, even if
 * they are retained as migrated permissions.
 */
int avc_ss_revoke(u32 ssid, u32 tsid, u16 tclass,
                  u32 perms, u32 seqno)
{
	return avc_control(AVC_CALLBACK_REVOKE,
			   ssid, tsid, tclass, perms, seqno, 0);
}

/**
 * avc_ss_reset - Flush the cache and revalidate migrated permissions.
 * @seqno: policy sequence number
 */
int avc_ss_reset(u32 seqno)
{
	struct avc_callback_node *c;
	int i, rc = 0;
	struct avc_node *node, *tmp;
	unsigned long flags;

	avc_hash_eval("reset");

	spin_lock_irqsave(&avc_lock,flags);

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		node = avc_cache.slots[i];
		while (node) {
			tmp = node;
			node = node->next;
			tmp->ae.ssid = tmp->ae.tsid = SECSID_NULL;
			tmp->ae.tclass = SECCLASS_NULL;
			tmp->ae.avd.allowed = tmp->ae.avd.decided = 0;
			tmp->ae.avd.auditallow = tmp->ae.avd.auditdeny = 0;
			tmp->ae.used = 0;
			tmp->next = avc_node_freelist;
			avc_node_freelist = tmp;
			avc_cache.active_nodes--;
		}
		avc_cache.slots[i] = 0;
	}
	avc_cache.lru_hint = 0;

	spin_unlock_irqrestore(&avc_lock,flags);

	for (i = 0; i < AVC_NSTATS; i++)
		avc_cache_stats[i] = 0;

	for (c = avc_callbacks; c; c = c->next) {
		if (c->events & AVC_CALLBACK_RESET) {
			rc = c->callback(AVC_CALLBACK_RESET,
					 0, 0, 0, 0, 0);
			if (rc)
				goto out;
		}
	}

	spin_lock_irqsave(&avc_lock,flags);
	if (seqno > avc_cache.latest_notif)
		avc_cache.latest_notif = seqno;
	spin_unlock_irqrestore(&avc_lock,flags);
out:
	return rc;
}

/**
 * avc_ss_set_auditallow - Enable or disable auditing of granted permissions.
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions to grant
 * @seqno: policy sequence number
 * @enable: enable flag.
 */
int avc_ss_set_auditallow(u32 ssid, u32 tsid, u16 tclass,
                          u32 perms, u32 seqno, u32 enable)
{
	if (enable)
		return avc_control(AVC_CALLBACK_AUDITALLOW_ENABLE,
				   ssid, tsid, tclass, perms, seqno, 0);
	else
		return avc_control(AVC_CALLBACK_AUDITALLOW_DISABLE,
				   ssid, tsid, tclass, perms, seqno, 0);
}

/**
 * avc_ss_set_auditdeny - Enable or disable auditing of denied permissions.
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions to grant
 * @seqno: policy sequence number
 * @enable: enable flag.
 */
int avc_ss_set_auditdeny(u32 ssid, u32 tsid, u16 tclass,
                         u32 perms, u32 seqno, u32 enable)
{
	if (enable)
		return avc_control(AVC_CALLBACK_AUDITDENY_ENABLE,
				   ssid, tsid, tclass, perms, seqno, 0);
	else
		return avc_control(AVC_CALLBACK_AUDITDENY_DISABLE,
				   ssid, tsid, tclass, perms, seqno, 0);
}

/**
 * avc_has_perm_noaudit - Check permissions but perform no auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @aeref:  AVC entry reference
 * @avd: access vector decisions
 *
 * Check the AVC to determine whether the @requested permissions are granted
 * for the SID pair (@ssid, @tsid), interpreting the permissions
 * based on @tclass, and call the security server on a cache miss to obtain
 * a new decision and add it to the cache.  Update @aeref to refer to an AVC
 * entry with the resulting decisions, and return a copy of the decisions
 * in @avd.  Return %0 if all @requested permissions are granted,
 * -%EACCES if any permissions are denied, or another -errno upon
 * other errors.  This function is typically called by avc_has_perm(),
 * but may also be called directly to separate permission checking from
 * auditing, e.g. in cases where a lock must be held for the check but
 * should be released for the auditing.
 */
int avc_has_perm_noaudit(u32 ssid, u32 tsid,
                         u16 tclass, u32 requested,
                         struct avc_entry_ref *aeref, struct av_decision *avd)
{
	struct avc_entry *ae;
	int rc = 0;
	unsigned long flags;
	struct avc_entry entry;
	u32 denied;
	struct avc_entry_ref ref;

	if (!aeref) {
		avc_entry_ref_init(&ref);
		aeref = &ref;
	}

	spin_lock_irqsave(&avc_lock, flags);
	avc_cache_stats_incr(AVC_ENTRY_LOOKUPS);
	ae = aeref->ae;
	if (ae) {
		if (ae->ssid == ssid &&
		    ae->tsid == tsid &&
		    ae->tclass == tclass &&
		    ((ae->avd.decided & requested) == requested)) {
			avc_cache_stats_incr(AVC_ENTRY_HITS);
			ae->used = 1;
		} else {
			avc_cache_stats_incr(AVC_ENTRY_DISCARDS);
			ae = 0;
		}
	}

	if (!ae) {
		avc_cache_stats_incr(AVC_ENTRY_MISSES);
		rc = avc_lookup(ssid, tsid, tclass, requested, aeref);
		if (rc) {
			spin_unlock_irqrestore(&avc_lock,flags);
			rc = security_compute_av(ssid,tsid,tclass,requested,&entry.avd);
			if (rc)
				goto out;
			spin_lock_irqsave(&avc_lock, flags);
			rc = avc_insert(ssid,tsid,tclass,&entry,aeref);
			if (rc) {
				spin_unlock_irqrestore(&avc_lock,flags);
				goto out;
			}
		}
		ae = aeref->ae;
	}

	if (avd)
		memcpy(avd, &ae->avd, sizeof(*avd));

	denied = requested & ~(ae->avd.allowed);

	if (!requested || denied) {
		if (selinux_enforcing) {
			spin_unlock_irqrestore(&avc_lock,flags);
			rc = -EACCES;
			goto out;
		} else {
			ae->avd.allowed |= requested;
			spin_unlock_irqrestore(&avc_lock,flags);
			goto out;
		}
	}

	spin_unlock_irqrestore(&avc_lock,flags);
out:
	return rc;
}

/**
 * avc_has_perm - Check permissions and perform any appropriate auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @aeref:  AVC entry reference
 * @auditdata: auxiliary audit data
 *
 * Check the AVC to determine whether the @requested permissions are granted
 * for the SID pair (@ssid, @tsid), interpreting the permissions
 * based on @tclass, and call the security server on a cache miss to obtain
 * a new decision and add it to the cache.  Update @aeref to refer to an AVC
 * entry with the resulting decisions.  Audit the granting or denial of
 * permissions in accordance with the policy.  Return %0 if all @requested
 * permissions are granted, -%EACCES if any permissions are denied, or
 * another -errno upon other errors.
 */
int avc_has_perm(u32 ssid, u32 tsid, u16 tclass,
                 u32 requested, struct avc_entry_ref *aeref,
                 struct avc_audit_data *auditdata)
{
	struct av_decision avd;
	int rc;

	rc = avc_has_perm_noaudit(ssid, tsid, tclass, requested, aeref, &avd);
	avc_audit(ssid, tsid, tclass, requested, &avd, rc, auditdata);
	return rc;
}
