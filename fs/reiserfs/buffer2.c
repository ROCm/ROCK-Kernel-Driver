/*
 *  Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README  
 */


/*
 * Contains code from
 *
 *  linux/include/linux/lock.h and linux/fs/buffer.c /linux/fs/minix/fsync.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/reiserfs_fs.h>
#include <linux/smp_lock.h>

/*
 *  wait_buffer_until_released
 *  reiserfs_bread
 *  reiserfs_getblk
 *  get_new_buffer
 */



/* when we allocate a new block (get_new_buffer, get_empty_nodes) and
   get buffer for it, it is possible that it is held by someone else
   or even by this process. In this function we wait until all other
   holders release buffer. To make sure, that current process does not
   hold we did free all buffers in tree balance structure
   (get_empty_nodes and get_nodes_for_preserving) or in path structure
   only (get_new_buffer) just before calling this */
void wait_buffer_until_released (struct buffer_head * bh)
{
  int repeat_counter = 0;

  while (atomic_read (&(bh->b_count)) > 1) {

    if ( !(++repeat_counter % 30000000) ) {
      reiserfs_warning ("vs-3050: wait_buffer_until_released: nobody releases buffer (%b). Still waiting (%d) %cJDIRTY %cJWAIT\n",
			bh, repeat_counter, buffer_journaled(bh) ? ' ' : '!',
			buffer_journal_dirty(bh) ? ' ' : '!');
    }
    run_task_queue(&tq_disk);
    current->policy |= SCHED_YIELD;
    /*current->counter = 0;*/
    schedule();
  }
  if (repeat_counter > 30000000) {
    reiserfs_warning("vs-3051: done waiting, ignore vs-3050 messages for (%b)\n", bh) ;
  }
}

/*
 * reiserfs_bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
/* It first tries to find the block in cache, and if it cannot do so
   then it creates a new buffer and schedules I/O to read the
   block. */
/* The function is NOT SCHEDULE-SAFE! */

struct buffer_head  * reiserfs_bread (kdev_t n_dev, int n_block, int n_size) 
{
    return bread (n_dev, n_block, n_size);
}

/* This function looks for a buffer which contains a given block.  If
   the block is in cache it returns it, otherwise it returns a new
   buffer which is not uptodate.  This is called by reiserfs_bread and
   other functions. Note that get_new_buffer ought to be called this
   and this ought to be called get_new_buffer, since this doesn't
   actually get the block off of the disk. */
/* The function is NOT SCHEDULE-SAFE! */

struct buffer_head  * reiserfs_getblk (kdev_t n_dev, int n_block, int n_size)
{
    return getblk (n_dev, n_block, n_size);
}

#ifdef NEW_GET_NEW_BUFFER

