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

#define NFSDBG_FACILITY		NFSDBG_PROC

#define GET_OP(cp,name)		&cp->ops[cp->req_nops].u.name
#define OPNUM(cp)		cp->ops[cp->req_nops].opnum

extern u32 *nfs4_decode_dirent(u32 *p, struct nfs_entry *entry, int plus);
extern struct rpc_procinfo nfs4_procedures[];

static nfs4_stateid zero_stateid =
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static spinlock_t renew_lock = SPIN_LOCK_UNLOCKED;

static void
nfs4_setup_compound(struct nfs4_compound *cp, struct nfs4_op *ops,
		    struct nfs_server *server, char *tag)
{
	memset(cp, 0, sizeof(*cp));
	cp->ops = ops;
	cp->server = server;
}

static void
nfs4_setup_access(struct nfs4_compound *cp, u32 req_access, u32 *resp_supported, u32 *resp_access)
{
	struct nfs4_access *access = GET_OP(cp, access);
	
	access->ac_req_access = req_access;
	access->ac_resp_supported = resp_supported;
	access->ac_resp_access = resp_access;
	
	OPNUM(cp) = OP_ACCESS;
	cp->req_nops++;
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

u32 nfs4_fsinfo_bitmap[2] = {
	FATTR4_WORD0_MAXFILESIZE
	| FATTR4_WORD0_MAXREAD
        | FATTR4_WORD0_MAXWRITE
	| FATTR4_WORD0_LEASE_TIME,
	0
};

u32 nfs4_pathconf_bitmap[2] = {
	FATTR4_WORD0_MAXLINK
	| FATTR4_WORD0_MAXNAME,
	0
};

/* mount bitmap: fattr bitmap + lease time */
u32 nfs4_mount_bitmap[2] = {
	FATTR4_WORD0_TYPE
	| FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_SIZE
	| FATTR4_WORD0_FSID
	| FATTR4_WORD0_FILEID
	| FATTR4_WORD0_LEASE_TIME,
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

static inline void
__nfs4_setup_getattr(struct nfs4_compound *cp, u32 *bitmap,
		     struct nfs_fattr *fattr,
		     struct nfs_fsstat *fsstat,
		     struct nfs_fsinfo *fsinfo,
		     struct nfs_pathconf *pathconf,
		     u32 *bmres)
{
        struct nfs4_getattr *getattr = GET_OP(cp, getattr);

        getattr->gt_bmval = bitmap;
        getattr->gt_attrs = fattr;
	getattr->gt_fsstat = fsstat;
	getattr->gt_fsinfo = fsinfo;
	getattr->gt_pathconf = pathconf;
	getattr->gt_bmres = bmres;

        OPNUM(cp) = OP_GETATTR;
        cp->req_nops++;
}

static void
nfs4_setup_getattr(struct nfs4_compound *cp,
		struct nfs_fattr *fattr,
		u32 *bmres)
{
	__nfs4_setup_getattr(cp, nfs4_fattr_bitmap, fattr,
			NULL, NULL, NULL, bmres);
}

static void
nfs4_setup_getrootattr(struct nfs4_compound *cp,
		struct nfs_fattr *fattr,
		struct nfs_fsinfo *fsinfo,
		u32 *bmres)
{
	__nfs4_setup_getattr(cp, nfs4_mount_bitmap,
			fattr, NULL, fsinfo, NULL, bmres);
}

static void
nfs4_setup_statfs(struct nfs4_compound *cp,
		struct nfs_fsstat *fsstat,
		u32 *bmres)
{
	__nfs4_setup_getattr(cp, nfs4_statfs_bitmap,
			NULL, fsstat, NULL, NULL, bmres);
}

static void
nfs4_setup_fsinfo(struct nfs4_compound *cp,
		struct nfs_fsinfo *fsinfo,
		u32 *bmres)
{
	__nfs4_setup_getattr(cp, nfs4_fsinfo_bitmap,
			NULL, NULL, fsinfo, NULL, bmres);
}

static void
nfs4_setup_pathconf(struct nfs4_compound *cp,
		struct nfs_pathconf *pathconf,
		u32 *bmres)
{
	__nfs4_setup_getattr(cp, nfs4_pathconf_bitmap,
			NULL, NULL, NULL, pathconf, bmres);
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
nfs4_setup_lookup(struct nfs4_compound *cp, struct qstr *q)
{
	struct nfs4_lookup *lookup = GET_OP(cp, lookup);

	lookup->lo_name = q;

	OPNUM(cp) = OP_LOOKUP;
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
nfs4_setup_putrootfh(struct nfs4_compound *cp)
{
        OPNUM(cp) = OP_PUTROOTFH;
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
	memcpy(readdir->rd_req_verifier, verifier, sizeof(nfs4_verifier));
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
nfs4_setup_renew(struct nfs4_compound *cp)
{
	struct nfs4_client **client_state = GET_OP(cp, renew);

	*client_state = cp->server->nfs4_state;

	OPNUM(cp) = OP_RENEW;
	cp->req_nops++;
	cp->renew_index = cp->req_nops;
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
nfs4_setup_setattr(struct nfs4_compound *cp, char *stateid, struct iattr *iap)
{
	struct nfs4_setattr *setattr = GET_OP(cp, setattr);

	setattr->st_stateid = stateid;
	setattr->st_iap = iap;
	
	OPNUM(cp) = OP_SETATTR;
	cp->req_nops++;
}

static void
nfs4_setup_setclientid(struct nfs4_compound *cp, u32 program, unsigned short port)
{
	struct nfs4_setclientid *setclientid = GET_OP(cp, setclientid);
	struct nfs_server *server = cp->server;
	struct timespec tv;
	u32 *p;

	tv = CURRENT_TIME;
 	p = (u32 *)setclientid->sc_verifier;
	*p++ = tv.tv_sec;
	*p++ = tv.tv_nsec;
	setclientid->sc_name = server->ip_addr;
	sprintf(setclientid->sc_netid, "udp");
	sprintf(setclientid->sc_uaddr, "%s.%d.%d", server->ip_addr, port >> 8, port & 255);
	setclientid->sc_prog = program;
	setclientid->sc_cb_ident = 0;
	setclientid->sc_state = server->nfs4_state;
	
	OPNUM(cp) = OP_SETCLIENTID;
	cp->req_nops++;
}

static void
nfs4_setup_setclientid_confirm(struct nfs4_compound *cp)
{
	struct nfs4_client **client_state = GET_OP(cp, setclientid_confirm);

	*client_state = cp->server->nfs4_state;

	OPNUM(cp) = OP_SETCLIENTID_CONFIRM;
	cp->req_nops++;
	cp->renew_index = cp->req_nops;
}

static void
renew_lease(struct nfs_server *server, unsigned long timestamp)
{
	spin_lock(&renew_lock);
	if (time_before(server->last_renewal,timestamp))
		server->last_renewal = timestamp;
	spin_unlock(&renew_lock);
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

int
nfs4_do_open(struct inode *dir, struct qstr *name, int flags,
		struct iattr *sattr, struct nfs_fattr *fattr,
		struct nfs_fh *fhandle, struct nfs4_shareowner **spp)
{
	struct nfs4_shareowner  *sp;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct nfs4_change_info d_cinfo;
	int                     status;
	u32                     f_bmres[2];
	u32                     d_bmres[2];
	struct nfs_fattr        d_attr = {
		.valid          0,
	};
	struct nfs_fattr        f_attr = {
		.valid          0,
	};
	struct nfs4_getattr     f_getattr = {
		.gt_bmval       = nfs4_fattr_bitmap,
		.gt_attrs       = (fattr == NULL ? &f_attr: fattr),
		.gt_bmres       = f_bmres,
	};
	struct nfs4_getattr     d_getattr = {
		.gt_bmval       = nfs4_fattr_bitmap,
		.gt_attrs       = &d_attr,
		.gt_bmres       = d_bmres,
	};
	struct nfs_openargs o_arg = {
		.fh             = NFS_FH(dir),
		.share_access   = flags & O_ACCMODE,
		.clientid       = NFS_SERVER(dir)->nfs4_state->cl_clientid,
		.opentype       = (flags & O_CREAT) ? NFS4_OPEN_CREATE : NFS4_OPEN_NOCREATE,
		.createmode     = (flags & O_EXCL) ? NFS4_CREATE_EXCLUSIVE : NFS4_CREATE_UNCHECKED,
		.name           = name,
		.f_getattr      = &f_getattr,
		.d_getattr      = &d_getattr,
	};
	struct nfs_openres o_res = {
		.cinfo          = &d_cinfo,
		.f_getattr      = &f_getattr,
		.d_getattr      = &d_getattr,
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN],
		.rpc_argp       = &o_arg,
		.rpc_resp       = &o_res,
	};

	status = -ENOMEM;
	if (!(sp = nfs4_get_shareowner(dir))) {
		dprintk("nfs4_do_open: nfs4_get_shareowner failed!\n");
		goto out;
	}
	if (o_arg.createmode & NFS4_CREATE_EXCLUSIVE){
		u32 *p = (u32 *) o_arg.u.verifier;
		p[0] = jiffies;
		p[1] = current->pid;
	} else if (o_arg.createmode == NFS4_CREATE_UNCHECKED) {
		o_arg.u.attrs = sattr;
	}
	/* Serialization for the sequence id */
	down(&sp->so_sema);
	o_arg.seqid = sp->so_seqid;
	o_arg.id = sp->so_id;

	status = rpc_call_sync(server->client, &msg, 0);
	if (status) {
		goto out_up;
	}
	nfs4_increment_seqid(status, sp);
	process_cinfo(&d_cinfo, &d_attr);
	nfs_refresh_inode(dir, &d_attr);

	if (fhandle) {
		memset(fhandle, 0, sizeof(*fhandle));
		fhandle->size = (o_res.fh.size < NFS_MAXFHSIZE ? o_res.fh.size : NFS_MAXFHSIZE);
		memcpy(fhandle->data, o_res.fh.data, fhandle->size);
	}

	if(o_res.rflags & NFS4_OPEN_RESULT_CONFIRM) {
		struct nfs_open_confirmargs oc_arg = {
			.fh             = &o_res.fh,
			.seqid          = sp->so_seqid,
		};
		struct nfs_open_confirmres oc_res = {
			.status         = 0,
		};
		struct 	rpc_message msg = {
			.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN_CONFIRM],
			.rpc_argp       = &oc_arg,
			.rpc_resp       = &oc_res,
		};

		memcpy(oc_arg.stateid, o_res.stateid, sizeof(nfs4_stateid));
		status = rpc_call_sync(server->client, &msg, 0);
		if (status)
			goto out_up;
		nfs4_increment_seqid(status, sp);
		memcpy(sp->so_stateid, oc_res.stateid, sizeof(nfs4_stateid));
	} else
		memcpy(sp->so_stateid, o_res.stateid, sizeof(nfs4_stateid));
	sp->so_flags = flags & O_ACCMODE;

out_up:
	up(&sp->so_sema);
out:
	*spp = sp;
	return status;
}

static int
do_setattr(struct nfs_server *server, struct nfs_fattr *fattr,
	   struct nfs_fh *fhandle, struct iattr *sattr, char *stateid)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[3];
	u32			bmres[2];

	fattr->valid = 0;
	nfs4_setup_compound(&compound, ops, server, "setattr");
	nfs4_setup_putfh(&compound, fhandle);
	nfs4_setup_setattr(&compound, stateid, sattr);
	nfs4_setup_getattr(&compound, fattr, bmres);
	return nfs4_call_compound(&compound, NULL, 0);
}

/* 
 * It is possible for data to be read/written from a mem-mapped file 
 * after the sys_close call (which hits the vfs layer as a flush).
 * This means that we can't safely call nfsv4 close on a file until 
 * the inode is cleared. This in turn means that we are not good
 * NFSv4 citizens - we do not indicate to the server to update the file's 
 * share state even when we are done with one of the three share 
 * stateid's in the inode.
 */
int
nfs4_do_close(struct inode *inode, struct nfs4_shareowner *sp) 
{
	int status = 0;
	struct nfs_closeargs arg = {
		.fh		= NFS_FH(inode),
	};
	struct nfs_closeres res = {
		.status		= 0,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_CLOSE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};

        memcpy(arg.stateid, sp->so_stateid, sizeof(nfs4_stateid));
	/* Serialization for the sequence id */
	down(&sp->so_sema);
	arg.seqid = sp->so_seqid,
	status = rpc_call_sync(NFS_SERVER(inode)->client, &msg, 0);

        /* hmm. we are done with the inode, and in the process of freeing
	 * the shareowner. we keep this around to process errors
	 */
	nfs4_increment_seqid(status, sp);
	up(&sp->so_sema);

	return status;
}

static int
nfs4_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[4];
	struct nfs_fsinfo	fsinfo;
	u32			bmres[2];
	unsigned char *		p;
	struct qstr		q;
	int			status;

	fattr->valid = 0;

	if (!(server->nfs4_state = nfs4_get_client()))
		return -ENOMEM;

	/* 
	 * SETCLIENTID.
	 * Until delegations are imported, we don't bother setting the program
	 * number and port to anything meaningful.
	 */
	nfs4_setup_compound(&compound, ops, server, "setclientid");
	nfs4_setup_setclientid(&compound, 0, 0);
	if ((status = nfs4_call_compound(&compound, NULL, 0)))
		goto out;

	/*
	 * SETCLIENTID_CONFIRM, plus root filehandle.
	 * We also get the lease time here.
	 */
	nfs4_setup_compound(&compound, ops, server, "setclientid_confirm");
	nfs4_setup_setclientid_confirm(&compound);
	nfs4_setup_putrootfh(&compound);
	nfs4_setup_getrootattr(&compound, fattr, &fsinfo, bmres);
	nfs4_setup_getfh(&compound, fhandle);
	if ((status = nfs4_call_compound(&compound, NULL, 0)))
		goto out;
	
	/*
	 * Now that we have instantiated the clientid and determined
	 * the lease time, we can initialize the renew daemon for this
	 * server.
	 */
	server->lease_time = fsinfo.lease_time * HZ;
	if ((status = nfs4_init_renewd(server)))
		goto out;
	
	/*
	 * Now we do a separate LOOKUP for each component of the mount path.
	 * The LOOKUPs are done separately so that we can conveniently
	 * catch an ERR_WRONGSEC if it occurs along the way...
	 */
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

		nfs4_setup_compound(&compound, ops, server, "mount");
		nfs4_setup_putfh(&compound, fhandle);
		nfs4_setup_lookup(&compound, &q);
		nfs4_setup_getattr(&compound, fattr, bmres);
		nfs4_setup_getfh(&compound, fhandle);
		status = nfs4_call_compound(&compound, NULL, 0);
		if (!status)
			continue;
		if (status == -ENOENT) {
			printk(KERN_NOTICE "NFS: mount path %s does not exist!\n", server->mnt_path);
			printk(KERN_NOTICE "NFS: suggestion: try mounting '/' instead.\n");
		}
		break;
	}

out:
	return status;
}

static int
nfs4_proc_getattr(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs4_compound compound;
	struct nfs4_op ops[2];
	u32 bmres[2];

	fattr->valid = 0;

	nfs4_setup_compound(&compound, ops, NFS_SERVER(inode), "getattr");
	nfs4_setup_putfh(&compound, NFS_FH(inode));
	nfs4_setup_getattr(&compound, fattr, bmres);
	return nfs4_call_compound(&compound, NULL, 0);
}

/* 
 * The file is not closed if it is opened due to the a request to change
 * the size of the file. The open call will not be needed once the
 * VFS layer lookup-intents are implemented.
 * Close is called when the inode is destroyed.
 */
static int
nfs4_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
		  struct iattr *sattr)
{
	struct inode *		inode = dentry->d_inode;
	int			size_change = sattr->ia_valid & ATTR_SIZE;
	struct nfs_fh		throwaway_fh;
	struct nfs4_shareowner	*sp = NULL;
	int			status, fake = 1;

	fattr->valid = 0;
	
	if (size_change) {
		status = nfs4_do_open(dentry->d_parent->d_inode, 
				&dentry->d_name,
				O_WRONLY, NULL, fattr,
				&throwaway_fh,&sp);
		if (status)
			return status;
		fake = 0;
		/*
		 * Because OPEN is always done by name in nfsv4, it is
		 * possible that we opened a different file by the same
		 * name.  We can recognize this race condition, but we
		 * can't do anything about it besides returning an error.
		 *
		 * XXX: Should we compare filehandles too, as in
		 * nfs_find_actor()?
		 */
		if (fattr->fileid != NFS_FILEID(inode)) {
			printk(KERN_WARNING "nfs: raced in setattr, returning -EIO\n");
			return -EIO;
		}
	}
	
	status = do_setattr(NFS_SERVER(inode), fattr, NFS_FH(inode), sattr, 
	                    fake == 1? zero_stateid: sp->so_stateid);
	return status;
}

static int
nfs4_proc_lookup(struct inode *dir, struct qstr *name,
		 struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[5];
	struct nfs_fattr	dir_attr;
	u32			dir_bmres[2];
	u32			bmres[2];
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	dprintk("NFS call  lookup %s\n", name->name);
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "lookup");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_getattr(&compound, &dir_attr, dir_bmres);
	nfs4_setup_lookup(&compound, name);
	nfs4_setup_getattr(&compound, fattr, bmres);
	nfs4_setup_getfh(&compound, fhandle);
	status = nfs4_call_compound(&compound, NULL, 0);
	dprintk("NFS reply lookup: %d\n", status);

	if (status >= 0)
		status = nfs_refresh_inode(dir, &dir_attr);
	return status;
}

