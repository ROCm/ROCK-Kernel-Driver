/*
 *  attr.c
 *
 *  Copyright (C) 1996-1999 Martin von Löwis
 *  Copyright (C) 1996-1997 Régis Duchesne
 *  Copyright (C) 1998 Joseph Malicki
 *  Copyright (C) 1999 Steve Dodd
 */

#include "ntfstypes.h"
#include "struct.h"
#include "attr.h"

#include <linux/errno.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "macros.h"
#include "support.h"
#include "util.h"
#include "super.h"
#include "inode.h"

/* Look if an attribute already exists in the inode, and if not, create it */
int 
ntfs_new_attr(ntfs_inode *ino,int type,void *name,int namelen,int *pos,
			int *found, int do_search )
{
	int do_insert=0;
	int i;

	for(i=0;i<ino->attr_count;i++)
	{
		int n=min(namelen,ino->attrs[i].namelen);
		int s=ntfs_uni_strncmp(ino->attrs[i].name,name,n);
		if( do_search ) {
			/*
			 * We assume that each attribute can be uniquely 
			 * identified by inode
			 * number, attribute type and attribute name.
			 */
			if(ino->attrs[i].type==type && ino->attrs[i].namelen==namelen && !s){
				*found=1;
				*pos=i;
				return 0;
			}
		}
		/* attributes are ordered by type, then by name */
		if(ino->attrs[i].type>type || (ino->attrs[i].type==type && s==1)){
			do_insert=1;
			break;
		}
	}

	/* re-allocate space */
	if(ino->attr_count % 8 ==0)
	{
		ntfs_attribute* new;
		new = (ntfs_attribute*)ntfs_malloc((ino->attr_count+8)*
			     sizeof(ntfs_attribute));
		if( !new )
			return ENOMEM;
		if( ino->attrs ) {
			ntfs_memcpy( new, ino->attrs, ino->attr_count*sizeof(ntfs_attribute) );
			ntfs_free( ino->attrs );
		}
		ino->attrs = new;
	}
	if(do_insert)
		ntfs_memmove(ino->attrs+i+1,ino->attrs+i,(ino->attr_count-i)*
			    sizeof(ntfs_attribute));

	ino->attr_count++;
	ino->attrs[i].type=type;
	ino->attrs[i].namelen=namelen;
	ino->attrs[i].name=name;
	*pos=i;
	*found=0;
	return 0;
}

int 
ntfs_make_attr_resident(ntfs_inode *ino,ntfs_attribute *attr)
{
	int size=attr->size;
	if(size>0){
		/* FIXME: read data, free clusters */
		return EOPNOTSUPP;
	}
	attr->resident=1;
	return 0;
}

/* Store in the inode readable information about a run */
void
ntfs_insert_run(ntfs_attribute *attr,int cnum,ntfs_cluster_t cluster,int len)
{
	/* (re-)allocate space if necessary */
	if(attr->d.r.len % 8 == 0) {
		ntfs_runlist* new;
		new = ntfs_malloc((attr->d.r.len+8)*sizeof(ntfs_runlist));
		if( !new )
			return;
		if( attr->d.r.runlist ) {
			ntfs_memcpy(new, attr->d.r.runlist, attr->d.r.len
				    *sizeof(ntfs_runlist));
			ntfs_free( attr->d.r.runlist );
		}
		attr->d.r.runlist = new;
	}
	if(attr->d.r.len>cnum)
		ntfs_memmove(attr->d.r.runlist+cnum+1,attr->d.r.runlist+cnum,
			    (attr->d.r.len-cnum)*sizeof(ntfs_runlist));
	attr->d.r.runlist[cnum].cluster=cluster;
	attr->d.r.runlist[cnum].len=len;
	attr->d.r.len++;
}

/* Extends an attribute. Another run will be added if necessary,
 * but we try to extend the last run in the runlist first.
 * FIXME: what if there isn't enough contiguous space, we don't create
 * multiple runs?
 *
 * *len: the desired new length of the attr (_not_ the amount to extend by)
 */