/* returns one buffer with a blocknr near blocknr. */
static int get_new_buffer_near_blocknr(
                   struct super_block *  p_s_sb,
                   int blocknr,
                   struct buffer_head ** pp_s_new_bh,
                   struct path         * p_s_path 
                   ) {
  unsigned      long n_new_blocknumber = 0;
  int           n_ret_value,
                n_repeat = CARRY_ON;

#ifdef CONFIG_REISERFS_CHECK
  int repeat_counter = 0;
  
  if (!blocknr)
    printk ("blocknr passed to get_new_buffer_near_blocknr was 0");
#endif


  if ( (n_ret_value = reiserfs_new_blocknrs (p_s_sb, &n_new_blocknumber,
                                             blocknr, 1)) == NO_DISK_SPACE )
    return NO_DISK_SPACE;
  
  *pp_s_new_bh = reiserfs_getblk(p_s_sb->s_dev, n_new_blocknumber, p_s_sb->s_blocksize);
  if ( buffer_uptodate(*pp_s_new_bh) ) {

    RFALSE( buffer_dirty(*pp_s_new_bh) || (*pp_s_new_bh)->b_dev == NODEV,
	    "PAP-14080: invalid uptodate buffer %b for the new block", 
	    *pp_s_new_bh);

    /* Free path buffers to prevent deadlock. */
    /* It is possible that this process has the buffer, which this function is getting, already in
       its path, and is responsible for double incrementing the value of b_count.  If we recalculate
       the path after schedule we can avoid risking an endless loop.  This problematic situation is
       possible in a multiple processing environment.  Suppose process 1 has acquired a path P; then
       process 2 balanced and remove block A from the tree.  Process 1 continues and runs
       get_new_buffer, that returns buffer with block A. If node A was on the path P, then it will
       have b_count == 2. If we now will simply wait in while ( (*pp_s_new_bh)->b_count > 1 ) we get
       into an endless loop, as nobody will release this buffer and the current process holds buffer
       twice. That is why we do decrement_counters_in_path(p_s_path) before waiting until b_count
       becomes 1. (it there were other processes holding node A, then eventually we will get a
       moment, when all of them released a buffer). */
    if ( atomic_read (&((*pp_s_new_bh)->b_count)) > 1  ) {
      decrement_counters_in_path(p_s_path);
      n_ret_value |= SCHEDULE_OCCURRED;
    }

    while ( atomic_read (&((*pp_s_new_bh)->b_count)) > 1 ) {

#ifdef REISERFS_INFO
      printk("get_new_buffer() calls schedule to decrement b_count\n");
#endif

#ifdef CONFIG_REISERFS_CHECK
      if ( ! (++repeat_counter % 10000) )
	printk("get_new_buffer(%u): counter(%d) too big", current->pid, repeat_counter);
#endif

      current->counter = 0;
      schedule();
    }

#ifdef CONFIG_REISERFS_CHECK
    if ( buffer_dirty(*pp_s_new_bh) || (*pp_s_new_bh)->b_dev == NODEV ) {
      print_buffer_head(*pp_s_new_bh,"get_new_buffer");
      reiserfs_panic(p_s_sb, "PAP-14090: get_new_buffer: invalid uptodate buffer %b for the new block(case 2)", *pp_s_new_bh);
    }
#endif

  }
  else {
    ;

    RFALSE( atomic_read (&((*pp_s_new_bh)->b_count)) != 1,
	    "PAP-14100: not uptodate buffer %b for the new block has b_count more than one",
	    *pp_s_new_bh);

  }
  return (n_ret_value | n_repeat);
}


/* returns the block number of the last unformatted node, assumes p_s_key_to_search.k_offset is a byte in the tail of
   the file, Useful for when you want to append to a file, and convert a direct item into an unformatted node near the
   last unformatted node of the file.  Putting the unformatted node near the direct item is potentially very bad to do.
   If there is no unformatted node in the file, then we return the block number of the direct item.  */
/* The function is NOT SCHEDULE-SAFE! */
inline int get_last_unformatted_node_blocknr_of_file(  struct key * p_s_key_to_search, struct super_block * p_s_sb,
                                                       struct buffer_head * p_s_bh
                                                       struct path * p_unf_search_path, struct inode * p_s_inode)

{
  struct key unf_key_to_search;
  struct item_head * p_s_ih;
  int n_pos_in_item;
  struct buffer_head * p_indirect_item_bh;

      copy_key(&unf_key_to_search,p_s_key_to_search);
      unf_key_to_search.k_uniqueness = TYPE_INDIRECT;
      unf_key_to_search.k_offset = p_s_inode->u.reiserfs_i.i_first_direct_byte - 1;

