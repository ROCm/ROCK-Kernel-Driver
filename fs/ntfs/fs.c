/*
 *  fs.c
 *  NTFS driver for Linux 2.3.x
 *
 *  Copyright (C) 1995-1997, 1999 Martin von Löwis
 *  Copyright (C) 1996 Richard Russon
 *  Copyright (C) 1996-1997 Régis Duchesne
 *  Copyright (C) 2000, Anton Altaparmakov
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef NTFS_IN_LINUX_KERNEL
#include <linux/config.h>
#endif

#include "ntfstypes.h"
#include "struct.h"
#include "util.h"
#include "inode.h"
#include "super.h"
#include "dir.h"
#include "support.h"
#include "macros.h"
#include "sysctl.h"
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/nls.h>
#include <linux/locks.h>
#include <linux/init.h>
#include <linux/smp_lock.h>

/* Forward declarations */
static struct inode_operations ntfs_dir_inode_operations;
static struct file_operations ntfs_dir_operations;

#define ITEM_SIZE 2040

/* io functions to user space */
static void ntfs_putuser(ntfs_io* dest,void *src,ntfs_size_t len)
{
	copy_to_user(dest->param,src,len);
	dest->param+=len;
}

#ifdef CONFIG_NTFS_RW
struct ntfs_getuser_update_vm_s{
	const char *user;
	struct inode *ino;
	loff_t off;
};

static void ntfs_getuser_update_vm (void *dest, ntfs_io *src, ntfs_size_t len)
{
	struct ntfs_getuser_update_vm_s *p = src->param;
	copy_from_user (dest, p->user, len);
	p->user += len;
	p->off += len;
}
#endif

static ssize_t
ntfs_read(struct file * filp, char *buf, size_t count, loff_t *off)
{
	int error;
	ntfs_io io;
	ntfs_inode *ino=NTFS_LINO2NINO(filp->f_dentry->d_inode);

	/* inode is not properly initialized */
	if(!ino)return -EINVAL;
	ntfs_debug(DEBUG_OTHER, "ntfs_read %x,%x,%x ->",
		   (unsigned)ino->i_number,(unsigned)*off,(unsigned)count);
	/* inode has no unnamed data attribute */
	if(!ntfs_find_attr(ino,ino->vol->at_data,NULL))
		return -EINVAL;
	
	/* read the data */
	io.fn_put=ntfs_putuser;
	io.fn_get=0;
	io.param=buf;
	io.size=count;
	error=ntfs_read_attr(ino,ino->vol->at_data,NULL,*off,&io);
	if(error && !io.size)return -error;
	
	*off+=io.size;
	return io.size;
}

#ifdef CONFIG_NTFS_RW
static ssize_t
ntfs_write(struct file *filp,const char* buf,size_t count,loff_t *pos)
{
	int ret;
	ntfs_io io;
	struct inode *inode = filp->f_dentry->d_inode;
	ntfs_inode *ino = NTFS_LINO2NINO(inode);
	struct ntfs_getuser_update_vm_s param;

	if (!ino)
		return -EINVAL;
	ntfs_debug (DEBUG_LINUX, "ntfs_write %x,%x,%x ->\n",
	       (unsigned)ino->i_number, (unsigned)*pos, (unsigned)count);
	/* Allows to lock fs ro at any time */
	if (inode->i_sb->s_flags & MS_RDONLY)
		return -ENOSPC;
	if (!ntfs_find_attr(ino,ino->vol->at_data,NULL))
		return -EINVAL;

	/* Evaluating O_APPEND is the file system's job... */
	if (filp->f_flags & O_APPEND)
		*pos = inode->i_size;
	param.user = buf;
	param.ino = inode;
	param.off = *pos;
	io.fn_put = 0;
	io.fn_get = ntfs_getuser_update_vm;
	io.param = &param;
	io.size = count;
	ret = ntfs_write_attr (ino, ino->vol->at_data, NULL, *pos, &io);
	ntfs_debug (DEBUG_LINUX, "write -> %x\n", ret);
	if(ret<0)
		return -EINVAL;

	*pos += io.size;
	if (*pos > inode->i_size)
		inode->i_size = *pos;
	mark_inode_dirty (filp->f_dentry->d_inode);
	return io.size;
}
#endif

