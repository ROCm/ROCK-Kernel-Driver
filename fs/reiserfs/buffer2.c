/*
 *  Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README  
 */

#include <linux/config.h>
#include <linux/time.h>
#include <linux/reiserfs_fs.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/buffer_head.h>

/*
 *  wait_buffer_until_released
 *  reiserfs_bread
 */

/* when we allocate a new block (get_new_buffer, get_empty_nodes) and
   get buffer for it, it is possible that it is held by someone else
   or even by this process. In this function we wait until all other
   holders release buffer. To make sure, that current process does not
   hold we did free all buffers in tree balance structure
   (get_empty_nodes and get_nodes_for_preserving) or in path structure
   only (get_new_buffer) just before calling this */
void wait_buffer_until_released (const struct buffer_head * bh)
{
  int repeat_counter = 0;

  /*
   * FIXME! Temporary cludge until ReiserFS people tell what they
   * actually are trying to protect against!
   */
  if (1)
  	return;

  while (atomic_read (&(bh->b_count)) > 1) {

    if ( !(++repeat_counter % 30000000) ) {
      reiserfs_warning ("vs-3050: wait_buffer_until_released: nobody releases buffer (%b). Still waiting (%d) %cJDIRTY %cJWAIT\n",
			bh, repeat_counter, buffer_journaled(bh) ? ' ' : '!',
			buffer_journal_dirty(bh) ? ' ' : '!');
    }
    blk_run_queues();
    yield();
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
struct buffer_head  * reiserfs_bread (struct super_block *super, int n_block) 
{
    struct buffer_head  *result;
    PROC_EXP( unsigned int ctx_switches = nr_context_switches() );

    result = sb_bread(super, n_block);
    PROC_INFO_INC( super, breads );
    PROC_EXP( if( nr_context_switches() != ctx_switches )
	      PROC_INFO_INC( super, bread_miss ) );
    return result;
}