static int
nfs4_proc_access(struct inode *inode, struct rpc_cred *cred, int mode)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[3];
	struct nfs_fattr	fattr;
	u32			bmres[2];
	u32			req_access = 0, resp_supported, resp_access;
	int			status;

	fattr.valid = 0;

	/*
	 * Determine which access bits we want to ask for...
	 */
	if (mode & MAY_READ)
		req_access |= NFS4_ACCESS_READ;
	if (S_ISDIR(inode->i_mode)) {
		if (mode & MAY_WRITE)
			req_access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE;
		if (mode & MAY_EXEC)
			req_access |= NFS4_ACCESS_LOOKUP;
	}
	else {
		if (mode & MAY_WRITE)
			req_access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND;
		if (mode & MAY_EXEC)
			req_access |= NFS4_ACCESS_EXECUTE;
	}

	nfs4_setup_compound(&compound, ops, NFS_SERVER(inode), "access");
	nfs4_setup_putfh(&compound, NFS_FH(inode));
	nfs4_setup_getattr(&compound, &fattr, bmres);
	nfs4_setup_access(&compound, req_access, &resp_supported, &resp_access);
	status = nfs4_call_compound(&compound, cred, 0);
	nfs_refresh_inode(inode, &fattr);

	if (!status) {
		if (req_access != resp_supported) {
			printk(KERN_NOTICE "NFS: server didn't support all access bits!\n");
			status = -ENOTSUPP;
		}
		else if (req_access != resp_access)
			status = -EACCES;
	}
	return status;
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
	return nfs4_call_compound(&compound, NULL, 0);
}

