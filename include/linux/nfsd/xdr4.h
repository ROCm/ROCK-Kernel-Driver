/*
 *  include/linux/nfsd/xdr4.h
 *
 *  Server-side types for NFSv4.
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
 *
 */

#ifndef _LINUX_NFSD_XDR4_H
#define _LINUX_NFSD_XDR4_H

#define NFSD4_MAX_TAGLEN	128

typedef u32 delegation_zero_t;
typedef u32 delegation_boot_t;
typedef u64 delegation_id_t;

typedef struct {
	delegation_zero_t	ds_zero;
	delegation_boot_t	ds_boot;
	delegation_id_t		ds_id;
} delegation_stateid_t;

struct nfsd4_change_info {
	u32		atomic;
	u32		before_size;
	u32		before_ctime;
	u32		after_size;
	u32		after_ctime;
};

struct nfsd4_access {
	u32		ac_req_access;      /* request */
	u32		ac_supported;       /* response */
	u32		ac_resp_access;     /* response */
};

struct nfsd4_close {
	u32		cl_seqid;           /* request */
	stateid_t	cl_stateid;         /* request+response */
};

struct nfsd4_commit {
	u64		co_offset;          /* request */
	u32		co_count;           /* request */
	nfs4_verifier	co_verf;            /* response */
};

struct nfsd4_create {
	u32		cr_namelen;         /* request */
	char *		cr_name;            /* request */
	u32		cr_type;            /* request */
	union {                             /* request */
		struct {
			u32 namelen;
			char *name;
		} link;   /* NF4LNK */
		struct {
			u32 specdata1;
			u32 specdata2;
		} dev;    /* NF4BLK, NF4CHR */
	} u;
	u32		cr_bmval[2];        /* request */
	struct iattr	cr_iattr;           /* request */
	struct nfsd4_change_info  cr_cinfo; /* response */
};
#define cr_linklen	u.link.namelen
#define cr_linkname	u.link.name
#define cr_specdata1	u.dev.specdata1
#define cr_specdata2	u.dev.specdata2

struct nfsd4_getattr {
	u32		ga_bmval[2];        /* request */
	struct svc_fh	*ga_fhp;            /* response */
};

struct nfsd4_link {
	u32		li_namelen;         /* request */
	char *		li_name;            /* request */
	struct nfsd4_change_info  li_cinfo; /* response */
};

struct nfsd4_lookup {
	u32		lo_len;             /* request */
	char *		lo_name;            /* request */
};

struct nfsd4_putfh {
	u32		pf_fhlen;           /* request */
	char		*pf_fhval;          /* request */
};

struct nfsd4_open {
	u32		op_claim_type;      /* request */
	struct xdr_netobj op_fname;	    /* request - everything but CLAIM_PREV */
	u32		op_delegate_type;   /* request - CLAIM_PREV only */
	delegation_stateid_t	op_delegate_stateid; /* request - CLAIM_DELEGATE_CUR only */
	u32		op_create;     	    /* request */
	u32		op_createmode;      /* request */
	u32		op_bmval[2];        /* request */
	union {                             /* request */
		struct iattr	iattr;		            /* UNCHECKED4,GUARDED4 */
		nfs4_verifier	verf;		                     /* EXCLUSIVE4 */
	} u;
	clientid_t	op_clientid;        /* request */
	struct xdr_netobj op_owner;           /* request */
	u32		op_seqid;           /* request */
	u32		op_share_access;    /* request */
	u32		op_share_deny;      /* request */
	stateid_t	op_stateid;         /* response */
	struct nfsd4_change_info  op_cinfo; /* response */
	u32		op_rflags;          /* response */
	int		op_truncate;        /* used during processing */
	struct nfs4_stateowner *op_stateowner; /* used during processing */

};
#define op_iattr	u.iattr
#define op_verf		u.verf

struct nfsd4_read {
	stateid_t	rd_stateid;         /* request */
	u64		rd_offset;          /* request */
	u32		rd_length;          /* request */
	struct iovec	rd_iov[RPCSVC_MAXPAGES];
	int		rd_vlen;
	
	struct svc_rqst *rd_rqstp;          /* response */
	struct svc_fh * rd_fhp;             /* response */
};

struct nfsd4_readdir {
	u64		rd_cookie;          /* request */
	nfs4_verifier	rd_verf;            /* request */
	u32		rd_dircount;        /* request */
	u32		rd_maxcount;        /* request */
	u32		rd_bmval[2];        /* request */
	struct svc_rqst *rd_rqstp;          /* response */
	struct svc_fh * rd_fhp;             /* response */

	struct readdir_cd	common;
	u32 *			buffer;
	int			buflen;
	u32 *			offset;
};

struct nfsd4_readlink {
	struct svc_rqst *rl_rqstp;          /* request */
	struct svc_fh *	rl_fhp;             /* request */
};

struct nfsd4_remove {
	u32		rm_namelen;         /* request */
	char *		rm_name;            /* request */
	struct nfsd4_change_info  rm_cinfo; /* response */
};

