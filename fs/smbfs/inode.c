/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include <linux/smp_lock.h>
#include <linux/nls.h>

#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/smb_mount.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "smb_debug.h"
#include "getopt.h"

/* Always pick a default string */
#ifdef CONFIG_SMB_NLS_REMOTE
#define SMB_NLS_REMOTE CONFIG_SMB_NLS_REMOTE
#else
#define SMB_NLS_REMOTE ""
#endif

static void smb_delete_inode(struct inode *);
static void smb_put_super(struct super_block *);
static int  smb_statfs(struct super_block *, struct statfs *);
static void smb_set_inode_attr(struct inode *, struct smb_fattr *);

static struct super_operations smb_sops =
{
	put_inode:	force_delete,
	delete_inode:	smb_delete_inode,
	put_super:	smb_put_super,
	statfs:		smb_statfs,
};


/* We are always generating a new inode here */
struct inode *
smb_iget(struct super_block *sb, struct smb_fattr *fattr)
{
	struct inode *result;

	DEBUG1("smb_iget: %p\n", fattr);

	result = new_inode(sb);
	if (!result)
		return result;
	result->i_ino = fattr->f_ino;
	memset(&(result->u.smbfs_i), 0, sizeof(result->u.smbfs_i));
	smb_set_inode_attr(result, fattr);
	if (S_ISREG(result->i_mode)) {
		result->i_op = &smb_file_inode_operations;
		result->i_fop = &smb_file_operations;
		result->i_data.a_ops = &smb_file_aops;
	} else if (S_ISDIR(result->i_mode)) {
		result->i_op = &smb_dir_inode_operations;
		result->i_fop = &smb_dir_operations;
	}
	insert_inode_hash(result);
	return result;
}

/*
 * Copy the inode data to a smb_fattr structure.
 */
void
smb_get_inode_attr(struct inode *inode, struct smb_fattr *fattr)
{
	memset(fattr, 0, sizeof(struct smb_fattr));
	fattr->f_mode	= inode->i_mode;
	fattr->f_nlink	= inode->i_nlink;
	fattr->f_ino	= inode->i_ino;
	fattr->f_uid	= inode->i_uid;
	fattr->f_gid	= inode->i_gid;
	fattr->f_rdev	= inode->i_rdev;
	fattr->f_size	= inode->i_size;
	fattr->f_mtime	= inode->i_mtime;
	fattr->f_ctime	= inode->i_ctime;
	fattr->f_atime	= inode->i_atime;
	fattr->f_blksize= inode->i_blksize;
	fattr->f_blocks	= inode->i_blocks;

	fattr->attr	= inode->u.smbfs_i.attr;
	/*
	 * Keep the attributes in sync with the inode permissions.
	 */
	if (fattr->f_mode & S_IWUSR)
		fattr->attr &= ~aRONLY;
	else
		fattr->attr |= aRONLY;
}

static void
smb_set_inode_attr(struct inode *inode, struct smb_fattr *fattr)
{
	inode->i_mode	= fattr->f_mode;
	inode->i_nlink	= fattr->f_nlink;
	inode->i_uid	= fattr->f_uid;
	inode->i_gid	= fattr->f_gid;
	inode->i_rdev	= fattr->f_rdev;
	inode->i_ctime	= fattr->f_ctime;
	inode->i_blksize= fattr->f_blksize;
	inode->i_blocks = fattr->f_blocks;
	/*
	 * Don't change the size and mtime/atime fields
	 * if we're writing to the file.
	 */
	if (!(inode->u.smbfs_i.cache_valid & SMB_F_LOCALWRITE))
	{
		inode->i_size  = fattr->f_size;
		inode->i_mtime = fattr->f_mtime;
		inode->i_atime = fattr->f_atime;
	}

	inode->u.smbfs_i.attr = fattr->attr;
	/*
	 * Update the "last time refreshed" field for revalidation.
	 */
	inode->u.smbfs_i.oldmtime = jiffies;
}

/*
 * This is called if the connection has gone bad ...
 * try to kill off all the current inodes.
 */
