/*
 *  linux/fs/nfs/inode.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs inode and superblock handling functions
 *
 *  Modularised by Alan Cox <Alan.Cox@linux.org>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_flushd.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define CONFIG_NFS_SNAPSHOT 1
#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

static struct inode * __nfs_fhget(struct super_block *, struct nfs_fh *, struct nfs_fattr *);
void nfs_zap_caches(struct inode *);
static void nfs_invalidate_inode(struct inode *);

static struct inode *nfs_alloc_inode(struct super_block *sb);
static void nfs_destroy_inode(struct inode *);
static void nfs_write_inode(struct inode *,int);
static void nfs_delete_inode(struct inode *);
static void nfs_put_super(struct super_block *);
static void nfs_clear_inode(struct inode *);
static void nfs_umount_begin(struct super_block *);
static int  nfs_statfs(struct super_block *, struct statfs *);
static int  nfs_show_options(struct seq_file *, struct vfsmount *);

static struct super_operations nfs_sops = { 
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs_write_inode,
	.delete_inode	= nfs_delete_inode,
	.put_super	= nfs_put_super,
	.statfs		= nfs_statfs,
	.clear_inode	= nfs_clear_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
};

/*
 * RPC cruft for NFS
 */
struct rpc_stat			nfs_rpcstat = {
	.program		= &nfs_program
};
static struct rpc_version *	nfs_version[] = {
	NULL,
	NULL,
	&nfs_version2,
#ifdef CONFIG_NFS_V3
	&nfs_version3,
#endif
};

struct rpc_program		nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= sizeof(nfs_version) / sizeof(nfs_version[0]),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
};

static inline unsigned long
nfs_fattr_to_ino_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_ino_t(fattr->fileid);
}

static void
nfs_write_inode(struct inode *inode, int sync)
{
	int flags = sync ? FLUSH_WAIT : 0;

	nfs_sync_file(inode, NULL, 0, 0, flags);
}

static void
nfs_delete_inode(struct inode * inode)
{
	dprintk("NFS: delete_inode(%s/%ld)\n", inode->i_sb->s_id, inode->i_ino);

	/*
	 * The following can never actually happen...
	 */
	if (nfs_have_writebacks(inode) || nfs_have_read(inode)) {
		printk(KERN_ERR "nfs_delete_inode: inode %ld has pending RPC requests\n", inode->i_ino);
	}

	clear_inode(inode);
}

/*
 * For the moment, the only task for the NFS clear_inode method is to
 * release the mmap credential
 */
static void
nfs_clear_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct rpc_cred *cred = nfsi->mm_cred;

	if (cred)
		put_rpccred(cred);
	cred = nfsi->cache_access.cred;
	if (cred)
		put_rpccred(cred);
}

void
nfs_put_super(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);
	struct rpc_clnt	*rpc;

	/*
	 * First get rid of the request flushing daemon.
	 * Relies on rpc_shutdown_client() waiting on all
	 * client tasks to finish.
	 */
	nfs_reqlist_exit(server);

	if ((rpc = server->client) != NULL)
		rpc_shutdown_client(rpc);

	nfs_reqlist_free(server);

	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_down();	/* release rpc.lockd */
	rpciod_down();		/* release rpciod */

	kfree(server->hostname);
}

void
nfs_umount_begin(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);
	struct rpc_clnt	*rpc;

	/* -EIO all pending I/O */
	if ((rpc = server->client) != NULL)
		rpc_killall_tasks(rpc);
}


static inline unsigned long
nfs_block_bits(unsigned long bsize, unsigned char *nrbitsp)
{
	/* make sure blocksize is a power of two */
	if ((bsize & (bsize - 1)) || nrbitsp) {
		unsigned char	nrbits;

		for (nrbits = 31; nrbits && !(bsize & (1 << nrbits)); nrbits--)
			;
		bsize = 1 << nrbits;
		if (nrbitsp)
			*nrbitsp = nrbits;
	}

	return bsize;
}

/*
 * Calculate the number of 512byte blocks used.
 */
static inline unsigned long
nfs_calc_block_size(u64 tsize)
{
	loff_t used = (tsize + 511) >> 9;
	return (used > ULONG_MAX) ? ULONG_MAX : used;
}

/*
 * Compute and set NFS server blocksize
 */
static inline unsigned long
nfs_block_size(unsigned long bsize, unsigned char *nrbitsp)
{
	if (bsize < 1024)
		bsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	else if (bsize >= NFS_MAX_FILE_IO_BUFFER_SIZE)
		bsize = NFS_MAX_FILE_IO_BUFFER_SIZE;

	return nfs_block_bits(bsize, nrbitsp);
}

/*
 * Obtain the root inode of the file system.
 */
static struct inode *
nfs_get_root(struct super_block *sb, struct nfs_fh *rootfh)
{
	struct nfs_server	*server = NFS_SB(sb);
	struct nfs_fattr	fattr;
	struct inode		*inode;
	int			error;

	if ((error = server->rpc_ops->getroot(server, rootfh, &fattr)) < 0) {
		printk(KERN_NOTICE "nfs_get_root: getattr error = %d\n", -error);
		return NULL;
	}

	inode = __nfs_fhget(sb, rootfh, &fattr);
	return inode;
}

