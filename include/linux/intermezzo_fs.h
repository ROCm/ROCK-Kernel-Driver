/*
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  Copyright (C) 2000 Stelias Computing, Inc.
 *  Copyright (C) 2000 Red Hat, Inc.
 *  Copyright (C) 2000 TurboLinux, Inc.
 *  Copyright (C) 2000 Los Alamos National Laboratory.
 *  Copyright (C) 2001 Tacitus Systems, Inc.
 *  Copyright (C) 2001 Cluster File Systems, Inc. 
 */

#ifndef __INTERMEZZO_FS_H_
#define __INTERMEZZO_FS_H_ 1

#ifdef __KERNEL__
#include <linux/smp.h>
#include <linux/fsfilter.h>

/* fixups for fs.h */
#ifndef fs_down
#define fs_down(sem) down(sem)
#endif

#ifndef fs_up
#define fs_up(sem) up(sem)
#endif

/* We will be more tolerant than the default ea patch with attr name sizes and
 * the size of value. If these come via VFS from the default ea patches, the
 * corresponding character strings will be truncated anyway. During journalling- * we journal length for both name and value. See journal_set_ext_attr.
 */
#define PRESTO_EXT_ATTR_NAME_MAX 128
#define PRESTO_EXT_ATTR_VALUE_MAX 8192

#define KML_IDLE                        0
#define KML_DECODE                      1
#define KML_OPTIMIZE                    2
#define KML_REINT                       3

#define KML_OPEN_REINT                  0x0100
#define KML_REINT_BEGIN                 0x0200
#define KML_BACKFETCH                   0x0400
#define KML_REINT_END                   0x0800
#define KML_CLOSE_REINT                 0x1000
#define FSET_GET_KMLDATA(fset)          fset->fset_kmldata
#define KML_REINT_MAXBUF              	(64 * 1024)

struct  kml_fsdata
{
        int                kml_state;

        /* kml optimize support */
        struct list_head   kml_kop_cache;

        /* kml reint support */
        int                kml_reint_state;
        struct list_head   kml_reint_cache;
        struct list_head  *kml_reint_current;
        int                kml_maxsize;  /* max buffer */
        int                kml_len;
        char *             kml_buf;
        loff_t             kml_reintpos;
        int                kml_count;
};

/* super.c */
struct presto_cache *presto_find_cache(kdev_t dev) ;
extern struct file_system_type presto_fs_type;
extern int init_intermezzo_fs(void);

#define CACHE_TYPE_LENGTH       16

int presto_ispresto(struct inode *);

#define CACHE_CLIENT_RO       0x4
#define CACHE_LENTO_RO        0x8
#define CACHE_FSETROOT_SET     0x10


struct presto_cache {
        spinlock_t         cache_lock; 
	loff_t             cache_reserved;
        struct list_head cache_chain; /* for the dev/cache hash */

        int   cache_flags;
        char *cache_root_fileset;  /* fileset mounted on cache "/"  */

        kdev_t cache_dev;            /* underlying block device */
	struct super_block *cache_sb;
        struct dentry *cache_mtde;  /* unix mtpt of cache XXX NOT VALID XXX */
        char *cache_mtpt;           /*  again */

        char *cache_type;            /* filesystem type of cache */
        struct filter_fs *cache_filter;

        struct upc_comm *cache_psdev;  /* points to /dev/intermezzo? we use */
        struct list_head cache_psdev_chain; 

        struct list_head cache_fset_list; /* filesets mounted in cache */
};




/* file sets */
#define CHUNK_BITS  16

struct presto_log_fd {
        rwlock_t         fd_lock; 
        loff_t           fd_offset;  /* offset where next record should go */ 
        struct file    *fd_file;
        int             fd_truncating;
        unsigned int   fd_recno;   /* last recno written */ 
        struct list_head  fd_reservations;
};

struct presto_file_set {
        struct list_head fset_list;
        struct presto_log_fd fset_kml;
        struct presto_log_fd fset_lml;
        struct file *fset_last_rcvd;
        struct dentry *fset_mtpt;
        struct nameidata fset_nd; 
        struct presto_cache *fset_cache;