struct ntfs_filldir{
	struct inode *dir;
	filldir_t filldir;
	unsigned int type;
	ntfs_u32 ph,pl;
	void *dirent;
	char *name;
	int namelen;
};
	
static int ntfs_printcb(ntfs_u8 *entry,void *param)
{
	struct ntfs_filldir* nf=param;
	int flags=NTFS_GETU8(entry+0x51);
	int show_hidden=0;
	int length=NTFS_GETU8(entry+0x50);
	int inum=NTFS_GETU32(entry);
	int error;
#ifdef NTFS_NGT_NT_DOES_LOWER
	int i,to_lower=0;
#endif
	switch(nf->type){
	case ngt_dos:
		/* Don't display long names */
		if((flags & 2)==0)
			return 0;
		break;
	case ngt_nt:
		/* Don't display short-only names */
		switch(flags&3){
		case 2: return 0;
#ifdef NTFS_NGT_NT_DOES_LOWER
		case 3: to_lower=1;
#endif
		}
		break;
	case ngt_posix:
		break;
	case ngt_full:
		show_hidden=1;
		break;
	}
	if(!show_hidden && ((NTFS_GETU8(entry+0x48) & 2)==2)){
		ntfs_debug(DEBUG_OTHER,"Skipping hidden file\n");
		return 0;
	}
	nf->name=0;
	if(ntfs_encodeuni(NTFS_INO2VOL(nf->dir),(ntfs_u16*)(entry+0x52),
			  length,&nf->name,&nf->namelen)){
		ntfs_debug(DEBUG_OTHER,"Skipping unrepresentable file\n");
		if(nf->name)ntfs_free(nf->name);
		return 0;
	}
	/* Do not return ".", as this is faked */
	if(length==1 && *nf->name=='.')
		return 0;
#ifdef NTFS_NGT_NT_DOES_LOWER
	if(to_lower)
		for(i=0;i<nf->namelen;i++)
			/* This supports ASCII only. Since only DOS-only
			   names get converted, and since those are restricted
			   to ASCII, this should be correct */
			if(nf->name[i]>='A' && nf->name[i]<='Z')
				nf->name[i]+='a'-'A';
#endif
	nf->name[nf->namelen]=0;
	ntfs_debug(DEBUG_OTHER, "readdir got %s,len %d\n",nf->name,nf->namelen);
	/* filldir expects an off_t rather than an loff_t.
	   Hope we don't have more than 65535 index records */
	error=nf->filldir(nf->dirent,nf->name,nf->namelen,
			(nf->ph<<16)|nf->pl,inum,DT_UNKNOWN);
	ntfs_free(nf->name);
	/* Linux filldir errors are negative, other errors positive */
	return error;
}

/* readdir returns '..', then '.', then the directory entries in sequence
   As the root directory contains a entry for itself, '.' is not emulated
   for the root directory */
static int ntfs_readdir(struct file* filp, void *dirent, filldir_t filldir)
{
	struct ntfs_filldir cb;
	int error;
	struct inode *dir=filp->f_dentry->d_inode;

	ntfs_debug(DEBUG_OTHER, "ntfs_readdir ino %x mode %x\n",
	       (unsigned)dir->i_ino,(unsigned int)dir->i_mode);

	ntfs_debug(DEBUG_OTHER, "readdir: Looking for file %x dircount %d\n",
	       (unsigned)filp->f_pos,atomic_read(&dir->i_count));
	cb.pl=filp->f_pos & 0xFFFF;
	cb.ph=filp->f_pos >> 16;
	/* end of directory */
	if(cb.ph==0xFFFF){
		/* FIXME: Maybe we can return those with the previous call */
		switch(cb.pl){
		case 0: filldir(dirent,".",1,filp->f_pos,dir->i_ino,DT_DIR);
			filp->f_pos=0xFFFF0001;
			return 0;
			/* FIXME: parent directory */
		case 1: filldir(dirent,"..",2,filp->f_pos,0,DT_DIR);
			filp->f_pos=0xFFFF0002;
			return 0;
		}
		ntfs_debug(DEBUG_OTHER, "readdir: EOD\n");
		return 0;
	}
	cb.dir=dir;
	cb.filldir=filldir;
	cb.dirent=dirent;
	cb.type=NTFS_INO2VOL(dir)->ngt;
	do{
		ntfs_debug(DEBUG_OTHER,"looking for next file\n");
		error=ntfs_getdir_unsorted(NTFS_LINO2NINO(dir),&cb.ph,&cb.pl,
				   ntfs_printcb,&cb);
	}while(!error && cb.ph!=0xFFFFFFFF);
	filp->f_pos=(cb.ph<<16)|cb.pl;
	ntfs_debug(DEBUG_OTHER, "new position %x\n",(unsigned)filp->f_pos);
        /* -EINVAL is on user buffer full. This is not considered 
	   as an error by sys_getdents */
	if(error<0) 
		error=0;
	/* Otherwise (device error, inconsistent data), switch the sign */
	return -error;
}

