#ifndef _LINUX_NFS_XDR_H
#define _LINUX_NFS_XDR_H

#include <linux/sunrpc/xprt.h>

struct nfs_fattr {
	unsigned short		valid;		/* which fields are valid */
	__u64			pre_size;	/* pre_op_attr.size	  */
	struct timespec		pre_mtime;	/* pre_op_attr.mtime	  */
	struct timespec		pre_ctime;	/* pre_op_attr.ctime	  */
	enum nfs_ftype		type;		/* always use NFSv2 types */
	__u32			mode;
	__u32			nlink;
	__u32			uid;
	__u32			gid;
	__u64			size;
	union {
		struct {
			__u32	blocksize;
			__u32	blocks;
		} nfs2;
		struct {
			__u64	used;
		} nfs3;
	} du;
	dev_t			rdev;
	union {
		__u64		nfs3;		/* also nfs2 */
		struct {
			__u64	major;
			__u64	minor;
		} nfs4;
	} fsid_u;
	__u64			fileid;
	struct timespec		atime;
	struct timespec		mtime;
	struct timespec		ctime;
	__u32			bitmap[2];	/* NFSv4 returned attribute bitmap */
	__u64			change_attr;	/* NFSv4 change attribute */
	__u64			pre_change_attr;/* pre-op NFSv4 change attribute */
	unsigned long		timestamp;
};

#define NFS_ATTR_WCC		0x0001		/* pre-op WCC data    */
#define NFS_ATTR_FATTR		0x0002		/* post-op attributes */
#define NFS_ATTR_FATTR_V3	0x0004		/* NFSv3 attributes */
#define NFS_ATTR_FATTR_V4	0x0008
#define NFS_ATTR_PRE_CHANGE	0x0010

/*
 * Info on the file system
 */
struct nfs_fsinfo {
	struct nfs_fattr	*fattr; /* Post-op attributes */
	__u32			rtmax;	/* max.  read transfer size */
	__u32			rtpref;	/* pref. read transfer size */
	__u32			rtmult;	/* reads should be multiple of this */
	__u32			wtmax;	/* max.  write transfer size */
	__u32			wtpref;	/* pref. write transfer size */
	__u32			wtmult;	/* writes should be multiple of this */
	__u32			dtpref;	/* pref. readdir transfer size */
	__u64			maxfilesize;
	__u32			lease_time; /* in seconds */
};

struct nfs_fsstat {
	struct nfs_fattr	*fattr; /* Post-op attributes */
	__u64			tbytes;	/* total size in bytes */
	__u64			fbytes;	/* # of free bytes */
	__u64			abytes;	/* # of bytes available to user */
	__u64			tfiles;	/* # of files */
	__u64			ffiles;	/* # of free files */
	__u64			afiles;	/* # of files available to user */
};

struct nfs2_fsstat {
	__u32			tsize;  /* Server transfer size */
	__u32			bsize;  /* Filesystem block size */
	__u32			blocks; /* No. of "bsize" blocks on filesystem */
	__u32			bfree;  /* No. of free "bsize" blocks */
	__u32			bavail; /* No. of available "bsize" blocks */
};

struct nfs_pathconf {
	struct nfs_fattr	*fattr; /* Post-op attributes */
	__u32			max_link; /* max # of hard links */
	__u32			max_namelen; /* max name length */
};

/*
 * Arguments to the open call.
 */
struct nfs_openargs {
	struct nfs_fh *         fh;
	__u32                   seqid;
	__u32                   share_access;
	__u64                   clientid;
	__u32                   id;
	__u32                   opentype;
	__u32                   createmode;
	union {
		struct iattr *  attrs;    /* UNCHECKED, GUARDED */
		nfs4_verifier   verifier; /* EXCLUSIVE */
	} u;
	struct qstr *           name;
	struct nfs4_getattr *   f_getattr;
	struct nfs4_getattr *   d_getattr;
	struct nfs_server *     server;	 /* Needed for ID mapping */
};

struct nfs_openres {
	nfs4_stateid            stateid;
	struct nfs_fh           fh;
	struct nfs4_change_info * cinfo;
	__u32                   rflags;
	struct nfs4_getattr *   f_getattr;
	struct nfs4_getattr *   d_getattr;
	struct nfs_server *     server;
};

/*
 * Arguments to the open_confirm call.
 */
