#ifndef _LINUX_MSDOS_FS_H
#define _LINUX_MSDOS_FS_H

/*
 * The MS-DOS filesystem constants/structures
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/fd.h>

#include <asm/byteorder.h>

#define MSDOS_ROOT_INO  1 /* == MINIX_ROOT_INO */
#define SECTOR_SIZE     512 /* sector size (bytes) */
#define SECTOR_BITS	9 /* log2(SECTOR_SIZE) */
#define MSDOS_DPB	(MSDOS_DPS) /* dir entries per block */
#define MSDOS_DPB_BITS	4 /* log2(MSDOS_DPB) */
#define MSDOS_DPS	(SECTOR_SIZE/sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS	4 /* log2(MSDOS_DPS) */
#define MSDOS_DIR_BITS	5 /* log2(sizeof(struct msdos_dir_entry)) */

#define MSDOS_SUPER_MAGIC 0x4d44 /* MD */

#define FAT_CACHE    8 /* FAT cache size */

#define MSDOS_MAX_EXTRA	3 /* tolerate up to that number of clusters which are
			     inaccessible because the FAT is too short */

#define ATTR_RO      1  /* read-only */
#define ATTR_HIDDEN  2  /* hidden */
#define ATTR_SYS     4  /* system */
#define ATTR_VOLUME  8  /* volume label */
#define ATTR_DIR     16 /* directory */
#define ATTR_ARCH    32 /* archived */

#define ATTR_NONE    0 /* no attribute bits */
#define ATTR_UNUSED  (ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
	/* attribute bits that are copied "as is" */
#define ATTR_EXT     (ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)
	/* bits that are used by the Windows 95/Windows NT extended FAT */

#define ATTR_DIR_READ_BOTH 512 /* read both short and long names from the
				* vfat filesystem.  This is used by Samba
				* to export the vfat filesystem with correct
				* shortnames. */
#define ATTR_DIR_READ_SHORT 1024

#define CASE_LOWER_BASE 8	/* base is lower case */
#define CASE_LOWER_EXT  16	/* extension is lower case */

#define SCAN_ANY     0  /* either hidden or not */
#define SCAN_HID     1  /* only hidden */
#define SCAN_NOTHID  2  /* only not hidden */
#define SCAN_NOTANY  3  /* test name, then use SCAN_HID or SCAN_NOTHID */

#define DELETED_FLAG 0xe5 /* marks file as deleted when in name[0] */
#define IS_FREE(n) (!*(n) || *(const unsigned char *) (n) == DELETED_FLAG || \
  *(const unsigned char *) (n) == FD_FILL_BYTE)

#define MSDOS_VALID_MODE (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
	/* valid file mode bits */

#define MSDOS_SB(s) (&((s)->u.msdos_sb))
#define MSDOS_I(i) (&((i)->u.msdos_i))

#define MSDOS_NAME 11 /* maximum name length */
#define MSDOS_LONGNAME 256 /* maximum name length */
#define MSDOS_SLOTS 21  /* max # of slots needed for short and long names */
#define MSDOS_DOT    ".          " /* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT "..         " /* "..", padded to MSDOS_NAME chars */

#define MSDOS_FAT12 4084 /* maximum number of clusters in a 12 bit FAT */

#define EOF_FAT12 0xFF8		/* standard EOF */
#define EOF_FAT16 0xFFF8
#define EOF_FAT32 0xFFFFFF8
#define EOF_FAT(s) (MSDOS_SB(s)->fat_bits == 32 ? EOF_FAT32 : \
	MSDOS_SB(s)->fat_bits == 16 ? EOF_FAT16 : EOF_FAT12)

/*
 * Inode flags
 */
#define FAT_BINARY_FL		0x00000001 /* File contains binary data */

/*
 * ioctl commands
 */
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct dirent [2])
#define	VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct dirent [2])

/*
 * Conversion from and to little-endian byte order. (no-op on i386/i486)
 *
 * Naming: Ca_b_c, where a: F = from, T = to, b: LE = little-endian,
 * BE = big-endian, c: W = word (16 bits), L = longword (32 bits)
 */

#define CF_LE_W(v) le16_to_cpu(v)
#define CF_LE_L(v) le32_to_cpu(v)
#define CT_LE_W(v) cpu_to_le16(v)
#define CT_LE_L(v) cpu_to_le32(v)

struct fat_boot_sector {
	__s8	ignored[3];	/* Boot strap short or near jump */
	__s8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	__u8	sector_size[2];	/* bytes per logical sector */
	__u8	cluster_size;	/* sectors/cluster */
	__u16	reserved;	/* reserved sectors */
	__u8	fats;		/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	sectors[2];	/* number of sectors */
	__u8	media;		/* media code (unused) */
	__u16	fat_length;	/* sectors/FAT */
	__u16	secs_track;	/* sectors per track */
	__u16	heads;		/* number of heads */
	__u32	hidden;		/* hidden sectors (unused) */
	__u32	total_sect;	/* number of sectors (if sectors == 0) */

