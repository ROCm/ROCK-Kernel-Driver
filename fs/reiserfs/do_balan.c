/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

/* Now we have all buffers that must be used in balancing of the tree 	*/
/* Further calculations can not cause schedule(), and thus the buffer 	*/
/* tree will be stable until the balancing will be finished 		*/
/* balance the tree according to the analysis made before,		*/
/* and using buffers obtained after all above.				*/


/**
 ** balance_leaf_when_delete
 ** balance_leaf
 ** do_balance
 **
 **/

#ifdef __KERNEL__

#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/reiserfs_fs.h>

#else

#include "nokernel.h"

#endif


#ifdef CONFIG_REISERFS_CHECK

struct tree_balance * cur_tb = NULL; /* detects whether more than one
                                        copy of tb exists as a means
                                        of checking whether schedule
                                        is interrupting do_balance */
#endif


inline void do_balance_mark_leaf_dirty (struct tree_balance * tb, 
					struct buffer_head * bh, int flag)
{
    if (reiserfs_dont_log(tb->tb_sb)) {
	if (!test_and_set_bit(BH_Dirty, &bh->b_state)) {
	    __mark_buffer_dirty(bh) ;
	    tb->need_balance_dirty = 1;
	}
    } else {
	int windex = push_journal_writer("do_balance") ;
	journal_mark_dirty(tb->transaction_handle, tb->transaction_handle->t_super, bh) ;
	pop_journal_writer(windex) ;
    }
}

#define do_balance_mark_internal_dirty do_balance_mark_leaf_dirty
#define do_balance_mark_sb_dirty do_balance_mark_leaf_dirty


/* summary: 
 if deleting something ( tb->insert_size[0] < 0 )
   return(balance_leaf_when_delete()); (flag d handled here)
 else
   if lnum is larger than 0 we put items into the left node
   if rnum is larger than 0 we put items into the right node
   if snum1 is larger than 0 we put items into the new node s1
   if snum2 is larger than 0 we put items into the new node s2 
Note that all *num* count new items being created.

It would be easier to read balance_leaf() if each of these summary
lines was a separate procedure rather than being inlined.  I think
that there are many passages here and in balance_leaf_when_delete() in
which two calls to one procedure can replace two passages, and it
might save cache space and improve software maintenance costs to do so.  

Vladimir made the perceptive comment that we should offload most of
the decision making in this function into fix_nodes/check_balance, and
then create some sort of structure in tb that says what actions should
be performed by do_balance.

-Hans */



/* Balance leaf node in case of delete or cut: insert_size[0] < 0
 *
 * lnum, rnum can have values >= -1
 *	-1 means that the neighbor must be joined with S
 *	 0 means that nothing should be done with the neighbor
 *	>0 means to shift entirely or partly the specified number of items to the neighbor
 */
static int balance_leaf_when_delete (struct tree_balance * tb, int flag)
{
    struct buffer_head * tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
    int item_pos = PATH_LAST_POSITION (tb->tb_path);
    int pos_in_item = tb->tb_path->pos_in_item;
    struct buffer_info bi;
    int n;
    struct item_head * ih;

#ifdef CONFIG_REISERFS_CHECK
    if ( tb->FR[0] && B_LEVEL (tb->FR[0]) != DISK_LEAF_NODE_LEVEL + 1)
	reiserfs_panic (tb->tb_sb,
			"vs- 12000: balance_leaf_when_delete:level: wrong FR %z\n", tb->FR[0]);
    if ( tb->blknum[0] > 1 )
	reiserfs_panic (tb->tb_sb,
			"PAP-12005: balance_leaf_when_delete: "
			"tb->blknum == %d, can not be > 1", tb->blknum[0]);
	
    if ( ! tb->blknum[0] && ! PATH_H_PPARENT(tb->tb_path, 0))
	reiserfs_panic (tb->tb_sb, "PAP-12010: balance_leaf_when_delete: tree can not be empty");
#endif

    ih = B_N_PITEM_HEAD (tbS0, item_pos);

    /* Delete or truncate the item */

    switch (flag) {
    case M_DELETE:   /* delete item in S[0] */

#ifdef CONFIG_REISERFS_CHECK
	if (le16_to_cpu (ih->ih_item_len) + IH_SIZE != -tb->insert_size [0])
	    reiserfs_panic (tb->tb_sb, "vs-12013: balance_leaf_when_delete: "
			    "mode Delete, insert size %d, ih to be deleted %h", ih);

#if 0 /* rigth delim key not supported */
	if ( ! item_pos && (! tb->L[0] || COMP_KEYS(B_PRIGHT_DELIM_KEY(tb->L[0]), B_N_PKEY(tbS0, 0))) ) {
	    print_cur_tb ("12015");
	    reiserfs_panic (tb->tb_sb, "PAP-12015: balance_leaf_when_delete: L0's rkey does not match to 1st key of S0: "
			    "rkey in L %k, first key in S0 %k, rkey in CFL %k",
			    tb->L[0] ? B_PRIGHT_DELIM_KEY(tb->L[0]) : 0, 
			    B_N_PKEY(tbS0, 0),
			    tb->CFL[0] ? B_N_PDELIM_KEY(tb->CFL[0],tb->lkey[0]) : 0);
	}
#endif

#endif

	bi.tb = tb;
	bi.bi_bh = tbS0;
	bi.bi_parent = PATH_H_PPARENT (tb->tb_path, 0);
	bi.bi_position = PATH_H_POSITION (tb->tb_path, 1);
	leaf_delete_items (&bi, 0, item_pos, 1, -1);

	if ( ! item_pos && tb->CFL[0] ) {
	    if ( B_NR_ITEMS(tbS0) ) {
		replace_key(tb, tb->CFL[0],tb->lkey[0],tbS0,0);
#if 0 /* right delim key support */
		copy_key(B_PRIGHT_DELIM_KEY(tb->L[0]), B_N_PKEY(tbS0, 0));
		reiserfs_mark_buffer_dirty (tb->L[0], 0);
#endif
	    }
	    else {
		if ( ! PATH_H_POSITION (tb->tb_path, 1) )
		    replace_key(tb, tb->CFL[0],tb->lkey[0],PATH_H_PPARENT(tb->tb_path, 0),0);
#if 0 /* right delim key support */
		copy_key(B_PRIGHT_DELIM_KEY(tb->L[0]), B_PRIGHT_DELIM_KEY(tbS0));
		reiserfs_mark_buffer_dirty (tb->L[0], 0);
#endif
	    }
	} 

#ifdef CONFIG_REISERFS_CHECK
#if 0
	if (! item_pos && (!tb->CFL[0] || !tb->L[0]))
#endif
	    if (! item_pos && !tb->CFL[0])
		reiserfs_panic (tb->tb_sb, "PAP-12020: balance_leaf_when_delete: tb->CFL[0]==%p, tb->L[0]==%p", tb->CFL[0], tb->L[0]);
#endif
    
	break;

    case M_CUT: {  /* cut item in S[0] */
	bi.tb = tb;
	bi.bi_bh = tbS0;
	bi.bi_parent = PATH_H_PPARENT (tb->tb_path, 0);
	bi.bi_position = PATH_H_POSITION (tb->tb_path, 1);
	if (is_direntry_le_ih (ih)) {

#ifdef CONFIG_REISERFS_CHECK
#if 0 /* right delim key support */
	    if ( ! item_pos && ! pos_in_item && (! tb->L[0] || COMP_KEYS(B_PRIGHT_DELIM_KEY(tb->L[0]), 
									 B_N_PKEY(tbS0, 0))) )
		reiserfs_panic(tb->tb_sb, "PAP-12025: balance_leaf_when_delete: illegal right delimiting key");
#endif
#endif

	    /* UFS unlink semantics are such that you can only delete one directory entry at a time. */
	    /* when we cut a directory tb->insert_size[0] means number of entries to be cut (always 1) */
	    tb->insert_size[0] = -1;
	    leaf_cut_from_buffer (&bi, item_pos, pos_in_item, -tb->insert_size[0]);

#ifdef CONFIG_REISERFS_CHECK
	    if (! item_pos && ! pos_in_item && ! tb->CFL[0])
		reiserfs_panic (tb->tb_sb, "PAP-12030: balance_leaf_when_delete: can not change delimiting key. CFL[0]=%p", tb->CFL[0]);
#endif /* CONFIG_REISERFS_CHECK */

	    if ( ! item_pos && ! pos_in_item && tb->CFL[0] ) {
		replace_key(tb, tb->CFL[0],tb->lkey[0],tbS0,0);
#if 0/* right delim key support */
		copy_key(B_PRIGHT_DELIM_KEY(tb->L[0]), B_N_PKEY(tbS0, 0));
		reiserfs_mark_buffer_dirty (tb->L[0], 0);
#endif
	    }
	} else {
	    leaf_cut_from_buffer (&bi, item_pos, pos_in_item, -tb->insert_size[0]);

#ifdef CONFIG_REISERFS_CHECK
	    if (! ih->ih_item_len)
		reiserfs_panic (tb->tb_sb, "PAP-12035: balance_leaf_when_delete: cut must leave non-zero dynamic length of item");
#endif /* CONFIG_REISERFS_CHECK */
	}
	break;
    }

    default:
	print_cur_tb ("12040");
	reiserfs_panic (tb->tb_sb, "PAP-12040: balance_leaf_when_delete: unexpectable mode: %s(%d)",
			(flag == M_PASTE) ? "PASTE" : ((flag == M_INSERT) ? "INSERT" : "UNKNOWN"), flag);
    }

    /* the rule is that no shifting occurs unless by shifting a node can be freed */
    n = B_NR_ITEMS(tbS0);
    if ( tb->lnum[0] )     /* L[0] takes part in balancing */
    {
	if ( tb->lnum[0] == -1 )    /* L[0] must be joined with S[0] */
	{
	    if ( tb->rnum[0] == -1 )    /* R[0] must be also joined with S[0] */
	    {			
		if ( tb->FR[0] == PATH_H_PPARENT(tb->tb_path, 0) )
		{
		    /* all contents of all the 3 buffers will be in L[0] */
		    if ( PATH_H_POSITION (tb->tb_path, 1) == 0 && 1 < B_NR_ITEMS(tb->FR[0]) )
			replace_key(tb, tb->CFL[0],tb->lkey[0],tb->FR[0],1);

		    /* update right_delimiting_key field */
#if 0
		    copy_key (B_PRIGHT_DELIM_KEY (tb->L[0]), B_PRIGHT_DELIM_KEY (tb->R[0]));
#endif
		    leaf_move_items (LEAF_FROM_S_TO_L, tb, n, -1, 0);
		    leaf_move_items (LEAF_FROM_R_TO_L, tb, B_NR_ITEMS(tb->R[0]), -1, 0);

#if 0/*preserve list*/
		    preserve_invalidate(tb, tbS0, tb->L[0]); 
		    preserve_invalidate(tb, tb->R[0], tb->L[0]);
#endif
		    reiserfs_invalidate_buffer (tb, tbS0);
		    reiserfs_invalidate_buffer (tb, tb->R[0]);

		    return 0;
		}
		/* all contents of all the 3 buffers will be in R[0] */
		leaf_move_items (LEAF_FROM_S_TO_R, tb, n, -1, 0);
		leaf_move_items (LEAF_FROM_L_TO_R, tb, B_NR_ITEMS(tb->L[0]), -1, 0);

		/* right_delimiting_key is correct in R[0] */
		replace_key(tb, tb->CFR[0],tb->rkey[0],tb->R[0],0);

#if 0
		/* mark tb->R[0] as suspected recipient */
		preserve_invalidate(tb,tbS0, tb->R[0]);
		preserve_invalidate(tb,tb->L[0], tb->R[0]); 
#endif
		reiserfs_invalidate_buffer (tb, tbS0);
		reiserfs_invalidate_buffer (tb, tb->L[0]);

		return -1;
	    }

#ifdef CONFIG_REISERFS_CHECK
	    if ( tb->rnum[0] != 0 )
		reiserfs_panic (tb->tb_sb, "PAP-12045: balance_leaf_when_delete: "
				"rnum must be 0 (%d)", tb->rnum[0]);
#endif /* CONFIG_REISERFS_CHECK */

	    /* all contents of L[0] and S[0] will be in L[0] */
	    leaf_shift_left(tb, n, -1);

#if 0/*preserve list*/
	    preserve_invalidate(tb, tbS0, tb->L[0]);  /* preserved, shifting */
#endif
	    reiserfs_invalidate_buffer (tb, tbS0);

	    return 0;
	}
	/* a part of contents of S[0] will be in L[0] and the rest part of S[0] will be in R[0] */

#ifdef CONFIG_REISERFS_CHECK
	if (( tb->lnum[0] + tb->rnum[0] < n ) || ( tb->lnum[0] + tb->rnum[0] > n+1 ))
	    reiserfs_panic (tb->tb_sb, "PAP-12050: balance_leaf_when_delete: "
			    "rnum(%d) and lnum(%d) and item number in S[0] are not consistent",
			    tb->rnum[0], tb->lnum[0], n);

	if (( tb->lnum[0] + tb->rnum[0] == n ) && (tb->lbytes != -1 || tb->rbytes != -1))
	    reiserfs_panic (tb->tb_sb, "PAP-12055: balance_leaf_when_delete: "
			    "bad rbytes (%d)/lbytes (%d) parameters when items are not split", 
			    tb->rbytes, tb->lbytes);
	if (( tb->lnum[0] + tb->rnum[0] == n + 1 ) && (tb->lbytes < 1 || tb->rbytes != -1))
	    reiserfs_panic (tb->tb_sb, "PAP-12060: balance_leaf_when_delete: "
			    "bad rbytes (%d)/lbytes (%d) parameters when items are split", 
			    tb->rbytes, tb->lbytes);
#endif

	leaf_shift_left (tb, tb->lnum[0], tb->lbytes);
	leaf_shift_right(tb, tb->rnum[0], tb->rbytes);

#if 0/*preserve list*/
	preserve_invalidate (tb, tbS0, tb->L[0]);
	mark_suspected_recipient (tb->tb_sb, tb->R[0]);
#endif
	reiserfs_invalidate_buffer (tb, tbS0);

	return 0;
    }

    if ( tb->rnum[0] == -1 ) {
	/* all contents of R[0] and S[0] will be in R[0] */
	leaf_shift_right(tb, n, -1);
#if 0/*preserve list*/
	preserve_invalidate(tb, tbS0, tb->R[0]); 
#endif
	reiserfs_invalidate_buffer (tb, tbS0);
	return 0;
    }

#ifdef CONFIG_REISERFS_CHECK
    if ( tb->rnum[0] )
	reiserfs_panic (tb->tb_sb, "PAP-12065: balance_leaf_when_delete: "
			"bad rnum parameter must be 0 (%d)", tb->rnum[0]);
#endif

    return 0;
}