        unsigned int fset_lento_recno;  /* last recno mentioned to lento */
        loff_t fset_lento_off;    /* last offset mentioned to lento */
        char * fset_name;

        int fset_flags;
        int fset_permit_count;
        int fset_permit_cookie;
        int fset_chunkbits; 
	struct  kml_fsdata *fset_kmldata;
	loff_t  fset_file_maxio;  /* writing more than this causes a close */ 
};

/* This is the default number of bytes written before a close is recorded*/
#define FSET_DEFAULT_MAX_FILEIO (1024<<10)

struct journal_ops {
        loff_t (*tr_avail)(struct presto_cache *fset, struct super_block *);
        void *(*tr_start)(struct presto_file_set *, struct inode *, int op);
        void (*tr_commit)(struct presto_file_set *, void *handle);
        void (*tr_journal_data)(struct inode *);
};


extern struct journal_ops presto_ext2_journal_ops;
extern struct journal_ops presto_ext3_journal_ops;
extern struct journal_ops presto_xfs_journal_ops;
extern struct journal_ops presto_reiserfs_journal_ops;
extern struct journal_ops presto_obdfs_journal_ops;
struct lento_vfs_context {
        __u32 slot_offset;
        __u32 recno;
        __u64 kml_offset;
        __u32 flags;
        __u32 updated_time;
};


#define LENTO_FL_KML            0x0001
#define LENTO_FL_EXPECT         0x0002
#define LENTO_FL_VFSCHECK       0x0004
#define LENTO_FL_JUSTLOG        0x0008
#define LENTO_FL_WRITE_KML      0x0010
#define LENTO_FL_CANCEL_LML     0x0020
#define LENTO_FL_WRITE_EXPECT   0x0040
#define LENTO_FL_IGNORE_TIME    0x0080

struct presto_cache *presto_get_cache(struct inode *inode) ;
int presto_sprint_mounts(char *buf, int buflen, int minor);
struct presto_file_set *presto_fset(struct dentry *de);
int presto_journal(struct dentry *dentry, char *buf, size_t size);
int presto_fwrite(struct file *file, const char *str, int len, loff_t *off);

/* psdev.c */
int presto_psdev_init(void);
extern void presto_psdev_cleanup(void);
inline int presto_lento_up(int minor);

/* inode.c */
extern struct super_operations presto_super_ops;
extern int presto_excluded_gid; 
#define PRESTO_EXCL_GID 4711
void presto_set_ops(struct inode *inode, struct  filter_fs *filter);
void presto_read_inode(struct inode *inode);
void presto_put_super(struct super_block *);

/* journal.c */
void presto_trans_commit(struct presto_file_set *fset, void *handle);
void *presto_trans_start(struct presto_file_set *fset, struct inode *inode,
                           int op);

/* dcache.c */
void presto_frob_dop(struct dentry *de) ;
char * presto_path(struct dentry *dentry, struct dentry *root,
                   char *buffer, int buflen);
void presto_set_dd(struct dentry *);
void presto_init_ddata_cache(void);
void presto_cleanup_ddata_cache(void);
extern struct dentry_operations presto_dentry_ops;



/* dir.c */
extern struct inode_operations presto_dir_iops;
extern struct inode_operations presto_file_iops;
extern struct inode_operations presto_sym_iops;
extern struct file_operations presto_dir_fops;
extern struct file_operations presto_file_fops;
extern struct file_operations presto_sym_fops;
int presto_setattr(struct dentry *de, struct iattr *iattr);
extern int presto_ilookup_uid; 
#define PRESTO_ILOOKUP_MAGIC "...ino:"
#define PRESTO_ILOOKUP_SEP ':'

struct dentry *presto_lookup(struct inode * dir, struct dentry *dentry);

/* file.c */
struct presto_reservation_data {
        unsigned int ri_recno; 
        loff_t ri_offset;
        loff_t ri_size;
        struct list_head ri_list;
};


struct presto_dentry_data { 
        int dd_count; /* how mnay dentries are using this dentry */
        struct presto_file_set *dd_fset;
        loff_t dd_kml_offset;
        int dd_flags;

}; 

