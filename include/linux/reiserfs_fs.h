/*
 * Copyright 1996, 1997, 1998 Hans Reiser, see reiserfs/README for licensing and copyright details
 */

				/* this file has an amazingly stupid
                                   name, yura please fix it to be
                                   reiserfs.h, and merge all the rest
                                   of our .h files that are in this
                                   directory into it.  */


#ifndef _LINUX_REISER_FS_H
#define _LINUX_REISER_FS_H


#include <linux/types.h>
#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/tqueue.h>
#endif

/*
 *  include/linux/reiser_fs.h
 *
 *  Reiser File System constants and structures
 *
 */

/* in reading the #defines, it may help to understand that they employ
   the following abbreviations:

   B = Buffer
   I = Item header
   H = Height within the tree (should be changed to LEV)
   N = Number of the item in the node
   STAT = stat data
   DEH = Directory Entry Header
   EC = Entry Count
   E = Entry number
   UL = Unsigned Long
   BLKH = BLocK Header
   UNFM = UNForMatted node
   DC = Disk Child
   P = Path

   These #defines are named by concatenating these abbreviations,
   where first comes the arguments, and last comes the return value,
   of the macro.

*/

				/* Vladimir, what is the story with
                                   new_get_new_buffer nowadays?  I
                                   want a complete explanation written
                                   here. */

/* NEW_GET_NEW_BUFFER will try to allocate new blocks better */
/*#define NEW_GET_NEW_BUFFER*/
#define OLD_GET_NEW_BUFFER

				/* Vladimir, what about this one too? */
/* if this is undefined, all inode changes get into stat data immediately, if it can be found in RAM */
#define DIRTY_LATER

/* enable journalling */
#define ENABLE_JOURNAL

#ifdef __KERNEL__

/* #define REISERFS_CHECK */

#define REISERFS_PREALLOCATE
#endif
#define PREALLOCATION_SIZE 8

/* if this is undefined, all inode changes get into stat data
   immediately, if it can be found in RAM */
#define DIRTY_LATER


/*#define READ_LOCK_REISERFS*/


/* n must be power of 2 */
#define _ROUND_UP(x,n) (((x)+(n)-1u) & ~((n)-1u))

// to be ok for alpha and others we have to align structures to 8 byte
// boundary.
// FIXME: do not change 4 by anything else: there is code which relies on that
				/* what 4? -Hans */
#define ROUND_UP(x) _ROUND_UP(x,8LL)

/* debug levels.  Right now, CONFIG_REISERFS_CHECK means print all debug
** messages.
*/
#define REISERFS_DEBUG_CODE 5 /* extra messages to help find/debug errors */ 

/*
 * Disk Data Structures
 */

/***************************************************************************/
/*                             SUPER BLOCK                                 */
/***************************************************************************/

/*
 * Structure of super block on disk, a version of which in RAM is often accessed as s->u.reiserfs_sb.s_rs
 * the version in RAM is part of a larger structure containing fields never written to disk.
 */

				/* used by gcc */
#define REISERFS_SUPER_MAGIC 0x52654973
				/* used by file system utilities that
                                   look at the superblock, etc. */
#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING "ReIsEr2Fs"

extern inline int is_reiserfs_magic_string (struct reiserfs_super_block * rs)
{
    return (!strncmp (rs->s_magic, REISERFS_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_SUPER_MAGIC_STRING)) ||
	    !strncmp (rs->s_magic, REISER2FS_SUPER_MAGIC_STRING, 
		      strlen ( REISER2FS_SUPER_MAGIC_STRING)));
}

				/* ReiserFS leaves the first 64k unused,
                                   so that partition labels have enough
                                   space.  If someone wants to write a
                                   fancy bootloader that needs more than
                                   64k, let us know, and this will be
                                   increased in size.  This number must
                                   be larger than than the largest block
                                   size on any platform, or code will
                                   break.  -Hans */
#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)
#define REISERFS_FIRST_BLOCK unused_define

/* the spot for the super in versions 3.5 - 3.5.10 (inclusive) */
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES (8 * 1024)


// reiserfs internal error code (used by search_by_key adn fix_nodes))
#define CARRY_ON      0
#define REPEAT_SEARCH -1
#define IO_ERROR      -2
#define NO_DISK_SPACE -3
#define NO_BALANCING_NEEDED  (-4)
#define NO_MORE_UNUSED_CONTIGUOUS_BLOCKS (-5)

//#define SCHEDULE_OCCURRED  	1
//#define PATH_INCORRECT    	2

//#define NO_DISK_SPACE        (-1)



typedef unsigned long b_blocknr_t;
typedef __u32 unp_t;

				/* who is responsible for this
                                   completely uncommented struct? */
struct unfm_nodeinfo {
				/* This is what? */
    unp_t unfm_nodenum;
				/* now this I know what it is, and
                                   most of the people on our project
                                   know what it is, but I bet nobody
                                   new I hire will have a clue. */
    unsigned short unfm_freespace;
};


/* when reiserfs_file_write is called with a byte count >= MIN_PACK_ON_CLOSE,
** it sets the inode to pack on close, and when extending the file, will only
** use unformatted nodes.
**
** This is a big speed up for the journal, which is badly hurt by direct->indirect
** conversions (they must be logged).
*/
#define MIN_PACK_ON_CLOSE		512

/* the defines below say, that if file size is >=
   DIRECT_TAIL_SUPPRESSION_SIZE * blocksize, then if tail is longer
   than MAX_BYTES_SUPPRESS_DIRECT_TAIL, it will be stored in
   unformatted node */
#define DIRECT_TAIL_SUPPRESSION_SIZE      1024
#define MAX_BYTES_SUPPRESS_DIRECT_TAIL    1024

#if 0

//
#define mark_file_with_tail(inode,offset) \
{\
inode->u.reiserfs_i.i_has_tail = 1;\
}

#define mark_file_without_tail(inode) \
{\
inode->u.reiserfs_i.i_has_tail = 0;\
}

#endif

// this says about version of all items (but stat data) the object
// consists of
#define inode_items_version(inode) ((inode)->u.reiserfs_i.i_version)


/* We store tail in unformatted node if it is too big to fit into a
   formatted node or if DIRECT_TAIL_SUPPRESSION_SIZE,
   MAX_BYTES_SUPPRESS_DIRECT_TAIL and file size say that. */
/* #define STORE_TAIL_IN_UNFM(n_file_size,n_tail_size,n_block_size) \ */
/* ( ((n_tail_size) > MAX_DIRECT_ITEM_LEN(n_block_size)) || \ */
/*   ( ( (n_file_size) >= (n_block_size) * DIRECT_TAIL_SUPPRESSION_SIZE ) && \ */
/*    ( (n_tail_size) >= MAX_BYTES_SUPPRESS_DIRECT_TAIL ) ) ) */

  /* This is an aggressive tail suppression policy, I am hoping it
     improves our benchmarks. The principle behind it is that
     percentage space saving is what matters, not absolute space
     saving.  This is non-intuitive, but it helps to understand it if
     you consider that the cost to access 4 blocks is not much more
     than the cost to access 1 block, if you have to do a seek and
     rotate.  A tail risks a non-linear disk access that is
     significant as a percentage of total time cost for a 4 block file
     and saves an amount of space that is less significant as a
     percentage of space, or so goes the hypothesis.  -Hans */
#define STORE_TAIL_IN_UNFM(n_file_size,n_tail_size,n_block_size) \
(\
  (!(n_tail_size)) || \
  (((n_tail_size) > MAX_DIRECT_ITEM_LEN(n_block_size)) || \
   ( (n_file_size) >= (n_block_size) * 4 ) || \
   ( ( (n_file_size) >= (n_block_size) * 3 ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size))/4) ) || \
   ( ( (n_file_size) >= (n_block_size) * 2 ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size))/2) ) || \
   ( ( (n_file_size) >= (n_block_size) ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size) * 3)/4) ) ) \
)


/*
 * values for s_state field
 */
#define REISERFS_VALID_FS    1
#define REISERFS_ERROR_FS    2



/***************************************************************************/
/*                       KEY & ITEM HEAD                                   */
/***************************************************************************/

//
// we do support for old format of reiserfs: the problem is to
// distinuquish keys with 32 bit offset and keys with 60 bit ones. On
// leaf level we use ih_version of struct item_head (was
// ih_reserved). For all old items it is set to 0
// (ITEM_VERSION_1). For new items it is ITEM_VERSION_2. On internal
// levels we have to know version of item key belongs to.
//
#define ITEM_VERSION_1 0
#define ITEM_VERSION_2 1


/* loff_t - long long */


//
// directories use this key as well as old files
//
struct offset_v1 {
    __u32 k_offset;
    __u32 k_uniqueness;
} __attribute__ ((__packed__));

struct offset_v2 {
    __u64 k_offset:60;
    __u64 k_type: 4;
} __attribute__ ((__packed__));



/* Key of an item determines its location in the S+tree, and
   is composed of 4 components */
struct key {
    __u32 k_dir_id;    /* packing locality: by default parent
			  directory object id */
    __u32 k_objectid;  /* object identifier */
    union {
	struct offset_v1 k_offset_v1;
	struct offset_v2 k_offset_v2;
    } __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));


struct cpu_key {
    struct key on_disk_key;
    int version;
    int key_length; /* 3 in all cases but direct2indirect and
		       indirect2direct conversion */
};







 /* Our function for comparing keys can compare keys of different
    lengths.  It takes as a parameter the length of the keys it is to
    compare.  These defines are used in determining what is to be
    passed to it as that parameter. */
#define REISERFS_FULL_KEY_LEN     4

#define REISERFS_SHORT_KEY_LEN    2

/* The result of the key compare */
#define FIRST_GREATER 1
#define SECOND_GREATER -1
#define KEYS_IDENTICAL 0
#define KEY_FOUND 1
#define KEY_NOT_FOUND 0


#define KEY_SIZE (sizeof(struct key))
#define SHORT_KEY_SIZE (sizeof (__u32) + sizeof (__u32))

/* return values for search_by_key and clones */
#define ITEM_FOUND 1
#define ITEM_NOT_FOUND 0
#define ENTRY_FOUND 1
#define ENTRY_NOT_FOUND 0
#define DIRECTORY_NOT_FOUND -1
#define REGULAR_FILE_FOUND -2
#define DIRECTORY_FOUND -3
#define BYTE_FOUND 1
#define BYTE_NOT_FOUND 0
#define FILE_NOT_FOUND -1

#define POSITION_FOUND 1
#define POSITION_NOT_FOUND 0

// return values for reiserfs_find_entry and search_by_entry_key
#define NAME_FOUND 1
#define NAME_NOT_FOUND 0
#define GOTO_PREVIOUS_ITEM 2
#define NAME_FOUND_INVISIBLE 3



/*  Everything in the filesystem is stored as a set of items.  The
    item head contains the key of the item, its free space (for
    indirect items) and specifies the location of the item itself
    within the block.  */

struct item_head
{
  struct key ih_key; 	/* Everything in the tree is found by searching for it based on its key.*/

