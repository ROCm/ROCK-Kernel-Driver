/*
 * linux/fs/nfsd/auth.c
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/nfsd/nfsd.h>

#define	CAP_NFSD_MASK (CAP_FS_MASK|CAP_TO_MASK(CAP_SYS_RESOURCE))

int nfsd_setuser(struct svc_rqst *rqstp, struct svc_export *exp)
{
	struct svc_cred	*cred = &rqstp->rq_cred;
	int i;
	int ret;
	uid_t luid = cred->cr_uid;
	gid_t lgid = cred->cr_gid;
	struct group_info *gi = NULL;

	if (exp->ex_flags & NFSEXP_ALLSQUASH) {
		luid = exp->ex_anon_uid;
		lgid = exp->ex_anon_gid;
		gi = groups_alloc(0);
		if (!gi)
			return -ENOMEM;
	} else if (exp->ex_flags & NFSEXP_ROOTSQUASH) {
		if (!cred->cr_uid)
			luid = exp->ex_anon_uid;
		if (!cred->cr_gid)
			lgid = exp->ex_anon_gid;
		gi = groups_alloc(cred->cr_group_info->ngroups);
		if (!gi)
			return -ENOMEM;
		for (i = 0; i < cred->cr_group_info->ngroups; i++) {
			if (!GROUP_AT(cred->cr_group_info, i))
				GROUP_AT(gi, i) = exp->ex_anon_gid;
			else
				GROUP_AT(gi, i) = GROUP_AT(cred->cr_group_info, i);
		}
	}

	if (luid != (uid_t) -1)
		current->fsuid = luid;
	else
		current->fsuid = exp->ex_anon_uid;
	if (lgid != (gid_t) -1)
		current->fsgid = lgid;
	else
		current->fsgid = exp->ex_anon_gid;

	if (gi) {
		ret = set_current_groups(gi);
		put_group_info(gi);
	} else
		ret = set_current_groups(cred->cr_group_info);
	if ((luid)) {
		cap_t(current->cap_effective) &= ~CAP_NFSD_MASK;
	} else {
		cap_t(current->cap_effective) |= (CAP_NFSD_MASK &
						  current->cap_permitted);
	}
	return ret;
}
