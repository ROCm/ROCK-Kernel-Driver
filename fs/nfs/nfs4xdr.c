/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Client-side XDR for NFSv4.
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

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>

/* Emperically, it seems that the NFS client gets confused if
 * cookies larger than this are returned -- presumably a
 * signedness issue?
 */
#define COOKIE_MAX		0x7fffffff

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

extern int			nfs_stat_to_errno(int);

#define NFS4_enc_compound_sz	1024  /* XXX: large enough? */
#define NFS4_dec_compound_sz	1024  /* XXX: large enough? */

static struct {
	unsigned int	mode;
	unsigned int	nfs2type;
} nfs_type2fmt[] = {
	{ 0,		NFNON	     },
	{ S_IFREG,	NFREG	     },
	{ S_IFDIR,	NFDIR	     },
	{ S_IFBLK,	NFBLK	     },
	{ S_IFCHR,	NFCHR	     },
	{ S_IFLNK,	NFLNK	     },
	{ S_IFSOCK,	NFSOCK	     },
	{ S_IFIFO,	NFFIFO	     },
	{ 0,		NFNON	     },
	{ 0,		NFNON	     },
};

/*
 * START OF "GENERIC" ENCODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((uint32_t)((n) >> 32));				\
	*p++ = htonl((uint32_t)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	p = xdr_writemem(p, ptr, nbytes);			\
} while (0)

#define RESERVE_SPACE(nbytes)	do {				\
	p = xdr_reserve_space(xdr, nbytes);			\
	BUG_ON(!p);						\
} while (0)

static inline
uint32_t *xdr_writemem(uint32_t *p, const void *ptr, int nbytes)
{
	int tmp = XDR_QUADLEN(nbytes);
	if (!tmp)
		return p;
	p[tmp-1] = 0;
	memcpy(p, ptr, nbytes);
	return p + tmp;
}

/*
 * FIXME: The following dummy entries will be replaced once the userland
 * upcall gets in...
 */
static int
encode_uid(char *p, uid_t uid)
{
	strcpy(p, "nobody");
	return 6;
}

/*
 * FIXME: The following dummy entries will be replaced once the userland
 * upcall gets in...
 */
static int
encode_gid(char *p, gid_t gid)
{
	strcpy(p, "nobody");
	return 6;
}

static int
encode_attrs(struct xdr_stream *xdr, struct iattr *iap)
{
	char owner_name[256];
	char owner_group[256];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	uint32_t *p;
	uint32_t *q;
	int len;
	uint32_t bmval0 = 0;
	uint32_t bmval1 = 0;
	int status;

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 * In the worst-case, this would be
	 *   12(bitmap) + 4(attrlen) + 8(size) + 4(mode) + 4(atime) + 4(mtime)
	 *          = 36 bytes, plus any contribution from variable-length fields
	 *            such as owner/group/acl's.
	 */
	len = 36;

	/* Sigh */
	if (iap->ia_valid & ATTR_UID) {
		status = owner_namelen = encode_uid(owner_name, iap->ia_uid);
		if (status < 0) {
			printk(KERN_WARNING "nfs: couldn't resolve uid %d to string\n",
			       iap->ia_uid);
			goto out;
		}
		len += XDR_QUADLEN(owner_namelen);
	}
	if (iap->ia_valid & ATTR_GID) {
		status = owner_grouplen = encode_gid(owner_group, iap->ia_gid);
		if (status < 0) {
			printk(KERN_WARNING "nfs4: couldn't resolve gid %d to string\n",
			       iap->ia_gid);
			goto out;
		}
		len += XDR_QUADLEN(owner_grouplen);
	}
	RESERVE_SPACE(len);

	/*
	 * We write the bitmap length now, but leave the bitmap and the attribute
	 * buffer length to be backfilled at the end of this routine.
	 */
	WRITE32(2);
	q = p;
	p += 3;

	if (iap->ia_valid & ATTR_SIZE) {
		bmval0 |= FATTR4_WORD0_SIZE;
		WRITE64(iap->ia_size);
	}
	if (iap->ia_valid & ATTR_MODE) {
		bmval1 |= FATTR4_WORD1_MODE;
		WRITE32(iap->ia_mode);
	}
	if (iap->ia_valid & ATTR_UID) {
		bmval1 |= FATTR4_WORD1_OWNER;
		WRITE32(owner_namelen);
		WRITEMEM(owner_name, owner_namelen);
		p += owner_namelen;
	}
	if (iap->ia_valid & ATTR_GID) {
		bmval1 |= FATTR4_WORD1_OWNER_GROUP;
		WRITE32(owner_grouplen);
		WRITEMEM(owner_group, owner_grouplen);
		p += owner_namelen;
	}
	if (iap->ia_valid & ATTR_ATIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_ATIME) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	if (iap->ia_valid & ATTR_MTIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_MTIME) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	
	/*
	 * Now we backfill the bitmap and the attribute buffer length.
	 */
	len = (char *)p - (char *)q - 12;
	*q++ = htonl(bmval0);
	*q++ = htonl(bmval1);
	*q++ = htonl(len);

	status = 0;
out:
	return status;
}

static int
encode_access(struct xdr_stream *xdr, struct nfs4_access *access)
{
	uint32_t *p;

	RESERVE_SPACE(8);
	WRITE32(OP_ACCESS);
	WRITE32(access->ac_req_access);
	
	return 0;
}

static int
encode_close(struct xdr_stream *xdr, struct nfs4_close *close)
{
	uint32_t *p;

	RESERVE_SPACE(20);
	WRITE32(OP_CLOSE);
	WRITE32(close->cl_seqid);
	WRITEMEM(close->cl_stateid, sizeof(nfs4_stateid));
	
	return 0;
}

static int
encode_commit(struct xdr_stream *xdr, struct nfs4_commit *commit)
{
	uint32_t *p;
        
        RESERVE_SPACE(16);
        WRITE32(OP_COMMIT);
        WRITE64(commit->co_start);
        WRITE32(commit->co_len);

        return 0;
}