struct presto_file_data { 
        int fd_do_lml;
        loff_t fd_lml_offset;
        uid_t fd_fsuid;
        gid_t fd_fsgid;
        uid_t fd_uid;
        gid_t fd_gid;
        mode_t fd_mode;
        int fd_ngroups;
        size_t fd_bytes_written; /* Number of bytes written so far on this fd*/
        gid_t fd_groups[NGROUPS_MAX];
};


/* presto.c and Lento::Downcall */
struct presto_version {
        __u64 pv_mtime;
        __u64 pv_ctime;
        __u64 pv_size;
};
inline struct presto_dentry_data *presto_d2d(struct dentry *);
int presto_walk(const char *name, struct nameidata *nd);
int presto_clear_fsetroot(char *path);
int presto_clear_all_fsetroots(char *path);
int  presto_get_kmlsize(char *path, size_t *size);
int  presto_get_lastrecno(char *path, off_t *size);
int presto_set_fsetroot(char *path, char *fsetname, unsigned int fsetid,
                       unsigned int flags);
int presto_has_all_data(struct inode *inode);
inline int presto_is_read_only(struct presto_file_set *);
int presto_truncate_lml(struct presto_file_set *fset);
int lento_write_lml(char *path,
                     __u64 remote_ino, 
                     __u32 remote_generation,
                     __u32 remote_version,
                    struct presto_version *remote_file_version);
int lento_reset_fset(char *path, __u64 offset, __u32 recno);
int lento_complete_closes(char *path);
int lento_cancel_lml(char *path,
                     __u64 lml_offset, 
                     __u64 remote_ino, 
                     __u32 remote_generation,
                     __u32 remote_version, 
                     struct lento_vfs_context *info);
inline int presto_f2m(struct presto_file_set *fset);

/* cache.c */
#define PRESTO_REQLOW  (3 * 4096)
#define PRESTO_REQHIGH (6 * 4096)
void presto_release_space(struct presto_cache *cache, loff_t req);
int presto_reserve_space(struct presto_cache *cache, loff_t req);

/* NOTE: PRESTO_FSETROOT MUST be 0x1:
   - if this bit is set dentry->d_fsdata points to a file_set
   - the address of the file_set if d_fsdata - 1
*/

#define PRESTO_FSETROOT         0x00000001 /* dentry is fileset root */
#define PRESTO_DATA             0x00000002 /* cached data is valid */
#define PRESTO_ATTR             0x00000004 /* attributes cached */

#define EISFSETROOT             0x2001


struct presto_file_set *presto_path2fileset(const char *name);
int presto_permit_downcall(const char *path, int *cookie);
int presto_chk(struct dentry *dentry, int flag);
void presto_set(struct dentry *dentry, int flag);
int presto_get_permit(struct inode *inode);
int presto_put_permit(struct inode *inode);
int presto_mark_dentry(const char *path, int and, int or, int *res);
int presto_mark_cache(const char *path, int and_bits, int or_bits, int *);
int presto_mark_fset(const char *path, int and_bits, int or_bits, int *);
void presto_getversion(struct presto_version *pv, struct inode *inode);
int presto_i2m(struct inode *inode);
int presto_c2m(struct presto_cache *cache);

/* journal.c */
struct rec_info {
        loff_t offset;
        int size;
        int recno;
        int is_kml;
};
void presto_trans_commit(struct presto_file_set *fset, void *handle);
void *presto_trans_start(struct presto_file_set *fset, struct inode *inode,
                           int op);
int presto_clear_lml_close(struct presto_file_set *fset, 
                           loff_t  lml_offset);
int presto_write_lml_close(struct rec_info *rec,
                           struct presto_file_set *fset, 
                           struct file *file,
                           __u64 remote_ino,
                           __u32 remote_generation,
                           __u32 remote_version,
                           struct presto_version *new_file_ver);
int presto_complete_lml(struct presto_file_set *fset); 

/* vfs.c */
int presto_do_setattr(struct presto_file_set *fset, struct dentry *dentry,
                      struct iattr *iattr, struct lento_vfs_context *info);
int presto_do_create(struct presto_file_set *fset, struct dentry *dir,
                     struct dentry *dentry, int mode,
                     struct lento_vfs_context *info);
