/*
 *  fs/nfs/nfs4proc.c
 *
 *  Client-side procedure declarations for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
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

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>

#define NFSDBG_FACILITY		NFSDBG_PROC

#define NFS4_POLL_RETRY_TIME	(15*HZ)

#define GET_OP(cp,name)		&cp->ops[cp->req_nops].u.name
#define OPNUM(cp)		cp->ops[cp->req_nops].opnum

static int nfs4_do_fsinfo(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *);
static int nfs4_async_handle_error(struct rpc_task *, struct nfs_server *);
extern u32 *nfs4_decode_dirent(u32 *p, struct nfs_entry *entry, int plus);
extern struct rpc_procinfo nfs4_procedures[];

extern nfs4_stateid zero_stateid;

/* Prevent leaks of NFSv4 errors into userland */
static inline int nfs4_map_errors(int err)
{
	if (err < -1000) {
		printk(KERN_WARNING "%s could not handle NFSv4 error %d\n",
				__FUNCTION__, -err);
		return -EIO;
	}
	return err;
}

static void
nfs4_setup_compound(struct nfs4_compound *cp, struct nfs4_op *ops,
		    struct nfs_server *server, char *tag)
{
	memset(cp, 0, sizeof(*cp));
	cp->ops = ops;
	cp->server = server;
}

static void
nfs4_setup_create_dir(struct nfs4_compound *cp, struct qstr *name,
		      struct iattr *sattr, struct nfs4_change_info *info)
{
	struct nfs4_create *create = GET_OP(cp, create);
	
	create->cr_ftype = NF4DIR;
	create->cr_namelen = name->len;
	create->cr_name = name->name;
	create->cr_attrs = sattr;
	create->cr_cinfo = info;
	
	OPNUM(cp) = OP_CREATE;
	cp->req_nops++;
}

static void
nfs4_setup_create_symlink(struct nfs4_compound *cp, struct qstr *name,
			  struct qstr *linktext, struct iattr *sattr,
			  struct nfs4_change_info *info)
{
	struct nfs4_create *create = GET_OP(cp, create);

	create->cr_ftype = NF4LNK;
	create->cr_textlen = linktext->len;
	create->cr_text = linktext->name;
	create->cr_namelen = name->len;
	create->cr_name = name->name;
	create->cr_attrs = sattr;
	create->cr_cinfo = info;

	OPNUM(cp) = OP_CREATE;
	cp->req_nops++;
}

static void
nfs4_setup_create_special(struct nfs4_compound *cp, struct qstr *name,
			    dev_t dev, struct iattr *sattr,
			    struct nfs4_change_info *info)
{
	int mode = sattr->ia_mode;
	struct nfs4_create *create = GET_OP(cp, create);

	BUG_ON(!(sattr->ia_valid & ATTR_MODE));
	BUG_ON(!S_ISFIFO(mode) && !S_ISBLK(mode) && !S_ISCHR(mode) && !S_ISSOCK(mode));
	
	if (S_ISFIFO(mode))
		create->cr_ftype = NF4FIFO;
	else if (S_ISBLK(mode)) {
		create->cr_ftype = NF4BLK;
		create->cr_specdata1 = MAJOR(dev);
		create->cr_specdata2 = MINOR(dev);
	}
	else if (S_ISCHR(mode)) {
		create->cr_ftype = NF4CHR;
		create->cr_specdata1 = MAJOR(dev);
		create->cr_specdata2 = MINOR(dev);
	}
	else
		create->cr_ftype = NF4SOCK;
	
	create->cr_namelen = name->len;
	create->cr_name = name->name;
	create->cr_attrs = sattr;
	create->cr_cinfo = info;

	OPNUM(cp) = OP_CREATE;
	cp->req_nops++;
}

/*
 * This is our standard bitmap for GETATTR requests.
 */
u32 nfs4_fattr_bitmap[2] = {
	FATTR4_WORD0_TYPE
	| FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_SIZE
	| FATTR4_WORD0_FSID
	| FATTR4_WORD0_FILEID,
	FATTR4_WORD1_MODE
	| FATTR4_WORD1_NUMLINKS
	| FATTR4_WORD1_OWNER
	| FATTR4_WORD1_OWNER_GROUP
	| FATTR4_WORD1_RAWDEV
	| FATTR4_WORD1_SPACE_USED
	| FATTR4_WORD1_TIME_ACCESS
	| FATTR4_WORD1_TIME_METADATA
	| FATTR4_WORD1_TIME_MODIFY
};

u32 nfs4_statfs_bitmap[2] = {
	FATTR4_WORD0_FILES_AVAIL
	| FATTR4_WORD0_FILES_FREE
	| FATTR4_WORD0_FILES_TOTAL,
	FATTR4_WORD1_SPACE_AVAIL
	| FATTR4_WORD1_SPACE_FREE
	| FATTR4_WORD1_SPACE_TOTAL
};

u32 nfs4_pathconf_bitmap[2] = {
	FATTR4_WORD0_MAXLINK
	| FATTR4_WORD0_MAXNAME,
	0
};

static inline void
__nfs4_setup_getattr(struct nfs4_compound *cp, u32 *bitmap,
		     struct nfs_fattr *fattr,
		     struct nfs_fsstat *fsstat,
		     struct nfs_pathconf *pathconf)
{
        struct nfs4_getattr *getattr = GET_OP(cp, getattr);

        getattr->gt_bmval = bitmap;
        getattr->gt_attrs = fattr;
	getattr->gt_fsstat = fsstat;
	getattr->gt_pathconf = pathconf;

        OPNUM(cp) = OP_GETATTR;
        cp->req_nops++;
}

static void
nfs4_setup_getattr(struct nfs4_compound *cp,
		struct nfs_fattr *fattr)
{
	__nfs4_setup_getattr(cp, nfs4_fattr_bitmap, fattr,
			NULL, NULL);
}

static void
nfs4_setup_statfs(struct nfs4_compound *cp,
		struct nfs_fsstat *fsstat)
{
	__nfs4_setup_getattr(cp, nfs4_statfs_bitmap,
			NULL, fsstat, NULL);
}

static void
nfs4_setup_pathconf(struct nfs4_compound *cp,
		struct nfs_pathconf *pathconf)
{
	__nfs4_setup_getattr(cp, nfs4_pathconf_bitmap,
			NULL, NULL, pathconf);
}

static void
nfs4_setup_getfh(struct nfs4_compound *cp, struct nfs_fh *fhandle)
{
	struct nfs4_getfh *getfh = GET_OP(cp, getfh);

	getfh->gf_fhandle = fhandle;

	OPNUM(cp) = OP_GETFH;
	cp->req_nops++;
}

static void
nfs4_setup_link(struct nfs4_compound *cp, struct qstr *name,
		struct nfs4_change_info *info)
{
	struct nfs4_link *link = GET_OP(cp, link);

	link->ln_namelen = name->len;
	link->ln_name = name->name;
	link->ln_cinfo = info;

	OPNUM(cp) = OP_LINK;
	cp->req_nops++;
}

static void
nfs4_setup_putfh(struct nfs4_compound *cp, struct nfs_fh *fhandle)
{
	struct nfs4_putfh *putfh = GET_OP(cp, putfh);

	putfh->pf_fhandle = fhandle;

	OPNUM(cp) = OP_PUTFH;
	cp->req_nops++;
}

static void
nfs4_setup_readdir(struct nfs4_compound *cp, u64 cookie, u32 *verifier,
		     struct page **pages, unsigned int bufsize, struct dentry *dentry)
{
	u32 *start, *p;
	struct nfs4_readdir *readdir = GET_OP(cp, readdir);