	/* The following fields are only used by FAT32 */
	__u32	fat32_length;	/* sectors/FAT */
	__u16	flags;		/* bit 8: fat mirroring, low 4: active fat */
	__u8	version[2];	/* major, minor filesystem version */
	__u32	root_cluster;	/* first cluster in root directory */
	__u16	info_sector;	/* filesystem info sector */
	__u16	backup_boot;	/* backup boot sector */
	__u16	reserved2[6];	/* Unused */
};

struct fat_boot_fsinfo {
	__u32   reserved1;	/* Nothing as far as I can tell */
	__u32   signature;	/* 0x61417272L */
	__u32   free_clusters;	/* Free cluster count.  -1 if unknown */
	__u32   next_cluster;	/* Most recently allocated cluster.
				 * Unused under Linux. */
	__u32   reserved2[4];
};

struct msdos_dir_entry {
	__s8	name[8],ext[3];	/* name and extension */
	__u8	attr;		/* attribute bits */
	__u8    lcase;		/* Case for base and extension */
	__u8	ctime_ms;	/* Creation time, milliseconds */
	__u16	ctime;		/* Creation time */
	__u16	cdate;		/* Creation date */
	__u16	adate;		/* Last access date */
	__u16   starthi;	/* High 16 bits of cluster in FAT32 */
	__u16	time,date,start;/* time, date and first cluster */
	__u32	size;		/* file size (in bytes) */
};

/* Up to 13 characters of the name */
struct msdos_dir_slot {
	__u8    id;		/* sequence number for slot */
	__u8    name0_4[10];	/* first 5 characters in name */
	__u8    attr;		/* attribute byte */
	__u8    reserved;	/* always 0 */
	__u8    alias_checksum;	/* checksum for 8.3 alias */
	__u8    name5_10[12];	/* 6 more characters in name */
	__u16   start;		/* starting cluster number, 0 in long slots */
	__u8    name11_12[4];	/* last 2 characters in name */
};

struct vfat_slot_info {
	int is_long;		       /* was the found entry long */
	int long_slots;		       /* number of long slots in filename */
	int total_slots;	       /* total slots (long and short) */
	loff_t longname_offset;	       /* dir offset for longname start */
	loff_t shortname_offset;       /* dir offset for shortname start */
	int ino;		       /* ino for the file */
};

/* Determine whether this FS has kB-aligned data. */
#define MSDOS_CAN_BMAP(mib) (!(((mib)->cluster_size & 1) || \
    ((mib)->data_start & 1)))

/* Convert attribute bits and a mask to the UNIX mode. */
#define MSDOS_MKMODE(a,m) (m & (a & ATTR_RO ? S_IRUGO|S_IXUGO : S_IRWXUGO))

/* Convert the UNIX mode to MS-DOS attribute bits. */
#define MSDOS_MKATTR(m) ((m & S_IWUGO) ? ATTR_NONE : ATTR_RO)


#ifdef __KERNEL__

struct fat_cache {
	kdev_t device; /* device number. 0 means unused. */
	int start_cluster; /* first cluster of the chain. */
	int file_cluster; /* cluster number in the file. */
	int disk_cluster; /* cluster number on disk. */
	struct fat_cache *next; /* next cache entry */
};

/* misc.c */
extern int fat_is_binary(char conversion,char *extension);
extern void lock_fat(struct super_block *sb);
extern void unlock_fat(struct super_block *sb);
extern int fat_add_cluster(struct inode *inode);
extern struct buffer_head *fat_extend_dir(struct inode *inode);
extern int date_dos2unix(__u16 time, __u16 date);
extern void fat_fs_panic(struct super_block *s,const char *msg);
extern void fat_lock_creation(void);
extern void fat_unlock_creation(void);
extern void fat_date_unix2dos(int unix_date,__u16 *time, __u16 *date);
extern int fat__get_entry(struct inode *dir,loff_t *pos,struct buffer_head **bh,
			 struct msdos_dir_entry **de,int *ino);
static __inline__ int fat_get_entry(struct inode *dir,loff_t *pos,
		struct buffer_head **bh,struct msdos_dir_entry **de,int *ino)
{
	/* Fast stuff first */
	if (*bh && *de &&
	    	(*de - (struct msdos_dir_entry *)(*bh)->b_data) < MSDOS_DPB-1) {
		*pos += sizeof(struct msdos_dir_entry);
		(*de)++;
		(*ino)++;
		return 0;
	}
	return fat__get_entry(dir,pos,bh,de,ino);
}
extern int fat_scan(struct inode *dir,const char *name,struct buffer_head **res_bh,
		    struct msdos_dir_entry **res_de,int *ino);
extern int fat_parent_ino(struct inode *dir,int locked);
extern int fat_subdirs(struct inode *dir);
void fat_clusters_flush(struct super_block *sb);