static int
encode_create(struct xdr_stream *xdr, struct nfs4_create *create)
{
	uint32_t *p;
	
	RESERVE_SPACE(8);
	WRITE32(OP_CREATE);
	WRITE32(create->cr_ftype);

	switch (create->cr_ftype) {
	case NF4LNK:
		RESERVE_SPACE(4 + create->cr_textlen);
		WRITE32(create->cr_textlen);
		WRITEMEM(create->cr_text, create->cr_textlen);
		break;

	case NF4BLK: case NF4CHR:
		RESERVE_SPACE(8);
		WRITE32(create->cr_specdata1);
		WRITE32(create->cr_specdata2);
		break;

	default:
		break;
	}

	RESERVE_SPACE(4 + create->cr_namelen);
	WRITE32(create->cr_namelen);
	WRITEMEM(create->cr_name, create->cr_namelen);

	return encode_attrs(xdr, create->cr_attrs);
}

static int
encode_getattr(struct xdr_stream *xdr, struct nfs4_getattr *getattr)
{
        uint32_t *p;

        RESERVE_SPACE(16);
        WRITE32(OP_GETATTR);
        WRITE32(2);
        WRITE32(getattr->gt_bmval[0]);
        WRITE32(getattr->gt_bmval[1]);

        return 0;
}

static int
encode_getfh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_GETFH);

	return 0;
}

static int
encode_link(struct xdr_stream *xdr, struct nfs4_link *link)
{
	uint32_t *p;

	RESERVE_SPACE(8 + link->ln_namelen);
	WRITE32(OP_LINK);
	WRITE32(link->ln_namelen);
	WRITEMEM(link->ln_name, link->ln_namelen);
	
	return 0;
}

static int
encode_lookup(struct xdr_stream *xdr, struct nfs4_lookup *lookup)
{
	int len = lookup->lo_name->len;
	uint32_t *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_LOOKUP);
	WRITE32(len);
	WRITEMEM(lookup->lo_name->name, len);

	return 0;
}

static int
encode_open(struct xdr_stream *xdr, struct nfs4_open *open)
{
	static int global_id = 0;
	int id = global_id++;
	int status;
	uint32_t *p;
	
	/* seqid, share_access, share_deny, clientid, ownerlen, owner, opentype */
	RESERVE_SPACE(52);
	WRITE32(OP_OPEN);
	WRITE32(0);                       /* seqid */
	WRITE32(open->op_share_access);
	WRITE32(0);                       /* for us, share_deny== 0 always */
	WRITE64(open->op_client_state->cl_clientid);
	WRITE32(4);
	WRITE32(id);
	WRITE32(open->op_opentype);
	
	if (open->op_opentype == NFS4_OPEN_CREATE) {
		if (open->op_createmode == NFS4_CREATE_EXCLUSIVE) {
			RESERVE_SPACE(12);
			WRITE32(open->op_createmode);
			WRITEMEM(open->op_verifier, sizeof(nfs4_verifier));
		}
		else if (open->op_attrs) {
			RESERVE_SPACE(4);
			WRITE32(open->op_createmode);
			if ((status = encode_attrs(xdr, open->op_attrs)))
				return status;
		}
		else {
			RESERVE_SPACE(12);
			WRITE32(open->op_createmode);
			WRITE32(0);
			WRITE32(0);
		}
	}

	RESERVE_SPACE(8 + open->op_name->len);
	WRITE32(NFS4_OPEN_CLAIM_NULL);
	WRITE32(open->op_name->len);
	WRITEMEM(open->op_name->name, open->op_name->len);
	
	return 0;
}

static int
encode_open_confirm(struct xdr_stream *xdr, struct nfs4_open_confirm *open_confirm)
{
	uint32_t *p;

	/*
	 * Note: In this "stateless" implementation, the OPEN_CONFIRM
	 * seqid is always equal to 1.
	 */
	RESERVE_SPACE(24);
	WRITE32(OP_OPEN_CONFIRM);
	WRITEMEM(open_confirm->oc_stateid, sizeof(nfs4_stateid));
	WRITE32(1);
	
	return 0;
}

static int
encode_putfh(struct xdr_stream *xdr, struct nfs4_putfh *putfh)
{
	int len = putfh->pf_fhandle->size;
	uint32_t *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_PUTFH);
	WRITE32(len);
	WRITEMEM(putfh->pf_fhandle->data, len);

	return 0;
}

static int
encode_putrootfh(struct xdr_stream *xdr)
{
        uint32_t *p;
        
        RESERVE_SPACE(4);
        WRITE32(OP_PUTROOTFH);

        return 0;
}

