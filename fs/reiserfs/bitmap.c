/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/reiserfs_fs.h>
#include <linux/locks.h>
#include <asm/bitops.h>
#include <linux/list.h>

#ifdef CONFIG_REISERFS_CHECK

/* this is a safety check to make sure
** blocks are reused properly.  used for debugging only.
**
** this checks, that block can be reused, and it has correct state
**   (free or busy) 
*/
int is_reusable (struct super_block * s, unsigned long block, int bit_value)
{
    int i, j;
  
    if (block == 0 || block >= SB_BLOCK_COUNT (s)) {
	reiserfs_warning ("vs-4010: is_reusable: block number is out of range %lu (%u)\n",
			  block, SB_BLOCK_COUNT (s));
	return 0;
    }

    /* it can't be one of the bitmap blocks */
    for (i = 0; i < SB_BMAP_NR (s); i ++)
	if (block == SB_AP_BITMAP (s)[i]->b_blocknr) {
	    reiserfs_warning ("vs: 4020: is_reusable: "
			      "bitmap block %lu(%u) can't be freed or reused\n",
			      block, SB_BMAP_NR (s));
	    return 0;
	}
  
    i = block / (s->s_blocksize << 3);
    if (i >= SB_BMAP_NR (s)) {
	reiserfs_warning ("vs-4030: is_reusable: there is no so many bitmap blocks: "
			  "block=%lu, bitmap_nr=%d\n", block, i);
	return 0;
    }

    j = block % (s->s_blocksize << 3);
    if ((bit_value == 0 && 
         reiserfs_test_le_bit(j, SB_AP_BITMAP(s)[i]->b_data)) ||
	(bit_value == 1 && 
	 reiserfs_test_le_bit(j, SB_AP_BITMAP (s)[i]->b_data) == 0)) {
	reiserfs_warning ("vs-4040: is_reusable: corresponding bit of block %lu does not "
			  "match required value (i==%d, j==%d) test_bit==%d\n",
		block, i, j, reiserfs_test_le_bit (j, SB_AP_BITMAP (s)[i]->b_data));
	return 0;
    }

    if (bit_value == 0 && block == SB_ROOT_BLOCK (s)) {
	reiserfs_warning ("vs-4050: is_reusable: this is root block (%u), "
			  "it must be busy", SB_ROOT_BLOCK (s));
	return 0;
    }

    return 1;
}




#endif /* CONFIG_REISERFS_CHECK */

/* get address of corresponding bit (bitmap block number and offset in it) */
static inline void get_bit_address (struct super_block * s, unsigned long block, int * bmap_nr, int * offset)
{
                                /* It is in the bitmap block number equal to the block number divided by the number of
                                   bits in a block. */
    *bmap_nr = block / (s->s_blocksize << 3);
                                /* Within that bitmap block it is located at bit offset *offset. */
    *offset = block % (s->s_blocksize << 3);
    return;
}


/* There would be a modest performance benefit if we write a version
   to free a list of blocks at once. -Hans */
				/* I wonder if it would be less modest
                                   now that we use journaling. -Hans */
void reiserfs_free_block (struct reiserfs_transaction_handle *th, unsigned long block)
{
    struct super_block * s = th->t_super;
    struct reiserfs_super_block * rs;
    struct buffer_head * sbh;
    struct buffer_head ** apbh;
    int nr, offset;

  RFALSE(!s, "vs-4060: trying to free block on nonexistent device");
  RFALSE(is_reusable (s, block, 1) == 0, "vs-4070: can not free such block");

  rs = SB_DISK_SUPER_BLOCK (s);
  sbh = SB_BUFFER_WITH_SB (s);
  apbh = SB_AP_BITMAP (s);

  get_bit_address (s, block, &nr, &offset);

  /* mark it before we clear it, just in case */
  journal_mark_freed(th, s, block) ;

  reiserfs_prepare_for_journal(s, apbh[nr], 1 ) ;

  /* clear bit for the given block in bit map */
  if (!reiserfs_test_and_clear_le_bit (offset, apbh[nr]->b_data)) {
      reiserfs_warning ("vs-4080: reiserfs_free_block: "
			"free_block (%04x:%lu)[dev:blocknr]: bit already cleared\n", 
	    s->s_dev, block);
  }
  journal_mark_dirty (th, s, apbh[nr]);

  reiserfs_prepare_for_journal(s, sbh, 1) ;
  /* update super block */
  rs->s_free_blocks = cpu_to_le32 (le32_to_cpu (rs->s_free_blocks) + 1);

  journal_mark_dirty (th, s, sbh);
  s->s_dirt = 1;
}



