
/*
 * Directory operations for Coda filesystem
 * Original version: (C) 1996 P. Braam and M. Callahan
 * Rewritten for Linux 2.1. (C) 1997 Carnegie Mellon University
 * 
 * Carnegie Mellon encourages users to contribute improvements to
 * the Coda project. Contact Peter Braam (coda@cs.cmu.edu).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/string.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_psdev.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_cache.h>
#include <linux/coda_proc.h>

/* dir inode-ops */
static int coda_create(struct inode *dir, struct dentry *new, int mode);
static int coda_mknod(struct inode *dir, struct dentry *new, int mode, int rdev);
static struct dentry *coda_lookup(struct inode *dir, struct dentry *target);
static int coda_link(struct dentry *old_dentry, struct inode *dir_inode, 
		     struct dentry *entry);
static int coda_unlink(struct inode *dir_inode, struct dentry *entry);
static int coda_symlink(struct inode *dir_inode, struct dentry *entry,
			const char *symname);
static int coda_mkdir(struct inode *dir_inode, struct dentry *entry, int mode);
static int coda_rmdir(struct inode *dir_inode, struct dentry *entry);
static int coda_rename(struct inode *old_inode, struct dentry *old_dentry, 
                       struct inode *new_inode, struct dentry *new_dentry);

/* dir file-ops */
static int coda_readdir(struct file *file, void *dirent, filldir_t filldir);

/* dentry ops */
static int coda_dentry_revalidate(struct dentry *de, int);
static int coda_dentry_delete(struct dentry *);

/* support routines */
static void coda_prepare_fakefile(struct inode *coda_inode,
		                  struct file *coda_file, 
				  struct inode *open_inode,
				  struct file *open_file,
				  struct dentry *open_dentry);
static int coda_venus_readdir(struct file *filp, void *dirent, 
			      filldir_t filldir);
int coda_fsync(struct file *, struct dentry *dentry, int datasync);

int coda_hasmknod;

struct dentry_operations coda_dentry_operations =
{
	d_revalidate:	coda_dentry_revalidate,
	d_delete:	coda_dentry_delete,
};

struct inode_operations coda_dir_inode_operations =
{
	create:		coda_create,
	lookup:		coda_lookup,
	link:		coda_link,
	unlink:		coda_unlink,
	symlink:	coda_symlink,
	mkdir:		coda_mkdir,
	rmdir:		coda_rmdir,
	mknod:		coda_mknod,
	rename:		coda_rename,
	permission:	coda_permission,
        revalidate:	coda_revalidate_inode,
	setattr:	coda_notify_change,
};

struct file_operations coda_dir_operations = {
	read:		generic_read_dir,
	readdir:	coda_readdir,
	open:		coda_open,
	release:	coda_release,
	fsync:		coda_fsync,
};


/* inode operations for directories */
/* acces routines: lookup, readlink, permission */
static struct dentry *coda_lookup(struct inode *dir, struct dentry *entry)
{
	struct inode *res_inode = NULL;
	struct ViceFid resfid = {0,0,0};
	int dropme = 0; /* to indicate entry should not be cached */
	int type = 0;
	int error = 0;
	const char *name = entry->d_name.name;
	size_t length = entry->d_name.len;
	
        ENTRY;

	if ( length > CODA_MAXNAMLEN ) {
	        printk("name too long: lookup, %s (%*s)\n", 
		       coda_i2s(dir), (int)length, name);
		return ERR_PTR(-ENAMETOOLONG);
	}

        CDEBUG(D_INODE, "name %s, len %ld in ino %ld, fid %s\n", 
	       name, (long)length, dir->i_ino, coda_i2s(dir));

        /* control object, create inode on the fly */
        if (coda_isroot(dir) && coda_iscontrol(name, length)) {
	        error = coda_cnode_makectl(&res_inode, dir->i_sb);
		CDEBUG(D_SPECIAL, 
		       "Lookup on CTL object; dir ino %ld, count %d\n", 
		       dir->i_ino, atomic_read(&dir->i_count));
		dropme = 1;
                goto exit;
        }

	error = venus_lookup(dir->i_sb, coda_i2f(dir), 
			     (const char *)name, length, &type, &resfid);