	BUG_ON(bufsize < 80);
	readdir->rd_cookie = (cookie > 2) ? cookie : 0;
	memcpy(&readdir->rd_req_verifier, verifier, sizeof(readdir->rd_req_verifier));
	readdir->rd_count = bufsize;
	readdir->rd_bmval[0] = FATTR4_WORD0_FILEID;
	readdir->rd_bmval[1] = 0;
	readdir->rd_pages = pages;
	readdir->rd_pgbase = 0;
	
	OPNUM(cp) = OP_READDIR;
	cp->req_nops++;

	if (cookie >= 2)
		return;
	
	/*
	 * NFSv4 servers do not return entries for '.' and '..'
	 * Therefore, we fake these entries here.  We let '.'
	 * have cookie 0 and '..' have cookie 1.  Note that
	 * when talking to the server, we always send cookie 0
	 * instead of 1 or 2.
	 */
	start = p = (u32 *)kmap_atomic(*pages, KM_USER0);
	
	if (cookie == 0) {
		*p++ = xdr_one;                                  /* next */
		*p++ = xdr_zero;                   /* cookie, first word */
		*p++ = xdr_one;                   /* cookie, second word */
		*p++ = xdr_one;                             /* entry len */
		memcpy(p, ".\0\0\0", 4);                        /* entry */
		p++;
		*p++ = xdr_one;                         /* bitmap length */
		*p++ = htonl(FATTR4_WORD0_FILEID);             /* bitmap */
		*p++ = htonl(8);              /* attribute buffer length */
		p = xdr_encode_hyper(p, NFS_FILEID(dentry->d_inode));
	}
	
	*p++ = xdr_one;                                  /* next */
	*p++ = xdr_zero;                   /* cookie, first word */
	*p++ = xdr_two;                   /* cookie, second word */
	*p++ = xdr_two;                             /* entry len */
	memcpy(p, "..\0\0", 4);                         /* entry */
	p++;
	*p++ = xdr_one;                         /* bitmap length */
	*p++ = htonl(FATTR4_WORD0_FILEID);             /* bitmap */
	*p++ = htonl(8);              /* attribute buffer length */
	p = xdr_encode_hyper(p, NFS_FILEID(dentry->d_parent->d_inode));

	readdir->rd_pgbase = (char *)p - (char *)start;
	readdir->rd_count -= readdir->rd_pgbase;
	kunmap_atomic(start, KM_USER0);
}

static void
nfs4_setup_readlink(struct nfs4_compound *cp, int count, struct page **pages)
{
	struct nfs4_readlink *readlink = GET_OP(cp, readlink);

	readlink->rl_count = count;
	readlink->rl_pages = pages;

	OPNUM(cp) = OP_READLINK;
	cp->req_nops++;
}

static void
nfs4_setup_remove(struct nfs4_compound *cp, struct qstr *name, struct nfs4_change_info *cinfo)
{
	struct nfs4_remove *remove = GET_OP(cp, remove);

	remove->rm_namelen = name->len;
	remove->rm_name = name->name;
	remove->rm_cinfo = cinfo;

	OPNUM(cp) = OP_REMOVE;
	cp->req_nops++;
}

static void
nfs4_setup_rename(struct nfs4_compound *cp, struct qstr *old, struct qstr *new,
		  struct nfs4_change_info *old_cinfo, struct nfs4_change_info *new_cinfo)
{
	struct nfs4_rename *rename = GET_OP(cp, rename);

	rename->rn_oldnamelen = old->len;
	rename->rn_oldname = old->name;
	rename->rn_newnamelen = new->len;
	rename->rn_newname = new->name;
	rename->rn_src_cinfo = old_cinfo;
	rename->rn_dst_cinfo = new_cinfo;

	OPNUM(cp) = OP_RENAME;
	cp->req_nops++;
}

static void
nfs4_setup_restorefh(struct nfs4_compound *cp)
{
        OPNUM(cp) = OP_RESTOREFH;
        cp->req_nops++;
}

static void
nfs4_setup_savefh(struct nfs4_compound *cp)
{
        OPNUM(cp) = OP_SAVEFH;
        cp->req_nops++;
}

static void
renew_lease(struct nfs_server *server, unsigned long timestamp)
{
	struct nfs4_client *clp = server->nfs4_state;
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

static inline void
process_lease(struct nfs4_compound *cp)
{
        /*
         * Generic lease processing: If this operation contains a
	 * lease-renewing operation, and it succeeded, update the RENEW time
	 * in the superblock.  Instead of the current time, we use the time
	 * when the request was sent out.  (All we know is that the lease was
	 * renewed sometime between then and now, and we have to assume the
	 * worst case.)
	 *
	 * Notes:
	 *   (1) renewd doesn't acquire the spinlock when messing with
	 *     server->last_renewal; this is OK since rpciod always runs
	 *     under the BKL.
	 *   (2) cp->timestamp was set at the end of XDR encode.
         */
	if (!cp->renew_index)
		return;
	if (!cp->toplevel_status || cp->resp_nops > cp->renew_index)
		renew_lease(cp->server, cp->timestamp);
}

static int
nfs4_call_compound(struct nfs4_compound *cp, struct rpc_cred *cred, int flags)
{
	int status;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMPOUND],
		.rpc_argp = cp,
		.rpc_resp = cp,
		.rpc_cred = cred,
	};

	status = rpc_call_sync(cp->server->client, &msg, flags);
	if (!status)
		process_lease(cp);
	
	return status;
}

static inline void
process_cinfo(struct nfs4_change_info *info, struct nfs_fattr *fattr)
{
	BUG_ON((fattr->valid & NFS_ATTR_FATTR) == 0);
	BUG_ON((fattr->valid & NFS_ATTR_FATTR_V4) == 0);
	
	if (fattr->change_attr == info->after) {
		fattr->pre_change_attr = info->before;
		fattr->valid |= NFS_ATTR_PRE_CHANGE;
		fattr->timestamp = jiffies;
	}
}

/*
 * OPEN_RECLAIM:
 * 	reclaim state on the server after a reboot.
 * 	Assumes caller is holding the sp->so_sem
 */
int
nfs4_open_reclaim(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_fattr fattr = {
		.valid = 0,
	};
	struct nfs4_change_info d_cinfo;
	struct nfs4_getattr     f_getattr = {
		.gt_bmval       = nfs4_fattr_bitmap,
		.gt_attrs       = &fattr,
	};

	struct nfs_open_reclaimargs o_arg = {
		.fh = NFS_FH(inode),
		.seqid = sp->so_seqid,
		.id = sp->so_id,
		.share_access = state->state,
		.clientid = server->nfs4_state->cl_clientid,
		.claim = NFS4_OPEN_CLAIM_PREVIOUS,
		.f_getattr = &f_getattr,
	};
	struct nfs_openres o_res = {
		.cinfo = &d_cinfo,
		.f_getattr = &f_getattr,
		.server = server,	/* Grrr */
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN_RECLAIM],
		.rpc_argp       = &o_arg,
		.rpc_resp	= &o_res,
		.rpc_cred	= sp->so_cred,
	};
	int status;

	status = rpc_call_sync(server->client, &msg, 0);
	nfs4_increment_seqid(status, sp);
	if (status == 0)
		memcpy(&state->stateid, &o_res.stateid, sizeof(state->stateid));
	/* Update the inode attributes */
	nfs_refresh_inode(inode, &fattr);
	return status;
}

/*
 * Returns an nfs4_state + an referenced inode
 */
