#ifndef _LINUX_NTFS_FS_SB_H
#define _LINUX_NTFS_FS_SB_H

/* 2-byte Unicode character type. */
typedef __u16 uchar_t;

/*
 * The NTFS in memory super block structure.
 */
struct ntfs_sb_info {
	/*
	 * FIXME: Reorder to have commonly used together element within the
	 * same cache line, aiming at a cache line size of 32 bytes. Aim for
	 * 64 bytes for less commonly used together elements. Put most commonly
	 * used elements to front of structure. Obviously do this only when the
	 * structure has stabilized... (AIA)
	 */
	/* Device specifics. */
	struct super_block *sb;		/* Pointer back to the super_block,
					   so we don't have to get the offset
					   every time. */
	LCN nr_blocks;			/* Number of NTFS_BLOCK_SIZE bytes
					   sized blocks on the device. */
	/* Configuration provided by user at mount time. */
	uid_t uid;			/* uid that files will be mounted as. */
	gid_t gid;			/* gid that files will be mounted as. */
	mode_t fmask;			/* The mask for file permissions. */
	mode_t dmask;			/* The mask for directory
					   permissions. */
	__u8 mft_zone_multiplier;	/* Initial mft zone multiplier. */
	__u8 on_errors;			/* What to do on file system errors. */
	/* NTFS bootsector provided information. */
	__u16 sector_size;		/* in bytes */
	__u8 sector_size_bits;		/* log2(sector_size) */
	__u32 cluster_size;		/* in bytes */
	__u32 cluster_size_mask;	/* cluster_size - 1 */
	__u8 cluster_size_bits;		/* log2(cluster_size) */
	__u32 mft_record_size;		/* in bytes */
	__u32 mft_record_size_mask;	/* mft_record_size - 1 */
	__u8 mft_record_size_bits;	/* log2(mft_record_size) */
	__u32 index_record_size;	/* in bytes */
	__u32 index_record_size_mask;	/* index_record_size - 1 */
	__u8 index_record_size_bits;	/* log2(index_record_size) */
	union {
		LCN nr_clusters;	/* Volume size in clusters. */
		LCN nr_lcn_bits;	/* Number of bits in lcn bitmap. */
	};
	LCN mft_lcn;			/* Cluster location of mft data. */
	LCN mftmirr_lcn;		/* Cluster location of copy of mft. */
	__u64 serial_no;		/* The volume serial number. */
	/* Mount specific NTFS information. */
	__u32 upcase_len;		/* Number of entries in upcase[]. */
	uchar_t *upcase;		/* The upcase table. */
	LCN mft_zone_start;		/* First cluster of the mft zone. */
	LCN mft_zone_end;		/* First cluster beyond the mft zone. */
	struct inode *mft_ino;		/* The VFS inode of $MFT. */
	struct rw_semaphore mftbmp_lock; /* Lock for serializing accesses to the
					    mft record bitmap ($MFT/$BITMAP). */
	union {
		__s64 nr_mft_records;	/* Number of records in the mft. */
		__s64 nr_mft_bits;	/* Number of bits in mft bitmap. */
	};
	struct address_space mftbmp_mapping; /* Page cache for $MFT/$BITMAP. */
	run_list *mftbmp_rl;		/* Run list for $MFT/$BITMAP. */
	struct inode *mftmirr_ino;	/* The VFS inode of $MFTMirr. */
	struct inode *lcnbmp_ino;	/* The VFS inode of $Bitmap. */
	struct rw_semaphore lcnbmp_lock; /* Lock for serializing accesses to the
					    cluster bitmap ($Bitmap/$DATA). */
	struct inode *vol_ino;		/* The VFS inode of $Volume. */
	unsigned long vol_flags;	/* Volume flags (VOLUME_*). */
	__u8 major_ver;			/* Ntfs major version of volume. */
	__u8 minor_ver;			/* Ntfs minor version of volume. */
	struct inode *root_ino;		/* The VFS inode of the root
					   directory. */
	struct inode *secure_ino;	/* The VFS inode of $Secure (NTFS3.0+
					   only, otherwise NULL). */
	struct nls_table *nls_map;
};

#endif /* _LINUX_NTFS_FS_SB_H */