static int balance_leaf (struct tree_balance * tb,
			 struct item_head * ih,		/* item header of inserted item (this is on little endian) */
			 const char * body,		/* body  of inserted item or bytes to paste */
			 int flag,			/* i - insert, d - delete, c - cut, p - paste
							   (see comment to do_balance) */
			 struct item_head * insert_key,  /* in our processing of one level we sometimes determine what
							    must be inserted into the next higher level.  This insertion
							    consists of a key or two keys and their corresponding
							    pointers */
			 struct buffer_head ** insert_ptr /* inserted node-ptrs for the next level */
    )
{
    struct buffer_head * tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#if 0/*preserve list*/
    struct buffer_head * tbF0 = PATH_H_PPARENT (tb->tb_path, 0);
    int S0_b_item_order = PATH_H_B_ITEM_ORDER (tb->tb_path, 0);
#endif
    int item_pos = PATH_LAST_POSITION (tb->tb_path);	/*  index into the array of item headers in S[0] 
							    of the affected item */
    struct buffer_info bi;
    struct buffer_head *S_new[2];  /* new nodes allocated to hold what could not fit into S */
    int snum[2];	    /* number of items that will be placed
                               into S_new (includes partially shifted
                               items) */
    int sbytes[2];          /* if an item is partially shifted into S_new then 
			       if it is a directory item 
			       it is the number of entries from the item that are shifted into S_new
			       else
			       it is the number of bytes from the item that are shifted into S_new
			    */
    int n, i;
    int ret_val;
    int pos_in_item;
    int zeros_num;

#if 0
    if (tb->insert_size [0] % 4) {
	reiserfs_panic (tb->tb_sb, "balance_leaf: wrong insert_size %d", 
			tb->insert_size [0]);
    }
#endif
    /* Make balance in case insert_size[0] < 0 */
    if ( tb->insert_size[0] < 0 )
	return balance_leaf_when_delete (tb, flag);
  
    zeros_num = 0;
    if (flag == M_INSERT && body == 0)
	zeros_num = le16_to_cpu (ih->ih_item_len); 

    pos_in_item = tb->tb_path->pos_in_item;
    /* for indirect item pos_in_item is measured in unformatted node
       pointers. Recalculate to bytes */
    if (flag != M_INSERT && is_indirect_le_ih (B_N_PITEM_HEAD (tbS0, item_pos)))
	pos_in_item *= UNFM_P_SIZE;

    if ( tb->lnum[0] > 0 ) {
	/* Shift lnum[0] items from S[0] to the left neighbor L[0] */
	if ( item_pos < tb->lnum[0] ) {
	    /* new item or it part falls to L[0], shift it too */
	    n = B_NR_ITEMS(tb->L[0]);

	    switch (flag) {
	    case M_INSERT:   /* insert item into L[0] */

		if ( item_pos == tb->lnum[0] - 1 && tb->lbytes != -1 ) {
		    /* part of new item falls into L[0] */
		    int new_item_len;
		    int version;

#ifdef CONFIG_REISERFS_CHECK
		    if (!is_direct_le_ih (ih))
			reiserfs_panic (tb->tb_sb, "PAP-12075: balance_leaf: " 
					"only direct inserted item can be broken. %h", ih);
#endif
		    ret_val = leaf_shift_left (tb, tb->lnum[0]-1, -1);
		    /* when reading the if conditions preceding the subsequent preserve_shifted
		       lines understand that their goal is to determine if all that we are
		       shifting is the new data being added */
#if 0/*preserve list*/
		    if (tb->lnum[0] - 1 > 0) {
			preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
			tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
		    }
#endif

		    /* Calculate item length to insert to S[0] */
		    new_item_len = le16_to_cpu (ih->ih_item_len) - tb->lbytes;
		    /* Calculate and check item length to insert to L[0] */
		    ih->ih_item_len -= new_item_len;

#ifdef CONFIG_REISERFS_CHECK
		    if ( (int)(ih->ih_item_len) <= 0 )
			reiserfs_panic(tb->tb_sb, "PAP-12080: balance_leaf: "
				       "there is nothing to insert into L[0]: ih_item_len=%d",
				       (int)ih->ih_item_len);
#endif

		    /* Insert new item into L[0] */
		    bi.tb = tb;
		    bi.bi_bh = tb->L[0];
		    bi.bi_parent = tb->FL[0];
		    bi.bi_position = get_left_neighbor_position (tb, 0);
		    leaf_insert_into_buf (&bi, n + item_pos - ret_val, ih, body,
					  zeros_num > ih->ih_item_len ? ih->ih_item_len : zeros_num);

		    version = ih_version (ih);

		    /* Calculate key component, item length and body to insert into S[0] */
		    set_le_key_k_offset (ih_version (ih), &(ih->ih_key),
					 le_key_k_offset (ih_version (ih), &(ih->ih_key)) + tb->lbytes);
		    ih->ih_item_len = cpu_to_le16 (new_item_len);
		    if ( tb->lbytes >  zeros_num ) {
			body += (tb->lbytes - zeros_num);
			zeros_num = 0;
		    }
		    else
			zeros_num -= tb->lbytes;

#ifdef CONFIG_REISERFS_CHECK
		    if ( (int)(ih->ih_item_len) <= 0 )
			reiserfs_panic(tb->tb_sb, "PAP-12085: balance_leaf: "
				       "there is nothing to insert into S[0]: ih_item_len=%d",
				       (int)ih->ih_item_len);
#endif
		} else {
		    /* new item in whole falls into L[0] */
		    /* Shift lnum[0]-1 items to L[0] */
		    ret_val = leaf_shift_left(tb, tb->lnum[0]-1, tb->lbytes);
#if 0/*preserve list*/
		    if (tb->lnum[0] > 1) {
			preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
			tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
		    }
#endif
		    /* Insert new item into L[0] */
		    bi.tb = tb;
		    bi.bi_bh = tb->L[0];
		    bi.bi_parent = tb->FL[0];
		    bi.bi_position = get_left_neighbor_position (tb, 0);
		    leaf_insert_into_buf (&bi, n + item_pos - ret_val, ih, body, zeros_num);
#if 0/*preserve list*/
		    if (tb->preserve_mode == PRESERVE_INDIRECT_TO_DIRECT){
			mark_suspected_recipient (tb->tb_sb, bi.bi_bh);
		    }
#endif
		    tb->insert_size[0] = 0;
		    zeros_num = 0;
		}
		break;

	    case M_PASTE:   /* append item in L[0] */

		if ( item_pos == tb->lnum[0] - 1 && tb->lbytes != -1 ) {
		    /* we must shift the part of the appended item */
		    if ( is_direntry_le_ih (B_N_PITEM_HEAD (tbS0, item_pos))) {

#ifdef CONFIG_REISERFS_CHECK
			if ( zeros_num )
			    reiserfs_panic(tb->tb_sb, "PAP-12090: balance_leaf: illegal parameter in case of a directory");
#endif
            
			/* directory item */
			if ( tb->lbytes > pos_in_item ) {
			    /* new directory entry falls into L[0] */
			    struct item_head * pasted;
			    int l_pos_in_item = pos_in_item;
							  
			    /* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 entries from given directory item */
			    ret_val = leaf_shift_left(tb, tb->lnum[0], tb->lbytes - 1);
#if 0/*preserve list*/
			    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
			    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
			    if ( ret_val && ! item_pos ) {
				pasted =  B_N_PITEM_HEAD(tb->L[0],B_NR_ITEMS(tb->L[0])-1);
				l_pos_in_item += I_ENTRY_COUNT(pasted) - (tb->lbytes-1);
			    }

			    /* Append given directory entry to directory item */
			    bi.tb = tb;
			    bi.bi_bh = tb->L[0];
			    bi.bi_parent = tb->FL[0];
			    bi.bi_position = get_left_neighbor_position (tb, 0);
			    leaf_paste_in_buffer (&bi, n + item_pos - ret_val, l_pos_in_item,
						  tb->insert_size[0], body, zeros_num);

			    /* previous string prepared space for pasting new entry, following string pastes this entry */

			    /* when we have merge directory item, pos_in_item has been changed too */

			    /* paste new directory entry. 1 is entry number */
			    leaf_paste_entries (bi.bi_bh, n + item_pos - ret_val, l_pos_in_item, 1,
						(struct reiserfs_de_head *)body, 
						body + DEH_SIZE, tb->insert_size[0]
				);
			    tb->insert_size[0] = 0;
			} else {
			    /* new directory item doesn't fall into L[0] */
			    /* Shift lnum[0]-1 items in whole. Shift lbytes directory entries from directory item number lnum[0] */
			    leaf_shift_left (tb, tb->lnum[0], tb->lbytes);
#if 0/*preserve list*/
			    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
			    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
			}
			/* Calculate new position to append in item body */
			pos_in_item -= tb->lbytes;
		    }
		    else {
			/* regular object */

#ifdef CONFIG_REISERFS_CHECK
			if ( tb->lbytes  <= 0 )
			    reiserfs_panic(tb->tb_sb, "PAP-12095: balance_leaf: " 
					   "there is nothing to shift to L[0]. lbytes=%d",
					   tb->lbytes);
			if ( pos_in_item != B_N_PITEM_HEAD(tbS0, item_pos)->ih_item_len )
			    reiserfs_panic(tb->tb_sb, "PAP-12100: balance_leaf: " 
					   "incorrect position to paste: item_len=%d, pos_in_item=%d",
					   B_N_PITEM_HEAD(tbS0,item_pos)->ih_item_len, pos_in_item);
#endif

			if ( tb->lbytes >= pos_in_item ) {
			    /* appended item will be in L[0] in whole */
			    int l_n;

			    /* this bytes number must be appended to the last item of L[h] */
			    l_n = tb->lbytes - pos_in_item;

			    /* Calculate new insert_size[0] */
			    tb->insert_size[0] -= l_n;

#ifdef CONFIG_REISERFS_CHECK
			    if ( tb->insert_size[0] <= 0 )
				reiserfs_panic(tb->tb_sb, "PAP-12105: balance_leaf: " 
					       "there is nothing to paste into L[0]. insert_size=%d",
					       tb->insert_size[0]);
#endif

			    ret_val =  leaf_shift_left(tb,tb->lnum[0], 
						       B_N_PITEM_HEAD(tbS0,item_pos)->ih_item_len);
#if 0/*preserve list*/
			    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
			    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
			    /* Append to body of item in L[0] */
			    bi.tb = tb;
			    bi.bi_bh = tb->L[0];
			    bi.bi_parent = tb->FL[0];
			    bi.bi_position = get_left_neighbor_position (tb, 0);
			    leaf_paste_in_buffer(
				&bi,n + item_pos - ret_val,
				B_N_PITEM_HEAD(tb->L[0],n+item_pos-ret_val)->ih_item_len,
				l_n,body, zeros_num > l_n ? l_n : zeros_num
				);

#ifdef CONFIG_REISERFS_CHECK
			    if (l_n && is_indirect_le_ih(B_N_PITEM_HEAD(tb->L[0],
									n + item_pos - ret_val)))
				reiserfs_panic(tb->tb_sb, "PAP-12110: balance_leaf: "
					       "pasting more than 1 unformatted node pointer into indirect item");
#endif

			    /* 0-th item in S0 can be only of DIRECT type when l_n != 0*/
			    {
			      int version;

			      version = le16_to_cpu (B_N_PITEM_HEAD (tbS0, 0)->ih_version);
			      set_le_key_k_offset (version, B_N_PKEY (tbS0, 0), 
						   le_key_k_offset (version, B_N_PKEY (tbS0, 0)) + l_n);
			      set_le_key_k_offset (version, B_N_PDELIM_KEY(tb->CFL[0],tb->lkey[0]),
						   le_key_k_offset (version, B_N_PDELIM_KEY(tb->CFL[0],tb->lkey[0])) + l_n);
			    }
#if 0
			    set_le_key_k_offset (B_PRIGHT_DELIM_KEY(tb->L[0]), le_key_k_offset (B_PRIGHT_DELIM_KEY(tb->L[0])) + l_n);
#endif
			    /*    k_offset (B_N_PKEY (tbS0, 0)) += l_n;
				  k_offset (B_N_PDELIM_KEY(tb->CFL[0],tb->lkey[0])) += l_n;
				  k_offset (B_PRIGHT_DELIM_KEY(tb->L[0])) += l_n;*/

#ifdef NO_CONFIG_REISERFS_CHECK /* journal victim */
			    if (!buffer_dirty (tbS0) || !buffer_dirty (tb->CFL[0]) || !buffer_dirty (tb->L[0]))
				reiserfs_panic(tb->tb_sb, "PAP-12115: balance_leaf: L, CLF and S must be dirty already");
#endif

			    /* Calculate new body, position in item and insert_size[0] */
			    if ( l_n > zeros_num ) {
				body += (l_n - zeros_num);
				zeros_num = 0;
			    }
			    else
				zeros_num -= l_n;
			    pos_in_item = 0;	

#ifdef CONFIG_REISERFS_CHECK	
			    if (comp_short_le_keys (B_N_PKEY(tbS0,0),
						    B_N_PKEY(tb->L[0],B_NR_ITEMS(tb->L[0])-1)) ||
				!op_is_left_mergeable (B_N_PKEY (tbS0, 0), tbS0->b_size) ||
				!op_is_left_mergeable(B_N_PDELIM_KEY(tb->CFL[0],tb->lkey[0]), tbS0->b_size))
				reiserfs_panic (tb->tb_sb, "PAP-12120: balance_leaf: "
						"item must be merge-able with left neighboring item");
#endif

			}
			else /* only part of the appended item will be in L[0] */
			{
			    /* Calculate position in item for append in S[0] */
			    pos_in_item -= tb->lbytes;

#ifdef CONFIG_REISERFS_CHECK
			    if ( pos_in_item <= 0 )
				reiserfs_panic(tb->tb_sb, "PAP-12125: balance_leaf: "
					       "no place for paste. pos_in_item=%d", pos_in_item);
#endif

			    /* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 byte from item number lnum[0] */
			    leaf_shift_left(tb,tb->lnum[0],tb->lbytes);
#if 0/*preserve list*/
			    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
			    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
			}
		    }
		}
		else /* appended item will be in L[0] in whole */
		{
		    struct item_head * pasted;

#ifdef REISERFS_FSCK
		    if ( ! item_pos  && is_left_mergeable (tb->tb_sb, tb->tb_path) == 1 )
#else
			if ( ! item_pos  && op_is_left_mergeable (B_N_PKEY (tbS0, 0), tbS0->b_size) )
#endif
			{ /* if we paste into first item of S[0] and it is left mergable */
			    /* then increment pos_in_item by the size of the last item in L[0] */
			    pasted = B_N_PITEM_HEAD(tb->L[0],n-1);
			    if ( is_direntry_le_ih (pasted) )
				pos_in_item += le16_to_cpu (pasted->u.ih_entry_count);
			    else
				pos_in_item += le16_to_cpu (pasted->ih_item_len);
			}

		    /* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 byte from item number lnum[0] */
		    ret_val = leaf_shift_left(tb,tb->lnum[0],tb->lbytes);
#if 0/*preserve list*/
		    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
		    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
		    /* Append to body of item in L[0] */
		    bi.tb = tb;
		    bi.bi_bh = tb->L[0];
		    bi.bi_parent = tb->FL[0];
		    bi.bi_position = get_left_neighbor_position (tb, 0);
		    leaf_paste_in_buffer (&bi, n + item_pos - ret_val, pos_in_item, tb->insert_size[0],
					  body, zeros_num);

		    /* if appended item is directory, paste entry */
		    pasted = B_N_PITEM_HEAD (tb->L[0], n + item_pos - ret_val);
		    if (is_direntry_le_ih (pasted))
			leaf_paste_entries (
			    bi.bi_bh, n + item_pos - ret_val, pos_in_item, 1, 
			    (struct reiserfs_de_head *)body, body + DEH_SIZE, tb->insert_size[0]
			    );
		    /* if appended item is indirect item, put unformatted node into un list */
		    if (is_indirect_le_ih (pasted))
			set_ih_free_space (pasted, ((struct unfm_nodeinfo*)body)->unfm_freespace);
		    tb->insert_size[0] = 0;
		    zeros_num = 0;
		}
		break;
	    default:    /* cases d and t */
		reiserfs_panic (tb->tb_sb, "PAP-12130: balance_leaf: lnum > 0: unexpectable mode: %s(%d)",
				(flag == M_DELETE) ? "DELETE" : ((flag == M_CUT) ? "CUT" : "UNKNOWN"), flag);
	    }
	} else { 
	    /* new item doesn't fall into L[0] */
	    leaf_shift_left(tb,tb->lnum[0],tb->lbytes);
#if 0/*preserve list*/
	    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->L[0]);
	    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
	}
    }	/* tb->lnum[0] > 0 */

    /* Calculate new item position */
    item_pos -= ( tb->lnum[0] - (( tb->lbytes != -1 ) ? 1 : 0));

    if ( tb->rnum[0] > 0 ) {
	/* shift rnum[0] items from S[0] to the right neighbor R[0] */
	n = B_NR_ITEMS(tbS0);
	switch ( flag ) {

	case M_INSERT:   /* insert item */
	    if ( n - tb->rnum[0] < item_pos )
	    { /* new item or its part falls to R[0] */
		if ( item_pos == n - tb->rnum[0] + 1 && tb->rbytes != -1 )
		{ /* part of new item falls into R[0] */
		    int old_key_comp, old_len, r_zeros_number;
		    const char * r_body;
		    int version;
		    loff_t offset;

#ifdef CONFIG_REISERFS_CHECK
		    if ( !is_direct_le_ih (ih) )
			reiserfs_panic(tb->tb_sb, "PAP-12135: balance_leaf: "
				       "only direct item can be split. (%h)", ih);
#endif

		    leaf_shift_right(tb,tb->rnum[0]-1,-1);
#if 0/*preserve list*/
		    if (tb->rnum[0]>1) {
			preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
			tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
		    }
#endif

		    version = le16_to_cpu (ih->ih_version);
		    /* Remember key component and item length */
		    old_key_comp = le_key_k_offset (version, &(ih->ih_key));
		    old_len = le16_to_cpu (ih->ih_item_len);

		    /* Calculate key component and item length to insert into R[0] */
		    offset = le_key_k_offset (version, &(ih->ih_key)) + (old_len - tb->rbytes);
		    set_le_key_k_offset (version, &(ih->ih_key), offset);
		    ih->ih_item_len = cpu_to_le16 (tb->rbytes);
		    /* Insert part of the item into R[0] */
		    bi.tb = tb;
		    bi.bi_bh = tb->R[0];
		    bi.bi_parent = tb->FR[0];
		    bi.bi_position = get_right_neighbor_position (tb, 0);
		    if ( offset - old_key_comp > zeros_num ) {
			r_zeros_number = 0;
			r_body = body + offset - old_key_comp - zeros_num;
		    }
		    else {
			r_body = body;
			r_zeros_number = zeros_num - (offset - old_key_comp);
			zeros_num -= r_zeros_number;
		    }

		    leaf_insert_into_buf (&bi, 0, ih, r_body, r_zeros_number);

		    /* Replace right delimiting key by first key in R[0] */
		    replace_key(tb, tb->CFR[0],tb->rkey[0],tb->R[0],0);

		    /* Calculate key component and item length to insert into S[0] */
		    set_le_key_k_offset (version, &(ih->ih_key), old_key_comp);
		    ih->ih_item_len = cpu_to_le16 (old_len - tb->rbytes);

		    tb->insert_size[0] -= tb->rbytes;

		}
		else /* whole new item falls into R[0] */
		{					  
		    /* Shift rnum[0]-1 items to R[0] */
		    ret_val = leaf_shift_right(tb,tb->rnum[0]-1,tb->rbytes);
#if 0/*preserve list*/
		    if (tb->rnum[0]>1) {
			preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
			tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
		    }
#endif
		    /* Insert new item into R[0] */
		    bi.tb = tb;
		    bi.bi_bh = tb->R[0];
		    bi.bi_parent = tb->FR[0];
		    bi.bi_position = get_right_neighbor_position (tb, 0);
		    leaf_insert_into_buf (&bi, item_pos - n + tb->rnum[0] - 1, ih, body, zeros_num);
#if 0/*preserve list*/
		    if (tb->preserve_mode == PRESERVE_INDIRECT_TO_DIRECT){
			mark_suspected_recipient (tb->tb_sb, bi.bi_bh);
		    }
#endif

		    /* If we insert new item in the begin of R[0] change the right delimiting key */
		    if ( item_pos - n + tb->rnum[0] - 1 == 0 ) {
			replace_key(tb, tb->CFR[0],tb->rkey[0],tb->R[0],0);

#if 0
			/* update right delimiting key */
			copy_key(B_PRIGHT_DELIM_KEY(tbS0), &(ih->ih_key));
			reiserfs_mark_buffer_dirty (tbS0, 0);
#endif
		    }
		    zeros_num = tb->insert_size[0] = 0;
		}
	    }
	    else /* new item or part of it doesn't fall into R[0] */
	    {
		leaf_shift_right(tb,tb->rnum[0],tb->rbytes);
#if 0/*preserve list*/
		preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
		tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
	    }
	    break;

	case M_PASTE:   /* append item */

	    if ( n - tb->rnum[0] <= item_pos )  /* pasted item or part of it falls to R[0] */
	    {
		if ( item_pos == n - tb->rnum[0] && tb->rbytes != -1 )
		{ /* we must shift the part of the appended item */
		    if ( is_direntry_le_ih (B_N_PITEM_HEAD(tbS0, item_pos)))
		    { /* we append to directory item */
			int entry_count;

#ifdef CONFIG_REISERFS_CHECK
			if ( zeros_num )
			    reiserfs_panic(tb->tb_sb, "PAP-12145: balance_leaf: illegal parametr in case of a directory");
#endif

			entry_count = I_ENTRY_COUNT(B_N_PITEM_HEAD(tbS0, item_pos));
			if ( entry_count - tb->rbytes < pos_in_item )
			    /* new directory entry falls into R[0] */
			{
			    int paste_entry_position;

#ifdef CONFIG_REISERFS_CHECK
			    if ( tb->rbytes - 1 >= entry_count || ! tb->insert_size[0] )
				reiserfs_panic(tb->tb_sb, "PAP-12150: balance_leaf: "
					       "no enough of entries to shift to R[0]: rbytes=%d, entry_count=%d",
					       tb->rbytes, entry_count);
#endif

			    /* Shift rnum[0]-1 items in whole. Shift rbytes-1 directory entries from directory item number rnum[0] */
			    leaf_shift_right(tb,tb->rnum[0],tb->rbytes - 1);
#if 0/*preserve list*/
			    /* if we are shifting more than just the new entry */
			    if (tb->rbytes > 1 || tb->rnum[0] > 1) {
				preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
				tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
			    }
#endif
			    /* Paste given directory entry to directory item */
			    paste_entry_position = pos_in_item - entry_count + tb->rbytes - 1;
			    bi.tb = tb;
			    bi.bi_bh = tb->R[0];
			    bi.bi_parent = tb->FR[0];
			    bi.bi_position = get_right_neighbor_position (tb, 0);
			    leaf_paste_in_buffer (&bi, 0, paste_entry_position,
						  tb->insert_size[0],body,zeros_num);
			    /* paste entry */
			    leaf_paste_entries (
				bi.bi_bh, 0, paste_entry_position, 1, (struct reiserfs_de_head *)body, 
				body + DEH_SIZE, tb->insert_size[0]
				);								
						
			    if ( paste_entry_position == 0 ) {
				/* change delimiting keys */
				replace_key(tb, tb->CFR[0],tb->rkey[0],tb->R[0],0);
#if 0
				copy_key(B_PRIGHT_DELIM_KEY(tbS0), B_N_PKEY(tb->R[0], 0));
				reiserfs_mark_buffer_dirty (tbS0, 0);
#endif
			    }

			    tb->insert_size[0] = 0;
			    pos_in_item++;
			}
			else /* new directory entry doesn't fall into R[0] */
			{
			    leaf_shift_right(tb,tb->rnum[0],tb->rbytes);
#if 0/*preserve list*/
			    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
			    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
			}
		    }
		    else /* regular object */
		    {
			int n_shift, n_rem, r_zeros_number;
			const char * r_body;

			/* Calculate number of bytes which must be shifted from appended item */
			if ( (n_shift = tb->rbytes - tb->insert_size[0]) < 0 )
			    n_shift = 0;

#ifdef CONFIG_REISERFS_CHECK
			if (pos_in_item != B_N_PITEM_HEAD (tbS0, item_pos)->ih_item_len)
			    reiserfs_panic(tb->tb_sb,"PAP-12155: balance_leaf: invalid position to paste. ih_item_len=%d, pos_in_item=%d",
					   pos_in_item, B_N_PITEM_HEAD(tbS0,item_pos)->ih_item_len);
#endif

			leaf_shift_right(tb,tb->rnum[0],n_shift);
#if 0/*preserve list*/
			/* if we are shifting an old part from the appended item or more than the appended item is going into R */
			if (n_shift || tb->rnum[0] > 1) {
			    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
			    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
			}
#endif
			/* Calculate number of bytes which must remain in body after appending to R[0] */
			if ( (n_rem = tb->insert_size[0] - tb->rbytes) < 0 )
			    n_rem = 0;
			
			{
			  int version;
			  
			  version = ih_version (B_N_PITEM_HEAD (tb->R[0],0));
			  set_le_key_k_offset (version, B_N_PKEY(tb->R[0],0), 
					       le_key_k_offset (version, B_N_PKEY(tb->R[0],0)) + n_rem);
			  set_le_key_k_offset (version, B_N_PDELIM_KEY(tb->CFR[0],tb->rkey[0]), 
					       le_key_k_offset (version, B_N_PDELIM_KEY(tb->CFR[0],tb->rkey[0])) + n_rem);
			}
/*		  k_offset (B_N_PKEY(tb->R[0],0)) += n_rem;
		  k_offset (B_N_PDELIM_KEY(tb->CFR[0],tb->rkey[0])) += n_rem;*/
			do_balance_mark_internal_dirty (tb, tb->CFR[0], 0);

#if 0
			set_le_key_k_offset (B_PRIGHT_DELIM_KEY(tbS0), le_key_k_offset (B_PRIGHT_DELIM_KEY(tbS0)) + n_rem);
/*		  k_offset (B_PRIGHT_DELIM_KEY(tbS0)) += n_rem;*/
			reiserfs_mark_buffer_dirty (tbS0, 0);
#endif
			/* Append part of body into R[0] */
			bi.tb = tb;
			bi.bi_bh = tb->R[0];
			bi.bi_parent = tb->FR[0];
			bi.bi_position = get_right_neighbor_position (tb, 0);
			if ( n_rem > zeros_num ) {
			    r_zeros_number = 0;
			    r_body = body + n_rem - zeros_num;
			}
			else {
			    r_body = body;
			    r_zeros_number = zeros_num - n_rem;
			    zeros_num -= r_zeros_number;
			}

			leaf_paste_in_buffer(&bi, 0, n_shift, tb->insert_size[0] - n_rem, r_body, r_zeros_number);

			if (is_indirect_le_ih (B_N_PITEM_HEAD(tb->R[0],0))) {

#ifdef CONFIG_REISERFS_CHECK
			    if (n_rem)
				reiserfs_panic(tb->tb_sb, "PAP-12160: balance_leaf: paste more than one unformatted node pointer");
#endif

			    set_ih_free_space (B_N_PITEM_HEAD(tb->R[0],0), ((struct unfm_nodeinfo*)body)->unfm_freespace);
			}

			tb->insert_size[0] = n_rem;
			if ( ! n_rem )
			    pos_in_item ++;
		    }
		}
		else /* pasted item in whole falls into R[0] */
		{
		    struct item_head * pasted;

		    ret_val = leaf_shift_right(tb,tb->rnum[0],tb->rbytes);
#if 0/*preserve list*/
		    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
		    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
		    /* append item in R[0] */
		    if ( pos_in_item >= 0 ) {
			bi.tb = tb;
			bi.bi_bh = tb->R[0];
			bi.bi_parent = tb->FR[0];
			bi.bi_position = get_right_neighbor_position (tb, 0);
			leaf_paste_in_buffer(&bi,item_pos - n + tb->rnum[0], pos_in_item,
					     tb->insert_size[0],body, zeros_num);
		    }

		    /* paste new entry, if item is directory item */
		    pasted = B_N_PITEM_HEAD(tb->R[0], item_pos - n + tb->rnum[0]);
		    if (is_direntry_le_ih (pasted) && pos_in_item >= 0 ) {
			leaf_paste_entries (
			    bi.bi_bh, item_pos - n + tb->rnum[0], pos_in_item, 1, 
			    (struct reiserfs_de_head *)body, body + DEH_SIZE, tb->insert_size[0]
			    );
			if ( ! pos_in_item ) {

#ifdef CONFIG_REISERFS_CHECK
			    if ( item_pos - n + tb->rnum[0] )
				reiserfs_panic (tb->tb_sb, "PAP-12165: balance_leaf: " 
						"directory item must be first item of node when pasting is in 0th position");
#endif

			    /* update delimiting keys */
			    replace_key(tb, tb->CFR[0],tb->rkey[0],tb->R[0],0);
#if 0
			    copy_key(B_PRIGHT_DELIM_KEY(tbS0),B_N_PKEY(tb->R[0], 0));
			    reiserfs_mark_buffer_dirty (tbS0, 0);
#endif
			}
		    }

		    if (is_indirect_le_ih (pasted))
			set_ih_free_space (pasted, ((struct unfm_nodeinfo*)body)->unfm_freespace);
		    zeros_num = tb->insert_size[0] = 0;
		}
	    }
	    else /* new item doesn't fall into R[0] */
	    {
		leaf_shift_right(tb,tb->rnum[0],tb->rbytes);
#if 0/*preserve list*/
		preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, tb->R[0]);
		tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
	    }
	    break;
	default:    /* cases d and t */
	    reiserfs_panic (tb->tb_sb, "PAP-12175: balance_leaf: rnum > 0: unexpectable mode: %s(%d)",
			    (flag == M_DELETE) ? "DELETE" : ((flag == M_CUT) ? "CUT" : "UNKNOWN"), flag);
	}
    
    }	/* tb->rnum[0] > 0 */