void
smb_invalidate_inodes(struct smb_sb_info *server)
{
	VERBOSE("\n");
	shrink_dcache_sb(SB_of(server));
	invalidate_inodes(SB_of(server));
}

/*
 * This is called to update the inode attributes after
 * we've made changes to a file or directory.
 */
static int
smb_refresh_inode(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct smb_fattr fattr;

	error = smb_proc_getattr(dentry, &fattr);
	if (!error)
	{
		smb_renew_times(dentry);
		/*
		 * Check whether the type part of the mode changed,
		 * and don't update the attributes if it did.
		 */
		if ((inode->i_mode & S_IFMT) == (fattr.f_mode & S_IFMT))
			smb_set_inode_attr(inode, &fattr);
		else
		{
			/*
			 * Big trouble! The inode has become a new object,
			 * so any operations attempted on it are invalid.
			 *
			 * To limit damage, mark the inode as bad so that
			 * subsequent lookup validations will fail.
			 */
			PARANOIA("%s/%s changed mode, %07o to %07o\n",
				 DENTRY_PATH(dentry),
				 inode->i_mode, fattr.f_mode);

			fattr.f_mode = inode->i_mode; /* save mode */
			make_bad_inode(inode);
			inode->i_mode = fattr.f_mode; /* restore mode */
			/*
			 * No need to worry about unhashing the dentry: the
			 * lookup validation will see that the inode is bad.
			 * But we do want to invalidate the caches ...
			 */
			if (!S_ISDIR(inode->i_mode))
				invalidate_inode_pages(inode);
			else
				smb_invalid_dir_cache(inode);
			error = -EIO;
		}
	}
	return error;
}

/*
 * This is called when we want to check whether the inode
 * has changed on the server.  If it has changed, we must
 * invalidate our local caches.
 */
int
smb_revalidate_inode(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	time_t last_time;
	int error = 0;

	DEBUG1("smb_revalidate_inode\n");
	/*
	 * If this is a file opened with write permissions,
	 * the inode will be up-to-date.
	 */
	lock_kernel();
	if (S_ISREG(inode->i_mode) && smb_is_open(inode))
	{
		if (inode->u.smbfs_i.access != SMB_O_RDONLY)
			goto out;
	}

	/*
	 * Check whether we've recently refreshed the inode.
	 */
	if (time_before(jiffies, inode->u.smbfs_i.oldmtime + HZ/10))
	{
		VERBOSE("up-to-date, jiffies=%lu, oldtime=%lu\n",
			jiffies, inode->u.smbfs_i.oldmtime);
		goto out;
	}

	/*
	 * Save the last modified time, then refresh the inode.
	 * (Note: a size change should have a different mtime.)
	 */
	last_time = inode->i_mtime;
	error = smb_refresh_inode(dentry);
	if (error || inode->i_mtime != last_time)
	{
		VERBOSE("%s/%s changed, old=%ld, new=%ld\n",
			DENTRY_PATH(dentry),
			(long) last_time, (long) inode->i_mtime);

		if (!S_ISDIR(inode->i_mode))
			invalidate_inode_pages(inode);
		else
			smb_invalid_dir_cache(inode);
	}
out:
	unlock_kernel();
	return error;
}

/*
 * This routine is called when i_nlink == 0 and i_count goes to 0.
 * All blocking cleanup operations need to go here to avoid races.
 */
static void
smb_delete_inode(struct inode *ino)
{
	DEBUG1("ino=%ld\n", ino->i_ino);
	lock_kernel();
	if (smb_close(ino))
		PARANOIA("could not close inode %ld\n", ino->i_ino);
	unlock_kernel();
	clear_inode(ino);
}

/* FIXME: flags and has_arg could probably be merged. */
struct option opts[] = {
	{ "version",	1, 0, 'v' },
	{ "win95",	0, SMB_MOUNT_WIN95, 1 },
	{ "oldattr",	0, SMB_MOUNT_OLDATTR, 1 },
	{ "dirattr",	0, SMB_MOUNT_DIRATTR, 1 },
	{ "case",	0, SMB_MOUNT_CASE, 1 },
	{ "uid",	1, 0, 'u' },
	{ "gid",	1, 0, 'g' },
	{ "file_mode",	1, 0, 'f' },
	{ "dir_mode",	1, 0, 'd' },
	{ "iocharset",	1, 0, 'i' },
	{ "codepage",	1, 0, 'c' },
	{ NULL,		0, 0, 0}
};