        /* p_s_key_to_search->k_offset -  MAX_ITEM_LEN(p_s_sb->s_blocksize); */
      if (search_for_position_by_key (p_s_sb, &unf_key_to_search, p_unf_search_path, &n_pos_in_item) == POSITION_FOUND)
        {
          p_s_ih = B_N_PITEM_HEAD(p_indirect_item_bh = PATH_PLAST_BUFFER(p_unf_search_path), PATH_LAST_POSITION(p_unf_search_path));
          return (B_I_POS_UNFM_POINTER(p_indirect_item_bh, p_s_ih, n_pos_in_item));
        }
     /*  else */
      printk("reiser-1800: search for unformatted node failed, p_s_key_to_search->k_offset = %u,  unf_key_to_search.k_offset = %u, MAX_ITEM_LEN(p_s_sb->s_blocksize) = %ld, debug this\n", p_s_key_to_search->k_offset, unf_key_to_search.k_offset,  MAX_ITEM_LEN(p_s_sb->s_blocksize) );
      print_buffer_head(PATH_PLAST_BUFFER(p_unf_search_path), "the buffer holding the item before the key we failed to find");
      print_block_head(PATH_PLAST_BUFFER(p_unf_search_path), "the block head");
      return 0;                         /* keeps the compiler quiet */
}


                                /* hasn't been out of disk space tested  */
/* The function is NOT SCHEDULE-SAFE! */
static int get_buffer_near_last_unf ( struct super_block * p_s_sb, struct key * p_s_key_to_search,
                                                 struct inode *  p_s_inode,  struct buffer_head * p_s_bh, 
                                                 struct buffer_head ** pp_s_un_bh, struct path * p_s_search_path)
{
  int unf_blocknr = 0, /* blocknr from which we start search for a free block for an unformatted node, if 0
                          then we didn't find an unformatted node though we might have found a file hole */
      n_repeat = CARRY_ON;
  struct key unf_key_to_search;
  struct path unf_search_path;

  copy_key(&unf_key_to_search,p_s_key_to_search);
  unf_key_to_search.k_uniqueness = TYPE_INDIRECT;
  
  if (
      (p_s_inode->u.reiserfs_i.i_first_direct_byte > 4095) /* i_first_direct_byte gets used for all sorts of
                                                              crap other than what the name indicates, thus
                                                              testing to see if it is 0 is not enough */
      && (p_s_inode->u.reiserfs_i.i_first_direct_byte < MAX_KEY_OFFSET) /* if there is no direct item then
                                                                           i_first_direct_byte = MAX_KEY_OFFSET */
      )
    {
                                /* actually, we don't want the last unformatted node, we want the last unformatted node
                                   which is before the current file offset */
      unf_key_to_search.k_offset = ((p_s_inode->u.reiserfs_i.i_first_direct_byte -1) < unf_key_to_search.k_offset) ? p_s_inode->u.reiserfs_i.i_first_direct_byte -1 :  unf_key_to_search.k_offset;

      while (unf_key_to_search.k_offset > -1)
        {
                                /* This is our poorly documented way of initializing paths. -Hans */
          init_path (&unf_search_path);
                                /* get the blocknr from which we start the search for a free block. */
          unf_blocknr = get_last_unformatted_node_blocknr_of_file(  p_s_key_to_search, /* assumes this points to the file tail */
                                                                    p_s_sb,     /* lets us figure out the block size */
                                                                    p_s_bh, /* if there is no unformatted node in the file,
                                                                               then it returns p_s_bh->b_blocknr */
                                                                    &unf_search_path,
                                                                    p_s_inode
                                                                    );
/*        printk("in while loop: unf_blocknr = %d,  *pp_s_un_bh = %p\n", unf_blocknr, *pp_s_un_bh); */
          if (unf_blocknr) 
            break;
          else                  /* release the path and search again, this could be really slow for huge
                                   holes.....better to spend the coding time adding compression though.... -Hans */
            {
                                /* Vladimir, is it a problem that I don't brelse these buffers ?-Hans */
              decrement_counters_in_path(&unf_search_path);
              unf_key_to_search.k_offset -= 4096;
            }
        }
      if (unf_blocknr) {
        n_repeat |= get_new_buffer_near_blocknr(p_s_sb, unf_blocknr, pp_s_un_bh, p_s_search_path);
      }
      else {                    /* all unformatted nodes are holes */
        n_repeat |= get_new_buffer_near_blocknr(p_s_sb, p_s_bh->b_blocknr, pp_s_un_bh, p_s_search_path); 
      }
    }
  else {                        /* file has no unformatted nodes */
    n_repeat |= get_new_buffer_near_blocknr(p_s_sb, p_s_bh->b_blocknr, pp_s_un_bh, p_s_search_path);
/*     printk("in else: unf_blocknr = %d,  *pp_s_un_bh = %p\n", unf_blocknr, *pp_s_un_bh); */
/*     print_path (0,  p_s_search_path); */
  }