				/* This is bloat, this should be part
                                   of the item not the item
                                   header. -Hans */
  union {
    __u16 ih_free_space_reserved; /* The free space in the last unformatted node of an indirect item if this
				     is an indirect item.  This equals 0xFFFF iff this is a direct item or
				     stat data item. Note that the key, not this field, is used to determine
				     the item type, and thus which field this union contains. */
    __u16 ih_entry_count; /* Iff this is a directory item, this field equals the number of directory
				      entries in the directory item. */
  } __attribute__ ((__packed__)) u;
  __u16 ih_item_len;           /* total size of the item body                  */
  __u16 ih_item_location;      /* an offset to the item body within the block  */
				/* I thought we were going to use this
                                   for having lots of item types? Why
                                   don't you use this for item type
                                   not item version.  That is how you
                                   talked me into this field a year
                                   ago, remember?  I am still not
                                   convinced it needs to be 16 bits
                                   (for at least many years), but at
                                   least I can sympathize with that
                                   hope. Change the name from version
                                   to type, and tell people not to use
                                   FFFF in case 16 bits is someday too
                                   small and needs to be extended:-). */
  __u16 ih_version;	       /* 0 for all old items, 2 for new
                                  ones. Highest bit is set by fsck
                                  temporary, cleaned after all done */
} __attribute__ ((__packed__));
/* size of item header     */
#define IH_SIZE (sizeof(struct item_head))

#define ih_free_space(ih)            le16_to_cpu((ih)->u.ih_free_space_reserved)
#define ih_version(ih)               le16_to_cpu((ih)->ih_version)
#define ih_entry_count(ih)           le16_to_cpu((ih)->u.ih_entry_count)
#define ih_location(ih)              le16_to_cpu((ih)->ih_item_location)
#define ih_item_len(ih)              le16_to_cpu((ih)->ih_item_len)

#define put_ih_free_space(ih, val)   do { (ih)->u.ih_free_space_reserved = cpu_to_le16(val); } while(0)
#define put_ih_version(ih, val)      do { (ih)->ih_version = cpu_to_le16(val); } while (0)
#define put_ih_entry_count(ih, val)  do { (ih)->u.ih_entry_count = cpu_to_le16(val); } while (0)
#define put_ih_location(ih, val)     do { (ih)->ih_item_location = cpu_to_le16(val); } while (0)
#define put_ih_item_len(ih, val)     do { (ih)->ih_item_len = cpu_to_le16(val); } while (0)


// FIXME: now would that work for other than i386 archs
#define unreachable_item(ih) (ih->ih_version & (1 << 15))

#define get_ih_free_space(ih) (ih_version (ih) == ITEM_VERSION_2 ? 0 : ih_free_space (ih))
#define set_ih_free_space(ih,val) put_ih_free_space((ih), ((ih_version(ih) == ITEM_VERSION_2) ? 0 : (val)))


//
// there are 5 item types currently
//
#define TYPE_STAT_DATA 0
#define TYPE_INDIRECT 1
#define TYPE_DIRECT 2
#define TYPE_DIRENTRY 3 
#define TYPE_ANY 15 // FIXME: comment is required

//
// in old version uniqueness field shows key type
//
#define V1_SD_UNIQUENESS 0
#define V1_INDIRECT_UNIQUENESS 0xfffffffe
#define V1_DIRECT_UNIQUENESS 0xffffffff
#define V1_DIRENTRY_UNIQUENESS 500
#define V1_ANY_UNIQUENESS 555 // FIXME: comment is required

//
// here are conversion routines
//
extern inline int uniqueness2type (__u32 uniqueness)
{
    switch (uniqueness) {
    case V1_SD_UNIQUENESS: return TYPE_STAT_DATA;
    case V1_INDIRECT_UNIQUENESS: return TYPE_INDIRECT;
    case V1_DIRECT_UNIQUENESS: return TYPE_DIRECT;
    case V1_DIRENTRY_UNIQUENESS: return TYPE_DIRENTRY;
    }
/*
    if (uniqueness != V1_ANY_UNIQUENESS) {
	printk ("uniqueness %d\n", uniqueness);
	BUG (); 
    }
*/
    return TYPE_ANY;
}

extern inline __u32 type2uniqueness (int type)
{
    switch (type) {
    case TYPE_STAT_DATA: return V1_SD_UNIQUENESS;
    case TYPE_INDIRECT: return V1_INDIRECT_UNIQUENESS;
    case TYPE_DIRECT: return V1_DIRECT_UNIQUENESS;
    case TYPE_DIRENTRY: return V1_DIRENTRY_UNIQUENESS;
    }
    /*
    if (type != TYPE_ANY)
	BUG ();
    */
    return V1_ANY_UNIQUENESS;
}


//
// key is pointer to on disk key which is stored in le, result is cpu,
// there is no way to get version of object from key, so, provide
// version to these defines
//
extern inline loff_t le_key_k_offset (int version, struct key * key)
{
    return (version == ITEM_VERSION_1) ? key->u.k_offset_v1.k_offset :
	le64_to_cpu (key->u.k_offset_v2.k_offset);
}
extern inline loff_t le_ih_k_offset (struct item_head * ih)
{
    return le_key_k_offset (ih_version (ih), &(ih->ih_key));
}


extern inline loff_t le_key_k_type (int version, struct key * key)
{
    return (version == ITEM_VERSION_1) ? uniqueness2type (key->u.k_offset_v1.k_uniqueness) :
	le16_to_cpu (key->u.k_offset_v2.k_type);
}
extern inline loff_t le_ih_k_type (struct item_head * ih)
{
    return le_key_k_type (ih_version (ih), &(ih->ih_key));
}


extern inline void set_le_key_k_offset (int version, struct key * key, loff_t offset)
{
    (version == ITEM_VERSION_1) ? (key->u.k_offset_v1.k_offset = offset) :
	(key->u.k_offset_v2.k_offset = cpu_to_le64 (offset));
}
extern inline void set_le_ih_k_offset (struct item_head * ih, loff_t offset)
{
    set_le_key_k_offset (ih_version (ih), &(ih->ih_key), offset);
}



extern inline void set_le_key_k_type (int version, struct key * key, int type)
{
    (version == ITEM_VERSION_1) ? (key->u.k_offset_v1.k_uniqueness = type2uniqueness (type)) :
	(key->u.k_offset_v2.k_type = cpu_to_le16 (type));
}
extern inline void set_le_ih_k_type (struct item_head * ih, int type)
{
    set_le_key_k_type (ih_version (ih), &(ih->ih_key), type);
}


#define is_direntry_le_key(version,key) (le_key_k_type (version, key) == TYPE_DIRENTRY)
#define is_direct_le_key(version,key) (le_key_k_type (version, key) == TYPE_DIRECT)
#define is_indirect_le_key(version,key) (le_key_k_type (version, key) == TYPE_INDIRECT)
#define is_statdata_le_key(version,key) (le_key_k_type (version, key) == TYPE_STAT_DATA)

//
// item header has version.
//
#define is_direntry_le_ih(ih) is_direntry_le_key (ih_version (ih), &((ih)->ih_key))
#define is_direct_le_ih(ih) is_direct_le_key (ih_version (ih), &((ih)->ih_key))
#define is_indirect_le_ih(ih) is_indirect_le_key (ih_version(ih), &((ih)->ih_key))
#define is_statdata_le_ih(ih) is_statdata_le_key (ih_version (ih), &((ih)->ih_key))



//
// key is pointer to cpu key, result is cpu
//
extern inline loff_t cpu_key_k_offset (struct cpu_key * key)
{
    return (key->version == ITEM_VERSION_1) ? key->on_disk_key.u.k_offset_v1.k_offset :
	key->on_disk_key.u.k_offset_v2.k_offset;
}

extern inline loff_t cpu_key_k_type (struct cpu_key * key)
{
    return (key->version == ITEM_VERSION_1) ? uniqueness2type (key->on_disk_key.u.k_offset_v1.k_uniqueness) :
	key->on_disk_key.u.k_offset_v2.k_type;
}

extern inline void set_cpu_key_k_offset (struct cpu_key * key, loff_t offset)
{
    (key->version == ITEM_VERSION_1) ? (key->on_disk_key.u.k_offset_v1.k_offset = offset) :
	(key->on_disk_key.u.k_offset_v2.k_offset = offset);
}


extern inline void set_cpu_key_k_type (struct cpu_key * key, int type)
{
    (key->version == ITEM_VERSION_1) ? (key->on_disk_key.u.k_offset_v1.k_uniqueness = type2uniqueness (type)) :
	(key->on_disk_key.u.k_offset_v2.k_type = type);
}

extern inline void cpu_key_k_offset_dec (struct cpu_key * key)
{
    if (key->version == ITEM_VERSION_1)
	key->on_disk_key.u.k_offset_v1.k_offset --;
    else
	key->on_disk_key.u.k_offset_v2.k_offset --;
}


#define is_direntry_cpu_key(key) (cpu_key_k_type (key) == TYPE_DIRENTRY)
#define is_direct_cpu_key(key) (cpu_key_k_type (key) == TYPE_DIRECT)
#define is_indirect_cpu_key(key) (cpu_key_k_type (key) == TYPE_INDIRECT)
#define is_statdata_cpu_key(key) (cpu_key_k_type (key) == TYPE_STAT_DATA)


/* are these used ? */
#define is_direntry_cpu_ih(ih) (is_direntry_cpu_key (&((ih)->ih_key)))
#define is_direct_cpu_ih(ih) (is_direct_cpu_key (&((ih)->ih_key)))
#define is_indirect_cpu_ih(ih) (is_indirect_cpu_key (&((ih)->ih_key)))
#define is_statdata_cpu_ih(ih) (is_statdata_cpu_key (&((ih)->ih_key)))





#define I_K_KEY_IN_ITEM(p_s_ih, p_s_key, n_blocksize) \
    ( ! COMP_SHORT_KEYS(p_s_ih, p_s_key) && \
          I_OFF_BYTE_IN_ITEM(p_s_ih, k_offset (p_s_key), n_blocksize) )

/* maximal length of item */ 
#define MAX_ITEM_LEN(block_size) (block_size - BLKH_SIZE - IH_SIZE)
#define MIN_ITEM_LEN 1


/* object identifier for root dir */
#define REISERFS_ROOT_OBJECTID 2
#define REISERFS_ROOT_PARENT_OBJECTID 1
extern struct key root_key;




/* 
 * Picture represents a leaf of the S+tree
 *  ______________________________________________________
 * |      |  Array of     |                   |           |
 * |Block |  Object-Item  |      F r e e      |  Objects- |
 * | head |  Headers      |     S p a c e     |   Items   |
 * |______|_______________|___________________|___________|
 */

/* Header of a disk block.  More precisely, header of a formatted leaf
   or internal node, and not the header of an unformatted node. */
struct block_head {       
  __u16 blk_level;        /* Level of a block in the tree. */
  __u16 blk_nr_item;      /* Number of keys/items in a block. */
  __u16 blk_free_space;   /* Block free space in bytes. */
  __u16 blk_reserved;
				/* dump this in v4/planA */
  struct key  blk_right_delim_key; /* kept only for compatibility */
};

#define BLKH_SIZE (sizeof(struct block_head))

/*
 * values for blk_level field of the struct block_head
 */

#define FREE_LEVEL 0 /* when node gets removed from the tree its
			blk_level is set to FREE_LEVEL. It is then
			used to see whether the node is still in the
			tree */

#define DISK_LEAF_NODE_LEVEL  1 /* Leaf node level.*/