#ifdef CONFIG_REISERFS_CHECK
    if ( tb->blknum[0] > 3 )  
	reiserfs_panic (tb->tb_sb, "PAP-12180: balance_leaf: blknum can not be %d. It must be <= 3",  tb->blknum[0]);

    if ( tb->blknum[0] < 0 )  
	reiserfs_panic (tb->tb_sb, "PAP-12185: balance_leaf: blknum can not be %d. It must be >= 0",  tb->blknum[0]);
#endif

    /* if while adding to a node we discover that it is possible to split
       it in two, and merge the left part into the left neighbor and the
       right part into the right neighbor, eliminating the node */
    if ( tb->blknum[0] == 0 ) { /* node S[0] is empty now */

#ifdef CONFIG_REISERFS_CHECK
	if ( ! tb->lnum[0] || ! tb->rnum[0] )
	    reiserfs_panic(tb->tb_sb, "PAP-12190: balance_leaf: lnum and rnum must not be zero");
#if 0
	if (COMP_KEYS (B_N_PKEY(tb->R[0], 0), B_PRIGHT_DELIM_KEY(tbS0)))
	    reiserfs_panic (tb->tb_sb, "vs-12192: balance_leaf: S[0] is being removed from the tree, it has incorrect right delimiting key");
#endif
#endif

#if 0
	/* if insertion was done before 0-th position in R[0], right
	   delimiting key of the tb->L[0]'s and left delimiting key are
	   not set correctly */
	if (tb->L[0]) {
	    copy_key(B_PRIGHT_DELIM_KEY(tb->L[0]), B_PRIGHT_DELIM_KEY(tbS0));
	    reiserfs_mark_buffer_dirty (tb->L[0], 0);
	}

	if (tb->CFL[0]) {
	    copy_key (B_N_PDELIM_KEY (tb->CFL[0], tb->lkey[0]), B_PRIGHT_DELIM_KEY(tbS0));
	    reiserfs_mark_buffer_dirty (tb->CFL[0], 0);
	}
#endif
    
	/* if insertion was done before 0-th position in R[0], right
	   delimiting key of the tb->L[0]'s and left delimiting key are
	   not set correctly */
	if (tb->CFL[0]) {
	    if (!tb->CFR[0])
		reiserfs_panic (tb->tb_sb, "vs-12195: balance_leaf: CFR not initialized");
	    copy_key (B_N_PDELIM_KEY (tb->CFL[0], tb->lkey[0]), B_N_PDELIM_KEY (tb->CFR[0], tb->rkey[0]));
	    do_balance_mark_internal_dirty (tb, tb->CFL[0], 0);
	}

	reiserfs_invalidate_buffer(tb,tbS0);									
	return 0;
    }


    /* Fill new nodes that appear in place of S[0] */

    /* I am told that this copying is because we need an array to enable
       the looping code. -Hans */
    snum[0] = tb->s1num,
	snum[1] = tb->s2num;
    sbytes[0] = tb->s1bytes;
    sbytes[1] = tb->s2bytes;
    for( i = tb->blknum[0] - 2; i >= 0; i-- ) {

#ifdef CONFIG_REISERFS_CHECK
	if (!snum[i])
	    reiserfs_panic(tb->tb_sb,"PAP-12200: balance_leaf: snum[%d] == %d. Must be > 0", i, snum[i]);
#endif /* CONFIG_REISERFS_CHECK */

	/* here we shift from S to S_new nodes */

	S_new[i] = get_FEB(tb);

	/* initialized block type and tree level */
	B_BLK_HEAD(S_new[i])->blk_level = cpu_to_le16 (DISK_LEAF_NODE_LEVEL);


	n = B_NR_ITEMS(tbS0);
	
	switch (flag) {
	case M_INSERT:   /* insert item */

	    if ( n - snum[i] < item_pos )
	    { /* new item or it's part falls to first new node S_new[i]*/
		if ( item_pos == n - snum[i] + 1 && sbytes[i] != -1 )
		{ /* part of new item falls into S_new[i] */
		    int old_key_comp, old_len, r_zeros_number;
		    const char * r_body;
		    int version;

#ifdef CONFIG_REISERFS_CHECK
		    if ( !is_direct_le_ih(ih) )
			/* The items which can be inserted are:
			   Stat_data item, direct item, indirect item and directory item which consist of only two entries "." and "..".
			   These items must not be broken except for a direct one. */
			reiserfs_panic(tb->tb_sb, "PAP-12205: balance_leaf: "
				       "non-direct item can not be broken when inserting");
#endif

		    /* Move snum[i]-1 items from S[0] to S_new[i] */
		    leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i] - 1, -1, S_new[i]);
#if 0/*preserve list*/
		    if (snum[i] > 1 ) {
			preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, S_new[i]);
			tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
		    }
#endif
		    /* Remember key component and item length */
		    version = ih_version (ih);
		    old_key_comp = le_key_k_offset (version, &(ih->ih_key));
		    old_len = le16_to_cpu (ih->ih_item_len);

		    /* Calculate key component and item length to insert into S_new[i] */
		    set_le_key_k_offset (version, &(ih->ih_key), 
					 le_key_k_offset (version, &(ih->ih_key)) + (old_len - sbytes[i]));

		    ih->ih_item_len = cpu_to_le16 (sbytes[i]);

		    /* Insert part of the item into S_new[i] before 0-th item */
		    bi.tb = tb;
		    bi.bi_bh = S_new[i];
		    bi.bi_parent = 0;
		    bi.bi_position = 0;

		    if ( le_key_k_offset (version, &(ih->ih_key)) - old_key_comp > zeros_num ) {
			r_zeros_number = 0;
			r_body = body + (le_key_k_offset (version, &(ih->ih_key)) - old_key_comp) - zeros_num;
		    }
		    else {
			r_body = body;
			r_zeros_number = zeros_num - (le_key_k_offset (version, &(ih->ih_key)) - old_key_comp);
			zeros_num -= r_zeros_number;
		    }

		    leaf_insert_into_buf (&bi, 0, ih, r_body, r_zeros_number);

		    /* Calculate key component and item length to insert into S[i] */
		    set_le_key_k_offset (version, &(ih->ih_key), old_key_comp);
		    ih->ih_item_len = cpu_to_le16 (old_len - sbytes[i]);
		    tb->insert_size[0] -= sbytes[i];
		}
		else /* whole new item falls into S_new[i] */
		{
		    /* Shift snum[0] - 1 items to S_new[i] (sbytes[i] of split item) */
		    leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i] - 1, sbytes[i], S_new[i]);

		    /* Insert new item into S_new[i] */
		    bi.tb = tb;
		    bi.bi_bh = S_new[i];
		    bi.bi_parent = 0;
		    bi.bi_position = 0;
		    leaf_insert_into_buf (&bi, item_pos - n + snum[i] - 1, ih, body, zeros_num);