/*
 * The way this works is that the mount process passes a structure
 * in the data argument which contains the server's IP address
 * and the root file handle obtained from the server's mount
 * daemon. We stash these away in the private superblock fields.
 */
int nfs_fill_super(struct super_block *sb, struct nfs_mount_data *data, int silent)
{
	struct nfs_server	*server;
	struct rpc_xprt		*xprt = NULL;
	struct rpc_clnt		*clnt = NULL;
	struct inode		*root_inode = NULL;
	rpc_authflavor_t	authflavor;
	struct rpc_timeout	timeparms;
	struct nfs_fsinfo	fsinfo;
	int			tcp, version, maxlen;

	/* We probably want something more informative here */
	snprintf(sb->s_id, sizeof(sb->s_id), "%x:%x", MAJOR(sb->s_dev), MINOR(sb->s_dev));

	sb->s_magic      = NFS_SUPER_MAGIC;
	sb->s_op         = &nfs_sops;
	sb->s_blocksize_bits = 0;
	sb->s_blocksize  = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	server           = NFS_SB(sb);
	server->rsize    = nfs_block_size(data->rsize, NULL);
	server->wsize    = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags & NFS_MOUNT_FLAGMASK;

	if (data->flags & NFS_MOUNT_NOAC) {
		data->acregmin = data->acregmax = 0;
		data->acdirmin = data->acdirmax = 0;
		sb->s_flags |= MS_SYNCHRONOUS;
	}
	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;

	server->namelen  = data->namlen;
	server->hostname = kmalloc(strlen(data->hostname) + 1, GFP_KERNEL);
	if (!server->hostname)
		goto out_unlock;
	strcpy(server->hostname, data->hostname);
	INIT_LIST_HEAD(&server->lru_read);
	INIT_LIST_HEAD(&server->lru_dirty);
	INIT_LIST_HEAD(&server->lru_commit);
	INIT_LIST_HEAD(&server->lru_busy);

 nfsv3_try_again:
	server->caps = 0;
	/* Check NFS protocol revision and initialize RPC op vector
	 * and file handle pool. */
	if (data->flags & NFS_MOUNT_VER3) {
#ifdef CONFIG_NFS_V3
		server->rpc_ops = &nfs_v3_clientops;
		version = 3;
		server->caps |= NFS_CAP_READDIRPLUS;
		if (data->version < 4) {
			printk(KERN_NOTICE "NFS: NFSv3 not supported by mount program.\n");
			goto out_unlock;
		}
#else
		printk(KERN_NOTICE "NFS: NFSv3 not supported.\n");
		goto out_unlock;
#endif
	} else {
		server->rpc_ops = &nfs_v2_clientops;
		version = 2;
        }

	/* Which protocol do we use? */
	tcp   = (data->flags & NFS_MOUNT_TCP);

	/* Initialize timeout values */
	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_maxval  = tcp? RPC_MAX_TCP_TIMEOUT : RPC_MAX_UDP_TIMEOUT;
	timeparms.to_exponential = 1;

	if (!timeparms.to_initval)
		timeparms.to_initval = (tcp ? 600 : 11) * HZ / 10;
	if (!timeparms.to_retries)
		timeparms.to_retries = 5;

	/* Now create transport and client */
	xprt = xprt_create_proto(tcp? IPPROTO_TCP : IPPROTO_UDP,
						&server->addr, &timeparms);
	if (xprt == NULL)
		goto out_no_xprt;

	/* Choose authentication flavor */
	authflavor = RPC_AUTH_UNIX;
	if (data->flags & NFS_MOUNT_SECURE)
		authflavor = RPC_AUTH_DES;
	else if (data->flags & NFS_MOUNT_KERBEROS)
		authflavor = RPC_AUTH_KRB;

	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
				 version, authflavor);
	if (clnt == NULL)
		goto out_no_client;

	clnt->cl_intr     = (data->flags & NFS_MOUNT_INTR)? 1 : 0;
	clnt->cl_softrtry = (data->flags & NFS_MOUNT_SOFT)? 1 : 0;
	clnt->cl_droppriv = (data->flags & NFS_MOUNT_BROKEN_SUID) ? 1 : 0;
	clnt->cl_chatty   = 1;
	server->client    = clnt;

	/* Fire up rpciod if not yet running */
	if (rpciod_up() != 0)
		goto out_no_iod;

	/*
	 * Keep the super block locked while we try to get 
	 * the root fh attributes.
	 */
	/* Did getting the root inode fail? */
	if (!(root_inode = nfs_get_root(sb, &server->fh))
	    && (data->flags & NFS_MOUNT_VER3)) {
		data->flags &= ~NFS_MOUNT_VER3;
		rpciod_down();
		rpc_shutdown_client(server->client);
		goto nfsv3_try_again;
	}

	if (!root_inode)
		goto out_no_root;
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		goto out_no_root;

	sb->s_root->d_op = &nfs_dentry_operations;

	/* Get some general file system info */
        if (server->rpc_ops->statfs(server, &server->fh, &fsinfo) >= 0) {
		if (server->namelen == 0)
			server->namelen = fsinfo.namelen;
	} else {
		printk(KERN_NOTICE "NFS: cannot retrieve file system info.\n");
		goto out_no_root;
        }

	/* Work out a lot of parameters */
	if (data->rsize == 0)
		server->rsize = nfs_block_size(fsinfo.rtpref, NULL);
	if (data->wsize == 0)
		server->wsize = nfs_block_size(fsinfo.wtpref, NULL);
	/* NFSv3: we don't have bsize, but rather rtmult and wtmult... */
	if (!fsinfo.bsize)
		fsinfo.bsize = (fsinfo.rtmult>fsinfo.wtmult) ? fsinfo.rtmult : fsinfo.wtmult;
	/* Also make sure we don't go below rsize/wsize since
	 * RPC calls are expensive */
	if (fsinfo.bsize < server->rsize)
		fsinfo.bsize = server->rsize;
	if (fsinfo.bsize < server->wsize)
		fsinfo.bsize = server->wsize;

	if (data->bsize == 0)
		sb->s_blocksize = nfs_block_bits(fsinfo.bsize, &sb->s_blocksize_bits);
	if (server->rsize > fsinfo.rtmax)
		server->rsize = fsinfo.rtmax;
	if (server->wsize > fsinfo.wtmax)
		server->wsize = fsinfo.wtmax;

	server->rpages = (server->rsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if (server->rpages > NFS_READ_MAXIOV) {
		server->rpages = NFS_READ_MAXIOV;
		server->rsize = server->rpages << PAGE_CACHE_SHIFT;
	}

	server->wpages = (server->wsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
        if (server->wpages > NFS_WRITE_MAXIOV) {
		server->wpages = NFS_WRITE_MAXIOV;
                server->wsize = server->wpages << PAGE_CACHE_SHIFT;
	}

	server->dtsize = nfs_block_size(fsinfo.dtpref, NULL);
	if (server->dtsize > PAGE_CACHE_SIZE)
		server->dtsize = PAGE_CACHE_SIZE;
	if (server->dtsize > server->rsize)
		server->dtsize = server->rsize;

        maxlen = (version == 2) ? NFS2_MAXNAMLEN : NFS3_MAXNAMLEN;

        if (server->namelen == 0 || server->namelen > maxlen)
                server->namelen = maxlen;

	sb->s_maxbytes = fsinfo.maxfilesize;
	if (sb->s_maxbytes > MAX_LFS_FILESIZE) 
		sb->s_maxbytes = MAX_LFS_FILESIZE; 

	/* Fire up the writeback cache */
	if (nfs_reqlist_alloc(server) < 0) {
		printk(KERN_NOTICE "NFS: cannot initialize writeback cache.\n");
		goto failure_kill_reqlist;
	}

	/* We're airborne Set socket buffersize */
	rpc_setbufsize(clnt, server->wsize + 100, server->rsize + 100);

	/* Check whether to start the lockd process */
	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_up();
	return 0;

	/* Yargs. It didn't work out. */
 failure_kill_reqlist:
	nfs_reqlist_exit(server);
out_no_root:
	printk("nfs_read_super: get root inode failed\n");
	iput(root_inode);
	rpciod_down();
	goto out_shutdown;

out_no_iod:
	printk(KERN_WARNING "NFS: couldn't start rpciod!\n");
out_shutdown:
	rpc_shutdown_client(server->client);
	goto out_free_host;

out_no_client:
	printk(KERN_WARNING "NFS: cannot create RPC client.\n");
	xprt_destroy(xprt);
	goto out_free_host;

out_no_xprt:
	printk(KERN_WARNING "NFS: cannot create RPC transport.\n");

out_free_host:
	nfs_reqlist_free(server);
	kfree(server->hostname);
out_unlock:
	goto out_fail;

out_fail:
	return -EINVAL;
}

static int
nfs_statfs(struct super_block *sb, struct statfs *buf)
{
	struct nfs_server *server = NFS_SB(sb);
	unsigned char blockbits;
	unsigned long blockres;
	struct nfs_fsinfo res;
	int error;

	lock_kernel();

	error = server->rpc_ops->statfs(server, NFS_FH(sb->s_root->d_inode), &res);
	buf->f_type = NFS_SUPER_MAGIC;
	if (error < 0)
		goto out_err;

	if (res.bsize == 0)
		res.bsize = sb->s_blocksize;
	buf->f_bsize = nfs_block_bits(res.bsize, &blockbits);
	blockres = (1 << blockbits) - 1;
	buf->f_blocks = (res.tbytes + blockres) >> blockbits;
	buf->f_bfree = (res.fbytes + blockres) >> blockbits;
	buf->f_bavail = (res.abytes + blockres) >> blockbits;
	buf->f_files = res.tfiles;
	buf->f_ffree = res.afiles;
	if (res.namelen == 0 || res.namelen > server->namelen)
		res.namelen = server->namelen;
	buf->f_namelen = res.namelen;

 out:
	unlock_kernel();

	return 0;

 out_err:
	printk(KERN_WARNING "nfs_statfs: statfs error = %d\n", -error);
	buf->f_bsize = buf->f_blocks = buf->f_bfree = buf->f_bavail = -1;
	goto out;

}

static int nfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	static struct proc_nfs_info {
		int flag;
		char *str;
		char *nostr;
	} nfs_info[] = {
		{ NFS_MOUNT_SOFT, ",soft", ",hard" },
		{ NFS_MOUNT_INTR, ",intr", "" },
		{ NFS_MOUNT_POSIX, ",posix", "" },
		{ NFS_MOUNT_TCP, ",tcp", ",udp" },
		{ NFS_MOUNT_NOCTO, ",nocto", "" },
		{ NFS_MOUNT_NOAC, ",noac", "" },
		{ NFS_MOUNT_NONLM, ",nolock", ",lock" },
		{ NFS_MOUNT_BROKEN_SUID, ",broken_suid", "" },
		{ 0, NULL, NULL }
	};
	struct proc_nfs_info *nfs_infop;
	struct nfs_server *nfss = NFS_SB(mnt->mnt_sb);

	seq_printf(m, ",v%d", nfss->rpc_ops->version);
	seq_printf(m, ",rsize=%d", nfss->rsize);
	seq_printf(m, ",wsize=%d", nfss->wsize);
	if (nfss->acregmin != 3*HZ)
		seq_printf(m, ",acregmin=%d", nfss->acregmin/HZ);
	if (nfss->acregmax != 60*HZ)
		seq_printf(m, ",acregmax=%d", nfss->acregmax/HZ);
	if (nfss->acdirmin != 30*HZ)
		seq_printf(m, ",acdirmin=%d", nfss->acdirmin/HZ);
	if (nfss->acdirmax != 60*HZ)
		seq_printf(m, ",acdirmax=%d", nfss->acdirmax/HZ);
	for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
		if (nfss->flags & nfs_infop->flag)
			seq_puts(m, nfs_infop->str);
		else
			seq_puts(m, nfs_infop->nostr);
	}
	seq_puts(m, ",addr=");
	seq_escape(m, nfss->hostname, " \t\n\\");
	return 0;
}

