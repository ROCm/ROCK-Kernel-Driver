/*
 * struct.h
 * Structure definitions
 *
 *  Copyright (C) 1997 Régis Duchesne
 *  Copyright (C) 2000 Anton Altaparmakov
 */

/* Necessary forward definition */
struct ntfs_inode;

#ifdef __FreeBSD__
#include <sys/queue.h>
/* Define the struct ntfs_head type */
LIST_HEAD(ntfs_head,ntfs_inode);
#endif

/* which files should be returned from a director listing */
/* only short names, no hidden files */
#define ngt_dos   1
/* only long names, all-uppercase becomes all-lowercase, no hidden files */
#define ngt_nt    2
/* all names except hidden files */
#define ngt_posix 3
/* all entries */
#define ngt_full  4

#ifdef NTFS_IN_LINUX_KERNEL
typedef struct ntfs_sb_info ntfs_volume;
#else
typedef struct _ntfs_volume{
	/* NTFS_SB_INFO_START */
	/* Configuration provided by user at mount time */
	ntfs_uid_t uid;
	ntfs_gid_t gid;
	ntmode_t umask;
        unsigned int nct;
	void *nls_map;
	unsigned int ngt;
	/* Configuration provided by user with ntfstools */
	ntfs_size_t partition_bias;	/* for access to underlying device */
	/* Attribute definitions */
	ntfs_u32 at_standard_information;
	ntfs_u32 at_attribute_list;
	ntfs_u32 at_file_name;
	ntfs_u32 at_volume_version;
	ntfs_u32 at_security_descriptor;
	ntfs_u32 at_volume_name;
	ntfs_u32 at_volume_information;
	ntfs_u32 at_data;
	ntfs_u32 at_index_root;
	ntfs_u32 at_index_allocation;
	ntfs_u32 at_bitmap;
	ntfs_u32 at_symlink; /* aka SYMBOLIC_LINK or REPARSE_POINT */
	/* Data read from the boot file */
	int blocksize;
	int clusterfactor;
	int clustersize;
	int mft_recordsize;
	int mft_clusters_per_record;
	int index_recordsize;
	int index_clusters_per_record;
	int mft_cluster;
	/* data read from special files */
	unsigned char *mft;
	unsigned short *upcase;
	unsigned int upcase_length;
	/* inodes we always hold onto */
	struct ntfs_inode *mft_ino;
	struct ntfs_inode *mftmirr;
	struct ntfs_inode *bitmap;
	/* NTFS_SB_INFO_END */
	union{
		int fd;		/* file descriptor for the tools */
		void *sb;	/* pointer to super block for the kernel */
	}u;
#ifdef __FreeBSD__
	dev_t rdev;
	struct vnode *devvp;
	struct ntfs_head *inode_hash;   /* not really a hash */
#endif
}ntfs_volume;
#endif

typedef struct {
	ntfs_cluster_t cluster;
	ntfs_cluster_t len;
}ntfs_runlist;

typedef struct ntfs_attribute{
	int type;
	ntfs_u16 *name;
	int namelen;
	int attrno;
	int size,allocated,initialized,compsize;
	int compressed,resident,indexed;
	int cengine;
	union{
		void *data;             /* if resident */
		struct {
			ntfs_runlist *runlist;
			int len;
		}r;
	}d;
}ntfs_attribute;

/* Structure to define IO to user buffer. do_read means that
   the destination has to be written using fn_put, do_write means
   that the destination has to read using fn_get. So, do_read is
   from a user's point of view, while put and get are from the driver's
   point of view. The first argument is always the destination of the IO
*/
#ifdef NTFS_IN_LINUX_KERNEL
typedef struct ntfs_inode_info ntfs_inode;
#else
typedef struct ntfs_inode{
	ntfs_volume *vol;
	/* NTFS_INODE_INFO_START */
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
	/* NTFS_INODE_INFO_END */
#ifdef __FreeBSD__
	struct vnode *vp;
	LIST_ENTRY(ntfs_inode) h_next;
#endif
}ntfs_inode;
#endif

typedef struct ntfs_io{
	int do_read;
	void (*fn_put)(struct ntfs_io *dest, void *buf, ntfs_size_t);
	void (*fn_get)(void *buf, struct ntfs_io *src, ntfs_size_t len);
	void *param;
	int size;
}ntfs_io;

#if 0
typedef struct {
	ntfs_volume *vol;
	ntfs_inode *ino;
	int type;
	char *name;
	int mftno;
	int start_vcn;
} ntfs_attrlist_item;
#endif