/* Given the buffer head of a formatted node, resolve to the block head of that node. */
#define B_BLK_HEAD(p_s_bh)  ((struct block_head *)((p_s_bh)->b_data))
/* Number of items that are in buffer. */
#define B_NR_ITEMS(p_s_bh)	  	(le16_to_cpu ( B_BLK_HEAD(p_s_bh)->blk_nr_item ))
#define B_LEVEL(bh)			(le16_to_cpu ( B_BLK_HEAD(bh)->blk_level ))
#define B_FREE_SPACE(bh)		(le16_to_cpu ( B_BLK_HEAD(bh)->blk_free_space ))

#define PUT_B_NR_ITEMS(p_s_bh)	  	do { B_BLK_HEAD(p_s_bh)->blk_nr_item = cpu_to_le16(val); } while (0)
#define PUT_B_LEVEL(bh, val)		do { B_BLK_HEAD(bh)->blk_level = cpu_to_le16(val); } while (0)
#define PUT_B_FREE_SPACE(bh)		do { B_BLK_HEAD(bh)->blk_free_space = cpu_to_le16(val); } while (0)

/* Get right delimiting key. */
#define B_PRIGHT_DELIM_KEY(p_s_bh)	( &(B_BLK_HEAD(p_s_bh)->blk_right_delim_key) )

/* Does the buffer contain a disk leaf. */
#define B_IS_ITEMS_LEVEL(p_s_bh)   	( B_BLK_HEAD(p_s_bh)->blk_level == DISK_LEAF_NODE_LEVEL )

/* Does the buffer contain a disk internal node */
#define B_IS_KEYS_LEVEL(p_s_bh) 	( B_BLK_HEAD(p_s_bh)->blk_level > DISK_LEAF_NODE_LEVEL &&\
					  B_BLK_HEAD(p_s_bh)->blk_level <= MAX_HEIGHT )




/***************************************************************************/
/*                             STAT DATA                                   */
/***************************************************************************/


//
// old stat data is 32 bytes long. We are going to distinguish new one by
// different size
//
struct stat_data_v1
{
    __u16 sd_mode;	/* file type, permissions */
    __u16 sd_nlink;	/* number of hard links */
    __u16 sd_uid;		/* owner */
    __u16 sd_gid;		/* group */
    __u32 sd_size;	/* file size */
    __u32 sd_atime;	/* time of last access */
    __u32 sd_mtime;	/* time file was last modified  */
    __u32 sd_ctime;	/* time inode (stat data) was last changed (except changes to sd_atime and sd_mtime) */
    union {
	__u32 sd_rdev;
	__u32 sd_blocks;	/* number of blocks file uses */
    } __attribute__ ((__packed__)) u;
    __u32 sd_first_direct_byte; /* first byte of file which is stored
				   in a direct item: except that if it
				   equals 1 it is a symlink and if it
				   equals ~(__u32)0 there is no
				   direct item.  The existence of this
				   field really grates on me. Let's
				   replace it with a macro based on
				   sd_size and our tail suppression
				   policy.  Someday.  -Hans */
} __attribute__ ((__packed__));

#define SD_V1_SIZE (sizeof(struct stat_data_v1))


/* Stat Data on disk (reiserfs version of UFS disk inode minus the
   address blocks) */
struct stat_data {
    __u16 sd_mode;	/* file type, permissions */
    __u16 sd_reserved;
    __u32 sd_nlink;	/* number of hard links */
    __u64 sd_size;	/* file size */
    __u32 sd_uid;		/* owner */
    __u32 sd_gid;		/* group */
    __u32 sd_atime;	/* time of last access */
    __u32 sd_mtime;	/* time file was last modified  */
    __u32 sd_ctime;	/* time inode (stat data) was last changed (except changes to sd_atime and sd_mtime) */
    __u32 sd_blocks;
    union {
	__u32 sd_rdev;
      //__u32 sd_first_direct_byte; 
      /* first byte of file which is stored in a
				       direct item: except that if it equals 1
				       it is a symlink and if it equals
				       ~(__u32)0 there is no direct item.  The
				       existence of this field really grates
				       on me. Let's replace it with a macro
				       based on sd_size and our tail
				       suppression policy? */
  } __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));
//
// this is 40 bytes long
//
#define SD_SIZE (sizeof(struct stat_data))

#define stat_data_v1(ih) (ih_version (ih) == ITEM_VERSION_1)


/***************************************************************************/
/*                      DIRECTORY STRUCTURE                                */
/***************************************************************************/
/* 
   Picture represents the structure of directory items
   ________________________________________________
   |  Array of     |   |     |        |       |   |
   | directory     |N-1| N-2 | ....   |   1st |0th|
   | entry headers |   |     |        |       |   |
   |_______________|___|_____|________|_______|___|
                    <----   directory entries         ------>

 First directory item has k_offset component 1. We store "." and ".."
 in one item, always, we never split "." and ".." into differing
 items.  This makes, among other things, the code for removing
 directories simpler. */
#define SD_OFFSET  0
#define SD_UNIQUENESS 0
#define DOT_OFFSET 1
#define DOT_DOT_OFFSET 2
#define DIRENTRY_UNIQUENESS 500

/* */
#define FIRST_ITEM_OFFSET 1

/*
   Q: How to get key of object pointed to by entry from entry?  

   A: Each directory entry has its header. This header has deh_dir_id and deh_objectid fields, those are key
      of object, entry points to */

/* NOT IMPLEMENTED:   
   Directory will someday contain stat data of object */



struct reiserfs_de_head
{
  __u32 deh_offset;		/* third component of the directory entry key */
  __u32 deh_dir_id;		/* objectid of the parent directory of the object, that is referenced
					   by directory entry */
  __u32 deh_objectid;		/* objectid of the object, that is referenced by directory entry */
  __u16 deh_location;		/* offset of name in the whole item */
  __u16 deh_state;		/* whether 1) entry contains stat data (for future), and 2) whether
					   entry is hidden (unlinked) */
} __attribute__ ((__packed__));
#define DEH_SIZE sizeof(struct reiserfs_de_head)

/* empty directory contains two entries "." and ".." and their headers */
#define EMPTY_DIR_SIZE \
(DEH_SIZE * 2 + ROUND_UP (strlen (".")) + ROUND_UP (strlen ("..")))

/* old format directories have this size when empty */
#define EMPTY_DIR_SIZE_V1 (DEH_SIZE * 2 + 3)

#define DEH_Statdata 0			/* not used now */
#define DEH_Visible 2

/* bitops which deals with unaligned addrs; 
   needed for alpha port. --zam */
#ifdef __alpha__
#   define ADDR_UNALIGNED_BITS  (5)
#endif

#ifdef ADDR_UNALIGNED_BITS

#   define aligned_address(addr)           ((void *)((long)(addr) & ~((1UL << ADDR_UNALIGNED_BITS) - 1)))
#   define unaligned_offset(addr)          (((int)((long)(addr) & ((1 << ADDR_UNALIGNED_BITS) - 1))) << 3)

#   define set_bit_unaligned(nr, addr)     set_bit((nr) + unaligned_offset(addr), aligned_address(addr))
#   define clear_bit_unaligned(nr, addr)   clear_bit((nr) + unaligned_offset(addr), aligned_address(addr))
#   define test_bit_unaligned(nr, addr)    test_bit((nr) + unaligned_offset(addr), aligned_address(addr))

#else

#   define set_bit_unaligned(nr, addr)     set_bit(nr, addr)
#   define clear_bit_unaligned(nr, addr)   clear_bit(nr, addr)
#   define test_bit_unaligned(nr, addr)    test_bit(nr, addr)

#endif

#define deh_dir_id(deh) (__le32_to_cpu ((deh)->deh_dir_id))
#define deh_objectid(deh) (__le32_to_cpu ((deh)->deh_objectid))
#define deh_offset(deh) (__le32_to_cpu ((deh)->deh_offset))


#define mark_de_with_sd(deh)        set_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define mark_de_without_sd(deh)     clear_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define mark_de_visible(deh)	    set_bit_unaligned (DEH_Visible, &((deh)->deh_state))
#define mark_de_hidden(deh)	    clear_bit_unaligned (DEH_Visible, &((deh)->deh_state))

#define de_with_sd(deh)		    test_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define de_visible(deh)	    	    test_bit_unaligned (DEH_Visible, &((deh)->deh_state))
#define de_hidden(deh)	    	    !test_bit_unaligned (DEH_Visible, &((deh)->deh_state))

/* compose directory item containing "." and ".." entries (entries are
   not aligned to 4 byte boundary) */
extern inline void make_empty_dir_item_v1 (char * body, __u32 dirid, __u32 objid,
					   __u32 par_dirid, __u32 par_objid)
{
    struct reiserfs_de_head * deh;

    memset (body, 0, EMPTY_DIR_SIZE_V1);
    deh = (struct reiserfs_de_head *)body;
    
    /* direntry header of "." */
    deh[0].deh_offset = cpu_to_le32 (DOT_OFFSET);
    deh[0].deh_dir_id = cpu_to_le32 (dirid);
    deh[0].deh_objectid = cpu_to_le32 (objid);
    deh[0].deh_location = cpu_to_le16 (EMPTY_DIR_SIZE_V1 - strlen ("."));
    deh[0].deh_state = 0;
    mark_de_visible(&(deh[0]));
  
    /* direntry header of ".." */
    deh[1].deh_offset = cpu_to_le32 (DOT_DOT_OFFSET);
    /* key of ".." for the root directory */
    deh[1].deh_dir_id = cpu_to_le32 (par_dirid);
    deh[1].deh_objectid = cpu_to_le32 (par_objid);
    deh[1].deh_location = cpu_to_le16 (le16_to_cpu (deh[0].deh_location) - strlen (".."));
    deh[1].deh_state = 0;
    mark_de_visible(&(deh[1]));

    /* copy ".." and "." */
    memcpy (body + deh[0].deh_location, ".", 1);
    memcpy (body + deh[1].deh_location, "..", 2);
}

/* compose directory item containing "." and ".." entries */
extern inline void make_empty_dir_item (char * body, __u32 dirid, __u32 objid,
					__u32 par_dirid, __u32 par_objid)
{
    struct reiserfs_de_head * deh;

    memset (body, 0, EMPTY_DIR_SIZE);
    deh = (struct reiserfs_de_head *)body;
    
    /* direntry header of "." */
    deh[0].deh_offset = cpu_to_le32 (DOT_OFFSET);
    deh[0].deh_dir_id = cpu_to_le32 (dirid);
    deh[0].deh_objectid = cpu_to_le32 (objid);
    deh[0].deh_location = cpu_to_le16 (EMPTY_DIR_SIZE - ROUND_UP (strlen (".")));
    deh[0].deh_state = 0;
    mark_de_visible(&(deh[0]));
  
    /* direntry header of ".." */
    deh[1].deh_offset = cpu_to_le32 (DOT_DOT_OFFSET);
    /* key of ".." for the root directory */
    deh[1].deh_dir_id = cpu_to_le32 (par_dirid);
    deh[1].deh_objectid = cpu_to_le32 (par_objid);
    deh[1].deh_location = cpu_to_le16 (le16_to_cpu (deh[0].deh_location) - ROUND_UP (strlen ("..")));
    deh[1].deh_state = 0;
    mark_de_visible(&(deh[1]));

    /* copy ".." and "." */
    memcpy (body + deh[0].deh_location, ".", 1);
    memcpy (body + deh[1].deh_location, "..", 2);
}