/* fat.c */
extern int fat_access(struct super_block *sb,int nr,int new_value);
extern int fat_free(struct inode *inode,int skip);
void fat_cache_inval_inode(struct inode *inode);
void fat_cache_inval_dev(kdev_t device);
extern void fat_cache_init(void);
void fat_cache_lookup(struct inode *inode,int cluster,int *f_clu,int *d_clu);
void fat_cache_add(struct inode *inode,int f_clu,int d_clu);
int fat_get_cluster(struct inode *inode,int cluster);

/* inode.c */
extern void fat_hash_init(void);
extern int fat_bmap(struct inode *inode,int block);
extern int fat_get_block(struct inode *, long, struct buffer_head *, int);
extern int fat_notify_change(struct dentry *, struct iattr *);
extern void fat_clear_inode(struct inode *inode);
extern void fat_delete_inode(struct inode *inode);
extern void fat_put_super(struct super_block *sb);
extern void fat_attach(struct inode *inode, int ino);
extern void fat_detach(struct inode *inode);
extern struct inode *fat_iget(struct super_block*,int);
extern struct inode *fat_build_inode(struct super_block*,struct msdos_dir_entry*,int,int*);
extern struct super_block *fat_read_super(struct super_block *s, void *data, int silent, struct inode_operations *dir_ops);
extern void msdos_put_super(struct super_block *sb);
extern int fat_statfs(struct super_block *sb,struct statfs *buf);
extern void fat_write_inode(struct inode *inode, int);

/* dir.c */
extern struct file_operations fat_dir_operations;
extern int fat_search_long(struct inode *dir, const char *name, int len,
			   int anycase, loff_t *spos, loff_t *lpos);
extern int fat_readdir(struct file *filp,
		       void *dirent, filldir_t);
extern int fat_dir_ioctl(struct inode * inode, struct file * filp,
			 unsigned int cmd, unsigned long arg);
int fat_add_entries(struct inode *dir,int slots, struct buffer_head **bh,
		  struct msdos_dir_entry **de, int *ino);
int fat_dir_empty(struct inode *dir);
int fat_new_dir(struct inode *inode, struct inode *parent, int is_vfat);

/* file.c */
extern struct inode_operations fat_file_inode_operations;
extern struct inode_operations fat_file_inode_operations_1024;
extern struct inode_operations fat_file_inode_operations_readpage;
extern struct file_operations fat_file_operations;
extern ssize_t fat_file_read(struct file *, char *, size_t, loff_t *);
extern ssize_t fat_file_write(struct file *, const char *, size_t, loff_t *);
extern void fat_truncate(struct inode *inode);

/* msdos.c */
extern struct super_block *msdos_read_super(struct super_block *sb,void *data, int silent);

/* msdos.c - these are for Umsdos */
extern void msdos_read_inode(struct inode *inode);
extern struct dentry *msdos_lookup(struct inode *dir,struct dentry *);
extern int msdos_create(struct inode *dir,struct dentry *dentry,int mode);
extern int msdos_rmdir(struct inode *dir,struct dentry *dentry);
extern int msdos_mkdir(struct inode *dir,struct dentry *dentry,int mode);
extern int msdos_unlink(struct inode *dir,struct dentry *dentry);
extern int msdos_rename(struct inode *old_dir,struct dentry *old_dentry,
			struct inode *new_dir,struct dentry *new_dentry);

/* nls.c */
extern struct fat_nls_table *fat_load_nls(int codepage);

/* tables.c */
extern unsigned char fat_uni2esc[];
extern unsigned char fat_esc2uni[];

/* fatfs_syms.c */
extern void cleanup_fat_fs(void);

/* nls.c */
extern int fat_register_nls(struct fat_nls_table * fmt);
extern int fat_unregister_nls(struct fat_nls_table * fmt);
extern struct fat_nls_table *fat_find_nls(int codepage);
extern struct fat_nls_table *fat_load_nls(int codepage);
extern void fat_unload_nls(int codepage);
extern int init_fat_nls(void);

/* vfat/namei.c - these are for dmsdos */
extern int vfat_create(struct inode *dir,struct dentry *dentry,int mode);
extern int vfat_unlink(struct inode *dir,struct dentry *dentry);
extern int vfat_mkdir(struct inode *dir,struct dentry *dentry,int mode);
extern int vfat_rmdir(struct inode *dir,struct dentry *dentry);
extern int vfat_rename(struct inode *old_dir,struct dentry *old_dentry,
		       struct inode *new_dir,struct dentry *new_dentry);
extern struct super_block *vfat_read_super(struct super_block *sb,void *data,
					   int silent);
extern void vfat_read_inode(struct inode *inode);
extern struct dentry *vfat_lookup(struct inode *dir,struct dentry *);

/* vfat/vfatfs_syms.c */
extern struct file_system_type vfat_fs_type;

#endif /* __KERNEL__ */

#endif
