/*
 *  smb_fs.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_FS_H
#define _LINUX_SMB_FS_H

#include <linux/smb.h>

/*
 * ioctl commands
 */
#define	SMB_IOC_GETMOUNTUID		_IOR('u', 1, __kernel_old_uid_t)
#define SMB_IOC_NEWCONN                 _IOW('u', 2, struct smb_conn_opt)

/* __kernel_uid_t can never change, so we have to use __kernel_uid32_t */
#define	SMB_IOC_GETMOUNTUID32		_IOR('u', 3, __kernel_uid32_t)


#ifdef __KERNEL__

#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/smb_mount.h>
#include <asm/unaligned.h>


/* macro names are short for word, double-word, long value (?) */
#define WVAL(buf,pos) \
	(le16_to_cpu(get_unaligned((u16 *)((u8 *)(buf) + (pos)))))
#define DVAL(buf,pos) \
	(le32_to_cpu(get_unaligned((u32 *)((u8 *)(buf) + (pos)))))
#define LVAL(buf,pos) \
	(le64_to_cpu(get_unaligned((u64 *)((u8 *)(buf) + (pos)))))
#define WSET(buf,pos,val) \
	put_unaligned(cpu_to_le16((u16)(val)), (u16 *)((u8 *)(buf) + (pos)))
#define DSET(buf,pos,val) \
	put_unaligned(cpu_to_le32((u32)(val)), (u32 *)((u8 *)(buf) + (pos)))
#define LSET(buf,pos,val) \
	put_unaligned(cpu_to_le64((u64)(val)), (u64 *)((u8 *)(buf) + (pos)))

/* where to find the base of the SMB packet proper */
#define smb_base(buf) ((u8 *)(((u8 *)(buf))+4))

#ifdef DEBUG_SMB_MALLOC

extern int smb_malloced;
extern int smb_current_vmalloced;

static inline void *
smb_vmalloc(unsigned int size)
{
        smb_malloced += 1;
        smb_current_vmalloced += 1;
        return vmalloc(size);
}

static inline void
smb_vfree(void *obj)
{
        smb_current_vmalloced -= 1;
        vfree(obj);
}

#else /* DEBUG_SMB_MALLOC */

#define smb_kmalloc(s,p) kmalloc(s,p)
#define smb_kfree_s(o,s) kfree(o)
#define smb_vmalloc(s)   vmalloc(s)
#define smb_vfree(o)     vfree(o)

#endif /* DEBUG_SMB_MALLOC */

/*
 * Flags for the in-memory inode
 */
#define SMB_F_LOCALWRITE	0x02	/* file modified locally */


/* NT1 protocol capability bits */
#define SMB_CAP_RAW_MODE         0x0001
#define SMB_CAP_MPX_MODE         0x0002
#define SMB_CAP_UNICODE          0x0004
#define SMB_CAP_LARGE_FILES      0x0008
#define SMB_CAP_NT_SMBS          0x0010
#define SMB_CAP_RPC_REMOTE_APIS  0x0020
#define SMB_CAP_STATUS32         0x0040
#define SMB_CAP_LEVEL_II_OPLOCKS 0x0080
#define SMB_CAP_LOCK_AND_READ    0x0100
#define SMB_CAP_NT_FIND          0x0200
#define SMB_CAP_DFS              0x1000
#define SMB_CAP_LARGE_READX      0x4000


/*
 * This is the time we allow an inode, dentry or dir cache to live. It is bad
 * for performance to have shorter ttl on an inode than on the cache. It can
 * cause refresh on each inode for a dir listing ... one-by-one
 */
#define SMB_MAX_AGE(server) (((server)->mnt->ttl * HZ) / 1000)

static inline void
smb_age_dentry(struct smb_sb_info *server, struct dentry *dentry)
{
	dentry->d_time = jiffies - SMB_MAX_AGE(server);
}

struct smb_cache_head {
	time_t		mtime;	/* unused */
	unsigned long	time;	/* cache age */
	unsigned long	end;	/* last valid fpos in cache */
	int		eof;
};

#define SMB_DIRCACHE_SIZE	((int)(PAGE_CACHE_SIZE/sizeof(struct dentry *)))
union smb_dir_cache {
	struct smb_cache_head   head;
	struct dentry           *dentry[SMB_DIRCACHE_SIZE];
};

#define SMB_FIRSTCACHE_SIZE	((int)((SMB_DIRCACHE_SIZE * \
	sizeof(struct dentry *) - sizeof(struct smb_cache_head)) / \
	sizeof(struct dentry *)))

