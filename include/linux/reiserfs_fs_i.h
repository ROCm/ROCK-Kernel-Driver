#ifndef _REISER_FS_I
#define _REISER_FS_I

/* these are used to keep track of the pages that need
** flushing before the current transaction can commit
*/
struct reiserfs_page_list ;

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

  /* pointer to the page that must be flushed before 
  ** the current transaction can commit.
  **
  ** this pointer is only used when the tail is converted back into
  ** a direct item, or the file is deleted
  */
  struct reiserfs_page_list *i_converted_page ;

  /* we save the id of the transaction when we did the direct->indirect
  ** conversion.  That allows us to flush the buffers to disk
  ** without having to update this inode to zero out the converted
  ** page variable
  */
  int i_conversion_trans_id ;

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

				/* I regret that you think the below
                                   is a comment you should make.... -Hans */
  //nopack-attribute
  int nopack;
};


#endif