static int
nfs4_proc_read(struct inode *inode, struct rpc_cred *cred,
	       struct nfs_fattr *fattr, int flags,
	       unsigned int base, unsigned int count,
	       struct page *page, int *eofp)
{
	struct nfs_server *server = NFS_SERVER(inode);
	uint64_t offset = page_offset(page) + base;
	struct nfs_readargs arg = {
		.fh		= NFS_FH(inode),
		.offset		= offset,
		.count		= count,
		.pgbase		= base,
		.pages		= &page,
	};
	struct nfs_readres res = {
		.fattr		= fattr,
		.count		= count,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	unsigned long timestamp = jiffies;
	int status;

	dprintk("NFS call  read %d @ %Ld\n", count, (long long)offset);
	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, flags);
	if (!status) {
		renew_lease(server, timestamp);
		/* Check cache consistency */
		if (fattr->change_attr != NFS_CHANGE_ATTR(inode))
			nfs_zap_caches(inode);
	}
	dprintk("NFS reply read: %d\n", status);
	*eofp = res.eof;
	return status;
}

static int
nfs4_proc_write(struct inode *inode, struct rpc_cred *cred,
		struct nfs_fattr *fattr, int flags,
		unsigned int base, unsigned int count,
		struct page *page, struct nfs_writeverf *verf)
{
	struct nfs_server *server = NFS_SERVER(inode);
	uint64_t offset = page_offset(page) + base;
	struct nfs_writeargs arg = {
		.fh		= NFS_FH(inode),
		.offset		= offset,
		.count		= count,
		.stable		= (flags & NFS_RW_SYNC) ? NFS_FILE_SYNC : NFS_UNSTABLE,
		.pgbase		= base,
		.pages		= &page,
	};
	struct nfs_writeres res = {
		.fattr		= fattr,
		.count		= count,
		.verf		= verf,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	int			rpcflags = (flags & NFS_RW_SWAP) ? NFS_RPC_SWAPFLAGS : 0;