/*
 * Invalidate the local caches
 */
void
nfs_zap_caches(struct inode *inode)
{
	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	NFS_ATTRTIMEO_UPDATE(inode) = jiffies;

	invalidate_inode_pages(inode);

	memset(NFS_COOKIEVERF(inode), 0, sizeof(NFS_COOKIEVERF(inode)));
	NFS_CACHEINV(inode);
}

/*
 * Invalidate, but do not unhash, the inode
 */
static void
nfs_invalidate_inode(struct inode *inode)
{
	umode_t save_mode = inode->i_mode;

	make_bad_inode(inode);
	inode->i_mode = save_mode;
	nfs_zap_caches(inode);
}

struct nfs_find_desc {
	struct nfs_fh		*fh;
	struct nfs_fattr	*fattr;
};

/*
 * In NFSv3 we can have 64bit inode numbers. In order to support
 * this, and re-exported directories (also seen in NFSv2)
 * we are forced to allow 2 different inodes to have the same
 * i_ino.
 */
static int
nfs_find_actor(struct inode *inode, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fh		*fh = desc->fh;
	struct nfs_fattr	*fattr = desc->fattr;

	if (NFS_FILEID(inode) != fattr->fileid)
		return 0;
	if (memcmp(NFS_FH(inode), fh, sizeof(struct nfs_fh)) != 0)
		return 0;
	if (is_bad_inode(inode))
		return 0;
	/* Force an attribute cache update if inode->i_count == 0 */
	if (!atomic_read(&inode->i_count))
		NFS_CACHEINV(inode);
	return 1;
}