	res_inode = NULL;
	if (!error) {
		if (type & CODA_NOCACHE) {
			type &= (~CODA_NOCACHE);
			CDEBUG(D_INODE, "dropme set for %s\n", 
			       coda_f2s(&resfid));
			dropme = 1;
		}

	    	error = coda_cnode_make(&res_inode, &resfid, dir->i_sb);
		if (error) return ERR_PTR(error);

		/* make sure we drop unexpected weird fid's */
		if (coda_f2i(&resfid) != res_inode->i_ino &&
		    !coda_fid_is_weird(&resfid))
			dropme = 1;
	} else if (error != -ENOENT) {
	        CDEBUG(D_INODE, "error for %s(%*s)%d\n",
		       coda_i2s(dir), (int)length, name, error);
		return ERR_PTR(error);
	}
	CDEBUG(D_INODE, "lookup: %s is (%s), type %d result %d, dropme %d\n",
	       name, coda_f2s(&resfid), type, error, dropme);

exit:
	entry->d_time = 0;
	entry->d_op = &coda_dentry_operations;
	d_add(entry, res_inode);
	if ( dropme ) {
		d_drop(entry);
		coda_flag_inode(res_inode, C_VATTR);
	}
        EXIT;
        return NULL;
}


int coda_permission(struct inode *inode, int mask)
{
        int error;
 
        ENTRY;
	coda_vfs_stat.permission++;

        if ( mask == 0 )
                return 0;

	if ( coda_access_cache ) {
		coda_permission_stat.count++;

		if ( coda_cache_check(inode, mask) ) {
			coda_permission_stat.hit_count++;
			return 0; 
		}
	}

        CDEBUG(D_INODE, "mask is %o\n", mask);
        error = venus_access(inode->i_sb, coda_i2f(inode), mask);
    
        CDEBUG(D_INODE, "fid: %s, ino: %ld (mask: %o) error: %d\n", 
	       coda_i2s(inode), inode->i_ino, mask, error);

	if (!error)
		coda_cache_enter(inode, mask);

        return error; 
}


static inline void coda_dir_changed(struct inode *dir, int link)
{
#ifdef REQUERY_VENUS_FOR_MTIME
	/* invalidate the directory cnode's attributes so we refetch the
	 * attributes from venus next time the inode is referenced */
	coda_flag_inode(dir, C_VATTR);
#else
	/* optimistically we can also act as if our nose bleeds. The
         * granularity of the mtime is coarse anyways so we might actually be
         * right most of the time. Note: we only do this for directories. */
	dir->i_mtime = CURRENT_TIME;
#endif
	if (link)
		dir->i_nlink += link;
}

/* creation routines: create, mknod, mkdir, link, symlink */
static int coda_create(struct inode *dir, struct dentry *de, int mode)
{
        int error=0;
	const char *name=de->d_name.name;
	int length=de->d_name.len;
	struct inode *result = NULL;
	struct ViceFid newfid;
	struct coda_vattr attrs;

	ENTRY;
	coda_vfs_stat.create++;

	CDEBUG(D_INODE, "name: %s, length %d, mode %o\n", name, length, mode);

	if (coda_isroot(dir) && coda_iscontrol(name, length))
		return -EPERM;

	error = venus_create(dir->i_sb, coda_i2f(dir), name, length, 
				0, mode, 0, &newfid, &attrs);

        if ( error ) {
		CDEBUG(D_INODE, "create: %s, result %d\n",
		       coda_f2s(&newfid), error); 
		d_drop(de);
		return error;
	}

	error = coda_cnode_make(&result, &newfid, dir->i_sb);
	if ( error ) {
		d_drop(de);
		result = NULL;
		return error;
	}

	/* invalidate the directory cnode's attributes */
	coda_dir_changed(dir, 0);
	d_instantiate(de, result);
        return 0;
}

