/*
 *  linux/fs/xip2fs/file.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include <linux/time.h>
#include "xip2.h"
#include "xattr.h"
#include "acl.h"
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

void xip2_do_file_read(struct file *filp, loff_t *ppos, read_descriptor_t *desc)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	sector_t blockno;
	unsigned long block,offset,rdlen,count, iblock, lblock;
	void* block_ptr,* cpystart;
	int error,cpycount;

	if  (*ppos > inode->i_size)
		return;
	iblock = (*ppos)/PAGE_SIZE;
	offset = (*ppos)%PAGE_SIZE;
	rdlen = desc->count;
	if ((*ppos)+desc->count > inode->i_size)
		rdlen = inode->i_size - (*ppos);
	lblock = (*ppos + rdlen) / PAGE_SIZE;
	count = 0;
	for (block = iblock; block <= lblock; block++) {
		error=xip2_get_block(inode, block, &blockno, 0);
		if (error) {
			desc->error = error;
			desc->written = count;
			return;
		}
		block_ptr = xip2_sb_bread (inode->i_sb, blockno);
		if (block_ptr) {
			if (block == iblock) {
				cpystart = block_ptr + offset;
				cpycount = PAGE_SIZE - offset;
			} else {
				cpystart = block_ptr;
				cpycount = PAGE_SIZE;
			}
		} else {
			// there is no block assigned, copy zeros over
			if (block == iblock) {
				cpystart = empty_zero_page;
				cpycount = PAGE_SIZE - offset;
			} else {
				cpystart = empty_zero_page;
				cpycount = PAGE_SIZE;
			}
		}
		if (cpycount > rdlen-count) {
			cpycount = rdlen-count;
			if (block!=lblock) BUG();
		}
		if (copy_to_user(desc->buf+count, cpystart, cpycount)) {
			desc->error = -EFAULT;
			desc->written = count;
			return;
		}
		count += cpycount;
	}
	if (rdlen-count>0) BUG();
	desc->error = 0;
	desc->written = count;
	*ppos+=count;
	return;
}


ssize_t
__xip2_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t *ppos)
{
	struct file *filp = iocb->ki_filp;
	ssize_t retval;
	unsigned long seg;
	size_t count;

	count = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		count += iv->iov_len;
		if (unlikely((ssize_t)(count|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_WRITE, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		nr_segs = seg;
		count -= iv->iov_len;	/* This segment is no good */
		break;
	}

	retval = 0;
	if (count) {
		for (seg = 0; seg < nr_segs; seg++) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.buf = iov[seg].iov_base;
			desc.count = iov[seg].iov_len;
			if (desc.count == 0)
				continue;
			desc.error = 0;
			xip2_do_file_read(filp,ppos,&desc);
			retval += desc.written;
			if (!retval) {
				retval = desc.error;
				break;
			}
		}
	}
	return retval;
}

ssize_t
xip2_file_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct iovec local_iov = { .iov_base = buf, .iov_len = count };
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = __xip2_file_aio_read(&kiocb, &local_iov, 1, ppos);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	return ret;
}


ssize_t
xip2_file_aio_read(struct kiocb *iocb, char __user *buf, size_t count,
		   loff_t pos)
{
	struct iovec local_iov = { .iov_base = buf, .iov_len = count };

	BUG_ON(iocb->ki_pos != pos);
	return __xip2_file_aio_read(iocb, &local_iov, 1, &iocb->ki_pos);
}

ssize_t xip2_file_readv(struct file *filp, const struct iovec *iov,
			unsigned long nr_segs, loff_t *ppos)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = __xip2_file_aio_read(&kiocb, iov, nr_segs, ppos);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&kiocb);
	return ret;
}