/* array of the entry headers */
 /* get item body */
#define B_I_PITEM(bh,ih) ( (bh)->b_data + (ih)->ih_item_location )
#define B_I_DEH(bh,ih) ((struct reiserfs_de_head *)(B_I_PITEM(bh,ih)))

/* length of the directory entry in directory item. This define
   calculates length of i-th directory entry using directory entry
   locations from dir entry head. When it calculates length of 0-th
   directory entry, it uses length of whole item in place of entry
   location of the non-existent following entry in the calculation.
   See picture above.*/
/*
#define I_DEH_N_ENTRY_LENGTH(ih,deh,i) \
((i) ? (((deh)-1)->deh_location - (deh)->deh_location) : ((ih)->ih_item_len) - (deh)->deh_location)
*/
extern inline int entry_length (struct buffer_head * bh, struct item_head * ih,
				int pos_in_item)
{
    struct reiserfs_de_head * deh;

    deh = B_I_DEH (bh, ih) + pos_in_item;
    if (pos_in_item)
	return (le16_to_cpu ((deh - 1)->deh_location) - le16_to_cpu (deh->deh_location));
    return (le16_to_cpu (ih->ih_item_len) - le16_to_cpu (deh->deh_location));
}



/* number of entries in the directory item, depends on ENTRY_COUNT being at the start of directory dynamic data. */
#define I_ENTRY_COUNT(ih) ((ih)->u.ih_entry_count)


/* name by bh, ih and entry_num */
#define B_I_E_NAME(bh,ih,entry_num) ((char *)(bh->b_data + ih->ih_item_location + (B_I_DEH(bh,ih)+(entry_num))->deh_location))

// two entries per block (at least)
//#define REISERFS_MAX_NAME_LEN(block_size) 
//((block_size - BLKH_SIZE - IH_SIZE - DEH_SIZE * 2) / 2)

// two entries per block (at least)
#define REISERFS_MAX_NAME_LEN(block_size) 255




/* this structure is used for operations on directory entries. It is
   not a disk structure. */
/* When reiserfs_find_entry or search_by_entry_key find directory
   entry, they return filled reiserfs_dir_entry structure */
struct reiserfs_dir_entry
{
  struct buffer_head * de_bh;
  int de_item_num;
  struct item_head * de_ih;
  int de_entry_num;
  struct reiserfs_de_head * de_deh;
  int de_entrylen;
  int de_namelen;
  char * de_name;
  char * de_gen_number_bit_string;

  __u32 de_dir_id;
  __u32 de_objectid;

  struct cpu_key de_entry_key;
};
   
/* these defines are useful when a particular member of a reiserfs_dir_entry is needed */

/* pointer to file name, stored in entry */
#define B_I_DEH_ENTRY_FILE_NAME(bh,ih,deh) (B_I_PITEM (bh, ih) + (deh)->deh_location)

/* length of name */
#define I_DEH_N_ENTRY_FILE_NAME_LENGTH(ih,deh,entry_num) \
(I_DEH_N_ENTRY_LENGTH (ih, deh, entry_num) - (de_with_sd (deh) ? SD_SIZE : 0))



/* hash value occupies bits from 7 up to 30 */
#define GET_HASH_VALUE(offset) ((offset) & 0x7fffff80LL)
/* generation number occupies 7 bits starting from 0 up to 6 */
#define GET_GENERATION_NUMBER(offset) ((offset) & 0x7fLL)
#define MAX_GENERATION_NUMBER  127

#define SET_GENERATION_NUMBER(offset,gen_number) (GET_HASH_VALUE(offset)|(gen_number))


/*
 * Picture represents an internal node of the reiserfs tree
 *  ______________________________________________________
 * |      |  Array of     |  Array of         |  Free     |
 * |block |    keys       |  pointers         | space     |
 * | head |      N        |      N+1          |           |
 * |______|_______________|___________________|___________|
 */

/***************************************************************************/
/*                      DISK CHILD                                         */
/***************************************************************************/
/* Disk child pointer: The pointer from an internal node of the tree
   to a node that is on disk. */
struct disk_child {
  __u32       dc_block_number;              /* Disk child's block number. */
  __u16       dc_size;		            /* Disk child's used space.   */
  __u16       dc_reserved;
};

#define DC_SIZE (sizeof(struct disk_child))

/* Get disk child by buffer header and position in the tree node. */
#define B_N_CHILD(p_s_bh,n_pos)  ((struct disk_child *)\
((p_s_bh)->b_data+BLKH_SIZE+B_NR_ITEMS(p_s_bh)*KEY_SIZE+DC_SIZE*(n_pos)))

/* Get disk child number by buffer header and position in the tree node. */
#define B_N_CHILD_NUM(p_s_bh,n_pos) (le32_to_cpu (B_N_CHILD(p_s_bh,n_pos)->dc_block_number))
#define PUT_B_N_CHILD_NUM(p_s_bh,n_pos, val) do { B_N_CHILD(p_s_bh,n_pos)->dc_block_number = cpu_to_le32(val); } while (0)

 /* maximal value of field child_size in structure disk_child */ 
 /* child size is the combined size of all items and their headers */
#define MAX_CHILD_SIZE(bh) ((int)( (bh)->b_size - BLKH_SIZE ))

/* amount of used space in buffer (not including block head) */
#define B_CHILD_SIZE(cur) (MAX_CHILD_SIZE(cur)-(B_FREE_SPACE(cur)))

/* max and min number of keys in internal node */
#define MAX_NR_KEY(bh) ( (MAX_CHILD_SIZE(bh)-DC_SIZE)/(KEY_SIZE+DC_SIZE) )
#define MIN_NR_KEY(bh)    (MAX_NR_KEY(bh)/2)

/***************************************************************************/
/*                      PATH STRUCTURES AND DEFINES                        */
/***************************************************************************/


/* Search_by_key fills up the path from the root to the leaf as it descends the tree looking for the
   key.  It uses reiserfs_bread to try to find buffers in the cache given their block number.  If it
   does not find them in the cache it reads them from disk.  For each node search_by_key finds using
   reiserfs_bread it then uses bin_search to look through that node.  bin_search will find the
   position of the block_number of the next node if it is looking through an internal node.  If it
   is looking through a leaf node bin_search will find the position of the item which has key either
   equal to given key, or which is the maximal key less than the given key. */

struct  path_element  {
  struct buffer_head *	pe_buffer;    /* Pointer to the buffer at the path in the tree. */
  int         		pe_position;  /* Position in the tree node which is placed in the */
                                      /* buffer above.                                  */
};

#define MAX_HEIGHT 5 /* maximal height of a tree. don't change this without changing JOURNAL_PER_BALANCE_CNT */
#define EXTENDED_MAX_HEIGHT         7 /* Must be equals MAX_HEIGHT + FIRST_PATH_ELEMENT_OFFSET */
#define FIRST_PATH_ELEMENT_OFFSET   2 /* Must be equal to at least 2. */

#define ILLEGAL_PATH_ELEMENT_OFFSET 1 /* Must be equal to FIRST_PATH_ELEMENT_OFFSET - 1 */
#define MAX_FEB_SIZE 6   /* this MUST be MAX_HEIGHT + 1. See about FEB below */



/* We need to keep track of who the ancestors of nodes are.  When we
   perform a search we record which nodes were visited while
   descending the tree looking for the node we searched for. This list
   of nodes is called the path.  This information is used while
   performing balancing.  Note that this path information may become
   invalid, and this means we must check it when using it to see if it
   is still valid. You'll need to read search_by_key and the comments
   in it, especially about decrement_counters_in_path(), to understand
   this structure.  

Paths make the code so much harder to work with and debug.... An
enormous number of bugs are due to them, and trying to write or modify
code that uses them just makes my head hurt.  They are based on an
excessive effort to avoid disturbing the precious VFS code.:-( The
gods only know how we are going to SMP the code that uses them.
znodes are the way! */


struct  path {
  int                   path_length;                      	/* Length of the array above.   */
  struct  path_element  path_elements[EXTENDED_MAX_HEIGHT];	/* Array of the path elements.  */
  int			pos_in_item;
};

#define pos_in_item(path) ((path)->pos_in_item)

#define INITIALIZE_PATH(var) \
struct path var = {ILLEGAL_PATH_ELEMENT_OFFSET, }

/* Get path element by path and path position. */
#define PATH_OFFSET_PELEMENT(p_s_path,n_offset)  ((p_s_path)->path_elements +(n_offset))

/* Get buffer header at the path by path and path position. */
#define PATH_OFFSET_PBUFFER(p_s_path,n_offset)   (PATH_OFFSET_PELEMENT(p_s_path,n_offset)->pe_buffer)

/* Get position in the element at the path by path and path position. */
#define PATH_OFFSET_POSITION(p_s_path,n_offset) (PATH_OFFSET_PELEMENT(p_s_path,n_offset)->pe_position)


#define PATH_PLAST_BUFFER(p_s_path) (PATH_OFFSET_PBUFFER((p_s_path), (p_s_path)->path_length))
				/* you know, to the person who didn't
                                   write this the macro name does not
                                   at first suggest what it does.
                                   Maybe POSITION_FROM_PATH_END? Or
                                   maybe we should just focus on
                                   dumping paths... -Hans */
#define PATH_LAST_POSITION(p_s_path) (PATH_OFFSET_POSITION((p_s_path), (p_s_path)->path_length))


#define PATH_PITEM_HEAD(p_s_path)    B_N_PITEM_HEAD(PATH_PLAST_BUFFER(p_s_path),PATH_LAST_POSITION(p_s_path))

/* in do_balance leaf has h == 0 in contrast with path structure,
   where root has level == 0. That is why we need these defines */
#define PATH_H_PBUFFER(p_s_path, h) PATH_OFFSET_PBUFFER (p_s_path, p_s_path->path_length - (h))	/* tb->S[h] */
#define PATH_H_PPARENT(path, h) PATH_H_PBUFFER (path, (h) + 1)			/* tb->F[h] or tb->S[0]->b_parent */
#define PATH_H_POSITION(path, h) PATH_OFFSET_POSITION (path, path->path_length - (h))	
#define PATH_H_B_ITEM_ORDER(path, h) PATH_H_POSITION(path, h + 1)		/* tb->S[h]->b_item_order */

#define PATH_H_PATH_OFFSET(p_s_path, n_h) ((p_s_path)->path_length - (n_h))

#define get_bh(path) PATH_PLAST_BUFFER(path)
#define get_ih(path) PATH_PITEM_HEAD(path)
#define get_item_pos(path) PATH_LAST_POSITION(path)
#define get_item(path) ((void *)B_N_PITEM(PATH_PLAST_BUFFER(path), PATH_LAST_POSITION (path)))
#define item_moved(ih,path) comp_items(ih, path)
#define path_changed(ih,path) comp_items (ih, path)


/***************************************************************************/
/*                       MISC                                              */
/***************************************************************************/

/* Size of pointer to the unformatted node. */
#define UNFM_P_SIZE (sizeof(unp_t))