static int
parse_options(struct smb_mount_data_kernel *mnt, char *options)
{
	int c;
	unsigned long flags;
	unsigned long value;
	char *optarg;
	char *optopt;

	flags = 0;
	while ( (c = smb_getopt("smbfs", &options, opts,
				&optopt, &optarg, &flags, &value)) > 0) {

		VERBOSE("'%s' -> '%s'\n", optopt, optarg ? optarg : "<none>");

		switch (c) {
		case 1:
			/* got a "flag" option */
			break;
		case 'v':
			if (value != SMB_MOUNT_VERSION) {
			printk ("smbfs: Bad mount version %ld, expected %d\n",
				value, SMB_MOUNT_VERSION);
				return 0;
			}
			mnt->version = value;
			break;
		case 'u':
			mnt->uid = value;
			break;
		case 'g':
			mnt->gid = value;
			break;
		case 'f':
			mnt->file_mode = value & (S_IRWXU | S_IRWXG | S_IRWXO);
			mnt->file_mode |= S_IFREG;
			break;
		case 'd':
			mnt->dir_mode = value & (S_IRWXU | S_IRWXG | S_IRWXO);
			mnt->dir_mode |= S_IFDIR;
			break;
		case 'i':
			strncpy(mnt->codepage.local_name, optarg, 
				SMB_NLS_MAXNAMELEN);
			break;
		case 'c':
			strncpy(mnt->codepage.remote_name, optarg,
				SMB_NLS_MAXNAMELEN);
			break;
		default:
			printk ("smbfs: Unrecognized mount option %s\n",
				optopt);
			return -1;
		}
	}
	mnt->flags = flags;
	return c;
}


static void
smb_put_super(struct super_block *sb)
{
	struct smb_sb_info *server = &(sb->u.smbfs_sb);

	if (server->sock_file) {
		smb_proc_disconnect(server);
		smb_dont_catch_keepalive(server);
		fput(server->sock_file);
	}

	if (server->conn_pid)
	       kill_proc(server->conn_pid, SIGTERM, 1);

	kfree(server->mnt);
	kfree(sb->u.smbfs_sb.temp_buf);
	if (server->packet)
		smb_vfree(server->packet);

	if(sb->u.smbfs_sb.remote_nls) {
		unload_nls(sb->u.smbfs_sb.remote_nls);
		sb->u.smbfs_sb.remote_nls = NULL;
	}
	if(sb->u.smbfs_sb.local_nls) {
		unload_nls(sb->u.smbfs_sb.local_nls);
		sb->u.smbfs_sb.local_nls = NULL;
	}
}

