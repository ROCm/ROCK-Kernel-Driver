/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#include <linux/config.h>
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/uio.h>

#include <linux/nfs_fs_sb.h>

#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/clnt.h>

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_xdr.h>

/*
 * Enable debugging support for nfs client.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFS_DEBUG
#endif

#define NFS_MAX_FILE_IO_BUFFER_SIZE	32768
#define NFS_DEF_FILE_IO_BUFFER_SIZE	4096

/*
 * The upper limit on timeouts for the exponential backoff algorithm.
 */
#define NFS_WRITEBACK_DELAY		(5*HZ)
#define NFS_WRITEBACK_LOCKDELAY		(60*HZ)
#define NFS_COMMIT_DELAY		(5*HZ)

/*
 * superblock magic number for NFS
 */
#define NFS_SUPER_MAGIC			0x6969

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

#define NFS_RW_SYNC		0x0001	/* O_SYNC handling */
#define NFS_RW_SWAP		0x0002	/* This is a swap request */

/*
 * When flushing a cluster of dirty pages, there can be different
 * strategies:
 */
#define FLUSH_AGING		0	/* only flush old buffers */
#define FLUSH_SYNC		1	/* file being synced, or contention */
#define FLUSH_WAIT		2	/* wait for completion */
#define FLUSH_STABLE		4	/* commit to stable storage */

#ifdef __KERNEL__

/*
 * NFSv3 Access mode cache
 */
struct nfs_access_cache {
	unsigned long		jiffies;
	struct rpc_cred *	cred;
	int			mask;
	int			err;
};

/*
 * nfs fs inode data in memory
 */
struct nfs_inode {
	/*
	 * The 64bit 'inode number'
	 */
	__u64 fileid;

	/*
	 * NFS file handle
	 */
	struct nfs_fh		fh;

	/*
	 * Various flags
	 */
	unsigned short		flags;

	/*
	 * read_cache_jiffies is when we started read-caching this inode,
	 * and read_cache_mtime is the mtime of the inode at that time.
	 * attrtimeo is for how long the cached information is assumed
	 * to be valid. A successful attribute revalidation doubles
	 * attrtimeo (up to acregmax/acdirmax), a failure resets it to
	 * acregmin/acdirmin.
	 *
	 * We need to revalidate the cached attrs for this inode if
	 *
	 *	jiffies - read_cache_jiffies > attrtimeo
	 *
	 * and invalidate any cached data/flush out any dirty pages if
	 * we find that
	 *
	 *	mtime != read_cache_mtime
	 */
	unsigned long		read_cache_jiffies;
	struct timespec		read_cache_ctime;
	struct timespec		read_cache_mtime;
	__u64			read_cache_isize;
	unsigned long		attrtimeo;
	unsigned long		attrtimeo_timestamp;
	__u64			change_attr;		/* v4 only */

	/*
	 * Timestamp that dates the change made to read_cache_mtime.
	 * This is of use for dentry revalidation
	 */
	unsigned long		cache_mtime_jiffies;

	struct nfs_access_cache	cache_access;

	/*
	 * This is the cookie verifier used for NFSv3 readdir
	 * operations
	 */
	__u32			cookieverf[2];

	/*
	 * This is the list of dirty unwritten pages.
	 */
	struct list_head	dirty;
	struct list_head	commit;
	struct radix_tree_root	nfs_page_tree;

	unsigned int		ndirty,
				ncommit,
				npages;

	/* Credentials for shared mmap */
	struct rpc_cred		*mm_cred;

	wait_queue_head_t	nfs_i_wait;

#ifdef CONFIG_NFS_V4
        /* NFSv4 state */
	struct list_head	open_states;
#endif /* CONFIG_NFS_V4*/

	struct inode		vfs_inode;
};

/*
 * Legal inode flag values
 */
#define NFS_INO_STALE		0x0001		/* possible stale inode */
#define NFS_INO_ADVISE_RDPLUS   0x0002          /* advise readdirplus */
#define NFS_INO_REVALIDATING	0x0004		/* revalidating attrs */
#define NFS_INO_FLUSH		0x0008		/* inode is due for flushing */
#define NFS_INO_FAKE_ROOT	0x0080		/* root inode placeholder */

static inline struct nfs_inode *NFS_I(struct inode *inode)
{
	return container_of(inode, struct nfs_inode, vfs_inode);
}
#define NFS_SB(s)		((struct nfs_server *)(s->s_fs_info))