static int coda_mknod(struct inode *dir, struct dentry *de, int mode, int rdev)
{
        int error=0;
	const char *name=de->d_name.name;
	int length=de->d_name.len;
	struct inode *result = NULL;
	struct ViceFid newfid;
	struct coda_vattr attrs;

	if ( coda_hasmknod == 0 )
		return -EIO;

	coda_vfs_stat.create++;

	CDEBUG(D_INODE, "name: %s, length %d, mode %o, rdev %x\n",
	       name, length, mode, rdev);

	if (coda_isroot(dir) && coda_iscontrol(name, length))
		return -EPERM;

	error = venus_create(dir->i_sb, coda_i2f(dir), name, length, 
				0, mode, rdev, &newfid, &attrs);

        if ( error ) {
		CDEBUG(D_INODE, "mknod: %s, result %d\n",
		       coda_f2s(&newfid), error); 
		d_drop(de);
		return error;
	}

	error = coda_cnode_make(&result, &newfid, dir->i_sb);
	if ( error ) {
		d_drop(de);
		result = NULL;
		return error;
	}

	/* invalidate the directory cnode's attributes */
	coda_dir_changed(dir, 0);
	d_instantiate(de, result);
        return 0;
}			     

static int coda_mkdir(struct inode *dir, struct dentry *de, int mode)
{
	struct inode *inode;
	struct coda_vattr attr;
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int error;
	struct ViceFid newfid;

	ENTRY;
	coda_vfs_stat.mkdir++;

	if (coda_isroot(dir) && coda_iscontrol(name, len))
		return -EPERM;

	CDEBUG(D_INODE, "mkdir %s (len %d) in %s, mode %o.\n", 
	       name, len, coda_i2s(dir), mode);

	attr.va_mode = mode;
	error = venus_mkdir(dir->i_sb, coda_i2f(dir), 
			       name, len, &newfid, &attr);
        
        if ( error ) {
	        CDEBUG(D_INODE, "mkdir error: %s result %d\n", 
		       coda_f2s(&newfid), error); 
		d_drop(de);
                return error;
        }
         
	CDEBUG(D_INODE, "mkdir: new dir has fid %s.\n", 
	       coda_f2s(&newfid)); 

	error = coda_cnode_make(&inode, &newfid, dir->i_sb);
	if ( error ) {
		d_drop(de);
		return error;
	}
	
	/* invalidate the directory cnode's attributes */
	coda_dir_changed(dir, 1);
	d_instantiate(de, inode);
        return 0;
}

/* try to make de an entry in dir_inodde linked to source_de */ 
static int coda_link(struct dentry *source_de, struct inode *dir_inode, 
	  struct dentry *de)
{
	struct inode *inode = source_de->d_inode;
        const char * name = de->d_name.name;
	int len = de->d_name.len;
	int error;

        ENTRY;
	coda_vfs_stat.link++;

	if (coda_isroot(dir_inode) && coda_iscontrol(name, len))
		return -EPERM;

	CDEBUG(D_INODE, "old: fid: %s\n", coda_i2s(inode));
	CDEBUG(D_INODE, "directory: %s\n", coda_i2s(dir_inode));

	error = venus_link(dir_inode->i_sb, coda_i2f(inode),
			   coda_i2f(dir_inode), (const char *)name, len);

	if (error) { 
		d_drop(de);
		goto out;
	}

	coda_dir_changed(dir_inode, 0);
	atomic_inc(&inode->i_count);
	d_instantiate(de, inode);
	inode->i_nlink++;
        
out:
	CDEBUG(D_INODE, "link result %d\n",error);
	EXIT;
	return(error);
}


static int coda_symlink(struct inode *dir_inode, struct dentry *de,
			const char *symname)
{
        const char *name = de->d_name.name;
	int len = de->d_name.len;
	int symlen;
        int error=0;
        
        ENTRY;
	coda_vfs_stat.symlink++;

	if (coda_isroot(dir_inode) && coda_iscontrol(name, len))
		return -EPERM;

	symlen = strlen(symname);
	if ( symlen > CODA_MAXPATHLEN )
                return -ENAMETOOLONG;

        CDEBUG(D_INODE, "symname: %s, length: %d\n", symname, symlen);

	/*
	 * This entry is now negative. Since we do not create
	 * an inode for the entry we have to drop it. 
	 */
	d_drop(de);
	error = venus_symlink(dir_inode->i_sb, coda_i2f(dir_inode), name, len, 
			      symname, symlen);

	/* mtime is no good anymore */
	if ( !error )
		coda_dir_changed(dir_inode, 0);

        CDEBUG(D_INODE, "in symlink result %d\n",error);
        EXIT;
        return error;
}