struct nfs_open_confirmargs {
	struct nfs_fh *         fh;
	nfs4_stateid            stateid;
	__u32                   seqid;
};

struct nfs_open_confirmres {
	nfs4_stateid            stateid;
};

/*
 * Arguments to the open_reclaim call.
 */
struct nfs_open_reclaimargs {
	struct nfs_fh *		fh;
	__u64			clientid;
	__u32			seqid;
	__u32			id;
	__u32			share_access;
	__u32			claim;
	struct nfs4_getattr *   f_getattr;
};

/*
 * Arguments to the close call.
 */
struct nfs_closeargs {
	struct nfs_fh *         fh;
	nfs4_stateid            stateid;
	__u32                   seqid;
	__u32			share_access;
};

struct nfs_closeres {
	nfs4_stateid            stateid;
};
/*
 *  * Arguments to the lock,lockt, and locku call.
 *   */
struct nfs_lowner {
	__u64           clientid;
	u32                     id;
};

struct nfs_open_to_lock {
	__u32                   open_seqid;
	nfs4_stateid            open_stateid;
	__u32                   lock_seqid;
	struct nfs_lowner       lock_owner;
};

struct nfs_exist_lock {
	nfs4_stateid            stateid;
	__u32                   seqid;
};

struct nfs_lock_opargs {
	__u32                   reclaim;
	__u32                   new_lock_owner;
	union {
		struct nfs_open_to_lock *open_lock;
		struct nfs_exist_lock   *exist_lock;
	} u;
};

struct nfs_locku_opargs {
	__u32                   seqid;
	nfs4_stateid            stateid;
};

struct nfs_lockargs {
	struct nfs_fh *         fh;
	__u32                   type;
	__u64                   offset; 
	__u64                   length; 
	union {
		struct nfs_lock_opargs  *lock;    /* LOCK  */
		struct nfs_lowner       *lockt;  /* LOCKT */
		struct nfs_locku_opargs *locku;  /* LOCKU */
	} u;
};

struct nfs_lock_denied {
	__u64                   offset;
	__u64                   length;
	__u32                   type;
	struct nfs_lowner   	owner;
};

struct nfs_lockres {
	union {
		nfs4_stateid            stateid;/* LOCK success, LOCKU */
		struct nfs_lock_denied  denied; /* LOCK failed, LOCKT success */
	} u;
	struct nfs_server *     server;
};

/*
 * Arguments to the read call.
 */

#define NFS_READ_MAXIOV		(9U)
#if (NFS_READ_MAXIOV > (MAX_IOVEC -2))
#error "NFS_READ_MAXIOV is too large"
#endif

struct nfs_readargs {
	struct nfs_fh *		fh;
	fl_owner_t		lockowner;
	struct nfs4_state *	state;
	__u64			offset;
	__u32			count;
	unsigned int		pgbase;
	struct page **		pages;
};

struct nfs_readres {
	struct nfs_fattr *	fattr;
	__u32			count;
	int                     eof;
};

/*
 * Arguments to the write call.
 */
#define NFS_WRITE_MAXIOV	(9U)
#if (NFS_WRITE_MAXIOV > (MAX_IOVEC -2))
#error "NFS_WRITE_MAXIOV is too large"
#endif

struct nfs_writeargs {
	struct nfs_fh *		fh;
	fl_owner_t		lockowner;
	struct nfs4_state *	state;
	__u64			offset;
	__u32			count;
	enum nfs3_stable_how	stable;
	unsigned int		pgbase;
	struct page **		pages;
};

struct nfs_writeverf {
	enum nfs3_stable_how	committed;
	__u32			verifier[2];
};

struct nfs_writeres {
	struct nfs_fattr *	fattr;
	struct nfs_writeverf *	verf;
	__u32			count;
};

/*
 * Argument struct for decode_entry function
 */
struct nfs_entry {
	__u64			ino;
	__u64			cookie,
				prev_cookie;
	const char *		name;
	unsigned int		len;
	int			eof;
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

/*
 * The following types are for NFSv2 only.
 */
struct nfs_sattrargs {
	struct nfs_fh *		fh;
	struct iattr *		sattr;
};

struct nfs_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
};

struct nfs_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	struct iattr *		sattr;
};