struct nfs4_state *
nfs4_do_open(struct inode *dir, struct qstr *name, int flags, struct iattr *sattr, struct rpc_cred *cred)
{
	struct nfs4_state_owner  *sp;
	struct nfs4_state     *state = NULL;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct inode *inode = NULL;
	struct nfs4_change_info d_cinfo;
	int                     status;
	struct nfs_fattr        d_attr = {
		.valid          = 0,
	};
	struct nfs_fattr        f_attr = {
		.valid          = 0,
	};
	struct nfs4_getattr     f_getattr = {
		.gt_bmval       = nfs4_fattr_bitmap,
		.gt_attrs       = &f_attr,
	};
	struct nfs4_getattr     d_getattr = {
		.gt_bmval       = nfs4_fattr_bitmap,
		.gt_attrs       = &d_attr,
	};
	struct nfs_openargs o_arg = {
		.fh             = NFS_FH(dir),
		.share_access   = flags & (FMODE_READ|FMODE_WRITE),
		.opentype       = (flags & O_CREAT) ? NFS4_OPEN_CREATE : NFS4_OPEN_NOCREATE,
		.createmode     = (flags & O_EXCL) ? NFS4_CREATE_EXCLUSIVE : NFS4_CREATE_UNCHECKED,
		.name           = name,
		.f_getattr      = &f_getattr,
		.d_getattr      = &d_getattr,
		.server         = server,
	};
	struct nfs_openres o_res = {
		.cinfo          = &d_cinfo,
		.f_getattr      = &f_getattr,
		.d_getattr      = &d_getattr,
		.server         = server,
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN],
		.rpc_argp       = &o_arg,
		.rpc_resp       = &o_res,
		.rpc_cred	= cred,
	};

retry:
	status = -ENOMEM;
	if (!(sp = nfs4_get_state_owner(NFS_SERVER(dir), cred))) {
		dprintk("nfs4_do_open: nfs4_get_state_owner failed!\n");
		goto out;
	}
	if (o_arg.createmode & NFS4_CREATE_EXCLUSIVE){
		u32 *p = (u32 *) o_arg.u.verifier.data;
		p[0] = jiffies;
		p[1] = current->pid;
	} else if (o_arg.createmode == NFS4_CREATE_UNCHECKED) {
		o_arg.u.attrs = sattr;
	}
	/* Serialization for the sequence id */
	down(&sp->so_sema);
	o_arg.seqid = sp->so_seqid;
	o_arg.id = sp->so_id;
	o_arg.clientid = NFS_SERVER(dir)->nfs4_state->cl_clientid,

	status = rpc_call_sync(server->client, &msg, 0);
	nfs4_increment_seqid(status, sp);
	if (status)
		goto out_up;
	process_cinfo(&d_cinfo, &d_attr);
	nfs_refresh_inode(dir, &d_attr);

	status = -ENOMEM;
	inode = nfs_fhget(dir->i_sb, &o_res.fh, &f_attr);
	if (!inode)
		goto out_up;
	state = nfs4_get_open_state(inode, sp);
	if (!state)
		goto out_up;

	if(o_res.rflags & NFS4_OPEN_RESULT_CONFIRM) {
		struct nfs_open_confirmargs oc_arg = {
			.fh             = &o_res.fh,
			.seqid          = sp->so_seqid,
		};
		struct nfs_open_confirmres oc_res;
		struct 	rpc_message msg = {
			.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN_CONFIRM],
			.rpc_argp       = &oc_arg,
			.rpc_resp       = &oc_res,
			.rpc_cred	= cred,
		};

		memcpy(&oc_arg.stateid, &o_res.stateid, sizeof(oc_arg.stateid));
		status = rpc_call_sync(server->client, &msg, 0);
		nfs4_increment_seqid(status, sp);
		if (status)
			goto out_up;
		memcpy(&state->stateid, &oc_res.stateid, sizeof(state->stateid));
	} else
		memcpy(&state->stateid, &o_res.stateid, sizeof(state->stateid));
	spin_lock(&inode->i_lock);
	if (flags & FMODE_READ)
		state->nreaders++;
	if (flags & FMODE_WRITE)
		state->nwriters++;
	state->state |= flags & (FMODE_READ|FMODE_WRITE);
	spin_unlock(&inode->i_lock);

	up(&sp->so_sema);
	nfs4_put_state_owner(sp);
	return state;

out_up:
	up(&sp->so_sema);
	nfs4_put_state_owner(sp);
	if (state) {
		nfs4_put_open_state(state);
		state = NULL;
	}
	if (inode) {
		iput(inode);
		inode = NULL;
	}
	/* NOTE: BAD_SEQID means the server and client disagree about the
	 * book-keeping w.r.t. state-changing operations
	 * (OPEN/CLOSE/LOCK/LOCKU...)
	 * It is actually a sign of a bug on the client or on the server.
	 *
	 * If we receive a BAD_SEQID error in the particular case of
	 * doing an OPEN, we assume that nfs4_increment_seqid() will
	 * have unhashed the old state_owner for us, and that we can
	 * therefore safely retry using a new one. We should still warn
	 * the user though...
	 */
	if (status == -NFS4ERR_BAD_SEQID) {
		printk(KERN_WARNING "NFS: v4 server returned a bad sequence-id error!\n");
		goto retry;
	}
	status = nfs4_handle_error(server, status);
	if (!status)
		goto retry;
	BUG_ON(status < -1000 || status > 0);
out:
	return ERR_PTR(status);
}

int
nfs4_do_setattr(struct nfs_server *server, struct nfs_fattr *fattr,
                struct nfs_fh *fhandle, struct iattr *sattr,
                struct nfs4_state *state)
{
        struct nfs4_getattr     getattr = {
                .gt_bmval       = nfs4_fattr_bitmap,
                .gt_attrs       = fattr,
        };
        struct nfs_setattrargs  arg = {
                .fh             = fhandle,
                .iap            = sattr,
                .attr           = &getattr,
		.server		= server,
        };
        struct nfs_setattrres  res = {
                .attr           = &getattr,
		.server		= server,
        };
        struct rpc_message msg = {
                .rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
                .rpc_argp       = &arg,
                .rpc_resp       = &res,
        };
	int status;

retry:
        fattr->valid = 0;

	if (sattr->ia_valid & ATTR_SIZE)
		nfs4_copy_stateid(&arg.stateid, state, 0);
	else
		memcpy(&arg.stateid, &zero_stateid, sizeof(arg.stateid));

	status = rpc_call_sync(server->client, &msg, 0);
	if (status) {
		status = nfs4_handle_error(server, status);
		if (!status)
			goto retry;
	}
	return status;
}

/* 
 * It is possible for data to be read/written from a mem-mapped file 
 * after the sys_close call (which hits the vfs layer as a flush).
 * This means that we can't safely call nfsv4 close on a file until 
 * the inode is cleared. This in turn means that we are not good
 * NFSv4 citizens - we do not indicate to the server to update the file's 
 * share state even when we are done with one of the three share 
 * stateid's in the inode.
 *
 * NOTE: Caller must be holding the sp->so_owner semaphore!
 */
int
nfs4_do_close(struct inode *inode, struct nfs4_state *state) 
{
	struct nfs4_state_owner *sp = state->owner;
	int status = 0;
	struct nfs_closeargs arg = {
		.fh		= NFS_FH(inode),
	};
	struct nfs_closeres res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_CLOSE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};

	memcpy(&arg.stateid, &state->stateid, sizeof(arg.stateid));
	/* Serialization for the sequence id */
	arg.seqid = sp->so_seqid,
	status = rpc_call_sync(NFS_SERVER(inode)->client, &msg, 0);

        /* hmm. we are done with the inode, and in the process of freeing
	 * the state_owner. we keep this around to process errors
	 */
	nfs4_increment_seqid(status, sp);
	if (!status)
		memcpy(&state->stateid, &res.stateid, sizeof(state->stateid));

	return status;
}

