/*
 *   fs/cifs/file.c
 *
 *   vfs operations that deal with files
 *
 */
ssize_t cifs_write(struct file * file, const char *write_data,
	   size_t write_size, loff_t * poffset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;
	struct cifsFileInfo * open_file;

	xid = GetXid();

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	/*cFYI(1,
	   (" write %d bytes to offset %lld of %s", write_size,
	   *poffset, file->f_dentry->d_name.name)); */

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *) file->private_data;


	if (*poffset > file->f_dentry->d_inode->i_size)
		long_op = 2;  /* writes past end of file can take a long time */
	else
		long_op = 1;

	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while(rc == -EAGAIN) {
			if ((open_file->invalidHandle) && (!open_file->closePend)) {
				rc = cifs_reopen_file(file->f_dentry->d_inode,file);
				if(rc != 0)
					break;
			}

			rc = CIFSSMBWrite(xid, pTcon,
				   open_file->netfid,
				  write_size - total_written, *poffset,
				  &bytes_written,
				  write_data + total_written, long_op);
		}
		if (rc || (bytes_written == 0)) {
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else
			*poffset += bytes_written;
		long_op = FALSE; /* subsequent writes fast - 15 seconds is plenty */
	}
	file->f_dentry->d_inode->i_ctime = file->f_dentry->d_inode->i_mtime =
		CURRENT_TIME;
	if (bytes_written > 0) {
		if (*poffset > file->f_dentry->d_inode->i_size)
			i_size_write(file->f_dentry->d_inode, *poffset);
	}
	mark_inode_dirty_sync(file->f_dentry->d_inode);
	FreeXid(xid);
	return total_written;
}

static int
cifs_partialpagewrite(struct page *page,unsigned from, unsigned to)
{
	struct address_space *mapping = page->mapping;
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char * write_data;
	int rc = -EFAULT;
	int bytes_written = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct inode *inode = page->mapping->host;
	struct cifsInodeInfo *cifsInode;
	struct cifsFileInfo *open_file = NULL;
	struct list_head *tmp;
	struct list_head *tmp1;

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	/* figure out which file struct to use 
	if (file->private_data == NULL) {
		return -EBADF;
	}     
	 */
	if (!mapping) {
		return -EFAULT;
	} else if(!mapping->host) {
		return -EFAULT;
	}

	offset += (loff_t)from;
	write_data = kmap(page);
	write_data += from;

	if((to > PAGE_CACHE_SIZE) || (from > to)) {
		kunmap(page);
		return -EIO;
	}

	/* racing with truncate? */
	if(offset > mapping->host->i_size) {
		kunmap(page);
		return 0; /* don't care */
	}

	/* check to make sure that we are not extending the file */
	if(mapping->host->i_size - offset < (loff_t)to)
		to = (unsigned)(mapping->host->i_size - offset); 
		

	cifsInode = CIFS_I(mapping->host);
	read_lock(&GlobalSMBSeslock); 
	list_for_each_safe(tmp, tmp1, &cifsInode->openFileList) {            
		open_file = list_entry(tmp,struct cifsFileInfo, flist);
		/* We check if file is open for writing first */
		if((open_file->pfile) && 
		   ((open_file->pfile->f_flags & O_RDWR) || 
			(open_file->pfile->f_flags & O_WRONLY))) {
			read_unlock(&GlobalSMBSeslock);
			bytes_written = cifs_write(open_file->pfile, write_data,
					to-from, &offset);
			read_lock(&GlobalSMBSeslock);
		/* Does mm or vfs already set times? */
			inode->i_atime = inode->i_mtime = CURRENT_TIME;
			if ((bytes_written > 0) && (offset)) {

				rc = 0;
			} else if(bytes_written < 0) {
				rc = bytes_written;
			}
			break;  /* now that we found a valid file handle
				and tried to write to it we are done, no
				sense continuing to loop looking for another */
		}
		if(tmp->next == NULL) {
			cFYI(1,("File instance %p removed",tmp));
			break;
		}
	}
	read_unlock(&GlobalSMBSeslock);
	if(open_file == NULL) {
		cFYI(1,("No writeable filehandles for inode"));
		rc = -EIO;
	}

	kunmap(page);
	return rc;
}

static int
cifs_writepage(struct page* page, struct writeback_control *wbc)
{
	int rc = -EFAULT;
	int xid;

	xid = GetXid();
/* BB add check for wbc flags */
	page_cache_get(page);
	
	rc = cifs_partialpagewrite(page,0,PAGE_CACHE_SIZE);
	SetPageUptodate(page); /* BB add check for error and Clearuptodate? */
	unlock_page(page);
	page_cache_release(page);	
	FreeXid(xid);
	return rc;
}