/* destruction routines: unlink, rmdir */
int coda_unlink(struct inode *dir, struct dentry *de)
{
        int error;
	const char *name = de->d_name.name;
	int len = de->d_name.len;

	ENTRY;
	coda_vfs_stat.unlink++;

        CDEBUG(D_INODE, " %s in %s, dirino %ld\n", name , 
	       coda_i2s(dir), dir->i_ino);

        error = venus_remove(dir->i_sb, coda_i2f(dir), name, len);
        if ( error ) {
                CDEBUG(D_INODE, "upc returned error %d\n", error);
                return error;
        }

	coda_dir_changed(dir, 0);
	de->d_inode->i_nlink--;

        return 0;
}

int coda_rmdir(struct inode *dir, struct dentry *de)
{
	const char *name = de->d_name.name;
	int len = de->d_name.len;
        int error;

	ENTRY;
	coda_vfs_stat.rmdir++;

	if (!d_unhashed(de))
		return -EBUSY;
	error = venus_rmdir(dir->i_sb, coda_i2f(dir), name, len);

        if ( error ) {
                CDEBUG(D_INODE, "upc returned error %d\n", error);
                return error;
        }

	coda_dir_changed(dir, -1);
	de->d_inode->i_nlink--;
	d_delete(de);

        return 0;
}

/* rename */
static int coda_rename(struct inode *old_dir, struct dentry *old_dentry, 
		       struct inode *new_dir, struct dentry *new_dentry)
{
        const char *old_name = old_dentry->d_name.name;
        const char *new_name = new_dentry->d_name.name;
	int old_length = old_dentry->d_name.len;
	int new_length = new_dentry->d_name.len;
        int link_adjust = 0;
        int error;

	ENTRY;
	coda_vfs_stat.rename++;

        CDEBUG(D_INODE, "old: %s, (%d length), new: %s"
	       "(%d length). old:d_count: %d, new:d_count: %d\n", 
	       old_name, old_length, new_name, new_length,
	       atomic_read(&old_dentry->d_count), atomic_read(&new_dentry->d_count));

        error = venus_rename(old_dir->i_sb, coda_i2f(old_dir), 
			     coda_i2f(new_dir), old_length, new_length, 
			     (const char *) old_name, (const char *)new_name);

        if ( !error ) {
		if ( new_dentry->d_inode ) {
			if ( S_ISDIR(new_dentry->d_inode->i_mode) )
                        	link_adjust = 1;

                        coda_dir_changed(old_dir, -link_adjust);
                        coda_dir_changed(new_dir,  link_adjust);
			coda_flag_inode(new_dentry->d_inode, C_VATTR);
		} else {
			coda_flag_inode(old_dir, C_VATTR);
			coda_flag_inode(new_dir, C_VATTR);
                }
	}

	CDEBUG(D_INODE, "result %d\n", error); 

	EXIT;
	return error;
}


/* file operations for directories */
int coda_readdir(struct file *file, void *dirent,  filldir_t filldir)
{
        int result = 0;
        struct file open_file;
	struct dentry open_dentry;
	struct inode *inode=file->f_dentry->d_inode, *container;

        ENTRY;
	coda_vfs_stat.readdir++;

        if ( inode->i_mapping == &inode->i_data ) {
                CDEBUG(D_FILE, "no container inode.\n");
                return -EIO;
        }

	container = inode->i_mapping->host;

	coda_prepare_fakefile(inode, file, container, &open_file, &open_dentry);

        if ( S_ISREG(container->i_mode) ) {
                /* Venus: we must read Venus dirents from the file */
                result = coda_venus_readdir(&open_file, dirent, filldir);
        } else {
		/* potemkin case: we are handed a directory inode */
                result = vfs_readdir(&open_file, filldir, dirent);
        }

	/* we only have to restore the file position (and f_version?) */
	file->f_pos = open_file.f_pos;
	file->f_version = open_file.f_version;

        EXIT;
	return result;
}