int
nfs4_do_downgrade(struct inode *inode, struct nfs4_state *state, mode_t mode) 
{
	struct nfs4_state_owner *sp = state->owner;
	int status = 0;
	struct nfs_closeargs arg = {
		.fh		= NFS_FH(inode),
		.seqid		= sp->so_seqid,
		.share_access	= mode,
	};
	struct nfs_closeres res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_OPEN_DOWNGRADE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};

	memcpy(&arg.stateid, &state->stateid, sizeof(arg.stateid));
	status = rpc_call_sync(NFS_SERVER(inode)->client, &msg, 0);
	nfs4_increment_seqid(status, sp);
	if (!status)
		memcpy(&state->stateid, &res.stateid, sizeof(state->stateid));

	return status;
}

struct inode *
nfs4_atomic_open(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct iattr attr;
	struct rpc_cred *cred;
	struct nfs4_state *state;

	if (nd->flags & LOOKUP_CREATE) {
		attr.ia_mode = nd->intent.open.create_mode;
		attr.ia_valid = ATTR_MODE;
		if (!IS_POSIXACL(dir))
			attr.ia_mode &= ~current->fs->umask;
	} else {
		attr.ia_valid = 0;
		BUG_ON(nd->intent.open.flags & O_CREAT);
	}

	cred = rpcauth_lookupcred(NFS_SERVER(dir)->client->cl_auth, 0);
	state = nfs4_do_open(dir, &dentry->d_name, nd->intent.open.flags, &attr, cred);
	put_rpccred(cred);
	if (IS_ERR(state))
		return (struct inode *)state;
	return state->inode;
}

int
nfs4_open_revalidate(struct inode *dir, struct dentry *dentry, int openflags)
{
	struct rpc_cred *cred;
	struct nfs4_state *state;
	struct inode *inode;

	cred = rpcauth_lookupcred(NFS_SERVER(dir)->client->cl_auth, 0);
	state = nfs4_do_open(dir, &dentry->d_name, openflags, NULL, cred);
	put_rpccred(cred);
	if (state == ERR_PTR(-ENOENT) && dentry->d_inode == 0)
		return 1;
	if (IS_ERR(state))
		return 0;
	inode = state->inode;
	if (inode == dentry->d_inode) {
		iput(inode);
		return 1;
	}
	d_drop(dentry);
	nfs4_close_state(state, openflags);
	iput(inode);
	return 0;
}

static int nfs4_lookup_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs_fattr *	fattr = info->fattr;
	struct nfs4_lookup_root_arg args = {
		.bitmask = nfs4_fattr_bitmap,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP_ROOT],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	fattr->valid = 0;
	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs_fattr *	fattr = info->fattr;
	unsigned char *		p;
	struct qstr		q;
	struct nfs4_lookup_arg args = {
		.dir_fh = fhandle,
		.name = &q,
		.bitmask = nfs4_fattr_bitmap,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	/*
	 * Now we do a separate LOOKUP for each component of the mount path.
	 * The LOOKUPs are done separately so that we can conveniently
	 * catch an ERR_WRONGSEC if it occurs along the way...
	 */
	status = nfs4_lookup_root(server, fhandle, info);
	if (status)
		goto out;

	p = server->mnt_path;
	for (;;) {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		q.name = p;
		while (*p && (*p != '/'))
			p++;
		q.len = p - q.name;

		fattr->valid = 0;
		status = rpc_call_sync(server->client, &msg, 0);
		if (!status)
			continue;
		if (status == -ENOENT) {
			printk(KERN_NOTICE "NFS: mount path %s does not exist!\n", server->mnt_path);
			printk(KERN_NOTICE "NFS: suggestion: try mounting '/' instead.\n");
		}
		break;
	}
	if (status == 0)
		status = nfs4_do_fsinfo(server, fhandle, info);
out:
	return nfs4_map_errors(status);
}

static int nfs4_proc_getattr(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs4_getattr_arg args = {
		.fh = NFS_FH(inode),
		.bitmask = nfs4_fattr_bitmap,
	};
	struct nfs4_getattr_res res = {
		.fattr = fattr,
		.server = NFS_SERVER(inode),
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETATTR],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	
	fattr->valid = 0;

	return nfs4_map_errors(rpc_call_sync(NFS_CLIENT(inode), &msg, 0));
}

/* 
 * The file is not closed if it is opened due to the a request to change
 * the size of the file. The open call will not be needed once the
 * VFS layer lookup-intents are implemented.
 *
 * Close is called when the inode is destroyed.
 * If we haven't opened the file for O_WRONLY, we
 * need to in the size_change case to obtain a stateid.
 *
 * Got race?
 * Because OPEN is always done by name in nfsv4, it is
 * possible that we opened a different file by the same
 * name.  We can recognize this race condition, but we
 * can't do anything about it besides returning an error.
 *
 * This will be fixed with VFS changes (lookup-intent).
 */
static int
nfs4_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
		  struct iattr *sattr)
{
	struct inode *		inode = dentry->d_inode;
	int			size_change = sattr->ia_valid & ATTR_SIZE;
	struct nfs4_state	*state = NULL;
	int need_iput = 0;
	int status;

	fattr->valid = 0;
	
	if (size_change) {
		struct rpc_cred *cred = rpcauth_lookupcred(NFS_SERVER(inode)->client->cl_auth, 0);
		state = nfs4_find_state(inode, cred, FMODE_WRITE);
		if (!state) {
			state = nfs4_do_open(dentry->d_parent->d_inode, 
				&dentry->d_name, FMODE_WRITE, NULL, cred);
			need_iput = 1;
		}
		put_rpccred(cred);
		if (IS_ERR(state))
			return PTR_ERR(state);

		if (state->inode != inode) {
			printk(KERN_WARNING "nfs: raced in setattr (%p != %p), returning -EIO\n", inode, state->inode);
			status = -EIO;
			goto out;
		}
	}
	status = nfs4_do_setattr(NFS_SERVER(inode), fattr,
			NFS_FH(inode), sattr, state);
out:
	if (state) {
		inode = state->inode;
		nfs4_close_state(state, FMODE_WRITE);
		if (need_iput)
			iput(inode);
	}
	return status;
}

static int nfs4_proc_lookup(struct inode *dir, struct qstr *name,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int		       status;
	struct nfs4_lookup_arg args = {
		.bitmask = nfs4_fattr_bitmap,
		.dir_fh = NFS_FH(dir),
		.name = name,
	};
	struct nfs4_lookup_res res = {
		.server = NFS_SERVER(dir),
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	
	fattr->valid = 0;
	
	dprintk("NFS call  lookup %s\n", name->name);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	dprintk("NFS reply lookup: %d\n", status);
	return nfs4_map_errors(status);
}

static int nfs4_proc_access(struct inode *inode, struct rpc_cred *cred, int mode)
{
	int			status;
	struct nfs4_accessargs args = {
		.fh = NFS_FH(inode),
	};
	struct nfs4_accessres res = { 0 };
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_ACCESS],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};

	/*
	 * Determine which access bits we want to ask for...
	 */
	if (mode & MAY_READ)
		args.access |= NFS4_ACCESS_READ;
	if (S_ISDIR(inode->i_mode)) {
		if (mode & MAY_WRITE)
			args.access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE;
		if (mode & MAY_EXEC)
			args.access |= NFS4_ACCESS_LOOKUP;
	}
	else {
		if (mode & MAY_WRITE)
			args.access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND;
		if (mode & MAY_EXEC)
			args.access |= NFS4_ACCESS_EXECUTE;
	}
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (!status) {
		if (args.access != res.supported) {
			printk(KERN_NOTICE "NFS: server didn't support all access bits!\n");
			status = -ENOTSUPP;
		} else if ((args.access & res.access) != args.access)
			status = -EACCES;
	}
	return nfs4_map_errors(status);
}