	dprintk("NFS call  write %d @ %Ld\n", count, (long long)offset);
	fattr->valid = 0;
	return rpc_call_sync(server->client, &msg, rpcflags);
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

static int
nfs4_proc_create(struct inode *dir, struct qstr *name, struct iattr *sattr,
                 int flags, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int                     oflags;
	struct nfs4_shareowner   *sp = NULL;
	int                     status;

	oflags = O_RDONLY | O_CREAT | (flags & O_EXCL);
	status = nfs4_do_open(dir, name, oflags, sattr, fattr, fhandle, &sp);
	if (!status) {
		if (flags & O_EXCL) {
			status = do_setattr(NFS_SERVER(dir), fattr,
			                     fhandle, sattr, sp->so_stateid);
		/* XXX should i bother closing the file? */
		}
	}
	return status;
}

static int
nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[3];
	struct nfs4_change_info	dir_cinfo;
	struct nfs_fattr	dir_attr;
	u32			dir_bmres[2];
	int			status;

	dir_attr.valid = 0;
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "remove");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_remove(&compound, name, &dir_cinfo);
	nfs4_setup_getattr(&compound, &dir_attr, dir_bmres);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return status;
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
	u32			bmres[2];

	up = (struct unlink_desc *) kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;
	cp = &up->compound;
	