/* beginning from offset-th bit in bmap_nr-th bitmap block,
   find_forward finds the closest zero bit. It returns 1 and zero
   bit address (bitmap, offset) if zero bit found or 0 if there is no
   zero bit in the forward direction */
/* The function is NOT SCHEDULE-SAFE! */
static int find_forward (struct super_block * s, int * bmap_nr, int * offset, int for_unformatted)
{
  int i, j;
  struct buffer_head * bh;
  unsigned long block_to_try = 0;
  unsigned long next_block_to_try = 0 ;

  for (i = *bmap_nr; i < SB_BMAP_NR (s); i ++, *offset = 0) {
    /* get corresponding bitmap block */
    bh = SB_AP_BITMAP (s)[i];
    if (buffer_locked (bh)) {
        __wait_on_buffer (bh);
    }
retry:
    j = reiserfs_find_next_zero_le_bit ((unsigned long *)bh->b_data, 
                                         s->s_blocksize << 3, *offset);

    /* wow, this really needs to be redone.  We can't allocate a block if
    ** it is in the journal somehow.  reiserfs_in_journal makes a suggestion
    ** for a good block if the one you ask for is in the journal.  Note,
    ** reiserfs_in_journal might reject the block it suggests.  The big
    ** gain from the suggestion is when a big file has been deleted, and
    ** many blocks show free in the real bitmap, but are all not free
    ** in the journal list bitmaps.
    **
    ** this whole system sucks.  The bitmaps should reflect exactly what
    ** can and can't be allocated, and the journal should update them as
    ** it goes.  TODO.
    */
    if (j < (s->s_blocksize << 3)) {
      block_to_try = (i * (s->s_blocksize << 3)) + j; 

      /* the block is not in the journal, we can proceed */
      if (!(reiserfs_in_journal(s, s->s_dev, block_to_try, s->s_blocksize, for_unformatted, &next_block_to_try))) {
	*bmap_nr = i;
	*offset = j;
	return 1;
      } 
      /* the block is in the journal */
      else if ((j+1) < (s->s_blocksize << 3)) { /* try again */
	/* reiserfs_in_journal suggested a new block to try */
	if (next_block_to_try > 0) {
	  int new_i ;
	  get_bit_address (s, next_block_to_try, &new_i, offset);

	  /* block is not in this bitmap. reset i and continue
	  ** we only reset i if new_i is in a later bitmap.
	  */
	  if (new_i > i) {
	    i = (new_i - 1 ); /* i gets incremented by the for loop */
	    continue ;
	  }
	} else {
	  /* no suggestion was made, just try the next block */
	  *offset = j+1 ;
	}
	goto retry ;
      }
    }
  }
    /* zero bit not found */
    return 0;
}