#if 0/*preserve list*/
		    if (tb->preserve_mode == PRESERVE_INDIRECT_TO_DIRECT){
			mark_suspected_recipient (tb->tb_sb, bi.bi_bh);
		    }
#endif

		    zeros_num = tb->insert_size[0] = 0;
		}
	    }

	    else /* new item or it part don't falls into S_new[i] */
	    {
		leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i], sbytes[i], S_new[i]);
#if 0/*preserve list*/
		preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, S_new[i]);
		tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
	    }
	    break;

	case M_PASTE:   /* append item */

	    if ( n - snum[i] <= item_pos )  /* pasted item or part if it falls to S_new[i] */
	    {
		if ( item_pos == n - snum[i] && sbytes[i] != -1 )
		{ /* we must shift part of the appended item */
		    struct item_head * aux_ih;

#ifdef CONFIG_REISERFS_CHECK
		    if ( ih )
			reiserfs_panic (tb->tb_sb, "PAP-12210: balance_leaf: ih must be 0");
#endif /* CONFIG_REISERFS_CHECK */

		    if ( is_direntry_le_ih (aux_ih = B_N_PITEM_HEAD(tbS0,item_pos))) {
			/* we append to directory item */

			int entry_count;
		
			entry_count = le16_to_cpu (aux_ih->u.ih_entry_count);

			if ( entry_count - sbytes[i] < pos_in_item  && pos_in_item <= entry_count ) {
			    /* new directory entry falls into S_new[i] */
		  
#ifdef CONFIG_REISERFS_CHECK
			    if ( ! tb->insert_size[0] )
				reiserfs_panic (tb->tb_sb, "PAP-12215: balance_leaif: insert_size is already 0");
			    if ( sbytes[i] - 1 >= entry_count )
				reiserfs_panic (tb->tb_sb, "PAP-12220: balance_leaf: "
						"there are no so much entries (%d), only %d",
						sbytes[i] - 1, entry_count);
#endif

			    /* Shift snum[i]-1 items in whole. Shift sbytes[i] directory entries from directory item number snum[i] */
			    leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i], sbytes[i]-1, S_new[i]);
#if 0/*preserve list*/
			    /* if more than the affected item is shifted, or if more than
			       one entry (from the affected item) is shifted */
			    if (snum[i] > 1 || sbytes[i] > 1) {
				preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, S_new[i]);
				tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
			    }
#endif
			    /* Paste given directory entry to directory item */
			    bi.tb = tb;
			    bi.bi_bh = S_new[i];
			    bi.bi_parent = 0;
			    bi.bi_position = 0;
			    leaf_paste_in_buffer (&bi, 0, pos_in_item - entry_count + sbytes[i] - 1,
						  tb->insert_size[0], body,zeros_num);
			    /* paste new directory entry */
			    leaf_paste_entries (
				bi.bi_bh, 0, pos_in_item - entry_count + sbytes[i] - 1,
				1, (struct reiserfs_de_head *)body, body + DEH_SIZE,
				tb->insert_size[0]
				);
			    tb->insert_size[0] = 0;
			    pos_in_item++;
			} else { /* new directory entry doesn't fall into S_new[i] */
			    leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i], sbytes[i], S_new[i]);
			}
		    }
		    else /* regular object */
		    {
			int n_shift, n_rem, r_zeros_number;
			const char * r_body;

#ifdef CONFIG_REISERFS_CHECK
			if ( pos_in_item != B_N_PITEM_HEAD(tbS0,item_pos)->ih_item_len ||
			     tb->insert_size[0] <= 0 )
			    reiserfs_panic (tb->tb_sb, "PAP-12225: balance_leaf: item too short or insert_size <= 0");
#endif

			/* Calculate number of bytes which must be shifted from appended item */
			n_shift = sbytes[i] - tb->insert_size[0];
			if ( n_shift < 0 )
			    n_shift = 0;
			leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i], n_shift, S_new[i]);

			/* Calculate number of bytes which must remain in body after append to S_new[i] */
			n_rem = tb->insert_size[0] - sbytes[i];
			if ( n_rem < 0 )
			    n_rem = 0;
			/* Append part of body into S_new[0] */
			bi.tb = tb;
			bi.bi_bh = S_new[i];
			bi.bi_parent = 0;
			bi.bi_position = 0;

			if ( n_rem > zeros_num ) {
			    r_zeros_number = 0;
			    r_body = body + n_rem - zeros_num;
			}
			else {
			    r_body = body;
			    r_zeros_number = zeros_num - n_rem;
			    zeros_num -= r_zeros_number;
			}

			leaf_paste_in_buffer(&bi, 0, n_shift, tb->insert_size[0]-n_rem, r_body,r_zeros_number);
			{
			    struct item_head * tmp;

			    tmp = B_N_PITEM_HEAD(S_new[i],0);
			    if (is_indirect_le_ih (tmp)) {
				if (n_rem)
				    reiserfs_panic (tb->tb_sb, "PAP-12230: balance_leaf: invalid action with indirect item");
				set_ih_free_space (tmp, ((struct unfm_nodeinfo*)body)->unfm_freespace);
			    }
			    set_le_key_k_offset (ih_version (tmp), &tmp->ih_key, 
						 le_key_k_offset (ih_version (tmp), &tmp->ih_key) + n_rem);
			}

			tb->insert_size[0] = n_rem;
			if ( ! n_rem )
			    pos_in_item++;
		    }
		}
		else
		    /* item falls wholly into S_new[i] */
		{
		    int ret_val;
		    struct item_head * pasted;

#ifdef CONFIG_REISERFS_CHECK
		    struct item_head * ih = B_N_PITEM_HEAD(tbS0,item_pos);

		    if ( ! is_direntry_le_ih(ih) && (pos_in_item != ih->ih_item_len ||
						     tb->insert_size[0] <= 0) )
			reiserfs_panic (tb->tb_sb, "PAP-12235: balance_leaf: pos_in_item must be equal to ih_item_len");
#endif /* CONFIG_REISERFS_CHECK */

		    ret_val = leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i], sbytes[i], S_new[i]);