// in in-core inode key is stored on le form
#define INODE_PKEY(inode) ((struct key *)((inode)->u.reiserfs_i.i_key))
//#define mark_tail_converted(inode) (atomic_set(&((inode)->u.reiserfs_i.i_converted),1))
//#define unmark_tail_converted(inode) (atomic_set(&((inode)->u.reiserfs_i.i_converted), 0))
//#define is_tail_converted(inode) (atomic_read(&((inode)->u.reiserfs_i.i_converted)))



#define MAX_UL_INT 0xffffffff
#define MAX_INT    0x7ffffff
#define MAX_US_INT 0xffff

///#define TOO_LONG_LENGTH		(~0ULL)

// reiserfs version 2 has max offset 60 bits. Version 1 - 32 bit offset
#define U32_MAX (~(__u32)0)
extern inline loff_t max_reiserfs_offset (struct inode * inode)
{
    if (inode_items_version (inode) == ITEM_VERSION_1)
	return (loff_t)U32_MAX;

    return (loff_t)((~(__u64)0) >> 4);
}


/*#define MAX_KEY_UNIQUENESS	MAX_UL_INT*/
#define MAX_KEY_OBJECTID	MAX_UL_INT


#define MAX_B_NUM  MAX_UL_INT
#define MAX_FC_NUM MAX_US_INT


/* the purpose is to detect overflow of an unsigned short */
#define REISERFS_LINK_MAX (MAX_US_INT - 1000)


/* The following defines are used in reiserfs_insert_item and reiserfs_append_item  */
#define REISERFS_KERNEL_MEM		0	/* reiserfs kernel memory mode	*/
#define REISERFS_USER_MEM		1	/* reiserfs user memory mode		*/

#define fs_generation(s) ((s)->u.reiserfs_sb.s_generation_counter)
#define get_generation(s) atomic_read (&fs_generation(s))
#define FILESYSTEM_CHANGED_TB(tb)  (get_generation((tb)->tb_sb) != (tb)->fs_gen)
#define fs_changed(gen,s) (gen != get_generation (s))


/***************************************************************************/
/*                  FIXATE NODES                                           */
/***************************************************************************/

//#define VI_TYPE_STAT_DATA 1
//#define VI_TYPE_DIRECT 2
//#define VI_TYPE_INDIRECT 4
//#define VI_TYPE_DIRECTORY 8
//#define VI_TYPE_FIRST_DIRECTORY_ITEM 16
//#define VI_TYPE_INSERTED_DIRECTORY_ITEM 32

#define VI_TYPE_LEFT_MERGEABLE 1
#define VI_TYPE_RIGHT_MERGEABLE 2

/* To make any changes in the tree we always first find node, that
   contains item to be changed/deleted or place to insert a new
   item. We call this node S. To do balancing we need to decide what
   we will shift to left/right neighbor, or to a new node, where new
   item will be etc. To make this analysis simpler we build virtual
   node. Virtual node is an array of items, that will replace items of
   node S. (For instance if we are going to delete an item, virtual
   node does not contain it). Virtual node keeps information about
   item sizes and types, mergeability of first and last items, sizes
   of all entries in directory item. We use this array of items when
   calculating what we can shift to neighbors and how many nodes we
   have to have if we do not any shiftings, if we shift to left/right
   neighbor or to both. */
struct virtual_item
{
    int vi_index; // index in the array of item operations
    unsigned short vi_type;	// left/right mergeability
    unsigned short vi_item_len;           /* length of item that it will have after balancing */
    struct item_head * vi_ih;
    const char * vi_item;     // body of item (old or new)
    const void * vi_new_data; // 0 always but paste mode
    void * vi_uarea;    // item specific area
};


struct virtual_node
{
  char * vn_free_ptr;		/* this is a pointer to the free space in the buffer */
  unsigned short vn_nr_item;	/* number of items in virtual node */
  short vn_size;        	/* size of node , that node would have if it has unlimited size and no balancing is performed */
  short vn_mode;		/* mode of balancing (paste, insert, delete, cut) */
  short vn_affected_item_num; 
  short vn_pos_in_item;
  struct item_head * vn_ins_ih;	/* item header of inserted item, 0 for other modes */
  const void * vn_data;
  struct virtual_item * vn_vi;	/* array of items (including a new one, excluding item to be deleted) */
};


/***************************************************************************/
/*                  TREE BALANCE                                           */
/***************************************************************************/

/* This temporary structure is used in tree balance algorithms, and
   constructed as we go to the extent that its various parts are
   needed.  It contains arrays of nodes that can potentially be
   involved in the balancing of node S, and parameters that define how
   each of the nodes must be balanced.  Note that in these algorithms
   for balancing the worst case is to need to balance the current node
   S and the left and right neighbors and all of their parents plus
   create a new node.  We implement S1 balancing for the leaf nodes
   and S0 balancing for the internal nodes (S1 and S0 are defined in
   our papers.)*/

#define MAX_FREE_BLOCK 7	/* size of the array of buffers to free at end of do_balance */

/* maximum number of FEB blocknrs on a single level */
#define MAX_AMOUNT_NEEDED 2

/* someday somebody will prefix every field in this struct with tb_ */
struct tree_balance
{
  int tb_mode;
  int need_balance_dirty;
  struct super_block * tb_sb;
  struct reiserfs_transaction_handle *transaction_handle ;
  struct path * tb_path;
  struct buffer_head * L[MAX_HEIGHT];        /* array of left neighbors of nodes in the path */
  struct buffer_head * R[MAX_HEIGHT];        /* array of right neighbors of nodes in the path*/
  struct buffer_head * FL[MAX_HEIGHT];       /* array of fathers of the left  neighbors      */
  struct buffer_head * FR[MAX_HEIGHT];       /* array of fathers of the right neighbors      */
  struct buffer_head * CFL[MAX_HEIGHT];      /* array of common parents of center node and its left neighbor  */
  struct buffer_head * CFR[MAX_HEIGHT];      /* array of common parents of center node and its right neighbor */

  struct buffer_head * FEB[MAX_FEB_SIZE]; /* array of empty buffers. Number of buffers in array equals
					     cur_blknum. */
  struct buffer_head * used[MAX_FEB_SIZE];
  struct buffer_head * thrown[MAX_FEB_SIZE];
  int lnum[MAX_HEIGHT];	/* array of number of items which must be
			   shifted to the left in order to balance the
			   current node; for leaves includes item that
			   will be partially shifted; for internal
			   nodes, it is the number of child pointers
			   rather than items. It includes the new item
			   being created. The code sometimes subtracts
			   one to get the number of wholly shifted
			   items for other purposes. */
  int rnum[MAX_HEIGHT];	/* substitute right for left in comment above */
  int lkey[MAX_HEIGHT];               /* array indexed by height h mapping the key delimiting L[h] and
					       S[h] to its item number within the node CFL[h] */
  int rkey[MAX_HEIGHT];               /* substitute r for l in comment above */
  int insert_size[MAX_HEIGHT];        /* the number of bytes by we are trying to add or remove from
					       S[h]. A negative value means removing.  */
  int blknum[MAX_HEIGHT];             /* number of nodes that will replace node S[h] after
					       balancing on the level h of the tree.  If 0 then S is
					       being deleted, if 1 then S is remaining and no new nodes
					       are being created, if 2 or 3 then 1 or 2 new nodes is
					       being created */

  /* fields that are used only for balancing leaves of the tree */
  int cur_blknum;	/* number of empty blocks having been already allocated			*/
  int s0num;             /* number of items that fall into left most  node when S[0] splits	*/
  int s1num;             /* number of items that fall into first  new node when S[0] splits	*/
  int s2num;             /* number of items that fall into second new node when S[0] splits	*/
  int lbytes;            /* number of bytes which can flow to the left neighbor from the	left	*/
  /* most liquid item that cannot be shifted from S[0] entirely		*/
  /* if -1 then nothing will be partially shifted */
  int rbytes;            /* number of bytes which will flow to the right neighbor from the right	*/
  /* most liquid item that cannot be shifted from S[0] entirely		*/
  /* if -1 then nothing will be partially shifted                           */
  int s1bytes;		/* number of bytes which flow to the first  new node when S[0] splits	*/
            			/* note: if S[0] splits into 3 nodes, then items do not need to be cut	*/
  int s2bytes;
  struct buffer_head * buf_to_free[MAX_FREE_BLOCK]; /* buffers which are to be freed after do_balance finishes by unfix_nodes */
  char * vn_buf;		/* kmalloced memory. Used to create
				   virtual node and keep map of
				   dirtied bitmap blocks */
  int vn_buf_size;		/* size of the vn_buf */
  struct virtual_node * tb_vn;	/* VN starts after bitmap of bitmap blocks */

  int fs_gen;                  /* saved value of `reiserfs_generation' counter
			          see FILESYSTEM_CHANGED() macro in reiserfs_fs.h */
} ;


#if 0
				/* when balancing we potentially affect a 3 node wide column of nodes
                                   in the tree (the top of the column may be tapered). C is the nodes
                                   at the center of this column, and L and R are the nodes to the
                                   left and right.  */
  struct seal * L_path_seals[MAX_HEIGHT];
  struct seal * C_path_seals[MAX_HEIGHT];
  struct seal * R_path_seals[MAX_HEIGHT];
  char L_path_lock_types[MAX_HEIGHT];   /* 'r', 'w', or 'n' for read, write, or none */
  char C_path_lock_types[MAX_HEIGHT];
  char R_path_lock_types[MAX_HEIGHT];


  struct seal_list_elem * C_seal[MAX_HEIGHT];        /* array of seals on nodes in the path */
  struct seal_list_elem * L_seal[MAX_HEIGHT];        /* array of seals on left neighbors of nodes in the path */
  struct seal_list_elem * R_seal[MAX_HEIGHT];        /* array of seals on right neighbors of nodes in the path*/
  struct seal_list_elem * FL_seal[MAX_HEIGHT];       /* array of seals on fathers of the left  neighbors      */
  struct seal_list_elem * FR_seal[MAX_HEIGHT];       /* array of seals on fathers of the right neighbors      */
  struct seal_list_elem * CFL_seal[MAX_HEIGHT];      /* array of seals on common parents of center node and its left neighbor  */
  struct seal_list_elem * CFR_seal[MAX_HEIGHT];      /* array of seals on common parents of center node and its right neighbor */
 
  struct char C_desired_lock_type[MAX_HEIGHT]; /* 'r', 'w', or 'n' for read, write, or none */
  struct char L_desired_lock_type[MAX_HEIGHT];        
  struct char R_desired_lock_type[MAX_HEIGHT];        
  struct char FL_desired_lock_type[MAX_HEIGHT];       
  struct char FR_desired_lock_type[MAX_HEIGHT];       
  struct char CFL_desired_lock_type[MAX_HEIGHT];      
  struct char CFR_desired_lock_type[MAX_HEIGHT];      
#endif





/* These are modes of balancing */

/* When inserting an item. */
#define M_INSERT	'i'
/* When inserting into (directories only) or appending onto an already
   existant item. */
#define M_PASTE		'p'
/* When deleting an item. */
#define M_DELETE	'd'
/* When truncating an item or removing an entry from a (directory) item. */
#define M_CUT 		'c'

/* used when balancing on leaf level skipped (in reiserfsck) */
#define M_INTERNAL	'n'

/* When further balancing is not needed, then do_balance does not need
   to be called. */