/*
 * TODO: For the time being, we don't try to get any attributes
 * along with any of the zero-copy operations READ, READDIR,
 * READLINK, WRITE.
 *
 * In the case of the first three, we want to put the GETATTR
 * after the read-type operation -- this is because it is hard
 * to predict the length of a GETATTR response in v4, and thus
 * align the READ data correctly.  This means that the GETATTR
 * may end up partially falling into the page cache, and we should
 * shift it into the 'tail' of the xdr_buf before processing.
 * To do this efficiently, we need to know the total length
 * of data received, which doesn't seem to be available outside
 * of the RPC layer.
 *
 * In the case of WRITE, we also want to put the GETATTR after
 * the operation -- in this case because we want to make sure
 * we get the post-operation mtime and size.  This means that
 * we can't use xdr_encode_pages() as written: we need a variant
 * of it which would leave room in the 'tail' iovec.
 *
 * Both of these changes to the XDR layer would in fact be quite
 * minor, but I decided to leave them for a subsequent patch.
 */
static int
nfs4_proc_readlink(struct inode *inode, struct page *page)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[2];

	nfs4_setup_compound(&compound, ops, NFS_SERVER(inode), "readlink");
	nfs4_setup_putfh(&compound, NFS_FH(inode));
	nfs4_setup_readlink(&compound, PAGE_CACHE_SIZE, &page);
	return nfs4_map_errors(nfs4_call_compound(&compound, NULL, 0));
}

static int
nfs4_proc_read(struct nfs_read_data *rdata, struct file *filp)
{
	int flags = rdata->flags;
	struct inode *inode = rdata->inode;
	struct nfs_fattr *fattr = rdata->res.fattr;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp	= &rdata->args,
		.rpc_resp	= &rdata->res,
	};
	unsigned long timestamp = jiffies;
	int status;

	dprintk("NFS call  read %d @ %Ld\n", rdata->args.count,
			(long long) rdata->args.offset);

	/*
	 * Try first to use O_RDONLY, then O_RDWR stateid.
	 */
	if (filp) {
		struct nfs4_state *state;
		state = (struct nfs4_state *)filp->private_data;
		rdata->args.state = state;
		msg.rpc_cred = state->owner->so_cred;
	} else {
		rdata->args.state = NULL;
		msg.rpc_cred = NFS_I(inode)->mm_cred;
	}

	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, flags);
	if (!status)
		renew_lease(server, timestamp);
	dprintk("NFS reply read: %d\n", status);
	return nfs4_map_errors(status);
}

static int
nfs4_proc_write(struct nfs_write_data *wdata, struct file *filp)
{
	int rpcflags = wdata->flags;
	struct inode *inode = wdata->inode;
	struct nfs_fattr *fattr = wdata->res.fattr;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp	= &wdata->args,
		.rpc_resp	= &wdata->res,
	};
	int status;

	dprintk("NFS call  write %d @ %Ld\n", wdata->args.count,
			(long long) wdata->args.offset);

	/*
	 * Try first to use O_WRONLY, then O_RDWR stateid.
	 */
	if (filp) {
		struct nfs4_state *state;
		state = (struct nfs4_state *)filp->private_data;
		wdata->args.state = state;
		msg.rpc_cred = state->owner->so_cred;
	} else {
		wdata->args.state = NULL;
		msg.rpc_cred = NFS_I(inode)->mm_cred;
	}

	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, rpcflags);
	dprintk("NFS reply write: %d\n", status);
	return nfs4_map_errors(status);
}

static int
nfs4_proc_commit(struct nfs_write_data *cdata, struct file *filp)
{
	struct inode *inode = cdata->inode;
	struct nfs_fattr *fattr = cdata->res.fattr;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_COMMIT],
		.rpc_argp	= &cdata->args,
		.rpc_resp	= &cdata->res,
	};
	int status;

	dprintk("NFS call  commit %d @ %Ld\n", cdata->args.count,
			(long long) cdata->args.offset);

	/*
	 * Try first to use O_WRONLY, then O_RDWR stateid.
	 */
	if (filp)
		msg.rpc_cred = ((struct nfs4_state *)filp->private_data)->owner->so_cred;
	else
		msg.rpc_cred = NFS_I(inode)->mm_cred;

	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("NFS reply commit: %d\n", status);
	return nfs4_map_errors(status);
}

/*
 * Got race?
 * We will need to arrange for the VFS layer to provide an atomic open.
 * Until then, this create/open method is prone to inefficiency and race
 * conditions due to the lookup, create, and open VFS calls from sys_open()
 * placed on the wire.
 *
 * Given the above sorry state of affairs, I'm simply sending an OPEN.
 * The file will be opened again in the subsequent VFS open call
 * (nfs4_proc_file_open).
 *
 * The open for read will just hang around to be used by any process that
 * opens the file O_RDONLY. This will all be resolved with the VFS changes.
 */

static struct inode *
nfs4_proc_create(struct inode *dir, struct qstr *name, struct iattr *sattr,
                 int flags)
{
	struct inode *inode;
	struct nfs4_state *state = NULL;
	struct rpc_cred *cred;

	cred = rpcauth_lookupcred(NFS_SERVER(dir)->client->cl_auth, 0);
	state = nfs4_do_open(dir, name, flags, sattr, cred);
	put_rpccred(cred);
	if (!IS_ERR(state)) {
		inode = state->inode;
		if (flags & O_EXCL) {
			struct nfs_fattr fattr;
			int status;
			status = nfs4_do_setattr(NFS_SERVER(dir), &fattr,
			                     NFS_FH(inode), sattr, state);
			if (status != 0) {
				nfs4_close_state(state, flags);
				iput(inode);
				inode = ERR_PTR(status);
			}
		}
	} else
		inode = (struct inode *)state;
	return inode;
}

static int
nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[3];
	struct nfs4_change_info	dir_cinfo;
	struct nfs_fattr	dir_attr;
	int			status;

	dir_attr.valid = 0;
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "remove");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_remove(&compound, name, &dir_cinfo);
	nfs4_setup_getattr(&compound, &dir_attr);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return nfs4_map_errors(status);
}

struct unlink_desc {
	struct nfs4_compound	compound;
	struct nfs4_op		ops[3];
	struct nfs4_change_info	cinfo;
	struct nfs_fattr	attrs;
};

static int
nfs4_proc_unlink_setup(struct rpc_message *msg, struct dentry *dir, struct qstr *name)
{
	struct unlink_desc *	up;
	struct nfs4_compound *	cp;

	up = (struct unlink_desc *) kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;
	cp = &up->compound;
	
	nfs4_setup_compound(cp, up->ops, NFS_SERVER(dir->d_inode), "unlink_setup");
	nfs4_setup_putfh(cp, NFS_FH(dir->d_inode));
	nfs4_setup_remove(cp, name, &up->cinfo);
	nfs4_setup_getattr(cp, &up->attrs);
	
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMPOUND];
	msg->rpc_argp = cp;
	msg->rpc_resp = cp;
	return 0;
}

static int
nfs4_proc_unlink_done(struct dentry *dir, struct rpc_task *task)
{
	struct rpc_message *msg = &task->tk_msg;
	struct unlink_desc *up;
	
	if (msg->rpc_argp) {
		up = (struct unlink_desc *) msg->rpc_argp;
		process_lease(&up->compound);
		process_cinfo(&up->cinfo, &up->attrs);
		nfs_refresh_inode(dir->d_inode, &up->attrs);
		kfree(up);
		msg->rpc_argp = NULL;
	}
	return 0;
}

static int
nfs4_proc_rename(struct inode *old_dir, struct qstr *old_name,
		 struct inode *new_dir, struct qstr *new_name)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs4_change_info	old_cinfo, new_cinfo;
	struct nfs_fattr	old_dir_attr, new_dir_attr;
	int			status;

	old_dir_attr.valid = 0;
	new_dir_attr.valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(old_dir), "rename");
	nfs4_setup_putfh(&compound, NFS_FH(old_dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_putfh(&compound, NFS_FH(new_dir));
	nfs4_setup_rename(&compound, old_name, new_name, &old_cinfo, &new_cinfo);
	nfs4_setup_getattr(&compound, &new_dir_attr);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &old_dir_attr);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&old_cinfo, &old_dir_attr);
		process_cinfo(&new_cinfo, &new_dir_attr);
		nfs_refresh_inode(old_dir, &old_dir_attr);
		nfs_refresh_inode(new_dir, &new_dir_attr);
	}
	return nfs4_map_errors(status);
}