int ntfs_extend_attr(ntfs_inode *ino, ntfs_attribute *attr, int *len,
		int flags)
{
	int error=0;
	ntfs_runlist *rl;
	int rlen;
	ntfs_cluster_t cluster;
	int clen;

	if(attr->compressed)return EOPNOTSUPP;
	if(ino->record_count>1)return EOPNOTSUPP;

	if(attr->resident) {
		error = ntfs_make_attr_nonresident(ino,attr);
		if(error)
			return error;
	}

	if( *len <= attr->allocated )
		return 0;	/* truely stupid things do sometimes happen */

	rl=attr->d.r.runlist;
	rlen=attr->d.r.len-1;

	if(rlen>=0)
		cluster=rl[rlen].cluster+rl[rlen].len;
	else
		/* no preference for allocation space */
		cluster=0;

	/* calculate the extra space we need, and round up to multiple of cluster
	 * size to get number of new clusters needed */

	clen=( (*len - attr->allocated ) + ino->vol->clustersize - 1 ) /
		ino->vol->clustersize;
	if(clen==0)
		return 0;

	/* FIXME: try to allocate smaller pieces */
	error=ntfs_allocate_clusters(ino->vol,&cluster,&clen,
				     flags|ALLOC_REQUIRE_SIZE);
	if(error)return error;
	attr->allocated += clen*ino->vol->clustersize;
	*len = attr->allocated;

	/* contiguous chunk */
	if(rlen>=0 && cluster==rl[rlen].cluster+rl[rlen].len){
		rl[rlen].len += clen;
		return 0;
	}
	ntfs_insert_run(attr,rlen+1,cluster,clen);
	return 0;
}

int
ntfs_make_attr_nonresident(ntfs_inode *ino, ntfs_attribute *attr)
{
	void *data=attr->d.data;
	int len=attr->size;
	int error,alen;
	ntfs_io io;
	attr->d.r.len=0;
	attr->d.r.runlist=0;
	attr->resident=0;
	attr->allocated=attr->initialized=0;
	alen=len;
	error=ntfs_extend_attr(ino,attr,&alen,ALLOC_REQUIRE_SIZE);
	if(error)return error;/* FIXME: On error, restore old values */
	io.fn_put=ntfs_put;
	io.fn_get=ntfs_get;
	io.param=data;
	io.size=len;
	io.do_read=0;
	return ntfs_readwrite_attr(ino,attr,0,&io);
}

int
ntfs_attr_allnonresident(ntfs_inode *ino)
{
	int i, error=0;
        ntfs_volume *vol = ino->vol;

	for (i=0; !error && i<ino->attr_count; i++)
	{
		if (ino->attrs[i].type != vol->at_security_descriptor
		    && ino->attrs[i].type != vol->at_data)
			continue;
		error = ntfs_make_attr_nonresident (ino, ino->attrs+i);
	}
	return error;
}