#if 0/*preserve list*/
		    /* we must preserve that which we are pasting onto the end of and shifting */
		    preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, S_new[i]);
		    tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif

#ifdef CONFIG_REISERFS_CHECK
		    if ( ret_val )
			reiserfs_panic (tb->tb_sb, "PAP-12240: balance_leaf: "
					"unexpected value returned by leaf_move_items (%d)",
					ret_val);
#endif /* CONFIG_REISERFS_CHECK */

		    /* paste into item */
		    bi.tb = tb;
		    bi.bi_bh = S_new[i];
		    bi.bi_parent = 0;
		    bi.bi_position = 0;
		    leaf_paste_in_buffer(&bi, item_pos - n + snum[i], pos_in_item, tb->insert_size[0], body, zeros_num);

		    pasted = B_N_PITEM_HEAD(S_new[i], item_pos - n + snum[i]);
		    if (is_direntry_le_ih (pasted))
		    {
			leaf_paste_entries (
			    bi.bi_bh, item_pos - n + snum[i], pos_in_item, 1, 
			    (struct reiserfs_de_head *)body, body + DEH_SIZE, tb->insert_size[0]
			    );
		    }

		    /* if we paste to indirect item update ih_free_space */
		    if (is_indirect_le_ih (pasted))
			set_ih_free_space (pasted, ((struct unfm_nodeinfo*)body)->unfm_freespace);
		    zeros_num = tb->insert_size[0] = 0;
		}
	    }

	    else /* pasted item doesn't fall into S_new[i] */
	    {
		leaf_move_items (LEAF_FROM_S_TO_SNEW, tb, snum[i], sbytes[i], S_new[i]);
#if 0/*preserve list*/
		preserve_shifted(tb, &(PATH_PLAST_BUFFER (tb->tb_path)), tbF0, S0_b_item_order, S_new[i]);
		tbS0 = PATH_PLAST_BUFFER (tb->tb_path);
#endif
	    }
	    break;
	default:    /* cases d and t */
	    reiserfs_panic (tb->tb_sb, "PAP-12245: balance_leaf: blknum > 2: unexpectable mode: %s(%d)",
			    (flag == M_DELETE) ? "DELETE" : ((flag == M_CUT) ? "CUT" : "UNKNOWN"), flag);
	}

	memcpy (insert_key + i,B_N_PKEY(S_new[i],0),KEY_SIZE);
	insert_ptr[i] = S_new[i];