struct nfs_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs_setattrargs {
	struct nfs_fh *                 fh;
	nfs4_stateid                    stateid;
	struct iattr *                  iap;
	struct nfs4_getattr *           attr;
	struct nfs_server *             server; /* Needed for name mapping */
};

struct nfs_setattrres {
	struct nfs4_getattr *           attr;
	struct nfs_server *             server;
};

struct nfs_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	const char *		topath;
	unsigned int		tolen;
	struct iattr *		sattr;
};

struct nfs_readdirargs {
	struct nfs_fh *		fh;
	__u32			cookie;
	unsigned int		count;
	struct page **		pages;
};

struct nfs_diropok {
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs_readlinkargs {
	struct nfs_fh *		fh;
	unsigned int		count;
	struct page **		pages;
};

struct nfs3_sattrargs {
	struct nfs_fh *		fh;
	struct iattr *		sattr;
	unsigned int		guard;
	struct timespec		guardtime;
};

struct nfs3_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
};

struct nfs3_accessargs {
	struct nfs_fh *		fh;
	__u32			access;
};

struct nfs3_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	struct iattr *		sattr;
	enum nfs3_createmode	createmode;
	__u32			verifier[2];
};

struct nfs3_mkdirargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	struct iattr *		sattr;
};

struct nfs3_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	const char *		topath;
	unsigned int		tolen;
	struct iattr *		sattr;
};

struct nfs3_mknodargs {
	struct nfs_fh *		fh;
	const char *		name;
	unsigned int		len;
	enum nfs3_ftype		type;
	struct iattr *		sattr;
	dev_t			rdev;
};

struct nfs3_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	unsigned int		fromlen;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs3_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
	unsigned int		tolen;
};

struct nfs3_readdirargs {
	struct nfs_fh *		fh;
	__u64			cookie;
	__u32			verf[2];
	int			plus;
	unsigned int            count;
	struct page **		pages;
};