/* Resize the attribute to a newsize */
int ntfs_resize_attr(ntfs_inode *ino, ntfs_attribute *attr, int newsize)
{
	int error=0;
	int oldsize=attr->size;
	int clustersize=ino->vol->clustersize;
	int i,count,newlen,newcount;
	ntfs_runlist *rl;

	if(newsize==oldsize)
		return 0;
	/* modifying compressed attributes not supported yet */
	if(attr->compressed)
		/* extending is easy: just insert sparse runs */
		return EOPNOTSUPP;
	if(attr->resident){
		void *v;
		if(newsize>ino->vol->mft_recordsize){
			error=ntfs_make_attr_nonresident(ino,attr);
			if(error)return error;
			return ntfs_resize_attr(ino,attr,newsize);
		}
		v=attr->d.data;
		if(newsize){
			attr->d.data=ntfs_malloc(newsize);
			if(!attr->d.data) {
				ntfs_free(v);
				return ENOMEM;
			}
			if(newsize>oldsize)
				ntfs_bzero((char*)attr->d.data+oldsize,
					   newsize-oldsize);
			ntfs_memcpy((char*)attr->d.data,v,min(newsize,oldsize));
		}else
			attr->d.data=0;
		ntfs_free(v);
		attr->size=newsize;
		return 0;
	}
	/* non-resident attribute */
	rl=attr->d.r.runlist;
	if(newsize<oldsize){
		for(i=0,count=0;i<attr->d.r.len;i++){
			if((count+rl[i].len)*clustersize>newsize)
				break;
			count+=(int)rl[i].len;
		}
		newlen=i+1;
		/* free unused clusters in current run, unless sparse */
		newcount=count;
		if(rl[i].cluster!=MAX_CLUSTER_T){
			int rounded=newsize-count*clustersize;
			rounded=(rounded+clustersize-1)/clustersize;
			error=ntfs_deallocate_clusters(ino->vol,rl[i].cluster+rounded,
						       (int)rl[i].len-rounded);
			if(error)
				return error; /* FIXME: incomplete operation */
			rl[i].len=rounded;
			newcount=count+rounded;
		}
		/* free all other runs */
		for(i++;i<attr->d.r.len;i++)
			if(rl[i].cluster!=MAX_CLUSTER_T){
				error=ntfs_deallocate_clusters(ino->vol,rl[i].cluster,(int)rl[i].len);
				if(error)
					return error; /* FIXME: incomplete operation */
			}
		/* FIXME? free space for extra runs in memory */
		attr->d.r.len=newlen;
	}else{
		newlen=newsize;
		error=ntfs_extend_attr(ino,attr,&newlen,ALLOC_REQUIRE_SIZE);
		if(error)return error; /* FIXME: incomplete */
		newcount=newlen/clustersize;
	}
	/* fill in new sizes */
	attr->allocated = newcount*clustersize;
	attr->size = newsize;
	/* attr->initialized does not change. */
	if(!newsize)
		error=ntfs_make_attr_resident(ino,attr);
	return error;
}

int ntfs_create_attr(ntfs_inode *ino, int anum, char *aname, void *data,
	int dsize, ntfs_attribute **rattr)
{
	void *name;
	int namelen;
	int found,i;
	int error;
	ntfs_attribute *attr;
	if(dsize>ino->vol->mft_recordsize)
		/* FIXME: non-resident attributes */
		return EOPNOTSUPP;
	if(aname){
		namelen=strlen(aname);
		name=ntfs_malloc(2*namelen);
		if( !name )
			return ENOMEM;
		ntfs_ascii2uni(name,aname,namelen);
	}else{
		name=0;
		namelen=0;
	}

	error = ntfs_new_attr(ino,anum,name,namelen,&i,&found,1);
	if( error ) {
		ntfs_free( name );
		return error;
	}

	if(found){
		ntfs_free(name);
		return EEXIST;
	}
	*rattr=attr=ino->attrs+i;
	/* allocate a new number.
	   FIXME: Should this happen on inode writeback?
	   FIXME: extensions records not supported */
	error=ntfs_allocate_attr_number(ino,&i);
	if(error)
		return error;
	attr->attrno=i;

	attr->resident=1;
	attr->compressed=attr->cengine=0;
	attr->size=attr->allocated=attr->initialized=dsize;

	/* FIXME: INDEXED information should come from $AttrDef
	   Currently, only file names are indexed */
	if(anum==ino->vol->at_file_name){
		attr->indexed=1;
	}else
		attr->indexed=0;
	attr->d.data=ntfs_malloc(dsize);

	if( !attr->d.data )
		return ENOMEM;

	ntfs_memcpy(attr->d.data,data,dsize);
	return 0;
}

/* Non-resident attributes are stored in runs (intervals of clusters).
 *
 * This function stores in the inode readable information about a non-resident
 * attribute.
 */