#ifdef CONFIG_REISERFS_CHECK
	if (atomic_read (&(S_new[i]->b_count)) != 1) {
	    if (atomic_read(&(S_new[i]->b_count)) != 2 ||
	      !(buffer_journaled(S_new[i]) || buffer_journal_dirty(S_new[i]))) {
	      reiserfs_panic (tb->tb_sb, "PAP-12247: balance_leaf: S_new[%d] : (%b)\n", i, S_new[i]);
	    }
	}
#endif

#if 0
	/* update right_delimiting_key fields */
	copy_key (B_PRIGHT_DELIM_KEY (S_new[i]), B_PRIGHT_DELIM_KEY (tbS0));
	copy_key (B_PRIGHT_DELIM_KEY (tbS0), B_N_PKEY (S_new[i], 0));
	reiserfs_mark_buffer_dirty (tbS0, 0);
#endif

    }

    /* if the affected item was not wholly shifted then we perform all necessary operations on that part or whole of the
       affected item which remains in S */
    if ( 0 <= item_pos && item_pos < tb->s0num )
    { /* if we must insert or append into buffer S[0] */

	switch (flag)
	{
	case M_INSERT:   /* insert item into S[0] */
	    bi.tb = tb;
	    bi.bi_bh = tbS0;
	    bi.bi_parent = PATH_H_PPARENT (tb->tb_path, 0);
	    bi.bi_position = PATH_H_POSITION (tb->tb_path, 1);
	    leaf_insert_into_buf (&bi, item_pos, ih, body, zeros_num);
#if 0/*preserve list*/
	    if (tb->preserve_mode == PRESERVE_INDIRECT_TO_DIRECT){
		mark_suspected_recipient (tb->tb_sb, bi.bi_bh);
	    }
#endif

	    /* If we insert the first key change the delimiting key */
	    if( item_pos == 0 ) {
		if (tb->CFL[0]) /* can be 0 in reiserfsck */
		    replace_key(tb, tb->CFL[0], tb->lkey[0],tbS0,0);

#if 0 /* right delim key support */
#ifdef CONFIG_REISERFS_CHECK
		if ( ! tb->CFL[0] || ! tb->L[0] || (B_NR_ITEMS (tbS0) > 1 && 
						    COMP_KEYS(B_PRIGHT_DELIM_KEY(tb->L[0]), B_N_PKEY(tbS0, 1))) )
		    reiserfs_panic(tb->tb_sb, "PAP-12250: balance_leaf: invalid right delimiting key");
		if (!buffer_dirty (tb->L[0]) && !(buffer_journaled(tb->L[0]) ||
		     buffer_journal_dirty(tb->L[0])))
		    reiserfs_panic (tb->tb_sb, "PAP-12255: balance_leaf: tb->L[0] must be dirty");
#endif
		if (tb->L[0]) /* can be 0 in reiserfsck */
		    copy_key (B_PRIGHT_DELIM_KEY (tb->L[0]), &(ih->ih_key));   
#endif /* right delim key support */
	    }
	    break;

	case M_PASTE: {  /* append item in S[0] */
	    struct item_head * pasted;

	    pasted = B_N_PITEM_HEAD (tbS0, item_pos);
	    /* when directory, may be new entry already pasted */
	    if (is_direntry_le_ih (pasted)) {
		if ( pos_in_item >= 0 && pos_in_item <= le16_to_cpu (pasted->u.ih_entry_count) ) {

#ifdef CONFIG_REISERFS_CHECK
		    if ( ! tb->insert_size[0] )
			reiserfs_panic (tb->tb_sb, "PAP-12260: balance_leaf: insert_size is 0 already");
#endif /* CONFIG_REISERFS_CHECK */

		    /* prepare space */
		    bi.tb = tb;
		    bi.bi_bh = tbS0;
		    bi.bi_parent = PATH_H_PPARENT (tb->tb_path, 0);
		    bi.bi_position = PATH_H_POSITION (tb->tb_path, 1);
		    leaf_paste_in_buffer(&bi, item_pos, pos_in_item, tb->insert_size[0], body, zeros_num);


#ifdef CONFIG_REISERFS_CHECK
#if 0
		    if ( ! item_pos && ! pos_in_item  && (! tb->L[0] || COMP_KEYS(B_PRIGHT_DELIM_KEY(tb->L[0]), 
										  B_N_PKEY(tbS0, 0))) )
			reiserfs_panic(tb->tb_sb, "PAP-12265: balance_leaf: invalid right delimiting key");
#endif
#endif

		    /* paste entry */
		    leaf_paste_entries (
			bi.bi_bh, item_pos, pos_in_item, 1, (struct reiserfs_de_head *)body,
			body + DEH_SIZE, tb->insert_size[0]
			);
		    if ( ! item_pos && ! pos_in_item ) {

#ifdef CONFIG_REISERFS_CHECK
			if (!tb->CFL[0] || !tb->L[0])
			    reiserfs_panic (tb->tb_sb, "PAP-12270: balance_leaf: CFL[0]/L[0] must be specified");
#endif /* CONFIG_REISERFS_CHECK */

			if (tb->CFL[0]) {
			    replace_key(tb, tb->CFL[0], tb->lkey[0],tbS0,0);

#if 0
			    /* update right delimiting key */
			    copy_key (B_PRIGHT_DELIM_KEY (tb->L[0]), B_N_PKEY(tbS0, 0));   
			    /* probably not needed as something has been shifted to tb->L[0] already */
			    reiserfs_mark_buffer_dirty (tb->L[0], 0);
#endif
			}
		    }
		    tb->insert_size[0] = 0;
		}
	    } else { /* regular object */
		if ( pos_in_item == pasted->ih_item_len ) {

#ifdef CONFIG_REISERFS_CHECK
		    if ( tb->insert_size[0] <= 0 )
			reiserfs_panic (tb->tb_sb,
					"PAP-12275: balance_leaf: insert size must not be %d", tb->insert_size[0]);
#endif /* CONFIG_REISERFS_CHECK */
		    bi.tb = tb;
		    bi.bi_bh = tbS0;
		    bi.bi_parent = PATH_H_PPARENT (tb->tb_path, 0);
		    bi.bi_position = PATH_H_POSITION (tb->tb_path, 1);
		    leaf_paste_in_buffer (&bi, item_pos, pos_in_item, tb->insert_size[0], body, zeros_num);

		    if (is_indirect_le_ih (pasted)) {

#ifdef CONFIG_REISERFS_CHECK
			if ( tb->insert_size[0] != UNFM_P_SIZE )
			    reiserfs_panic (tb->tb_sb,
					    "PAP-12280: balance_leaf: insert_size for indirect item must be %d, not %d",
					    UNFM_P_SIZE, tb->insert_size[0]);
#endif /* CONFIG_REISERFS_CHECK */

			set_ih_free_space (pasted, ((struct unfm_nodeinfo*)body)->unfm_freespace);
		    }
		    tb->insert_size[0] = 0;
		}

#ifdef CONFIG_REISERFS_CHECK
		else {
		    if ( tb->insert_size[0] ) {
			print_cur_tb ("12285");
			reiserfs_panic (tb->tb_sb, "PAP-12285: balance_leaf: insert_size must be 0 (%d)", tb->insert_size[0]);
		    }
		}
#endif /* CONFIG_REISERFS_CHECK */
	    
	    }
	} /* case M_PASTE: */
	}
    }

