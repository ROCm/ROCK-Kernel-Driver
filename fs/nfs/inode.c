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
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

/* Maximum number of readahead requests
 * FIXME: this should really be a sysctl so that users may tune it to suit
 *        their needs. People that do NFS over a slow network, might for
 *        instance want to reduce it to something closer to 1 for improved
 *        interactive response.
 *
 *        For the moment, though, we instead set it to RPC_MAXREQS, which
 *        is the maximum number of simultaneous RPC requests on the wire.
 */
#define NFS_MAX_READAHEAD	RPC_MAXREQS

void nfs_zap_caches(struct inode *);
static void nfs_invalidate_inode(struct inode *);

static struct inode *nfs_alloc_inode(struct super_block *sb);
static void nfs_destroy_inode(struct inode *);
static void nfs_write_inode(struct inode *,int);
static void nfs_delete_inode(struct inode *);
static void nfs_put_super(struct super_block *);
static void nfs_clear_inode(struct inode *);
static void nfs_umount_begin(struct super_block *);
static int  nfs_statfs(struct super_block *, struct kstatfs *);
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
#if defined(CONFIG_NFS_V3)
	&nfs_version3,
#elif defined(CONFIG_NFS_V4)
	NULL,
#endif
#if defined(CONFIG_NFS_V4)
	&nfs_version4,
#endif
};

struct rpc_program		nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= sizeof(nfs_version) / sizeof(nfs_version[0]),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
	.pipe_dir_name		= "/nfs",
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

	nfs_commit_file(inode, NULL, 0, 0, flags);
}