/* Copied from vfat driver */
static int simple_getbool(char *s, int *setval)
{
	if (s) {
		if (!strcmp(s,"1") || !strcmp(s,"yes") || !strcmp(s,"true")) {
			*setval = 1;
		} else if (!strcmp(s,"0") || !strcmp(s,"no") || !strcmp(s,"false")) {
			*setval = 0;
		} else {
			return 0;
		}
	} else {
		*setval = 1;
	}
	return 1;
}

/* Parse the (re)mount options */
static int parse_options(ntfs_volume* vol,char *opt)
{
	char *value;

	vol->uid=vol->gid=0;
	vol->umask=0077;
	vol->ngt=ngt_nt;
	vol->nls_map=0;
	vol->nct=0;
	if(!opt)goto done;

	for(opt = strtok(opt,",");opt;opt=strtok(NULL,","))
	{
		if ((value = strchr(opt, '=')) != NULL)
			*value++='\0';
		if(strcmp(opt,"uid")==0)
		{
			if(!value || !*value)goto needs_arg;
			vol->uid=simple_strtoul(value,&value,0);
			if(*value){
				printk(KERN_ERR "NTFS: uid invalid argument\n");
				return 0;
			}
		}else if(strcmp(opt, "gid") == 0)
		{
			if(!value || !*value)goto needs_arg;
			vol->gid=simple_strtoul(value,&value,0);
			if(*value){
				printk(KERN_ERR "gid invalid argument\n");
				return 0;
			}
		}else if(strcmp(opt, "umask") == 0)
		{
			if(!value || !*value)goto needs_arg;
			vol->umask=simple_strtoul(value,&value,0);
			if(*value){
				printk(KERN_ERR "umask invalid argument\n");
				return 0;
			}
		}else if(strcmp(opt, "iocharset") == 0){
			if(!value || !*value)goto needs_arg;
			vol->nls_map=load_nls(value);
			vol->nct |= nct_map;
			if(!vol->nls_map){
				printk(KERN_ERR "NTFS: charset not found");
				return 0;
			}
		}else if(strcmp(opt, "posix") == 0){
			int val;
			if(!value || !*value)goto needs_arg;
			if(!simple_getbool(value,&val))
				goto needs_bool;
			vol->ngt=val?ngt_posix:ngt_nt;
		}else if(strcmp(opt,"utf8") == 0){
			int val=0;
			if(!value || !*value)
				val=1;
			else if(!simple_getbool(value,&val))
				goto needs_bool;
			if(val)
				vol->nct|=nct_utf8;
		}else if(strcmp(opt,"uni_xlate") == 0){
			int val=0;
			/* no argument: uni_vfat.
			   boolean argument: uni_vfat.
			   "2": uni.
			*/
			if(!value || !*value)
				val=1;
			else if(strcmp(value,"2")==0)
				vol->nct |= nct_uni_xlate;
			else if(!simple_getbool(value,&val))
				goto needs_bool;
			if(val)
				vol->nct |= nct_uni_xlate_vfat | nct_uni_xlate;
		}else{
			printk(KERN_ERR "NTFS: unkown option '%s'\n", opt);
			return 0;
		}
	}
	if(vol->nct & nct_utf8 & (nct_map | nct_uni_xlate)){
		printk(KERN_ERR "utf8 cannot be combined with iocharset or uni_xlate\n");
		return 0;
	}
 done:
	if((vol->nct & (nct_uni_xlate | nct_map | nct_utf8))==0)
		/* default to UTF-8 */
		vol->nct=nct_utf8;
	if(!vol->nls_map){
		vol->nls_map=load_nls_default();
		if (vol->nls_map)
			vol->nct=nct_map | (vol->nct&nct_uni_xlate);
	}
	return 1;

 needs_arg:
	printk(KERN_ERR "NTFS: %s needs an argument",opt);
	return 0;
 needs_bool:
	printk(KERN_ERR "NTFS: %s needs boolean argument",opt);
	return 0;
}
			
