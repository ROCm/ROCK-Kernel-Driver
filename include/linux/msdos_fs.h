#ifndef _LINUX_MSDOS_FS_H
#define _LINUX_MSDOS_FS_H

/*
 * The MS-DOS filesystem constants/structures
 */
#include <asm/byteorder.h>

#define SECTOR_SIZE	512		/* sector size (bytes) */
#define SECTOR_BITS	9		/* log2(SECTOR_SIZE) */
#define MSDOS_DPB	(MSDOS_DPS)	/* dir entries per block */
#define MSDOS_DPB_BITS	4		/* log2(MSDOS_DPB) */
#define MSDOS_DPS	(SECTOR_SIZE / sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS	4		/* log2(MSDOS_DPS) */


#define MSDOS_SUPER_MAGIC 0x4d44 /* MD */

#define MSDOS_ROOT_INO	1	/* == MINIX_ROOT_INO */
#define MSDOS_DIR_BITS	5	/* log2(sizeof(struct msdos_dir_entry)) */

/* directory limit */
#define FAT_MAX_DIR_ENTRIES	(65536)
#define FAT_MAX_DIR_SIZE	(FAT_MAX_DIR_ENTRIES << MSDOS_DIR_BITS)

#define ATTR_NONE    0 /* no attribute bits */
#define ATTR_RO      1  /* read-only */
#define ATTR_HIDDEN  2  /* hidden */
#define ATTR_SYS     4  /* system */
#define ATTR_VOLUME  8  /* volume label */
#define ATTR_DIR     16 /* directory */
#define ATTR_ARCH    32 /* archived */

/* attribute bits that are copied "as is" */
#define ATTR_UNUSED  (ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
/* bits that are used by the Windows 95/Windows NT extended FAT */
#define ATTR_EXT     (ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)

#define CASE_LOWER_BASE 8	/* base is lower case */
#define CASE_LOWER_EXT  16	/* extension is lower case */

#define DELETED_FLAG 0xe5 /* marks file as deleted when in name[0] */
#define IS_FREE(n) (!*(n) || *(n) == DELETED_FLAG)

/* valid file mode bits */
#define MSDOS_VALID_MODE (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
/* Convert attribute bits and a mask to the UNIX mode. */
#define MSDOS_MKMODE(a, m) (m & (a & ATTR_RO ? S_IRUGO|S_IXUGO : S_IRWXUGO))
/* Convert the UNIX mode to MS-DOS attribute bits. */
#define MSDOS_MKATTR(m) ((m & S_IWUGO) ? ATTR_NONE : ATTR_RO)

#define MSDOS_NAME 11 /* maximum name length */
#define MSDOS_LONGNAME 256 /* maximum name length */
#define MSDOS_SLOTS 21  /* max # of slots needed for short and long names */
#define MSDOS_DOT    ".          " /* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT "..         " /* "..", padded to MSDOS_NAME chars */

/* media of boot sector */
#define FAT_VALID_MEDIA(x)	((0xF8 <= (x) && (x) <= 0xFF) || (x) == 0xF0)
#define FAT_FIRST_ENT(s, x)	((MSDOS_SB(s)->fat_bits == 32 ? 0x0FFFFF00 : \
	MSDOS_SB(s)->fat_bits == 16 ? 0xFF00 : 0xF00) | (x))

/* maximum number of clusters */
#define MAX_FAT12 0xFF4
#define MAX_FAT16 0xFFF4
#define MAX_FAT32 0x0FFFFFF6
#define MAX_FAT(s) (MSDOS_SB(s)->fat_bits == 32 ? MAX_FAT32 : \
	MSDOS_SB(s)->fat_bits == 16 ? MAX_FAT16 : MAX_FAT12)

/* bad cluster mark */
#define BAD_FAT12 0xFF7
#define BAD_FAT16 0xFFF7
#define BAD_FAT32 0x0FFFFFF7
#define BAD_FAT(s) (MSDOS_SB(s)->fat_bits == 32 ? BAD_FAT32 : \
	MSDOS_SB(s)->fat_bits == 16 ? BAD_FAT16 : BAD_FAT12)