static void
nfs_delete_inode(struct inode * inode)
{
	dprintk("NFS: delete_inode(%s/%ld)\n", inode->i_sb->s_id, inode->i_ino);

	/*
	 * The following can never actually happen...
	 */
	if (nfs_have_writebacks(inode)) {
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

#ifdef CONFIG_NFS_V4
	if (server->idmap != NULL)
		nfs_idmap_delete(server);
#endif /* CONFIG_NFS_V4 */

	if (server->client != NULL)
		rpc_shutdown_client(server->client);
	if (server->client_sys != NULL)
		rpc_shutdown_client(server->client_sys);

	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_down();	/* release rpc.lockd */
	rpciod_down();		/* release rpciod */

	destroy_nfsv4_state(server);

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
static int
nfs_get_root(struct inode **rooti, rpc_authflavor_t authflavor, struct super_block *sb, struct nfs_fh *rootfh)
{
	struct nfs_server	*server = NFS_SB(sb);
	struct nfs_fattr	fattr = { };
	int			error;

	error = server->rpc_ops->getroot(server, rootfh, &fattr);
	if (error == -EACCES && authflavor > RPC_AUTH_MAXFLAVOR) {
		/*
		 * Some authentication types (gss/krb5, most notably)
		 * are such that root won't be able to present a
		 * credential for GETATTR (ie, getroot()).
		 *
		 * We still want the mount to succeed.
		 * 
		 * So we fake the attr values and mark the inode as such.
		 * On the first succesful traversal, we fix everything.
		 * The auth type test isn't quite correct, but whatever.
		 */
		dfprintk(VFS, "NFS: faking root inode\n");

		fattr.fileid = 1;
		fattr.nlink = 2;	/* minimum for a dir */
		fattr.type = NFDIR;
		fattr.mode = S_IFDIR|S_IRUGO|S_IXUGO;
		fattr.size = 4096;
		fattr.du.nfs3.used = 1;
		fattr.valid = NFS_ATTR_FATTR|NFS_ATTR_FATTR_V3;
	} else if (error < 0) {
		printk(KERN_NOTICE "nfs_get_root: getattr error = %d\n", -error);
		*rooti = NULL;	/* superfluous ... but safe */
		return error;
	}

	*rooti = nfs_fhget(sb, rootfh, &fattr);
	if (error == -EACCES && authflavor > RPC_AUTH_MAXFLAVOR) {
		if (*rooti) {
			NFS_FLAGS(*rooti) |= NFS_INO_FAKE_ROOT;
			NFS_CACHEINV((*rooti));
			error = 0;
		}
	}
	return error;
}

/*
 * Do NFS version-independent mount processing, and sanity checking
 */
static int
nfs_sb_init(struct super_block *sb, rpc_authflavor_t authflavor)
{
	struct nfs_server	*server;
	struct inode		*root_inode = NULL;
	struct nfs_fattr	fattr;
	struct nfs_fsinfo	fsinfo = {
					.fattr = &fattr,
				};
	struct nfs_pathconf pathinfo = {
			.fattr = &fattr,
	};

	/* We probably want something more informative here */
	snprintf(sb->s_id, sizeof(sb->s_id), "%x:%x", MAJOR(sb->s_dev), MINOR(sb->s_dev));

	server = NFS_SB(sb);

	sb->s_magic      = NFS_SUPER_MAGIC;
	sb->s_op         = &nfs_sops;

	/* Did getting the root inode fail? */
	if (nfs_get_root(&root_inode, authflavor, sb, &server->fh) < 0)
		goto out_no_root;
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		goto out_no_root;

	sb->s_root->d_op = &nfs_dentry_operations;

	/* Get some general file system info */
        if (server->rpc_ops->fsinfo(server, &server->fh, &fsinfo) < 0) {
		printk(KERN_NOTICE "NFS: cannot retrieve file system info.\n");
		goto out_no_root;
        }
	if (server->namelen == 0 &&
	    server->rpc_ops->pathconf(server, &server->fh, &pathinfo) >= 0)
		server->namelen = pathinfo.max_namelen;
	/* Work out a lot of parameters */
	if (server->rsize == 0)
		server->rsize = nfs_block_size(fsinfo.rtpref, NULL);
	if (server->wsize == 0)
		server->wsize = nfs_block_size(fsinfo.wtpref, NULL);
	if (sb->s_blocksize == 0) {
		if (fsinfo.wtmult == 0) {
			sb->s_blocksize = 512;
			sb->s_blocksize_bits = 9;
		} else
			sb->s_blocksize = nfs_block_bits(fsinfo.wtmult,
							 &sb->s_blocksize_bits);
	}

	if (fsinfo.rtmax >= 512 && server->rsize > fsinfo.rtmax)
		server->rsize = nfs_block_size(fsinfo.rtmax, NULL);
	if (fsinfo.wtmax >= 512 && server->wsize > fsinfo.wtmax)
		server->wsize = nfs_block_size(fsinfo.wtmax, NULL);

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

	if (server->flags & NFS_MOUNT_NOAC) {
		server->acregmin = server->acregmax = 0;
		server->acdirmin = server->acdirmax = 0;
		sb->s_flags |= MS_SYNCHRONOUS;
	}
	server->backing_dev_info.ra_pages = server->rpages * NFS_MAX_READAHEAD;

	sb->s_maxbytes = fsinfo.maxfilesize;
	if (sb->s_maxbytes > MAX_LFS_FILESIZE) 
		sb->s_maxbytes = MAX_LFS_FILESIZE; 

	/* We're airborne Set socket buffersize */
	rpc_setbufsize(server->client, server->wsize + 100, server->rsize + 100);
	return 0;
	/* Yargs. It didn't work out. */
out_free_all:
	if (root_inode)
		iput(root_inode);
	return -EINVAL;
out_no_root:
	printk("nfs_read_super: get root inode failed\n");
	goto out_free_all;
}

/*
 * Create an RPC client handle.
 */
static struct rpc_clnt *
nfs_create_client(struct nfs_server *server, const struct nfs_mount_data *data)
{
	struct rpc_timeout	timeparms;
	struct rpc_xprt		*xprt = NULL;
	struct rpc_clnt		*clnt = NULL;
	int			tcp   = (data->flags & NFS_MOUNT_TCP);

	/* Initialize timeout values */
	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_maxval  = tcp ? RPC_MAX_TCP_TIMEOUT : RPC_MAX_UDP_TIMEOUT;
	timeparms.to_exponential = 1;

	if (!timeparms.to_initval)
		timeparms.to_initval = (tcp ? 600 : 11) * HZ / 10;
	if (!timeparms.to_retries)
		timeparms.to_retries = 5;

	/* create transport and client */
	xprt = xprt_create_proto(tcp ? IPPROTO_TCP : IPPROTO_UDP,
				 &server->addr, &timeparms);
	if (xprt == NULL) {
		printk(KERN_WARNING "NFS: cannot create RPC transport.\n");
		goto out_fail;
	}
	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
				 server->rpc_ops->version, data->pseudoflavor);
	if (clnt == NULL) {
		printk(KERN_WARNING "NFS: cannot create RPC client.\n");
		goto out_fail;
	}

	clnt->cl_intr     = (server->flags & NFS_MOUNT_INTR) ? 1 : 0;
	clnt->cl_softrtry = (server->flags & NFS_MOUNT_SOFT) ? 1 : 0;
	clnt->cl_droppriv = (server->flags & NFS_MOUNT_BROKEN_SUID) ? 1 : 0;
	clnt->cl_chatty   = 1;

	return clnt;

out_fail:
	if (xprt)
		xprt_destroy(xprt);
	return NULL;
}

/*
 * The way this works is that the mount process passes a structure
 * in the data argument which contains the server's IP address
 * and the root file handle obtained from the server's mount
 * daemon. We stash these away in the private superblock fields.
 */
static int
nfs_fill_super(struct super_block *sb, struct nfs_mount_data *data, int silent)
{
	struct nfs_server	*server;
	int			err = -EIO;
	rpc_authflavor_t	authflavor;

	server           = NFS_SB(sb);
	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	if (data->bsize)
		sb->s_blocksize = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags & NFS_MOUNT_FLAGMASK;

	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;

	server->namelen  = data->namlen;
	server->hostname = kmalloc(strlen(data->hostname) + 1, GFP_KERNEL);
	if (!server->hostname)
		goto out_fail;
	strcpy(server->hostname, data->hostname);

	/* Check NFS protocol revision and initialize RPC op vector
	 * and file handle pool. */
	if (server->flags & NFS_MOUNT_VER3) {
#ifdef CONFIG_NFS_V3
		server->rpc_ops = &nfs_v3_clientops;
		server->caps |= NFS_CAP_READDIRPLUS;
		if (data->version < 4) {
			printk(KERN_NOTICE "NFS: NFSv3 not supported by mount program.\n");
			goto out_fail;
		}
#else
		printk(KERN_NOTICE "NFS: NFSv3 not supported.\n");
		goto out_fail;
#endif
	} else {
		server->rpc_ops = &nfs_v2_clientops;
	}

	/* Fill in pseudoflavor for mount version < 5 */
	if (!(data->flags & NFS_MOUNT_SECFLAVOUR))
		data->pseudoflavor = RPC_AUTH_UNIX;
	authflavor = data->pseudoflavor;	/* save for sb_init() */
	/* XXX maybe we want to add a server->pseudoflavor field */

	/* Create RPC client handles */
	server->client = nfs_create_client(server, data);
	if (server->client == NULL)
		goto out_fail;
	data->pseudoflavor = RPC_AUTH_UNIX;	/* RFC 2623, sec 2.3.2 */
	server->client_sys = nfs_create_client(server, data);
	if (server->client_sys == NULL)
		goto out_shutdown;

	/* Fire up rpciod if not yet running */
	if (rpciod_up() != 0) {
		printk(KERN_WARNING "NFS: couldn't start rpciod!\n");
		goto out_shutdown;
	}

	err = nfs_sb_init(sb, authflavor);
	if (err != 0)
		goto out_noinit;

	if (server->flags & NFS_MOUNT_VER3) {
		if (server->namelen == 0 || server->namelen > NFS3_MAXNAMLEN)
			server->namelen = NFS3_MAXNAMLEN;
	} else {
		if (server->namelen == 0 || server->namelen > NFS2_MAXNAMLEN)
			server->namelen = NFS2_MAXNAMLEN;
	}

	/* Check whether to start the lockd process */
	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_up();
	return 0;
out_noinit:
	rpciod_down();
out_shutdown:
	if (server->client)
		rpc_shutdown_client(server->client);
	if (server->client_sys)
		rpc_shutdown_client(server->client_sys);
out_fail:
	if (server->hostname)
		kfree(server->hostname);
	return err;
}

static int
nfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct nfs_server *server = NFS_SB(sb);
	unsigned char blockbits;
	unsigned long blockres;
	struct nfs_fh *rootfh = NFS_FH(sb->s_root->d_inode);
	struct nfs_fattr fattr;
	struct nfs_fsstat res = {
			.fattr = &fattr,
	};
	int error;

	lock_kernel();

	error = server->rpc_ops->statfs(server, rootfh, &res);
	buf->f_type = NFS_SUPER_MAGIC;
	if (error < 0)
		goto out_err;

	buf->f_bsize = sb->s_blocksize;
	blockbits = sb->s_blocksize_bits;
	blockres = (1 << blockbits) - 1;
	buf->f_blocks = (res.tbytes + blockres) >> blockbits;
	buf->f_bfree = (res.fbytes + blockres) >> blockbits;
	buf->f_bavail = (res.abytes + blockres) >> blockbits;
	buf->f_files = res.tfiles;
	buf->f_ffree = res.afiles;

	buf->f_namelen = server->namelen;
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

	invalidate_remote_inode(inode);

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

/* Don't use READDIRPLUS on directories that we believe are too large */
#define NFS_LIMIT_READDIRPLUS (8*PAGE_SIZE)

/*
 * This is our front-end to iget that looks up inodes by file handle
 * instead of inode number.
 */
struct inode *
nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr)
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
		struct nfs_inode *nfsi = NFS_I(inode);

		/* We set i_ino for the few things that still rely on it,
		 * such as stat(2) */
		inode->i_ino = hash;

		/* We can't support update_atime(), since the server will reset it */
		inode->i_flags |= S_NOATIME;
		inode->i_mode = fattr->mode;
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_inode_operations.
		 */
		inode->i_op = &nfs_file_inode_operations;
		if (S_ISREG(inode->i_mode)) {
			inode->i_fop = &nfs_file_operations;
			inode->i_data.a_ops = &nfs_file_aops;
			inode->i_data.backing_dev_info = &NFS_SB(sb)->backing_dev_info;
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

		nfsi->read_cache_jiffies = fattr->timestamp;
		inode->i_atime = fattr->atime;
		inode->i_mtime = fattr->mtime;
		inode->i_ctime = fattr->ctime;
		nfsi->read_cache_ctime = fattr->ctime;
		nfsi->read_cache_mtime = fattr->mtime;
		nfsi->cache_mtime_jiffies = fattr->timestamp;
		nfsi->read_cache_isize = fattr->size;
		if (fattr->valid & NFS_ATTR_FATTR_V4)
			nfsi->change_attr = fattr->change_attr;
		inode->i_size = nfs_size_to_loff_t(fattr->size);
		inode->i_nlink = fattr->nlink;
		inode->i_uid = fattr->uid;
		inode->i_gid = fattr->gid;
		if (fattr->valid & (NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4)) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
			inode->i_blksize = inode->i_sb->s_blocksize;
		} else {
			inode->i_blocks = fattr->du.nfs2.blocks;
			inode->i_blksize = fattr->du.nfs2.blocksize;
		}
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		nfsi->cache_access.cred = NULL;

		unlock_new_inode(inode);
	} else
		nfs_refresh_inode(inode, fattr);
	dprintk("NFS: nfs_fhget(%s/%Ld ct=%d)\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		atomic_read(&inode->i_count));

out:
	return inode;

out_no_inode:
	printk("nfs_fhget: iget failed\n");
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

	if (!S_ISREG(inode->i_mode)) {
		attr->ia_valid &= ~ATTR_SIZE;
	} else {
		filemap_fdatawrite(inode->i_mapping);
		error = nfs_wb_all(inode);
		filemap_fdatawait(inode->i_mapping);
		if (error)
			goto out;
	}

	error = NFS_PROTO(inode)->setattr(dentry, &fattr, attr);
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
		struct nfs_inode *nfsi = NFS_I(inode);
		fattr.pre_size = nfsi->read_cache_isize;
		fattr.pre_mtime = nfsi->read_cache_mtime;
		fattr.pre_ctime = nfsi->read_cache_ctime;
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
void
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

	auth = NFS_CLIENT(inode)->cl_auth;
	cred = rpcauth_lookupcred(auth, 0);
	filp->private_data = cred;
	if (filp->f_mode & FMODE_WRITE)
		nfs_set_mmcred(inode, cred);
	return 0;
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
	if (NFS_FAKE_ROOT(inode)) {
		dfprintk(VFS, "NFS: not revalidating fake root\n");
		status = 0;
		goto out_nowait;
	}

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
	struct nfs_inode *nfsi = NFS_I(inode);
	long cdif;

	if (time_after(jiffies, nfsi->read_cache_jiffies + nfsi->attrtimeo))
		goto out_valid;
	cdif = fattr->ctime.tv_sec - nfsi->read_cache_ctime.tv_sec;
	if (cdif == 0)
		cdif = fattr->ctime.tv_nsec - nfsi->read_cache_ctime.tv_nsec;
	if (cdif > 0)
		goto out_valid;
	/* Ugh... */
	if (cdif == 0 && fattr->size > nfsi->read_cache_isize)
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
	struct nfs_inode *nfsi = NFS_I(inode);
	__u64		new_size;
	loff_t		new_isize;
	int		invalid = 0;
	int		mtime_update = 0;
	loff_t		cur_isize;

	dfprintk(VFS, "NFS: refresh_inode(%s/%ld ct=%d info=0x%x)\n",
			inode->i_sb->s_id, inode->i_ino,
			atomic_read(&inode->i_count), fattr->valid);

	/* First successful call after mount, fill real data. */
	if (NFS_FAKE_ROOT(inode)) {
		dfprintk(VFS, "NFS: updating fake root\n");
		nfsi->fileid = fattr->fileid;
		NFS_FLAGS(inode) &= ~NFS_INO_FAKE_ROOT;
	}

	if (nfsi->fileid != fattr->fileid) {
		printk(KERN_ERR "nfs_refresh_inode: inode number mismatch\n"
		       "expected (%s/0x%Lx), got (%s/0x%Lx)\n",
		       inode->i_sb->s_id, (long long)nfsi->fileid,
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

	new_size = fattr->size;
 	new_isize = nfs_size_to_loff_t(fattr->size);

	/* Avoid races */
	if (nfs_fattr_obsolete(inode, fattr))
		goto out_nochange;

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	nfsi->read_cache_jiffies = fattr->timestamp;

	/*
	 * Note: NFS_CACHE_ISIZE(inode) reflects the state of the cache.
	 *       NOT inode->i_size!!!
	 */
	if (nfsi->read_cache_isize != new_size) {
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
	if (!timespec_equal(&nfsi->read_cache_mtime, &fattr->mtime)) {
#ifdef NFS_DEBUG_VERBOSE
		printk(KERN_DEBUG "NFS: mtime change on %s/%ld\n", inode->i_sb->s_id, inode->i_ino);
#endif
		invalid = 1;
		mtime_update = 1;
	}

	if ((fattr->valid & NFS_ATTR_FATTR_V4)
	    && nfsi->change_attr != fattr->change_attr) {
#ifdef NFS_DEBUG_VERBOSE
		printk(KERN_DEBUG "NFS: change_attr change on %s/%ld\n",
		       inode->i_sb->s_id, inode->i_ino);
#endif
		invalid = 1;
	}

	/* Check Weak Cache Consistency data.
	 * If size and mtime match the pre-operation values, we can
	 * assume that any attribute changes were caused by our NFS
         * operation, so there's no need to invalidate the caches.
         */
	if ((fattr->valid & NFS_ATTR_PRE_CHANGE)
	    && nfsi->change_attr == fattr->pre_change_attr) {
		invalid = 0;
	}
	else if ((fattr->valid & NFS_ATTR_WCC)
	    && nfsi->read_cache_isize == fattr->pre_size
	    && timespec_equal(&nfsi->read_cache_mtime, &fattr->pre_mtime)) {
		invalid = 0;
	}

	/*
	 * If we have pending writebacks, things can get
	 * messy.
	 */
	cur_isize = i_size_read(inode);
	if (nfs_have_writebacks(inode) && new_isize < cur_isize)
		new_isize = cur_isize;

	nfsi->read_cache_ctime = fattr->ctime;
	inode->i_ctime = fattr->ctime;
	inode->i_atime = fattr->atime;

	if (mtime_update) {
		if (invalid)
			nfsi->cache_mtime_jiffies = fattr->timestamp;
		nfsi->read_cache_mtime = fattr->mtime;
		inode->i_mtime = fattr->mtime;
	}

	nfsi->read_cache_isize = new_size;
	i_size_write(inode, new_isize);

	if (inode->i_mode != fattr->mode ||
	    inode->i_uid != fattr->uid ||
	    inode->i_gid != fattr->gid) {
		struct rpc_cred **cred = &NFS_I(inode)->cache_access.cred;
		if (*cred) {
			put_rpccred(*cred);
			*cred = NULL;
		}
	}

	if (fattr->valid & NFS_ATTR_FATTR_V4)
		nfsi->change_attr = fattr->change_attr;

	inode->i_mode = fattr->mode;
	inode->i_nlink = fattr->nlink;
	inode->i_uid = fattr->uid;
	inode->i_gid = fattr->gid;

	if (fattr->valid & (NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4)) {
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
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
		invalidate_remote_inode(inode);
		memset(NFS_COOKIEVERF(inode), 0, sizeof(NFS_COOKIEVERF(inode)));
	} else if (time_after(jiffies, nfsi->attrtimeo_timestamp+nfsi->attrtimeo)) {
		if ((nfsi->attrtimeo <<= 1) > NFS_MAXATTRTIMEO(inode))
			nfsi->attrtimeo = NFS_MAXATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
	}

	return 0;
 out_nochange:
	if (!timespec_equal(&fattr->atime, &inode->i_atime))
		inode->i_atime = fattr->atime;
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
	s->s_fs_info = data;
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
	int flags, const char *dev_name, void *raw_data)
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
		if (data->version < 5)
			data->flags &= ~NFS_MOUNT_SECFLAVOUR;
	}

	if (root->size > sizeof(root->data)) {
		printk("nfs_get_sb: invalid root filehandle\n");
		kfree(server);
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
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT,
};

#ifdef CONFIG_NFS_V4

static int nfs4_fill_super(struct super_block *sb, struct nfs4_mount_data *data, int silent)
{
	struct nfs_server *server;
	struct rpc_xprt *xprt = NULL;
	struct rpc_clnt *clnt = NULL;
	struct rpc_timeout timeparms;
	rpc_authflavor_t authflavour;
	int proto, err = -EIO;

	sb->s_blocksize_bits = 0;
	sb->s_blocksize = 0;
	server = NFS_SB(sb);
	if (data->rsize != 0)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize != 0)
		server->wsize = nfs_block_size(data->wsize, NULL);
	server->flags = data->flags & NFS_MOUNT_FLAGMASK;

	/* NFSv4 doesn't use NLM locking */
	server->flags |= NFS_MOUNT_NONLM;

	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;

	server->rpc_ops = &nfs_v4_clientops;
	/* Initialize timeout values */

	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_exponential = 1;
	if (!timeparms.to_retries)
		timeparms.to_retries = 5;

	proto = data->proto;
	/* Which IP protocol do we use? */
	switch (proto) {
	case IPPROTO_TCP:
		timeparms.to_maxval  = RPC_MAX_TCP_TIMEOUT;
		if (!timeparms.to_initval)
			timeparms.to_initval = 600 * HZ / 10;
		break;
	case IPPROTO_UDP:
		timeparms.to_maxval  = RPC_MAX_UDP_TIMEOUT;
		if (!timeparms.to_initval)
			timeparms.to_initval = 11 * HZ / 10;
		break;
	default:
		return -EINVAL;
	}

	/* Now create transport and client */
	xprt = xprt_create_proto(proto, &server->addr, &timeparms);
	if (xprt == NULL) {
		printk(KERN_WARNING "NFS: cannot create RPC transport.\n");
		goto out_fail;
	}

	authflavour = RPC_AUTH_UNIX;
	if (data->auth_flavourlen != 0) {
		if (data->auth_flavourlen > 1)
			printk(KERN_INFO "NFS: cannot yet deal with multiple auth flavours.\n");
		if (copy_from_user(&authflavour, data->auth_flavours, sizeof(authflavour))) {
			err = -EFAULT;
			goto out_fail;
		}
	}
	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
				 server->rpc_ops->version, authflavour);
	if (clnt == NULL) {
		printk(KERN_WARNING "NFS: cannot create RPC client.\n");
		xprt_destroy(xprt);
		goto out_fail;
	}

	clnt->cl_intr     = (server->flags & NFS4_MOUNT_INTR) ? 1 : 0;
	clnt->cl_softrtry = (server->flags & NFS4_MOUNT_SOFT) ? 1 : 0;
	clnt->cl_chatty   = 1;
	server->client    = clnt;

	/* Fire up rpciod if not yet running */
	if (rpciod_up() != 0) {
		printk(KERN_WARNING "NFS: couldn't start rpciod!\n");
		goto out_shutdown;
	}

	if (create_nfsv4_state(server, data))
		goto out_shutdown;

	if ((server->idmap = nfs_idmap_new(server)) == NULL)
		printk(KERN_WARNING "NFS: couldn't start IDmap\n");

	err = nfs_sb_init(sb, authflavour);
	if (err == 0)
		return 0;
	rpciod_down();
	destroy_nfsv4_state(server);
	if (server->idmap != NULL)
		nfs_idmap_delete(server);
out_shutdown:
	rpc_shutdown_client(server->client);
out_fail:
	return err;
}