static int
cifs_commit_write(struct file *file, struct page *page, unsigned offset,
		  unsigned to)
{
	int xid;
	int rc = 0;
	struct inode *inode = page->mapping->host;
	loff_t position = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;
	/* struct cifsFileInfo *open_file;
	struct cifs_sb_info *cifs_sb; */

	xid = GetXid();
	cFYI(1,("commit write for page %p up to position %lld for %d",page,position,to));
	if (position > inode->i_size){
		i_size_write(inode, position);
		/*if (file->private_data == NULL) {
			rc = -EBADF;
		} else {
			open_file = (struct cifsFileInfo *)file->private_data;
			cifs_sb = CIFS_SB(inode->i_sb);
			rc = -EAGAIN;
			while(rc == -EAGAIN) {
				if((open_file->invalidHandle) && 
				  (!open_file->closePend)) {
					rc = cifs_reopen_file(file->f_dentry->d_inode,file);
					if(rc != 0)
						break;
				}
				if(!open_file->closePend) {
					rc = CIFSSMBSetFileSize(xid, cifs_sb->tcon, 
						position, open_file->netfid,
						open_file->pid,FALSE);
				} else {
					rc = -EBADF;
					break;
				}
			}
			cFYI(1,(" SetEOF (commit write) rc = %d",rc));
		}*/
	}
	set_page_dirty(page);

	FreeXid(xid);
	return rc;
}

ssize_t
cifs_read(struct file * file, char *read_data, size_t read_size,
	  loff_t * poffset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read;
	unsigned int current_read_size;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	char * current_offset;
	struct cifsFileInfo * open_file;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;

	for (total_read = 0,current_offset=read_data; read_size > total_read;
				total_read += bytes_read,current_offset+=bytes_read) {
		current_read_size = min_t(const int,read_size - total_read,cifs_sb->rsize);
		rc = -EAGAIN;
		while(rc == -EAGAIN) {
			if ((open_file->invalidHandle) && (!open_file->closePend)) {
				rc = cifs_reopen_file(file->f_dentry->d_inode,file);
				if(rc != 0)
					break;
			}

			rc = CIFSSMBRead(xid, pTcon,
				 open_file->netfid,
				 current_read_size, *poffset,
				 &bytes_read, &current_offset);
		}
		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else {
			*poffset += bytes_read;
		}
	}

	FreeXid(xid);
	return total_read;
}

static void cifs_copy_cache_pages(struct address_space *mapping, 
		struct list_head *pages, int bytes_read, 
		char *data,struct pagevec * plru_pvec)
{
	struct page *page;
	char * target;

	while (bytes_read > 0) {
		if(list_empty(pages))
			break;

		page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);

		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			cFYI(1,("Add page cache failed"));
			continue;
		}

		page_cache_get(page);
		target = kmap_atomic(page,KM_USER0);

		if(PAGE_CACHE_SIZE > bytes_read) {
			memcpy(target,data,bytes_read);
			/* zero the tail end of this partial page */
			memset(target+bytes_read,0,PAGE_CACHE_SIZE-bytes_read);
			bytes_read = 0;
		} else {
			memcpy(target,data,PAGE_CACHE_SIZE);
			bytes_read -= PAGE_CACHE_SIZE;
		}
		kunmap_atomic(target,KM_USER0);

		if (!pagevec_add(plru_pvec, page))
			__pagevec_lru_add(plru_pvec);
		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);   /* BB verify we need to unlock here */
	   /* page_cache_release(page);*/
		data += PAGE_CACHE_SIZE;
	}
	return;
}