static struct dentry *ntfs_lookup(struct inode *dir, struct dentry *d)
{
	struct inode *res=0;
	char *item=0;
	ntfs_iterate_s walk;
	int error;
	ntfs_debug(DEBUG_NAME1, "Looking up %s in %x\n",d->d_name.name,
		   (unsigned)dir->i_ino);
	/* convert to wide string */
	error=ntfs_decodeuni(NTFS_INO2VOL(dir),(char*)d->d_name.name,
			     d->d_name.len,&walk.name,&walk.namelen);
	if(error)
		return ERR_PTR(-error);
	item=ntfs_malloc(ITEM_SIZE);
	if( !item )
		return ERR_PTR(-ENOMEM);
	/* ntfs_getdir will place the directory entry into item,
	   and the first long long is the MFT record number */
	walk.type=BY_NAME;
	walk.dir=NTFS_LINO2NINO(dir);
	walk.result=item;
	if(ntfs_getdir_byname(&walk))
	{
		res=iget(dir->i_sb,NTFS_GETU32(item));
	}
	d_add(d,res);
	ntfs_free(item);
	ntfs_free(walk.name);
	/* Always return success, the dcache will handle negative entries. */
	return NULL;
}

static struct file_operations ntfs_file_operations_nommap = {
	read:		ntfs_read,
#ifdef CONFIG_NTFS_RW
	write:		ntfs_write,
#endif
};

static struct inode_operations ntfs_inode_operations_nobmap;

#ifdef CONFIG_NTFS_RW
static int
ntfs_create(struct inode* dir,struct dentry *d,int mode)
{
	struct inode *r=0;
	ntfs_inode *ino=0;
	ntfs_volume *vol;
	int error=0;
	ntfs_attribute *si;

	r=new_inode(dir->i_sb);
	if(!r){
		error=ENOMEM;
		goto fail;
	}

	ntfs_debug(DEBUG_OTHER, "ntfs_create %s\n",d->d_name.name);
	vol=NTFS_INO2VOL(dir);
#ifdef NTFS_IN_LINUX_KERNEL
	ino=NTFS_LINO2NINO(r);
#else
	ino=ntfs_malloc(sizeof(ntfs_inode));
	if(!ino){
		error=ENOMEM;
		goto fail;
	}
	r->u.generic_ip=ino;
#endif
	error=ntfs_alloc_file(NTFS_LINO2NINO(dir),ino,(char*)d->d_name.name,
			       d->d_name.len);
	if(error)goto fail;
	error=ntfs_update_inode(ino);
	if(error)goto fail;
	error=ntfs_update_inode(NTFS_LINO2NINO(dir));
	if(error)goto fail;

	r->i_uid=vol->uid;
	r->i_gid=vol->gid;
	/* FIXME: dirty? dev? */
	/* get the file modification times from the standard information */
	si=ntfs_find_attr(ino,vol->at_standard_information,NULL);
	if(si){
		char *attr=si->d.data;
		r->i_atime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+0x18));
		r->i_ctime=ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		r->i_mtime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+8));
	}
	/* It's not a directory */
	r->i_op=&ntfs_inode_operations_nobmap;
	r->i_fop=&ntfs_file_operations_nommap,
	r->i_mode=S_IFREG|S_IRUGO;
#ifdef CONFIG_NTFS_RW
	r->i_mode|=S_IWUGO;
#endif
	r->i_mode &= ~vol->umask;

	insert_inode_hash(r);
	d_instantiate(d,r);
	return 0;
 fail:
	#ifndef NTFS_IN_LINUX_KERNEL
	if(ino)ntfs_free(ino);
	#endif
	if(r)iput(r);
	return -error;
}