/* standard EOF */
#define EOF_FAT12 0xFFF
#define EOF_FAT16 0xFFFF
#define EOF_FAT32 0x0FFFFFFF
#define EOF_FAT(s) (MSDOS_SB(s)->fat_bits == 32 ? EOF_FAT32 : \
	MSDOS_SB(s)->fat_bits == 16 ? EOF_FAT16 : EOF_FAT12)

#define FAT_ENT_FREE	(0)
#define FAT_ENT_BAD	(BAD_FAT32)
#define FAT_ENT_EOF	(EOF_FAT32)

#define FAT_FSINFO_SIG1	0x41615252
#define FAT_FSINFO_SIG2	0x61417272
#define IS_FSINFO(x)	(le32_to_cpu((x)->signature1) == FAT_FSINFO_SIG1 \
			 && le32_to_cpu((x)->signature2) == FAT_FSINFO_SIG2)

/*
 * ioctl commands
 */
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct dirent [2])
#define	VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct dirent [2])

/* 
 * vfat shortname flags
 */
#define VFAT_SFN_DISPLAY_LOWER	0x0001 /* convert to lowercase for display */
#define VFAT_SFN_DISPLAY_WIN95	0x0002 /* emulate win95 rule for display */
#define VFAT_SFN_DISPLAY_WINNT	0x0004 /* emulate winnt rule for display */
#define VFAT_SFN_CREATE_WIN95	0x0100 /* emulate win95 rule for create */
#define VFAT_SFN_CREATE_WINNT	0x0200 /* emulate winnt rule for create */

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
	__u8	ignored[3];	/* Boot strap short or near jump */
	__u8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	__u8	sector_size[2];	/* bytes per logical sector */
	__u8	sec_per_clus;	/* sectors/cluster */
	__le16	reserved;	/* reserved sectors */
	__u8	fats;		/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	sectors[2];	/* number of sectors */
	__u8	media;		/* media code */
	__le16	fat_length;	/* sectors/FAT */
	__le16	secs_track;	/* sectors per track */
	__le16	heads;		/* number of heads */
	__le32	hidden;		/* hidden sectors (unused) */
	__le32	total_sect;	/* number of sectors (if sectors == 0) */

	/* The following fields are only used by FAT32 */
	__le32	fat32_length;	/* sectors/FAT */
	__le16	flags;		/* bit 8: fat mirroring, low 4: active fat */
	__u8	version[2];	/* major, minor filesystem version */
	__le32	root_cluster;	/* first cluster in root directory */
	__le16	info_sector;	/* filesystem info sector */
	__le16	backup_boot;	/* backup boot sector */
	__le16	reserved2[6];	/* Unused */
};

struct fat_boot_fsinfo {
	__le32   signature1;	/* 0x41615252L */
	__le32   reserved1[120];	/* Nothing as far as I can tell */
	__le32   signature2;	/* 0x61417272L */
	__le32   free_clusters;	/* Free cluster count.  -1 if unknown */
	__le32   next_cluster;	/* Most recently allocated cluster */
	__le32   reserved2[4];
};

struct msdos_dir_entry {
	__u8	name[8],ext[3];	/* name and extension */
	__u8	attr;		/* attribute bits */
	__u8    lcase;		/* Case for base and extension */
	__u8	ctime_ms;	/* Creation time, milliseconds */
	__le16	ctime;		/* Creation time */
	__le16	cdate;		/* Creation date */
	__le16	adate;		/* Last access date */
	__le16   starthi;	/* High 16 bits of cluster in FAT32 */
	__le16	time,date,start;/* time, date and first cluster */
	__le32	size;		/* file size (in bytes) */
};

/* Up to 13 characters of the name */
struct msdos_dir_slot {
	__u8    id;		/* sequence number for slot */
	__u8    name0_4[10];	/* first 5 characters in name */
	__u8    attr;		/* attribute byte */
	__u8    reserved;	/* always 0 */
	__u8    alias_checksum;	/* checksum for 8.3 alias */
	__u8    name5_10[12];	/* 6 more characters in name */
	__le16   start;		/* starting cluster number, 0 in long slots */
	__u8    name11_12[4];	/* last 2 characters in name */
};

struct vfat_slot_info {
	int long_slots;		       /* number of long slots in filename */
	loff_t longname_offset;	       /* dir offset for longname start */
	loff_t i_pos;		       /* on-disk position of directory entry */
};

