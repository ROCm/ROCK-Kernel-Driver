/*
 *  fs/nfs4acl/acl.c
 *
 *  Common NFSv4 ACL handling code.
 *
 *  Copyright (c) 2002, 2003 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
 *  Jeff Sedlak <jsedlak@umich.edu>
 *  J. Bruce Fields <bfields@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/nfs_fs.h>
#include <linux/nfs4.h>
#include <linux/nfs4_acl.h>

struct nfs4_acl *
nfs4_acl_new(void)
{
	struct nfs4_acl *acl;

	if ((acl = kmalloc(sizeof(*acl), GFP_KERNEL)) == NULL)
		return NULL;

	acl->naces = 0;
	INIT_LIST_HEAD(&acl->ace_head);

	return acl;
}

void
nfs4_acl_free(struct nfs4_acl *acl)
{
	struct list_head *h;
	struct nfs4_ace *ace;

	if (!acl)
		return;

	while (!list_empty(&acl->ace_head)) {
		h = acl->ace_head.next;
		list_del(h);
		ace = list_entry(h, struct nfs4_ace, l_ace);
		kfree(ace);
	}

	kfree(acl);

	return;
}

int
nfs4_acl_add_ace(struct nfs4_acl *acl, u32 type, u32 flag, u32 access_mask,
		int whotype, uid_t who)
{
	struct nfs4_ace *ace;

	if ((ace = kmalloc(sizeof(*ace), GFP_KERNEL)) == NULL)
		return -1;

	ace->type = type;
	ace->flag = flag;
	ace->access_mask = access_mask;
	ace->whotype = whotype;
	ace->who = who;

	list_add_tail(&ace->l_ace, &acl->ace_head);
	acl->naces++;

	return 0;
}

static struct {
	char *string;
	int   stringlen;
	int type;
} s2t_map[] = {
	{
		.string    = "OWNER@",
		.stringlen = sizeof("OWNER@") - 1,
		.type      = NFS4_ACL_WHO_OWNER,
	},
	{
		.string    = "GROUP@",
		.stringlen = sizeof("GROUP@") - 1,
		.type      = NFS4_ACL_WHO_GROUP,
	},
	{
		.string    = "EVERYONE@",
		.stringlen = sizeof("EVERYONE@") - 1,
		.type      = NFS4_ACL_WHO_EVERYONE,
	},
};

int
nfs4_acl_get_whotype(char *p, u32 len)
{
	int i;

	for (i=0; i < sizeof(s2t_map) / sizeof(*s2t_map); i++) {
		if (s2t_map[i].stringlen == len &&
				0 == memcmp(s2t_map[i].string, p, len))
			return s2t_map[i].type;
	}
	return NFS4_ACL_WHO_NAMED;
}

int
nfs4_acl_write_who(int who, char *p)
{
	int i;

	for (i=0; i < sizeof(s2t_map) / sizeof(*s2t_map); i++) {
		if (s2t_map[i].type == who) {
			memcpy(p, s2t_map[i].string, s2t_map[i].stringlen);
			return s2t_map[i].stringlen;
		}
	}
	BUG();
	return -1;
}

static inline int
match_who(struct nfs4_ace *ace, uid_t owner, gid_t group, uid_t who)
{
	switch (ace->whotype) {
		case NFS4_ACL_WHO_NAMED:
			return who == ace->who;
		case NFS4_ACL_WHO_OWNER:
			return who == owner;
		case NFS4_ACL_WHO_GROUP:
			return who == group;
		case NFS4_ACL_WHO_EVERYONE:
			return 1;
		default:
			return 0;
	}
}

/* 0 = granted, -EACCES = denied; mask is an nfsv4 mask, not mode bits */
int
nfs4_acl_permission(struct nfs4_acl *acl, uid_t owner, gid_t group,
			uid_t who, u32 mask)
{
	struct nfs4_ace *ace;
	u32 allowed = 0;

	list_for_each_entry(ace, &acl->ace_head, l_ace) {
		if (!match_who(ace, group, owner, who))
			continue;
		switch (ace->type) {
			case NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE:
				allowed |= ace->access_mask;
				if ((allowed & mask) == mask)
					return 0;
				break;
			case NFS4_ACE_ACCESS_DENIED_ACE_TYPE:
				if (ace->access_mask & mask)
					return -EACCES;
				break;
		}
	}
	return -EACCES;
}

EXPORT_SYMBOL(nfs4_acl_new);
EXPORT_SYMBOL(nfs4_acl_free);
EXPORT_SYMBOL(nfs4_acl_add_ace);
EXPORT_SYMBOL(nfs4_acl_get_whotype);
EXPORT_SYMBOL(nfs4_acl_write_who);
EXPORT_SYMBOL(nfs4_acl_permission);