/* return 0 if no free blocks, else return 1 */
/* The function is NOT SCHEDULE-SAFE!  
** because the bitmap block we want to change could be locked, and on its
** way to the disk when we want to read it, and because of the 
** flush_async_commits.  Per bitmap block locks won't help much, and 
** really aren't needed, as we retry later on if we try to set the bit
** and it is already set.
*/
static int find_zero_bit_in_bitmap (struct super_block * s, 
                                    unsigned long search_start, 
				    int * bmap_nr, int * offset, 
				    int for_unformatted)
{
  int retry_count = 0 ;
  /* get bit location (bitmap number and bit offset) of search_start block */
  get_bit_address (s, search_start, bmap_nr, offset);

    /* note that we search forward in the bitmap, benchmarks have shown that it is better to allocate in increasing
       sequence, which is probably due to the disk spinning in the forward direction.. */
    if (find_forward (s, bmap_nr, offset, for_unformatted) == 0) {
      /* there wasn't a free block with number greater than our
         starting point, so we are going to go to the beginning of the disk */

retry:
      search_start = 0; /* caller will reset search_start for itself also. */
      get_bit_address (s, search_start, bmap_nr, offset);
      if (find_forward (s, bmap_nr,offset,for_unformatted) == 0) {
	if (for_unformatted) {	/* why only unformatted nodes? -Hans */
	  if (retry_count == 0) {
	    /* we've got a chance that flushing async commits will free up
	    ** some space.  Sync then retry
	    */
	    flush_async_commits(s) ;
	    retry_count++ ;
	    goto retry ;
	  } else if (retry_count > 0) {
	    /* nothing more we can do.  Make the others wait, flush
	    ** all log blocks to disk, and flush to their home locations.
	    ** this will free up any blocks held by the journal
	    */
	    SB_JOURNAL(s)->j_must_wait = 1 ;
	  }
	}
        return 0;
      }
    }
  return 1;
}

/* get amount_needed free block numbers from scanning the bitmap of
   free/used blocks.
   
   Optimize layout by trying to find them starting from search_start
   and moving in increasing blocknr direction.  (This was found to be
   faster than using a bi-directional elevator_direction, in part
   because of disk spin direction, in part because by the time one
   reaches the end of the disk the beginning of the disk is the least
   congested).

   search_start is the block number of the left
   semantic neighbor of the node we create.

   return CARRY_ON if everything is ok
   return NO_DISK_SPACE if out of disk space
   return NO_MORE_UNUSED_CONTIGUOUS_BLOCKS if the block we found is not contiguous to the last one
   
   return block numbers found, in the array free_blocknrs.  assumes
   that any non-zero entries already present in the array are valid.
   This feature is perhaps convenient coding when one might not have
   used all blocknrs from the last time one called this function, or
   perhaps it is an archaism from the days of schedule tracking, one
   of us ought to reread the code that calls this, and analyze whether
   it is still the right way to code it.

   spare space is used only when priority is set to 1. reiserfsck has
   its own reiserfs_new_blocknrs, which can use reserved space

   exactly what reserved space?  the SPARE_SPACE?  if so, please comment reiserfs.h.

   Give example of who uses spare space, and say that it is a deadlock
   avoidance mechanism.  -Hans */

/* This function is NOT SCHEDULE-SAFE! */