struct nfs3_diropres {
	struct nfs_fattr *	dir_attr;
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs3_accessres {
	struct nfs_fattr *	fattr;
	__u32			access;
};

struct nfs3_readlinkargs {
	struct nfs_fh *		fh;
	unsigned int		count;
	struct page **		pages;
};

struct nfs3_renameres {
	struct nfs_fattr *	fromattr;
	struct nfs_fattr *	toattr;
};

struct nfs3_linkres {
	struct nfs_fattr *	dir_attr;
	struct nfs_fattr *	fattr;
};

struct nfs3_readdirres {
	struct nfs_fattr *	dir_attr;
	__u32 *			verf;
	int			plus;
};

#ifdef CONFIG_NFS_V4

typedef u64 clientid4;

struct nfs4_change_info {
	u32				atomic;
	u64				before;
	u64				after;
};

struct nfs4_accessargs {
	const struct nfs_fh *		fh;
	u32				access;
};

struct nfs4_accessres {
	u32				supported;
	u32				access;
};

struct nfs4_close {
	char *				cl_stateid;        /* request */
	u32				cl_seqid;          /* request */
};

struct nfs4_create {
	u32				cr_ftype;          /* request */
	union {                                            /* request */
		struct {
			u32		textlen;
			const char *	text;
		} symlink;   /* NF4LNK */
		struct {
			u32		specdata1;
			u32		specdata2;
		} device;    /* NF4BLK, NF4CHR */
	} u;
	u32				cr_namelen;        /* request */
	const char *			cr_name;           /* request */
	struct iattr *			cr_attrs;          /* request */
	struct nfs4_change_info	*	cr_cinfo;          /* response */
};
#define cr_textlen			u.symlink.textlen
#define cr_text				u.symlink.text
#define cr_specdata1			u.device.specdata1
#define cr_specdata2			u.device.specdata2

struct nfs4_getattr {
        u32 *				gt_bmval;          /* request */
        struct nfs_fattr *		gt_attrs;          /* response */
	struct nfs_fsstat *		gt_fsstat;         /* response */
	struct nfs_pathconf *		gt_pathconf;       /* response */
};

struct nfs4_getfh {
	struct nfs_fh *			gf_fhandle;       /* response */
};

struct nfs4_link {
	u32				ln_namelen;       /* request */
	const char *			ln_name;          /* request */
	struct nfs4_change_info *	ln_cinfo;         /* response */
};

struct nfs4_lookup {
	struct qstr *			lo_name;          /* request */
};

struct nfs4_open {
	struct nfs4_client *		op_client_state;  /* request */
	u32				op_share_access;  /* request */
	u32				op_opentype;      /* request */
	u32				op_createmode;    /* request */
	union {                                           /* request */
		struct iattr *		attrs;    /* UNCHECKED, GUARDED */
		nfs4_verifier		verifier; /* EXCLUSIVE */
	} u;
	struct qstr *			op_name;          /* request */
	char *				op_stateid;       /* response */
	struct nfs4_change_info	*	op_cinfo;         /* response */
	u32 *				op_rflags;        /* response */
};
#define op_attrs     u.attrs
#define op_verifier  u.verifier

struct nfs4_open_confirm {
	char *				oc_stateid;       /* request */
};

struct nfs4_putfh {
	struct nfs_fh *			pf_fhandle;       /* request */
};

struct nfs4_readdir {
	u64				rd_cookie;        /* request */
	nfs4_verifier			rd_req_verifier;  /* request */
	u32				rd_count;         /* request */
	u32				rd_bmval[2];      /* request */	
	nfs4_verifier			rd_resp_verifier; /* response */
	struct page **			rd_pages;   /* zero-copy data */
	unsigned int			rd_pgbase;  /* zero-copy data */
};

struct nfs4_readlink {
	u32				rl_count;   /* zero-copy data */
	struct page **			rl_pages;   /* zero-copy data */
};

struct nfs4_remove {
	u32				rm_namelen;       /* request */
	const char *			rm_name;          /* request */
	struct nfs4_change_info *	rm_cinfo;         /* response */
};

struct nfs4_rename {
	u32				rn_oldnamelen;    /* request */
	const char *			rn_oldname;       /* request */
	u32				rn_newnamelen;    /* request */
	const char *			rn_newname;       /* request */
	struct nfs4_change_info	*	rn_src_cinfo;     /* response */
	struct nfs4_change_info *	rn_dst_cinfo;     /* response */
};

struct nfs4_setattr {
	char *				st_stateid;       /* request */
	struct iattr *			st_iap;           /* request */
};

struct nfs4_setclientid {
	nfs4_verifier			sc_verifier;      /* request */
	char *				sc_name;	  /* request */
	u32				sc_prog;          /* request */
	char				sc_netid[4];	  /* request */
	char				sc_uaddr[24];     /* request */
	u32				sc_cb_ident;      /* request */
	struct nfs4_client *		sc_state;	  /* response */
};

struct nfs4_op {
	u32				opnum;
	union {
		struct nfs4_close	close;
		struct nfs4_create	create;
		struct nfs4_getattr	getattr;
		struct nfs4_getfh	getfh;
		struct nfs4_link	link;
		struct nfs4_lookup	lookup;
		struct nfs4_open	open;
		struct nfs4_open_confirm open_confirm;
		struct nfs4_putfh	putfh;
		struct nfs4_readdir	readdir;
		struct nfs4_readlink	readlink;
		struct nfs4_remove	remove;
		struct nfs4_rename	rename;
		struct nfs4_client *	renew;
		struct nfs4_setattr	setattr;
	} u;
};

struct nfs4_compound {
	unsigned int		flags;   /* defined below */
	struct nfs_server *	server;

	/* RENEW information */
	int			renew_index;
	unsigned long		timestamp;

	/* scratch variables for XDR encode/decode */
	int			nops;
	u32 *			p;
	u32 *			end;

	/* the individual COMPOUND operations */
	struct nfs4_op		*ops;

	/* request */
	int			req_nops;
	u32			taglen;
	char *			tag;
	