static int
_linux_ntfs_mkdir(struct inode *dir, struct dentry* d, int mode)
{
	int error;
	struct inode *r = 0;
	ntfs_volume *vol;
	ntfs_inode *ino;
	ntfs_attribute *si;

	ntfs_debug (DEBUG_DIR1, "mkdir %s in %x\n",d->d_name.name, dir->i_ino);
	error = ENAMETOOLONG;
	if (d->d_name.len > /* FIXME */255)
		goto out;

	error = EIO;
	r = new_inode(dir->i_sb);
	if (!r)
		goto out;
	
	vol = NTFS_INO2VOL(dir);
#ifdef NTFS_IN_LINUX_KERNEL
	ino = NTFS_LINO2NINO(r);
#else
	ino = ntfs_malloc(sizeof(ntfs_inode));
	error = ENOMEM;
	if(!ino)
		goto out;
	r->u.generic_ip = ino;
#endif
	error = ntfs_mkdir(NTFS_LINO2NINO(dir), 
			   d->d_name.name, d->d_name.len, ino);
	if(error)
		goto out;
	r->i_uid = vol->uid;
	r->i_gid = vol->gid;
	si = ntfs_find_attr(ino,vol->at_standard_information,NULL);
	if(si){
		char *attr = si->d.data;
		r->i_atime = ntfs_ntutc2unixutc(NTFS_GETU64(attr+0x18));
		r->i_ctime = ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		r->i_mtime = ntfs_ntutc2unixutc(NTFS_GETU64(attr+8));
	}
	/* It's a directory */
	r->i_op = &ntfs_dir_inode_operations;
	r->i_fop = &ntfs_dir_operations;
	r->i_mode = S_IFDIR|S_IRUGO|S_IXUGO;
#ifdef CONFIG_NTFS_RW
	r->i_mode|=S_IWUGO;
#endif
	r->i_mode &= ~vol->umask;	
	
	insert_inode_hash(r);
	d_instantiate(d, r);
	error = 0;
 out:
 	ntfs_debug (DEBUG_DIR1, "mkdir returns %d\n", -error);
	return -error;
}
#endif

#if 0
static int 
ntfs_bmap(struct inode *ino,int block)
{
	int ret=ntfs_vcn_to_lcn(NTFS_LINO2NINO(ino),block);
	ntfs_debug(DEBUG_OTHER, "bmap of %lx,block %x is %x\n",
	       ino->i_ino,block,ret);
	return (ret==-1) ? 0:ret;
}
#endif

/* It's fscking broken. */
/* FIXME: [bm]map code is disabled until ntfs_get_block gets sorted! */
/*
static int ntfs_get_block(struct inode *inode, long block, struct buffer_head *bh, int create)
{
	BUG();
	return -1;
}

static struct file_operations ntfs_file_operations = {
	read:		ntfs_read,
	mmap:		generic_file_mmap,
#ifdef CONFIG_NTFS_RW
	write:		ntfs_write,
#endif
};

static struct inode_operations ntfs_inode_operations;
*/

static struct file_operations ntfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	ntfs_readdir,
};

static struct inode_operations ntfs_dir_inode_operations = {
	lookup:		ntfs_lookup,
#ifdef CONFIG_NTFS_RW
	create:		ntfs_create,
	mkdir:		_linux_ntfs_mkdir,
#endif
};

/*
static int ntfs_writepage(struct page *page)
{
	return block_write_full_page(page,ntfs_get_block);
}
static int ntfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,ntfs_get_block);
}
static int ntfs_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return cont_prepare_write(page,from,to,ntfs_get_block,
		&page->mapping->host->u.ntfs_i.mmu_private);
}
static int _ntfs_bmap(struct address_space *mapping, long block)
{
	return generic_block_bmap(mapping,block,ntfs_get_block);
}
struct address_space_operations ntfs_aops = {
	readpage: ntfs_readpage,
	writepage: ntfs_writepage,
	sync_page: block_sync_page,
	prepare_write: ntfs_prepare_write,
	commit_write: generic_commit_write,
	bmap: _ntfs_bmap
};
*/

/* ntfs_read_inode is called by the Virtual File System (the kernel layer that
 * deals with filesystems) when iget is called requesting an inode not already
 * present in the inode table. Typically filesystems have separate
 * inode_operations for directories, files and symlinks.
 */