	nfs4_setup_compound(cp, up->ops, NFS_SERVER(dir->d_inode), "unlink_setup");
	nfs4_setup_putfh(cp, NFS_FH(dir->d_inode));
	nfs4_setup_remove(cp, name, &up->cinfo);
	nfs4_setup_getattr(cp, &up->attrs, bmres);
	
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
	u32			old_dir_bmres[2], new_dir_bmres[2];
	int			status;

	old_dir_attr.valid = 0;
	new_dir_attr.valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(old_dir), "rename");
	nfs4_setup_putfh(&compound, NFS_FH(old_dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_putfh(&compound, NFS_FH(new_dir));
	nfs4_setup_rename(&compound, old_name, new_name, &old_cinfo, &new_cinfo);
	nfs4_setup_getattr(&compound, &new_dir_attr, new_dir_bmres);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &old_dir_attr, old_dir_bmres);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&old_cinfo, &old_dir_attr);
		process_cinfo(&new_cinfo, &new_dir_attr);
		nfs_refresh_inode(old_dir, &old_dir_attr);
		nfs_refresh_inode(new_dir, &new_dir_attr);
	}
	return status;
}

static int
nfs4_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs4_change_info	dir_cinfo;
	struct nfs_fattr	dir_attr, fattr;
	u32			dir_bmres[2], bmres[2];
	int			status;
	
	dir_attr.valid = 0;
	fattr.valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(inode), "link");
	nfs4_setup_putfh(&compound, NFS_FH(inode));
	nfs4_setup_savefh(&compound);
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_link(&compound, name, &dir_cinfo);
	nfs4_setup_getattr(&compound, &dir_attr, dir_bmres);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &fattr, bmres);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
		nfs_refresh_inode(inode, &fattr);
	}
	return status;
}

