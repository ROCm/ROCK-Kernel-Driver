#ifndef _LINUX_NTFS_FS_I_H
#define _LINUX_NTFS_FS_I_H

/* Forward declarations, to keep number of mutual includes low */
struct ntfs_attribute;
struct ntfs_sb_info;

/* Duplicate definitions from ntfs/ntfstypes.h */
#ifndef NTFS_INTEGRAL_TYPES
#define NTFS_INTEGRAL_TYPES
typedef u8  ntfs_u8;
typedef u16 ntfs_u16;
typedef u32 ntfs_u32;
typedef u64 ntfs_u64;
typedef s8  ntfs_s8;
typedef s16 ntfs_s16;
typedef s32 ntfs_s32;
typedef s64 ntfs_s64;
#endif

#ifndef NTMODE_T
#define NTMODE_T
typedef __kernel_mode_t ntmode_t;
#endif
#ifndef NTFS_UID_T
#define NTFS_UID_T
typedef uid_t ntfs_uid_t;
#endif
#ifndef NTFS_GID_T
#define NTFS_GID_T
typedef gid_t ntfs_gid_t;
#endif
#ifndef NTFS_SIZE_T
#define NTFS_SIZE_T
typedef __kernel_size_t ntfs_size_t;
#endif
#ifndef NTFS_TIME_T
#define NTFS_TIME_T
typedef __kernel_time_t ntfs_time_t;
#endif

/* unicode character type */
#ifndef NTFS_WCHAR_T
#define NTFS_WCHAR_T
typedef unsigned short     ntfs_wchar_t;
#endif
/* file offset */
#ifndef NTFS_OFFSET_T
#define NTFS_OFFSET_T
typedef unsigned long long ntfs_offset_t;
#endif
/* UTC */
#ifndef NTFS_TIME64_T
#define NTFS_TIME64_T
typedef unsigned long long ntfs_time64_t;
#endif
/* This is really unsigned long long. So we support only volumes up to 2 TB */
#ifndef NTFS_CLUSTER_T
#define NTFS_CLUSTER_T
typedef unsigned int ntfs_cluster_t;
#endif

/* Definition of NTFS in-memory inode structure */
struct ntfs_inode_info{
	unsigned long mmu_private;
	struct ntfs_sb_info *vol;
	int i_number;                /* should be really 48 bits */
	unsigned sequence_number;
	unsigned char* attr;         /* array of the attributes */
	int attr_count;              /* size of attrs[] */
	struct ntfs_attribute *attrs;
	int record_count;            /* size of records[] */
	/* array of the record numbers of the MFT 
	   whose attributes have been inserted in the inode */
	int *records;
	union{
		struct{
			int recordsize;
			int clusters_per_record;
		}index;
	} u;	
};

#endif
