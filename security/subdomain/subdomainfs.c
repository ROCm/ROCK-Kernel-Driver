#include <linux/version.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>
#include "subdomain.h"
#include "inline.h"

#ifdef SUBDOMAIN_FS

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define CHARUSER __user char
#else
#define CHARUSER char
#endif

/* extern from list.c */
extern struct seq_operations subdomainfs_profiles_op;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static struct dentry *sd_root_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nameidata);
#else
static struct dentry *sd_root_lookup(struct inode *dir, struct dentry *dentry);
#endif
static int sd_add_root_entry(struct dentry *root, const char *name, struct file_operations *fops, int access, struct inode **rinode, struct dentry **rdentry, int mode, int ino);
static int sd_prof_open(struct inode *inode, struct file *file);
static int sd_prof_release(struct inode *inode, struct file *file);
static ssize_t sd_version_read(struct file *file, CHARUSER *buf, size_t size, loff_t *ppos);
static ssize_t sd_control_read(struct file *file, CHARUSER *buf, size_t size, loff_t *ppos);
static ssize_t sd_control_write(struct file *file, const CHARUSER *buf, size_t size, loff_t *ppos);

/* extern from module-interface.c */
extern ssize_t sd_file_prof_add(void *, size_t);
extern ssize_t sd_file_prof_repl (void *, size_t);
extern ssize_t sd_file_prof_remove (const char *, int);
extern ssize_t sd_file_prof_debug(void *, size_t);

static ssize_t sd_profile_load(struct file *f, const char *buf,
                               size_t size, loff_t *pos);
static ssize_t sd_profile_replace(struct file *f, const char *buf,
                                  size_t size, loff_t *pos);
static ssize_t sd_profile_remove(struct file *f, const char *buf,
                                  size_t size, loff_t *pos);
static ssize_t sd_profile_debug(struct file *f, const char *buf,
                                size_t size, loff_t *pos);

static struct file_operations subdomainfs_profile_load = {
  .write = sd_profile_load
};

static struct file_operations subdomainfs_profile_replace = {
  .write = sd_profile_replace
};

static struct file_operations subdomainfs_profile_remove = {
  .write = sd_profile_remove
};

static struct file_operations subdomainfs_profile_debug = {
  .write = sd_profile_debug
};

static struct inode_operations sd_root_iops = {
	.lookup =	sd_root_lookup,
};

static struct file_operations subdomainfs_profiles_fops = {
	.open =		sd_prof_open,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	sd_prof_release,
};

static struct file_operations subdomainfs_version_fops = {
	.read = 	sd_version_read,
};

static struct file_operations subdomainfs_control_fops = {
	.read = 	sd_control_read,
	.write = 	sd_control_write,
};

const char *control_debug = "debug",
	   *control_audit = "audit",
	   *control_complain = "complain",
	   *control_owlsm = "owlsm";

/* GLOBAL FUNCTIONS */
struct inode * sd_new_inode(struct super_block *sb, int mode, unsigned long ino)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = ino;
		inode->i_mode = mode;
		inode->i_uid = inode->i_gid = 0;
		inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		switch (mode & S_IFMT) {
		case S_IFDIR:
			inode->i_op = &sd_root_iops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
			inode->i_fop = &simple_dir_operations;
#else
			inode->i_fop = &dcache_dir_ops;
#endif
			break;
		case S_IFREG:
			break;
		}
	}

	return inode;
}