static int
nfs_init_locked(struct inode *inode, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fh		*fh = desc->fh;
	struct nfs_fattr	*fattr = desc->fattr;

	NFS_FILEID(inode) = fattr->fileid;
	memcpy(NFS_FH(inode), fh, sizeof(struct nfs_fh));
	return 0;
}

/*
 * This is our own version of iget that looks up inodes by file handle
 * instead of inode number.  We use this technique instead of using
 * the vfs read_inode function because there is no way to pass the
 * file handle or current attributes into the read_inode function.
 *
 */
struct inode *
nfs_fhget(struct dentry *dentry, struct nfs_fh *fhandle,
				 struct nfs_fattr *fattr)
{
	struct super_block *sb = dentry->d_sb;

	dprintk("NFS: nfs_fhget(%s/%s fileid=%Ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(long long)fattr->fileid);
	return __nfs_fhget(sb, fhandle, fattr);
}

/* Don't use READDIRPLUS on directories that we believe are too large */
#define NFS_LIMIT_READDIRPLUS (8*PAGE_SIZE)

/*
 * Look up the inode by super block and fattr->fileid.
 */
static struct inode *
__nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr
	};
	struct inode *inode = NULL;
	unsigned long hash;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		goto out_no_inode;

	if (!fattr->nlink) {
		printk("NFS: Buggy server - nlink == 0!\n");
		goto out_no_inode;
	}

	hash = nfs_fattr_to_ino_t(fattr);

	if (!(inode = iget5_locked(sb, hash, nfs_find_actor, nfs_init_locked, &desc)))
		goto out_no_inode;

	if (inode->i_state & I_NEW) {
		__u64		new_size, new_mtime;
		loff_t		new_isize;
		time_t		new_atime;

		/* We set i_ino for the few things that still rely on it,
		 * such as stat(2) */
		inode->i_ino = hash;

		/* We can't support UPDATE_ATIME(), since the server will reset it */
		inode->i_flags |= S_NOATIME;
		inode->i_mode = fattr->mode;
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_inode_operations.
		 */
		inode->i_op = &nfs_file_inode_operations;
		if (S_ISREG(inode->i_mode)) {
			inode->i_fop = &nfs_file_operations;
			inode->i_data.a_ops = &nfs_file_aops;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = &nfs_dir_inode_operations;
			inode->i_fop = &nfs_dir_operations;
			if (nfs_server_capable(inode, NFS_CAP_READDIRPLUS)
			    && fattr->size <= NFS_LIMIT_READDIRPLUS)
				NFS_FLAGS(inode) |= NFS_INO_ADVISE_RDPLUS;
		} else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else
			init_special_inode(inode, inode->i_mode, fattr->rdev);

		new_mtime = fattr->mtime;
		new_size = fattr->size;
		new_isize = nfs_size_to_loff_t(fattr->size);
		new_atime = nfs_time_to_secs(fattr->atime);

		NFS_READTIME(inode) = fattr->timestamp;
		NFS_CACHE_CTIME(inode) = fattr->ctime;
		inode->i_ctime = nfs_time_to_secs(fattr->ctime);
		inode->i_atime = new_atime;
		NFS_CACHE_MTIME(inode) = new_mtime;
		inode->i_mtime = nfs_time_to_secs(new_mtime);
		NFS_MTIME_UPDATE(inode) = fattr->timestamp;
		NFS_CACHE_ISIZE(inode) = new_size;
		inode->i_size = new_isize;
		inode->i_mode = fattr->mode;
		inode->i_nlink = fattr->nlink;
		inode->i_uid = fattr->uid;
		inode->i_gid = fattr->gid;
		if (fattr->valid & NFS_ATTR_FATTR_V3) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
			inode->i_blksize = inode->i_sb->s_blocksize;
		} else {
			inode->i_blocks = fattr->du.nfs2.blocks;
			inode->i_blksize = fattr->du.nfs2.blocksize;
		}
		NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
		NFS_ATTRTIMEO_UPDATE(inode) = jiffies;
		memset(NFS_COOKIEVERF(inode), 0, sizeof(NFS_COOKIEVERF(inode)));
		NFS_I(inode)->cache_access.cred = NULL;

		unlock_new_inode(inode);
	} else
		nfs_refresh_inode(inode, fattr);
	dprintk("NFS: __nfs_fhget(%s/%Ld ct=%d)\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		atomic_read(&inode->i_count));

out:
	return inode;

out_no_inode:
	printk("__nfs_fhget: iget failed\n");
	goto out;
}