#ifdef CONFIG_REISERFS_CHECK
    if ( flag == M_PASTE && tb->insert_size[0] ) {
	print_cur_tb ("12290");
	reiserfs_panic (tb->tb_sb, "PAP-12290: balance_leaf: insert_size is still not 0 (%d)", tb->insert_size[0]);
    }
#endif /* CONFIG_REISERFS_CHECK */

    return 0;
} /* Leaf level of the tree is balanced (end of balance_leaf) */



/* Make empty node */
void make_empty_node (struct buffer_info * bi)
{
    struct block_head * blkh;

#ifdef CONFIG_REISERFS_CHECK
    if (bi->bi_bh == NULL)
	reiserfs_panic (0, "PAP-12295: make_empty_node: pointer to the buffer is NULL");
#endif

    (blkh = B_BLK_HEAD(bi->bi_bh))->blk_nr_item = cpu_to_le16 (0);
    blkh->blk_free_space = cpu_to_le16 (MAX_CHILD_SIZE(bi->bi_bh));

    if (bi->bi_parent)
	B_N_CHILD (bi->bi_parent, bi->bi_position)->dc_size = 0; 
}


/* Get first empty buffer */
struct buffer_head * get_FEB (struct tree_balance * tb)
{
    int i;
    struct buffer_head * first_b;
    struct buffer_info bi;

    for (i = 0; i < MAX_FEB_SIZE; i ++)
	if (tb->FEB[i] != 0)
	    break;

    if (i == MAX_FEB_SIZE)
	reiserfs_panic(tb->tb_sb, "vs-12300: get_FEB: FEB list is empty");

    bi.tb = tb;
    bi.bi_bh = first_b = tb->FEB[i];
    bi.bi_parent = 0;
    bi.bi_position = 0;
    make_empty_node (&bi);
    set_bit(BH_Uptodate, &first_b->b_state);
    tb->FEB[i] = 0;
    tb->used[i] = first_b;

#ifdef REISERFS_FSCK
    mark_block_formatted (first_b->b_blocknr);
#endif

    return(first_b);
}


/* This is now used because reiserfs_free_block has to be able to
** schedule.
*/
static void store_thrown (struct tree_balance * tb, struct buffer_head * bh)
{
    int i;

    if (buffer_dirty (bh))
      printk ("store_thrown deals with dirty buffer\n");
    for (i = 0; i < sizeof (tb->thrown)/sizeof (tb->thrown[0]); i ++)
	if (!tb->thrown[i]) {
	    tb->thrown[i] = bh;
	    atomic_inc(&bh->b_count) ; /* decremented in free_thrown */
	    return;
	}
    reiserfs_warning ("store_thrown: too many thrown buffers\n");
}

static void free_thrown(struct tree_balance *tb) {
    int i ;
    unsigned long blocknr ;
    for (i = 0; i < sizeof (tb->thrown)/sizeof (tb->thrown[0]); i++) {
	if (tb->thrown[i]) {
	    blocknr = tb->thrown[i]->b_blocknr ;
	    if (buffer_dirty (tb->thrown[i]))
	      printk ("free_thrown deals with dirty buffer %ld\n", blocknr);
	    brelse(tb->thrown[i]) ; /* incremented in store_thrown */
	    reiserfs_free_block (tb->transaction_handle, blocknr);
	}
    }
}

void reiserfs_invalidate_buffer (struct tree_balance * tb, struct buffer_head * bh)
{
    B_BLK_HEAD (bh)->blk_level = cpu_to_le16 (FREE_LEVEL)/*0*/;
    B_BLK_HEAD (bh)->blk_nr_item = cpu_to_le16 (0);
    mark_buffer_clean (bh);
    /* reiserfs_free_block is no longer schedule safe 
    reiserfs_free_block (tb->transaction_handle, tb->tb_sb, bh->b_blocknr);
    */

    store_thrown (tb, bh);
#if 0
#ifdef REISERFS_FSCK
    {
	struct buffer_head * to_be_forgotten;
	
	to_be_forgotten = find_buffer (bh->b_dev, bh->b_blocknr, bh->b_size);
	if (to_be_forgotten) {
	    to_be_forgotten->b_count ++;
	    bforget (to_be_forgotten);
	}
	unmark_block_formatted (bh->b_blocknr);
    }
#endif
#endif
}

/* Replace n_dest'th key in buffer dest by n_src'th key of buffer src.*/
void replace_key (struct tree_balance * tb, struct buffer_head * dest, int n_dest,
		  struct buffer_head * src, int n_src)
{

#ifdef CONFIG_REISERFS_CHECK
    if (dest == NULL || src == NULL)
	reiserfs_panic (0, "vs-12305: replace_key: sourse or destination buffer is 0 (src=%p, dest=%p)", src, dest);

    if ( ! B_IS_KEYS_LEVEL (dest) )
	reiserfs_panic (0, "vs-12310: replace_key: invalid level (%z) for destination buffer. dest must be leaf",
			dest);

    if (n_dest < 0 || n_src < 0)
	reiserfs_panic (0, "vs-12315: replace_key: src(%d) or dest(%d) key number less than 0", n_src, n_dest);

    if (n_dest >= B_NR_ITEMS(dest) || n_src >= B_NR_ITEMS(src))
	reiserfs_panic (0, "vs-12320: replace_key: src(%d(%d)) or dest(%d(%d)) key number is too big",
			n_src, B_NR_ITEMS(src), n_dest, B_NR_ITEMS(dest));
#endif	/* CONFIG_REISERFS_CHECK */
   
    if (B_IS_ITEMS_LEVEL (src))
	/* source buffer contains leaf node */
	memcpy (B_N_PDELIM_KEY(dest,n_dest), B_N_PITEM_HEAD(src,n_src), KEY_SIZE);
    else
	memcpy (B_N_PDELIM_KEY(dest,n_dest), B_N_PDELIM_KEY(src,n_src), KEY_SIZE);

    do_balance_mark_internal_dirty (tb, dest, 0);
}


int get_left_neighbor_position (
				struct tree_balance * tb, 
				int h
				)
{
  int Sh_position = PATH_H_POSITION (tb->tb_path, h + 1);

#ifdef CONFIG_REISERFS_CHECK
  if (PATH_H_PPARENT (tb->tb_path, h) == 0 || tb->FL[h] == 0)
    reiserfs_panic (tb->tb_sb, "vs-12325: get_left_neighbor_position: FL[%d](%p) or F[%d](%p) does not exist", 
		    h, tb->FL[h], h, PATH_H_PPARENT (tb->tb_path, h));
#endif

  if (Sh_position == 0)
    return B_NR_ITEMS (tb->FL[h]);
  else
    return Sh_position - 1;
}


int get_right_neighbor_position (struct tree_balance * tb, int h)
{
  int Sh_position = PATH_H_POSITION (tb->tb_path, h + 1);

#ifdef CONFIG_REISERFS_CHECK
  if (PATH_H_PPARENT (tb->tb_path, h) == 0 || tb->FR[h] == 0)
    reiserfs_panic (tb->tb_sb, "vs-12330: get_right_neighbor_position: F[%d](%p) or FR[%d](%p) does not exist", 
		    h, PATH_H_PPARENT (tb->tb_path, h), h, tb->FR[h]);
#endif

  if (Sh_position == B_NR_ITEMS (PATH_H_PPARENT (tb->tb_path, h)))
    return 0;
  else
    return Sh_position + 1;
}


#ifdef CONFIG_REISERFS_CHECK