struct nfsd4_rename {
	u32		rn_snamelen;        /* request */
	char *		rn_sname;           /* request */
	u32		rn_tnamelen;        /* request */
	char *		rn_tname;           /* request */
	struct nfsd4_change_info  rn_sinfo; /* response */
	struct nfsd4_change_info  rn_tinfo; /* response */
};

struct nfsd4_setattr {
	stateid_t	sa_stateid;         /* request */
	u32		sa_bmval[2];        /* request */
	struct iattr	sa_iattr;           /* request */
};

struct nfsd4_setclientid {
	nfs4_verifier	se_verf;            /* request */
	u32		se_namelen;         /* request */
	char *		se_name;            /* request */
	u32		se_callback_prog;   /* request */
	u32		se_callback_netid_len;  /* request */
	char *		se_callback_netid_val;  /* request */
	u32		se_callback_addr_len;   /* request */
	char *		se_callback_addr_val;   /* request */
	u32		se_callback_ident;  /* request */
	clientid_t	se_clientid;        /* response */
	nfs4_verifier	se_confirm;         /* response */
};

struct nfsd4_setclientid_confirm {
	clientid_t	sc_clientid;
	nfs4_verifier	sc_confirm;
};

/* also used for NVERIFY */
struct nfsd4_verify {
	u32		ve_bmval[2];        /* request */
	u32		ve_attrlen;         /* request */
	char *		ve_attrval;         /* request */
};

struct nfsd4_write {
	stateid_t	wr_stateid;         /* request */
	u64		wr_offset;          /* request */
	u32		wr_stable_how;      /* request */
	u32		wr_buflen;          /* request */
	struct iovec	wr_vec[RPCSVC_MAXPAGES]; /* request */
	int		wr_vlen;

	u32		wr_bytes_written;   /* response */
	u32		wr_how_written;     /* response */
	nfs4_verifier	wr_verifier;        /* response */
};

struct nfsd4_op {
	int					opnum;
	int					status;
	union {
		struct nfsd4_access		access;
		struct nfsd4_close		close;
		struct nfsd4_commit		commit;
		struct nfsd4_create		create;
		struct nfsd4_getattr		getattr;
		struct svc_fh *			getfh;
		struct nfsd4_link		link;
		struct nfsd4_lookup		lookup;
		struct nfsd4_verify		nverify;
		struct nfsd4_open		open;
		struct nfsd4_putfh		putfh;
		struct nfsd4_read		read;
		struct nfsd4_readdir		readdir;
		struct nfsd4_readlink		readlink;
		struct nfsd4_remove		remove;
		struct nfsd4_rename		rename;
		clientid_t			renew;
		struct nfsd4_setattr		setattr;
		struct nfsd4_setclientid	setclientid;
		struct nfsd4_setclientid_confirm setclientid_confirm;
		struct nfsd4_verify		verify;
		struct nfsd4_write		write;
	} u;
};

struct nfsd4_compoundargs {
	/* scratch variables for XDR decode */
	u32 *				p;
	u32 *				end;
	struct page **			pagelist;
	int				pagelen;
	u32				tmp[8];
	u32 *				tmpp;
	struct tmpbuf {
		struct tmpbuf *next;
		void *buf;
	}				*to_free;
	
	u32				taglen;
	char *				tag;
	u32				minorversion;
	u32				opcnt;
	struct nfsd4_op			*ops;
	struct nfsd4_op			iops[8];
};

struct nfsd4_compoundres {
	/* scratch variables for XDR encode */
	u32 *				p;
	u32 *				end;
	struct xdr_buf *		xbuf;
	struct svc_rqst *		rqstp;

	u32				taglen;
	char *				tag;
	u32				opcnt;
	u32 *				tagp; /* where to encode tag and  opcount */
};

#define NFS4_SVC_XDRSIZE		sizeof(struct nfsd4_compoundargs)

static inline void
set_change_info(struct nfsd4_change_info *cinfo, struct svc_fh *fhp)
{
	BUG_ON(!fhp->fh_pre_saved || !fhp->fh_post_saved);
	cinfo->atomic = 1;
	cinfo->before_size = fhp->fh_pre_size;
	cinfo->before_ctime = fhp->fh_pre_ctime.tv_sec;
	cinfo->after_size = fhp->fh_post_size;
	cinfo->after_ctime = fhp->fh_post_ctime.tv_sec;
}

int nfs4svc_encode_voidres(struct svc_rqst *, u32 *, void *);
int nfs4svc_decode_compoundargs(struct svc_rqst *, u32 *, struct nfsd4_compoundargs *);
int nfs4svc_encode_compoundres(struct svc_rqst *, u32 *, struct nfsd4_compoundres *);

void nfsd4_encode_operation(struct nfsd4_compoundres *, struct nfsd4_op *);
int nfsd4_encode_fattr(struct svc_fh *fhp, struct svc_export *exp,
		       struct dentry *dentry, u32 *buffer, int *countp, u32 *bmval);
extern int nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_setclientid *setclid);
extern int nfsd4_setclientid_confirm(struct svc_rqst *rqstp, struct nfsd4_setclientid_confirm *setclientid_confirm);
extern int nfsd4_process_open1(struct nfsd4_open *open);
extern int nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open);
#endif

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