#define M_SKIP_BALANCING 		's'
#define M_CONVERT	'v'

/* modes of leaf_move_items */
#define LEAF_FROM_S_TO_L 0
#define LEAF_FROM_S_TO_R 1
#define LEAF_FROM_R_TO_L 2
#define LEAF_FROM_L_TO_R 3
#define LEAF_FROM_S_TO_SNEW 4

#define FIRST_TO_LAST 0
#define LAST_TO_FIRST 1

/* used in do_balance for passing parent of node information that has
   been gotten from tb struct */
struct buffer_info {
    struct tree_balance * tb;
    struct buffer_head * bi_bh;
    struct buffer_head * bi_parent;
    int bi_position;
};


/* there are 4 types of items: stat data, directory item, indirect, direct.
+-------------------+------------+--------------+------------+
|	            |  k_offset  | k_uniqueness | mergeable? |
+-------------------+------------+--------------+------------+
|     stat data     |	0        |      0       |   no       |
+-------------------+------------+--------------+------------+
| 1st directory item| DOT_OFFSET |DIRENTRY_UNIQUENESS|   no       | 
| non 1st directory | hash value |              |   yes      |
|     item          |            |              |            |
+-------------------+------------+--------------+------------+
| indirect item     | offset + 1 |TYPE_INDIRECT |   if this is not the first indirect item of the object
+-------------------+------------+--------------+------------+
| direct item       | offset + 1 |TYPE_DIRECT   | if not this is not the first direct item of the object
+-------------------+------------+--------------+------------+
*/

struct item_operations {
    int (*bytes_number) (struct item_head * ih, int block_size);
    void (*decrement_key) (struct cpu_key *);
    int (*is_left_mergeable) (struct key * ih, unsigned long bsize);
    void (*print_item) (struct item_head *, char * item);
    void (*check_item) (struct item_head *, char * item);

    int (*create_vi) (struct virtual_node * vn, struct virtual_item * vi, 
		      int is_affected, int insert_size);
    int (*check_left) (struct virtual_item * vi, int free, 
			    int start_skip, int end_skip);
    int (*check_right) (struct virtual_item * vi, int free);
    int (*part_size) (struct virtual_item * vi, int from, int to);
    int (*unit_num) (struct virtual_item * vi);
    void (*print_vi) (struct virtual_item * vi);
};


extern struct item_operations stat_data_ops, indirect_ops, direct_ops, 
  direntry_ops;
extern struct item_operations * item_ops [4];

#define op_bytes_number(ih,bsize)                    item_ops[le_ih_k_type (ih)]->bytes_number (ih, bsize)
#define op_is_left_mergeable(key,bsize)              item_ops[le_key_k_type (le_key_version (key), key)]->is_left_mergeable (key, bsize)
#define op_print_item(ih,item)                       item_ops[le_ih_k_type (ih)]->print_item (ih, item)
#define op_check_item(ih,item)                       item_ops[le_ih_k_type (ih)]->check_item (ih, item)
#define op_create_vi(vn,vi,is_affected,insert_size)  item_ops[le_ih_k_type ((vi)->vi_ih)]->create_vi (vn,vi,is_affected,insert_size)
#define op_check_left(vi,free,start_skip,end_skip) item_ops[(vi)->vi_index]->check_left (vi, free, start_skip, end_skip)
#define op_check_right(vi,free)                      item_ops[(vi)->vi_index]->check_right (vi, free)
#define op_part_size(vi,from,to)                     item_ops[(vi)->vi_index]->part_size (vi, from, to)
#define op_unit_num(vi)				     item_ops[(vi)->vi_index]->unit_num (vi)
#define op_print_vi(vi)                              item_ops[(vi)->vi_index]->print_vi (vi)





#define COMP_KEYS comp_keys
#define COMP_SHORT_KEYS comp_short_keys
#define keys_of_same_object comp_short_keys

/*#define COMP_KEYS(p_s_key1, p_s_key2)		comp_keys((unsigned long *)(p_s_key1), (unsigned long *)(p_s_key2))
#define COMP_SHORT_KEYS(p_s_key1, p_s_key2)	comp_short_keys((unsigned long *)(p_s_key1), (unsigned long *)(p_s_key2))*/


/* number of blocks pointed to by the indirect item */
#define I_UNFM_NUM(p_s_ih)	( (p_s_ih)->ih_item_len / UNFM_P_SIZE )

/* the used space within the unformatted node corresponding to pos within the item pointed to by ih */
#define I_POS_UNFM_SIZE(ih,pos,size) (((pos) == I_UNFM_NUM(ih) - 1 ) ? (size) - (ih)->u.ih_free_space : (size))

/* number of bytes contained by the direct item or the unformatted nodes the indirect item points to */


/* get the item header */ 
#define B_N_PITEM_HEAD(bh,item_num) ( (struct item_head * )((bh)->b_data + BLKH_SIZE) + (item_num) )

/* get key */
#define B_N_PDELIM_KEY(bh,item_num) ( (struct key * )((bh)->b_data + BLKH_SIZE) + (item_num) )

/* get the key */
#define B_N_PKEY(bh,item_num) ( &(B_N_PITEM_HEAD(bh,item_num)->ih_key) )

/* get item body */
#define B_N_PITEM(bh,item_num) ( (bh)->b_data + B_N_PITEM_HEAD((bh),(item_num))->ih_item_location)

/* get the stat data by the buffer header and the item order */
#define B_N_STAT_DATA(bh,nr) \
( (struct stat_data *)((bh)->b_data+B_N_PITEM_HEAD((bh),(nr))->ih_item_location ) )

                 /* following defines use reiserfs buffer header and item header */

/* get stat-data */
#define B_I_STAT_DATA(bh, ih) ( (struct stat_data * )((bh)->b_data + (ih)->ih_item_location) )

// this is 3976 for size==4096
#define MAX_DIRECT_ITEM_LEN(size) ((size) - BLKH_SIZE - 2*IH_SIZE - SD_SIZE - UNFM_P_SIZE)

/* indirect items consist of entries which contain blocknrs, pos
   indicates which entry, and B_I_POS_UNFM_POINTER resolves to the
   blocknr contained by the entry pos points to */
#define B_I_POS_UNFM_POINTER(bh,ih,pos) (*(((unp_t *)B_I_PITEM(bh,ih)) + (pos)))
#define PUT_B_I_POS_UNFM_POINTER(bh,ih,pos, val) do {*(((unp_t *)B_I_PITEM(bh,ih)) + (pos)) = cpu_to_le32(val); } while (0)

/* Reiserfs buffer cache statistics. */
#ifdef REISERFS_CACHE_STAT
 struct reiserfs_cache_stat
	{
  	int nr_reiserfs_ll_r_block; 		/* Number of block reads. */
  	int nr_reiserfs_ll_w_block; 		/* Number of block writes. */
	int nr_reiserfs_schedule; 		/* Number of locked buffers waits. */
	unsigned long nr_reiserfs_bread;	/* Number of calls to reiserfs_bread function */
	unsigned long nr_returns; /* Number of breads of buffers that were hoped to contain a key but did not after bread completed
				     (usually due to object shifting while bread was executing.)
				     In the code this manifests as the number
				     of times that the repeat variable is nonzero in search_by_key.*/
	unsigned long nr_fixed;		/* number of calls of fix_nodes function */
	unsigned long nr_failed;	/* number of calls of fix_nodes in which schedule occurred while the function worked */
	unsigned long nr_find1;		/* How many times we access a child buffer using its direct pointer from an internal node.*/
	unsigned long nr_find2;	        /* Number of times there is neither a direct pointer to
					   nor any entry in the child list pointing to the buffer. */
	unsigned long nr_find3;	        /* When parent is locked (meaning that there are no direct pointers)
					   or parent is leaf and buffer to be found is an unformatted node. */
	}  cache_stat;
#endif

struct reiserfs_iget4_args {
    __u32 objectid ;
} ;

/***************************************************************************/
/*                    FUNCTION DECLARATIONS                                */
/***************************************************************************/

/*#ifdef __KERNEL__*/

/* journal.c see journal.c for all the comments here */

#define JOURNAL_TRANS_HALF 1018   /* must be correct to keep the desc and commit structs at 4k */


/* first block written in a commit.  */
struct reiserfs_journal_desc {
  __u32 j_trans_id ;			/* id of commit */
  __u32 j_len ;			/* length of commit. len +1 is the commit block */
  __u32 j_mount_id ;				/* mount id of this trans*/
  __u32 j_realblock[JOURNAL_TRANS_HALF] ; /* real locations for each block */
  char j_magic[12] ;
} ;

/* last block written in a commit */
struct reiserfs_journal_commit {
  __u32 j_trans_id ;			/* must match j_trans_id from the desc block */
  __u32 j_len ;			/* ditto */
  __u32 j_realblock[JOURNAL_TRANS_HALF] ; /* real locations for each block */
  char j_digest[16] ;			/* md5 sum of all the blocks involved, including desc and commit. not used, kill it */
} ;

/* this header block gets written whenever a transaction is considered fully flushed, and is more recent than the
** last fully flushed transaction.  fully flushed means all the log blocks and all the real blocks are on disk,
** and this transaction does not need to be replayed.
*/
struct reiserfs_journal_header {
  __u32 j_last_flush_trans_id ;		/* id of last fully flushed transaction */
  __u32 j_first_unflushed_offset ;      /* offset in the log of where to start replay after a crash */
  __u32 j_mount_id ;
} ;

/* these are used to keep flush pages that contain converted direct items.
** if the page is not flushed before the transaction that converted it
** is committed, we risk losing data
**
** note, while a page is in this list, its counter is incremented.
*/
struct reiserfs_page_list {
  struct reiserfs_page_list *next ;
  struct reiserfs_page_list *prev ;
  struct page *page ;
  unsigned long blocknr ; /* block number holding converted data */

  /* if a transaction writer has the page locked the flush_page_list
  ** function doesn't need to (and can't) get the lock while flushing
  ** the page.  do_not_lock needs to be set by anyone who calls journal_end
  ** with a page lock held.  They have to look in the inode and see
  ** if the inode has the page they have locked in the flush list.
  **
  ** this sucks.
  */
  int do_not_lock ; 
} ;

extern task_queue reiserfs_commit_thread_tq ;
extern wait_queue_head_t reiserfs_commit_thread_wait ;

/* biggest tunable defines are right here */
#define JOURNAL_BLOCK_COUNT 8192 /* number of blocks in the journal */
#define JOURNAL_MAX_BATCH   900 /* max blocks to batch into one transaction, don't make this any bigger than 900 */
#define JOURNAL_MAX_COMMIT_AGE 30 
#define JOURNAL_MAX_TRANS_AGE 30
#define JOURNAL_PER_BALANCE_CNT 12   /* must be >= (5 + 2 * (MAX_HEIGHT-2) + 1) */

/* both of these can be as low as 1, or as high as you want.  The min is the
** number of 4k bitmap nodes preallocated on mount. New nodes are allocated
** as needed, and released when transactions are committed.  On release, if 
** the current number of nodes is > max, the node is freed, otherwise, 
** it is put on a free list for faster use later.
*/
#define REISERFS_MIN_BITMAP_NODES 10 
#define REISERFS_MAX_BITMAP_NODES 100 