#define SMB_DIRCACHE_START      (SMB_DIRCACHE_SIZE - SMB_FIRSTCACHE_SIZE)

struct smb_cache_control {
	struct  smb_cache_head		head;
	struct  page			*page;
	union   smb_dir_cache		*cache;
	unsigned long			fpos, ofs;
	int				filled, valid, idx;
};

static inline int
smb_is_open(struct inode *i)
{
	return (i->u.smbfs_i.open == SMB_SERVER(i)->generation);
}


/* FIXME! the prototype list is probably not correct. Automate? */

/* linux/fs/smbfs/file.c */
extern struct inode_operations smb_file_inode_operations;
extern struct file_operations smb_file_operations;
extern struct address_space_operations smb_file_aops;

/* linux/fs/smbfs/dir.c */
extern struct inode_operations smb_dir_inode_operations;
extern struct file_operations smb_dir_operations;
void smb_new_dentry(struct dentry *dentry);
void smb_renew_times(struct dentry *);

/* linux/fs/smbfs/ioctl.c */
int smb_ioctl (struct inode *, struct file *, unsigned int, unsigned long);

/* linux/fs/smbfs/inode.c */
struct super_block *smb_read_super(struct super_block *, void *, int);
void smb_get_inode_attr(struct inode *, struct smb_fattr *);
void smb_set_inode_attr(struct inode *, struct smb_fattr *);
void smb_invalidate_inodes(struct smb_sb_info *);
int  smb_revalidate_inode(struct dentry *);
int  smb_notify_change(struct dentry *, struct iattr *);
struct inode *smb_iget(struct super_block *, struct smb_fattr *);

/* linux/fs/smbfs/proc.c */
int smb_setcodepage(struct smb_sb_info *server, struct smb_nls_codepage *cp);
__u32 smb_len(unsigned char *);
__u8 *smb_setup_header(struct smb_sb_info *, __u8, __u16, __u16);
int smb_get_rsize(struct smb_sb_info *);
int smb_get_wsize(struct smb_sb_info *);
int smb_newconn(struct smb_sb_info *, struct smb_conn_opt *);
int smb_errno(struct smb_sb_info *);
int smb_close(struct inode *);
int smb_close_fileid(struct dentry *, __u16);
int smb_open(struct dentry *, int);
int smb_proc_read(struct inode *, off_t, int, char *);
int smb_proc_write(struct inode *, off_t, int, const char *);
int smb_proc_create(struct dentry *, __u16, time_t, __u16 *);
int smb_proc_mv(struct dentry *, struct dentry *);
int smb_proc_mkdir(struct dentry *);
int smb_proc_rmdir(struct dentry *);
int smb_proc_unlink(struct dentry *);
int smb_proc_readdir(struct file *filp, void *dirent, filldir_t filldir,
		     struct smb_cache_control *ctl);
int smb_proc_getattr(struct dentry *, struct smb_fattr *);
int smb_proc_setattr(struct dentry *, struct smb_fattr *);
int smb_proc_settime(struct dentry *, struct smb_fattr *);
int smb_proc_dskattr(struct super_block *, struct statfs *);
int smb_proc_disconnect(struct smb_sb_info *);
int smb_proc_trunc(struct smb_sb_info *, __u16, __u32);
void smb_init_root_dirent(struct smb_sb_info *, struct smb_fattr *);

/* linux/fs/smbfs/sock.c */
int smb_round_length(int);
int smb_valid_socket(struct inode *);
void smb_close_socket(struct smb_sb_info *);
int smb_request(struct smb_sb_info *server);
int smb_catch_keepalive(struct smb_sb_info *server);
int smb_dont_catch_keepalive(struct smb_sb_info *server);
int smb_trans2_request(struct smb_sb_info *server, __u16 trans2_command,
		       int ldata, unsigned char *data,
		       int lparam, unsigned char *param,
		       int *lrdata, unsigned char **rdata,
		       int *lrparam, unsigned char **rparam);

/* fs/smbfs/cache.c */

void smb_invalid_dir_cache(struct inode * dir);
void smb_invalidate_dircache_entries(struct dentry *parent);
struct dentry * smb_dget_fpos(struct dentry *dentry, struct dentry *parent, 
			      unsigned long fpos);
int smb_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
		   struct smb_cache_control *ctrl, struct qstr *qname,
		   struct smb_fattr *entry);


#endif /* __KERNEL__ */

#endif /* _LINUX_SMB_FS_H */