struct super_block *
smb_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct smb_mount_data_kernel *mnt;
	struct smb_mount_data *oldmnt;
	struct inode *root_inode;
	struct smb_fattr root;
	int ver;

	if (!raw_data)
		goto out_no_data;

	oldmnt = (struct smb_mount_data *) raw_data;
	ver = oldmnt->version;
	if (ver != SMB_MOUNT_OLDVERSION && cpu_to_be32(ver) != SMB_MOUNT_ASCII)
		goto out_wrong_data;

	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = SMB_SUPER_MAGIC;
	sb->s_flags = 0;
	sb->s_op = &smb_sops;

	sb->u.smbfs_sb.mnt = NULL;
	sb->u.smbfs_sb.sock_file = NULL;
	init_MUTEX(&sb->u.smbfs_sb.sem);
	init_waitqueue_head(&sb->u.smbfs_sb.wait);
	sb->u.smbfs_sb.conn_pid = 0;
	sb->u.smbfs_sb.state = CONN_INVALID; /* no connection yet */
	sb->u.smbfs_sb.generation = 0;
	sb->u.smbfs_sb.packet_size = smb_round_length(SMB_INITIAL_PACKET_SIZE);	
	sb->u.smbfs_sb.packet = smb_vmalloc(sb->u.smbfs_sb.packet_size);
	if (!sb->u.smbfs_sb.packet)
		goto out_no_mem;

	/* Allocate the global temp buffer */
	sb->u.smbfs_sb.temp_buf = kmalloc(2*SMB_MAXPATHLEN + 20, GFP_KERNEL);
	if (!sb->u.smbfs_sb.temp_buf)
		goto out_no_temp;

	/* Setup NLS stuff */
	sb->u.smbfs_sb.remote_nls = NULL;
	sb->u.smbfs_sb.local_nls = NULL;
	sb->u.smbfs_sb.name_buf = sb->u.smbfs_sb.temp_buf + SMB_MAXPATHLEN + 20;

	/* Allocate the mount data structure */
	/* FIXME: merge this with the other malloc and get a whole page? */
	mnt = kmalloc(sizeof(struct smb_mount_data_kernel), GFP_KERNEL);
	if (!mnt)
		goto out_no_mount;
	sb->u.smbfs_sb.mnt = mnt;

	memset(mnt, 0, sizeof(struct smb_mount_data_kernel));
	strncpy(mnt->codepage.local_name, CONFIG_NLS_DEFAULT,
		SMB_NLS_MAXNAMELEN);
	strncpy(mnt->codepage.remote_name, SMB_NLS_REMOTE,
		SMB_NLS_MAXNAMELEN);

	if (ver == SMB_MOUNT_OLDVERSION) {
		mnt->version = oldmnt->version;

		/* FIXME: is this enough to convert uid/gid's ? */
		mnt->mounted_uid = oldmnt->mounted_uid;
		mnt->uid = oldmnt->uid;
		mnt->gid = oldmnt->gid;

		mnt->file_mode =
			oldmnt->file_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
		mnt->dir_mode =
			oldmnt->dir_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
		mnt->file_mode |= S_IFREG;
		mnt->dir_mode  |= S_IFDIR;

		mnt->flags = (oldmnt->file_mode >> 9);
	} else {
		if (parse_options(mnt, raw_data))
			goto out_bad_option;

		mnt->mounted_uid = current->uid;
	}
	smb_setcodepage(&sb->u.smbfs_sb, &mnt->codepage);
	if (!sb->u.smbfs_sb.convert)
		PARANOIA("convert funcptr was NULL!\n");

	/*
	 * Display the enabled options
	 * Note: smb_proc_getattr uses these in 2.4 (but was changed in 2.2)
	 */
	if (mnt->flags & SMB_MOUNT_OLDATTR)
		printk("SMBFS: Using core getattr (Win 95 speedup)\n");
	else if (mnt->flags & SMB_MOUNT_DIRATTR)
		printk("SMBFS: Using dir ff getattr\n");

	/*
	 * Keep the super block locked while we get the root inode.
	 */
	smb_init_root_dirent(&(sb->u.smbfs_sb), &root);
	root_inode = smb_iget(sb, &root);
	if (!root_inode)
		goto out_no_root;

	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		goto out_no_root;

	return sb;

out_no_root:
	iput(root_inode);
out_bad_option:
	kfree(sb->u.smbfs_sb.mnt);
out_no_mount:
	kfree(sb->u.smbfs_sb.temp_buf);
out_no_temp:
	smb_vfree(sb->u.smbfs_sb.packet);
out_no_mem:
	if (!sb->u.smbfs_sb.mnt)
		printk(KERN_ERR "smb_read_super: allocation failure\n");
	goto out_fail;
out_wrong_data:
	printk(KERN_ERR "smbfs: mount_data version %d is not supported\n", ver);
	goto out_fail;
out_no_data:
	printk(KERN_ERR "smb_read_super: missing data argument\n");
out_fail:
	return NULL;
}

static int
smb_statfs(struct super_block *sb, struct statfs *buf)
{
	smb_proc_dskattr(sb, buf);

	buf->f_type = SMB_SUPER_MAGIC;
	buf->f_namelen = SMB_MAXPATHLEN;
	return 0;
}

