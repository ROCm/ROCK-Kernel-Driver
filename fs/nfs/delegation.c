/*
 * linux/fs/nfs/delegation.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFS file delegation management
 *
 */
#include <linux/config.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "delegation.h"

static struct nfs_delegation *nfs_alloc_delegation(void)
{
	return (struct nfs_delegation *)kmalloc(sizeof(struct nfs_delegation), GFP_KERNEL);
}

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	if (delegation->cred)
		put_rpccred(delegation->cred);
	kfree(delegation);
}

/*
 * Set up a delegation on an inode
 */
int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs4_client *clp = NFS_SERVER(inode)->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int status = 0;

	delegation = nfs_alloc_delegation();
	if (delegation == NULL)
		return -ENOMEM;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	delegation->cred = get_rpccred(cred);
	delegation->inode = inode;

	spin_lock(&clp->cl_lock);
	if (nfsi->delegation == NULL) {
		list_add(&delegation->super_list, &clp->cl_delegations);
		nfsi->delegation = delegation;
		delegation = NULL;
	} else {
		if (memcmp(&delegation->stateid, &nfsi->delegation->stateid,
					sizeof(delegation->stateid)) != 0 ||
				delegation->type != nfsi->delegation->type) {
			printk("%s: server %u.%u.%u.%u, handed out a duplicate delegation!\n",
					__FUNCTION__, NIPQUAD(clp->cl_addr));
			status = -EIO;
		}
	}
	spin_unlock(&clp->cl_lock);
	if (delegation != NULL)
		kfree(delegation);
	return status;
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation)
{
	int res = 0;

	__nfs_revalidate_inode(NFS_SERVER(inode), inode);

	res = nfs4_proc_delegreturn(inode, delegation->cred, &delegation->stateid);
	nfs_free_delegation(delegation);
	return res;
}

/* Sync all data to disk upon delegation return */
static void nfs_msync_inode(struct inode *inode)
{
	filemap_fdatawrite(inode->i_mapping);
	nfs_wb_all(inode);
	filemap_fdatawait(inode->i_mapping);
}

/*
 * Basic procedure for returning a delegation to the server
 */
int nfs_inode_return_delegation(struct inode *inode)
{
	struct nfs4_client *clp = NFS_SERVER(inode)->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int res = 0;

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	/* Guard against new delegated open calls */
	down_write(&nfsi->rwsem);
	spin_lock(&clp->cl_lock);
	delegation = nfsi->delegation;
	if (delegation != NULL) {
		list_del_init(&delegation->super_list);
		nfsi->delegation = NULL;
	}
	spin_unlock(&clp->cl_lock);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	if (delegation != NULL)
		res = nfs_do_return_delegation(inode, delegation);
	return res;
}

/*
 * Return all delegations associated to a super block
 */
void nfs_return_all_delegations(struct super_block *sb)
{
	struct nfs4_client *clp = NFS_SB(sb)->nfs4_state;
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	spin_lock(&clp->cl_lock);
	list_for_each_entry(delegation, &clp->cl_delegations, super_list) {
		if (delegation->inode->i_sb != sb)
			continue;
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_unlock(&clp->cl_lock);
		nfs_inode_return_delegation(inode);
		iput(inode);
		goto restart;
	}
	spin_unlock(&clp->cl_lock);
}

struct recall_threadargs {
	struct inode *inode;
	struct nfs4_client *clp;
	const nfs4_stateid *stateid;

	struct completion started;
	int result;
};

static int recall_thread(void *data)
{
	struct recall_threadargs *args = (struct recall_threadargs *)data;
	struct inode *inode = igrab(args->inode);
	struct nfs4_client *clp = NFS_SERVER(inode)->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	daemonize("nfsv4-delegreturn");

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	down_write(&nfsi->rwsem);
	spin_lock(&clp->cl_lock);
	delegation = nfsi->delegation;
	if (delegation != NULL && memcmp(delegation->stateid.data,
				args->stateid->data,
				sizeof(delegation->stateid.data)) == 0) {
		list_del_init(&delegation->super_list);
		nfsi->delegation = NULL;
		args->result = 0;
	} else {
		delegation = NULL;
		args->result = -ENOENT;
	}
	spin_unlock(&clp->cl_lock);
	complete(&args->started);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	if (delegation != NULL)
		nfs_do_return_delegation(inode, delegation);
	iput(inode);
	module_put_and_exit(0);
}

/*
 * Asynchronous delegation recall!
 */
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid)
{
	struct recall_threadargs data = {
		.inode = inode,
		.stateid = stateid,
	};
	int status;

	init_completion(&data.started);
	__module_get(THIS_MODULE);
	status = kernel_thread(recall_thread, &data, CLONE_KERNEL);
	if (status < 0)
		goto out_module_put;
	wait_for_completion(&data.started);
	return data.result;
out_module_put:
	module_put(THIS_MODULE);
	return status;
}

/*
 * Retrieve the inode associated with a delegation
 */
struct inode *nfs_delegation_find_inode(struct nfs4_client *clp, const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct inode *res = NULL;
	spin_lock(&clp->cl_lock);
	list_for_each_entry(delegation, &clp->cl_delegations, super_list) {
		if (nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
			break;
		}
	}
	spin_unlock(&clp->cl_lock);
	return res;
}