static int
nfs4_proc_symlink(struct inode *dir, struct qstr *name, struct qstr *path,
		  struct iattr *sattr, struct nfs_fh *fhandle,
		  struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs_fattr	dir_attr;
	u32			dir_bmres[2], bmres[2];
	struct nfs4_change_info	dir_cinfo;
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "symlink");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_create_symlink(&compound, name, path, sattr, &dir_cinfo);
	nfs4_setup_getattr(&compound, fattr, bmres);
	nfs4_setup_getfh(&compound, fhandle);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &dir_attr, dir_bmres);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return status;
}

static int
nfs4_proc_mkdir(struct inode *dir, struct qstr *name, struct iattr *sattr,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs_fattr	dir_attr;
	u32			dir_bmres[2], bmres[2];
	struct nfs4_change_info	dir_cinfo;
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "mkdir");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_create_dir(&compound, name, sattr, &dir_cinfo);
	nfs4_setup_getattr(&compound, fattr, bmres);
	nfs4_setup_getfh(&compound, fhandle);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &dir_attr, dir_bmres);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return status;
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

	unlock_kernel();
	return status;
}

static int
nfs4_proc_mknod(struct inode *dir, struct qstr *name, struct iattr *sattr,
		dev_t rdev, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs4_compound	compound;
	struct nfs4_op		ops[7];
	struct nfs_fattr	dir_attr;
	u32			dir_bmres[2], bmres[2];
	struct nfs4_change_info	dir_cinfo;
	int			status;

	dir_attr.valid = 0;
	fattr->valid = 0;
	
	nfs4_setup_compound(&compound, ops, NFS_SERVER(dir), "mknod");
	nfs4_setup_putfh(&compound, NFS_FH(dir));
	nfs4_setup_savefh(&compound);
	nfs4_setup_create_special(&compound, name, rdev,sattr, &dir_cinfo);
	nfs4_setup_getattr(&compound, fattr, bmres);
	nfs4_setup_getfh(&compound, fh);
	nfs4_setup_restorefh(&compound);
	nfs4_setup_getattr(&compound, &dir_attr, dir_bmres);
	status = nfs4_call_compound(&compound, NULL, 0);

	if (!status) {
		process_cinfo(&dir_cinfo, &dir_attr);
		nfs_refresh_inode(dir, &dir_attr);
	}
	return status;
}