/* grab the ext2 inode of the container file */
static int coda_inode_grab(dev_t dev, ino_t ino, struct inode **ind)
{
        struct super_block *sbptr;

        sbptr = get_super(dev);

        if ( !sbptr ) {
                printk("coda_inode_grab: coda_find_super returns NULL.\n");
                return -ENXIO;
        }
                
        *ind = NULL;
        *ind = iget(sbptr, ino);

        if ( *ind == NULL ) {
		printk("coda_inode_grab: iget(dev: %d, ino: %ld) "
		       "returns NULL.\n", dev, (long)ino);
                return -ENOENT;
        }
	CDEBUG(D_FILE, "ino: %ld, ops at %p\n", (long)ino, (*ind)->i_op);
        return 0;
}

/* ask venus to cache the file and return the inode of the container file,
   put this inode pointer in the cnode for future read/writes */
int coda_open(struct inode *i, struct file *f)
{
        ino_t ino;
	dev_t dev;
        int error = 0;
        struct inode *cont_inode = NULL, *old_container;
        unsigned short flags = f->f_flags & (~O_EXCL);
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_cred *cred;
        struct coda_inode_info *cii;

	lock_kernel();
        ENTRY;
	coda_vfs_stat.open++;

        CDEBUG(D_SPECIAL, "OPEN inode number: %ld, count %d, flags %o.\n", 
	       f->f_dentry->d_inode->i_ino, atomic_read(&f->f_dentry->d_count), flags);

	error = venus_open(i->i_sb, coda_i2f(i), coda_flags, &ino, &dev); 
	if (error) {
	        CDEBUG(D_FILE, "venus: dev %d, inode %ld, out->result %d\n",
		       dev, (long)ino, error);
		unlock_kernel();
		return error;
	}

        /* coda_upcall returns ino number of cached object, get inode */
        CDEBUG(D_FILE, "cache file dev %d, ino %ld\n", dev, (long)ino);
	error = coda_inode_grab(dev, ino, &cont_inode);
	
	if ( error || !cont_inode ){
		printk("coda_open: coda_inode_grab error %d.", error);
		if (cont_inode) 
			iput(cont_inode);
		unlock_kernel();
		return error;
	}

	CODA_ALLOC(cred, struct coda_cred *, sizeof(*cred));
	coda_load_creds(cred);
	f->private_data = cred;

	if ( i->i_mapping != &i->i_data ) {
		old_container = i->i_mapping->host;
		i->i_mapping = &i->i_data;
		iput(old_container);
	}
	i->i_mapping = cont_inode->i_mapping;

        cii = ITOC(i);
        cii->c_contcount++;

	CDEBUG(D_FILE, "result %d, coda i->i_count is %d, cii->contcount is %d for ino %ld\n", 
	       error, atomic_read(&i->i_count), cii->c_contcount, i->i_ino);
	CDEBUG(D_FILE, "cache ino: %ld, count %d, ops %p\n", 
	       cont_inode->i_ino, atomic_read(&cont_inode->i_count),
               cont_inode->i_op);
        EXIT;
	unlock_kernel();
        return 0;
}

int coda_release(struct inode *i, struct file *f)
{
	struct inode *container = NULL;
	int error = 0;
        unsigned short flags = (f->f_flags) & (~O_EXCL);
	unsigned short cflags = coda_flags_to_cflags(flags);
	struct coda_cred *cred;
        struct coda_inode_info *cii;

	lock_kernel();
        ENTRY;
	coda_vfs_stat.release++;

	cred = (struct coda_cred *)f->private_data;

	if (i->i_mapping != &i->i_data)
		container = i->i_mapping->host;

        cii = ITOC(i);
        CDEBUG(D_FILE, "RELEASE coda (ino %ld, ct %d, cc %d) cache (ino %ld, ct %d)\n",
		i->i_ino, atomic_read(&i->i_count), cii->c_contcount,
                (container ? container->i_ino : 0),
		(container ? atomic_read(&container->i_count) : -99));

        if (--cii->c_contcount == 0 && container) {
                i->i_mapping = &i->i_data;
                iput(container);
        }

	error = venus_release(i->i_sb, coda_i2f(i), cflags, cred);

	f->private_data = NULL;
	if (cred)
		CODA_FREE(cred, sizeof(*cred));

        CDEBUG(D_FILE, "coda_release: result: %d\n", error);
	unlock_kernel();
        return error;
}

