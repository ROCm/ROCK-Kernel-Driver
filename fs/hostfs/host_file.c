/* 
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/string.h"
#include "linux/errno.h"
#include "linux/types.h"
#include "linux/slab.h"
#include "linux/fs.h"
#include "asm/fcntl.h"
#include "hostfs.h"
#include "filehandle.h"

extern int append;

char *get_path(const char *path[], char *buf, int size)
{
	const char **s;
	char *p;
	int new = 1;

	for(s = path; *s != NULL; s++){
		new += strlen(*s);
		if((*(s + 1) != NULL) && (strlen(*s) > 0) && 
		   ((*s)[strlen(*s) - 1] != '/'))
			new++;
	}

	if(new > size){
		buf = kmalloc(new, GFP_KERNEL);
		if(buf == NULL)
			return(NULL);
	}

	p = buf;
	for(s = path; *s != NULL; s++){
		strcpy(p, *s);
		p += strlen(*s);
		if((*(s + 1) != NULL) && (strlen(*s) > 0) && 
		   ((*s)[strlen(*s) - 1] != '/'))
			strcpy(p++, "/");
	}
		
	return(buf);
}

void free_path(const char *buf, char *tmp)
{
	if((buf != tmp) && (buf != NULL))
		kfree((char *) buf);
}

int host_open_file(const char *path[], int r, int w, struct file_handle *fh)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int mode = 0, err;
	struct openflags flags = OPENFLAGS();

	if (r)
		flags = of_read(flags);
	if (w)
		flags = of_write(flags);
	if(append)
		flags = of_append(flags);

	err = -ENOMEM;
	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;
	
	err = open_filehandle(file, flags, mode, fh);
 out:
	free_path(file, tmp);
	return(err);
}

void *host_open_dir(const char *path[])
{
	char tmp[HOSTFS_BUFSIZE], *file;
	void *dir = ERR_PTR(-ENOMEM);

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;
	
	dir = open_dir(file);
 out:
	free_path(file, tmp);
	return(dir);
}

char *host_read_dir(void *stream, unsigned long long *pos, 
		    unsigned long long *ino_out, int *len_out)
{
	int err;
	char *name;

	err = os_seek_dir(stream, *pos);
	if(err)
		return(ERR_PTR(err));

	err = os_read_dir(stream, ino_out, &name);
	if(err)
		return(ERR_PTR(err));

	if(name == NULL)
		return(NULL);

	*len_out = strlen(name);
	*pos = os_tell_dir(stream);
	return(name);
}

int host_file_type(const char *path[], int *rdev)
{
	char tmp[HOSTFS_BUFSIZE], *file;
 	struct uml_stat buf;
	int ret;

	ret = -ENOMEM;
	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	if(rdev != NULL){
		ret = os_lstat_file(file, &buf);
		if(ret)
			goto out;
		*rdev = MKDEV(buf.ust_rmajor, buf.ust_rminor);
	}

	ret = os_file_type(file);
 out:
	free_path(file, tmp);
	return(ret);
}

int host_create_file(const char *path[], int mode, struct file_handle *fh)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = open_filehandle(file, of_create(of_rdwr(OPENFLAGS())), mode, fh);
 out:
	free_path(file, tmp);
	return(err);
}

static int do_stat_file(const char *path, int *dev_out, 
			unsigned long long *inode_out, int *mode_out, 
			int *nlink_out, int *uid_out, int *gid_out, 
			unsigned long long *size_out, unsigned long *atime_out,
			unsigned long *mtime_out, unsigned long *ctime_out,
			int *blksize_out, unsigned long long *blocks_out)
{
	struct uml_stat buf;
	int err;

	err = os_lstat_file(path, &buf);
	if(err < 0)
		return(err);

	if(dev_out != NULL) *dev_out = MKDEV(buf.ust_major, buf.ust_minor);
	if(inode_out != NULL) *inode_out = buf.ust_ino;
	if(mode_out != NULL) *mode_out = buf.ust_mode;
	if(nlink_out != NULL) *nlink_out = buf.ust_nlink;
	if(uid_out != NULL) *uid_out = buf.ust_uid;
	if(gid_out != NULL) *gid_out = buf.ust_gid;
	if(size_out != NULL) *size_out = buf.ust_size;
	if(atime_out != NULL) *atime_out = buf.ust_atime;
	if(mtime_out != NULL) *mtime_out = buf.ust_mtime;
	if(ctime_out != NULL) *ctime_out = buf.ust_ctime;
	if(blksize_out != NULL) *blksize_out = buf.ust_blksize;
	if(blocks_out != NULL) *blocks_out = buf.ust_blocks;

	return(0);
}

int host_stat_file(const char *path[], int *dev_out,
		   unsigned long long *inode_out, int *mode_out, 
		   int *nlink_out, int *uid_out, int *gid_out, 
		   unsigned long long *size_out, unsigned long *atime_out,
		   unsigned long *mtime_out, unsigned long *ctime_out,
		   int *blksize_out, unsigned long long *blocks_out)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err;

	err = -ENOMEM;
	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = do_stat_file(file, dev_out, inode_out, mode_out, nlink_out, 
			   uid_out, gid_out, size_out, atime_out, mtime_out,
			   ctime_out, blksize_out, blocks_out);
 out:
	free_path(file, tmp);
	return(err);
}

int host_set_attr(const char *path[], struct externfs_iattr *attrs)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	unsigned long time;
	int err = 0, ma;

	if(append && (attrs->ia_valid & EXTERNFS_ATTR_SIZE))
		return(-EPERM);

	err = -ENOMEM;
	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	if(attrs->ia_valid & EXTERNFS_ATTR_MODE){
		err = os_set_file_perms(file, attrs->ia_mode);
		if(err < 0)
			goto out;
	}
	if(attrs->ia_valid & EXTERNFS_ATTR_UID){
		err = os_set_file_owner(file, attrs->ia_uid, -1);
		if(err < 0)
			goto out;
	}
	if(attrs->ia_valid & EXTERNFS_ATTR_GID){
		err = os_set_file_owner(file, -1, attrs->ia_gid);
		if(err < 0)
			goto out;
	}
	if(attrs->ia_valid & EXTERNFS_ATTR_SIZE){
		err = os_truncate_file(file, attrs->ia_size);
		if(err < 0)
			goto out;
	}
	ma = EXTERNFS_ATTR_ATIME_SET | EXTERNFS_ATTR_MTIME_SET;
	if((attrs->ia_valid & ma) == ma){
		err = os_set_file_time(file, attrs->ia_atime, attrs->ia_mtime);
		if(err)
			goto out;
	}
	else {
		if(attrs->ia_valid & EXTERNFS_ATTR_ATIME_SET){
			err = do_stat_file(file, NULL, NULL, NULL, NULL, NULL, 
					   NULL, NULL, NULL, &time, 
					   NULL, NULL, NULL);
			if(err != 0)
				goto out;

			err = os_set_file_time(file, attrs->ia_atime, time);
			if(err)
				goto out;
		}
		if(attrs->ia_valid & EXTERNFS_ATTR_MTIME_SET){
			err = do_stat_file(file, NULL, NULL, NULL, NULL, NULL, 
					   NULL, NULL, &time, NULL, 
					   NULL, NULL, NULL);
			if(err != 0)
				goto out;

			err = os_set_file_time(file, time, attrs->ia_mtime);
			if(err)
				goto out;
		}
	}
	if(attrs->ia_valid & EXTERNFS_ATTR_CTIME) ;
	if(attrs->ia_valid & (EXTERNFS_ATTR_ATIME | EXTERNFS_ATTR_MTIME)){
		err = do_stat_file(file, NULL, NULL, NULL, NULL, NULL, 
				   NULL, NULL, &attrs->ia_atime, 
				   &attrs->ia_mtime, NULL, NULL, NULL);
		if(err != 0)
			goto out;
	}

	err = 0;
 out:
	free_path(file, tmp);
	return(err);
}

int host_make_symlink(const char *from[], const char *to)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	file = get_path(from, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;
	
	err = os_make_symlink(to, file);
 out:
	free_path(file, tmp);
	return(err);
}

int host_unlink_file(const char *path[])
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	if(append)
		return(-EPERM);

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_remove_file(file);
 out:
	free_path(file, tmp);
	return(err);
}

int host_make_dir(const char *path[], int mode)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_make_dir(file, mode);
 out:
	free_path(file, tmp);
	return(err);
}

int host_remove_dir(const char *path[])
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_remove_dir(file);
 out:
	free_path(file, tmp);
	return(err);
}

int host_link_file(const char *to[], const char *from[])
{
	char from_tmp[HOSTFS_BUFSIZE], *f, to_tmp[HOSTFS_BUFSIZE], *t;
	int err = -ENOMEM;

	f = get_path(from, from_tmp, sizeof(from_tmp));
	t = get_path(to, to_tmp, sizeof(to_tmp));
	if((f == NULL) || (t == NULL))
		goto out;

	err = os_link_file(t, f);
 out:
	free_path(f, from_tmp);
	free_path(t, to_tmp);
	return(err);
}

int host_read_link(const char *path[], char *buf, int size)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int n = -ENOMEM;

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	n = os_read_symlink(file, buf, size);
	if(n < size) 
		buf[n] = '\0';
 out:
	free_path(file, tmp);
	return(n);
}

int host_rename_file(const char *from[], const char *to[])
{
	char from_tmp[HOSTFS_BUFSIZE], *f, to_tmp[HOSTFS_BUFSIZE], *t;
	int err = -ENOMEM;

	f = get_path(from, from_tmp, sizeof(from_tmp));
	t = get_path(to, to_tmp, sizeof(to_tmp));
	if((f == NULL) || (t == NULL))
		goto out;

	err = os_move_file(f, t);
 out:
	free_path(f, from_tmp);
	free_path(t, to_tmp);
	return(err);
}

int host_stat_fs(const char *path[], long *bsize_out, long long *blocks_out, 
		 long long *bfree_out, long long *bavail_out, 
		 long long *files_out, long long *ffree_out, void *fsid_out, 
		 int fsid_size, long *namelen_out, long *spare_out)
{
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_stat_filesystem(file, bsize_out, blocks_out, bfree_out, 
				 bavail_out, files_out, ffree_out, fsid_out, 
				 fsid_size, namelen_out, spare_out);
 out:
	free_path(file, tmp);
	return(err);
}

char *generic_host_read_dir(void *stream, unsigned long long *pos, 
			    unsigned long long *ino_out, int *len_out, 
			    void *mount)
{
	return(host_read_dir(stream, pos, ino_out, len_out));
}

int generic_host_truncate_file(struct file_handle *fh, __u64 size, void *m)
{
	return(truncate_file(fh, size));
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