#ifdef __KERNEL__

#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/msdos_fs_i.h>
#include <linux/msdos_fs_sb.h>

static inline struct msdos_sb_info *MSDOS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct msdos_inode_info *MSDOS_I(struct inode *inode)
{
	return container_of(inode, struct msdos_inode_info, vfs_inode);
}

static inline void fat16_towchar(wchar_t *dst, const __u8 *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		*dst++ = src[0] | (src[1] << 8);
		src += 2;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

static inline void fatwchar_to16(__u8 *dst, const wchar_t *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		dst[0] = *src & 0x00FF;
		dst[1] = (*src & 0xFF00) >> 8;
		dst += 2;
		src++;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

/* fat/cache.c */
extern int fat_access(struct super_block *sb, int nr, int new_value);
extern int __fat_access(struct super_block *sb, int nr, int new_value);
extern int fat_bmap(struct inode *inode, sector_t sector, sector_t *phys);
extern void fat_cache_inval_inode(struct inode *inode);
extern int fat_get_cluster(struct inode *inode, int cluster,
			   int *fclus, int *dclus);
extern int fat_free(struct inode *inode, int skip);

/* fat/dir.c */
extern struct file_operations fat_dir_operations;
extern int fat_search_long(struct inode *inode, const unsigned char *name,
			   int name_len, int anycase,
			   loff_t *spos, loff_t *lpos);
extern int fat_add_entries(struct inode *dir, int slots, struct buffer_head **bh,
			struct msdos_dir_entry **de, loff_t *i_pos);
extern int fat_new_dir(struct inode *dir, struct inode *parent, int is_vfat);
extern int fat_dir_empty(struct inode *dir);
extern int fat_subdirs(struct inode *dir);
extern int fat_scan(struct inode *dir, const unsigned char *name,
		    struct buffer_head **res_bh,
		    struct msdos_dir_entry **res_de, loff_t *i_pos);

/* fat/file.c */
extern struct file_operations fat_file_operations;
extern struct inode_operations fat_file_inode_operations;
extern int fat_get_block(struct inode *inode, sector_t iblock,
			 struct buffer_head *bh_result, int create);
extern void fat_truncate(struct inode *inode);

/* fat/inode.c */
extern void fat_attach(struct inode *inode, loff_t i_pos);
extern void fat_detach(struct inode *inode);
extern struct inode *fat_iget(struct super_block *sb, loff_t i_pos);
extern struct inode *fat_build_inode(struct super_block *sb,
			struct msdos_dir_entry *de, loff_t i_pos, int *res);
int fat_fill_super(struct super_block *sb, void *data, int silent,
		   struct inode_operations *fs_dir_inode_ops, int isvfat);
extern int fat_notify_change(struct dentry * dentry, struct iattr * attr);

/* fat/misc.c */
extern void fat_fs_panic(struct super_block *s, const char *fmt, ...);
extern void lock_fat(struct super_block *sb);
extern void unlock_fat(struct super_block *sb);
extern void fat_clusters_flush(struct super_block *sb);
extern int fat_add_cluster(struct inode *inode);
extern struct buffer_head *fat_extend_dir(struct inode *inode);
extern int date_dos2unix(unsigned short time, unsigned short date);
extern void fat_date_unix2dos(int unix_date, __le16 *time, __le16 *date);
extern int fat__get_entry(struct inode *dir, loff_t *pos,
			  struct buffer_head **bh,
			  struct msdos_dir_entry **de, loff_t *i_pos);
static __inline__ int fat_get_entry(struct inode *dir, loff_t *pos,
				    struct buffer_head **bh,
				    struct msdos_dir_entry **de, loff_t *i_pos)
{
	/* Fast stuff first */
	if (*bh && *de &&
	    (*de - (struct msdos_dir_entry *)(*bh)->b_data) < MSDOS_SB(dir->i_sb)->dir_per_block - 1) {
		*pos += sizeof(struct msdos_dir_entry);
		(*de)++;
		(*i_pos)++;
		return 0;
	}
	return fat__get_entry(dir, pos, bh, de, i_pos);
}

#endif /* __KERNEL__ */

#endif