static int nfs4_compare_super(struct super_block *sb, void *data)
{
	struct nfs_server *server = data;
	struct nfs_server *old = NFS_SB(sb);

	if (strcmp(server->hostname, old->hostname) != 0)
		return 0;
	if (strcmp(server->mnt_path, old->mnt_path) != 0)
		return 0;
	return 1;
}

static void *
nfs_copy_user_string(char *dst, struct nfs_string *src, int maxlen)
{
	void *p = NULL;

	if (!src->len)
		return ERR_PTR(-EINVAL);
	if (src->len < maxlen)
		maxlen = src->len;
	if (dst == NULL) {
		p = dst = kmalloc(maxlen + 1, GFP_KERNEL);
		if (p == NULL)
			return ERR_PTR(-ENOMEM);
	}
	if (copy_from_user(dst, src->data, maxlen)) {
		if (p != NULL)
			kfree(p);
		return ERR_PTR(-EFAULT);
	}
	dst[maxlen] = '\0';
	return dst;
}

static struct super_block *nfs4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data)
{
	int error;
	struct nfs_server *server;
	struct super_block *s;
	struct nfs4_mount_data *data = raw_data;
	void *p;

	if (!data) {
		printk("nfs_read_super: missing data argument\n");
		return ERR_PTR(-EINVAL);
	}