/* support routines */

/* instantiate a fake file and dentry to pass to coda_venus_readdir */
static void coda_prepare_fakefile(struct inode *i, struct file *coda_file, 
				  struct inode *cont_inode,
				  struct file *cont_file,
				  struct dentry *cont_dentry)
{
	cont_file->f_dentry = cont_dentry;
	cont_file->f_dentry->d_inode = cont_inode;
	cont_file->f_pos = coda_file->f_pos;
	cont_file->f_version = coda_file->f_version;
	cont_file->f_op = cont_inode->i_fop;
	return ;
}

/* 
 * this structure is manipulated by filldir in vfs layer.
 * the count holds the remaining amount of space in the getdents buffer,
 * beyond the current_dir pointer.
 *
 * What structure is this comment referring to?? -JH
 */

/* should be big enough to hold any single directory entry */
#define DIR_BUFSIZE 2048

static int coda_venus_readdir(struct file *filp, void *getdent, 
			      filldir_t filldir)
{
        int bufsize;
	int offset = filp->f_pos; /* offset in the directory file */
	int count = 0;
	int pos = 0;      /* offset in the block we read */
	int result = 0; /* either an error or # of entries returned */
	int errfill;
        char *buff = NULL;
        struct venus_dirent *vdirent;
        int string_offset = (int) (&((struct venus_dirent *)(0))->d_name);
	int i;

        ENTRY;        

        CODA_ALLOC(buff, char *, DIR_BUFSIZE);
        if ( !buff ) { 
                printk("coda_venus_readdir: out of memory.\n");
                return -ENOMEM;
        }

        /* we use this routine to read the file into our buffer */
        bufsize = kernel_read(filp, filp->f_pos, buff, DIR_BUFSIZE);
        if ( bufsize < 0) {
                printk("coda_venus_readdir: cannot read directory %d.\n",
		       bufsize);
                result = bufsize;
                goto exit;
        }
        if ( bufsize == 0) {
                result = 0;
                goto exit;
        }
	
        /* Parse and write into user space. Filldir tells us when done! */
        CDEBUG(D_FILE, "buffsize: %d offset %d, count %d.\n", 
	       bufsize, offset, count);

	i = 0;
	result = 0; 
        while ( pos + string_offset < bufsize && i < 1024) {
                vdirent = (struct venus_dirent *) (buff + pos);

                /* test if the name is fully in the buffer */
                if ( pos + string_offset + (int) vdirent->d_namlen >= bufsize ){
			if ( result == 0 )
				printk("CODA: Invalid directory cfino: %ld\n", 
				       filp->f_dentry->d_inode->i_ino);
                        break;
                }
                /* now we are certain that we can read the entry from buff */

                /* if we don't have a null entry, copy it */
                if ( vdirent->d_fileno && vdirent->d_reclen ) {
                        int namlen  = vdirent->d_namlen;
                        off_t offs  = filp->f_pos; 
                        ino_t ino   = vdirent->d_fileno;
                        char *name  = vdirent->d_name;

			errfill = filldir(getdent,  name, namlen, 
					  offs, ino, DT_UNKNOWN); 
CDEBUG(D_FILE, "entry %d: ino %ld, namlen %d, reclen %d, type %d, pos %d, string_offs %d, name %*s, offset %d, result: %d, errfill: %d.\n", i,vdirent->d_fileno, vdirent->d_namlen, vdirent->d_reclen, vdirent->d_type, pos,  string_offset, vdirent->d_namlen, vdirent->d_name, (u_int) offs, result, errfill);
			/* errfill means no space for filling in this round */
			if ( errfill < 0 ) {
				result = 0;
				break;
			}
                        /* adjust count */
                        result++;
                }
                /* next one */
                filp->f_pos += vdirent->d_reclen;
		if ( filp->f_pos > filp->f_dentry->d_inode->i_size )
			break; 
		if ( !vdirent->d_reclen ) {
			printk("CODA: Invalid directory, cfino: %ld\n", 
			       filp->f_dentry->d_inode->i_ino);
			result = -EINVAL;
			break;
		}
                pos += (unsigned int) vdirent->d_reclen;
		i++;
        } 

       if ( i >= 1024 ) {
               printk("Repeating too much in readdir %ld\n", 
                      filp->f_dentry->d_inode->i_ino);
               result = -EINVAL;
       }

exit:
        CODA_FREE(buff, DIR_BUFSIZE);
        return result;
}