static int
encode_read(struct xdr_stream *xdr, struct nfs4_read *read, struct rpc_rqst *req)
{
	struct rpc_auth	*auth = req->rq_task->tk_auth;
	int		replen;
	uint32_t *p;

	RESERVE_SPACE(32);
	WRITE32(OP_READ);
	WRITE32(0);   /* all-zero stateid! */
	WRITE32(0);
	WRITE32(0);
	WRITE32(0);
	WRITE64(read->rd_offset);
	WRITE32(read->rd_length);

	/* set up reply iovec
	 *    toplevel status + taglen + rescount + OP_PUTFH + status
	 *       + OP_READ + status + eof + datalen = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 9) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
			 read->rd_pages, read->rd_pgbase, read->rd_length);

	return 0;
}

static int
encode_readdir(struct xdr_stream *xdr, struct nfs4_readdir *readdir, struct rpc_rqst *req)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	int replen;
	uint32_t *p;

	RESERVE_SPACE(40);
	WRITE32(OP_READDIR);
	WRITE64(readdir->rd_cookie);
	WRITEMEM(readdir->rd_req_verifier, sizeof(nfs4_verifier));
	WRITE32(readdir->rd_count >> 5);  /* meaningless "dircount" field */
	WRITE32(readdir->rd_count);
	WRITE32(2);
	WRITE32(readdir->rd_bmval[0]);
	WRITE32(readdir->rd_bmval[1]);

	/* set up reply iovec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READDIR + status + verifer(2)  = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 9) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, readdir->rd_pages,
			 readdir->rd_pgbase, readdir->rd_count);

	return 0;
}

static int
encode_readlink(struct xdr_stream *xdr, struct nfs4_readlink *readlink, struct rpc_rqst *req)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	int replen;
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_READLINK);

	/* set up reply iovec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READLINK + status  = 7
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 7) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, readlink->rl_pages, 0, readlink->rl_count);
	
	return 0;
}

static int
encode_remove(struct xdr_stream *xdr, struct nfs4_remove *remove)
{
	uint32_t *p;

	RESERVE_SPACE(8 + remove->rm_namelen);
	WRITE32(OP_REMOVE);
	WRITE32(remove->rm_namelen);
	WRITEMEM(remove->rm_name, remove->rm_namelen);

	return 0;
}

static int
encode_rename(struct xdr_stream *xdr, struct nfs4_rename *rename)
{
	uint32_t *p;

	RESERVE_SPACE(8 + rename->rn_oldnamelen);
	WRITE32(OP_RENAME);
	WRITE32(rename->rn_oldnamelen);
	WRITEMEM(rename->rn_oldname, rename->rn_oldnamelen);
	
	RESERVE_SPACE(8 + rename->rn_newnamelen);
	WRITE32(rename->rn_newnamelen);
	WRITEMEM(rename->rn_newname, rename->rn_newnamelen);

	return 0;
}

static int
encode_renew(struct xdr_stream *xdr, struct nfs4_client *client_stateid)
{
	uint32_t *p;

	RESERVE_SPACE(12);
	WRITE32(OP_RENEW);
	WRITE64(client_stateid->cl_clientid);

	return 0;
}

static int
encode_restorefh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_RESTOREFH);

	return 0;
}

static int
encode_savefh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_SAVEFH);

	return 0;
}

static int
encode_setattr(struct xdr_stream *xdr, struct nfs4_setattr *setattr)
{
	int status;
	uint32_t *p;
	
        RESERVE_SPACE(20);
        WRITE32(OP_SETATTR);
	WRITEMEM(setattr->st_stateid, sizeof(nfs4_stateid));

        if ((status = encode_attrs(xdr, setattr->st_iap)))
		return status;

        return 0;
}

static int
encode_setclientid(struct xdr_stream *xdr, struct nfs4_setclientid *setclientid)
{
	uint32_t total_len;
	uint32_t len1, len2, len3;
	uint32_t *p;

	len1 = strlen(setclientid->sc_name);
	len2 = strlen(setclientid->sc_netid);
	len3 = strlen(setclientid->sc_uaddr);
	total_len = XDR_QUADLEN(len1) + XDR_QUADLEN(len2) + XDR_QUADLEN(len3);
	total_len = (total_len << 2) + 32;

	RESERVE_SPACE(total_len);
	WRITE32(OP_SETCLIENTID);
	WRITEMEM(setclientid->sc_verifier, sizeof(nfs4_verifier));
	WRITE32(len1);
	WRITEMEM(setclientid->sc_name, len1);
	WRITE32(setclientid->sc_prog);
	WRITE32(len2);
	WRITEMEM(setclientid->sc_netid, len2);
	WRITE32(len3);
	WRITEMEM(setclientid->sc_uaddr, len3);
	WRITE32(setclientid->sc_cb_ident);

	return 0;
}

static int
encode_setclientid_confirm(struct xdr_stream *xdr, struct nfs4_client *client_state)
{
        uint32_t *p;

        RESERVE_SPACE(12 + sizeof(nfs4_verifier));
        WRITE32(OP_SETCLIENTID_CONFIRM);
        WRITE64(client_state->cl_clientid);
        WRITEMEM(client_state->cl_confirm,sizeof(nfs4_verifier));

        return 0;
}

static int
encode_write(struct xdr_stream *xdr, struct nfs4_write *write, struct rpc_rqst *req)
{
	struct xdr_buf *sndbuf = &req->rq_snd_buf;
	uint32_t *p;

	RESERVE_SPACE(36);
	WRITE32(OP_WRITE);
	WRITE32(0xffffffff);     /* magic stateid -1 */
	WRITE32(0xffffffff);
	WRITE32(0xffffffff);
	WRITE32(0xffffffff);
	WRITE64(write->wr_offset);
	WRITE32(write->wr_stable_how);
	WRITE32(write->wr_len);

	xdr_encode_pages(sndbuf, write->wr_pages, write->wr_pgbase, write->wr_len);

	return 0;
}