int
nfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs_fattr fattr;
	int error;

	lock_kernel();

	/*
	 * Make sure the inode is up-to-date.
	 */
	error = nfs_revalidate_inode(NFS_SERVER(inode),inode);
	if (error) {
#ifdef NFS_PARANOIA
printk("nfs_setattr: revalidate failed, error=%d\n", error);
#endif
		goto out;
	}

	if (!S_ISREG(inode->i_mode))
		attr->ia_valid &= ~ATTR_SIZE;

	filemap_fdatawrite(inode->i_mapping);
	error = nfs_wb_all(inode);
	filemap_fdatawait(inode->i_mapping);
	if (error)
		goto out;

	error = NFS_PROTO(inode)->setattr(inode, &fattr, attr);
	if (error)
		goto out;
	/*
	 * If we changed the size or mtime, update the inode
	 * now to avoid invalidating the page cache.
	 */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size != fattr.size)
			printk("nfs_setattr: attr=%Ld, fattr=%Ld??\n",
			       (long long) attr->ia_size, (long long)fattr.size);
		vmtruncate(inode, attr->ia_size);
	}

	/*
	 * If we changed the size or mtime, update the inode
	 * now to avoid invalidating the page cache.
	 */
	if (!(fattr.valid & NFS_ATTR_WCC)) {
		fattr.pre_size = NFS_CACHE_ISIZE(inode);
		fattr.pre_mtime = NFS_CACHE_MTIME(inode);
		fattr.pre_ctime = NFS_CACHE_CTIME(inode);
		fattr.valid |= NFS_ATTR_WCC;
	}
	/* Force an attribute cache update */
	NFS_CACHEINV(inode);
	error = nfs_refresh_inode(inode, &fattr);
out:
	unlock_kernel();
	return error;
}

/*
 * Wait for the inode to get unlocked.
 * (Used for NFS_INO_LOCKED and NFS_INO_REVALIDATING).
 */
int
nfs_wait_on_inode(struct inode *inode, int flag)
{
	struct rpc_clnt	*clnt = NFS_CLIENT(inode);
	struct nfs_inode *nfsi = NFS_I(inode);

	int error;
	if (!(NFS_FLAGS(inode) & flag))
		return 0;
	atomic_inc(&inode->i_count);
	error = nfs_wait_event(clnt, nfsi->nfs_i_wait,
				!(NFS_FLAGS(inode) & flag));
	iput(inode);
	return error;
}

int nfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int err = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (!err)
		generic_fillattr(inode, stat);
	return err;
}

/*
 * Ensure that mmap has a recent RPC credential for use when writing out
 * shared pages
 */