	server = kmalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return ERR_PTR(-ENOMEM);
	memset(server, 0, sizeof(struct nfs_server));

	if (data->version != NFS4_MOUNT_VERSION) {
		printk("nfs warning: mount version %s than kernel\n",
			data->version < NFS_MOUNT_VERSION ? "older" : "newer");
	}

	p = nfs_copy_user_string(NULL, &data->hostname, 256);
	if (IS_ERR(p))
		goto out_err;
	server->hostname = p;

	p = nfs_copy_user_string(NULL, &data->mnt_path, 1024);
	if (IS_ERR(p))
		goto out_err;
	server->mnt_path = p;

	p = nfs_copy_user_string(server->ip_addr, &data->client_addr,
			sizeof(server->ip_addr) - 1);
	if (IS_ERR(p))
		goto out_err;

	/* We now require that the mount process passes the remote address */
	if (data->host_addrlen != sizeof(server->addr)) {
		s = ERR_PTR(-EINVAL);
		goto out_free;
	}
	if (copy_from_user(&server->addr, data->host_addr, sizeof(server->addr))) {
		s = ERR_PTR(-EFAULT);
		goto out_free;
	}
	if (server->addr.sin_family != AF_INET ||
	    server->addr.sin_addr.s_addr == INADDR_ANY) {
		printk("NFS: mount program didn't pass remote IP address!\n");
		s = ERR_PTR(-EINVAL);
		goto out_free;
	}