static int do_reiserfs_new_blocknrs (struct reiserfs_transaction_handle *th,
                                     unsigned long * free_blocknrs, 
				     unsigned long search_start, 
				     int amount_needed, int priority, 
				     int for_unformatted,
				     int for_prealloc)
{
  struct super_block * s = th->t_super;
  int i, j;
  unsigned long * block_list_start = free_blocknrs;
  int init_amount_needed = amount_needed;
  unsigned long new_block = 0 ; 

    if (SB_FREE_BLOCKS (s) < SPARE_SPACE && !priority)
	/* we can answer NO_DISK_SPACE being asked for new block with
	   priority 0 */
	return NO_DISK_SPACE;

  RFALSE( !s, "vs-4090: trying to get new block from nonexistent device");
  RFALSE( search_start == MAX_B_NUM,
	  "vs-4100: we are optimizing location based on "
	  "the bogus location of a temp buffer (%lu).", search_start);
  RFALSE( amount_needed < 1 || amount_needed > 2,
	  "vs-4110: amount_needed parameter incorrect (%d)", amount_needed);

  /* We continue the while loop if another process snatches our found
   * free block from us after we find it but before we successfully
   * mark it as in use, or if we need to use sync to free up some
   * blocks on the preserve list.  */

  while (amount_needed--) {
    /* skip over any blocknrs already gotten last time. */
    if (*(free_blocknrs) != 0) {
      RFALSE( is_reusable (s, *free_blocknrs, 1) == 0, 
	      "vs-4120: bad blocknr on free_blocknrs list");
      free_blocknrs++;
      continue;
    }
    /* look for zero bits in bitmap */
    if (find_zero_bit_in_bitmap(s,search_start, &i, &j,for_unformatted) == 0) {
      if (find_zero_bit_in_bitmap(s,search_start,&i,&j, for_unformatted) == 0) {
				/* recode without the goto and without
				   the if.  It will require a
				   duplicate for.  This is worth the
				   code clarity.  Your way was
				   admirable, and just a bit too
				   clever in saving instructions.:-)
				   I'd say create a new function, but
				   that would slow things also, yes?
				   -Hans */
free_and_return:
	for ( ; block_list_start != free_blocknrs; block_list_start++) {
	  reiserfs_free_block (th, *block_list_start);
	  *block_list_start = 0;
	}
	if (for_prealloc) 
	    return NO_MORE_UNUSED_CONTIGUOUS_BLOCKS;
	else
	    return NO_DISK_SPACE;
      }
    }
    
    /* i and j now contain the results of the search. i = bitmap block
       number containing free block, j = offset in this block.  we
       compute the blocknr which is our result, store it in
       free_blocknrs, and increment the pointer so that on the next
       loop we will insert into the next location in the array.  Also
       in preparation for the next loop, search_start is changed so
       that the next search will not rescan the same range but will
       start where this search finished.  Note that while it is
       possible that schedule has occurred and blocks have been freed
       in that range, it is perhaps more important that the blocks
       returned be near each other than that they be near their other
       neighbors, and it also simplifies and speeds the code this way.  */

    /* journal: we need to make sure the block we are giving out is not
    ** a log block, horrible things would happen there.
    */
    new_block = (i * (s->s_blocksize << 3)) + j; 
    if (for_prealloc && (new_block - 1) != search_start) {
      /* preallocated blocks must be contiguous, bail if we didnt find one.
      ** this is not a bug.  We want to do the check here, before the
      ** bitmap block is prepared, and before we set the bit and log the
      ** bitmap. 
      **
      ** If we do the check after this function returns, we have to 
      ** call reiserfs_free_block for new_block, which would be pure
      ** overhead.
      **
      ** for_prealloc should only be set if the caller can deal with the
      ** NO_MORE_UNUSED_CONTIGUOUS_BLOCKS return value.  This can be
      ** returned before the disk is actually full
      */
      goto free_and_return ;
    }
    search_start = new_block ;
    if (search_start >= reiserfs_get_journal_block(s) &&
        search_start < (reiserfs_get_journal_block(s) + JOURNAL_BLOCK_COUNT)) {
	reiserfs_warning("vs-4130: reiserfs_new_blocknrs: trying to allocate log block %lu\n",
			 search_start) ;
	search_start++ ;
	amount_needed++ ;
	continue ;
    }
       

    reiserfs_prepare_for_journal(s, SB_AP_BITMAP(s)[i], 1) ;

    RFALSE( buffer_locked (SB_AP_BITMAP (s)[i]) || 
	    is_reusable (s, search_start, 0) == 0,
	    "vs-4140: bitmap block is locked or bad block number found");

    /* if this bit was already set, we've scheduled, and someone else
    ** has allocated it.  loop around and try again
    */
    if (reiserfs_test_and_set_le_bit (j, SB_AP_BITMAP (s)[i]->b_data)) {
	reiserfs_warning("vs-4150: reiserfs_new_blocknrs, block not free");
	reiserfs_restore_prepared_buffer(s, SB_AP_BITMAP(s)[i]) ;
	amount_needed++ ;
	continue ;
    }    
    journal_mark_dirty (th, s, SB_AP_BITMAP (s)[i]); 
    *free_blocknrs = search_start ;
    free_blocknrs ++;
  }

  reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;
  /* update free block count in super block */
  s->u.reiserfs_sb.s_rs->s_free_blocks = cpu_to_le32 (SB_FREE_BLOCKS (s) - init_amount_needed);
  journal_mark_dirty (th, s, SB_BUFFER_WITH_SB (s));
  s->s_dirt = 1;

  return CARRY_ON;
}