static inline void
nfs_set_mmcred(struct inode *inode, struct rpc_cred *cred)
{
	struct rpc_cred **p = &NFS_I(inode)->mm_cred,
			*oldcred = *p;

	*p = get_rpccred(cred);
	if (oldcred)
		put_rpccred(oldcred);
}

/*
 * These are probably going to contain hooks for
 * allocating and releasing RPC credentials for
 * the file. I'll have to think about Tronds patch
 * a bit more..
 */
int nfs_open(struct inode *inode, struct file *filp)
{
	struct rpc_auth *auth;
	struct rpc_cred *cred;
	int err = 0;

	lock_kernel();
	/* Ensure that we revalidate the data cache */
	if (NFS_SERVER(inode)->flags & NFS_MOUNT_NOCTO) {
		err = __nfs_revalidate_inode(NFS_SERVER(inode),inode);
		if (err)
			goto out;
	}
	auth = NFS_CLIENT(inode)->cl_auth;
	cred = rpcauth_lookupcred(auth, 0);
	filp->private_data = cred;
	if (filp->f_mode & FMODE_WRITE)
		nfs_set_mmcred(inode, cred);
out:
	unlock_kernel();
	return err;
}

int nfs_release(struct inode *inode, struct file *filp)
{
	struct rpc_cred *cred;

	lock_kernel();
	cred = nfs_file_cred(filp);
	if (cred)
		put_rpccred(cred);
	unlock_kernel();
	return 0;
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 */
int
__nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	int		 status = -ESTALE;
	struct nfs_fattr fattr;

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Ld)\n",
		inode->i_sb->s_id, (long long)NFS_FILEID(inode));

	lock_kernel();
	if (!inode || is_bad_inode(inode))
 		goto out_nowait;
	if (NFS_STALE(inode) && inode != inode->i_sb->s_root->d_inode)
 		goto out_nowait;

	while (NFS_REVALIDATING(inode)) {
		status = nfs_wait_on_inode(inode, NFS_INO_REVALIDATING);
		if (status < 0)
			goto out_nowait;
		if (time_before(jiffies,NFS_READTIME(inode)+NFS_ATTRTIMEO(inode))) {
			status = NFS_STALE(inode) ? -ESTALE : 0;
			goto out_nowait;
		}
	}
	NFS_FLAGS(inode) |= NFS_INO_REVALIDATING;

	status = NFS_PROTO(inode)->getattr(inode, &fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) getattr failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		if (status == -ESTALE) {
			NFS_FLAGS(inode) |= NFS_INO_STALE;
			if (inode != inode->i_sb->s_root->d_inode)
				remove_inode_hash(inode);
		}
		goto out;
	}

	status = nfs_refresh_inode(inode, &fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) refresh failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		goto out;
	}
	dfprintk(PAGECACHE, "NFS: (%s/%Ld) revalidation complete\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode));

	NFS_FLAGS(inode) &= ~NFS_INO_STALE;
out:
	NFS_FLAGS(inode) &= ~NFS_INO_REVALIDATING;
	wake_up(&NFS_I(inode)->nfs_i_wait);
 out_nowait:
	unlock_kernel();
	return status;
}

/*
 * nfs_fattr_obsolete - Test if attribute data is newer than cached data
 * @inode: inode
 * @fattr: attributes to test
 *
 * Avoid stuffing the attribute cache with obsolete information.
 * We always accept updates if the attribute cache timed out, or if
 * fattr->ctime is newer than our cached value.
 * If fattr->ctime matches the cached value, we still accept the update
 * if it increases the file size.
 */
static inline
int nfs_fattr_obsolete(struct inode *inode, struct nfs_fattr *fattr)
{
	s64 cdif;

	if (time_after(jiffies, NFS_READTIME(inode)+NFS_ATTRTIMEO(inode)))
		goto out_valid;
	if ((cdif = (s64)fattr->ctime - (s64)NFS_CACHE_CTIME(inode)) > 0)
		goto out_valid;
	/* Ugh... */
	if (cdif == 0 && fattr->size > NFS_CACHE_ISIZE(inode))
		goto out_valid;
	return -1;
 out_valid:
	return 0;
}

/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the inode to reflect the state
 * of the server's inode.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_inode_pages.
 * To make sure no other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