/* called when a cache lookup succeeds */
static int coda_dentry_revalidate(struct dentry *de, int flags)
{
	struct inode *inode = de->d_inode;
	struct coda_inode_info *cii;
	ENTRY;

	if (!inode)
		return 1;
	lock_kernel();
	if (coda_isroot(inode))
		goto out;
	if (is_bad_inode(inode))
		goto bad;

	cii = ITOC(de->d_inode);
	if (! (cii->c_flags & (C_PURGE | C_FLUSH)) )
		goto out;

	shrink_dcache_parent(de);

	/* propagate for a flush */
	if (cii->c_flags & C_FLUSH) 
		coda_flag_inode_children(inode, C_FLUSH);

	if (atomic_read(&de->d_count) > 1) {
		/* pretend it's valid, but don't change the flags */
		CDEBUG(D_DOWNCALL, "BOOM for: ino %ld, %s\n",
		       de->d_inode->i_ino, coda_f2s(&cii->c_fid));
		goto out;
	}

	/* clear the flags. */
	cii->c_flags &= ~(C_VATTR | C_PURGE | C_FLUSH);

bad:
	unlock_kernel();
	return 0;
out:
	unlock_kernel();
	return 1;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes.
 */
static int coda_dentry_delete(struct dentry * dentry)
{
	int flags;

	if (!dentry->d_inode) 
		return 0;

	flags = (ITOC(dentry->d_inode)->c_flags) & C_PURGE;
	if (is_bad_inode(dentry->d_inode) || flags) {
		CDEBUG(D_DOWNCALL, "bad inode, unhashing %s/%s, %ld\n", 
		       dentry->d_parent->d_name.name, dentry->d_name.name,
		       dentry->d_inode->i_ino);
		return 1;
	}
	return 0;
}



/*
 * This is called when we want to check if the inode has
 * changed on the server.  Coda makes this easy since the
 * cache manager Venus issues a downcall to the kernel when this 
 * happens 
 */
int coda_revalidate_inode(struct dentry *dentry)
{
	struct coda_vattr attr;
	int error = 0;
	int old_mode;
	ino_t old_ino;
	struct inode *inode = dentry->d_inode, *container;
	struct coda_inode_info *cii = ITOC(inode);

	ENTRY;
	CDEBUG(D_INODE, "revalidating: %*s/%*s\n", 
	       dentry->d_name.len, dentry->d_name.name,
	       dentry->d_parent->d_name.len, dentry->d_parent->d_name.name);

	lock_kernel();
	if ( cii->c_flags == 0 )
		goto ok;

	if (cii->c_flags & (C_VATTR | C_PURGE | C_FLUSH)) {
		error = venus_getattr(inode->i_sb, &(cii->c_fid), &attr);
		if ( error )
			goto return_bad_inode;

		/* this inode may be lost if:
		   - it's ino changed 
		   - type changes must be permitted for repair and
		   missing mount points.
		*/
		old_mode = inode->i_mode;
		old_ino = inode->i_ino;
		coda_vattr_to_iattr(inode, &attr);

		if ((old_mode & S_IFMT) != (inode->i_mode & S_IFMT)) {
			printk("Coda: inode %ld, fid %s changed type!\n",
			       inode->i_ino, coda_f2s(&(cii->c_fid)));
		}

		/* the following can happen when a local fid is replaced 
		   with a global one, here we lose and declare the inode bad */
		if (inode->i_ino != old_ino)
			goto return_bad_inode;
		
		if ( cii->c_flags ) 
			coda_flag_inode_children(inode, C_FLUSH);
		
		cii->c_flags &= ~(C_VATTR | C_PURGE | C_FLUSH);
	}

ok:
	unlock_kernel();
	return 0;

return_bad_inode:
	if ( inode->i_mapping != &inode->i_data ) {
		container = inode->i_mapping->host;
		inode->i_mapping = &inode->i_data;
		iput(container);
	}
	make_bad_inode(inode);
	unlock_kernel();
	return -EIO;
}