int is_reusable (struct super_block * s, unsigned long block, int bit_value);
static void check_internal_node (struct super_block * s, struct buffer_head * bh, char * mes)
{
  struct disk_child * dc;
  int i;

  if (!bh)
    reiserfs_panic (s, "PAP-12336: check_internal_node: bh == 0");

  if (!bh || !B_IS_IN_TREE (bh))
    return;
 
  if (!buffer_dirty (bh) && 
      !(buffer_journaled(bh) || buffer_journal_dirty(bh))) {
    reiserfs_panic (s, "PAP-12337: check_internal_node: buffer (%b) must be dirty", bh);
  }

  dc = B_N_CHILD (bh, 0);

  for (i = 0; i <= B_NR_ITEMS (bh); i ++, dc ++) {
    if (!is_reusable (s, dc->dc_block_number, 1) ) {
      print_cur_tb (mes);
      reiserfs_panic (s, "PAP-12338: check_internal_node: invalid child pointer %y in %b", dc, bh);
    }
  }
}


static int locked_or_not_in_tree (struct buffer_head * bh, char * which)
{
  if ( buffer_locked (bh) || !B_IS_IN_TREE (bh) ) {
    reiserfs_warning ("vs-12339: locked_or_not_in_tree: %s (%b)\n", which, bh);
    return 1;
  } 
  return 0;
}


static int check_before_balancing (struct tree_balance * tb)
{
  int retval = 0;	

  if ( cur_tb ) {
    reiserfs_panic (tb->tb_sb, "vs-12335: check_before_balancing: "
		    "suspect that schedule occurred based on cur_tb not being null at this point in code. "
		    "do_balance cannot properly handle schedule occuring while it runs.");
  }
  
  /* double check that buffers that we will modify are unlocked. (fix_nodes should already have
     prepped all of these for us). */
  if ( tb->lnum[0] ) {
    retval |= locked_or_not_in_tree (tb->L[0], "L[0]");
    retval |= locked_or_not_in_tree (tb->FL[0], "FL[0]");
    retval |= locked_or_not_in_tree (tb->CFL[0], "CFL[0]");
    check_leaf (tb->L[0]);
  }
  if ( tb->rnum[0] ) {
    retval |= locked_or_not_in_tree (tb->R[0], "R[0]");
    retval |= locked_or_not_in_tree (tb->FR[0], "FR[0]");
    retval |= locked_or_not_in_tree (tb->CFR[0], "CFR[0]");
    check_leaf (tb->R[0]);
  }
  retval |= locked_or_not_in_tree (PATH_PLAST_BUFFER (tb->tb_path), "S[0]");
  check_leaf (PATH_PLAST_BUFFER (tb->tb_path));

  return retval;
}


void check_after_balance_leaf (struct tree_balance * tb)
{
    if (tb->lnum[0]) {
	if (B_FREE_SPACE (tb->L[0]) != 
	    MAX_CHILD_SIZE (tb->L[0]) - B_N_CHILD (tb->FL[0], get_left_neighbor_position (tb, 0))->dc_size) {
	    print_cur_tb ("12221");
	    reiserfs_panic (tb->tb_sb, "PAP-12355: check_after_balance_leaf: shift to left was incorrect");
	}
    }
    if (tb->rnum[0]) {
	if (B_FREE_SPACE (tb->R[0]) != 
	    MAX_CHILD_SIZE (tb->R[0]) - B_N_CHILD (tb->FR[0], get_right_neighbor_position (tb, 0))->dc_size) {
	    print_cur_tb ("12222");
	    reiserfs_panic (tb->tb_sb, "PAP-12360: check_after_balance_leaf: shift to right was incorrect");
	}
    }
    if (PATH_H_PBUFFER(tb->tb_path,1) && (B_FREE_SPACE (PATH_H_PBUFFER(tb->tb_path,0)) != 
					  (MAX_CHILD_SIZE (PATH_H_PBUFFER(tb->tb_path,0)) -
					   B_N_CHILD (PATH_H_PBUFFER(tb->tb_path,1),
						      PATH_H_POSITION (tb->tb_path, 1))->dc_size))) {
	print_cur_tb ("12223");
	reiserfs_panic (tb->tb_sb, "PAP-12365: check_after_balance_leaf: S is incorrect");
    }
}


void check_leaf_level (struct tree_balance * tb)
{
  check_leaf (tb->L[0]);
  check_leaf (tb->R[0]);
  check_leaf (PATH_PLAST_BUFFER (tb->tb_path));
}

void check_internal_levels (struct tree_balance * tb)
{
  int h;

  /* check all internal nodes */
  for (h = 1; tb->insert_size[h]; h ++) {
    check_internal_node (tb->tb_sb, PATH_H_PBUFFER (tb->tb_path, h), "BAD BUFFER ON PATH");
    if (tb->lnum[h])
      check_internal_node (tb->tb_sb, tb->L[h], "BAD L");
    if (tb->rnum[h])
      check_internal_node (tb->tb_sb, tb->R[h], "BAD R");
  }

}

#endif






/* Now we have all of the buffers that must be used in balancing of
   the tree.  We rely on the assumption that schedule() will not occur
   while do_balance works. ( Only interrupt handlers are acceptable.)
   We balance the tree according to the analysis made before this,
   using buffers already obtained.  For SMP support it will someday be
   necessary to add ordered locking of tb. */

/* Some interesting rules of balancing:

   we delete a maximum of two nodes per level per balancing: we never
   delete R, when we delete two of three nodes L, S, R then we move
   them into R.

   we only delete L if we are deleting two nodes, if we delete only
   one node we delete S

   if we shift leaves then we shift as much as we can: this is a
   deliberate policy of extremism in node packing which results in
   higher average utilization after repeated random balance operations
   at the cost of more memory copies and more balancing as a result of
   small insertions to full nodes.

   if we shift internal nodes we try to evenly balance the node
   utilization, with consequent less balancing at the cost of lower
   utilization.

   one could argue that the policy for directories in leaves should be
   that of internal nodes, but we will wait until another day to
   evaluate this....  It would be nice to someday measure and prove
   these assumptions as to what is optimal....

*/

static inline void do_balance_starts (struct tree_balance *tb)
{
    /* use print_cur_tb() to see initial state of struct
       tree_balance */

    /* store_print_tb (tb); */

#ifdef CONFIG_REISERFS_CHECK

    /* do not delete, just comment it out */
/*    print_tb(flag, PATH_LAST_POSITION(tb->tb_path), tb->tb_path->pos_in_item, tb, 
	     "check");*/

    if (check_before_balancing (tb))
	reiserfs_panic (tb->tb_sb, "PAP-12340: do_balance: locked buffers in TB");

#ifndef __KERNEL__
    if ( atomic_read(&(PATH_PLAST_BUFFER(tb->tb_path)->b_count)) > 1 || (tb->L[0] && atomic_read(&(tb->L[0]->b_count)) > 1) ||
	 (tb->R[0] && atomic_read(&(tb->R[0]->b_count)) > 1) ) {
	print_cur_tb ("first three parameters are invalid");
	reiserfs_panic (tb->tb_sb, "PAP-12345: do_balance: counter too big");
    }
#endif /* !__KERNEL__ */
    cur_tb = tb;
    
#endif /* CONFIG_REISERFS_CHECK */
}


static inline void do_balance_completed (struct tree_balance * tb)
{
    
#ifdef CONFIG_REISERFS_CHECK
    check_leaf_level (tb);
    check_internal_levels (tb);
    cur_tb = NULL;
#endif

    /* reiserfs_free_block is no longer schedule safe.  So, we need to
    ** put the buffers we want freed on the thrown list during do_balance,
    ** and then free them now
    */

    tb->tb_sb->u.reiserfs_sb.s_do_balance ++;


    /* release all nodes hold to perform the balancing */
    unfix_nodes(tb);

    free_thrown(tb) ;
}





void do_balance (struct tree_balance * tb, /* tree_balance structure */
		 struct item_head * ih,	   /* item header of inserted item */
		 const char * body,  /* body  of inserted item or bytes to paste */
		 int flag)  /* i - insert, d - delete
			       c - cut, p - paste
						      
			       Cut means delete part of an item
			       (includes removing an entry from a
			       directory).
						      
			       Delete means delete whole item.
						      
			       Insert means add a new item into the
			       tree.
						      						      
			       Paste means to append to the end of an
			       existing file or to insert a directory
			       entry.  */
{
    int child_pos,					/* position of a child node in its parent */
	h;								/* level of the tree being processed */
    struct item_head insert_key[2]; /* in our processing of one level
				       we sometimes determine what
				       must be inserted into the next
				       higher level.  This insertion
				       consists of a key or two keys
				       and their corresponding
				       pointers */
    struct buffer_head *insert_ptr[2]; /* inserted node-ptrs for the next
					  level */

    tb->tb_mode = flag;
    tb->need_balance_dirty = 0;

    if (FILESYSTEM_CHANGED_TB(tb)) {
        reiserfs_panic(tb->tb_sb, "clm-6000: do_balance, fs generation has changed\n") ;
    }
    /* if we have no real work to do  */
    if ( ! tb->insert_size[0] ) {
	reiserfs_warning ("PAP-12350: do_balance: insert_size == 0, mode == %c",
			  flag);
	unfix_nodes(tb);
	return;
    }

    atomic_inc (&(fs_generation (tb->tb_sb)));
    do_balance_starts (tb);
    
#ifdef REISERFS_FSCK
    if (flag == M_INTERNAL) {
	insert_ptr[0] = (struct buffer_head *)body;
	/* we must prepare insert_key */

	if (PATH_H_B_ITEM_ORDER (tb->tb_path, 0)/*LAST_POSITION (tb->tb_path)*//*item_pos*/ == -1) {
		/* get delimiting key from buffer in tree */
		copy_key (&insert_key[0].ih_key, B_N_PKEY (PATH_PLAST_BUFFER (tb->tb_path), 0));
		/*insert_ptr[0]->b_item_order = 0;*/
	} else {
	    /* get delimiting key from new buffer */
	    copy_key (&insert_key[0].ih_key, B_N_PKEY((struct buffer_head *)body,0));
	    /*insert_ptr[0]->b_item_order = item_pos;*/
	}
      
	/* and insert_ptr instead of balance_leaf */
	child_pos = PATH_H_B_ITEM_ORDER (tb->tb_path, 0)/*item_pos*/;
    } else
#endif

	/* balance leaf returns 0 except if combining L R and S into
	   one node.  see balance_internal() for explanation of this
	   line of code.*/
	child_pos = PATH_H_B_ITEM_ORDER (tb->tb_path, 0) +
	  balance_leaf (tb, ih, body, flag, insert_key, insert_ptr);

#ifdef CONFIG_REISERFS_CHECK
    check_after_balance_leaf (tb);
#endif

    /* Balance internal level of the tree. */
    for ( h = 1; h < MAX_HEIGHT && tb->insert_size[h]; h++ )
	child_pos = balance_internal (tb, h, child_pos, insert_key, insert_ptr);


    do_balance_completed (tb);

}