int
__nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	__u64		new_size, new_mtime;
	loff_t		new_isize;
	time_t		new_atime;
	int		invalid = 0;

	dfprintk(VFS, "NFS: refresh_inode(%s/%ld ct=%d info=0x%x)\n",
			inode->i_sb->s_id, inode->i_ino,
			atomic_read(&inode->i_count), fattr->valid);

	if (NFS_FILEID(inode) != fattr->fileid) {
		printk(KERN_ERR "nfs_refresh_inode: inode number mismatch\n"
		       "expected (%s/0x%Lx), got (%s/0x%Lx)\n",
		       inode->i_sb->s_id, (long long)NFS_FILEID(inode),
		       inode->i_sb->s_id, (long long)fattr->fileid);
		goto out_err;
	}

	/* Throw out obsolete READDIRPLUS attributes */
	if (time_before(fattr->timestamp, NFS_READTIME(inode)))
		return 0;
	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		goto out_changed;

 	new_mtime = fattr->mtime;
	new_size = fattr->size;
 	new_isize = nfs_size_to_loff_t(fattr->size);

	new_atime = nfs_time_to_secs(fattr->atime);
	/* Avoid races */
	if (nfs_fattr_obsolete(inode, fattr))
		goto out_nochange;

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	NFS_READTIME(inode) = fattr->timestamp;

	/*
	 * Note: NFS_CACHE_ISIZE(inode) reflects the state of the cache.
	 *       NOT inode->i_size!!!
	 */
	if (NFS_CACHE_ISIZE(inode) != new_size) {
#ifdef NFS_DEBUG_VERBOSE
		printk(KERN_DEBUG "NFS: isize change on %s/%ld\n", inode->i_sb->s_id, inode->i_ino);
#endif
		invalid = 1;
	}

	/*
	 * Note: we don't check inode->i_mtime since pipes etc.
	 *       can change this value in VFS without requiring a
	 *	 cache revalidation.
	 */
	if (NFS_CACHE_MTIME(inode) != new_mtime) {
#ifdef NFS_DEBUG_VERBOSE
		printk(KERN_DEBUG "NFS: mtime change on %s/%ld\n", inode->i_sb->s_id, inode->i_ino);
#endif
		invalid = 1;
	}

	/* Check Weak Cache Consistency data.
	 * If size and mtime match the pre-operation values, we can
	 * assume that any attribute changes were caused by our NFS
         * operation, so there's no need to invalidate the caches.
         */
        if ((fattr->valid & NFS_ATTR_WCC)
	    && NFS_CACHE_ISIZE(inode) == fattr->pre_size
	    && NFS_CACHE_MTIME(inode) == fattr->pre_mtime) {
		invalid = 0;
	}

	/*
	 * If we have pending writebacks, things can get
	 * messy.
	 */
	if (nfs_have_writebacks(inode) && new_isize < inode->i_size)
		new_isize = inode->i_size;

	NFS_CACHE_CTIME(inode) = fattr->ctime;
	inode->i_ctime = nfs_time_to_secs(fattr->ctime);

	inode->i_atime = new_atime;

	if (NFS_CACHE_MTIME(inode) != new_mtime) {
		if (invalid)
			NFS_MTIME_UPDATE(inode) = fattr->timestamp;
		NFS_CACHE_MTIME(inode) = new_mtime;
		inode->i_mtime = nfs_time_to_secs(new_mtime);
	}

	NFS_CACHE_ISIZE(inode) = new_size;
	inode->i_size = new_isize;

	if (inode->i_mode != fattr->mode ||
	    inode->i_uid != fattr->uid ||
	    inode->i_gid != fattr->gid) {
		struct rpc_cred **cred = &NFS_I(inode)->cache_access.cred;
		if (*cred) {
			put_rpccred(*cred);
			*cred = NULL;
		}
	}

	inode->i_mode = fattr->mode;
	inode->i_nlink = fattr->nlink;
	inode->i_uid = fattr->uid;
	inode->i_gid = fattr->gid;

	if (fattr->valid & NFS_ATTR_FATTR_V3) {
		/*
		 * report the blocks in 512byte units
		 */
		inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		inode->i_blksize = inode->i_sb->s_blocksize;
 	} else {
 		inode->i_blocks = fattr->du.nfs2.blocks;
 		inode->i_blksize = fattr->du.nfs2.blocksize;
 	}
 
	/* Update attrtimeo value */
	if (invalid) {
		NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
		NFS_ATTRTIMEO_UPDATE(inode) = jiffies;
		invalidate_inode_pages(inode);
		memset(NFS_COOKIEVERF(inode), 0, sizeof(NFS_COOKIEVERF(inode)));
	} else if (time_after(jiffies, NFS_ATTRTIMEO_UPDATE(inode)+NFS_ATTRTIMEO(inode))) {
		if ((NFS_ATTRTIMEO(inode) <<= 1) > NFS_MAXATTRTIMEO(inode))
			NFS_ATTRTIMEO(inode) = NFS_MAXATTRTIMEO(inode);
		NFS_ATTRTIMEO_UPDATE(inode) = jiffies;
	}

	return 0;
 out_nochange:
	if (new_atime - inode->i_atime > 0)
		inode->i_atime = new_atime;
	return 0;
 out_changed:
	/*
	 * Big trouble! The inode has become a different object.
	 */
#ifdef NFS_PARANOIA
	printk(KERN_DEBUG "nfs_refresh_inode: inode %ld mode changed, %07o to %07o\n",
	       inode->i_ino, inode->i_mode, fattr->mode);
#endif
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_invalidate_inode(inode);
 out_err:
	return -EIO;
}

/*
 * File system information
 */

static int nfs_set_super(struct super_block *s, void *data)
{
	s->u.generic_sbp = data;
	return set_anon_super(s, data);
}
 
static int nfs_compare_super(struct super_block *sb, void *data)
{
	struct nfs_server *server = data;
	struct nfs_server *old = NFS_SB(sb);

	if (old->addr.sin_addr.s_addr != server->addr.sin_addr.s_addr)
		return 0;
	if (old->addr.sin_port != server->addr.sin_port)
		return 0;
	return !memcmp(&old->fh, &server->fh, sizeof(struct nfs_fh));
}