static int
nfs4_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs4_change_info	dir_cinfo;
	struct nfs_fattr	dir_attr, fattr;
	int			status;
	
	dir_attr.valid = 0;
	fattr.valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(inode), "link");
	nfs4_setup_putfh(&compound, NFS_FH(inode));
	nfs4_setup_savefh(&compound);
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_link(&compound, name, &dir_cinfo);
	nfs4_setup_getattr(&compound, &dir_attr);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &fattr);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
		nfs_refresh_inode(inode, &fattr);
	}
	return nfs4_map_errors(status);
}

static int
nfs4_proc_symlink(struct inode *dir, struct qstr *name, struct qstr *path,
		  struct iattr *sattr, struct nfs_fh *fhandle,
		  struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs_fattr	dir_attr;
	struct nfs4_change_info	dir_cinfo;
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "symlink");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_create_symlink(&compound, name, path, sattr, &dir_cinfo);
	nfs4_setup_getattr(&compound, fattr);
	nfs4_setup_getfh(&compound, fhandle);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &dir_attr);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return nfs4_map_errors(status);
}

static int
nfs4_proc_mkdir(struct inode *dir, struct qstr *name, struct iattr *sattr,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs_fattr	dir_attr;
	struct nfs4_change_info	dir_cinfo;
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "mkdir");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_create_dir(&compound, name, sattr, &dir_cinfo);
	nfs4_setup_getattr(&compound, fattr);
	nfs4_setup_getfh(&compound, fhandle);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &dir_attr);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return nfs4_map_errors(status);
}

static int
nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
                  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs4_compound	compound;
	struct nfs4_op		ops[2];
	int			status;

	lock_kernel();

	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "readdir");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_readdir(&compound, cookie, NFS_COOKIEVERF(dir), &page, count, dentry);
	status = nfs4_call_compound(&compound, cred, 0);
	if (status == 0)
		memcpy(NFS_COOKIEVERF(dir), ops[1].u.readdir.rd_resp_verifier.data, NFS4_VERIFIER_SIZE);

	unlock_kernel();
	return nfs4_map_errors(status);
}

static int
nfs4_proc_mknod(struct inode *dir, struct qstr *name, struct iattr *sattr,
		dev_t rdev, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs_fattr	dir_attr;
	struct nfs4_change_info	dir_cinfo;
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "mknod");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_create_special(&compound, name, rdev,sattr, &dir_cinfo);
	nfs4_setup_getattr(&compound, fattr);
	nfs4_setup_getfh(&compound, fh);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &dir_attr);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return nfs4_map_errors(status);
}

static int
nfs4_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsstat *fsstat)
{
	struct nfs4_compound compound;
	struct nfs4_op ops[2];

	nfs4_setup_compound(&compound, ops, server, "statfs");
	nfs4_setup_putfh(&compound, fhandle);
	nfs4_setup_statfs(&compound, fsstat);
	return nfs4_map_errors(nfs4_call_compound(&compound, NULL, 0));
}

static int nfs4_do_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *fsinfo)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FSINFO],
		.rpc_argp = fhandle,
		.rpc_resp = fsinfo,
	};

	return nfs4_map_errors(rpc_call_sync(server->client, &msg, 0));
}

static int nfs4_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsinfo *fsinfo)
{
	fsinfo->fattr->valid = 0;
	return nfs4_map_errors(nfs4_do_fsinfo(server, fhandle, fsinfo));
}

static int
nfs4_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_pathconf *pathconf)
{
	struct nfs4_compound compound;
	struct nfs4_op ops[2];

	nfs4_setup_compound(&compound, ops, server, "statfs");
	nfs4_setup_putfh(&compound, fhandle);
	nfs4_setup_pathconf(&compound, pathconf);
	return nfs4_map_errors(nfs4_call_compound(&compound, NULL, 0));
}

static void
nfs4_read_done(struct rpc_task *task)
{
	struct nfs_read_data *data = (struct nfs_read_data *) task->tk_calldata;
	struct inode *inode = data->inode;

	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return;
	}
	if (task->tk_status > 0)
		renew_lease(NFS_SERVER(inode), data->timestamp);
	/* Call back common NFS readpage processing */
	nfs_readpage_result(task);
}

static void
nfs4_proc_read_setup(struct nfs_read_data *data)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	int flags;

	data->timestamp   = jiffies;

	/* N.B. Do we need to test? Never called for swapfile inode */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_read_done, flags);
	rpc_call_setup(task, &msg, 0);
}

static void
nfs4_write_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return;
	}
	if (task->tk_status >= 0)
		renew_lease(NFS_SERVER(inode), data->timestamp);
	/* Call back common NFS writeback processing */
	nfs_writeback_done(task);
}

static void
nfs4_proc_write_setup(struct nfs_write_data *data, int how)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	int stable;
	int flags;
	
	if (how & FLUSH_STABLE) {
		if (!NFS_I(inode)->ncommit)
			stable = NFS_FILE_SYNC;
		else
			stable = NFS_DATA_SYNC;
	} else
		stable = NFS_UNSTABLE;
	data->args.stable = stable;

	data->timestamp   = jiffies;

	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_write_done, flags);
	rpc_call_setup(task, &msg, 0);
}

static void
nfs4_commit_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return;
	}
	/* Call back common NFS writeback processing */
	nfs_commit_done(task);
}

static void
nfs4_proc_commit_setup(struct nfs_write_data *data, int how)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};	
	struct inode *inode = data->inode;
	int flags;
	
	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_commit_done, flags);
	rpc_call_setup(task, &msg, 0);	
}

/*
 * nfs4_proc_async_renew(): This is not one of the nfs_rpc_ops; it is a special
 * standalone procedure for queueing an asynchronous RENEW.
 */
static void
renew_done(struct rpc_task *task)
{
	struct nfs4_client *clp = (struct nfs4_client *)task->tk_msg.rpc_argp;
	unsigned long timestamp = (unsigned long)task->tk_calldata;

	if (task->tk_status < 0) {
		switch (task->tk_status) {
			case -NFS4ERR_STALE_CLIENTID:
				nfs4_schedule_state_recovery(clp);
				return;
		}
	}
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

int
nfs4_proc_async_renew(struct nfs4_client *clp)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= clp->cl_cred,
	};

	return rpc_call_async(clp->cl_rpcclient, &msg, RPC_TASK_SOFT,
			renew_done, (void *)jiffies);
}

int
nfs4_proc_renew(struct nfs4_client *clp)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= clp->cl_cred,
	};
	unsigned long now = jiffies;
	int status;

	status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,now))
		clp->cl_last_renewal = now;
	spin_unlock(&clp->cl_lock);
	return status;
}

/*
 * We will need to arrange for the VFS layer to provide an atomic open.
 * Until then, this open method is prone to inefficiency and race conditions
 * due to the lookup, potential create, and open VFS calls from sys_open()
 * placed on the wire.
 */