	/* response */
	int			resp_nops;
	int			toplevel_status;
};

#endif /* CONFIG_NFS_V4 */

struct nfs_page;

struct nfs_read_data {
	int			flags;
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_fattr	fattr;	/* fattr storage */
	struct list_head	pages;	/* Coalesced read requests */
	struct nfs_page		*req;	/* multi ops per nfs_page */
	struct page		*pagevec[NFS_READ_MAXIOV];
	struct nfs_readargs args;
	struct nfs_readres  res;
#ifdef CONFIG_NFS_V4
	unsigned long		timestamp;	/* For lease renewal */
#endif
	void (*complete) (struct nfs_read_data *, int);
};

struct nfs_write_data {
	int			flags;
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_fattr	fattr;
	struct nfs_writeverf	verf;
	struct list_head	pages;		/* Coalesced requests we wish to flush */
	struct nfs_page		*req;		/* multi ops per nfs_page */
	struct page		*pagevec[NFS_WRITE_MAXIOV];
	struct nfs_writeargs	args;		/* argument struct */
	struct nfs_writeres	res;		/* result struct */
#ifdef CONFIG_NFS_V4
	unsigned long		timestamp;	/* For lease renewal */
#endif
	void (*complete) (struct nfs_write_data *, int);
};

/*
 * RPC procedure vector for NFSv2/NFSv3 demuxing
 */
struct nfs_rpc_ops {
	int	version;		/* Protocol version */
	struct dentry_operations *dentry_ops;
	struct inode_operations *dir_inode_ops;

	int	(*getroot) (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*getattr) (struct inode *, struct nfs_fattr *);
	int	(*setattr) (struct dentry *, struct nfs_fattr *,
			    struct iattr *);
	int	(*lookup)  (struct inode *, struct qstr *,
			    struct nfs_fh *, struct nfs_fattr *);
	int	(*access)  (struct inode *, struct rpc_cred *, int);
	int	(*readlink)(struct inode *, struct page *);
	int	(*read)    (struct nfs_read_data *, struct file *);
	int	(*write)   (struct nfs_write_data *, struct file *);
	int	(*commit)  (struct nfs_write_data *, struct file *);
	struct inode *	(*create)  (struct inode *, struct qstr *,
			    struct iattr *, int);
	int	(*remove)  (struct inode *, struct qstr *);
	int	(*unlink_setup)  (struct rpc_message *,
			    struct dentry *, struct qstr *);
	int	(*unlink_done) (struct dentry *, struct rpc_task *);
	int	(*rename)  (struct inode *, struct qstr *,
			    struct inode *, struct qstr *);
	int	(*link)    (struct inode *, struct inode *, struct qstr *);
	int	(*symlink) (struct inode *, struct qstr *, struct qstr *,
			    struct iattr *, struct nfs_fh *,
			    struct nfs_fattr *);
	int	(*mkdir)   (struct inode *, struct qstr *, struct iattr *,
			    struct nfs_fh *, struct nfs_fattr *);
	int	(*rmdir)   (struct inode *, struct qstr *);
	int	(*readdir) (struct dentry *, struct rpc_cred *,
			    u64, struct page *, unsigned int, int);
	int	(*mknod)   (struct inode *, struct qstr *, struct iattr *,
			    dev_t, struct nfs_fh *, struct nfs_fattr *);
	int	(*statfs)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsstat *);
	int	(*fsinfo)  (struct nfs_server *, struct nfs_fh *,
			    struct nfs_fsinfo *);
	int	(*pathconf) (struct nfs_server *, struct nfs_fh *,
			     struct nfs_pathconf *);
	u32 *	(*decode_dirent)(u32 *, struct nfs_entry *, int plus);
	void	(*read_setup)   (struct nfs_read_data *);
	void	(*write_setup)  (struct nfs_write_data *, int how);
	void	(*commit_setup) (struct nfs_write_data *, int how);
	int	(*file_open)   (struct inode *, struct file *);
	int	(*file_release) (struct inode *, struct file *);
	void	(*request_init)(struct nfs_page *, struct file *);
	int	(*request_compatible)(struct nfs_page *, struct file *, struct page *);
	int	(*lock)(struct file *, int, struct file_lock *);
};

/*
 * 	NFS_CALL(getattr, inode, (fattr));
 * into
 *	NFS_PROTO(inode)->getattr(fattr);
 */
#define NFS_CALL(op, inode, args)	NFS_PROTO(inode)->op args

/*
 * Function vectors etc. for the NFS client
 */
extern struct nfs_rpc_ops	nfs_v2_clientops;
extern struct nfs_rpc_ops	nfs_v3_clientops;
extern struct nfs_rpc_ops	nfs_v4_clientops;
extern struct rpc_version	nfs_version2;
extern struct rpc_version	nfs_version3;
extern struct rpc_version	nfs_version4;
extern struct rpc_program	nfs_program;
extern struct rpc_stat		nfs_rpcstat;

#endif