#define NFS_FH(inode)			(&NFS_I(inode)->fh)
#define NFS_SERVER(inode)		(NFS_SB(inode->i_sb))
#define NFS_CLIENT(inode)		(NFS_SERVER(inode)->client)
#define NFS_PROTO(inode)		(NFS_SERVER(inode)->rpc_ops)
#define NFS_ADDR(inode)			(RPC_PEERADDR(NFS_CLIENT(inode)))
#define NFS_COOKIEVERF(inode)		(NFS_I(inode)->cookieverf)
#define NFS_READTIME(inode)		(NFS_I(inode)->read_cache_jiffies)
#define NFS_MTIME_UPDATE(inode)		(NFS_I(inode)->cache_mtime_jiffies)
#define NFS_CACHE_CTIME(inode)		(NFS_I(inode)->read_cache_ctime)
#define NFS_CACHE_MTIME(inode)		(NFS_I(inode)->read_cache_mtime)
#define NFS_CACHE_ISIZE(inode)		(NFS_I(inode)->read_cache_isize)
#define NFS_CHANGE_ATTR(inode)		(NFS_I(inode)->change_attr)
#define NFS_CACHEINV(inode) \
do { \
	NFS_READTIME(inode) = jiffies - NFS_MAXATTRTIMEO(inode) - 1; \
} while (0)
#define NFS_ATTRTIMEO(inode)		(NFS_I(inode)->attrtimeo)
#define NFS_MINATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmin \
			       : NFS_SERVER(inode)->acregmin)
#define NFS_MAXATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmax \
			       : NFS_SERVER(inode)->acregmax)
#define NFS_ATTRTIMEO_UPDATE(inode)	(NFS_I(inode)->attrtimeo_timestamp)

#define NFS_FLAGS(inode)		(NFS_I(inode)->flags)
#define NFS_REVALIDATING(inode)		(NFS_FLAGS(inode) & NFS_INO_REVALIDATING)
#define NFS_STALE(inode)		(NFS_FLAGS(inode) & NFS_INO_STALE)
#define NFS_FAKE_ROOT(inode)		(NFS_FLAGS(inode) & NFS_INO_FAKE_ROOT)

#define NFS_FILEID(inode)		(NFS_I(inode)->fileid)

static inline int nfs_server_capable(struct inode *inode, int cap)
{
	return NFS_SERVER(inode)->caps & cap;
}

static inline int NFS_USE_READDIRPLUS(struct inode *inode)
{
	return NFS_FLAGS(inode) & NFS_INO_ADVISE_RDPLUS;
}

static inline
loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_CACHE_SHIFT;
}

/*
 * linux/fs/nfs/inode.c
 */
extern void nfs_zap_caches(struct inode *);
extern struct inode *nfs_fhget(struct super_block *, struct nfs_fh *,
				struct nfs_fattr *);
extern int __nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int nfs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
extern int nfs_permission(struct inode *, int, struct nameidata *);
extern void nfs_set_mmcred(struct inode *, struct rpc_cred *);
extern int nfs_open(struct inode *, struct file *);
extern int nfs_release(struct inode *, struct file *);
extern int __nfs_revalidate_inode(struct nfs_server *, struct inode *);
extern int nfs_setattr(struct dentry *, struct iattr *);

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;
extern struct file_operations nfs_file_operations;
extern struct address_space_operations nfs_file_aops;

static __inline__ struct rpc_cred *
nfs_file_cred(struct file *file)
{
	struct rpc_cred *cred = NULL;
	if (file)
		cred = (struct rpc_cred *)file->private_data;
#ifdef RPC_DEBUG
	if (cred && cred->cr_magic != RPCAUTH_CRED_MAGIC)
		BUG();
#endif
	return cred;
}

/*
 * linux/fs/nfs/direct.c
 */
extern int nfs_direct_IO(int, struct kiocb *, const struct iovec *, loff_t,
			unsigned long);

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
extern struct file_operations nfs_dir_operations;
extern struct dentry_operations nfs_dentry_operations;

/*
 * linux/fs/nfs/symlink.c
 */
extern struct inode_operations nfs_symlink_inode_operations;

/*
 * linux/fs/nfs/locks.c
 */
extern int nfs_lock(struct file *, int, struct file_lock *);

/*
 * linux/fs/nfs/unlink.c
 */
extern int  nfs_async_unlink(struct dentry *);
extern void nfs_complete_unlink(struct dentry *);

/*
 * linux/fs/nfs/write.c
 */
extern int  nfs_writepage(struct page *page, struct writeback_control *wbc);
extern int  nfs_writepages(struct address_space *, struct writeback_control *);
extern int  nfs_flush_incompatible(struct file *file, struct page *page);
extern int  nfs_updatepage(struct file *, struct page *, unsigned int, unsigned int);
extern void nfs_writeback_done(struct rpc_task *task);
extern void nfs_writedata_release(struct rpc_task *task);

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
extern void nfs_commit_release(struct rpc_task *task);
extern void nfs_commit_done(struct rpc_task *);
#endif

