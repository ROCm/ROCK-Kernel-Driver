/*
 * Target device file I/O.
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/parser.h>

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "iotype.h"

struct fileio_data {
	char *path;
	struct file *filp;
};

static int fileio_make_request(struct iet_volume *lu, struct tio *tio, int rw)
{
	struct fileio_data *p = (struct fileio_data *) lu->private;
	struct file *filp;
	mm_segment_t oldfs;
	struct page *page;
	u32 offset, size;
	loff_t ppos, count;
	char *buf;
	int i, err = 0;
	ssize_t ret;

	assert(p);
	filp = p->filp;
	size = tio->size;
	offset= tio->offset;

	ppos = (loff_t) tio->idx << PAGE_CACHE_SHIFT;
	ppos += offset;

	for (i = 0; i < tio->pg_cnt; i++) {
		page = tio->pvec[i];
		assert(page);
		buf = page_address(page);
		buf += offset;

		if (offset + size > PAGE_CACHE_SIZE)
			count = PAGE_CACHE_SIZE - offset;
		else
			count = size;

		oldfs = get_fs();
		set_fs(get_ds());

		if (rw == READ)
			ret = generic_file_read(filp, buf, count, &ppos);
		else
			ret = generic_file_write(filp, buf, count, &ppos);

		set_fs(oldfs);

		if (ret != count) {
			eprintk("I/O error %lld, %ld\n", count, (long) ret);
			err = -EIO;
		}

		size -= count;
		offset = 0;
	}
	assert(!size);

	return err;
}

static int fileio_sync(struct iet_volume *lu, struct tio *tio)
{
	struct fileio_data *p = (struct fileio_data *) lu->private;
	struct inode *inode;
	loff_t ppos = (loff_t) tio->idx << PAGE_CACHE_SHIFT;
	ssize_t res;

	assert(p);
	inode = p->filp->f_dentry->d_inode;

	res = sync_page_range(inode, inode->i_mapping, ppos, (size_t) tio->size);

	return 0;
}

static int open_path(struct iet_volume *volume, const char *path)
{
	int err = 0;
	struct fileio_data *info = (struct fileio_data *) volume->private;
	struct file *filp;
	mm_segment_t oldfs;

	info->path = kmalloc(strlen(path) + 1, GFP_KERNEL);
	if (!info->path)
		return -ENOMEM;
	strcpy(info->path, path);
	info->path[strlen(path)] = '\0';

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, O_RDWR|O_LARGEFILE, 0);
	set_fs(oldfs);

	if (IS_ERR(filp)) {
		err = PTR_ERR(filp);
		eprintk("Can't open %s %d\n", path, err);
		info->filp = NULL;
	} else
		info->filp = filp;

	return err;
}

static int set_scsiid(struct iet_volume *volume, const char *id)
{
	size_t len;

	if ((len = strlen(id)) > SCSI_ID_LEN - VENDOR_ID_LEN) {
		eprintk("too long SCSI ID %lu\n", (unsigned long) len);
		return -EINVAL;
	}

	len = min(sizeof(volume->scsi_id) - VENDOR_ID_LEN, len);
	memcpy(volume->scsi_id + VENDOR_ID_LEN, id, len);

	return 0;
}

static void gen_scsiid(struct iet_volume *volume, struct inode *inode)
{
	int i;
	u32 *p;

	strlcpy(volume->scsi_id, VENDOR_ID, VENDOR_ID_LEN);

	for (i = VENDOR_ID_LEN; i < SCSI_ID_LEN; i++)
		if (volume->scsi_id[i])
			return;

	p = (u32 *) (volume->scsi_id + VENDOR_ID_LEN);
	*(p + 0) = volume->target->trgt_param.target_type;
	*(p + 1) = volume->target->tid;
	*(p + 2) = (unsigned int) inode->i_ino;
	*(p + 3) = (unsigned int) inode->i_sb->s_dev;
}

enum {
	Opt_scsiid, Opt_path, Opt_ignore, Opt_err,
};

static match_table_t tokens = {
	{Opt_scsiid, "ScsiId=%s"},
	{Opt_path, "Path=%s"},
	{Opt_ignore, "Type=%s"},
	{Opt_err, NULL},
};

static int parse_fileio_params(struct iet_volume *volume, char *params)
{
	int err = 0;
	char *p, *q;

	while ((p = strsep(&params, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_scsiid:
			if (!(q = match_strdup(&args[0]))) {
				err = -ENOMEM;
				goto out;
			}
			err = set_scsiid(volume, q);
			kfree(q);
			if (err < 0)
				goto out;
			break;
		case Opt_path:
			if (!(q = match_strdup(&args[0]))) {
				err = -ENOMEM;
				goto out;
			}
			err = open_path(volume, q);
			kfree(q);
			if (err < 0)
				goto out;
			break;
		case Opt_ignore:
			break;
		default:
			eprintk("Unknown %s\n", p);
			return -EINVAL;
		}
	}

out:
	return err;
}

static void fileio_detach(struct iet_volume *lu)
{
	struct fileio_data *p = (struct fileio_data *) lu->private;

	kfree(p->path);
	if (p->filp)
		filp_close(p->filp, NULL);
	kfree(p);
	lu->private = NULL;
}

static int fileio_attach(struct iet_volume *lu, char *args)
{
	int err = 0;
	struct fileio_data *p;
	struct inode *inode;

	if (lu->private) {
		printk("already attached ? %d\n", lu->lun);
		return -EBUSY;
	}

	if (!(p = kmalloc(sizeof(*p), GFP_KERNEL)))
		return -ENOMEM;
	memset(p, 0, sizeof(*p));
	lu->private = p;

	if ((err = parse_fileio_params(lu, args)) < 0) {
		eprintk("%d\n", err);
		goto out;
	}
	inode = p->filp->f_dentry->d_inode;

	gen_scsiid(lu, inode);

	if (S_ISREG(inode->i_mode))
		;
	else if (S_ISBLK(inode->i_mode))
		inode = inode->i_bdev->bd_inode;
	else {
		err = -EINVAL;
		goto out;
	}

	lu->blk_shift = SECTOR_SIZE_BITS;
	lu->blk_cnt = inode->i_size >> lu->blk_shift;

out:
	if (err < 0)
		fileio_detach(lu);
	return err;
}

void fileio_show(struct iet_volume *lu, struct seq_file *seq)
{
	struct fileio_data *p = (struct fileio_data *) lu->private;
	seq_printf(seq, " path:%s\n", p->path);
}

struct iotype fileio =
{
	.name = "fileio",
	.attach = fileio_attach,
	.make_request = fileio_make_request,
	.sync = fileio_sync,
	.detach = fileio_detach,
	.show = fileio_show,
};