/* FIXME: this sucks */
static int
encode_compound(struct xdr_stream *xdr, struct nfs4_compound *cp, struct rpc_rqst *req)
{
	int i, status = 0;
	uint32_t *p;

	dprintk("encode_compound: tag=%.*s\n", (int)cp->taglen, cp->tag);
	
	RESERVE_SPACE(12 + cp->taglen);
	WRITE32(cp->taglen);
	WRITEMEM(cp->tag, cp->taglen);
	WRITE32(NFS4_MINOR_VERSION);
	WRITE32(cp->req_nops);

	for (i = 0; i < cp->req_nops; i++) {
		switch (cp->ops[i].opnum) {
		case OP_ACCESS:
			status = encode_access(xdr, &cp->ops[i].u.access);
			break;
		case OP_CLOSE:
			status = encode_close(xdr, &cp->ops[i].u.close);
			break;
		case OP_COMMIT:
			status = encode_commit(xdr, &cp->ops[i].u.commit);
			break;
		case OP_CREATE:
			status = encode_create(xdr, &cp->ops[i].u.create);
			break;
		case OP_GETATTR:
			status = encode_getattr(xdr, &cp->ops[i].u.getattr);
			break;
		case OP_GETFH:
			status = encode_getfh(xdr);
			break;
		case OP_LINK:
			status = encode_link(xdr, &cp->ops[i].u.link);
			break;
		case OP_LOOKUP:
			status = encode_lookup(xdr, &cp->ops[i].u.lookup);
			break;
		case OP_OPEN:
			status = encode_open(xdr, &cp->ops[i].u.open);
			break;
		case OP_OPEN_CONFIRM:
			status = encode_open_confirm(xdr, &cp->ops[i].u.open_confirm);
			break;
		case OP_PUTFH:
			status = encode_putfh(xdr, &cp->ops[i].u.putfh);
			break;
		case OP_PUTROOTFH:
			status = encode_putrootfh(xdr);
			break;
		case OP_READ:
			status = encode_read(xdr, &cp->ops[i].u.read, req);
			break;
		case OP_READDIR:
			status = encode_readdir(xdr, &cp->ops[i].u.readdir, req);
			break;
		case OP_READLINK:
			status = encode_readlink(xdr, &cp->ops[i].u.readlink, req);
			break;
		case OP_REMOVE:
			status = encode_remove(xdr, &cp->ops[i].u.remove);
			break;
		case OP_RENAME:
			status = encode_rename(xdr, &cp->ops[i].u.rename);
			break;
		case OP_RENEW:
			status = encode_renew(xdr, cp->ops[i].u.renew);
			break;
		case OP_RESTOREFH:
			status = encode_restorefh(xdr);
			break;
		case OP_SAVEFH:
			status = encode_savefh(xdr);
			break;
		case OP_SETATTR:
			status = encode_setattr(xdr, &cp->ops[i].u.setattr);
			break;
		case OP_SETCLIENTID:
			status = encode_setclientid(xdr, &cp->ops[i].u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			status = encode_setclientid_confirm(xdr, cp->ops[i].u.setclientid_confirm);
			break;
		case OP_WRITE:
			status = encode_write(xdr, &cp->ops[i].u.write, req);
			break;
		default:
			BUG();
		}
		if (status)
			return status;
	}
	
	return 0;
}
/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */


/*
 * Encode COMPOUND argument
 */
static int
nfs4_xdr_enc_compound(struct rpc_rqst *req, uint32_t *p, struct nfs4_compound *cp)
{
	struct xdr_stream xdr;
	int status;
	
	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	status = encode_compound(&xdr, cp, req);
	cp->timestamp = jiffies;
	return status;
}


/*
 * START OF "GENERIC" DECODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define DECODE_TAIL				\
	status = 0;				\
out:						\
	return status;				\
xdr_error:					\
	printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); \
	status = -EIO;				\
	goto out

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define READTIME(x)       do {			\
	p++;					\
	(x.tv_sec) = ntohl(*p++);		\
	(x.tv_nsec) = ntohl(*p++);		\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

#define READ_BUF(nbytes)  do { \
	p = xdr_inline_decode(xdr, nbytes); \
	if (!p) { \
		printk(KERN_WARNING "%s: reply buffer overflowed in line %d.", \
				__FUNCTION__, __LINE__); \
			return -EIO; \
	} \
} while (0)

/*
 * FIXME: The following dummy entry will be replaced once the userland
 * upcall gets in...
 */
static int
decode_uid(char *p, uint32_t len, uid_t *uid)
{
	*uid = -2;
	return 0;
}

/*
 * FIXME: The following dummy entry will be replaced once the userland
 * upcall gets in...
 */
static int
decode_gid(char *p, uint32_t len, gid_t *gid)
{
	*gid = -2;
	return 0;
}

static int
decode_change_info(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	uint32_t *p;

	READ_BUF(20);
	READ32(cinfo->atomic);
	READ64(cinfo->before);
	READ64(cinfo->after);
	return 0;
}

static int
decode_access(struct xdr_stream *xdr, int nfserr, struct nfs4_access *access)
{
	uint32_t *p;
	uint32_t supp, acc;

	if (!nfserr) {
		READ_BUF(8);
		READ32(supp);
		READ32(acc);

		if ((supp & ~access->ac_req_access) || (acc & ~supp)) {
			printk(KERN_NOTICE "NFS: server returned bad bits in access call!\n");
			return -EIO;
		}
		*access->ac_resp_supported = supp;
		*access->ac_resp_access = acc;
	}
	return 0;
}

static int
decode_close(struct xdr_stream *xdr, int nfserr, struct nfs4_close *close)
{
	uint32_t *p;

	if (!nfserr) {
		READ_BUF(sizeof(nfs4_stateid));
		COPYMEM(close->cl_stateid, sizeof(nfs4_stateid));
	}
	return 0;
}

static int
decode_commit(struct xdr_stream *xdr, int nfserr, struct nfs4_commit *commit)
{
	uint32_t *p;

        if (!nfserr) {
                READ_BUF(8);
                COPYMEM(commit->co_verifier->verifier, 8);
        }
	return 0;
}

static int
decode_create(struct xdr_stream *xdr, int nfserr, struct nfs4_create *create)
{
	uint32_t *p;
	uint32_t bmlen;
	int status;

	if (!nfserr) {
		if ((status = decode_change_info(xdr, create->cr_cinfo)))
			goto out;
		READ_BUF(4);
		READ32(bmlen);
		if (bmlen > 2)
			goto xdr_error;
		READ_BUF(bmlen << 2);
	}

	DECODE_TAIL;
}

extern uint32_t nfs4_fattr_bitmap[2];
extern uint32_t nfs4_fsinfo_bitmap[2];
extern uint32_t nfs4_fsstat_bitmap[2];
extern uint32_t nfs4_pathconf_bitmap[2];

static int
decode_getattr(struct xdr_stream *xdr, int nfserr, struct nfs4_getattr *getattr)
{
        struct nfs_fattr *nfp = getattr->gt_attrs;
	struct nfs_fsstat *fsstat = getattr->gt_fsstat;
	struct nfs_fsinfo *fsinfo = getattr->gt_fsinfo;
	struct nfs_pathconf *pathconf = getattr->gt_pathconf;
	uint32_t *p;
        uint32_t bmlen;
        uint32_t bmval0 = 0;
        uint32_t bmval1 = 0;
        uint32_t attrlen;
        uint32_t dummy32;
        uint32_t len = 0;
	unsigned int type;
	int fmode = 0;
	int status;
	
        if (nfserr)
                goto success;
        
        READ_BUF(4);
        READ32(bmlen);
        if (bmlen > 2)
                goto xdr_error;
	
        READ_BUF((bmlen << 2) + 4);
        if (bmlen > 0)
                READ32(bmval0);
        if (bmlen > 1)
                READ32(bmval1);
        READ32(attrlen);

	if ((bmval0 & ~getattr->gt_bmval[0]) ||
	    (bmval1 & ~getattr->gt_bmval[1])) {
		dprintk("read_attrs: server returned bad attributes!\n");
		goto xdr_error;
	}
	getattr->gt_bmres[0] = bmval0;
	getattr->gt_bmres[1] = bmval1;

	/*
	 * In case the server doesn't return some attributes,
	 * we initialize them here to some nominal values..
	 */
	if (nfp) {
		nfp->valid = NFS_ATTR_FATTR | NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4;
		nfp->nlink = 1;
		nfp->timestamp = jiffies;
	}
	if (fsinfo) {
		fsinfo->rtmult = fsinfo->wtmult = 512;  /* ??? */
		fsinfo->lease_time = 60;
	}

        if (bmval0 & FATTR4_WORD0_TYPE) {
                READ_BUF(4);
                len += 4;
                READ32(type);
                if (type < NF4REG || type > NF4NAMEDATTR) {
                        dprintk("read_attrs: bad type %d\n", type);
                        goto xdr_error;
                }
		nfp->type = nfs_type2fmt[type].nfs2type;
		fmode = nfs_type2fmt[type].mode;
                dprintk("read_attrs: type=%d\n", (uint32_t)nfp->type);
        }
        if (bmval0 & FATTR4_WORD0_CHANGE) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->change_attr);
                dprintk("read_attrs: changeid=%Ld\n", (long long)nfp->change_attr);
        }
        if (bmval0 & FATTR4_WORD0_SIZE) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->size);
                dprintk("read_attrs: size=%Ld\n", (long long)nfp->size);
        }
        if (bmval0 & FATTR4_WORD0_FSID) {
                READ_BUF(16);
                len += 16;
                READ64(nfp->fsid_u.nfs4.major);
                READ64(nfp->fsid_u.nfs4.minor);
                dprintk("read_attrs: fsid=0x%Lx/0x%Lx\n",
			(long long)nfp->fsid_u.nfs4.major,
			(long long)nfp->fsid_u.nfs4.minor);
        }
        if (bmval0 & FATTR4_WORD0_LEASE_TIME) {
                READ_BUF(4);
                len += 4;
                READ32(fsinfo->lease_time);
                dprintk("read_attrs: lease_time=%d\n", fsinfo->lease_time);
        }
        if (bmval0 & FATTR4_WORD0_FILEID) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->fileid);
                dprintk("read_attrs: fileid=%Ld\n", (long long) nfp->fileid);
        }
	if (bmval0 & FATTR4_WORD0_FILES_AVAIL) {
		READ_BUF(8);
		len += 8;
		READ64(fsstat->afiles);
		dprintk("read_attrs: files_avail=0x%Lx\n", (long long) fsstat->afiles);
	}
        if (bmval0 & FATTR4_WORD0_FILES_FREE) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->ffiles);
                dprintk("read_attrs: files_free=0x%Lx\n", (long long) fsstat->ffiles);
        }
        if (bmval0 & FATTR4_WORD0_FILES_TOTAL) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->tfiles);
                dprintk("read_attrs: files_tot=0x%Lx\n", (long long) fsstat->tfiles);
        }
        if (bmval0 & FATTR4_WORD0_MAXFILESIZE) {
                READ_BUF(8);
                len += 8;
                READ64(fsinfo->maxfilesize);
                dprintk("read_attrs: maxfilesize=0x%Lx\n", (long long) fsinfo->maxfilesize);
        }
	if (bmval0 & FATTR4_WORD0_MAXLINK) {
		READ_BUF(4);
		len += 4;
		READ32(pathconf->max_link);
		dprintk("read_attrs: maxlink=%d\n", pathconf->max_link);
	}
        if (bmval0 & FATTR4_WORD0_MAXNAME) {
                READ_BUF(4);
                len += 4;
                READ32(pathconf->max_namelen);
                dprintk("read_attrs: maxname=%d\n", pathconf->max_namelen);
        }
        if (bmval0 & FATTR4_WORD0_MAXREAD) {
                READ_BUF(8);
                len += 8;
                READ64(fsinfo->rtmax);
		fsinfo->rtpref = fsinfo->dtpref = fsinfo->rtmax;
                dprintk("read_attrs: maxread=%d\n", fsinfo->rtmax);
        }
        if (bmval0 & FATTR4_WORD0_MAXWRITE) {
                READ_BUF(8);
                len += 8;
                READ64(fsinfo->wtmax);
		fsinfo->wtpref = fsinfo->wtmax;
                dprintk("read_attrs: maxwrite=%d\n", fsinfo->wtmax);
        }
	
        if (bmval1 & FATTR4_WORD1_MODE) {
                READ_BUF(4);
                len += 4;
                READ32(dummy32);
		nfp->mode = (dummy32 & ~S_IFMT) | fmode;
                dprintk("read_attrs: mode=0%o\n", nfp->mode);
        }
        if (bmval1 & FATTR4_WORD1_NUMLINKS) {
                READ_BUF(4);
                len += 4;
                READ32(nfp->nlink);
                dprintk("read_attrs: nlinks=0%o\n", nfp->nlink);
        }
        if (bmval1 & FATTR4_WORD1_OWNER) {
                READ_BUF(4);
                len += 4;
                READ32(dummy32);    /* name length */
                if (dummy32 > XDR_MAX_NETOBJ) {
			dprintk("read_attrs: name too long!\n");
                        goto xdr_error;
                }
                READ_BUF(dummy32);
                len += (XDR_QUADLEN(dummy32) << 2);
                if ((status = decode_uid((char *)p, dummy32, &nfp->uid))) {
                        dprintk("read_attrs: gss_get_num failed!\n");
                        goto out;
                }
                dprintk("read_attrs: uid=%d\n", (int)nfp->uid);
        }
        if (bmval1 & FATTR4_WORD1_OWNER_GROUP) {
                READ_BUF(4);
                len += 4;
                READ32(dummy32);
                if (dummy32 > XDR_MAX_NETOBJ) {
                        dprintk("read_attrs: name too long!\n");
                        goto xdr_error;
                }
                READ_BUF(dummy32);
                len += (XDR_QUADLEN(dummy32) << 2);
                if ((status = decode_gid((char *)p, dummy32, &nfp->gid))) {
                        dprintk("read_attrs: gss_get_num failed!\n");
                        goto out;
                }
                dprintk("read_attrs: gid=%d\n", (int)nfp->gid);
        }
        if (bmval1 & FATTR4_WORD1_RAWDEV) {
                READ_BUF(8);
                len += 8;
                READ32(dummy32);
		nfp->rdev = (dummy32 << MINORBITS);
                READ32(dummy32);
		nfp->rdev |= (dummy32 & MINORMASK);
                dprintk("read_attrs: rdev=%d\n", nfp->rdev);
        }
        if (bmval1 & FATTR4_WORD1_SPACE_AVAIL) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->abytes);
                dprintk("read_attrs: savail=0x%Lx\n", (long long) fsstat->abytes);
        }
	if (bmval1 & FATTR4_WORD1_SPACE_FREE) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->fbytes);
                dprintk("read_attrs: sfree=0x%Lx\n", (long long) fsstat->fbytes);
        }
        if (bmval1 & FATTR4_WORD1_SPACE_TOTAL) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->tbytes);
                dprintk("read_attrs: stotal=0x%Lx\n", (long long) fsstat->tbytes);
        }
        if (bmval1 & FATTR4_WORD1_SPACE_USED) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->du.nfs3.used);
                dprintk("read_attrs: sused=0x%Lx\n", (long long) nfp->du.nfs3.used);
        }
        if (bmval1 & FATTR4_WORD1_TIME_ACCESS) {
                READ_BUF(12);
                len += 12;
                READTIME(nfp->atime);
                dprintk("read_attrs: atime=%ld\n", (long)nfp->atime.tv_sec);
        }
        if (bmval1 & FATTR4_WORD1_TIME_METADATA) {
                READ_BUF(12);
                len += 12;
                READTIME(nfp->ctime);
                dprintk("read_attrs: ctime=%ld\n", (long)nfp->ctime.tv_sec);
        }
        if (bmval1 & FATTR4_WORD1_TIME_MODIFY) {
                READ_BUF(12);
                len += 12;
                READTIME(nfp->mtime);
                dprintk("read_attrs: mtime=%ld\n", (long)nfp->mtime.tv_sec);
        }
        if (len != attrlen)
                goto xdr_error;
	