static int
cifs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *page_list, unsigned num_pages)
{
	int rc = -EACCES;
	int xid;
	loff_t offset;
	struct page * page;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int bytes_read = 0;
	unsigned int read_size,i;
	char * smb_read_data = 0;
	struct smb_com_read_rsp * pSMBr;
	struct pagevec lru_pvec;
	struct cifsFileInfo * open_file;

	xid = GetXid();
	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	pagevec_init(&lru_pvec, 0);

	for(i = 0;i<num_pages;) {
		unsigned contig_pages;
		struct page * tmp_page;
		unsigned long expected_index;

		if(list_empty(page_list)) {
			break;
		}
		page = list_entry(page_list->prev, struct page, lru);
		offset = (loff_t)page->index << PAGE_CACHE_SHIFT;

		/* count adjacent pages that we will read into */
		contig_pages = 0;
		expected_index = list_entry(page_list->prev,struct page,lru)->index;
		list_for_each_entry_reverse(tmp_page,page_list,lru) {
			if(tmp_page->index == expected_index) {
				contig_pages++;
				expected_index++;
			} else {
				break; 
			}
		}
		if(contig_pages + i >  num_pages) {
			contig_pages = num_pages - i;
		}

		/* for reads over a certain size could initiate async read ahead */

		read_size = contig_pages * PAGE_CACHE_SIZE;
		/* Read size needs to be in multiples of one page */
		read_size = min_t(const unsigned int,read_size,cifs_sb->rsize & PAGE_CACHE_MASK);

		rc = -EAGAIN;
		while(rc == -EAGAIN) {
			if ((open_file->invalidHandle) && (!open_file->closePend)) {
				rc = cifs_reopen_file(file->f_dentry->d_inode,file);
				if(rc != 0)
					break;
			}

			rc = CIFSSMBRead(xid, pTcon,
				open_file->netfid,
				read_size, offset,
				&bytes_read, &smb_read_data);
			/* BB need to check return code here */
			if(rc== -EAGAIN) {
				if(smb_read_data) {
					cifs_buf_release(smb_read_data);
					smb_read_data = 0;
				}
			}
		}
		if ((rc < 0) || (smb_read_data == NULL)) {
			cFYI(1,("Read error in readpages: %d",rc));
			/* clean up remaing pages off list */
			while (!list_empty(page_list) && (i < num_pages)) {
				page = list_entry(page_list->prev, struct page, lru);
				list_del(&page->lru);
				page_cache_release(page);
			}
			break;
		} else if (bytes_read > 0) {
			pSMBr = (struct smb_com_read_rsp *)smb_read_data;
			cifs_copy_cache_pages(mapping, page_list, bytes_read,
				smb_read_data + 4 /* RFC1000 hdr */ +
				le16_to_cpu(pSMBr->DataOffset), &lru_pvec);

			i +=  bytes_read >> PAGE_CACHE_SHIFT;

			if((int)(bytes_read & PAGE_CACHE_MASK) != bytes_read) {
				cFYI(1,("Partial page %d of %d read to cache",i++,num_pages));

				i++; /* account for partial page */

				/* server copy of file can have smaller size than client */
				/* BB do we need to verify this common case ? this case is ok - 
				if we are at server EOF we will hit it on next read */

			/* while(!list_empty(page_list) && (i < num_pages)) {
					page = list_entry(page_list->prev,struct page, list);
					list_del(&page->list);
					page_cache_release(page);
				}
				break; */
			}
		} else {
			cFYI(1,("No bytes read (%d) at offset %lld . Cleaning remaining pages from readahead list",bytes_read,offset)); 
			/* BB turn off caching and do new lookup on file size at server? */
			while (!list_empty(page_list) && (i < num_pages)) {
				page = list_entry(page_list->prev, struct page, lru);
				list_del(&page->lru);
				page_cache_release(page); /* BB removeme - replace with zero of page? */
			}
			break;
		}
		if(smb_read_data) {
			cifs_buf_release(smb_read_data);
			smb_read_data = 0;
		}
		bytes_read = 0;
	}

	pagevec_lru_add(&lru_pvec);

/* need to free smb_read_data buf before exit */
if(smb_read_data) {
	cifs_buf_release(smb_read_data);
	smb_read_data = 0;
} 

	FreeXid(xid);
	return rc;
}

static int
cifs_readpage(struct file *file, struct page *page)
{
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char * read_data;
	int rc = -EACCES;
	int xid;

	xid = GetXid();

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	cFYI(0,("readpage %p at offset %d 0x%x\n",page,(int)offset,(int)offset));

	page_cache_get(page);
	read_data = kmap(page);
	/* for reads over a certain size could initiate async read ahead */

	rc = cifs_read(file, read_data, PAGE_CACHE_SIZE, &offset);

	if (rc < 0)
		goto io_error;
	else {
		cFYI(1,("Bytes read %d ",rc));
	}

	file->f_dentry->d_inode->i_atime = CURRENT_TIME;

	if(PAGE_CACHE_SIZE > rc) {
		memset(read_data+rc, 0, PAGE_CACHE_SIZE - rc);
	}
	flush_dcache_page(page);
	SetPageUptodate(page);
	rc = 0;

io_error:
	kunmap(page);
	unlock_page(page);

	page_cache_release(page);
	FreeXid(xid);
	return rc;
}

int cifs_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	cFYI(1,("prepare write for page %p from %d to %d",page,from,to));
	if (!PageUptodate(page)) {
		if (to - from != PAGE_CACHE_SIZE) {
			void *kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr, 0, from);
			memset(kaddr + to, 0, PAGE_CACHE_SIZE - to);
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
		}
		SetPageUptodate(page);
	}
	return 0;
}

struct address_space_operations cifs_addr_ops = {
	.readpage = cifs_readpage,
/*	.readpages = cifs_readpages,*/ /* BB removeme comments test BB */
	.writepage = cifs_writepage,
	.prepare_write = simple_prepare_write, /* BB fixme BB */
/*	.prepare_write = cifs_prepare_write, */  /* BB removeme BB */
	.commit_write = cifs_commit_write,
   /* .sync_page = cifs_sync_page, */
	/*.direct_IO = */
};