struct page * xip2_nopage_in_place(struct vm_area_struct * area,
				   unsigned long address, int* type)
{
	int error;
#ifdef CONFIG_LBD
	sector_t blockno = ~0ULL;
#else
	sector_t blockno = ~0UL;
#endif
	void* block_ptr;
	struct file *file = area->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long pgoff;

	pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) +
		area->vm_pgoff;
	error=xip2_get_block(inode, pgoff, &blockno, 0);
	if (error) {
		printk ("XIP2-FS: xip2_nopage_in_place could not fullfill "
			"page request\n");
		return NULL;
	}
	block_ptr = xip2_sb_bread(inode->i_sb, blockno);
	if (!block_ptr)
		return virt_to_page(empty_zero_page);
	return virt_to_page(block_ptr);
}

static struct vm_operations_struct xip2_file_vm_ops = {
	.nopage		= xip2_nopage_in_place,
//	.populate	= filemap_populate,
};


int xip2_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	file_accessed(file);
	vma->vm_ops = &xip2_file_vm_ops;
	return 0;
}

void xip2_do_file_sendfile(struct file *filp, loff_t *ppos,
			   read_descriptor_t *desc, read_actor_t actor)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	sector_t blockno;
	unsigned long block,offset,rdlen,count, iblock, lblock;
	void* block_ptr;
	struct page *cpypage;
	int error,cpycount,cpyoffset,copied;
	unsigned long actor_ret;

	if  (*ppos > inode->i_size)
		return;
	iblock = (*ppos)/PAGE_SIZE;
	offset = (*ppos)%PAGE_SIZE;
	rdlen = desc->count;
	if ((*ppos)+desc->count > inode->i_size)
		rdlen = inode->i_size - (*ppos);
	lblock = (*ppos + rdlen) / PAGE_SIZE;
	count = 0;
	for (block = iblock; block <= lblock; block++) {
		error=xip2_get_block(inode, block, &blockno, 0);
		if (error) {
			desc->error = error;
			desc->written = count;
			return;
		}
		block_ptr = xip2_sb_bread (inode->i_sb, blockno);
		if (block_ptr) {
			if (block == iblock) {
				cpypage  = virt_to_page (block_ptr);
				cpyoffset= offset;
				cpycount = PAGE_SIZE - offset;
			} else {
				cpypage  = virt_to_page (block_ptr);
				cpyoffset= 0;
				cpycount = PAGE_SIZE;
			}
		} else {
			// there is no block assigned, copy zeros over
			if (block == iblock) {
				cpypage  = virt_to_page (empty_zero_page);
				cpyoffset= 0;
				cpycount = PAGE_SIZE - offset;
			} else {
				cpypage  = virt_to_page (empty_zero_page);
				cpyoffset= 0;
				cpycount = PAGE_SIZE;
			}
		}
		if (cpycount > rdlen-count) {
			cpycount = rdlen-count;
			if (block!=lblock) BUG();
		}
		copied = 0;
		while (copied < cpycount) {
			actor_ret = actor(desc, cpypage, cpyoffset+copied,
					  cpycount-copied);
			if (desc->error)
				return;
			copied += actor_ret;
		}
		count += cpycount;
	}
	if (rdlen-count>0) BUG();
	desc->error = 0;
	desc->written = count;
	*ppos+=count;
	return;
}


ssize_t xip2_file_sendfile(struct file *in_file, loff_t *ppos,
			 size_t count, read_actor_t actor, void __user *target)
{
	read_descriptor_t desc;

	if (!count)
		return 0;

	desc.written = 0;
	desc.count = count;
	desc.buf = target;
	desc.error = 0;

	xip2_do_file_sendfile(in_file, ppos, &desc, actor);
	if (desc.written)
		return desc.written;
	return desc.error;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 */
struct file_operations xip2_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= xip2_file_read,
	.aio_read	= xip2_file_aio_read,
	.ioctl		= xip2_ioctl,
	.mmap		= xip2_file_mmap,
	.open		= generic_file_open,
	.readv		= xip2_file_readv,
	.sendfile	= xip2_file_sendfile,
};

struct inode_operations xip2_file_inode_operations = {
	.getxattr	= xip2_getxattr,
	.listxattr	= xip2_listxattr,
	.permission	= xip2_permission,
};
