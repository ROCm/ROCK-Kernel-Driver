#ifndef _REISER_FS_I
#define _REISER_FS_I

#include <linux/list.h>

struct reiserfs_inode_info {
  __u32 i_key [4];/* key is still 4 32 bit integers */
  
				/* this comment will be totally
                                   cryptic to readers not familiar
                                   with 3.5/3.6 format conversion, and
                                   it does not consider that that 3.6
                                   might not be the last version */
  int i_version;  // this says whether file is old or new

  int i_pack_on_close ; // file might need tail packing on close 

  __u32 i_first_direct_byte; // offset of first byte stored in direct item.

				/* My guess is this contains the first
                                   unused block of a sequence of
                                   blocks plus the length of the
                                   sequence, which I think is always
                                   at least two at the time of the
                                   preallocation.  I really prefer
                                   allocate on flush conceptually.....

				   You know, it really annoys me when
				   code is this badly commented that I
				   have to guess what it does.
				   Neither I nor anyone else has time
				   for guessing what your
				   datastructures mean.  -Hans */
  //For preallocation
  int i_prealloc_block;
  int i_prealloc_count;
  struct list_head i_prealloc_list;	/* per-transaction list of inodes which
				 * have preallocated blocks */
				/* I regret that you think the below
                                   is a comment you should make.... -Hans */
  //nopack-attribute
  int nopack;
  
  /* we use these for fsync or O_SYNC to decide which transaction needs
  ** to be committed in order for this inode to be properly flushed
  */
  unsigned long i_trans_id ;
  unsigned long i_trans_index ;
};


#endif
