/* 
 * Copyright (C) 2000 - 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/string.h"
#include "linux/types.h"
#include "linux/errno.h"
#include "linux/slab.h"
#include "linux/init.h"
#include "linux/fs.h"
#include "linux/stat.h"
#include "hostfs.h"
#include "kern.h"
#include "init.h"
#include "kern_util.h"
#include "filehandle.h"
#include "os.h"

/* Changed in hostfs_args before the kernel starts running */
static char *jail_dir = "/";
int append = 0;

static int __init hostfs_args(char *options, int *add)
{
	char *ptr;

	ptr = strchr(options, ',');
	if(ptr != NULL)
		*ptr++ = '\0';
	if(*options != '\0')
		jail_dir = options;

	options = ptr;
	while(options){
		ptr = strchr(options, ',');
		if(ptr != NULL)
			*ptr++ = '\0';
		if(*options != '\0'){
			if(!strcmp(options, "append"))
				append = 1;
			else printf("hostfs_args - unsupported option - %s\n",
				    options);
		}
		options = ptr;
	}
	return(0);
}

__uml_setup("hostfs=", hostfs_args,
"hostfs=<root dir>,<flags>,...\n"
"    This is used to set hostfs parameters.  The root directory argument\n"
"    is used to confine all hostfs mounts to within the specified directory\n"
"    tree on the host.  If this isn't specified, then a user inside UML can\n"
"    mount anything on the host that's accessible to the user that's running\n"
"    it.\n"
"    The only flag currently supported is 'append', which specifies that all\n"
"    files opened by hostfs will be opened in append mode.\n\n"
);

struct hostfs_data {
	struct externfs_data ext;
	char *mount;
};

struct hostfs_file {
	struct externfs_inode ext;
	struct file_handle fh;
};

static int hostfs_access_file(char *file, int uid, int w, int x, int gid, 
			      int r, struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };
	char tmp[HOSTFS_BUFSIZE];
	int err, mode = 0;

	if(r) mode = OS_ACC_R_OK;
	if(w) mode |= OS_ACC_W_OK;
	if(x) mode |= OS_ACC_X_OK;
	
	err = -ENOMEM;
	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_access(file, mode);
	free_path(file, tmp);
 out:
	return(err);
}

static int hostfs_make_node(const char *file, int mode, int uid, int gid, 
			    int type, int major, int minor, 
			    struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };
	char tmp[HOSTFS_BUFSIZE];
	int err = -ENOMEM;

	file = get_path(path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	/* XXX Pass type in an OS-independent way */
	mode |= type;

	err = os_make_dev(file, mode, major, minor);
	free_path(file, tmp);
 out:
	return(err);
}

static int hostfs_stat_file(const char *file, struct externfs_data *ed, 
			    dev_t *dev_out, unsigned long long *inode_out, 
			    int *mode_out, int *nlink_out, int *uid_out, 
			    int *gid_out, unsigned long long *size_out, 
			    unsigned long *atime_out, unsigned long *mtime_out,
			    unsigned long *ctime_out, int *blksize_out, 
			    unsigned long long *blocks_out)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	/* XXX Why pretend everything is owned by root? */
	*uid_out = 0;
	*gid_out = 0;
	return(host_stat_file(path, dev_out, inode_out, mode_out, nlink_out,
			      NULL, NULL, size_out, atime_out, mtime_out,
			      ctime_out, blksize_out, blocks_out));
}

static int hostfs_file_type(const char *file, int *rdev, 
			    struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_file_type(path, rdev));
}

static char *hostfs_name(struct inode *inode)
{
	struct externfs_data *ed = inode_externfs_info(inode);
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;

	return(inode_name_prefix(inode, mount));	
}

static struct externfs_inode *hostfs_init_file(struct externfs_data *ed)
{
	struct hostfs_file *hf;

	hf = kmalloc(sizeof(*hf), GFP_KERNEL);
	if(hf == NULL)
		return(NULL);

	hf->fh.fd = -1;
	return(&hf->ext);
}

static int hostfs_open_file(struct externfs_inode *ext, char *file, 
			    int uid, int gid, struct inode *inode, 
			    struct externfs_data *ed)
{
	struct hostfs_file *hf = container_of(ext, struct hostfs_file, ext);
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };
	int err;

	err = host_open_file(path, 1, 1, &hf->fh);
	if(err == -EISDIR)
		goto out;

	if(err == -EACCES)
		err = host_open_file(path, 1, 0, &hf->fh);

	if(err)
		goto out;

	is_reclaimable(&hf->fh, hostfs_name, inode);
 out:
	return(err);
}

static void *hostfs_open_dir(char *file, int uid, int gid, 
			     struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_open_dir(path));
}

static void hostfs_close_dir(void *stream, struct externfs_data *ed)
{
	os_close_dir(stream);
}

static char *hostfs_read_dir(void *stream, unsigned long long *pos, 
			     unsigned long long *ino_out, int *len_out, 
			     struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;

	return(generic_host_read_dir(stream, pos, ino_out, len_out, mount));
}

static int hostfs_read_file(struct externfs_inode *ext, 
			    unsigned long long offset, char *buf, int len, 
			    int ignore_start, int ignore_end,
			    void (*completion)(char *, int, void *), void *arg,
			    struct externfs_data *ed)
{
	struct hostfs_file *hf = container_of(ext, struct hostfs_file, ext);
	int err = 0;

	if(ignore_start != 0){
		err = read_file(&hf->fh, offset, buf, ignore_start);
		if(err < 0)
			goto out;
	}

	if(ignore_end != len)
		err = read_file(&hf->fh, offset + ignore_end, buf + ignore_end,
				len - ignore_end);

 out:

	(*completion)(buf, err, arg);
	if (err > 0)
		err = 0;
	return(err);
}