#define JBH_HASH_SHIFT 13 /* these are based on journal hash size of 8192 */
#define JBH_HASH_MASK 8191

/* After several hours of tedious analysis, the following hash
 * function won.  Do not mess with it... -DaveM
 */
#define _jhashfn(dev,block)	\
	((((dev)<<(JBH_HASH_SHIFT - 6)) ^ ((dev)<<(JBH_HASH_SHIFT - 9))) ^ \
	 (((block)<<(JBH_HASH_SHIFT - 6)) ^ ((block) >> 13) ^ ((block) << (JBH_HASH_SHIFT - 12))))
#define journal_hash(t,dev,block) ((t)[_jhashfn((dev),(block)) & JBH_HASH_MASK])

/* finds n'th buffer with 0 being the start of this commit.  Needs to go away, j_ap_blocks has changed
** since I created this.  One chunk of code in journal.c needs changing before deleting it
*/
#define JOURNAL_BUFFER(j,n) ((j)->j_ap_blocks[((j)->j_start + (n)) % JOURNAL_BLOCK_COUNT])

void reiserfs_wait_on_write_block(struct super_block *s) ;
void reiserfs_block_writes(struct reiserfs_transaction_handle *th) ;
void reiserfs_allow_writes(struct super_block *s) ;
void reiserfs_check_lock_depth(char *caller) ;
void reiserfs_prepare_for_journal(struct super_block *, struct buffer_head *bh, int wait) ;
void reiserfs_restore_prepared_buffer(struct super_block *, struct buffer_head *bh) ;
int journal_init(struct super_block *) ;
int journal_release(struct reiserfs_transaction_handle*, struct super_block *) ;
int journal_release_error(struct reiserfs_transaction_handle*, struct super_block *) ;
int journal_end(struct reiserfs_transaction_handle *, struct super_block *, unsigned long) ;
int journal_end_sync(struct reiserfs_transaction_handle *, struct super_block *, unsigned long) ;
int journal_mark_dirty_nolog(struct reiserfs_transaction_handle *, struct super_block *, struct buffer_head *bh) ;
int journal_mark_freed(struct reiserfs_transaction_handle *, struct super_block *, unsigned long blocknr) ;
int push_journal_writer(char *w) ;
int pop_journal_writer(int windex) ;
int journal_lock_dobalance(struct super_block *p_s_sb) ;
int journal_unlock_dobalance(struct super_block *p_s_sb) ;
int journal_transaction_should_end(struct reiserfs_transaction_handle *, int) ;
int reiserfs_in_journal(struct super_block *p_s_sb, kdev_t dev, unsigned long bl, int size, int searchall, unsigned long *next) ;
int journal_begin(struct reiserfs_transaction_handle *, struct super_block *p_s_sb, unsigned long) ;
int journal_join(struct reiserfs_transaction_handle *, struct super_block *p_s_sb, unsigned long) ;
struct super_block *reiserfs_get_super(kdev_t dev) ;
void flush_async_commits(struct super_block *p_s_sb) ;

int remove_from_transaction(struct super_block *p_s_sb, unsigned long blocknr, int already_cleaned) ;
int remove_from_journal_list(struct super_block *s, struct reiserfs_journal_list *jl, struct buffer_head *bh, int remove_freed) ;

int buffer_journaled(struct buffer_head *bh) ;
int mark_buffer_journal_new(struct buffer_head *bh) ;
int reiserfs_sync_all_buffers(kdev_t dev, int wait) ;
int reiserfs_sync_buffers(kdev_t dev, int wait) ;
int reiserfs_add_page_to_flush_list(struct reiserfs_transaction_handle *,
                                    struct inode *, struct buffer_head *) ;
int reiserfs_remove_page_from_flush_list(struct reiserfs_transaction_handle *,
                                         struct inode *) ;

int reiserfs_allocate_list_bitmaps(struct super_block *s, struct reiserfs_list_bitmap *, int) ;

				/* why is this kerplunked right here? */
static inline int reiserfs_buffer_prepared(struct buffer_head *bh) {
  if (bh && test_bit(BH_JPrepared, &bh->b_state))
    return 1 ;
  else
    return 0 ;
}

/* buffer was journaled, waiting to get to disk */
static inline int buffer_journal_dirty(struct buffer_head *bh) {
  if (bh)
    return test_bit(BH_JDirty_wait, &bh->b_state) ;
  else
    return 0 ;
}
static inline int mark_buffer_notjournal_dirty(struct buffer_head *bh) {
  if (bh)
    clear_bit(BH_JDirty_wait, &bh->b_state) ;
  return 0 ;
}
static inline int mark_buffer_notjournal_new(struct buffer_head *bh) {
  if (bh) {
    clear_bit(BH_JNew, &bh->b_state) ;
  }
  return 0 ;
}

/* objectid.c */
__u32 reiserfs_get_unused_objectid (struct reiserfs_transaction_handle *th);
void reiserfs_release_objectid (struct reiserfs_transaction_handle *th, __u32 objectid_to_release);
int reiserfs_convert_objectid_map_v1(struct super_block *) ;

/* stree.c */
int B_IS_IN_TREE(struct buffer_head *);
extern inline void copy_key (void * to, void * from);
extern inline void copy_short_key (void * to, void * from);
extern inline void copy_item_head(void * p_v_to, void * p_v_from);

// first key is in cpu form, second - le
extern inline int comp_keys (struct key * le_key, struct cpu_key * cpu_key);
extern inline int  comp_short_keys (struct key * le_key, struct cpu_key * cpu_key);
extern inline void le_key2cpu_key (struct cpu_key * to, struct key * from);

// both are cpu keys
extern inline int comp_cpu_keys (struct cpu_key *, struct cpu_key *);
extern inline int comp_short_cpu_keys (struct cpu_key *, struct cpu_key *);
extern inline void cpu_key2cpu_key (struct cpu_key *, struct cpu_key *);

// both are in le form
extern inline int comp_le_keys (struct key *, struct key *);
extern inline int comp_short_le_keys (struct key *, struct key *);

//
// get key version from on disk key - kludge
//
extern inline int le_key_version (struct key * key)
{
    int type;
    
    type = le16_to_cpu (key->u.k_offset_v2.k_type);
    if (type != TYPE_DIRECT && type != TYPE_INDIRECT && type != TYPE_DIRENTRY)
	return ITEM_VERSION_1;

    return ITEM_VERSION_2;
	
}


extern inline void copy_key (void * to, void * from)
{
  memcpy (to, from, KEY_SIZE);
}


int comp_items (struct item_head  * p_s_ih, struct path * p_s_path);
struct key * get_rkey (struct path * p_s_chk_path, struct super_block  * p_s_sb);
inline int bin_search (void * p_v_key, void * p_v_base, int p_n_num, int p_n_width, int * p_n_pos);
int search_by_key (struct super_block *, struct cpu_key *, struct path *, int);
#define search_item(s,key,path) search_by_key (s, key, path, DISK_LEAF_NODE_LEVEL)
int search_for_position_by_key (struct super_block * p_s_sb, struct cpu_key * p_s_cpu_key, struct path * p_s_search_path);
extern inline void decrement_bcount (struct buffer_head * p_s_bh);
void decrement_counters_in_path (struct path * p_s_search_path);
void pathrelse (struct path * p_s_search_path);
int reiserfs_check_path(struct path *p) ;
void pathrelse_and_restore (struct super_block *s, struct path * p_s_search_path);

int reiserfs_insert_item (struct reiserfs_transaction_handle *th, 
			  struct path * path, 
			  struct cpu_key * key,
			  struct item_head * ih, const char * body);

int reiserfs_paste_into_item (struct reiserfs_transaction_handle *th,
			      struct path * path,
			      struct cpu_key * key,
			      const char * body, int paste_size);

int reiserfs_cut_from_item (struct reiserfs_transaction_handle *th,
			    struct path * path,
			    struct cpu_key * key,
			    struct inode * inode,
			    struct page *page,
			    loff_t new_file_size);

int reiserfs_delete_item (struct reiserfs_transaction_handle *th,
			  struct path * path, 
			  struct cpu_key * key,
			  struct inode * inode, 
			  struct buffer_head  * p_s_un_bh);


void reiserfs_delete_object (struct reiserfs_transaction_handle *th, struct inode * p_s_inode);
void reiserfs_do_truncate (struct reiserfs_transaction_handle *th, 
			   struct  inode * p_s_inode, struct page *, 
			   int update_timestamps);
//
//void lock_inode_to_convert (struct inode * p_s_inode);
//void unlock_inode_after_convert (struct inode * p_s_inode);
//void increment_i_read_sync_counter (struct inode * p_s_inode);
//void decrement_i_read_sync_counter (struct inode * p_s_inode);


#define block_size(inode) ((inode)->i_sb->s_blocksize)
#define file_size(inode) ((inode)->i_size)
#define tail_size(inode) (file_size (inode) & (block_size (inode) - 1))

#define tail_has_to_be_packed(inode) (!dont_have_tails ((inode)->i_sb) &&\
!STORE_TAIL_IN_UNFM(file_size (inode), tail_size(inode), block_size (inode)))

/*
int get_buffer_by_range (struct super_block * p_s_sb, struct key * p_s_range_begin, struct key * p_s_range_end, 
			 struct buffer_head ** pp_s_buf, unsigned long * p_n_objectid);
int get_buffers_from_range (struct super_block * p_s_sb, struct key * p_s_range_start, struct key * p_s_range_end, 
                            struct buffer_head ** p_s_range_buffers,
			    int n_max_nr_buffers_to_return);
*/

#ifndef REISERFS_FSCK

//inline int is_left_mergeable (struct item_head * ih, unsigned long bsize);

#else

int is_left_mergeable (struct super_block * s, struct path * path);
int is_right_mergeable (struct super_block * s, struct path * path);
int are_items_mergeable (struct item_head * left, struct item_head * right, int bsize);

#endif
void padd_item (char * item, int total_length, int length);


/* inode.c */

int reiserfs_prepare_write(struct file *, struct page *, unsigned, unsigned) ;
void reiserfs_truncate_file(struct inode *, int update_timestamps) ;
void make_cpu_key (struct cpu_key * cpu_key, const struct inode * inode, loff_t offset,
		   int type, int key_length);
void make_le_item_head (struct item_head * ih, struct cpu_key * key, int version,
			       loff_t offset, int type, int length, int entry_count);
/*void store_key (struct key * key);
void forget_key (struct key * key);*/
int reiserfs_get_block (struct inode * inode, long block,
			struct buffer_head * bh_result, int create);
struct inode * reiserfs_iget (struct super_block * s, struct cpu_key * key);
void reiserfs_read_inode (struct inode * inode) ;
void reiserfs_read_inode2(struct inode * inode, void *p) ;
void reiserfs_delete_inode (struct inode * inode);
extern int reiserfs_notify_change(struct dentry * dentry, struct iattr * attr);
void reiserfs_write_inode (struct inode * inode, int) ;

/* we don't mark inodes dirty, we just log them */
void reiserfs_dirty_inode (struct inode * inode) ;

struct inode * reiserfs_new_inode (struct reiserfs_transaction_handle *th, const struct inode * dir, int mode, 
				   const char * symname, int item_len,
				   struct dentry *dentry, struct inode *inode, int * err);
