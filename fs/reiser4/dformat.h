/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Formats of on-disk data and conversion functions. */

/* put all item formats in the files describing the particular items,
   our model is, everything you need to do to add an item to reiser4,
   (excepting the changes to the plugin that uses the item which go
   into the file defining that plugin), you put into one file. */
/* Data on disk are stored in little-endian format.
   To declare fields of on-disk structures, use d8, d16, d32 and d64.
   d??tocpu() and cputod??() to convert. */

#if !defined( __FS_REISER4_DFORMAT_H__ )
#define __FS_REISER4_DFORMAT_H__


#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/types.h>

/* our default disk byteorder is little endian */

#if defined( __LITTLE_ENDIAN )
#define CPU_IN_DISK_ORDER  (1)
#else
#define CPU_IN_DISK_ORDER  (0)
#endif

/* code on-disk data-types as structs with a single field
   to rely on compiler type-checking. Like include/asm-i386/page.h */
typedef struct d8 {
	__u8 datum;
} d8 __attribute__ ((aligned(1)));
typedef struct d16 {
	__u16 datum;
} d16 __attribute__ ((aligned(2)));
typedef struct d32 {
	__u32 datum;
} d32 __attribute__ ((aligned(4)));
typedef struct d64 {
	__u64 datum;
} d64 __attribute__ ((aligned(8)));

#define PACKED __attribute__((packed))

static inline __u8
d8tocpu(const d8 * ondisk /* on-disk value to convert */ )
{
	return ondisk->datum;
}

static inline __u16
d16tocpu(const d16 * ondisk /* on-disk value to convert */ )
{
	return __le16_to_cpu(get_unaligned(&ondisk->datum));
}

static inline __u32
d32tocpu(const d32 * ondisk /* on-disk value to convert */ )
{
	return __le32_to_cpu(get_unaligned(&ondisk->datum));
}

static inline __u64
d64tocpu(const d64 * ondisk /* on-disk value to convert */ )
{
	return __le64_to_cpu(get_unaligned(&ondisk->datum));
}

static inline d8 *
cputod8(unsigned int oncpu /* CPU value to convert */ ,
	d8 * ondisk /* result */ )
{
	assert("nikita-1264", oncpu < 0x100);
	put_unaligned(oncpu, &ondisk->datum);
	return ondisk;
}

static inline d16 *
cputod16(unsigned int oncpu /* CPU value to convert */ ,
	 d16 * ondisk /* result */ )
{
	assert("nikita-1265", oncpu < 0x10000);
	put_unaligned(__cpu_to_le16(oncpu), &ondisk->datum);
	return ondisk;
}

static inline d32 *
cputod32(__u32 oncpu /* CPU value to convert */ ,
	 d32 * ondisk /* result */ )
{
	put_unaligned(__cpu_to_le32(oncpu), &ondisk->datum);
	return ondisk;
}

static inline d64 *
cputod64(__u64 oncpu /* CPU value to convert */ ,
	 d64 * ondisk /* result */ )
{
	put_unaligned(__cpu_to_le64(oncpu), &ondisk->datum);
	return ondisk;
}

/* data-type for block number on disk: these types enable changing the block
   size to other sizes, but they are only a start.  Suppose we wanted to
   support 48bit block numbers.  The dblock_nr blk would be changed to "short
   blk[3]".  The block_nr type should remain an integral type greater or equal
   to the dblock_nr type in size so that CPU arithmetic operations work. */
typedef __u64 reiser4_block_nr;

/* data-type for block number on disk, disk format */
union reiser4_dblock_nr {
	d64 blk;
};

static inline reiser4_block_nr
dblock_to_cpu(const reiser4_dblock_nr * dblock)
{
	return d64tocpu(&dblock->blk);
}

static inline void
cpu_to_dblock(reiser4_block_nr block, reiser4_dblock_nr * dblock)
{
	cputod64(block, &dblock->blk);
}

/* true if disk addresses are the same */
static inline int
disk_addr_eq(const reiser4_block_nr * b1	/* first block
						 * number to
						 * compare */ ,
	     const reiser4_block_nr * b2	/* second block
						 * number to
						 * compare */ )
{
	assert("nikita-1033", b1 != NULL);
	assert("nikita-1266", b2 != NULL);

	return !memcmp(b1, b2, sizeof *b1);
}

/* structure of master reiser4 super block */
typedef struct reiser4_master_sb {
	char magic[16];		/* "ReIsEr4" */
	d16 disk_plugin_id;	/* id of disk layout plugin */
	d16 blocksize;
	char uuid[16];		/* unique id */
	char label[16];		/* filesystem label */
	d64 diskmap;		/* location of the diskmap. 0 if not present */
} reiser4_master_sb;

/* __FS_REISER4_DFORMAT_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