  return n_repeat;
}

#endif /* NEW_GET_NEW_BUFFER */


#ifdef OLD_GET_NEW_BUFFER

/* The function is NOT SCHEDULE-SAFE! */
int get_new_buffer(
		   struct reiserfs_transaction_handle *th, 
		   struct buffer_head *  p_s_bh,
		   struct buffer_head ** pp_s_new_bh,
		   struct path	       * p_s_path
		   ) {
  unsigned	long n_new_blocknumber = 0;
  int		n_repeat;
  struct super_block *	 p_s_sb = th->t_super;

  if ( (n_repeat = reiserfs_new_unf_blocknrs (th, &n_new_blocknumber, p_s_bh->b_blocknr)) == NO_DISK_SPACE )
    return NO_DISK_SPACE;
  
  *pp_s_new_bh = reiserfs_getblk(p_s_sb->s_dev, n_new_blocknumber, p_s_sb->s_blocksize);
  if (atomic_read (&(*pp_s_new_bh)->b_count) > 1) {
    /* Free path buffers to prevent deadlock which can occur in the
       situation like : this process holds p_s_path; Block
       (*pp_s_new_bh)->b_blocknr is on the path p_s_path, but it is
       not necessary, that *pp_s_new_bh is in the tree; process 2
       could remove it from the tree and freed block
       (*pp_s_new_bh)->b_blocknr. Reiserfs_new_blocknrs in above
       returns block (*pp_s_new_bh)->b_blocknr. Reiserfs_getblk gets
       buffer for it, and it has b_count > 1. If we now will simply
       wait in while ( (*pp_s_new_bh)->b_count > 1 ) we get into an
       endless loop, as nobody will release this buffer and the
       current process holds buffer twice. That is why we do
       decrement_counters_in_path(p_s_path) before waiting until
       b_count becomes 1. (it there were other processes holding node
       pp_s_new_bh, then eventually we will get a moment, when all of
       them released a buffer). */
    decrement_counters_in_path(p_s_path);
    wait_buffer_until_released (*pp_s_new_bh);
    n_repeat |= SCHEDULE_OCCURRED;
  }

  RFALSE( atomic_read (&((*pp_s_new_bh)->b_count)) != 1 || 
	  buffer_dirty (*pp_s_new_bh),
	  "PAP-14100: not free or dirty buffer %b for the new block", 
	  *pp_s_new_bh);

  return n_repeat;
}

#endif /* OLD_GET_NEW_BUFFER */


#ifdef GET_MANY_BLOCKNRS
                                /* code not yet functional */
get_next_blocknr (
                  unsigned long *       p_blocknr_array,          /* we get a whole bunch of blocknrs all at once for
                                                                     the write.  This is better than getting them one at
                                                                     a time.  */
                  unsigned long **      p_blocknr_index,        /* pointer to current offset into the array. */
                  unsigned long        blocknr_array_length
)
{
  unsigned long return_value;

  if (*p_blocknr_index < p_blocknr_array + blocknr_array_length) {
    return_value = **p_blocknr_index;
    **p_blocknr_index = 0;
    *p_blocknr_index++;
    return (return_value);
  }
  else
    {
      kfree (p_blocknr_array);
    }
}
#endif /* GET_MANY_BLOCKNRS */

