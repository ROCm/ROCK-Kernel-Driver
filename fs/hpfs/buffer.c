/*
 *  linux/fs/hpfs/buffer.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  general buffer i/o
 */

#include <linux/buffer_head.h>
#include <linux/string.h>
#include "hpfs_fn.h"

void hpfs_lock_creation(struct super_block *s)
{
#ifdef DEBUG_LOCKS
	printk("lock creation\n");
#endif
	down(&hpfs_sb(s)->hpfs_creation_de);
}

void hpfs_unlock_creation(struct super_block *s)
{
#ifdef DEBUG_LOCKS
	printk("unlock creation\n");
#endif
	up(&hpfs_sb(s)->hpfs_creation_de);
}

void hpfs_lock_iget(struct super_block *s, int mode)
{
#ifdef DEBUG_LOCKS
	printk("lock iget\n");
#endif
	while (hpfs_sb(s)->sb_rd_inode) sleep_on(&hpfs_sb(s)->sb_iget_q);
	hpfs_sb(s)->sb_rd_inode = mode;
}

void hpfs_unlock_iget(struct super_block *s)
{
#ifdef DEBUG_LOCKS
	printk("unlock iget\n");
#endif
	hpfs_sb(s)->sb_rd_inode = 0;
	wake_up(&hpfs_sb(s)->sb_iget_q);
}

void hpfs_lock_inode(struct inode *i)
{
	if (i) {
		struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
		down(&hpfs_inode->i_sem);
	}
}

void hpfs_unlock_inode(struct inode *i)
{
	if (i) {
		struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
		up(&hpfs_inode->i_sem);
	}
}

void hpfs_lock_2inodes(struct inode *i1, struct inode *i2)
{
	struct hpfs_inode_info *hpfs_i1 = NULL, *hpfs_i2 = NULL;

	if (!i1) {
		if (i2) {
			hpfs_i2 = hpfs_i(i2);
			down(&hpfs_i2->i_sem);
		}
		return;
	}
	if (!i2) {
		if (i1) {
			hpfs_i1 = hpfs_i(i1);
			down(&hpfs_i1->i_sem);
		}
		return;
	}
	if (i1->i_ino < i2->i_ino) {
		down(&hpfs_i1->i_sem);
		down(&hpfs_i2->i_sem);
	} else if (i1->i_ino > i2->i_ino) {
		down(&hpfs_i2->i_sem);
		down(&hpfs_i1->i_sem);
	} else down(&hpfs_i1->i_sem);
}

void hpfs_unlock_2inodes(struct inode *i1, struct inode *i2)
{
	struct hpfs_inode_info *hpfs_i1 = NULL, *hpfs_i2 = NULL;

	if (!i1) {
		if (i2) {
			hpfs_i2 = hpfs_i(i2);
			up(&hpfs_i2->i_sem);
		}
		return;
	}
	if (!i2) {
		if (i1) {
			hpfs_i1 = hpfs_i(i1);
			up(&hpfs_i1->i_sem);
		}
		return;
	}
	if (i1->i_ino < i2->i_ino) {
		up(&hpfs_i2->i_sem);
		up(&hpfs_i1->i_sem);
	} else if (i1->i_ino > i2->i_ino) {
		up(&hpfs_i1->i_sem);
		up(&hpfs_i2->i_sem);
	} else up(&hpfs_i1->i_sem);
}

void hpfs_lock_3inodes(struct inode *i1, struct inode *i2, struct inode *i3)
{
	if (!i1) { hpfs_lock_2inodes(i2, i3); return; }
	if (!i2) { hpfs_lock_2inodes(i1, i3); return; }
	if (!i3) { hpfs_lock_2inodes(i1, i2); return; }
	if (i1->i_ino < i2->i_ino && i1->i_ino < i3->i_ino) {
		struct hpfs_inode_info *hpfs_i1 = hpfs_i(i1);
		down(&hpfs_i1->i_sem);
		hpfs_lock_2inodes(i2, i3);
	} else if (i2->i_ino < i1->i_ino && i2->i_ino < i3->i_ino) {
		struct hpfs_inode_info *hpfs_i2 = hpfs_i(i2);
		down(&hpfs_i2->i_sem);
		hpfs_lock_2inodes(i1, i3);
	} else if (i3->i_ino < i1->i_ino && i3->i_ino < i2->i_ino) {
		struct hpfs_inode_info *hpfs_i3 = hpfs_i(i3);
		down(&hpfs_i3->i_sem);
		hpfs_lock_2inodes(i1, i2);
	} else if (i1->i_ino != i2->i_ino) hpfs_lock_2inodes(i1, i2);
	else hpfs_lock_2inodes(i1, i3);
}
		
void hpfs_unlock_3inodes(struct inode *i1, struct inode *i2, struct inode *i3)
{
	if (!i1) { hpfs_unlock_2inodes(i2, i3); return; }
	if (!i2) { hpfs_unlock_2inodes(i1, i3); return; }
	if (!i3) { hpfs_unlock_2inodes(i1, i2); return; }
	if (i1->i_ino < i2->i_ino && i1->i_ino < i3->i_ino) {
		struct hpfs_inode_info *hpfs_i1 = hpfs_i(i1);
		hpfs_unlock_2inodes(i2, i3);
		up(&hpfs_i1->i_sem);
	} else if (i2->i_ino < i1->i_ino && i2->i_ino < i3->i_ino) {
		struct hpfs_inode_info *hpfs_i2 = hpfs_i(i2);
		hpfs_unlock_2inodes(i1, i3);
		up(&hpfs_i2->i_sem);
	} else if (i3->i_ino < i1->i_ino && i3->i_ino < i2->i_ino) {
		struct hpfs_inode_info *hpfs_i3 = hpfs_i(i3);
		hpfs_unlock_2inodes(i1, i2);
		up(&hpfs_i3->i_sem);
	} else if (i1->i_ino != i2->i_ino) hpfs_unlock_2inodes(i1, i2);
	else hpfs_unlock_2inodes(i1, i3);
}

