#ifndef __UDF_DECL_H
#define __UDF_DECL_H

#include <linux/udf_167.h>
#include <linux/udf_udf.h>
#include "udfend.h"

#include <linux/udf_fs.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fs.h>

#if !defined(CONFIG_UDF_FS) && !defined(CONFIG_UDF_FS_MODULE)
#define CONFIG_UDF_FS_MODULE
#include <linux/udf_fs_sb.h>
#include <linux/udf_fs_i.h>
#endif

#define udf_fixed_to_variable(x) ( ( ( (x) >> 5 ) * 39 ) + ( (x) & 0x0000001F ) )
#define udf_variable_to_fixed(x) ( ( ( (x) / 39 ) << 5 ) + ( (x) % 39 ) )

#define CURRENT_UTIME	(xtime.tv_usec)

#define udf_file_entry_alloc_offset(inode)\
	((UDF_I_EXTENDED_FE(inode) ?\
		sizeof(struct ExtendedFileEntry) :\
		sizeof(struct FileEntry)) + UDF_I_LENEATTR(inode))

#define udf_ext0_offset(inode)\
	(UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB ?\
		udf_file_entry_alloc_offset(inode) : 0)

#define udf_get_lb_pblock(sb,loc,offset) udf_get_pblock((sb), (loc).logicalBlockNum, (loc).partitionReferenceNum, (offset))

#else

#include <sys/types.h>

#endif /* __KERNEL__ */



#ifdef __KERNEL__

struct dentry;
struct inode;
struct task_struct;
struct buffer_head;
struct super_block;

extern struct inode_operations udf_dir_inode_operations;
extern struct file_operations udf_dir_operations;
extern struct inode_operations udf_file_inode_operations;
extern struct file_operations udf_file_operations;
extern struct address_space_operations udf_aops;
extern struct address_space_operations udf_adinicb_aops;
extern struct address_space_operations udf_symlink_aops;

struct udf_fileident_bh
{
	struct buffer_head *sbh;
	struct buffer_head *ebh;
	int soffset;
	int eoffset;
};

#endif /* __KERNEL__ */

struct udf_directory_record
{
	Uint32	d_parent;
	Uint32	d_inode;
	Uint32	d_name[255];
};


struct udf_vds_record
{
	Uint32 block;
	Uint32 volDescSeqNum;
};

struct ktm
{
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_isdst;
};

struct ustr
{
	Uint8 u_cmpID;
	Uint8 u_name[UDF_NAME_LEN];
	Uint8 u_len;
	Uint8 padding;
	unsigned long u_hash;
};

#ifdef __KERNEL__

/* super.c */
extern void udf_error(struct super_block *, const char *, const char *, ...);
extern void udf_warning(struct super_block *, const char *, const char *, ...);

/* namei.c */
extern int udf_write_fi(struct inode *inode, struct FileIdentDesc *, struct FileIdentDesc *, struct udf_fileident_bh *, Uint8 *, Uint8 *);

/* file.c */
extern int udf_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

/* inode.c */
extern struct inode *udf_iget(struct super_block *, lb_addr);
extern int udf_sync_inode(struct inode *);
extern void udf_expand_file_adinicb(struct inode *, int, int *);
extern struct buffer_head * udf_expand_dir_adinicb(struct inode *, int *, int *);
extern struct buffer_head * udf_getblk(struct inode *, long, int, int *);
extern struct buffer_head * udf_bread(struct inode *, int, int, int *);
extern void udf_truncate(struct inode *);
extern void udf_read_inode(struct inode *);
extern void udf_put_inode(struct inode *);
extern void udf_delete_inode(struct inode *);
extern void udf_write_inode(struct inode *, int);
extern long udf_block_map(struct inode *, long);
extern Sint8 inode_bmap(struct inode *, int, lb_addr *, Uint32 *, lb_addr *, Uint32 *, Uint32 *, struct buffer_head **);
extern Sint8 udf_add_aext(struct inode *, lb_addr *, int *, lb_addr, Uint32, struct buffer_head **, int);
extern Sint8 udf_write_aext(struct inode *, lb_addr, int *, lb_addr, Uint32, struct buffer_head *, int);
extern Sint8 udf_insert_aext(struct inode *, lb_addr, int, lb_addr, Uint32, struct buffer_head *);
extern Sint8 udf_delete_aext(struct inode *, lb_addr, int, lb_addr, Uint32, struct buffer_head *);
extern Sint8 udf_next_aext(struct inode *, lb_addr *, int *, lb_addr *, Uint32 *, struct buffer_head **, int);
extern Sint8 udf_current_aext(struct inode *, lb_addr *, int *, lb_addr *, Uint32 *, struct buffer_head **, int);
extern void udf_discard_prealloc(struct inode *);