static void ntfs_read_inode(struct inode* inode)
{
	ntfs_volume *vol;
	int can_mmap=0;
	ntfs_inode *ino;
	ntfs_attribute *data;
	ntfs_attribute *si;

	vol=NTFS_INO2VOL(inode);
	inode->i_mode=0;
	ntfs_debug(DEBUG_OTHER, "ntfs_read_inode %x\n",(unsigned)inode->i_ino);

	switch(inode->i_ino)
	{
		/* those are loaded special files */
	case FILE_MFT:
		ntfs_error("Trying to open MFT\n");return;
	default:
		#ifdef NTFS_IN_LINUX_KERNEL
		ino=&inode->u.ntfs_i;
		#else
		/* FIXME: check for ntfs_malloc failure */
		ino=(ntfs_inode*)ntfs_malloc(sizeof(ntfs_inode));
		inode->u.generic_ip=ino;
		#endif
		if(!ino || ntfs_init_inode(ino,
					   NTFS_INO2VOL(inode),inode->i_ino))
		{
			ntfs_debug(DEBUG_OTHER, "NTFS:Error loading inode %x\n",
			       (unsigned int)inode->i_ino);
			return;
		}
	}
	/* Set uid/gid from mount options */
	inode->i_uid=vol->uid;
	inode->i_gid=vol->gid;
	inode->i_nlink=1;
	/* Use the size of the data attribute as file size */
	data = ntfs_find_attr(ino,vol->at_data,NULL);
	if(!data)
	{
		inode->i_size=0;
		can_mmap=0;
	}
	else
	{
		inode->i_size=data->size;
		/* FIXME: once ntfs_get_block is implemented, uncomment the
		 * next line and remove the can_mmap = 0; */
		/* can_mmap=!data->resident && !data->compressed;*/
		can_mmap = 0;
	}
	/* get the file modification times from the standard information */
	si=ntfs_find_attr(ino,vol->at_standard_information,NULL);
	if(si){
		char *attr=si->d.data;
		inode->i_atime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+0x18));
		inode->i_ctime=ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		inode->i_mtime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+8));
	}
	/* if it has an index root, it's a directory */
	if(ntfs_find_attr(ino,vol->at_index_root,"$I30"))
	{
		ntfs_attribute *at;
		at = ntfs_find_attr (ino, vol->at_index_allocation, "$I30");
		inode->i_size = at ? at->size : 0;
	  
		inode->i_op=&ntfs_dir_inode_operations;
		inode->i_fop=&ntfs_dir_operations;
		inode->i_mode=S_IFDIR|S_IRUGO|S_IXUGO;
	}
	else
	{
		/* As long as ntfs_get_block() is just a call to BUG() do not
	 	 * define any [bm]map ops or we get the BUG() whenever someone
		 * runs mc or mpg123 on an ntfs partition!
		 * FIXME: Uncomment the below code when ntfs_get_block is
		 * implemented. */
		/* if (can_mmap) {
			inode->i_op = &ntfs_inode_operations;
			inode->i_fop = &ntfs_file_operations;
			inode->i_mapping->a_ops = &ntfs_aops;
			inode->u.ntfs_i.mmu_private = inode->i_size;
		} else */ {
			inode->i_op=&ntfs_inode_operations_nobmap;
			inode->i_fop=&ntfs_file_operations_nommap;
		}
		inode->i_mode=S_IFREG|S_IRUGO;
	}
#ifdef CONFIG_NTFS_RW
	if(!data || !data->compressed)
		inode->i_mode|=S_IWUGO;
#endif
	inode->i_mode &= ~vol->umask;
}

#ifdef CONFIG_NTFS_RW
static void 
ntfs_write_inode (struct inode *ino, int unused)
{
	lock_kernel();
	ntfs_debug (DEBUG_LINUX, "ntfs:write inode %x\n", ino->i_ino);
	ntfs_update_inode (NTFS_LINO2NINO (ino));
	unlock_kernel();
}
#endif

static void _ntfs_clear_inode(struct inode *ino)
{
	lock_kernel();
	ntfs_debug(DEBUG_OTHER, "ntfs_clear_inode %lx\n",ino->i_ino);
#ifdef NTFS_IN_LINUX_KERNEL
	if(ino->i_ino!=FILE_MFT)
		ntfs_clear_inode(&ino->u.ntfs_i);
#else
	if(ino->i_ino!=FILE_MFT && ino->u.generic_ip)
	{
		ntfs_clear_inode(ino->u.generic_ip);
		ntfs_free(ino->u.generic_ip);
		ino->u.generic_ip=0;
	}
#endif
	unlock_kernel();
	return;
}

