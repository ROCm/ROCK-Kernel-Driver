/* 
 * Copyright (C) 2004 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/slab.h"
#include "linux/list.h"
#include "linux/spinlock.h"
#include "linux/fs.h"
#include "linux/errno.h"
#include "filehandle.h"
#include "os.h"
#include "kern_util.h"

static spinlock_t open_files_lock = SPIN_LOCK_UNLOCKED;
static struct list_head open_files = LIST_HEAD_INIT(open_files);

#define NUM_RECLAIM 128

static void reclaim_fds(void)
{
	struct file_handle *victim;
	int closed = NUM_RECLAIM;

	spin_lock(&open_files_lock);
	while(!list_empty(&open_files) && closed--){
		victim = list_entry(open_files.prev, struct file_handle, list);
		os_close_file(victim->fd);
		victim->fd = -1;
		list_del_init(&victim->list);
	}
	spin_unlock(&open_files_lock);
}

int open_file(char *name, struct openflags flags, int mode)
{
	int fd;

	fd = os_open_file(name, flags, mode);
	if(fd != -EMFILE)
		return(fd);

	reclaim_fds();
	fd = os_open_file(name, flags, mode);

	return(fd);
}

void *open_dir(char *file)
{
	void *dir;
	int err;

	dir = os_open_dir(file, &err);
	if(dir != NULL)
		return(dir);
	if(err != -EMFILE)
		return(ERR_PTR(err));

	reclaim_fds();

	dir = os_open_dir(file, &err);
	if(dir == NULL)
		dir = ERR_PTR(err);

	return(dir);
}

void not_reclaimable(struct file_handle *fh)
{
	char *name;

	if(fh->get_name == NULL)
		return;

	if(list_empty(&fh->list)){
		name = (*fh->get_name)(fh->inode);
		if(name != NULL){
			fh->fd = open_file(name, fh->flags, 0);
			kfree(name);
		}
		else printk("File descriptor %d has no name\n", fh->fd);
	}
	else {
		spin_lock(&open_files_lock);
		list_del_init(&fh->list);
		spin_unlock(&open_files_lock);
	}
}

void is_reclaimable(struct file_handle *fh, char *(name_proc)(struct inode *),
		    struct inode *inode)
{
	fh->get_name = name_proc;
	fh->inode = inode;

	spin_lock(&open_files_lock);
	list_add(&fh->list, &open_files);
	spin_unlock(&open_files_lock);
}

static int active_handle(struct file_handle *fh)
{
	int fd;
	char *name;

	if(!list_empty(&fh->list))
		list_move(&fh->list, &open_files);

	if(fh->fd != -1)
		return(0);

	if(fh->inode == NULL)
		return(-ENOENT);

	name = (*fh->get_name)(fh->inode);
	if(name == NULL)
		return(-ENOMEM);

	fd = open_file(name, fh->flags, 0);
	kfree(name);
	if(fd < 0)
		return(fd);

	fh->fd = fd;
	is_reclaimable(fh, fh->get_name, fh->inode);

	return(0);
}

int filehandle_fd(struct file_handle *fh)
{
	int err;

	err = active_handle(fh);
	if(err)
		return(err);

	return(fh->fd);
}

static void init_fh(struct file_handle *fh, int fd, struct openflags flags)
{
	flags.c = 0;
	*fh = ((struct file_handle) { .list	= LIST_HEAD_INIT(fh->list),
				      .fd	= fd,
				      .get_name	= NULL,
				      .inode	= NULL,
				      .flags	= flags });
}

int open_filehandle(char *name, struct openflags flags, int mode, 
		    struct file_handle *fh)
{
	int fd;

	fd = open_file(name, flags, mode);
	if(fd < 0)
		return(fd);

	init_fh(fh, fd, flags);
	return(0);
}

int close_file(struct file_handle *fh)
{
	spin_lock(&open_files_lock);
	list_del(&fh->list);
	spin_unlock(&open_files_lock);

	os_close_file(fh->fd);

	fh->fd = -1;
	return(0);
}

int read_file(struct file_handle *fh, unsigned long long offset, char *buf,
	      int len)
{
	int err;

	err = active_handle(fh);
	if(err)
		return(err);

	err = os_seek_file(fh->fd, offset);
	if(err)
		return(err);

	return(os_read_file(fh->fd, buf, len));
}

int write_file(struct file_handle *fh, unsigned long long offset, 
	       const char *buf, int len)
{
	int err;

	err = active_handle(fh);
	if(err)
		return(err);

	if(offset != -1)
		err = os_seek_file(fh->fd, offset);
	if(err)
		return(err);

	return(os_write_file(fh->fd, buf, len));
}

int truncate_file(struct file_handle *fh, unsigned long long size)
{
	int err;

	err = active_handle(fh);
	if(err)
		return(err);

	return(os_truncate_fd(fh->fd, size));
}

int make_pipe(struct file_handle *fhs)
{
	int fds[2], err;

	err = os_pipe(fds, 1, 1);
	if(err && (err != -EMFILE))
		return(err);

	if(err){
		reclaim_fds();
		err = os_pipe(fds, 1, 1);
	}
	if(err)
		return(err);

	init_fh(&fhs[0], fds[0], OPENFLAGS());
	init_fh(&fhs[1], fds[1], OPENFLAGS());
	return(0);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