/* misc.c */
extern int udf_read_tagged_data(char *, int size, int fd, int block, int partref);
extern struct buffer_head *udf_tgetblk(struct super_block *, int);
extern struct buffer_head *udf_tread(struct super_block *, int);
extern struct GenericAttrFormat *udf_add_extendedattr(struct inode *, Uint32, Uint32, Uint8, struct buffer_head **);
extern struct GenericAttrFormat *udf_get_extendedattr(struct inode *, Uint32, Uint8, struct buffer_head **);
extern struct buffer_head *udf_read_tagged(struct super_block *, Uint32, Uint32, Uint16 *);
extern struct buffer_head *udf_read_ptagged(struct super_block *, lb_addr, Uint32, Uint16 *);
extern struct buffer_head *udf_read_untagged(struct super_block *, Uint32, Uint32);
extern void udf_release_data(struct buffer_head *);

/* lowlevel.c */
extern unsigned int udf_get_last_session(struct super_block *);
extern unsigned long udf_get_last_block(struct super_block *);

/* partition.c */
extern Uint32 udf_get_pblock(struct super_block *, Uint32, Uint16, Uint32);
extern Uint32 udf_get_pblock_virt15(struct super_block *, Uint32, Uint16, Uint32);
extern Uint32 udf_get_pblock_virt20(struct super_block *, Uint32, Uint16, Uint32);
extern Uint32 udf_get_pblock_spar15(struct super_block *, Uint32, Uint16, Uint32);
extern int udf_relocate_blocks(struct super_block *, long, long *);

/* unicode.c */
extern int udf_get_filename(struct super_block *, Uint8 *, Uint8 *, int);

/* ialloc.c */
extern void udf_free_inode(struct inode *);
extern struct inode * udf_new_inode (struct inode *, int, int *);

/* truncate.c */
extern void udf_truncate_extents(struct inode *);

/* balloc.c */
extern void udf_free_blocks(struct super_block *, struct inode *, lb_addr, Uint32, Uint32);
extern int udf_prealloc_blocks(struct super_block *, struct inode *, Uint16, Uint32, Uint32);
extern int udf_new_block(struct super_block *, struct inode *, Uint16, Uint32, int *);

/* fsync.c */
extern int udf_fsync_file(struct file *, struct dentry *, int);
extern int udf_fsync_inode(struct inode *, int);

/* directory.c */
extern Uint8 * udf_filead_read(struct inode *, Uint8 *, Uint8, lb_addr, int *, int *, struct buffer_head **, int *);
extern struct FileIdentDesc * udf_fileident_read(struct inode *, loff_t *, struct udf_fileident_bh *, struct FileIdentDesc *, lb_addr *, Uint32 *, lb_addr *, Uint32 *, Uint32 *, struct buffer_head **);

#endif /* __KERNEL__ */

/* Miscellaneous UDF Prototypes */

/* unicode.c */
extern int udf_ustr_to_dchars(Uint8 *, const struct ustr *, int);
extern int udf_ustr_to_char(Uint8 *, const struct ustr *, int);
extern int udf_ustr_to_dstring(dstring *, const struct ustr *, int);
extern int udf_dchars_to_ustr(struct ustr *, const Uint8 *, int);
extern int udf_char_to_ustr(struct ustr *, const Uint8 *, int);
extern int udf_dstring_to_ustr(struct ustr *, const dstring *, int);
extern int udf_translate_to_linux(Uint8 *, Uint8 *, int, Uint8 *, int);
extern int udf_build_ustr(struct ustr *, dstring *, int);
extern int udf_build_ustr_exact(struct ustr *, dstring *, int);
extern int udf_CS0toUTF8(struct ustr *, struct ustr *);
extern int udf_UTF8toCS0(dstring *, struct ustr *, int);
#ifdef __KERNEL__
extern int udf_CS0toNLS(struct nls_table *, struct ustr *, struct ustr *);
extern int udf_NLStoCS0(struct nls_table *, dstring *, struct ustr *, int);
#endif

/* crc.c */
extern Uint16 udf_crc(Uint8 *, Uint32, Uint16);

/* misc.c */
extern Uint32 udf64_low32(Uint64);
extern Uint32 udf64_high32(Uint64);
extern void udf_update_tag(char *, int);
extern void udf_new_tag(char *, Uint16, Uint16, Uint16, Uint32, int);

/* udftime.c */
extern time_t *udf_stamp_to_time(time_t *, long *, timestamp);
extern timestamp *udf_time_to_stamp(timestamp *, time_t, long);
extern time_t udf_converttime (struct ktm *);

/* directory.c */
extern struct FileIdentDesc * udf_get_fileident(void * buffer, int bufsize, int * offset);
extern extent_ad * udf_get_fileextent(void * buffer, int bufsize, int * offset);
extern long_ad * udf_get_filelongad(void * buffer, int bufsize, int * offset, int);
extern short_ad * udf_get_fileshortad(void * buffer, int bufsize, int * offset, int);
extern Uint8 * udf_get_filead(struct FileEntry *, Uint8 *, int, int, int, int *);

#endif /* __UDF_DECL_H */