/* Map a sector into a buffer and return pointers to it and to the buffer. */

void *hpfs_map_sector(struct super_block *s, unsigned secno, struct buffer_head **bhp,
		 int ahead)
{
	struct buffer_head *bh;

	*bhp = bh = sb_bread(s, secno);
	if (bh != NULL)
		return bh->b_data;
	else {
		printk("HPFS: hpfs_map_sector: read error\n");
		return NULL;
	}
}

/* Like hpfs_map_sector but don't read anything */

void *hpfs_get_sector(struct super_block *s, unsigned secno, struct buffer_head **bhp)
{
	struct buffer_head *bh;
	/*return hpfs_map_sector(s, secno, bhp, 0);*/

	if ((*bhp = bh = sb_getblk(s, secno)) != NULL) {
		if (!buffer_uptodate(bh)) wait_on_buffer(bh);
		set_buffer_uptodate(bh);
		return bh->b_data;
	} else {
		printk("HPFS: hpfs_get_sector: getblk failed\n");
		return NULL;
	}
}

/* Map 4 sectors into a 4buffer and return pointers to it and to the buffer. */

void *hpfs_map_4sectors(struct super_block *s, unsigned secno, struct quad_buffer_head *qbh,
		   int ahead)
{
	struct buffer_head *bh;
	char *data;

	if (secno & 3) {
		printk("HPFS: hpfs_map_4sectors: unaligned read\n");
		return 0;
	}

	qbh->data = data = (char *)kmalloc(2048, GFP_KERNEL);
	if (!data) {
		printk("HPFS: hpfs_map_4sectors: out of memory\n");
		goto bail;
	}

	qbh->bh[0] = bh = sb_bread(s, secno);
	if (!bh)
		goto bail0;
	memcpy(data, bh->b_data, 512);

	qbh->bh[1] = bh = sb_bread(s, secno + 1);
	if (!bh)
		goto bail1;
	memcpy(data + 512, bh->b_data, 512);

	qbh->bh[2] = bh = sb_bread(s, secno + 2);
	if (!bh)
		goto bail2;
	memcpy(data + 2 * 512, bh->b_data, 512);

	qbh->bh[3] = bh = sb_bread(s, secno + 3);
	if (!bh)
		goto bail3;
	memcpy(data + 3 * 512, bh->b_data, 512);

	return data;

 bail3:
	brelse(qbh->bh[2]);
 bail2:
	brelse(qbh->bh[1]);
 bail1:
	brelse(qbh->bh[0]);
 bail0:
	kfree(data);
	printk("HPFS: hpfs_map_4sectors: read error\n");
 bail:
	return NULL;
}

/* Don't read sectors */

void *hpfs_get_4sectors(struct super_block *s, unsigned secno,
                          struct quad_buffer_head *qbh)
{
	if (secno & 3) {
		printk("HPFS: hpfs_get_4sectors: unaligned read\n");
		return 0;
	}

	/*return hpfs_map_4sectors(s, secno, qbh, 0);*/
	if (!(qbh->data = kmalloc(2048, GFP_KERNEL))) {
		printk("HPFS: hpfs_get_4sectors: out of memory\n");
		return NULL;
	}
	if (!(hpfs_get_sector(s, secno, &qbh->bh[0]))) goto bail0;
	if (!(hpfs_get_sector(s, secno + 1, &qbh->bh[1]))) goto bail1;
	if (!(hpfs_get_sector(s, secno + 2, &qbh->bh[2]))) goto bail2;
	if (!(hpfs_get_sector(s, secno + 3, &qbh->bh[3]))) goto bail3;
	memcpy(qbh->data, qbh->bh[0]->b_data, 512);
	memcpy(qbh->data + 512, qbh->bh[1]->b_data, 512);
	memcpy(qbh->data + 2*512, qbh->bh[2]->b_data, 512);
	memcpy(qbh->data + 3*512, qbh->bh[3]->b_data, 512);
	return qbh->data;

	bail3:	brelse(qbh->bh[2]);
	bail2:	brelse(qbh->bh[1]);
	bail1:	brelse(qbh->bh[0]);
	bail0:
	return NULL;
}
	

void hpfs_brelse4(struct quad_buffer_head *qbh)
{
	brelse(qbh->bh[3]);
	brelse(qbh->bh[2]);
	brelse(qbh->bh[1]);
	brelse(qbh->bh[0]);
	kfree(qbh->data);
}	

void hpfs_mark_4buffers_dirty(struct quad_buffer_head *qbh)
{
	PRINTK(("hpfs_mark_4buffers_dirty\n"));
	memcpy(qbh->bh[0]->b_data, qbh->data, 512);
	memcpy(qbh->bh[1]->b_data, qbh->data + 512, 512);
	memcpy(qbh->bh[2]->b_data, qbh->data + 2 * 512, 512);
	memcpy(qbh->bh[3]->b_data, qbh->data + 3 * 512, 512);
	mark_buffer_dirty(qbh->bh[0]);
	mark_buffer_dirty(qbh->bh[1]);
	mark_buffer_dirty(qbh->bh[2]);
	mark_buffer_dirty(qbh->bh[3]);
}