/*
 * Try to write back everything synchronously (but check the
 * return value!)
 */
extern int  nfs_sync_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_flush_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_flush_list(struct list_head *, int, int);
#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
extern int  nfs_commit_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_commit_list(struct list_head *, int);
#else
static inline int
nfs_commit_file(struct inode *inode, struct file *file, unsigned long offset,
		unsigned int len, int flags)
{
	return 0;
}
#endif

static inline int
nfs_have_writebacks(struct inode *inode)
{
	return NFS_I(inode)->npages != 0;
}

static inline int
nfs_wb_all(struct inode *inode)
{
	int error = nfs_sync_file(inode, 0, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
static inline int
nfs_wb_page(struct inode *inode, struct page* page)
{
	int error = nfs_sync_file(inode, 0, page->index, 1,
						FLUSH_WAIT | FLUSH_STABLE);
	return (error < 0) ? error : 0;
}

/*
 * Write back all pending writes for one user.. 
 */
static inline int
nfs_wb_file(struct inode *inode, struct file *file)
{
	int error = nfs_sync_file(inode, file, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

/*
 * linux/fs/nfs/read.c
 */
extern int  nfs_readpage(struct file *, struct page *);
extern int  nfs_readpages(struct file *, struct address_space *,
		struct list_head *, unsigned);
extern int  nfs_pagein_list(struct list_head *, int);
extern void nfs_readpage_result(struct rpc_task *);
extern void nfs_readdata_release(struct rpc_task *);

/*
 * linux/fs/mount_clnt.c
 * (Used only by nfsroot module)
 */
extern int  nfsroot_mount(struct sockaddr_in *, char *, struct nfs_fh *,
		int, int);

/*
 * inline functions
 */
static inline int
nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	if (time_before(jiffies, NFS_READTIME(inode)+NFS_ATTRTIMEO(inode)))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return __nfs_revalidate_inode(server, inode);
}

static inline int
nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	return __nfs_refresh_inode(inode,fattr);
}

static inline loff_t
nfs_size_to_loff_t(__u64 size)
{
	loff_t maxsz = (((loff_t) ULONG_MAX) << PAGE_CACHE_SHIFT) + PAGE_CACHE_SIZE - 1;
	if (size > maxsz)
		return maxsz;
	return (loff_t) size;
}

static inline ino_t
nfs_fileid_to_ino_t(u64 fileid)
{
	ino_t ino = (ino_t) fileid;
	if (sizeof(ino_t) < sizeof(u64))
		ino ^= fileid >> (sizeof(u64)-sizeof(ino_t)) * 8;
	return ino;
}

/* NFS root */

extern void * nfs_root_data(void);

#define nfs_wait_event(clnt, wq, condition)				\
({									\
	int __retval = 0;						\
	if (clnt->cl_intr) {						\
		sigset_t oldmask;					\
		rpc_clnt_sigmask(clnt, &oldmask);			\
		__retval = wait_event_interruptible(wq, condition);	\
		rpc_clnt_sigunmask(clnt, &oldmask);			\
	} else								\
		wait_event(wq, condition);				\
	__retval;							\
})

#define NFS_JUKEBOX_RETRY_TIME (5 * HZ)

#ifdef CONFIG_NFS_V4

/*
 * In a seqid-mutating op, this macro controls which error return
 * values trigger incrementation of the seqid.
 *
 * from rfc 3010:
 * The client MUST monotonically increment the sequence number for the
 * CLOSE, LOCK, LOCKU, OPEN, OPEN_CONFIRM, and OPEN_DOWNGRADE
 * operations.  This is true even in the event that the previous
 * operation that used the sequence number received an error.  The only
 * exception to this rule is if the previous operation received one of
 * the following errors: NFSERR_STALE_CLIENTID, NFSERR_STALE_STATEID,
 * NFSERR_BAD_STATEID, NFSERR_BAD_SEQID, NFSERR_BADXDR,
 * NFSERR_RESOURCE, NFSERR_NOFILEHANDLE.
 *
 */
#define seqid_mutating_err(err)       \
(((err) != NFSERR_STALE_CLIENTID) &&  \
 ((err) != NFSERR_STALE_STATEID)  &&  \
 ((err) != NFSERR_BAD_STATEID)    &&  \
 ((err) != NFSERR_BAD_SEQID)      &&  \
 ((err) != NFSERR_BAD_XDR)        &&  \
 ((err) != NFSERR_RESOURCE)       &&  \
 ((err) != NFSERR_NOFILEHANDLE))

enum nfs4_client_state {
	NFS4CLNT_OK  = 0,
	NFS4CLNT_NEW,
};

/*
 * The nfs4_client identifies our client state to the server.
 */
struct nfs4_client {
	struct list_head	cl_servers;	/* Global list of servers */
	struct in_addr		cl_addr;	/* Server identifier */
	u64			cl_clientid;	/* constant */
	nfs4_verifier		cl_confirm;
	enum nfs4_client_state	cl_state;

	u32			cl_lockowner_id;

	/*
	 * The following rwsem ensures exclusive access to the server
	 * while we recover the state following a lease expiration.
	 */
	struct rw_semaphore	cl_sem;

	struct list_head	cl_state_owners;
	struct list_head	cl_unused;
	int			cl_nunused;
	spinlock_t		cl_lock;
	atomic_t		cl_count;
};

/*
 * NFS4 state_owners and lock_owners are simply labels for ordered
 * sequences of RPC calls. Their sole purpose is to provide once-only
 * semantics by allowing the server to identify replayed requests.
 *
 * The ->so_sema is held during all state_owner seqid-mutating operations:
 * OPEN, OPEN_DOWNGRADE, and CLOSE. Its purpose is to properly serialize
 * so_seqid.
 */
struct nfs4_state_owner {
	struct list_head     so_list;	 /* per-clientid list of state_owners */
	struct nfs4_client   *so_client;
	u32                  so_id;      /* 32-bit identifier, unique */
	struct semaphore     so_sema;
	u32                  so_seqid;   /* protected by so_sema */
	unsigned int         so_flags;   /* protected by so_sema */
	atomic_t	     so_count;

	struct rpc_cred	     *so_cred;	 /* Associated cred */
	struct list_head     so_states;
};

/*
 * struct nfs4_state maintains the client-side state for a given
 * (state_owner,inode) tuple.
 *
 * In order to know when to OPEN_DOWNGRADE or CLOSE the state on the server,
 * we need to know how many files are open for reading or writing on a
 * given inode. This information too is stored here.
 */
struct nfs4_state {
	struct list_head open_states;	/* List of states for the same state_owner */
	struct list_head inode_states;	/* List of states for the same inode */

	struct nfs4_state_owner *owner;	/* Pointer to the open owner */
	struct inode *inode;		/* Pointer to the inode */
	pid_t pid;			/* Thread that called OPEN */

	nfs4_stateid stateid;

	int state;			/* State on the server (R,W, or RW) */
	atomic_t count;
};


/* nfs4proc.c */
extern int nfs4_proc_renew(struct nfs_server *server);
extern int nfs4_do_close(struct inode *, struct nfs4_state *);

/* nfs4renewd.c */
extern int nfs4_init_renewd(struct nfs_server *server);

/* nfs4state.c */
extern struct nfs4_client *nfs4_get_client(struct in_addr *);
extern void nfs4_put_client(struct nfs4_client *clp);
extern struct nfs4_state_owner * nfs4_get_state_owner(struct nfs_server *, struct rpc_cred *);
extern void nfs4_put_state_owner(struct nfs4_state_owner *);
extern struct nfs4_state * nfs4_get_open_state(struct inode *, struct nfs4_state_owner *);
extern void nfs4_put_open_state(struct nfs4_state *);
extern void nfs4_increment_seqid(u32 status, struct nfs4_state_owner *sp);






struct nfs4_mount_data;
static inline int
create_nfsv4_state(struct nfs_server *server, struct nfs4_mount_data *data)
{
	server->nfs4_state = NULL;
	return 0;
}

static inline void
destroy_nfsv4_state(struct nfs_server *server)
{
	if (server->mnt_path) {
		kfree(server->mnt_path);
		server->mnt_path = NULL;
	}
	if (server->nfs4_state) {
		nfs4_put_client(server->nfs4_state);
		server->nfs4_state = NULL;
	}
}
#else
#define create_nfsv4_state(server, data)  0
#define destroy_nfsv4_state(server)       do { } while (0)
#define nfs4_put_state_owner(inode, owner) do { } while (0)
#define nfs4_put_open_state(state) do { } while (0)
#endif

#endif /* __KERNEL__ */

/*
 * NFS debug flags
 */
#define NFSDBG_VFS		0x0001
#define NFSDBG_DIRCACHE		0x0002
#define NFSDBG_LOOKUPCACHE	0x0004
#define NFSDBG_PAGECACHE	0x0008
#define NFSDBG_PROC		0x0010
#define NFSDBG_XDR		0x0020
#define NFSDBG_FILE		0x0040
#define NFSDBG_ROOT		0x0080
#define NFSDBG_ALL		0xFFFF

#ifdef __KERNEL__
# undef ifdebug
# ifdef NFS_DEBUG
#  define ifdebug(fac)		if (nfs_debug & NFSDBG_##fac)
# else
#  define ifdebug(fac)		if (0)
# endif
#endif /* __KERNEL */

#endif