int sd_fill_root(struct dentry * root)
{
	int i, parent_index;
	const int base_inode = 3,
	          max_depth = 5;
	
	struct dentry *dirstack[max_depth],
		      **dirstack_ptr;

	struct root_entry {
		const char *name;
		int mode;
		int access;
		struct file_operations *fops;
		struct inode *inode;
		struct dentry *dentry;
		int parent_index;
	} root_entries[] = {
		/* interface for obtaining list of profiles 
	 	 * currently loaded 
		 */
		{"profiles", 		S_IFREG, 0440, &subdomainfs_profiles_fops, 0, 0, 0},

		/* interface for obtaining version# of subdomain */
		{"version",  		S_IFREG, 0440, &subdomainfs_version_fops, 0, 0, 0},

		/* interface for loading/removing/replacing profiles */
		{".load",    		S_IFREG, 0640, &subdomainfs_profile_load, 0, 0, 0},
		{".replace", 		S_IFREG, 0640, &subdomainfs_profile_replace, 0, 0, 0},
		{".remove",  		S_IFREG, 0640, &subdomainfs_profile_remove, 0, 0, 0},
		{".debug",   		S_IFREG, 0640, &subdomainfs_profile_debug, 0, 0, 0},

		/* interface for setting binary config values */
		{"control",  		S_IFDIR, 0550, NULL, 0, 0, 0},
		{control_owlsm,    	S_IFREG, 0640, &subdomainfs_control_fops, 0, 0, 0},
		{control_complain, 	S_IFREG, 0640, &subdomainfs_control_fops, 0, 0, 0},
		{control_audit,    	S_IFREG, 0640, &subdomainfs_control_fops, 0, 0, 0},
		{control_debug,    	S_IFREG, 0640, &subdomainfs_control_fops, 0, 0, 0},
		{NULL,       		S_IFDIR, 0,    NULL, 0, 0, 0},
	};
		
	const int num_entries = sizeof(root_entries) / sizeof(struct root_entry);

	parent_index=-1;
	dirstack[0] = root;
	dirstack_ptr = &dirstack[0];

	/* zero out entries in table used to temp storage */
	for (i=0; i<num_entries; i++){
		root_entries[i].inode = NULL;
		root_entries[i].dentry = NULL;
		root_entries[i].parent_index=-1;
	}
		
	for (i=0; i<num_entries; i++){		
		root_entries[i].parent_index = parent_index;

		if (root_entries[i].name){
			if (sd_add_root_entry(*dirstack_ptr,
				      root_entries[i].name, 
				      root_entries[i].fops,
				      root_entries[i].access,
				      &root_entries[i].inode,
				      &root_entries[i].dentry,
				      root_entries[i].mode,
				      base_inode + i)){
				goto error;
			}


			if (root_entries[i].mode == S_IFDIR){
				if (++dirstack_ptr - dirstack >= max_depth){
					SD_ERROR("%s: Max directory depth (%d) for root_entry exceeded\n",
						__FUNCTION__, max_depth);
					goto error;
				}
				
				parent_index=i;
				*dirstack_ptr = root_entries[i].dentry;
			}
		}else{
			if (root_entries[i].mode == S_IFDIR){
				if (dirstack_ptr > dirstack){
					--dirstack_ptr;
					parent_index = root_entries[parent_index].parent_index;
				}else{
					SD_ERROR("%s: Root_entry %d invalid, stack underflow\n",
						__FUNCTION__, i);
				}
			}else{
				SD_ERROR("%s: Root_entry %d invalid, not S_IFDIR\n",
					__FUNCTION__, i);
				goto error;
			}
		}
	}

	return 0;

error:
	i = 0;
	while (root_entries[i].inode) {
		int index;

		if (root_entries[i].mode == S_IFDIR){
			if (root_entries[i].name){
				// if name, defer dir free till all member
				// files freed
				continue;
			}else{
				// !name, end of directory
				index=root_entries[i].parent_index;
			} 
		}else{
			index=i;
		}
			
		/* if inodes is set so is dentries */
		iput(root_entries[index].inode);
		dput(root_entries[index].dentry);
		i++;
	}

	return -ENOMEM;
}


/* STATIC FUNCTIONS */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static struct dentry *sd_root_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nameidata)
#else
static struct dentry *sd_root_lookup(struct inode *dir, struct dentry *dentry)
#endif
{
	d_add(dentry, NULL);
	return NULL;
}

static int sd_prof_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &subdomainfs_profiles_op);
}

static int sd_prof_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

static ssize_t sd_version_read(struct file *file, 
				CHARUSER *buf, 
				size_t size, loff_t *ppos)
{
const char *version = subdomain_version_nl();
size_t maxlen = strlen(version);
loff_t offset = *ppos;
int err;

	/* loff_t is signed */
	if (offset < 0)
		return -EFAULT;

	if (offset >= maxlen)
		return 0;

	size = min(maxlen - (size_t)offset, size);

	err = copy_to_user(buf, version + offset, size);

	if (err){
		return -EFAULT;
	}

	*ppos+=size;

	return size;
}

static size_t sd_control(int rw, struct file *file, CHARUSER *buf, size_t size , loff_t *ppos)
{
	const unsigned char *name = file->f_dentry->d_name.name;
	int *var = NULL;
	char varstr[2] = { 'X', '\n' };
	const size_t maxlen=sizeof(varstr);

	/* loff_t is signed */
	if (*ppos < 0)
		return -EFAULT;

	/* io from beginning only in one op */
	if (*ppos > 0 || size == 0){
		return 0;
	}

	if (strcmp(name, control_owlsm) == 0){
		var = &subdomain_owlsm;
	}else if (strcmp(name, control_debug) == 0){
		var = &subdomain_debug;
	}else if (strcmp(name, control_audit) == 0){
		var = &subdomain_audit;
	}else if (strcmp(name, control_complain) == 0){
		var = &subdomain_complain;
	}
	
	if (!var){
		/* shouldn't be possible */
		return	-EINVAL;
	}

	if (rw){
		/* write value */

		if (size > maxlen){
			return -ENOSPC;
		}

		size = min(maxlen, size);

		if (copy_from_user(varstr, buf, size)){
			return -EFAULT;
		}

		if (size == 1 || (size == 2 && varstr[1] == '\n')){
			if (*varstr == '0'){
				*var=0;
			}else if (*varstr == '1'){
				*var=1;
			}else{
				return -EIO;
			}
		}else{
			return -EIO;
		}

		SD_WARN("Control variable '%s' changed to %c\n",
			name, *varstr);	
	}else{
		/* read value */

		varstr[0] = *var ? '1' : '0';

		size = min(maxlen, size);

		if (copy_to_user(buf, varstr, size)){
			return -EFAULT;
		}
	}

	*ppos+=size;

	return size;
}