static int 
ntfs_process_runs(ntfs_inode *ino,ntfs_attribute* attr,unsigned char *data)
{
	int startvcn,endvcn;
	int vcn,cnum;
	ntfs_cluster_t cluster;
	int len,ctype;
	startvcn = NTFS_GETU64(data+0x10);
	endvcn = NTFS_GETU64(data+0x18);

	/* check whether this chunk really belongs to the end */
	for(cnum=0,vcn=0;cnum<attr->d.r.len;cnum++)
		vcn+=attr->d.r.runlist[cnum].len;
	if(vcn!=startvcn)
	{
		ntfs_error("Problem with runlist in extended record\n");
		return -1;
	}
	if(!endvcn)
	{
		endvcn = NTFS_GETU64(data+0x28)-1; /* allocated length */
		endvcn /= ino->vol->clustersize;
	}
	data=data+NTFS_GETU16(data+0x20);
	cnum=attr->d.r.len;
	cluster=0;
	for(vcn=startvcn; vcn<=endvcn; vcn+=len)
	{
		if(ntfs_decompress_run(&data,&len,&cluster,&ctype))
			return -1;
		if(ctype)
			ntfs_insert_run(attr,cnum,-1,len);
		else
			ntfs_insert_run(attr,cnum,cluster,len);
		cnum++;
	}
	return 0;
}
  
/* Insert the attribute starting at attr in the inode ino */
int ntfs_insert_attribute(ntfs_inode *ino, unsigned char* attrdata)
{
	int i,found;
	int type;
	short int *name;
	int namelen;
	void *data;
	ntfs_attribute *attr;
	int error;

	type = NTFS_GETU32(attrdata);
	namelen = NTFS_GETU8(attrdata+9);
	/* read the attribute's name if it has one */
	if(!namelen)
		name=0;
	else
	{
		/* 1 Unicode character fits in 2 bytes */
		name=ntfs_malloc(2*namelen);
		if( !name )
			return ENOMEM;

		ntfs_memcpy(name,attrdata+NTFS_GETU16(attrdata+10),2*namelen);
	}

	error = ntfs_new_attr(ino,type,name,namelen,&i,&found,1);
	if( error ) {
		if( name ) ntfs_free( name );
		return error;
	}

	/* We can have in one inode two attributes with type 0x00000030 (File Name) 
	   and without name */
	if(found && /*FIXME*/type!=ino->vol->at_file_name)
	{
		ntfs_process_runs(ino,ino->attrs+i,attrdata);
		return 0;
 	} else if( found ) {
 		/*	Don't understand the above, but I know it leaks memory below
 			as it overwrites a found entry without freeing it. So here we
 			call ntfs_new_attr again but this time ask it to always allocate a
 			new	entry */
 		ntfs_new_attr(ino,type,name,namelen,&i,&found,0);
	}
	attr=ino->attrs+i;
	attr->resident=NTFS_GETU8(attrdata+8)==0;
	attr->compressed=NTFS_GETU16(attrdata+0xC);
	attr->attrno=NTFS_GETU16(attrdata+0xE);
  
	if(attr->resident) {
		attr->size=NTFS_GETU16(attrdata+0x10);
		data=attrdata+NTFS_GETU16(attrdata+0x14);
		attr->d.data = (void*)ntfs_malloc(attr->size);
		if( !attr->d.data )
			return ENOMEM;
		ntfs_memcpy(attr->d.data,data,attr->size);
		attr->indexed=NTFS_GETU16(attrdata+0x16);
	}else{
		attr->allocated=NTFS_GETU32(attrdata+0x28);
		attr->size=NTFS_GETU32(attrdata+0x30);
		attr->initialized=NTFS_GETU32(attrdata+0x38);
		attr->cengine=NTFS_GETU16(attrdata+0x22);
		if(attr->compressed)
			attr->compsize=NTFS_GETU32(attrdata+0x40);
		ino->attrs[i].d.r.runlist=0;
		ino->attrs[i].d.r.len=0;
		ntfs_process_runs(ino,attr,attrdata);
	}
	return 0;
}

int
ntfs_read_zero(ntfs_io *dest,int size)
{
	char *sparse=ntfs_calloc(512);
	if(!sparse)
		return ENOMEM;
	while(size){
		int i=min(size,512);
		dest->fn_put(dest,sparse,i);
		size-=i;
	}
	ntfs_free(sparse);
	return 0;
}