int
smb_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct smb_sb_info *server = server_from_dentry(dentry);
	unsigned int mask = (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO);
	int error, changed, refresh = 0;
	struct smb_fattr fattr;

	error = smb_revalidate_inode(dentry);
	if (error)
		goto out;

	if ((error = inode_change_ok(inode, attr)) < 0)
		goto out;

	error = -EPERM;
	if ((attr->ia_valid & ATTR_UID) && (attr->ia_uid != server->mnt->uid))
		goto out;

	if ((attr->ia_valid & ATTR_GID) && (attr->ia_uid != server->mnt->gid))
		goto out;

	if ((attr->ia_valid & ATTR_MODE) && (attr->ia_mode & ~mask))
		goto out;

	if ((attr->ia_valid & ATTR_SIZE) != 0)
	{
		VERBOSE("changing %s/%s, old size=%ld, new size=%ld\n",
			DENTRY_PATH(dentry),
			(long) inode->i_size, (long) attr->ia_size);
		error = smb_open(dentry, O_WRONLY);
		if (error)
			goto out;
		error = smb_proc_trunc(server, inode->u.smbfs_i.fileid,
					 attr->ia_size);
		if (error)
			goto out;
		vmtruncate(inode, attr->ia_size);
		refresh = 1;
	}

	/*
	 * Initialize the fattr and check for changed fields.
	 * Note: CTIME under SMB is creation time rather than
	 * change time, so we don't attempt to change it.
	 */
	smb_get_inode_attr(inode, &fattr);

	changed = 0;
	if ((attr->ia_valid & ATTR_MTIME) != 0)
	{
		fattr.f_mtime = attr->ia_mtime;
		changed = 1;
	}
	if ((attr->ia_valid & ATTR_ATIME) != 0)
	{
		fattr.f_atime = attr->ia_atime;
		/* Earlier protocols don't have an access time */
		if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
			changed = 1;
	}
	if (changed)
	{
		error = smb_proc_settime(dentry, &fattr);
		if (error)
			goto out;
		refresh = 1;
	}

	/*
	 * Check for mode changes ... we're extremely limited in
	 * what can be set for SMB servers: just the read-only bit.
	 */
	if ((attr->ia_valid & ATTR_MODE) != 0)
	{
		VERBOSE("%s/%s mode change, old=%x, new=%x\n",
			DENTRY_PATH(dentry), fattr.f_mode, attr->ia_mode);
		changed = 0;
		if (attr->ia_mode & S_IWUSR)
		{
			if (fattr.attr & aRONLY)
			{
				fattr.attr &= ~aRONLY;
				changed = 1;
			}
		} else {
			if (!(fattr.attr & aRONLY))
			{
				fattr.attr |= aRONLY;
				changed = 1;
			}
		}
		if (changed)
		{
			error = smb_proc_setattr(dentry, &fattr);
			if (error)
				goto out;
			refresh = 1;
		}
	}
	error = 0;

out:
	if (refresh)
		smb_refresh_inode(dentry);
	return error;
}

#ifdef DEBUG_SMB_MALLOC
int smb_malloced;
int smb_current_kmalloced;
int smb_current_vmalloced;
#endif

static DECLARE_FSTYPE( smb_fs_type, "smbfs", smb_read_super, 0);

static int __init init_smb_fs(void)
{
	DEBUG1("registering ...\n");

#ifdef DEBUG_SMB_MALLOC
	smb_malloced = 0;
	smb_current_kmalloced = 0;
	smb_current_vmalloced = 0;
#endif

	return register_filesystem(&smb_fs_type);
}

static void __exit exit_smb_fs(void)
{
	DEBUG1("unregistering ...\n");
	unregister_filesystem(&smb_fs_type);
#ifdef DEBUG_SMB_MALLOC
	printk(KERN_DEBUG "smb_malloced: %d\n", smb_malloced);
	printk(KERN_DEBUG "smb_current_kmalloced: %d\n",smb_current_kmalloced);
	printk(KERN_DEBUG "smb_current_vmalloced: %d\n",smb_current_vmalloced);
#endif
}

EXPORT_NO_SYMBOLS;

module_init(init_smb_fs)
module_exit(exit_smb_fs)