int presto_do_link(struct presto_file_set *fset, struct dentry *dir,
                   struct dentry *old_dentry, struct dentry *new_dentry,
                   struct lento_vfs_context *info);
int presto_do_unlink(struct presto_file_set *fset, struct dentry *dir,
                     struct dentry *dentry, struct lento_vfs_context *info);
int presto_do_symlink(struct presto_file_set *fset, struct dentry *dir,
                      struct dentry *dentry, const char *name,
                      struct lento_vfs_context *info);
int presto_do_mkdir(struct presto_file_set *fset, struct dentry *dir,
                    struct dentry *dentry, int mode,
                    struct lento_vfs_context *info);
int presto_do_rmdir(struct presto_file_set *fset, struct dentry *dir,
                    struct dentry *dentry, struct lento_vfs_context *info);
int presto_do_mknod(struct presto_file_set *fset, struct dentry *dir,
                    struct dentry *dentry, int mode, dev_t dev,
                    struct lento_vfs_context *info);
int presto_do_rename(struct presto_file_set *fset, struct dentry *old_dir,
                     struct dentry *old_dentry, struct dentry *new_dir,
                     struct dentry *new_dentry, struct lento_vfs_context *info);

int lento_setattr(const char *name, struct iattr *iattr,
                  struct lento_vfs_context *info);
int lento_create(const char *name, int mode, struct lento_vfs_context *info);
int lento_link(const char *oldname, const char *newname,
               struct lento_vfs_context *info);
int lento_unlink(const char *name, struct lento_vfs_context *info);
int lento_symlink(const char *oldname,const char *newname,
                  struct lento_vfs_context *info);
int lento_mkdir(const char *name, int mode, struct lento_vfs_context *info);
int lento_rmdir(const char *name, struct lento_vfs_context *info);
int lento_mknod(const char *name, int mode, dev_t dev,
                struct lento_vfs_context *info);
int lento_rename(const char *oldname, const char *newname,
                 struct lento_vfs_context *info);
int lento_iopen(const char *name, ino_t ino, unsigned int generation,int flags);
int lento_close(unsigned int fd, struct lento_vfs_context *info);


/* journal.c */

#define JOURNAL_PAGE_SZ  PAGE_SIZE

__inline__ int presto_no_journal(struct presto_file_set *fset);
int journal_fetch(int minor);
int presto_journal_write(struct rec_info *rec, struct presto_file_set *fset,
                         struct file *file);
int presto_journal_setattr(struct rec_info *rec, struct presto_file_set *fset,
                           struct dentry *dentry,
                           struct presto_version *old_ver,
                           struct iattr *iattr);
int presto_journal_create(struct rec_info *rec, struct presto_file_set *fset,
                          struct dentry *dentry,
                          struct presto_version *tgt_dir_ver,
                          struct presto_version *new_file_ver, int mode);
int presto_journal_link(struct rec_info *rec, struct presto_file_set *fset,
                        struct dentry *src, struct dentry *tgt,
                        struct presto_version *tgt_dir_ver,
                        struct presto_version *new_link_ver);
int presto_journal_unlink(struct rec_info *rec, struct presto_file_set *fset,
                          struct dentry *dentry,
                          struct presto_version *tgt_dir_ver,
                          struct presto_version *old_file_ver, int len,
                          const char *name);
int presto_journal_symlink(struct rec_info *rec, struct presto_file_set *fset,
                           struct dentry *dentry, const char *target,
                           struct presto_version *tgt_dir_ver,
                           struct presto_version *new_link_ver);
int presto_journal_mkdir(struct rec_info *rec, struct presto_file_set *fset,
                         struct dentry *dentry,
                         struct presto_version *tgt_dir_ver,
                         struct presto_version *new_dir_ver, int mode);
int presto_journal_rmdir(struct rec_info *rec, struct presto_file_set *fset,
                         struct dentry *dentry,
                         struct presto_version *tgt_dir_ver,
                         struct presto_version *old_dir_ver, int len,
                         const char *name);
int presto_journal_mknod(struct rec_info *rec, struct presto_file_set *fset,
                         struct dentry *dentry,
                         struct presto_version *tgt_dir_ver,
                         struct presto_version *new_node_ver, int mode,
                         int dmajor, int dminor);