success:
        DECODE_TAIL;
}

static int
decode_getfh(struct xdr_stream *xdr, int nfserr, struct nfs4_getfh *getfh)
{
	struct nfs_fh *fh = getfh->gf_fhandle;
	uint32_t *p;
	uint32_t len;
	int status;

	/* Zero handle first to allow comparisons */
	memset(fh, 0, sizeof(*fh));
		
        if (!nfserr) {
                READ_BUF(4);
		READ32(len);
		if (len > NFS_MAXFHSIZE)
			goto xdr_error;
		fh->size = len;
                READ_BUF(len);
                COPYMEM(fh->data, len);
        }

        DECODE_TAIL;
}

static int
decode_link(struct xdr_stream *xdr, int nfserr, struct nfs4_link *link)
{
	int status = 0;
	
	if (!nfserr)
		status = decode_change_info(xdr, link->ln_cinfo);
	return status;
}

static int
decode_open(struct xdr_stream *xdr, int nfserr, struct nfs4_open *open)
{
	uint32_t *p;
	uint32_t bmlen, delegation_type;
	int status;
	
	if (!nfserr) {
		READ_BUF(sizeof(nfs4_stateid));
		COPYMEM(open->op_stateid, sizeof(nfs4_stateid));

		decode_change_info(xdr, open->op_cinfo);

		READ_BUF(8);
		READ32(*open->op_rflags);
		READ32(bmlen);
		if (bmlen > 10)
			goto xdr_error;
		
		READ_BUF((bmlen << 2) + 4);
		p += bmlen;
		READ32(delegation_type);
		if (delegation_type != NFS4_OPEN_DELEGATE_NONE)
			goto xdr_error;
	}
	
	DECODE_TAIL;
}

