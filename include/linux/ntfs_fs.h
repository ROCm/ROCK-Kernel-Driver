/*
 * ntfs_fs.h - Defines for NTFS Linux kernel driver. Part of the Linux-NTFS
 *	       project.
 *
 * Copyright (c) 2001 Anton Altaparmakov.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_FS_H
#define _LINUX_NTFS_FS_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ntfs_layout.h>
#include <linux/vmalloc.h>	/* For __vmalloc() and PAGE_KERNEL. */
#include <linux/nls.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

typedef enum {
	NTFS_BLOCK_SIZE		= 512,
	NTFS_BLOCK_SIZE_BITS	= 9,
	NTFS_SB_MAGIC		= 0x5346544e,	/* 'NTFS' */
	NTFS_MAX_NAME_LEN	= 255,
} NTFS_CONSTANTS;

typedef enum {
	FALSE = 0,
	TRUE = 1
} BOOL;

typedef enum {
	LCN_HOLE		= -1,
	LCN_RL_NOT_MAPPED	= -2,
	LCN_ENOENT		= -3,
	LCN_EINVAL		= -4,
} LCN_SPECIAL_VALUES;

typedef enum {
	CASE_SENSITIVE = 0,
	IGNORE_CASE = 1,
} IGNORE_CASE_BOOL;

/*
 * Defined bits for the state field in the ntfs_inode_info structure.
 * (f) = files only, (d) = directories only
 */
typedef enum {
	NI_Dirty,		/* 1: Mft record needs to be written to disk. */
	NI_AttrList,		/* 1: Mft record contains an attribute list. */
	NI_NonResident,		/* 1: Unnamed data attr is non-resident (f).
				   1: $I30 index alloc attr is present (d). */
	NI_Compressed,		/* 1: Unnamed data attr is compressed (f). */
	NI_Encrypted,		/* 1: Unnamed data attr is encrypted (f). */
	NI_BmpNonResident,	/* 1: $I30 bitmap attr is non resident (d). */
} ntfs_inode_state_bits;

/*
 * NOTE: We should be adding dirty mft records to a list somewhere and they
 * should be independent of the (ntfs/vfs) inode structure so that an inode can
 * be removed but the record can be left dirty for syncing later.
 */

#define NInoDirty(n_ino)	  test_bit(NI_Dirty, &(n_ino)->state)
#define NInoSetDirty(n_ino)	  set_bit(NI_Dirty, &(n_ino)->state)
#define NInoClearDirty(n_ino)	  clear_bit(NI_Dirty, &(n_ino)->state)

#define NInoAttrList(n_ino)	  test_bit(NI_AttrList, &(n_ino)->state)
#define NInoNonResident(n_ino)	  test_bit(NI_NonResident, &(n_ino)->state)
#define NInoIndexAllocPresent(n_ino) test_bit(NI_NonResident, &(n_ino)->state)
#define NInoCompressed(n_ino)	  test_bit(NI_Compressed, &(n_ino)->state)
#define NInoEncrypted(n_ino)	  test_bit(NI_Encrypted, &(n_ino)->state)
#define NInoBmpNonResident(n_ino) test_bit(NI_Encrypted, &(n_ino)->state)

/* Global variables. */

/* Slab cache of Unicode name strings (from super.c). */
extern kmem_cache_t *ntfs_name_cache;

/* The little endian Unicode string $I30 as a global constant. */
extern const uchar_t $I30[5];

/* The various operations structs defined throughout the driver files. */
extern struct super_operations ntfs_sops;
extern struct file_operations ntfs_file_ops;
extern struct inode_operations ntfs_file_inode_ops;
extern struct address_space_operations ntfs_file_aops;
extern struct file_operations ntfs_dir_ops;
extern struct inode_operations ntfs_dir_inode_ops;
extern struct address_space_operations ntfs_dir_aops;
extern struct file_operations ntfs_empty_file_ops;
extern struct inode_operations ntfs_empty_inode_ops;
extern struct address_space_operations ntfs_empty_aops;
extern struct address_space_operations ntfs_mftbmp_aops;