static struct super_block *nfs_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *raw_data)
{
	int error;
	struct nfs_server *server;
	struct super_block *s;
	struct nfs_fh *root;
	struct nfs_mount_data *data = raw_data;

	if (!data) {
		printk("nfs_read_super: missing data argument\n");
		return ERR_PTR(-EINVAL);
	}

	server = kmalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return ERR_PTR(-ENOMEM);
	memset(server, 0, sizeof(struct nfs_server));

	root = &server->fh;
	memcpy(root, &data->root, sizeof(*root));
	if (root->size < sizeof(root->data))
		memset(root->data+root->size, 0, sizeof(root->data)-root->size);

	if (data->version != NFS_MOUNT_VERSION) {
		printk("nfs warning: mount version %s than kernel\n",
			data->version < NFS_MOUNT_VERSION ? "older" : "newer");
		if (data->version < 2)
			data->namlen = 0;
		if (data->version < 3)
			data->bsize  = 0;
		if (data->version < 4) {
			data->flags &= ~NFS_MOUNT_VER3;
			memset(root, 0, sizeof(*root));
			root->size = NFS2_FHSIZE;
			memcpy(root->data, data->old_root.data, NFS2_FHSIZE);
		}
	}

	if (root->size > sizeof(root->data)) {
		printk("nfs_get_sb: invalid root filehandle\n");
		return ERR_PTR(-EINVAL);
	}
	/* We now require that the mount process passes the remote address */
	memcpy(&server->addr, &data->addr, sizeof(server->addr));
	if (server->addr.sin_addr.s_addr == INADDR_ANY) {
		printk("NFS: mount program didn't pass remote address!\n");
		kfree(server);
		return ERR_PTR(-EINVAL);
	}

	s = sget(fs_type, nfs_compare_super, nfs_set_super, server);

	if (IS_ERR(s) || s->s_root) {
		kfree(server);
		return s;
	}

	s->s_flags = flags;

	error = nfs_fill_super(s, data, flags & MS_VERBOSE ? 1 : 0);
	if (error) {
		up_write(&s->s_umount);
		deactivate_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= MS_ACTIVE;
	return s;
}

static void nfs_kill_super(struct super_block *s)
{
	struct nfs_server *server = NFS_SB(s);
	kill_anon_super(s);
	kfree(server);
}

static struct file_system_type nfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs",
	.get_sb		= nfs_get_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_ODD_RENAME,
};

extern int nfs_init_nfspagecache(void);
extern void nfs_destroy_nfspagecache(void);
extern int nfs_init_readpagecache(void);
extern int nfs_destroy_readpagecache(void);
extern int nfs_init_writepagecache(void);
extern int nfs_destroy_writepagecache(void);

static kmem_cache_t * nfs_inode_cachep;

static struct inode *nfs_alloc_inode(struct super_block *sb)
{
	struct nfs_inode *nfsi;
	nfsi = (struct nfs_inode *)kmem_cache_alloc(nfs_inode_cachep, SLAB_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0;
	nfsi->mm_cred = NULL;
	return &nfsi->vfs_inode;
}

static void nfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(nfs_inode_cachep, NFS_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct nfs_inode *nfsi = (struct nfs_inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&nfsi->vfs_inode);
		INIT_LIST_HEAD(&nfsi->read);
		INIT_LIST_HEAD(&nfsi->dirty);
		INIT_LIST_HEAD(&nfsi->commit);
		INIT_LIST_HEAD(&nfsi->writeback);
		nfsi->nread = 0;
		nfsi->ndirty = 0;
		nfsi->ncommit = 0;
		nfsi->npages = 0;
		init_waitqueue_head(&nfsi->nfs_i_wait);
	}
}
 
int nfs_init_inodecache(void)
{
	nfs_inode_cachep = kmem_cache_create("nfs_inode_cache",
					     sizeof(struct nfs_inode),
					     0, SLAB_HWCACHE_ALIGN,
					     init_once, NULL);
	if (nfs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_inodecache(void)
{
	if (kmem_cache_destroy(nfs_inode_cachep))
		printk(KERN_INFO "nfs_inode_cache: not all structures were freed\n");
}

/*
 * Initialize NFS
 */
static int __init init_nfs_fs(void)
{
	int err;

	err = nfs_init_nfspagecache();
	if (err)
		goto out4;

	err = nfs_init_inodecache();
	if (err)
		goto out3;

	err = nfs_init_readpagecache();
	if (err)
		goto out2;

	err = nfs_init_writepagecache();
	if (err)
		goto out1;

#ifdef CONFIG_PROC_FS
	rpc_proc_register(&nfs_rpcstat);
#endif
        err = register_filesystem(&nfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	rpc_proc_unregister("nfs");
	nfs_destroy_writepagecache();
out1:
	nfs_destroy_readpagecache();
out2:
	nfs_destroy_inodecache();
out3:
	nfs_destroy_nfspagecache();
out4:
	return err;
}

static void __exit exit_nfs_fs(void)
{
	nfs_destroy_writepagecache();
	nfs_destroy_readpagecache();
	nfs_destroy_inodecache();
	nfs_destroy_nfspagecache();
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	unregister_filesystem(&nfs_fs_type);
}

/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