static int
decode_open_confirm(struct xdr_stream *xdr, int nfserr, struct nfs4_open_confirm *open_confirm)
{
	uint32_t *p;

	if (!nfserr) {
		READ_BUF(sizeof(nfs4_stateid));
		COPYMEM(open_confirm->oc_stateid, sizeof(nfs4_stateid));
	}
	return 0;
}

static int
decode_read(struct xdr_stream *xdr, int nfserr, struct nfs4_read *read)
{
	uint32_t throwaway;
	uint32_t *p;
	int status;

	if (!nfserr) {
		READ_BUF(8);
		if (read->rd_eof)
			READ32(*read->rd_eof);
		else
			READ32(throwaway);
		READ32(*read->rd_bytes_read);
		if (*read->rd_bytes_read > read->rd_length)
			goto xdr_error;
	}

	DECODE_TAIL;
}

static int
decode_readdir(struct xdr_stream *xdr, int nfserr, struct rpc_rqst *req, struct nfs4_readdir *readdir)
{
	struct xdr_buf	*rcvbuf = &req->rq_rcv_buf;
	struct page	*page = *rcvbuf->pages;
	unsigned int	pglen = rcvbuf->page_len;
	uint32_t *end, *entry, *p;
	uint32_t len, attrlen, word;
	int i;

	if (!nfserr) {
		READ_BUF(8);
		COPYMEM(readdir->rd_resp_verifier, 8);

		BUG_ON(pglen > PAGE_CACHE_SIZE);
		p   = (uint32_t *) kmap(page);
		end = (uint32_t *) ((char *)p + pglen + readdir->rd_pgbase);

		while (*p++) {
			entry = p - 1;
			if (p + 3 > end)
				goto short_pkt;
			p += 2;     /* cookie */
			len = ntohl(*p++);  /* filename length */
			if (len > NFS4_MAXNAMLEN) {
				printk(KERN_WARNING "NFS: giant filename in readdir (len 0x%x)\n", len);
				goto err_unmap;
			}
			
			p += XDR_QUADLEN(len);
			if (p + 1 > end)
				goto short_pkt;
			len = ntohl(*p++);  /* bitmap length */
			if (len > 10) {
				printk(KERN_WARNING "NFS: giant bitmap in readdir (len 0x%x)\n", len);
				goto err_unmap;
			}
			if (p + len + 1 > end)
				goto short_pkt;
			attrlen = 0;
			for (i = 0; i < len; i++) {
				word = ntohl(*p++);
				if (!word)
					continue;
				else if (i == 0 && word == FATTR4_WORD0_FILEID) {
					attrlen = 8;
					continue;
				}
				printk(KERN_WARNING "NFS: unexpected bitmap word in readdir (0x%x)\n", word);
				goto err_unmap;
			}
			if (ntohl(*p++) != attrlen) {
				printk(KERN_WARNING "NFS: unexpected attrlen in readdir\n");
				goto err_unmap;
			}
			p += XDR_QUADLEN(attrlen);
			if (p + 1 > end)
				goto short_pkt;
		}
		kunmap(page);
	}
	
	return 0;
short_pkt:
	printk(KERN_NOTICE "NFS: short packet in readdir reply!\n");
	/* truncate listing */
	kunmap(page);
	entry[0] = entry[1] = 0;
	return 0;
err_unmap:
	kunmap(page);
	return -errno_NFSERR_IO;
}