int presto_journal_rename(struct rec_info *rec, struct presto_file_set *fset,
                          struct dentry *src, struct dentry *tgt,
                          struct presto_version *src_dir_ver,
                          struct presto_version *tgt_dir_ver);
int presto_journal_open(struct rec_info *rec, struct presto_file_set *fset,
                        struct dentry *dentry, struct presto_version *old_ver);
int presto_journal_close(struct rec_info *rec, struct presto_file_set *fset,
                         struct file *file, 
			 struct dentry *dentry, 
			 struct presto_version *new_ver);
int presto_close_journal_file(struct presto_file_set *fset);
void presto_log_op(void *data, int len);
int presto_write_last_rcvd(struct rec_info *recinfo,
                           struct presto_file_set *fset,
                           struct lento_vfs_context *info);

/* journal_ext3.c */
struct ext3_journal_data {
        struct file *jd_file;
};
extern struct ext3_journal_data e3jd;




/* sysctl.c */
int init_intermezzo_sysctl(void);
void cleanup_intermezzo_sysctl(void);

/* ext_attr.c */
#ifdef CONFIG_FS_EXT_ATTR
/* XXX: Borrowed from vfs.c. Once the ea patch is into CVS 
 * move this prototype -SHP
 */
int presto_do_set_ext_attr(struct presto_file_set *fset,
                           struct dentry *dentry,
                           const char *name, void *buffer,
                           size_t buffer_len, int flags, mode_t *mode,
                           struct lento_vfs_context *info);
int presto_set_ext_attr(struct inode *inode,
                        const char *name, void *buffer,
                        size_t buffer_len, int flags);
int lento_set_ext_attr(const char *path, const char *name,
                       void *buffer, size_t buffer_len, int flags,
                       mode_t mode, struct lento_vfs_context *info);
/* XXX: Borrowed from journal.c. Once the ea patch is into CVS 
 * move this prototype -SHP
 */
int presto_journal_set_ext_attr (struct rec_info *rec,
                                 struct presto_file_set *fset,
                                 struct dentry *dentry,
                                 struct presto_version *ver, const char *name,
                                 const char *buffer, int buffer_len,
                                 int flags);
#endif


/* global variables */
extern int presto_debug;
extern int presto_print_entry;

#define PRESTO_DEBUG
#ifdef PRESTO_DEBUG
/* debugging masks */
#define D_SUPER     1   /* print results returned by Venus */
#define D_INODE     2   /* print entry and exit into procedure */
#define D_FILE      4
#define D_CACHE     8   /* cache debugging */
#define D_MALLOC    16  /* print malloc, de-alloc information */
#define D_JOURNAL   32
#define D_UPCALL    64  /* up and downcall debugging */
#define D_PSDEV    128
#define D_PIOCTL   256
#define D_SPECIAL  512
#define D_TIMING  1024
#define D_DOWNCALL 2048
#define D_KML      4096