static int hostfs_write_file(struct externfs_inode *ext,
			     unsigned long long offset, const char *buf, 
			     int start, int len, 
			     void (*completion)(char *, int, void *), 
			     void *arg, struct externfs_data *ed)
{
	struct file_handle *fh;
	int err;

	fh = &container_of(ext, struct hostfs_file, ext)->fh;
	err = write_file(fh, offset + start, buf + start, len);

	(*completion)((char *) buf, err, arg);
	if (err > 0)
		err = 0;

	return(err);
}

static int hostfs_create_file(struct externfs_inode *ext, char *file, int mode,
			      int uid, int gid, struct inode *inode, 
			      struct externfs_data *ed)
{
	struct hostfs_file *hf = container_of(ext, struct hostfs_file, 
					      ext);
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };
	int err = -ENOMEM;
	
	err = host_create_file(path, mode, &hf->fh);
	if(err)
		goto out;

	is_reclaimable(&hf->fh, hostfs_name, inode);
 out:
	return(err);
}

static int hostfs_set_attr(const char *file, struct externfs_iattr *attrs, 
			   struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_set_attr(path, attrs));
}

static int hostfs_make_symlink(const char *from, const char *to, int uid, 
			       int gid, struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, from, NULL };

	return(host_make_symlink(path, to));
}

static int hostfs_link_file(const char *to, const char *from, int uid, int gid,
			    struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *to_path[] = { jail_dir, mount, to, NULL };
	const char *from_path[] = { jail_dir, mount, from, NULL };

	return(host_link_file(to_path, from_path));
}

static int hostfs_unlink_file(const char *file, struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_unlink_file(path));
}

static int hostfs_make_dir(const char *file, int mode, int uid, int gid, 
			   struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_make_dir(path, mode));
}

static int hostfs_remove_dir(const char *file, int uid, int gid, 
			     struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_remove_dir(path));
}

static int hostfs_read_link(char *file, int uid, int gid, char *buf, int size, 
			    struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, file, NULL };

	return(host_read_link(path, buf, size));
}

static int hostfs_rename_file(char *from, char *to, struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *to_path[] = { jail_dir, mount, to, NULL };
	const char *from_path[] = { jail_dir, mount, from, NULL };

	return(host_rename_file(from_path, to_path));
}

static int hostfs_stat_fs(long *bsize_out, long long *blocks_out, 
			  long long *bfree_out, long long *bavail_out, 
			  long long *files_out, long long *ffree_out,
			  void *fsid_out, int fsid_size, long *namelen_out, 
			  long *spare_out, struct externfs_data *ed)
{
	char *mount = container_of(ed, struct hostfs_data, ext)->mount;
	const char *path[] = { jail_dir, mount, NULL };

	return(host_stat_fs(path, bsize_out, blocks_out, bfree_out, bavail_out,
			    files_out, ffree_out, fsid_out, fsid_size, 
			    namelen_out, spare_out));
}

void hostfs_close_file(struct externfs_inode *ext,
		       unsigned long long size)
{
	struct hostfs_file *hf = container_of(ext, struct hostfs_file, ext);

	if(hf->fh.fd != -1){
		truncate_file(&hf->fh, size);
		close_file(&hf->fh);
	}

	kfree(hf);
}

int hostfs_truncate_file(struct externfs_inode *ext, __u64 size, 
			 struct externfs_data *ed)
{
	struct hostfs_file *hf = container_of(ext, struct hostfs_file, ext);

	return(truncate_file(&hf->fh, size));
}

static struct externfs_file_ops hostfs_file_ops = {
	.stat_file		= hostfs_stat_file,
	.file_type		= hostfs_file_type,
	.access_file		= hostfs_access_file,
	.open_file		= hostfs_open_file,
	.open_dir		= hostfs_open_dir,
	.read_dir		= hostfs_read_dir,
	.read_file		= hostfs_read_file,
	.write_file		= hostfs_write_file,
	.map_file_page		= NULL,
	.close_file		= hostfs_close_file,
	.close_dir		= hostfs_close_dir,
	.invisible		= NULL,
	.create_file		= hostfs_create_file,
	.set_attr		= hostfs_set_attr,
	.make_symlink		= hostfs_make_symlink,
	.unlink_file		= hostfs_unlink_file,
	.make_dir		= hostfs_make_dir,
	.remove_dir		= hostfs_remove_dir,
	.make_node		= hostfs_make_node,
	.link_file		= hostfs_link_file,
	.read_link		= hostfs_read_link,
	.rename_file		= hostfs_rename_file,
	.statfs			= hostfs_stat_fs,
	.truncate_file		= hostfs_truncate_file
};

static struct externfs_data *mount_fs(char *mount_arg)
{
	struct hostfs_data *hd;
	int err = -ENOMEM;

	hd = kmalloc(sizeof(*hd), GFP_KERNEL);
	if(hd == NULL)
		goto out;

	hd->mount = host_root_filename(mount_arg);
	if(hd->mount == NULL)
		goto out_free;

	init_externfs(&hd->ext, &hostfs_file_ops);

	return(&hd->ext);

 out_free:
	kfree(hd);
 out:
	return(ERR_PTR(err));
}

static struct externfs_mount_ops hostfs_mount_ops = {
	.init_file		= hostfs_init_file,
	.mount			= mount_fs,
};

static int __init init_hostfs(void)
{
	return(register_externfs("hostfs", &hostfs_mount_ops));
}

static void __exit exit_hostfs(void)
{
	unregister_externfs("hostfs");
}

__initcall(init_hostfs);
__exitcall(exit_hostfs);

#if 0
module_init(init_hostfs)
module_exit(exit_hostfs)
MODULE_LICENSE("GPL");
#endif

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