static int
decode_readlink(struct xdr_stream *xdr, int nfserr, struct rpc_rqst *req, struct nfs4_readlink *readlink)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	uint32_t *strlen;
	uint32_t len;
	char *string;

	if (!nfserr) {
		/*
		 * The XDR encode routine has set things up so that
		 * the link text will be copied directly into the
		 * buffer.  We just have to do overflow-checking,
		 * and and null-terminate the text (the VFS expects
		 * null-termination).
		 */
		strlen = (uint32_t *) kmap(rcvbuf->pages[0]);
		len = ntohl(*strlen);
		if (len > PAGE_CACHE_SIZE - 5) {
			printk(KERN_WARNING "nfs: server returned giant symlink!\n");
			kunmap(rcvbuf->pages[0]);
			return -EIO;
		}
		*strlen = len;
		
		string = (char *)(strlen + 1);
		string[len] = '\0';
		kunmap(rcvbuf->pages[0]);
	}
	return 0;
}

static int
decode_remove(struct xdr_stream *xdr, int nfserr, struct nfs4_remove *remove)
{
	int status;

	status = 0;
	if (!nfserr) 
		status = decode_change_info(xdr, remove->rm_cinfo);
	return status;
}

static int
decode_rename(struct xdr_stream *xdr, int nfserr, struct nfs4_rename *rename)
{
	int status = 0;

	if (!nfserr) {
		if ((status = decode_change_info(xdr, rename->rn_src_cinfo)))
			goto out;
		if ((status = decode_change_info(xdr, rename->rn_dst_cinfo)))
			goto out;
	}
out:
	return status;
}

static int
decode_setattr(struct xdr_stream *xdr)
{
	uint32_t *p;
        uint32_t bmlen;
	int status;
        
        READ_BUF(4);
        READ32(bmlen);
        if (bmlen > 10)
                goto xdr_error;
        READ_BUF(bmlen << 2);

        DECODE_TAIL;
}