/* Called when umounting a filesystem by do_umount() in fs/super.c */
static void ntfs_put_super(struct super_block *sb)
{
	ntfs_volume *vol;

	ntfs_debug(DEBUG_OTHER, "ntfs_put_super\n");

	vol=NTFS_SB2VOL(sb);

	ntfs_release_volume(vol);
	if(vol->nls_map)
		unload_nls(vol->nls_map);
#ifndef NTFS_IN_LINUX_KERNEL
	ntfs_free(vol);
#endif
	ntfs_debug(DEBUG_OTHER, "ntfs_put_super: done\n");
}

/* Called by the kernel when asking for stats */
static int ntfs_statfs(struct super_block *sb, struct statfs *sf)
{
	struct inode *mft;
	ntfs_volume *vol;
	ntfs_u64 size;
	int error;

	ntfs_debug(DEBUG_OTHER, "ntfs_statfs\n");
	vol=NTFS_SB2VOL(sb);
	sf->f_type=NTFS_SUPER_MAGIC;
	sf->f_bsize=vol->clustersize;

	error = ntfs_get_volumesize( NTFS_SB2VOL( sb ), &size );
	if( error )
		return -error;
	sf->f_blocks = size;	/* volumesize is in clusters */
	sf->f_bfree=ntfs_get_free_cluster_count(vol->bitmap);
	sf->f_bavail=sf->f_bfree;

	mft=iget(sb,FILE_MFT);
	if (!mft)
		return -EIO;
	/* So ... we lie... thus this following cast of loff_t value
	   is ok here.. */
	sf->f_files = (unsigned long)mft->i_size / vol->mft_recordsize;
	iput(mft);

	/* should be read from volume */
	sf->f_namelen=255;
	return 0;
}

/* Called when remounting a filesystem by do_remount_sb() in fs/super.c */
static int ntfs_remount_fs(struct super_block *sb, int *flags, char *options)
{
	if(!parse_options(NTFS_SB2VOL(sb), options))
		return -EINVAL;
	return 0;
}

/* Define the super block operation that are implemented */
static struct super_operations ntfs_super_operations = {
	read_inode:	ntfs_read_inode,
#ifdef CONFIG_NTFS_RW
	write_inode:	ntfs_write_inode,
#endif
	put_super:	ntfs_put_super,
	statfs:		ntfs_statfs,
	remount_fs:	ntfs_remount_fs,
	clear_inode:	_ntfs_clear_inode,
};

/* Called to mount a filesystem by read_super() in fs/super.c
 * Return a super block, the main structure of a filesystem
 *
 * NOTE : Don't store a pointer to an option, as the page containing the
 * options is freed after ntfs_read_super() returns.
 *
 * NOTE : A context switch can happen in kernel code only if the code blocks
 * (= calls schedule() in kernel/sched.c).
 */