static ssize_t sd_control_read(struct file *file, CHARUSER *buf, size_t size, loff_t *ppos)
{
	return sd_control(0, file, buf, size, ppos);
}

static ssize_t sd_control_write(struct file *file, const CHARUSER *buf, size_t size, loff_t *ppos)
{
	return sd_control(1, file, (char*)buf, size, ppos);
}

static int sd_add_root_entry(struct dentry *root, const char *name,
		      struct file_operations *fops, int access,
		      struct inode **rinode, struct dentry **rdentry,
		      int mode,
		      int ino)
{
	struct inode *inode=NULL;
	struct dentry *dentry;
	struct super_block *sb = root->d_sb;
	struct qstr ename;
	
	ename.name = name;
	ename.len = strlen(ename.name);
	ename.hash = full_name_hash(ename.name, ename.len);
	dentry = d_alloc(root, &ename);
	if (!dentry)
	  goto error;
	inode = sd_new_inode(sb, mode | access, ino);
	if (!inode)
		goto error;
	
	/* directory ops handled in sd_new_inode */
	if ((mode & S_IFMT) == S_IFREG){
		inode->i_fop = fops;
	}
	d_add(dentry, inode);
	*rinode = inode;
	*rdentry = dentry;
	return 0;
 
error:
	if (inode)
		iput(inode);

	if (dentry)
		dput(dentry);
	return -ENOMEM;
}

/* Profile loading, replacing, removing interface for 2.6 kernels */


static ssize_t sd_profile_load(struct file *f, const char *buf,
			       size_t size, loff_t *pos)
{
	void *data;
	ssize_t error = -EFAULT;

	if (*pos != 0) {
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;
	}

	/* don't allow confined processes to load profiles.  We shouldn't
         * need this if a profile is written correctly, but just in case
         * a mistake is made, in the profile, allowing access to
	 * /subdomain/.load reject it here */
	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile addition (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = vmalloc(size);
	if (data == NULL) return -ENOMEM;


	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}

	error = sd_file_prof_add(data, size);

 out:
	vfree(data);
	return error;
}


static ssize_t sd_profile_replace(struct file *f, const char *buf,
				  size_t size, loff_t *pos)
{
	void *data;
	ssize_t error = -EFAULT;

	if (*pos != 0) {
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;
	}

	/* don't allow confined processes to replace profiles.  We shouldn't
         * need this if a profile is written correctly, but just in case
         * a mistake is made, in the profile, allowing access to
	 * /subdomain/.replace we reject it here */
	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile replacement (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = vmalloc(size);
	if (data == NULL) return -ENOMEM;

	
	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}
  
	error = sd_file_prof_repl(data, size);

 out:
	vfree(data);
	return error;
}


static ssize_t sd_profile_remove(struct file *f, const char *buf,
				  size_t size, loff_t *pos)
{
	char *data;
	ssize_t error = -EFAULT;
	
	if (*pos != 0) {
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;
	}

	/* don't allow confined processes to remove profiles.  We shouldn't
         * need this if a profile is written correctly, but just in case
         * a mistake is made, in the profile, allowing access to
	 * /subdomain/.remove we reject it here */
	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile removal (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = (char *) vmalloc(size+1);
	if (data == NULL) return -ENOMEM;

	data[size] = 0;
	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}

	error = sd_file_prof_remove((char *) data, size);

 out:
	vfree(data);
	return error;
}


/* given a name dump what the kernel sees the profile as */
static ssize_t sd_profile_debug(struct file *f, const char *buf,
				size_t size, loff_t *pos)
{
	void *data;
	ssize_t error = -EFAULT;
	
	if (*pos != 0) {
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;
	}

	/* don't allow confined processes to debug profiles.  We shouldn't
         * need this if a profile is written correctly, but just in case
         * a mistake is made, in the profile, allowing access to
	 * /subdomain/.debug we reject it here */
	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile debug (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = vmalloc(size);
	if (data == NULL) return -ENOMEM;

	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}

	error = sd_file_prof_debug(data, size);

 out:
	vfree(data);
	return error;

}
#endif /* SUBDOMAIN FS */