// this is called only by get_empty_nodes with for_preserve_list==0
int reiserfs_new_blocknrs (struct reiserfs_transaction_handle *th, unsigned long * free_blocknrs,
			    unsigned long search_start, int amount_needed) {
  return do_reiserfs_new_blocknrs(th, free_blocknrs, search_start, amount_needed, 0/*for_preserve_list-priority*/, 0/*for_formatted*/, 0/*for_prealloc */) ;
}


// called by get_new_buffer and by reiserfs_get_block with amount_needed == 1 and for_preserve_list == 0
int reiserfs_new_unf_blocknrs(struct reiserfs_transaction_handle *th, unsigned long * free_blocknrs,
			      unsigned long search_start) {
  return do_reiserfs_new_blocknrs(th, free_blocknrs, search_start, 
                                  1/*amount_needed*/,
				  0/*for_preserve_list-priority*/, 
				  1/*for formatted*/,
				  0/*for prealloc */) ;
}

#ifdef REISERFS_PREALLOCATE

/* 
** We pre-allocate 8 blocks.  Pre-allocation is used for files > 16 KB only.
** This lowers fragmentation on large files by grabbing a contiguous set of
** blocks at once.  It also limits the number of times the bitmap block is
** logged by making X number of allocation changes in a single transaction.
**
** We are using a border to divide the disk into two parts.  The first part
** is used for tree blocks, which have a very high turnover rate (they
** are constantly allocated then freed)
**
** The second part of the disk is for the unformatted nodes of larger files.
** Putting them away from the tree blocks lowers fragmentation, and makes
** it easier to group files together.  There are a number of different
** allocation schemes being tried right now, each is documented below.
**
** A great deal of the allocator's speed comes because reiserfs_get_block
** sends us the block number of the last unformatted node in the file.  Once
** a given block is allocated past the border, we don't collide with the
** blocks near the search_start again.
** 
*/
int reiserfs_new_unf_blocknrs2 (struct reiserfs_transaction_handle *th, 
				struct inode       * p_s_inode,
				unsigned long      * free_blocknrs,
				unsigned long        search_start)
{
  int ret=0, blks_gotten=0;
  unsigned long border = 0;
  unsigned long bstart = 0;
  unsigned long hash_in, hash_out;
  unsigned long saved_search_start=search_start;
  int allocated[PREALLOCATION_SIZE];
  int blks;

  if (!reiserfs_no_border(th->t_super)) {
    /* we default to having the border at the 10% mark of the disk.  This
    ** is an arbitrary decision and it needs tuning.  It also needs a limit
    ** to prevent it from taking too much space on huge drives.
    */
    bstart = (SB_BLOCK_COUNT(th->t_super) / 10); 
  }
  if (!reiserfs_no_unhashed_relocation(th->t_super)) {
    /* this is a very simple first attempt at preventing too much grouping
    ** around the border value.  Since k_dir_id is never larger than the
    ** highest allocated oid, it is far from perfect, and files will tend
    ** to be grouped towards the start of the border
    */
    border = (INODE_PKEY(p_s_inode)->k_dir_id) % (SB_BLOCK_COUNT(th->t_super) - bstart - 1) ;
  } else {
    /* why would we want to delcare a local variable to this if statement
    ** name border????? -chris
    ** unsigned long border = 0;
    */
    if (!reiserfs_hashed_relocation(th->t_super)) {
      hash_in = (INODE_PKEY(p_s_inode))->k_dir_id;
				/* I wonder if the CPU cost of the
                                   hash will obscure the layout
                                   effect? Of course, whether that
                                   effect is good or bad we don't
                                   know.... :-) */
      
      hash_out = keyed_hash(((char *) (&hash_in)), 4);
      border = hash_out % (SB_BLOCK_COUNT(th->t_super) - bstart - 1) ;
    }
  }
  border += bstart ;
  allocated[0] = 0 ; /* important.  Allows a check later on to see if at
                      * least one block was allocated.  This prevents false
		      * no disk space returns
		      */

  if ( (p_s_inode->i_size < 4 * 4096) || 
       !(S_ISREG(p_s_inode->i_mode)) )
    {
      if ( search_start < border 
	   || (
				/* allow us to test whether it is a
                                   good idea to prevent files from
                                   getting too far away from their
                                   packing locality by some unexpected
                                   means.  This might be poor code for
                                   directories whose files total
                                   larger than 1/10th of the disk, and
                                   it might be good code for
                                   suffering from old insertions when the disk
                                   was almost full. */
               /* changed from !reiserfs_test3(th->t_super), which doesn't
               ** seem like a good idea.  Think about adding blocks to
               ** a large file.  If you've allocated 10% of the disk
               ** in contiguous blocks, you start over at the border value
               ** for every new allocation.  This throws away all the
               ** information sent in about the last block that was allocated
               ** in the file.  Not a good general case at all.
               ** -chris
               */
	       reiserfs_test4(th->t_super) && 
	       (search_start > border + (SB_BLOCK_COUNT(th->t_super) / 10))
	       )
	   )
	search_start=border;
  
      ret = do_reiserfs_new_blocknrs(th, free_blocknrs, search_start, 
				     1/*amount_needed*/, 
				     0/*use reserved blocks for root */,
				     1/*for_formatted*/,
				     0/*for prealloc */) ;  
      return ret;
    }

  /* take a block off the prealloc list and return it -Hans */
  if (p_s_inode->u.reiserfs_i.i_prealloc_count > 0) {
    p_s_inode->u.reiserfs_i.i_prealloc_count--;
    *free_blocknrs = p_s_inode->u.reiserfs_i.i_prealloc_block++;

    /* if no more preallocated blocks, remove inode from list */
    if (! p_s_inode->u.reiserfs_i.i_prealloc_count) {
      list_del(&p_s_inode->u.reiserfs_i.i_prealloc_list);
    }
    
    return ret;
  }

				/* else get a new preallocation for the file */
  reiserfs_discard_prealloc (th, p_s_inode);
  /* this uses the last preallocated block as the search_start.  discard
  ** prealloc does not zero out this number.
  */
  if (search_start <= p_s_inode->u.reiserfs_i.i_prealloc_block) {
    search_start = p_s_inode->u.reiserfs_i.i_prealloc_block;
  }
  
  /* doing the compare again forces search_start to be >= the border,
  ** even if the file already had prealloction done.  This seems extra,
  ** and should probably be removed
  */
  if ( search_start < border ) search_start=border; 

  /* If the disk free space is already below 10% we should 
  ** start looking for the free blocks from the beginning 
  ** of the partition, before the border line.
  */
  if ( SB_FREE_BLOCKS(th->t_super) <= (SB_BLOCK_COUNT(th->t_super) / 10) ) {
    search_start=saved_search_start;
  }

  *free_blocknrs = 0;
  blks = PREALLOCATION_SIZE-1;
  for (blks_gotten=0; blks_gotten<PREALLOCATION_SIZE; blks_gotten++) {
    ret = do_reiserfs_new_blocknrs(th, free_blocknrs, search_start, 
				   1/*amount_needed*/, 
				   0/*for root reserved*/,
				   1/*for_formatted*/,
				   (blks_gotten > 0)/*must_be_contiguous*/) ;
    /* if we didn't find a block this time, adjust blks to reflect
    ** the actual number of blocks allocated
    */ 
    if (ret != CARRY_ON) {
      blks = blks_gotten > 0 ? (blks_gotten - 1) : 0 ;
      break ;
    }
    allocated[blks_gotten]= *free_blocknrs;
#ifdef CONFIG_REISERFS_CHECK
    if ( (blks_gotten>0) && (allocated[blks_gotten] - allocated[blks_gotten-1]) != 1 ) {
      /* this should be caught by new_blocknrs now, checking code */
      reiserfs_warning("yura-1, reiserfs_new_unf_blocknrs2: pre-allocated not contiguous set of blocks!\n") ;
      reiserfs_free_block(th, allocated[blks_gotten]);
      blks = blks_gotten-1; 
      break;
    }
#endif
    if (blks_gotten==0) {
      p_s_inode->u.reiserfs_i.i_prealloc_block = *free_blocknrs;
    }
    search_start = *free_blocknrs; 
    *free_blocknrs = 0;
  }
  p_s_inode->u.reiserfs_i.i_prealloc_count = blks;
  *free_blocknrs = p_s_inode->u.reiserfs_i.i_prealloc_block;
  p_s_inode->u.reiserfs_i.i_prealloc_block++;

  /* if inode has preallocated blocks, link him to list */
  if (p_s_inode->u.reiserfs_i.i_prealloc_count) {
    list_add(&p_s_inode->u.reiserfs_i.i_prealloc_list,
	     &SB_JOURNAL(th->t_super)->j_prealloc_list);
  } 
  /* we did actually manage to get 1 block */
  if (ret != CARRY_ON && allocated[0] > 0) {
    return CARRY_ON ;
  }
  /* NO_MORE_UNUSED_CONTIGUOUS_BLOCKS should only mean something to
  ** the preallocation code.  The rest of the filesystem asks for a block
  ** and should either get it, or know the disk is full.  The code
  ** above should never allow ret == NO_MORE_UNUSED_CONTIGUOUS_BLOCK,
  ** as it doesn't send for_prealloc = 1 to do_reiserfs_new_blocknrs
  ** unless it has already successfully allocated at least one block.
  ** Just in case, we translate into a return value the rest of the
  ** filesystem can understand.
  **
  ** It is an error to change this without making the
  ** rest of the filesystem understand NO_MORE_UNUSED_CONTIGUOUS_BLOCKS
  ** If you consider it a bug to return NO_DISK_SPACE here, fix the rest
  ** of the fs first.
  */
  if (ret == NO_MORE_UNUSED_CONTIGUOUS_BLOCKS) {
#ifdef CONFIG_REISERFS_CHECK
    reiserfs_warning("reiser-2015: this shouldn't happen, may cause false out of disk space error");
#endif
     return NO_DISK_SPACE; 
  }
  return ret;
}