static int
nfs4_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsstat *fsstat)
{
	struct nfs4_compound compound;
	struct nfs4_op ops[2];
	u32 bmres[2];

	memset(fsstat, 0, sizeof(*fsstat));
	nfs4_setup_compound(&compound, ops, server, "statfs");
	nfs4_setup_putfh(&compound, fhandle);
	nfs4_setup_statfs(&compound, fsstat, bmres);
	return nfs4_call_compound(&compound, NULL, 0);
}

static int
nfs4_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsinfo *fsinfo)
{
	struct nfs4_compound compound;
	struct nfs4_op ops[2];
	u32 bmres[2];

	memset(fsinfo, 0, sizeof(*fsinfo));
	nfs4_setup_compound(&compound, ops, server, "statfs");
	nfs4_setup_putfh(&compound, fhandle);
	nfs4_setup_fsinfo(&compound, fsinfo, bmres);
	return nfs4_call_compound(&compound, NULL, 0);
}

static int
nfs4_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_pathconf *pathconf)
{
	struct nfs4_compound compound;
	struct nfs4_op ops[2];
	u32 bmres[2];

	memset(pathconf, 0, sizeof(*pathconf));
	nfs4_setup_compound(&compound, ops, server, "statfs");
	nfs4_setup_putfh(&compound, fhandle);
	nfs4_setup_pathconf(&compound, pathconf, bmres);
	return nfs4_call_compound(&compound, NULL, 0);
}

static void
nfs4_read_done(struct rpc_task *task)
{
	struct nfs_read_data *data = (struct nfs_read_data *) task->tk_calldata;
	struct inode *inode = data->inode;
	struct nfs_fattr *fattr = data->res.fattr;

	if (task->tk_status > 0)
		renew_lease(NFS_SERVER(inode), data->timestamp);
	/* Check cache consistency */
	if (fattr->change_attr != NFS_CHANGE_ATTR(inode))
		nfs_zap_caches(inode);
	if (fattr->bitmap[1] & FATTR4_WORD1_TIME_ACCESS)
		inode->i_atime = fattr->atime;
	/* Call back common NFS readpage processing */
	nfs_readpage_result(task);
}

static void
nfs4_proc_read_setup(struct nfs_read_data *data, unsigned int count)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	struct nfs_page *req = nfs_list_entry(data->pages.next);
	int flags;

	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req);
	data->args.pgbase = req->wb_pgbase;
	data->args.pages  = data->pagevec;
	data->args.count  = count;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.eof     = 0;
	data->timestamp   = jiffies;

	/* N.B. Do we need to test? Never called for swapfile inode */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_read_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_readdata_release;

	rpc_call_setup(task, &msg, 0);
}

static void
nfs4_write_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	/* Check cache consistency */
	if (fattr->pre_change_attr != NFS_CHANGE_ATTR(inode))
		nfs_zap_caches(inode);
	NFS_CHANGE_ATTR(inode) = fattr->change_attr;
	if (fattr->bitmap[1] & FATTR4_WORD1_SPACE_USED)
		inode->i_blocks = (fattr->du.nfs3.used + 511) >> 9;
	if (fattr->bitmap[1] & FATTR4_WORD1_TIME_METADATA)
		inode->i_ctime = fattr->ctime;
	if (fattr->bitmap[1] & FATTR4_WORD1_TIME_MODIFY)
		inode->i_mtime = fattr->mtime;
}

static void
nfs4_write_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;
	struct inode *inode = data->inode;
	
	if (task->tk_status >= 0)
		renew_lease(NFS_SERVER(inode), data->timestamp);
	nfs4_write_refresh_inode(inode, data->res.fattr);
	/* Call back common NFS writeback processing */
	nfs_writeback_done(task);
}