/* The classical max and min macros. */
#ifndef max
#define max(a, b)	((a) >= (b) ? (a): (b))
#endif

#ifndef min
#define min(a, b)	((a) <= (b) ? (a): (b))
#endif

/* Generic macro to convert pointers to values for comparison purposes. */
#ifndef p2n
#define p2n(p)          ((ptrdiff_t)((ptrdiff_t*)(p)))
#endif

/**
 * vmalloc_nofs - allocate any pages but don't allow calls into fs layer
 * @size:	number of bytes to allocate
 *
 * Allocate any pages but don't allow calls into fs layer.
 */
static inline void *vmalloc_nofs(unsigned long size)
{
	return __vmalloc(size, GFP_NOFS | __GFP_HIGHMEM, PAGE_KERNEL);
}

/**
 * NTFS_SB - return the ntfs super block given a vfs super block
 * @sb:		VFS super block
 *
 * NTFS_SB() returns the ntfs super block associated with the VFS super block
 * @sb. This function is here in case it is decided to get rid of the big union
 * in the struct super_block definition in include/linux/fs.h in favour of using
 * the generic_sbp field (or whatever).
 */
static inline struct ntfs_sb_info *NTFS_SB(struct super_block *sb)
{
	return &sb->u.ntfs_sb;
}

/**
 * NTFS_I - return the ntfs inode given a vfs inode
 * @inode:	VFS inode
 *
 * NTFS_I() returns the ntfs inode associated with the VFS @inode. This
 * function is here in case it is decided to get rid of the big union in the
 * struct inode definition in include/linux/fs.h in favour of using the
 * generic_ip field (or whatever).
 */
static inline struct ntfs_inode_info *NTFS_I(struct inode *inode)
{
	return &inode->u.ntfs_i;
}

#if 0 /* Fool kernel-doc since it doesn't do macros yet */
/**
 * ntfs_debug - write a debug level message to syslog
 * @f:		a printf format string containing the message
 * @...:	the variables to substitute into @f
 *
 * ntfs_debug() writes a DEBUG level message to the syslog but only if the
 * driver was compiled with -DDEBUG. Otherwise, the call turns into a NOP.
 */