//
// a portion of this function, was derived from minix or ext2's
// analog. You should be able to tell which portion by looking at the
// ext2 code and comparing. 
static void __discard_prealloc (struct reiserfs_transaction_handle * th,
				struct inode * inode)
{
  while (inode->u.reiserfs_i.i_prealloc_count > 0) {
    reiserfs_free_block(th,inode->u.reiserfs_i.i_prealloc_block);
    inode->u.reiserfs_i.i_prealloc_block++;
    inode->u.reiserfs_i.i_prealloc_count --;
  }
  list_del (&(inode->u.reiserfs_i.i_prealloc_list));
}


void reiserfs_discard_prealloc (struct reiserfs_transaction_handle *th, 
				struct inode * inode)
{
#ifdef CONFIG_REISERFS_CHECK
  if (inode->u.reiserfs_i.i_prealloc_count < 0)
     reiserfs_warning("zam-4001:" __FUNCTION__ ": inode has negative prealloc blocks count.\n");
#endif  
  if (inode->u.reiserfs_i.i_prealloc_count > 0) {
    __discard_prealloc(th, inode);
  }
}

void reiserfs_discard_all_prealloc (struct reiserfs_transaction_handle *th)
{
  struct list_head * plist = &SB_JOURNAL(th->t_super)->j_prealloc_list;
  struct inode * inode;
  
  while (!list_empty(plist)) {
    inode = list_entry(plist->next, struct inode, u.reiserfs_i.i_prealloc_list);
#ifdef CONFIG_REISERFS_CHECK
    if (!inode->u.reiserfs_i.i_prealloc_count) {
      reiserfs_warning("zam-4001:" __FUNCTION__ ": inode is in prealloc list but has no preallocated blocks.\n");
    }
#endif    
    __discard_prealloc(th, inode);
  }
}
#endif