static void
nfs4_proc_write_setup(struct nfs_write_data *data, unsigned int count, int how)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	struct nfs_page *req = nfs_list_entry(data->pages.next);
	int stable;
	int flags;
	
	if (how & FLUSH_STABLE) {
		if (!NFS_I(inode)->ncommit)
			stable = NFS_FILE_SYNC;
		else
			stable = NFS_DATA_SYNC;
	} else
		stable = NFS_UNSTABLE;

	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req);
	data->args.pgbase = req->wb_pgbase;
	data->args.count  = count;
	data->args.stable = stable;
	data->args.pages  = data->pagevec;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;
	data->timestamp   = jiffies;

	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_write_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_writedata_release;

	rpc_call_setup(task, &msg, 0);
}

static void
nfs4_commit_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;
	
	nfs4_write_refresh_inode(data->inode, data->res.fattr);
	/* Call back common NFS writeback processing */
	nfs_commit_done(task);
}

static void
nfs4_proc_commit_setup(struct nfs_write_data *data, u64 start, u32 len, int how)
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
	
	data->args.fh     = NFS_FH(data->inode);
	data->args.offset = start;
	data->args.count  = len;
	data->res.count   = len;
	data->res.fattr   = &data->fattr;
	data->res.verf    = &data->verf;
	
	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_commit_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_commit_release;
	
	rpc_call_setup(task, &msg, 0);	
}

/*
 * nfs4_proc_renew(): This is not one of the nfs_rpc_ops; it is a special
 * standalone procedure for queueing an asynchronous RENEW.
 */
struct renew_desc {
	struct rpc_task		task;
	struct nfs4_compound	compound;
	struct nfs4_op		ops[1];
};

static void
renew_done(struct rpc_task *task)
{
	struct nfs4_compound *cp = (struct nfs4_compound *) task->tk_msg.rpc_argp;
	process_lease(cp);
}

static void
renew_release(struct rpc_task *task)
{
	kfree(task->tk_calldata);
}

int
nfs4_proc_renew(struct nfs_server *server)
{
	struct renew_desc *rp;
	struct rpc_task *task;
	struct nfs4_compound *cp;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_COMPOUND],
	};

	rp = (struct renew_desc *) kmalloc(sizeof(*rp), GFP_KERNEL);
	if (!rp)
		return -ENOMEM;
	cp = &rp->compound;
	task = &rp->task;
	
	nfs4_setup_compound(cp, rp->ops, server, "renew");
	nfs4_setup_renew(cp);
	
	msg.rpc_argp = cp;
	msg.rpc_resp = cp;
	rpc_init_task(task, server->client, renew_done, RPC_TASK_ASYNC);
	rpc_call_setup(task, &msg, 0);
	task->tk_calldata = rp;
	task->tk_release = renew_release;
	
	return rpc_execute(task);
}

/*
 * We will need to arrange for the VFS layer to provide an atomic open.
 * Until then, this open method is prone to inefficiency and race conditions
 * due to the lookup, potential create, and open VFS calls from sys_open()
 * placed on the wire.
 */
int
nfs4_proc_file_open(struct inode *inode, struct file *filp)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_parent->d_inode;
	int flags, status = 0;

	dprintk("nfs4_proc_file_open: starting on (%.*s/%.*s)\n",
	                       (int)dentry->d_parent->d_name.len,
	                       dentry->d_parent->d_name.name,
	                       (int)dentry->d_name.len, dentry->d_name.name);

	lock_kernel();

	/* isn't this done in open_namei? */
	if (!S_ISREG(inode->i_mode)) {
		status = -EISDIR;
		goto out;
	}

	flags = filp->f_flags & O_ACCMODE;

/*
* Got race??
* We have already opened the file "O_EXCL" in nfs4_proc_create!!
* This ugliness will go away with lookup-intent...
*/
	while (!nfs4_get_inode_share(inode, flags)) {
		struct nfs4_shareowner *sp = NULL;
		status = nfs4_do_open(dir, &dentry->d_name, flags, NULL, NULL, NULL, &sp);
		if (status) {
			nfs4_put_shareowner(inode,sp);
			break;
		}
		if (nfs4_set_inode_share(inode, sp, flags))
			nfs4_put_shareowner(inode,sp);
	}
out:
	unlock_kernel();
	return status;
}


struct nfs_rpc_ops	nfs_v4_clientops = {
	.version	= 4,			/* protocol version */
	.getroot	= nfs4_proc_get_root,
	.getattr	= nfs4_proc_getattr,
	.setattr	= nfs4_proc_setattr,
	.lookup		= nfs4_proc_lookup,
	.access		= nfs4_proc_access,
	.readlink	= nfs4_proc_readlink,
	.read		= nfs4_proc_read,
	.write		= nfs4_proc_write,
	.commit		= NULL,
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
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