	s = sget(fs_type, nfs4_compare_super, nfs_set_super, server);

	if (IS_ERR(s) || s->s_root)
		goto out_free;

	s->s_flags = flags;

	error = nfs4_fill_super(s, data, flags & MS_VERBOSE ? 1 : 0);
	if (error) {
		up_write(&s->s_umount);
		deactivate_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= MS_ACTIVE;
	return s;
out_err:
	s = (struct super_block *)p;
out_free:
	if (server->mnt_path)
		kfree(server->mnt_path);
	if (server->hostname)
		kfree(server->hostname);
	kfree(server);
	return s;
}

static struct file_system_type nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.get_sb		= nfs4_get_sb,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_ODD_RENAME|FS_REVAL_DOT,
};

#define nfs4_zero_state(nfsi) \
	do { \
		INIT_LIST_HEAD(&(nfsi)->open_states); \
	} while(0)
#define register_nfs4fs() register_filesystem(&nfs4_fs_type)
#define unregister_nfs4fs() unregister_filesystem(&nfs4_fs_type)
#else
#define nfs4_zero_state(nfsi) \
	do { } while (0)
#define register_nfs4fs() (0)
#define unregister_nfs4fs()
#endif

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
	nfs4_zero_state(nfsi);
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
		INIT_LIST_HEAD(&nfsi->dirty);
		INIT_LIST_HEAD(&nfsi->commit);
		INIT_RADIX_TREE(&nfsi->nfs_page_tree, GFP_ATOMIC);
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
					     0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
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
	if ((err = register_nfs4fs()) != 0)
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
	unregister_nfs4fs();
}

/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
