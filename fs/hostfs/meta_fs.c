/* 
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <linux/slab.h>
#include <linux/init.h>
#include "hostfs.h"
#include "metadata.h"
#include "kern_util.h"

#define METADATA_FILE_PATH(meta) (meta)->root, "file_metadata"
#define METADATA_DIR_PATH(meta) (meta)->root, "dir_metadata"

struct meta_fs {
	struct humfs humfs;
	char *root;
};

struct meta_file {
	struct humfs_file humfs;
	struct file_handle fh;
};

static int meta_file_path(const char *path, struct meta_fs *meta, 
			  const char *path_out[])
{
	const char *data_path[] = { meta->root, "data", path, NULL };
	char data_tmp[HOSTFS_BUFSIZE];
	char *data_file = get_path(data_path, data_tmp, sizeof(data_tmp));

	if(data_file == NULL)
		return(-ENOMEM);

	path_out[0] = meta->root;
	path_out[2] = path;
	if(os_file_type(data_file) == OS_TYPE_DIR){
		path_out[1] = "dir_metadata";
		path_out[3] = "metadata";
		path_out[4] = NULL;
	}
	else {
		path_out[1] = "file_metadata";
		path_out[3] = NULL;
	}

	return(0);
}

static int open_meta_file(const char *path, struct humfs *humfs,
			  struct file_handle *fh)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	const char *meta_path[5];
	char meta_tmp[HOSTFS_BUFSIZE];
	char *meta_file;
	int err;

	err = meta_file_path(path, meta, meta_path);
	if(err)
		goto out;

	meta_file = get_path(meta_path, meta_tmp, sizeof(meta_tmp));
	if(meta_file == NULL)
		goto out;
	
	err = open_filehandle(meta_file, of_rdwr(OPENFLAGS()), 0, fh);

 out:
	return(err);
}

static char *meta_fs_name(struct inode *inode)
{
	struct humfs *mount = inode_humfs_info(inode);
	struct meta_fs *meta = container_of(mount, struct meta_fs, humfs);
	const char *metadata_path[5];
	char tmp[HOSTFS_BUFSIZE], *name, *file;

	if(meta_file_path("", meta, metadata_path))
		return(NULL);

	file = get_path(metadata_path, tmp, sizeof(tmp));
	if(file == NULL)
		return(NULL);

	name = inode_name_prefix(inode, file);

	free_path(file, tmp);
	return(name);
}

static void metafs_invisible(struct humfs_file *hf)
{
	struct meta_file *mf = container_of(hf, struct meta_file, humfs);

	not_reclaimable(&mf->fh);
}

static struct humfs_file *metafs_init_file(void)
{
	struct meta_file *mf;
	int err = -ENOMEM;

	mf = kmalloc(sizeof(*mf), GFP_KERNEL);
	if(mf == NULL)
		return(ERR_PTR(err));

	return(&mf->humfs);
}

static int metafs_open_file(struct humfs_file *hf, const char *path, 
			    struct inode *inode, struct humfs *humfs)
{
	struct meta_file *mf = container_of(hf, struct meta_file, humfs);
	int err;

	err = open_meta_file(path, humfs, &mf->fh);
	if(err)
		return(err);

	is_reclaimable(&mf->fh, meta_fs_name, inode);

	return(0);
}

static void metafs_close_file(struct humfs_file *hf)
{
	struct meta_file *meta = container_of(hf, struct meta_file, humfs);

	close_file(&meta->fh);
	kfree(meta);
}

static int metafs_create_file(struct humfs_file *hf, const char *path, 
			      int mode, int uid, int gid, struct inode *inode, 
			      struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	struct meta_file *mf = container_of(hf, struct meta_file, humfs);
	char tmp[HOSTFS_BUFSIZE];
	const char *metadata_path[] = { METADATA_FILE_PATH(meta), path, NULL };
	char *file = get_path(metadata_path, tmp, sizeof(tmp));
	char buf[sizeof("mmmm uuuuuuuuuu gggggggggg")];
	int err = -ENOMEM;

	if(file == NULL)
		goto out;

	err = open_filehandle(file, of_write(of_create(OPENFLAGS())), 0644, 
			      &mf->fh);
	if(err)
		goto out_free_path;

	if(inode != NULL)
		is_reclaimable(&mf->fh, meta_fs_name, inode);

	sprintf(buf, "%d %d %d\n", mode  & S_IRWXUGO, uid, gid);
	err = write_file(&mf->fh, 0, buf, strlen(buf));
	if(err < 0)
		goto out_rm;

	free_path(file, tmp);
	return(0);

 out_rm:
	close_file(&mf->fh);
	os_remove_file(file);
 out_free_path:
	free_path(file, tmp);
 out:
	return(err);
}

static int metafs_create_link(const char *to, const char *from, 
			      struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	const char *path_to[] = { METADATA_FILE_PATH(meta), to,  NULL };
	const char *path_from[] = { METADATA_FILE_PATH(meta), from, NULL };

	return(host_link_file(path_to, path_from));
}

static int metafs_remove_file(const char *path, struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	char tmp[HOSTFS_BUFSIZE];
	const char *metadata_path[] = { METADATA_FILE_PATH(meta), path, NULL };
	char *file = get_path(metadata_path, tmp, sizeof(tmp));
	int err = -ENOMEM;

	if(file == NULL)
		goto out;

	err = os_remove_file(file);

 out:
	free_path(file, tmp);
	return(err);
}

static int metafs_create_directory(const char *path, int mode, int uid, 
				   int gid, struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	char tmp[HOSTFS_BUFSIZE];
	const char *dir_path[] = { METADATA_DIR_PATH(meta), path, NULL, NULL };
	const char *file_path[] = { METADATA_FILE_PATH(meta), path, NULL, 
				    NULL };
	char *file, dir_meta[sizeof("mmmm uuuuuuuuuu gggggggggg\n")];
	int err, fd;

	err = host_make_dir(dir_path, 0755);
	if(err)
		goto out;

	err = host_make_dir(file_path, 0755);
	if(err)
		goto out_rm;

	/* This to make the index independent of the number of elements in
	 * METADATA_DIR_PATH().
	 */
	dir_path[sizeof(dir_path) / sizeof(dir_path[0]) - 2] = "metadata";

	err = -ENOMEM;
	file = get_path(dir_path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	fd = os_open_file(file, of_create(of_rdwr(OPENFLAGS())), 0644);
	if(fd < 0){
		err = fd;
		goto out_free;
	}

	sprintf(dir_meta, "%d %d %d\n", mode & S_IRWXUGO, uid, gid);
	err = os_write_file(fd, dir_meta, strlen(dir_meta));
	if(err > 0)
		err = 0;

	os_close_file(fd);

 out_free:
	free_path(file, tmp);
 out_rm:
	host_remove_dir(dir_path);
 out:
	return(err);
}