static int
nfs4_proc_file_open(struct inode *inode, struct file *filp)
{
	struct dentry *dentry = filp->f_dentry;
	struct nfs4_state *state;
	struct rpc_cred *cred;

	dprintk("nfs4_proc_file_open: starting on (%.*s/%.*s)\n",
	                       (int)dentry->d_parent->d_name.len,
	                       dentry->d_parent->d_name.name,
	                       (int)dentry->d_name.len, dentry->d_name.name);


	/* Find our open stateid */
	cred = rpcauth_lookupcred(NFS_SERVER(inode)->client->cl_auth, 0);
	state = nfs4_find_state(inode, cred, filp->f_mode);
	put_rpccred(cred);
	if (state == NULL) {
		printk(KERN_WARNING "NFS: v4 raced in function %s\n", __FUNCTION__);
		return -EIO; /* ERACE actually */
	}
	nfs4_close_state(state, filp->f_mode);
	if (filp->f_mode & FMODE_WRITE) {
		lock_kernel();
		nfs_set_mmcred(inode, state->owner->so_cred);
		nfs_begin_data_update(inode);
		unlock_kernel();
	}
	filp->private_data = state;
	return 0;
}

/*
 * Release our state
 */
static int
nfs4_proc_file_release(struct inode *inode, struct file *filp)
{
	struct nfs4_state *state = (struct nfs4_state *)filp->private_data;

	if (state)
		nfs4_close_state(state, filp->f_mode);
	if (filp->f_mode & FMODE_WRITE) {
		lock_kernel();
		nfs_end_data_update(inode);
		unlock_kernel();
	}
	return 0;
}

/*
 * Set up the nfspage struct with the right state info and credentials
 */
static void
nfs4_request_init(struct nfs_page *req, struct file *filp)
{
	struct nfs4_state *state;

	if (!filp) {
		req->wb_cred = get_rpccred(NFS_I(req->wb_inode)->mm_cred);
		req->wb_state = NULL;
		return;
	}
	state = (struct nfs4_state *)filp->private_data;
	req->wb_state = state;
	req->wb_cred = get_rpccred(state->owner->so_cred);
	req->wb_lockowner = current->files;
}

static int
nfs4_async_handle_error(struct rpc_task *task, struct nfs_server *server)
{
	struct nfs4_client *clp = server->nfs4_state;

	if (!clp || task->tk_status >= 0)
		return 0;
	switch(task->tk_status) {
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			rpc_sleep_on(&clp->cl_rpcwaitq, task, NULL, NULL);
			nfs4_schedule_state_recovery(clp);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			rpc_delay(task, NFS4_POLL_RETRY_TIME);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_OLD_STATEID:
			task->tk_status = 0;
			return -EAGAIN;
	}
	task->tk_status = nfs4_map_errors(task->tk_status);
	return 0;
}

int
nfs4_wait_clnt_recover(struct rpc_clnt *clnt, struct nfs4_client *clp)
{
	DEFINE_WAIT(wait);
	sigset_t oldset;
	int interruptible, res;

	might_sleep();

	rpc_clnt_sigmask(clnt, &oldset);
	interruptible = TASK_UNINTERRUPTIBLE;
	if (clnt->cl_intr)
		interruptible = TASK_INTERRUPTIBLE;
	do {
		res = 0;
		prepare_to_wait(&clp->cl_waitq, &wait, interruptible);
		nfs4_schedule_state_recovery(clp);
		if (test_bit(NFS4CLNT_OK, &clp->cl_state) &&
				!test_bit(NFS4CLNT_SETUP_STATE, &clp->cl_state))
			break;
		if (clnt->cl_intr && signalled()) {
			res = -ERESTARTSYS;
			break;
		}
		schedule();
	} while(!test_bit(NFS4CLNT_OK, &clp->cl_state));
	finish_wait(&clp->cl_waitq, &wait);
	rpc_clnt_sigunmask(clnt, &oldset);
	return res;
}

static int
nfs4_delay(struct rpc_clnt *clnt)
{
	sigset_t oldset;
	int res = 0;

	might_sleep();

	rpc_clnt_sigmask(clnt, &oldset);
	if (clnt->cl_intr) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(NFS4_POLL_RETRY_TIME);
		if (signalled())
			res = -ERESTARTSYS;
	} else {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(NFS4_POLL_RETRY_TIME);
	}
	rpc_clnt_sigunmask(clnt, &oldset);
	return res;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
int
nfs4_handle_error(struct nfs_server *server, int errorcode)
{
	struct nfs4_client *clp = server->nfs4_state;
	int ret = errorcode;

	switch(errorcode) {
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			ret = nfs4_wait_clnt_recover(server->client, clp);
			break;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			ret = nfs4_delay(server->client);
			break;
		case -NFS4ERR_OLD_STATEID:
			ret = 0;
	}
	/* We failed to handle the error */
	return nfs4_map_errors(ret);
}


static int
nfs4_request_compatible(struct nfs_page *req, struct file *filp, struct page *page)
{
	struct nfs4_state *state = NULL;
	struct rpc_cred *cred = NULL;

	if (req->wb_file != filp)
		return 0;
	if (req->wb_page != page)
		return 0;
	state = (struct nfs4_state *)filp->private_data;
	if (req->wb_state != state)
		return 0;
	if (req->wb_lockowner != current->files)
		return 0;
	cred = state->owner->so_cred;
	if (req->wb_cred != cred)
		return 0;
	return 1;
}

int
nfs4_proc_setclientid(struct nfs4_client *clp,
		u32 program, unsigned short port)
{
	u32 *p;
	struct nfs4_setclientid setclientid;
	struct timespec tv;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID],
		.rpc_argp = &setclientid,
		.rpc_resp = clp,
		.rpc_cred = clp->cl_cred,
	};

	tv = CURRENT_TIME;
	p = (u32*)setclientid.sc_verifier.data;
	*p++ = (u32)tv.tv_sec;
	*p = (u32)tv.tv_nsec;
	setclientid.sc_name = clp->cl_ipaddr;
	sprintf(setclientid.sc_netid, "tcp");
	sprintf(setclientid.sc_uaddr, "%s.%d.%d", clp->cl_ipaddr, port >> 8, port & 255);
	setclientid.sc_prog = htonl(program);
	setclientid.sc_cb_ident = 0;

	return rpc_call_sync(clp->cl_rpcclient, &msg, 0);
}

int
nfs4_proc_setclientid_confirm(struct nfs4_client *clp)
{
	struct nfs_fsinfo fsinfo;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID_CONFIRM],
		.rpc_argp = clp,
		.rpc_resp = &fsinfo,
		.rpc_cred = clp->cl_cred,
	};
	unsigned long now;
	int status;

	now = jiffies;
	status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
	if (status == 0) {
		spin_lock(&clp->cl_lock);
		clp->cl_lease_time = fsinfo.lease_time * HZ;
		clp->cl_last_renewal = now;
		spin_unlock(&clp->cl_lock);
	}
	return status;
}

#define NFS4_LOCK_MINTIMEOUT (1 * HZ)
#define NFS4_LOCK_MAXTIMEOUT (30 * HZ)

/* 
 * sleep, with exponential backoff, and retry the LOCK operation. 
 */
static unsigned long
nfs4_set_lock_task_retry(unsigned long timeout)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(timeout);
	timeout <<= 1;
	if (timeout > NFS4_LOCK_MAXTIMEOUT)
		return NFS4_LOCK_MAXTIMEOUT;
	return timeout;
}

static inline int
nfs4_lck_type(int cmd, struct file_lock *request)
{
	/* set lock type */
	switch (request->fl_type) {
		case F_RDLCK:
			return IS_SETLKW(cmd) ? NFS4_READW_LT : NFS4_READ_LT;
		case F_WRLCK:
			return IS_SETLKW(cmd) ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
		case F_UNLCK:
			return NFS4_WRITE_LT; 
	}
	BUG();
	return 0;
}