int reiserfs_sync_inode (struct reiserfs_transaction_handle *th, struct inode * inode);
void reiserfs_update_sd (struct reiserfs_transaction_handle *th, struct inode * inode);
int reiserfs_inode_setattr(struct dentry *,  struct iattr * attr);

/* namei.c */
inline void set_de_name_and_namelen (struct reiserfs_dir_entry * de);
int search_by_entry_key (struct super_block * sb, struct cpu_key * key, struct path * path, 
			 struct reiserfs_dir_entry * de);
struct dentry * reiserfs_lookup (struct inode * dir, struct dentry *dentry);
int reiserfs_create (struct inode * dir, struct dentry *dentry,	int mode);
int reiserfs_mknod (struct inode * dir_inode, struct dentry *dentry, int mode, int rdev);
int reiserfs_mkdir (struct inode * dir, struct dentry *dentry, int mode);
int reiserfs_rmdir (struct inode * dir,	struct dentry *dentry);
int reiserfs_unlink (struct inode * dir, struct dentry *dentry);
int reiserfs_symlink (struct inode * dir, struct dentry *dentry, const char * symname);
int reiserfs_link (struct dentry * old_dentry, struct inode * dir, struct dentry *dentry);
int reiserfs_rename (struct inode * old_dir, struct dentry *old_dentry, struct inode * new_dir, struct dentry *new_dentry);

/* super.c */
inline void reiserfs_mark_buffer_dirty (struct buffer_head * bh, int flag);
inline void reiserfs_mark_buffer_clean (struct buffer_head * bh);
void reiserfs_panic (struct super_block * s, const char * fmt, ...);
void reiserfs_write_super (struct super_block * s);
void reiserfs_put_super (struct super_block * s);
int reiserfs_remount (struct super_block * s, int * flags, char * data);
/*int read_super_block (struct super_block * s, int size);
int read_bitmaps (struct super_block * s);
int read_old_bitmaps (struct super_block * s);
int read_old_super_block (struct super_block * s, int size);*/
struct super_block * reiserfs_read_super (struct super_block * s, void * data, int silent);
int reiserfs_statfs (struct super_block * s, struct statfs * buf);

/* dir.c */
extern struct inode_operations reiserfs_dir_inode_operations;
extern struct file_operations reiserfs_dir_operations;

/* tail_conversion.c */
int direct2indirect (struct reiserfs_transaction_handle *, struct inode *, struct path *, struct buffer_head *, loff_t);
int indirect2direct (struct reiserfs_transaction_handle *, struct inode *, struct page *, struct path *, struct cpu_key *, loff_t, char *);
void reiserfs_unmap_buffer(struct buffer_head *) ;


/* file.c */
extern struct inode_operations reiserfs_file_inode_operations;
extern struct file_operations reiserfs_file_operations;
extern struct address_space_operations reiserfs_address_space_operations ;
int get_new_buffer (struct reiserfs_transaction_handle *th, struct buffer_head *,
		    struct buffer_head **, struct path *);


/* buffer2.c */
struct buffer_head * reiserfs_getblk (kdev_t n_dev, int n_block, int n_size);
void wait_buffer_until_released (struct buffer_head * bh);
struct buffer_head * reiserfs_bread (kdev_t n_dev, int n_block, int n_size);


/* fix_nodes.c */
void * reiserfs_kmalloc (size_t size, int flags, struct super_block * s);
void reiserfs_kfree (const void * vp, size_t size, struct super_block * s);
int fix_nodes (int n_op_mode, struct tree_balance * p_s_tb, struct item_head * p_s_ins_ih, const void *);
void unfix_nodes (struct tree_balance *);
void free_buffers_in_tb (struct tree_balance * p_s_tb);


/* prints.c */
void reiserfs_panic (struct super_block * s, const char * fmt, ...);
void reiserfs_warning (const char * fmt, ...);
void reiserfs_debug (struct super_block *s, int level, const char * fmt, ...);
void print_virtual_node (struct virtual_node * vn);
void print_indirect_item (struct buffer_head * bh, int item_num);
void store_print_tb (struct tree_balance * tb);
void print_cur_tb (char * mes);
void print_de (struct reiserfs_dir_entry * de);
void print_bi (struct buffer_info * bi, char * mes);
#define PRINT_LEAF_ITEMS 1   /* print all items */
#define PRINT_DIRECTORY_ITEMS 2 /* print directory items */
#define PRINT_DIRECT_ITEMS 4 /* print contents of direct items */
void print_block (struct buffer_head * bh, ...);
void print_path (struct tree_balance * tb, struct path * path);
void print_bmap (struct super_block * s, int silent);
void print_bmap_block (int i, char * data, int size, int silent);
/*void print_super_block (struct super_block * s, char * mes);*/
void print_objectid_map (struct super_block * s);
void print_block_head (struct buffer_head * bh, char * mes);
void check_leaf (struct buffer_head * bh);
void check_internal (struct buffer_head * bh);
void print_statistics (struct super_block * s);

/* lbalance.c */
int leaf_move_items (int shift_mode, struct tree_balance * tb, int mov_num, int mov_bytes, struct buffer_head * Snew);
int leaf_shift_left (struct tree_balance * tb, int shift_num, int shift_bytes);
int leaf_shift_right (struct tree_balance * tb, int shift_num, int shift_bytes);
void leaf_delete_items (struct buffer_info * cur_bi, int last_first, int first, int del_num, int del_bytes);
void leaf_insert_into_buf (struct buffer_info * bi, int before, 
                           struct item_head * inserted_item_ih, const char * inserted_item_body, int zeros_number);
void leaf_paste_in_buffer (struct buffer_info * bi, int pasted_item_num, 
                           int pos_in_item, int paste_size, const char * body, int zeros_number);
void leaf_cut_from_buffer (struct buffer_info * bi, int cut_item_num, int pos_in_item, 
                           int cut_size);
void leaf_paste_entries (struct buffer_head * bh, int item_num, int before, 
                         int new_entry_count, struct reiserfs_de_head * new_dehs, const char * records, int paste_size);
/* ibalance.c */
int balance_internal (struct tree_balance * , int, int, struct item_head * , 
                      struct buffer_head **);

/* do_balance.c */
inline void do_balance_mark_leaf_dirty (struct tree_balance * tb, 
					struct buffer_head * bh, int flag);
#define do_balance_mark_internal_dirty do_balance_mark_leaf_dirty
#define do_balance_mark_sb_dirty do_balance_mark_leaf_dirty

void do_balance (struct tree_balance * tb, struct item_head * ih, 
                 const char * body, int flag);
void reiserfs_invalidate_buffer (struct tree_balance * tb, struct buffer_head * bh);

int get_left_neighbor_position (struct tree_balance * tb, int h);
int get_right_neighbor_position (struct tree_balance * tb, int h);
void replace_key (struct tree_balance * tb, struct buffer_head *, int, struct buffer_head *, int);
void replace_lkey (struct tree_balance *, int, struct item_head *);
void replace_rkey (struct tree_balance *, int, struct item_head *);
void make_empty_node (struct buffer_info *);
struct buffer_head * get_FEB (struct tree_balance *);

/* bitmap.c */
int is_reusable (struct super_block * s, unsigned long block, int bit_value);
void reiserfs_free_block (struct reiserfs_transaction_handle *th, unsigned long);
int reiserfs_new_blocknrs (struct reiserfs_transaction_handle *th,
			   unsigned long * pblocknrs, unsigned long start_from, int amount_needed);
int reiserfs_new_unf_blocknrs (struct reiserfs_transaction_handle *th,
			       unsigned long * pblocknr, unsigned long start_from);
#ifdef REISERFS_PREALLOCATE
int reiserfs_new_unf_blocknrs2 (struct reiserfs_transaction_handle *th, 
				struct inode * inode,
				unsigned long * pblocknr, 
				unsigned long start_from);

void reiserfs_discard_prealloc (struct reiserfs_transaction_handle *th, 
				struct inode * inode);
#endif

/* hashes.c */
__u32 keyed_hash (const char *msg, int len);
__u32 yura_hash (const char *msg, int len);
__u32 r5_hash (const char *msg, int len);

/* version.c */
char *reiserfs_get_version_string(void) ;

/* the ext2 bit routines adjust for big or little endian as
** appropriate for the arch, so in our laziness we use them rather
** than using the bit routines they call more directly.  These
** routines must be used when changing on disk bitmaps.  */
#define reiserfs_test_and_set_le_bit   ext2_set_bit
#define reiserfs_test_and_clear_le_bit ext2_clear_bit
#define reiserfs_test_le_bit           ext2_test_bit
#define reiserfs_find_next_zero_le_bit ext2_find_next_zero_bit


//
// this was totally copied from from linux's
// find_first_zero_bit and changed a bit
//

#ifdef __i386__

extern __inline__ int 
find_first_nonzero_bit(void * addr, unsigned size) {
  int res;
  int __d0;
  void *__d1;


  if (!size) {
    return (0);
  }
  __asm__ __volatile__ (
	  "cld\n\t"
	  "xorl %%eax,%%eax\n\t"
	  "repe; scasl\n\t"
	  "je 1f\n\t"
	  "movl -4(%%edi),%%eax\n\t"
	  "subl $4, %%edi\n\t"
	  "bsfl %%eax,%%eax\n\t"
	  "1:\tsubl %%edx,%%edi\n\t"
	  "shll $3,%%edi\n\t"
	  "addl %%edi,%%eax"
	  :"=a" (res),
	  "=c"(__d0), "=D"(__d1)
	  :"1" ((size + 31) >> 5), "d" (addr), "2" (addr));
  return (res);
}

#else /* __i386__ */

extern __inline__ int find_next_nonzero_bit(void * addr, unsigned size, unsigned offset)
{
	unsigned int * p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *p++;
		/* set to zero first offset bits */
		tmp &= ~(~0UL >> (32-offset));
		if (size < 32)
			goto found_first;
		if (tmp != 0U)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = *p++) != 0U)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;
found_first:
found_middle:
	return result + ffs(tmp);
}

#define find_first_nonzero_bit(addr,size) find_next_nonzero_bit((addr), (size), 0)

#endif /* 0 */

/* sometimes reiserfs_truncate may require to allocate few new blocks
   to perform indirect2direct conversion. People probably used to
   think, that truncate should work without problems on a filesystem
   without free disk space. They may complain that they can not
   truncate due to lack of free disk space. This spare space allows us
   to not worry about it. 500 is probably too much, but it should be
   absolutely safe */
#define SPARE_SPACE 500

extern inline unsigned long reiserfs_get_journal_block(struct super_block *s) {
    return le32_to_cpu(SB_DISK_SUPER_BLOCK(s)->s_journal_block) ;
}
extern inline unsigned long reiserfs_get_journal_orig_size(struct super_block *s) {
    return le32_to_cpu(SB_DISK_SUPER_BLOCK(s)->s_orig_journal_size) ;
}

/* prototypes from ioctl.c */
int reiserfs_ioctl (struct inode * inode, struct file * filp, 
 		    unsigned int cmd, unsigned long arg);
int reiserfs_unpack (struct inode * inode, struct file * filp);
 
/* ioctl's command */
#define REISERFS_IOC_UNPACK		_IOW(0xCD,1,long)
 			         
#endif /* _LINUX_REISER_FS_H */