/* process compressed attributes */
int ntfs_read_compressed(ntfs_inode *ino, ntfs_attribute *attr, int offset,
	ntfs_io *dest)
{
	int error=0;
	int clustersize,l;
	int s_vcn,rnum,vcn,len,chunk,got,l1,offs1,copied;
	ntfs_cluster_t cluster,cl1;
	char *comp=0,*comp1;
	char *decomp=0;
	ntfs_io io;
	ntfs_runlist *rl;

	l=dest->size;
	clustersize=ino->vol->clustersize;
	/* starting cluster of potential chunk
	   there are three situations:
	   a) in a large uncompressible or sparse chunk, 
	   s_vcn is in the middle of a run
	   b) s_vcn is right on a run border
	   c) when several runs make a chunk, s_vcn is before the chunks
	*/
	s_vcn=offset/clustersize;
	/* round down to multiple of 16 */
	s_vcn &= ~15;
	rl=attr->d.r.runlist;
	for(rnum=vcn=0;rnum<attr->d.r.len && vcn+rl->len<=s_vcn;rnum++,rl++)
		vcn+=rl->len;
	if(rnum==attr->d.r.len){
		/* beyond end of file */
		/* FIXME: check allocated/initialized */
		dest->size=0;
		return 0;
	}
	io.do_read=1;
	io.fn_put=ntfs_put;
	io.fn_get=0;
	cluster=rl->cluster;
	len=rl->len;
	copied=0;
	while(l){
		chunk=0;
		if(cluster==MAX_CLUSTER_T){
			/* sparse cluster */
			int l1;
			if((len-(s_vcn-vcn)) & 15)
				ntfs_error("unexpected sparse chunk size");
			l1=chunk = min((vcn+len)*clustersize-offset,l);
			error = ntfs_read_zero(dest,l1);
			if(error)
				goto out;
		}else if(dest->do_read){
			if(!comp){
				comp=ntfs_malloc(16*clustersize);
				if(!comp){
					error=ENOMEM;
					goto out;
				}
			}
			got=0;
			/* we might need to start in the middle of a run */
			cl1=cluster+s_vcn-vcn;
			comp1=comp;
			do{
				io.param=comp1;
				l1=min(len-max(s_vcn-vcn,0),16-got);
				io.size=l1*clustersize;
				error=ntfs_getput_clusters(ino->vol,cl1,0,&io);
				if(error)goto out;
				if(l1+max(s_vcn-vcn,0)==len){
					rnum++;rl++;
					vcn+=len;
					cluster=cl1=rl->cluster;
					len=rl->len;
				}
				got+=l1;
				comp1+=l1*clustersize;
			}while(cluster!=MAX_CLUSTER_T && got<16); /* until empty run */
			chunk=16*clustersize;
			if(cluster!=MAX_CLUSTER_T || got==16)
				/* uncompressible */
				comp1=comp;
			else{
				if(!decomp){
					decomp=ntfs_malloc(16*clustersize);
					if(!decomp){
						error=ENOMEM;
						goto out;
					}
				}
				/* make sure there are null bytes
				   after the last block */
				*(ntfs_u32*)comp1=0;
				ntfs_decompress(decomp,comp,chunk);
				comp1=decomp;
			}
			offs1=offset-s_vcn*clustersize;
			chunk=min(16*clustersize-offs1,chunk);
			chunk=min(l,chunk);
			dest->fn_put(dest,comp1+offs1,chunk);
		}
		l-=chunk;
		copied+=chunk;
		offset+=chunk;
		s_vcn=offset/clustersize & ~15;
		if(l && offset>=((vcn+len)*clustersize)){
			rnum++;rl++;
			vcn+=len;
			cluster=rl->cluster;
			len=rl->len;
		}
	}
 out:
	if(comp)ntfs_free(comp);
	if(decomp)ntfs_free(decomp);
	dest->size=copied;
	return error;
}

int ntfs_write_compressed(ntfs_inode *ino, ntfs_attribute *attr, int offset,
	ntfs_io *dest)
{
	return EOPNOTSUPP;
}

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