static int
decode_setclientid(struct xdr_stream *xdr, int nfserr, struct nfs4_setclientid *setclientid)
{
	uint32_t *p;

	if (!nfserr) {
		READ_BUF(8 + sizeof(nfs4_verifier));
		READ64(setclientid->sc_state->cl_clientid);
		COPYMEM(setclientid->sc_state->cl_confirm, sizeof(nfs4_verifier));
	}
	else if (nfserr == NFSERR_CLID_INUSE) {
		uint32_t len;

		/* skip netid string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);

		/* skip uaddr string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
	}

	return 0;
}

static int
decode_write(struct xdr_stream *xdr, int nfserr, struct nfs4_write *write)
{
	uint32_t *p;
	int status;

	if (!nfserr) {
		READ_BUF(16);
		READ32(*write->wr_bytes_written);
		if (*write->wr_bytes_written > write->wr_len)
			goto xdr_error;
		READ32(write->wr_verf->committed);
		COPYMEM(write->wr_verf->verifier, 8);
	}

	DECODE_TAIL;
}

/* FIXME: this sucks */
static int
decode_compound(struct xdr_stream *xdr, struct nfs4_compound *cp, struct rpc_rqst *req)
{
	uint32_t *p;
	uint32_t taglen;
	uint32_t opnum, nfserr;
	int status;

	READ_BUF(8);
	READ32(cp->toplevel_status);
	READ32(taglen);

	/*
	 * We need this if our zero-copy I/O is going to work.  Rumor has
	 * it that the spec will soon mandate it...
	 */
	if (taglen != cp->taglen)
		dprintk("nfs4: non-conforming server returns tag length mismatch!\n");

	READ_BUF(taglen + 4);
	p += XDR_QUADLEN(taglen);
	READ32(cp->resp_nops);
	if (cp->resp_nops > cp->req_nops) {
		dprintk("nfs4: resp_nops > req_nops!\n");
		goto xdr_error;
	}

	for (cp->nops = 0; cp->nops < cp->resp_nops; cp->nops++) {
		READ_BUF(8);
		READ32(opnum);
		if (opnum != cp->ops[cp->nops].opnum) {
			dprintk("nfs4: operation mismatch!\n");
			goto xdr_error;
		}
		READ32(nfserr);
		if (cp->nops == cp->resp_nops - 1) {
			if (nfserr != cp->toplevel_status) {
				dprintk("nfs4: status mismatch!\n");
				goto xdr_error;
			}
		}
		else if (nfserr) {
			dprintk("nfs4: intermediate status nonzero!\n");
			goto xdr_error;
		}
		cp->ops[cp->nops].nfserr = nfserr;

		switch (opnum) {
		case OP_ACCESS:
			status = decode_access(xdr, nfserr, &cp->ops[cp->nops].u.access);
			break;
		case OP_CLOSE:
			status = decode_close(xdr, nfserr, &cp->ops[cp->nops].u.close);
			break;
		case OP_COMMIT:
			status = decode_commit(xdr, nfserr, &cp->ops[cp->nops].u.commit);
			break;
		case OP_CREATE:
			status = decode_create(xdr, nfserr, &cp->ops[cp->nops].u.create);
			break;
		case OP_GETATTR:
			status = decode_getattr(xdr, nfserr, &cp->ops[cp->nops].u.getattr);
			break;
		case OP_GETFH:
			status = decode_getfh(xdr, nfserr, &cp->ops[cp->nops].u.getfh);
			break;
		case OP_LINK:
			status = decode_link(xdr, nfserr, &cp->ops[cp->nops].u.link);
			break;
		case OP_LOOKUP:
			status = 0;
			break;
		case OP_OPEN:
			status = decode_open(xdr, nfserr, &cp->ops[cp->nops].u.open);
			break;
		case OP_OPEN_CONFIRM:
			status = decode_open_confirm(xdr, nfserr, &cp->ops[cp->nops].u.open_confirm);
			break;
		case OP_PUTFH:
			status = 0;
			break;
		case OP_PUTROOTFH:
			status = 0;
			break;
		case OP_READ:
			status = decode_read(xdr, nfserr, &cp->ops[cp->nops].u.read);
			break;
		case OP_READDIR:
			status = decode_readdir(xdr, nfserr, req, &cp->ops[cp->nops].u.readdir);
			break;
		case OP_READLINK:
			status = decode_readlink(xdr, nfserr, req, &cp->ops[cp->nops].u.readlink);
			break;
		case OP_RESTOREFH:
			status = 0;
			break;
		case OP_REMOVE:
			status = decode_remove(xdr, nfserr, &cp->ops[cp->nops].u.remove);
			break;
		case OP_RENAME:
			status = decode_rename(xdr, nfserr, &cp->ops[cp->nops].u.rename);
			break;
		case OP_RENEW:
			status = 0;
			break;
		case OP_SAVEFH:
			status = 0;
			break;
		case OP_SETATTR:
			status = decode_setattr(xdr);
			break;
		case OP_SETCLIENTID:
			status = decode_setclientid(xdr, nfserr, &cp->ops[cp->nops].u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			status = 0;
			break;
		case OP_WRITE:
			status = decode_write(xdr, nfserr, &cp->ops[cp->nops].u.write);
			break;
		default:
			BUG();
			return -EIO;
		}
		if (status)
			goto xdr_error;
	}

	DECODE_TAIL;
}
/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * Decode COMPOUND response
 */
static int
nfs4_xdr_dec_compound(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_compound *cp)
{
	struct xdr_stream xdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound(&xdr, cp, rqstp)))
		goto out;
	
	status = 0;
	if (cp->toplevel_status)
		status = -nfs_stat_to_errno(cp->toplevel_status);

out:
	return status;
}

uint32_t *
nfs4_decode_dirent(uint32_t *p, struct nfs_entry *entry, int plus)
{
	uint32_t len;

	if (!*p++) {
		if (!*p)
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	entry->prev_cookie = entry->cookie;
	p = xdr_decode_hyper(p, &entry->cookie);
	entry->len = ntohl(*p++);
	entry->name = (const char *) p;
	p += XDR_QUADLEN(entry->len);

	if (entry->cookie > COOKIE_MAX)
		entry->cookie = COOKIE_MAX;
	
	/*
	 * In case the server doesn't return an inode number,
	 * we fake one here.  (We don't use inode number 0,
	 * since glibc seems to choke on it...)
	 */
	entry->ino = 1;

	len = ntohl(*p++);             /* bitmap length */
	p += len;
	len = ntohl(*p++);             /* attribute buffer length */
	if (len)
		p = xdr_decode_hyper(p, &entry->ino);

	entry->eof = !p[0] && p[1];
	return p;
}

#ifndef MAX
# define MAX(a, b)	(((a) > (b))? (a) : (b))
#endif

#define PROC(proc, argtype, restype)				\
[NFSPROC4_##proc] = {						\
	.p_proc   = NFSPROC4_##proc,				\
	.p_encode = (kxdrproc_t) nfs4_xdr_##argtype,		\
	.p_decode = (kxdrproc_t) nfs4_xdr_##restype,		\
	.p_bufsiz = MAX(NFS4_##argtype##_sz,NFS4_##restype##_sz) << 2,	\
    }

struct rpc_procinfo	nfs4_procedures[] = {
  PROC(COMPOUND,	enc_compound,	dec_compound)
};

struct rpc_version		nfs_version4 = {
	.number			= 4,
	.nrprocs		= sizeof(nfs4_procedures)/sizeof(nfs4_procedures[0]),
	.procs			= nfs4_procedures
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