struct super_block * ntfs_read_super(struct super_block *sb, 
				     void *options, int silent)
{
	ntfs_volume *vol;
	struct buffer_head *bh;
	int i;

	ntfs_debug(DEBUG_OTHER, "ntfs_read_super\n");

#ifdef NTFS_IN_LINUX_KERNEL
	vol = NTFS_SB2VOL(sb);
#else
	if(!(vol = ntfs_malloc(sizeof(ntfs_volume))))
		goto ntfs_read_super_dec;
	NTFS_SB2VOL(sb)=vol;
#endif
	
	if(!parse_options(vol,(char*)options))
		goto ntfs_read_super_vol;

#if 0
	/* Set to read only, user option might reset it */
	sb->s_flags |= MS_RDONLY;
#endif

	/* Assume a 512 bytes block device for now */
	set_blocksize(sb->s_dev, 512);
	/* Read the super block (boot block) */
	if(!(bh=bread(sb->s_dev,0,512))) {
		ntfs_error("Reading super block failed\n");
		goto ntfs_read_super_unl;
	}
	ntfs_debug(DEBUG_OTHER, "Done reading boot block\n");

	/* Check for 'NTFS' magic number */
	if(!IS_NTFS_VOLUME(bh->b_data)){
		ntfs_debug(DEBUG_OTHER, "Not a NTFS volume\n");
		brelse(bh);
		goto ntfs_read_super_unl;
	}

	ntfs_debug(DEBUG_OTHER, "Going to init volume\n");
	ntfs_init_volume(vol,bh->b_data);
	ntfs_debug(DEBUG_OTHER, "MFT record at cluster 0x%X\n",vol->mft_cluster);
	brelse(bh);
	NTFS_SB(vol)=sb;
	ntfs_debug(DEBUG_OTHER, "Done to init volume\n");

	/* Inform the kernel that a device block is a NTFS cluster */
	sb->s_blocksize=vol->clustersize;
	for(i=sb->s_blocksize,sb->s_blocksize_bits=0;i != 1;i>>=1)
		sb->s_blocksize_bits++;
	set_blocksize(sb->s_dev,sb->s_blocksize);
	ntfs_debug(DEBUG_OTHER, "set_blocksize\n");

	/* Allocate a MFT record (MFT record can be smaller than a cluster) */
	if(!(vol->mft=ntfs_malloc(max(vol->mft_recordsize,vol->clustersize))))
		goto ntfs_read_super_unl;

	/* Read at least the MFT record for $MFT */
	for(i=0;i<max(vol->mft_clusters_per_record,1);i++){
		if(!(bh=bread(sb->s_dev,vol->mft_cluster+i,vol->clustersize))) {
			ntfs_error("Could not read MFT record 0\n");
			goto ntfs_read_super_mft;
		}
		ntfs_memcpy(vol->mft+i*vol->clustersize,bh->b_data,vol->clustersize);
		brelse(bh);
		ntfs_debug(DEBUG_OTHER, "Read cluster %x\n",vol->mft_cluster+i);
	}

	/* Check and fixup this MFT record */
	if(!ntfs_check_mft_record(vol,vol->mft)){
		ntfs_error("Invalid MFT record 0\n");
		goto ntfs_read_super_mft;
	}

	/* Inform the kernel about which super operations are available */
	sb->s_op = &ntfs_super_operations;
	sb->s_magic = NTFS_SUPER_MAGIC;
	
	ntfs_debug(DEBUG_OTHER, "Reading special files\n");
	if(ntfs_load_special_files(vol)){
		ntfs_error("Error loading special files\n");
		goto ntfs_read_super_mft;
	}

	ntfs_debug(DEBUG_OTHER, "Getting RootDir\n");
	/* Get the root directory */
	if(!(sb->s_root=d_alloc_root(iget(sb,FILE_ROOT)))){
		ntfs_error("Could not get root dir inode\n");
		goto ntfs_read_super_mft;
	}
	ntfs_debug(DEBUG_OTHER, "read_super: done\n");
	return sb;

ntfs_read_super_mft:
	ntfs_free(vol->mft);
ntfs_read_super_unl:
ntfs_read_super_vol:
	#ifndef NTFS_IN_LINUX_KERNEL
	ntfs_free(vol);
ntfs_read_super_dec:
	#endif
	ntfs_debug(DEBUG_OTHER, "read_super: done\n");
	return NULL;
}

/* Define the filesystem
 */
static DECLARE_FSTYPE_DEV(ntfs_fs_type, "ntfs", ntfs_read_super);

static int __init init_ntfs_fs(void)
{
	/* Comment this if you trust klogd. There are reasons not to trust it
	 */
#if defined(DEBUG) && !defined(MODULE)
	console_verbose();
#endif
	printk(KERN_NOTICE "NTFS version " NTFS_VERSION "\n");
	SYSCTL(1);
	ntfs_debug(DEBUG_OTHER, "registering %s\n",ntfs_fs_type.name);
	/* add this filesystem to the kernel table of filesystems */
	return register_filesystem(&ntfs_fs_type);
}

static void __exit exit_ntfs_fs(void)
{
	SYSCTL(0);
	ntfs_debug(DEBUG_OTHER, "unregistering %s\n",ntfs_fs_type.name);
	unregister_filesystem(&ntfs_fs_type);
}

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Martin von Löwis");
MODULE_DESCRIPTION("NTFS driver");
#ifdef DEBUG
MODULE_PARM(ntdebug, "i");
MODULE_PARM_DESC(ntdebug, "Debug level");
#endif

module_init(init_ntfs_fs)
module_exit(exit_ntfs_fs)
/*
 * Local variables:
 *  c-file-style: "linux"
 * End:
 */