static inline uint64_t
nfs4_lck_length(struct file_lock *request)
{
	if (request->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return request->fl_end - request->fl_start + 1;
}

int
nfs4_proc_getlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs_lockargs arg = {
		.fh = NFS_FH(inode),
		.type = nfs4_lck_type(cmd, request),
		.offset = request->fl_start,
		.length = nfs4_lck_length(request),
	};
	struct nfs_lockres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCKT],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs_lowner nlo;
	struct nfs4_lock_state *lsp;
	int status;

	nlo.clientid = clp->cl_clientid;
	down(&state->lock_sema);
	lsp = nfs4_find_lock_state(state, request->fl_owner);
	if (lsp)
		nlo.id = lsp->ls_id; 
	else {
		spin_lock(&clp->cl_lock);
		nlo.id = nfs4_alloc_lockowner_id(clp);
		spin_unlock(&clp->cl_lock);
	}
	arg.u.lockt = &nlo;
	status = rpc_call_sync(server->client, &msg, 0);
	if (!status) {
		request->fl_type = F_UNLCK;
	} else if (status == -NFS4ERR_DENIED) {
		int64_t len, start, end;
		start = res.u.denied.offset;
		len = res.u.denied.length;
		end = start + len - 1;
		if (end < 0 || len == 0)
			request->fl_end = OFFSET_MAX;
		else
			request->fl_end = (loff_t)end;
		request->fl_start = (loff_t)start;
		request->fl_type = F_WRLCK;
		if (res.u.denied.type & 1)
			request->fl_type = F_RDLCK;
		request->fl_pid = 0;
		status = 0;
	}
	if (lsp)
		nfs4_put_lock_state(lsp);
	up(&state->lock_sema);
	return nfs4_map_errors(status);
}

int
nfs4_proc_unlck(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_lockargs arg = {
		.fh = NFS_FH(inode),
		.type = nfs4_lck_type(cmd, request),
		.offset = request->fl_start,
		.length = nfs4_lck_length(request),
	};
	struct nfs_lockres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCKU],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs4_lock_state *lsp;
	struct nfs_locku_opargs luargs;
	int status = 0;
			
	down(&state->lock_sema);
	lsp = nfs4_find_lock_state(state, request->fl_owner);
	if (!lsp)
		goto out;
	luargs.seqid = lsp->ls_seqid;
	memcpy(&luargs.stateid, &lsp->ls_stateid, sizeof(luargs.stateid));
	arg.u.locku = &luargs;
	status = rpc_call_sync(server->client, &msg, 0);
	nfs4_increment_lock_seqid(status, lsp);

	if (status == 0) {
		memcpy(&lsp->ls_stateid,  &res.u.stateid, 
				sizeof(lsp->ls_stateid));
		nfs4_notify_unlck(inode, request, lsp);
	}
	nfs4_put_lock_state(lsp);
out:
	up(&state->lock_sema);
	return nfs4_map_errors(status);
}

static int
nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_lock_state *lsp;
	struct nfs_lockargs arg = {
		.fh = NFS_FH(inode),
		.type = nfs4_lck_type(cmd, request),
		.offset = request->fl_start,
		.length = nfs4_lck_length(request),
	};
	struct nfs_lockres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCK],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs_lock_opargs largs = {
		.new_lock_owner = 0,
	};
	int status;

	down(&state->lock_sema);
	lsp = nfs4_find_lock_state(state, request->fl_owner);
	if (lsp == NULL) {
		struct nfs4_state_owner *owner = state->owner;
		struct nfs_open_to_lock otl = {
			.lock_owner = {
				.clientid = server->nfs4_state->cl_clientid,
			},
		};
		status = -ENOMEM;
		lsp = nfs4_alloc_lock_state(state, request->fl_owner);
		if (!lsp)
			goto out;
		otl.lock_seqid = lsp->ls_seqid;
		otl.lock_owner.id = lsp->ls_id;
		memcpy(&otl.open_stateid, &state->stateid, sizeof(otl.open_stateid));
		largs.u.open_lock = &otl;
		largs.new_lock_owner = 1;
		arg.u.lock = &largs;
		down(&owner->so_sema);
		otl.open_seqid = owner->so_seqid;
		status = rpc_call_sync(server->client, &msg, 0);
		/* increment open_owner seqid on success, and 
		* seqid mutating errors */
		nfs4_increment_seqid(status, owner);
		up(&owner->so_sema);
	} else {
		struct nfs_exist_lock el = {
			.seqid = lsp->ls_seqid,
		};
		memcpy(&el.stateid, &lsp->ls_stateid, sizeof(el.stateid));
		largs.u.exist_lock = &el;
		largs.new_lock_owner = 0;
		arg.u.lock = &largs;
		status = rpc_call_sync(server->client, &msg, 0);
	}
	/* increment seqid on success, and * seqid mutating errors*/
	nfs4_increment_lock_seqid(status, lsp);
	/* save the returned stateid. */
	if (status == 0) {
		memcpy(&lsp->ls_stateid, &res.u.stateid, sizeof(nfs4_stateid));
		nfs4_notify_setlk(inode, request, lsp);
	} else if (status == -NFS4ERR_DENIED)
		status = -EAGAIN;
	nfs4_put_lock_state(lsp);
out:
	up(&state->lock_sema);
	return nfs4_map_errors(status);
}

static int
nfs4_proc_lock(struct file *filp, int cmd, struct file_lock *request)
{
	struct nfs4_state *state;
	unsigned long timeout = NFS4_LOCK_MINTIMEOUT;
	int status;

	/* verify open state */
	state = (struct nfs4_state *)filp->private_data;
	BUG_ON(!state);

	if (request->fl_start < 0 || request->fl_end < 0)
		return -EINVAL;

	if (IS_GETLK(cmd))
		return nfs4_proc_getlk(state, F_GETLK, request);

	if (!(IS_SETLK(cmd) || IS_SETLKW(cmd)))
		return -EINVAL;

	if (request->fl_type == F_UNLCK)
		return nfs4_proc_unlck(state, cmd, request);

	do {
		status = nfs4_proc_setlk(state, cmd, request);
		if ((status != -EAGAIN) || IS_SETLK(cmd))
			break;
		timeout = nfs4_set_lock_task_retry(timeout);
		status = -ERESTARTSYS;
		if (signalled())
			break;
	} while(status < 0);

	return status;
}

struct nfs_rpc_ops	nfs_v4_clientops = {
	.version	= 4,			/* protocol version */
	.dentry_ops	= &nfs4_dentry_operations,
	.dir_inode_ops	= &nfs4_dir_inode_operations,
	.getroot	= nfs4_proc_get_root,
	.getattr	= nfs4_proc_getattr,
	.setattr	= nfs4_proc_setattr,
	.lookup		= nfs4_proc_lookup,
	.access		= nfs4_proc_access,
	.readlink	= nfs4_proc_readlink,
	.read		= nfs4_proc_read,
	.write		= nfs4_proc_write,
	.commit		= nfs4_proc_commit,
	.create		= nfs4_proc_create,
	.remove		= nfs4_proc_remove,
	.unlink_setup	= nfs4_proc_unlink_setup,
	.unlink_done	= nfs4_proc_unlink_done,
	.rename		= nfs4_proc_rename,
	.link		= nfs4_proc_link,
	.symlink	= nfs4_proc_symlink,
	.mkdir		= nfs4_proc_mkdir,
	.rmdir		= nfs4_proc_remove,
	.readdir	= nfs4_proc_readdir,
	.mknod		= nfs4_proc_mknod,
	.statfs		= nfs4_proc_statfs,
	.fsinfo		= nfs4_proc_fsinfo,
	.pathconf	= nfs4_proc_pathconf,
	.decode_dirent	= nfs4_decode_dirent,
	.read_setup	= nfs4_proc_read_setup,
	.write_setup	= nfs4_proc_write_setup,
	.commit_setup	= nfs4_proc_commit_setup,
	.file_open      = nfs4_proc_file_open,
	.file_release   = nfs4_proc_file_release,
	.request_init	= nfs4_request_init,
	.request_compatible = nfs4_request_compatible,
	.lock		= nfs4_proc_lock,
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