static int metafs_remove_directory(const char *path, struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	char tmp[HOSTFS_BUFSIZE], *file;
	const char *dir_path[] = { METADATA_DIR_PATH(meta), path, "metadata", 
				   NULL };
	const char *file_path[] = { METADATA_FILE_PATH(meta), path, NULL };
	char *slash;
	int err;

	err = -ENOMEM;
	file = get_path(dir_path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_remove_file(file);
	if(err)
		goto out_free;

	slash = strrchr(file, '/');
	if(slash == NULL){
		printk("remove_shadow_directory failed to find last slash\n");
		goto out_free;
	}
	*slash = '\0';
	err = os_remove_dir(file);
	free_path(file, tmp);

	file = get_path(file_path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = os_remove_dir(file);
	if(err)
		goto out_free;

 out:
	return(err);
 out_free:
	free_path(file, tmp);
	goto out;
}

static int metafs_make_node(const char *path, int mode, int uid, int gid, 
			    int type, int maj, int min, struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	struct file_handle fh;
	char tmp[HOSTFS_BUFSIZE];
	const char *metadata_path[] = { METADATA_FILE_PATH(meta), path, NULL };
	int err;
	char buf[sizeof("mmmm uuuuuuuuuu gggggggggg x nnn mmm\n")], *file;

	sprintf(buf, "%d %d %d %c %d %d\n", mode & S_IRWXUGO, uid, gid, type, 
		maj, min);

	err = -ENOMEM;
	file = get_path(metadata_path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	err = open_filehandle(file, 
			      of_create(of_rdwr(OPENFLAGS())), 0644, &fh);
	if(err)
		goto out_free;

	err = write_file(&fh, 0, buf, strlen(buf));
	if(err > 0)
		err = 0;

	close_file(&fh);

 out_free:
	free_path(file, tmp);
 out:
	return(err);
}

static int metafs_ownerships(const char *path, int *mode_out, int *uid_out, 
			     int *gid_out, char *type_out, int *maj_out, 
			     int *min_out, struct humfs *humfs)
{
	struct file_handle fh;
	char buf[sizeof("mmmm uuuuuuuuuu gggggggggg x nnn mmm\n")];
	int err, n, mode, uid, gid, maj, min;
	char type;

	err = open_meta_file(path, humfs, &fh);
	if(err)
		goto out;

	err = os_read_file(fh.fd, buf, sizeof(buf) - 1);
	if(err < 0)
		goto out_close;

	buf[err] = '\0';
	err = 0;

	n = sscanf(buf, "%d %d %d %c %d %d", &mode, &uid, &gid, &type, &maj, 
		   &min);
	if(n == 3){
		maj = -1;
		min = -1;
		type = 0;
		err = 0;
	}
	else if(n != 6)
		err = -EINVAL;

	if(mode_out != NULL)
		*mode_out = mode;
	if(uid_out != NULL)
		*uid_out = uid;
	if(gid_out != NULL)
		*gid_out = uid;
	if(type_out != NULL)
		*type_out = type;
	if(maj_out != NULL)
		*maj_out = maj;
	if(min_out != NULL)
		*min_out = min;

 out_close:
	close_file(&fh);
 out:
	return(err);
}

static int metafs_change_ownerships(const char *path, int mode, int uid, 
				    int gid, struct humfs *humfs)
{
	struct file_handle fh;
	char type;
	char buf[sizeof("mmmm uuuuuuuuuu gggggggggg x nnn mmm\n")];
	int err = -ENOMEM, old_mode, old_uid, old_gid, n, maj, min;

	err = open_meta_file(path, humfs, &fh);
	if(err)
		goto out;

	err = read_file(&fh, 0, buf, sizeof(buf) - 1);
	if(err < 0)
		goto out_close;

	buf[err] = '\0';

	n = sscanf(buf, "%d %d %d %c %d %d\n", &old_mode, &old_uid, &old_gid,
		   &type, &maj, &min);
	if((n != 3) && (n != 6)){
		err = -EINVAL;
		goto out_close;
	}

	if(mode == -1)
                mode = old_mode;
	if(uid == -1)
		uid = old_uid;
	if(gid == -1)
		gid = old_gid;

	if(n == 3)
		sprintf(buf, "%d %d %d\n", mode & S_IRWXUGO, uid, gid);
	else
		sprintf(buf, "%d %d %d %c %d %d\n", mode & S_IRWXUGO, uid, gid,
			type, maj, min);

	err = write_file(&fh, 0, buf, strlen(buf));
	if(err > 0)
		err = 0;

	err = truncate_file(&fh, strlen(buf));

 out_close:
	close_file(&fh);
 out:
	return(err);
}

static int metafs_rename_file(const char *from, const char *to, 
			      struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	const char *metadata_path_from[5], *metadata_path_to[5];
	int err;

	err = meta_file_path(from, meta, metadata_path_from);
	if(err)
		return(err);

	err = meta_file_path(to, meta, metadata_path_to);
	if(err)
		return(err);

	return(host_rename_file(metadata_path_from, metadata_path_to));
}

static struct humfs *metafs_init_mount(char *root)
{
	struct meta_fs *meta;
	int err = -ENOMEM;

	meta = kmalloc(sizeof(*meta), GFP_KERNEL);
	if(meta == NULL)
		goto out;

	meta->root = uml_strdup(root);
	if(meta->root == NULL)
		goto out_free_meta;

	return(&meta->humfs);

 out_free_meta:
	kfree(meta);
 out:
	return(ERR_PTR(err));
}

static void metafs_free_mount(struct humfs *humfs)
{
	struct meta_fs *meta = container_of(humfs, struct meta_fs, humfs);
	
	kfree(meta);
}

struct humfs_meta_ops hum_fs_meta_fs_ops = {
	.list			= LIST_HEAD_INIT(hum_fs_meta_fs_ops.list),
	.name			= "shadow_fs",
	.init_file		= metafs_init_file,
	.open_file		= metafs_open_file,
	.close_file		= metafs_close_file,
	.ownerships		= metafs_ownerships,
	.make_node		= metafs_make_node,
	.create_file		= metafs_create_file,
	.create_link		= metafs_create_link,
	.remove_file		= metafs_remove_file,
	.create_dir		= metafs_create_directory,
	.remove_dir		= metafs_remove_directory,
	.change_ownerships	= metafs_change_ownerships,
	.rename_file		= metafs_rename_file,
	.invisible		= metafs_invisible,
	.init_mount		= metafs_init_mount,
	.free_mount		= metafs_free_mount,
};

static int __init init_meta_fs(void)
{
	register_meta(&hum_fs_meta_fs_ops);
	return(0);
}

static void __exit exit_meta_fs(void)
{
	unregister_meta(&hum_fs_meta_fs_ops);
}

__initcall(init_meta_fs);
__exitcall(exit_meta_fs);

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