#define CDEBUG(mask, format, a...)                                      \
        do {                                                            \
                if (presto_debug & mask) {                              \
                        printk("(%s:%s,l. %d %d): ", __FILE__, __FUNCTION__, __LINE__, current->pid);   \
                        printk(format, ##a); }                          \
        } while (0)

#define ENTRY                                                           \
        if(presto_print_entry)                                          \
                printk("Process %d entered %s\n", current->pid, __FUNCTION__)

#define EXIT                                                            \
        if(presto_print_entry)                                          \
                printk("Process %d leaving %s at %d\n", current->pid,   \
                       __FUNCTION__,__LINE__)

extern long presto_kmemory;
extern long presto_vmemory;

#define presto_kmem_inc(ptr, size) presto_kmemory += (size)
#define presto_kmem_dec(ptr, size) presto_kmemory -= (size)
#define presto_vmem_inc(ptr, size) presto_vmemory += (size)
#define presto_vmem_dec(ptr, size) presto_vmemory -= (size)
#else /* !PRESTO_DEBUG */
#define CDEBUG(mask, format, a...) do {} while (0)
#define ENTRY do {} while (0)
#define EXIT do {} while (0)
#define presto_kmem_inc(ptr, size) do {} while (0)
#define presto_kmem_dec(ptr, size) do {} while (0)
#define presto_vmem_inc(ptr, size) do {} while (0)
#define presto_vmem_dec(ptr, size) do {} while (0)
#endif /* PRESTO_DEBUG */


#define PRESTO_ALLOC(ptr, cast, size)                                   \
do {                                                                    \
    if (size <= 4096) {                                                 \
        ptr = (cast)kmalloc((unsigned long) size, GFP_KERNEL);          \
        CDEBUG(D_MALLOC, "kmalloced: %ld at %p.\n", (long)size, ptr);   \
        presto_kmem_inc(ptr, size);                                     \
    } else {                                                            \
        ptr = (cast)vmalloc((unsigned long) size);                      \
        CDEBUG(D_MALLOC, "vmalloced: %ld at %p.\n", (long)size, ptr);   \
        presto_vmem_inc(ptr, size);                                     \
    }                                                                   \
    if ((ptr) == 0)                                                     \
        printk("PRESTO: out of memory at %s:%d\n", __FILE__, __LINE__); \
    else                                                                \
        memset( ptr, 0, size );                                         \
} while (0)



#define PRESTO_FREE(ptr,size)                                           \
do {                                                                    \
    if (!ptr) {                                                         \
        printk("PRESTO: free NULL pointer (%ld bytes) at %s:%d\n",      \
               (long)size, __FILE__, __LINE__);                         \
        break;                                                          \
    }                                                                   \
    if (size <= 4096) {                                                 \
        CDEBUG(D_MALLOC, "kfreed: %ld at %p.\n", (long)size, ptr);      \
        presto_kmem_dec(ptr, size);                                     \
        kfree((ptr));                                         \
    } else {                                                            \
        CDEBUG(D_MALLOC, "vfreed: %ld at %p.\n", (long)size, ptr);      \
        presto_vmem_dec(ptr, size);                                     \
        vfree((ptr));                                                   \
    }                                                                   \
} while (0)

#define MYPATHLEN(buffer,path) (buffer + PAGE_SIZE - path - 1)

#else /* __KERNEL__ */
#include <asm/types.h>
#include <sys/ioctl.h>
struct lento_vfs_context {
        __u32 slot_offset;
        __u32 recno;
        __u64 kml_offset;
        __u32 flags;
        __u32 updated_time;
};
#endif /* __KERNEL__*/


/* marking flags for fsets */
#define FSET_CLIENT_RO 0x00000001
#define FSET_LENTO_RO  0x00000002
#define FSET_HASPERMIT  0x00000004 /* we have a permit to WB */
#define FSET_INSYNC     0x00000008 /* this fileset is in sync */
#define FSET_PERMIT_WAITING 0x00000010 /* Lento is waiting for permit */
#define FSET_STEAL_PERMIT 0x00000020 /* take permit if Lento is dead */
#define FSET_JCLOSE_ON_WRITE 0x00000040 /* Journal closes on writes */


/* what to mark indicator (ioctl parameter) */ 
#define MARK_DENTRY   101
#define MARK_FSET     102
#define MARK_CACHE    103
#define MARK_GETFL    104



struct readmount {
        int io_len;  /* this is IN & OUT: true length of str is returned */
        char *io_string;
};

/* modeled after setsockopt */
/* so if you have no /proc, oh well. */
/* for now it's all ints. We may grow this later for non-ints. */
struct psdev_opt {
        int optname;
        int optval;
};

struct lento_input {
        char *name;
        struct lento_vfs_context info;
};

struct lento_input_attr {
        char *name;
#if BITS_PER_LONG < 64
        __u32 dummy;    /* XXX on 64-bit platforms, this is not needed */
#endif
        __u32 valid;
        __u32 mode;
        __u32 uid;
        __u32 gid;
        __u64 size;
        __s64 atime;
        __s64 mtime;
        __s64 ctime;
        __u32 attr_flags;
        struct lento_vfs_context info;
};

struct lento_input_mode {
        char *name;
        __u32 mode;
        struct lento_vfs_context info;
};

struct lento_input_old_new {
        char *oldname;
        char *newname;
        struct lento_vfs_context info;
};

struct lento_input_dev {
        char *name;
        __u32 mode;
        __u32 major;
        __u32 minor;
        struct lento_vfs_context info;
};

struct lento_input_iopen {
        char *name;
#if BITS_PER_LONG < 64
        __u32 dummy;    /* XXX on 64-bit platforms, this is not needed */
#endif
        __u64 ino;
        __u32 generation;
        __u32 flags;
        __s32 fd;
};

struct lento_input_close {
        __u32 fd;
        struct lento_vfs_context info;
};

/* XXX: check for alignment */
struct lento_input_ext_attr {
        char  *path;
        char  *name;
        __u32 name_len;
        char  *buffer;
        __u32 buffer_len;
        __u32 flags;
        __u32 mode;
        struct lento_vfs_context info;
};

/* XXX should PRESTO_GET_* actually be of type _IOR, since we are reading? */
#define PRESTO_GETMOUNT         _IOW ('p',0x03, struct readmount *)
#define PRESTO_SETPID           _IOW ('p',0x04, struct readmount *)
#define PRESTO_CLOSE_JOURNALF   _IOW ('p',0x06, struct readmount *)
#define PRESTO_SET_FSETROOT     _IOW ('p',0x07, struct readmount *)
#define PRESTO_CLEAR_FSETROOT   _IOW ('p',0x08, struct readmount *)
#define PRESTO_SETOPT           _IOW ('p',0x09, struct psdev_opt *)
#define PRESTO_GETOPT           _IOW ('p',0x0a, struct psdev_opt *)
#define PRESTO_GET_KMLSIZE      _IOW ('p',0x0b, struct psdev_opt *)
#define PRESTO_GET_RECNO        _IOW ('p',0x0c, struct psdev_opt *)
#define PRESTO_VFS_SETATTR      _IOW ('p',0x10, struct lento_input_attr *)
#define PRESTO_VFS_CREATE       _IOW ('p',0x11, struct lento_input_mode *)
#define PRESTO_VFS_LINK         _IOW ('p',0x12, struct lento_input_old_new *)
#define PRESTO_VFS_UNLINK       _IOW ('p',0x13, struct lento_input *)
#define PRESTO_VFS_SYMLINK      _IOW ('p',0x14, struct lento_input_old_new *)
#define PRESTO_VFS_MKDIR        _IOW ('p',0x15, struct lento_input_mode *)
#define PRESTO_VFS_RMDIR        _IOW ('p',0x16, struct lento_input *)
#define PRESTO_VFS_MKNOD        _IOW ('p',0x17, struct lento_input_dev *)
#define PRESTO_VFS_RENAME       _IOW ('p',0x18, struct lento_input_old_new *)
#define PRESTO_VFS_CLOSE        _IOW ('p',0x1a, struct lento_input_close *)
#define PRESTO_VFS_IOPEN        _IOW ('p',0x1b, struct lento_input_iopen *)
#define PRESTO_VFS_SETEXTATTR   _IOW ('p',0x1c, struct lento_input_ext_attr *)
#define PRESTO_VFS_DELEXTATTR   _IOW ('p',0x1d, struct lento_input_ext_attr *)

#define PRESTO_MARK             _IOW ('p',0x20, struct lento_input_open *)
#define PRESTO_RELEASE_PERMIT   _IOW ('p',0x21, struct lento_input_open *)
#define PRESTO_CLEAR_ALL_FSETROOTS  _IOW ('p',0x22, struct readmount *)
#define PRESTO_BACKFETCH_LML    _IOW ('p',0x23, struct readmount *)
#define PRESTO_REINT            _IOW ('p',0x24, struct readmount *)
#define PRESTO_CANCEL_LML       _IOW ('p',0x25, struct readmount *)
#define PRESTO_RESET_FSET       _IOW ('p',0x26, struct readmount *)
#define PRESTO_COMPLETE_CLOSES  _IOW ('p',0x27, struct readmount *)

#define PRESTO_REINT_BEGIN      _IOW ('p',0x30, struct readmount *)
#define PRESTO_DO_REINT         _IOW ('p',0x31, struct readmount *)
#define PRESTO_REINT_END        _IOW ('p',0x32, struct readmount *)

#endif