static void ntfs_debug(const char *f, ...);
#endif
#ifdef DEBUG
#define ntfs_debug(f, a...)						\
	do {								\
		printk(KERN_DEBUG "NTFS-fs DEBUG (%s, %d): %s: ",	\
				__FILE__, __LINE__, __FUNCTION__);	\
		printk(f, ##a);						\
	} while (0)
#else	/* !DEBUG */
#define ntfs_debug(f, a...)	do {} while (0)
#endif	/* !DEBUG */

/*
 * Signed endianness conversion defines.
 */
#define sle16_to_cpu(x)		((__s16)__le16_to_cpu((__s16)(x)))
#define sle32_to_cpu(x)		((__s32)__le32_to_cpu((__s32)(x)))
#define sle64_to_cpu(x)		((__s64)__le64_to_cpu((__s64)(x)))

#define sle16_to_cpup(x)	((__s16)__le16_to_cpu(*(__s16*)(x)))
#define sle32_to_cpup(x)	((__s32)__le32_to_cpu(*(__s32*)(x)))
#define sle64_to_cpup(x)	((__s64)__le64_to_cpu(*(__s64*)(x)))

#define cpu_to_sle16(x)		((__s16)__cpu_to_le16((__s16)(x)))
#define cpu_to_sle32(x)		((__s32)__cpu_to_le32((__s32)(x)))
#define cpu_to_sle64(x)		((__s64)__cpu_to_le64((__s64)(x)))

#define cpu_to_sle16p(x)	((__s16)__cpu_to_le16(*(__s16*)(x)))
#define cpu_to_sle32p(x)	((__s32)__cpu_to_le32(*(__s32*)(x)))
#define cpu_to_sle64p(x)	((__s64)__cpu_to_le64(*(__s64*)(x)))

/**
 * ntfs_unmap_page - release a page that was mapped using ntfs_map_page()
 * @page:	the page to release
 *
 * Unpin, unmap and release a page that was obtained from ntfs_map_page().
 */
static inline void ntfs_unmap_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

/**
 * ntfs_map_page - map a page into accessible memory, reading it if necessary
 * @mapping:	address space for which to obtain the page
 * @index:	index into the page cache for @mapping of the page to map
 *
 * Read a page from the page cache of the address space @mapping at position
 * @index, where @index is in units of PAGE_CACHE_SIZE, and not in bytes.
 *
 * If the page is not in memory it is loaded from disk first using the readpage
 * method defined in the address space operations of @mapping and the page is
 * added to the page cache of @mapping in the process.
 *
 * If the page is in high memory it is mapped into memory directly addressible
 * by the kernel.
 *
 * Finally the page count is incremented, thus pinning the page into place.
 *
 * The above means that page_address(page) can be used on all pages obtained
 * with ntfs_map_page() to get the kernel virtual address of the page.
 *
 * When finished with the page, the caller has to call ntfs_unmap_page() to
 * unpin, unmap and release the page.
 *
 * Note this does not grant exclusive access. If such is desired, the caller
 * must provide it independently of the ntfs_{un}map_page() calls by using
 * a {rw_}semaphore or other means of serialization. A spin lock cannot be
 * used as ntfs_map_page() can block.
 *
 * The unlocked and uptodate page is returned on success or an encoded error
 * on failure. Caller has to test for error using the IS_ERR() macro on the
 * return value. If that evaluates to TRUE, the negative error code can be
 * obtained using PTR_ERR() on the return value of ntfs_map_page().
 */
static inline struct page *ntfs_map_page(struct address_space *mapping,
		unsigned long index)
{
	struct page *page = read_cache_page(mapping, index,
			(filler_t*)mapping->a_ops->readpage, NULL);

	if (!IS_ERR(page)) {
		wait_on_page(page);
		kmap(page);
		if (Page_Uptodate(page) && !PageError(page))
			return page;
		ntfs_unmap_page(page);
		return ERR_PTR(-EIO);
	}
	return page;
}

/**
 * attr_search_context - used in attribute search functions
 * @mrec:	buffer containing mft record to search
 * @attr:	attribute record in @mrec where to begin/continue search
 * @is_first:	if true lookup_attr() begins search with @attr, else after @attr
 *
 * Structure must be initialized to zero before the first call to one of the
 * attribute search functions. Initialize @mrec to point to the mft record to
 * search, and @attr to point to the first attribute within @mrec (not necessary
 * if calling the _first() functions), and set @is_first to TRUE (not necessary
 * if calling the _first() functions).
 *
 * If @is_first is TRUE, the search begins with @attr. If @is_first is FALSE,
 * the search begins after @attr. This is so that, after the first call to one
 * of the search attribute functions, we can call the function again, without
 * any modification of the search context, to automagically get the next
 * matching attribute.
 */
typedef struct {
	MFT_RECORD *mrec;
	ATTR_RECORD *attr;
	BOOL is_first;
} attr_search_context;

/* Declarations of functions and global variables. */

/* From fs/ntfs/aops.c */
extern int ntfs_file_get_block(struct inode *vfs_ino, const long blk,
		struct buffer_head *bh, const int create);

/* From fs/ntfs/compaops.c */
extern int ntfs_file_read_compressed_block(struct page *page);

/* From fs/ntfs/super.c */
#define default_upcase_len 0x10000
extern wchar_t *default_upcase;
extern unsigned long ntfs_nr_upcase_users;
extern unsigned long ntfs_nr_mounts;
extern struct semaphore ntfs_lock;

/* From fs/ntfs/mst.c */
extern inline void __post_read_mst_fixup(NTFS_RECORD *b, const __u32 size);
extern int post_read_mst_fixup(NTFS_RECORD *b, const __u32 size);
extern int pre_write_mst_fixup(NTFS_RECORD *b, const __u32 size);

/* From fs/ntfs/time.c */
extern inline __s64 utc2ntfs(const time_t time);
extern inline __s64 get_current_ntfs_time(void);
extern inline time_t ntfs2utc(const __s64 time);

/* From fs/ntfs/debug.c */
void ntfs_warning(const struct super_block *sb, const char *fmt, ...);
void ntfs_error(const struct super_block *sb, const char *fmt, ...);

/* From fs/ntfs/inode.c */
void ntfs_read_inode(struct inode *vfs_ino);
void ntfs_read_inode_mount(struct inode *vfs_ino);
void ntfs_dirty_inode(struct inode *vfs_ino);
void ntfs_clear_inode(struct inode *vfs_ino);

/* From fs/ntfs/dir.c */
__u64 ntfs_lookup_ino_by_name(struct inode *dir_ino, const uchar_t *uname,
		const int uname_len);

/* From fs/ntfs/attrib.c */
run_list *decompress_mapping_pairs(const ATTR_RECORD *attr, run_list *run_list);
LCN vcn_to_lcn(const run_list *rl, const VCN vcn);
BOOL find_attr(const ATTR_TYPES type, const uchar_t *name, const __u32 name_len,
		const IGNORE_CASE_BOOL ic, const uchar_t *upcase,
		const __u32 upcase_len, const __u8 *val, const __u32 val_len,
		attr_search_context *ctx);
extern inline BOOL find_first_attr(const ATTR_TYPES type, const uchar_t *name,
		const __u32 name_len, const IGNORE_CASE_BOOL ic,
		const uchar_t *upcase, const __u32 upcase_len,
		const __u8 *val, const __u32 val_len, attr_search_context *ctx);

/* From fs/ntfs/mft.c */
int format_mft_record(struct inode *vfs_ino, MFT_RECORD *m);
int format_mft_record2(struct super_block *vfs_sb, const unsigned long inum,
		MFT_RECORD *m);
MFT_RECORD *map_mft_record_for_read(struct inode *vfs_ino);
MFT_RECORD *map_mft_record_for_read2(struct super_block *vfs_sb,
		const unsigned long inum, struct inode **vfs_ino);
MFT_RECORD *map_mft_record_for_write(struct inode *vfs_ino);
MFT_RECORD *map_mft_record_for_write2(struct super_block *vfs_sb,
		const unsigned long inum, struct inode **vfs_ino);
void unmap_mft_record_from_read(struct inode *vfs_ino);
void unmap_mft_record_from_write(struct inode *vfs_ino);

/* From fs/ntfs/unistr.c */
BOOL ntfs_are_names_equal(const uchar_t *s1, size_t s1_len,
		     const uchar_t *s2, size_t s2_len,
		     const IGNORE_CASE_BOOL ic,
		     const uchar_t *upcase, const __u32 upcase_size);
int ntfs_collate_names(const uchar_t *name1, const __u32 name1_len,
		const uchar_t *name2, const __u32 name2_len,
		const int err_val, const IGNORE_CASE_BOOL ic,
		const uchar_t *upcase, const __u32 upcase_len);
int ntfs_ucsncmp(const uchar_t *s1, const uchar_t *s2, size_t n);
int ntfs_ucsncasecmp(const uchar_t *s1, const uchar_t *s2, size_t n,
		     const uchar_t *upcase, const __u32 upcase_size);
void ntfs_upcase_name(uchar_t *name, __u32 name_len, const uchar_t *upcase,
		const __u32 upcase_len);
void ntfs_file_upcase_value(FILE_NAME_ATTR *file_name_attr,
		const uchar_t *upcase, const __u32 upcase_len);
int ntfs_file_compare_values(FILE_NAME_ATTR *file_name_attr1,
		FILE_NAME_ATTR *file_name_attr2,
		const int err_val, const IGNORE_CASE_BOOL ic,
		const uchar_t *upcase, const __u32 upcase_len);
int ntfs_nlstoucs(const struct ntfs_sb_info *vol, const char *ins,
		const int ins_len, uchar_t **outs);
int ntfs_ucstonls(const struct ntfs_sb_info *vol, const uchar_t *ins,
		const int ins_len, unsigned char **outs, int outs_len);

/* From fs/ntfs/upcase.c */
uchar_t *generate_default_upcase(void);

#endif /* _LINUX_NTFS_FS_H */

