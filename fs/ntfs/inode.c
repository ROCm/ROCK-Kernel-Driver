/*
 *  inode.c
 *
 *  Copyright (C) 1995-1999 Martin von Löwis
 *  Copyright (C) 1996 Albert D. Cahalan
 *  Copyright (C) 1996-1997 Régis Duchesne
 *  Copyright (C) 1998 Joseph Malicki
 *  Copyright (C) 1999 Steve Dodd
 *  Copyright (C) 2000 Anton Altaparmakov
 */

#include "ntfstypes.h"
#include "ntfsendian.h"
#include "struct.h"
#include "inode.h"

#include <linux/errno.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "macros.h"
#include "attr.h"
#include "super.h"
#include "dir.h"
#include "support.h"
#include "util.h"

typedef struct {
	int recno;
	unsigned char* record;
} ntfs_mft_record;

typedef struct {
	int size;
	int count;
	ntfs_mft_record* records;
} ntfs_disk_inode;

void
ntfs_fill_mft_header(ntfs_u8*mft,int record_size,int blocksize,
		int sequence_number)
{
	int fixup_count = record_size / blocksize + 1;
	int attr_offset = (0x2a + (2 * fixup_count) + 7) & ~7;
	int fixup_offset = 0x2a;

	NTFS_PUTU32(mft + 0x00, 0x454c4946);	     /* FILE */
	NTFS_PUTU16(mft + 0x04, 0x2a);		     /* offset to fixup */
	NTFS_PUTU16(mft + 0x06, fixup_count);	     /* Number of fixups */
	NTFS_PUTU16(mft + 0x10, sequence_number);
	NTFS_PUTU16(mft + 0x12, 1);                  /* hard link count */
	NTFS_PUTU16(mft + 0x14, attr_offset);	     /* Offset to attributes */
	NTFS_PUTU16(mft + 0x16, 1);                  /*FIXME: flags ?? */
	NTFS_PUTU32(mft + 0x18, attr_offset + 0x08);	/* In use */
	NTFS_PUTU32(mft + 0x1c, record_size);	     /* Total size */

	NTFS_PUTU16(mft + fixup_offset, 1);		/* Fixup word */
	NTFS_PUTU32(mft + attr_offset, 0xffffffff);	/* End marker */
}

/* Search in an inode an attribute by type and name */
ntfs_attribute* 
ntfs_find_attr(ntfs_inode *ino,int type,char *name)
{
	int i;
	if(!ino){
		ntfs_error("ntfs_find_attr: NO INODE!\n");
		return 0;
	}
	for(i=0;i<ino->attr_count;i++)
	{
		if(type==ino->attrs[i].type)
		{
			if(!name && !ino->attrs[i].name)
				return ino->attrs+i;
			if(name && !ino->attrs[i].name)
				return 0;
			if(!name && ino->attrs[i].name)
				return 0;
			if(ntfs_ua_strncmp(ino->attrs[i].name,name,strlen(name))==0)
				return ino->attrs+i;
		}
		if(type<ino->attrs[i].type)
			return 0;
	}
	return 0;
}

/* FIXME: need better strategy to extend the MFT */
static int 
ntfs_extend_mft(ntfs_volume *vol)
{
	/* Try to allocate at least 0.1% of the remaining disk space
	   for inodes. If the disk is almost full, make sure at least one
	   inode is requested.
	 */
	int size,rcount,error,block;
	ntfs_attribute* mdata,*bmp;
	ntfs_u8 *buf;
	ntfs_io io;

	mdata=ntfs_find_attr(vol->mft_ino,vol->at_data,0);
	/* first check whether there is uninitialized space */
	if(mdata->allocated<mdata->size+vol->mft_recordsize){
		size=ntfs_get_free_cluster_count(vol->bitmap)*vol->clustersize;
		block=vol->mft_recordsize;
		size=max(size/1000,mdata->size+vol->mft_recordsize);
		size=((size+block-1)/block)*block;
		/* require this to be a single chunk */
		error=ntfs_extend_attr(vol->mft_ino,mdata,&size,
				       ALLOC_REQUIRE_SIZE);
		/* Try again, now we have the largest available fragment */
		if(error==ENOSPC){
			/* round down to multiple of mft record size */
			size=(size/vol->mft_recordsize)*vol->mft_recordsize;
			if(!size)return ENOSPC;
			error=ntfs_extend_attr(vol->mft_ino,mdata,&size,
					       ALLOC_REQUIRE_SIZE);
		}
		if(error)
			return error;
	}
	/* even though we might have allocated more than needed,
	   we initialize only one record */
	mdata->size+=vol->mft_recordsize;
	
	/* now extend the bitmap if necessary*/
	rcount=mdata->size/vol->mft_recordsize;
	bmp=ntfs_find_attr(vol->mft_ino,vol->at_bitmap,0);
	if(bmp->size*8<rcount){ /* less bits than MFT records */
		ntfs_u8 buf[1];
		/* extend bitmap by one byte */
		error=ntfs_resize_attr(vol->mft_ino,bmp,bmp->size+1);
		if(error)return error;
		/* write the single byte */
		buf[0]=0;
		io.fn_put=ntfs_put;
		io.fn_get=ntfs_get;
		io.param=buf;
		io.size=1;
		error=ntfs_write_attr(vol->mft_ino,vol->at_bitmap,0,
				      bmp->size-1,&io);
		if(error)return error;
		if(io.size!=1)return EIO;
	}

	/* now fill in the MFT header for the new block */
	buf=ntfs_calloc(vol->mft_recordsize);
	if(!buf)return ENOMEM;
	ntfs_fill_mft_header(buf,vol->mft_recordsize,vol->blocksize,0);
	ntfs_insert_fixups(buf,vol->blocksize);
	io.param=buf;
	io.size=vol->mft_recordsize;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	error=ntfs_write_attr(vol->mft_ino,vol->at_data,0,
			      (rcount-1)*vol->mft_recordsize,&io);
	if(error)return error;
	if(io.size!=vol->mft_recordsize)return EIO;
	error=ntfs_update_inode(vol->mft_ino);
	if(error)return error;
	return 0;
}

/* Insert all attributes from the record mftno of the MFT in the inode ino */
void ntfs_insert_mft_attributes(ntfs_inode* ino,char *mft,int mftno)
{
	int i;
	char *it;
	int type,len;
	/* check for duplicate */
	for(i=0;i<ino->record_count;i++)
		if(ino->records[i]==mftno)
			return;
	/* (re-)allocate space if necessary */
	if(ino->record_count % 8==0)
	{
		int *new;
		new = ntfs_malloc((ino->record_count+8)*sizeof(int));
		if( !new )
			return;
		if( ino->records ) {
			for(i=0;i<ino->record_count;i++)
				new[i] = ino->records[i];
			ntfs_free( ino->records );
		}
		ino->records = new;
	}
	ino->records[ino->record_count]=mftno;
	ino->record_count++;
	it = mft + NTFS_GETU16(mft + 0x14);
	do{
		type=NTFS_GETU32(it);
		len=NTFS_GETU32(it+4);
		if(type!=-1) {
			/* FIXME: check ntfs_insert_attribute for failure (e.g. no mem)? */
			ntfs_insert_attribute(ino,it);
		}
		it+=len;
	}while(type!=-1); /* attribute list ends with type -1 */
}

/* Read and insert all the attributes of an 'attribute list' attribute
   Return the number of remaining bytes in *plen
*/
static int parse_attributes(ntfs_inode *ino, ntfs_u8 *alist, int *plen)
{
	char *mft;
	int mftno,l,error;
	int last_mft=-1;
	int len=*plen;
	mft=ntfs_malloc(ino->vol->mft_recordsize);
	if( !mft )
		return ENOMEM;
	while(len>8)
	{
		l=NTFS_GETU16(alist+4);
		if(l>len)break;
	        /* process an attribute description */
		mftno=NTFS_GETU32(alist+0x10); /* BUG: this is u64 */
		if(mftno!=last_mft){
			last_mft=mftno;
			/* FIXME: avoid loading record if it's 
			   already processed */
			error=ntfs_read_mft_record(ino->vol,mftno,mft);
			if(error)return error;
			ntfs_insert_mft_attributes(ino,mft,mftno);
		}
		len-=l;
		alist+=l;
	}
	ntfs_free(mft);
	*plen=len;
	return 0;
}

static void ntfs_load_attributes(ntfs_inode* ino)
{
	ntfs_attribute *alist;
	int datasize;
	int offset,len,delta;
	char *buf;
	ntfs_volume *vol=ino->vol;
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 1\n",ino->i_number);
	ntfs_insert_mft_attributes(ino,ino->attr,ino->i_number);
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 2\n",ino->i_number);
	alist=ntfs_find_attr(ino,vol->at_attribute_list,0);
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 3\n",ino->i_number);
	if(!alist)
		return;
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 4\n",ino->i_number);
	datasize=alist->size;
	if(alist->resident)
	{
		parse_attributes(ino,alist->d.data,&datasize);
		return;
	}
	buf=ntfs_malloc(1024);
	if( !buf )
		return;
	delta=0;
	for(offset=0;datasize;datasize-=len,offset+=len)
	{
		ntfs_io io;
		io.fn_put=ntfs_put;
		io.fn_get=0;
		io.param=buf+delta;
		io.size=len=min(datasize,1024-delta);
		if(ntfs_read_attr(ino,vol->at_attribute_list,0,offset,&io)){
			ntfs_error("error in load_attributes\n");
		}
		delta+=len;
		parse_attributes(ino,buf,&delta);
		if(delta)
			/* move remaining bytes to buffer start */
			ntfs_memmove(buf,buf+len-delta,delta);
	}
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 5\n",ino->i_number);
	ntfs_free(buf);
}
	
int ntfs_init_inode(ntfs_inode *ino,ntfs_volume *vol,int inum)
{
	char *buf;
	int error;

	ntfs_debug(DEBUG_FILE1, "Initializing inode %x\n",inum);
	if(!vol)
		ntfs_error("NO VOLUME!\n");
	ino->i_number=inum;
	ino->vol=vol;
	ino->attr=buf=ntfs_malloc(vol->mft_recordsize);
	if( !buf )
		return ENOMEM;
	error=ntfs_read_mft_record(vol,inum,ino->attr);
	if(error){
		ntfs_debug(DEBUG_OTHER, "init inode: %x failed\n",inum);
		return error;
	}
	ntfs_debug(DEBUG_FILE2, "Init: got mft %x\n",inum);
	ino->sequence_number=NTFS_GETU16(buf+0x10);
	ino->attr_count=0;
	ino->record_count=0;
	ino->records=0;
	ino->attrs=0;
	ntfs_load_attributes(ino);
	ntfs_debug(DEBUG_FILE2, "Init: done %x\n",inum);
	return 0;
}

void ntfs_clear_inode(ntfs_inode *ino)
{
	int i;
	if(!ino->attr){
		ntfs_error("ntfs_clear_inode: double free\n");
		return;
	}
	ntfs_free(ino->attr);
	ino->attr=0;
	ntfs_free(ino->records);
	ino->records=0;
	for(i=0;i<ino->attr_count;i++)
	{
		if(ino->attrs[i].name)
			ntfs_free(ino->attrs[i].name);
		if(ino->attrs[i].resident)
		{
			if(ino->attrs[i].d.data)
				ntfs_free(ino->attrs[i].d.data);
		}else{
			if(ino->attrs[i].d.r.runlist)
				ntfs_free(ino->attrs[i].d.r.runlist);
		}
	}
	ntfs_free(ino->attrs);
	ino->attrs=0;
}

/* Check and fixup a MFT record */
int ntfs_check_mft_record(ntfs_volume *vol,char *record)
{
	return ntfs_fixup_record(vol, record, "FILE", vol->mft_recordsize);
}

/* Return (in result) the value indicating the next available attribute 
   chunk number. Works for inodes w/o extension records only */
int ntfs_allocate_attr_number(ntfs_inode *ino, int *result)
{
	if(ino->record_count!=1)
		return EOPNOTSUPP;
	*result=NTFS_GETU16(ino->attr+0x28);
	NTFS_PUTU16(ino->attr+0x28, (*result)+1);
	return 0;
}

/* find the location of an attribute in the inode. A name of NULL indicates
   unnamed attributes. Return pointer to attribute or NULL if not found */
char *
ntfs_get_attr(ntfs_inode *ino,int attr,char *name)
{
	/* location of first attribute */
	char *it= ino->attr + NTFS_GETU16(ino->attr + 0x14);
	int type;
	int len;
	/* Only check for magic DWORD here, fixup should have happened before */
	if(!IS_MFT_RECORD(ino->attr))return 0;
	do{
		type=NTFS_GETU32(it);
		len=NTFS_GETU16(it+4);
		/* We found the attribute type. Is the name correct, too? */
		if(type==attr)
		{
			int namelen=NTFS_GETU8(it+9);
			char *name_it;
			/* match given name and attribute name if present,
			   make sure attribute name is Unicode */
			for(name_it=it+NTFS_GETU16(it+10);namelen;
			    name++,name_it+=2,namelen--)
				if(*name_it!=*name || name_it[1])break;
			if(!namelen)break;
		}
		it+=len;
	}while(type!=-1); /* attribute list end with type -1 */
	if(type==-1)return 0;
	return it;
}

int 
ntfs_get_attr_size(ntfs_inode*ino,int type,char*name)
{
	ntfs_attribute *attr=ntfs_find_attr(ino,type,name);
	if(!attr)return 0;
	return attr->size;
}
	
int 
ntfs_attr_is_resident(ntfs_inode*ino,int type,char*name)
{
	ntfs_attribute *attr=ntfs_find_attr(ino,type,name);
	if(!attr)return 0;
	return attr->resident;
}
	
/*
 * A run is coded as a type indicator, an unsigned length, and a signed cluster
 * offset.
 * . To save space, length and offset are fields of variable length. The low
 *   nibble of the type indicates the width of the length :), the high nibble
 *   the width of the offset.
 * . The first offset is relative to cluster 0, later offsets are relative to
 *   the previous cluster.
 *
 * This function decodes a run. Length is an output parameter, data and cluster
 * are in/out parameters.
 */
int ntfs_decompress_run(unsigned char **data, int *length, ntfs_cluster_t *cluster,
	int *ctype)
{
	unsigned char type=*(*data)++;
	*ctype=0;
	switch(type & 0xF)
	{
	case 1: *length=NTFS_GETU8(*data);break;
	case 2: *length=NTFS_GETU16(*data);break;
	case 3: *length=NTFS_GETU24(*data);break;
        case 4: *length=NTFS_GETU32(*data);break;
        	/* Note: cases 5-8 are probably pointless to code,
        	   since how many runs > 4GB of length are there?
        	   at the most, cases 5 and 6 are probably necessary,
        	   and would also require making length 64-bit
        	   throughout */
	default:
		ntfs_error("Can't decode run type field %x\n",type);
		return -1;
	}
	*data+=(type & 0xF);

	switch(type & 0xF0)
	{
	case 0:	   *ctype=2; break;
	case 0x10: *cluster += NTFS_GETS8(*data);break;
	case 0x20: *cluster += NTFS_GETS16(*data);break;
	case 0x30: *cluster += NTFS_GETS24(*data);break;
	case 0x40: *cluster += NTFS_GETS32(*data);break;
#if 0 /* Keep for future, in case ntfs_cluster_t ever becomes 64bit */
	case 0x50: *cluster += NTFS_GETS40(*data);break;
	case 0x60: *cluster += NTFS_GETS48(*data);break;
	case 0x70: *cluster += NTFS_GETS56(*data);break;
	case 0x80: *cluster += NTFS_GETS64(*data);break;	
#endif
	default:
		ntfs_error("Can't decode run type field %x\n",type);
		return -1;
	}
	*data+=(type >> 4);
	return 0;
}

/* Reads l bytes of the attribute (attr,name) of ino starting at offset
   on vol into buf. Returns the number of bytes read in the ntfs_io struct.
   Returns 0 on success, errno on failure */
int ntfs_readwrite_attr(ntfs_inode *ino, ntfs_attribute *attr, int offset,
	ntfs_io *dest)
{
	int rnum;
	ntfs_cluster_t cluster,s_cluster,vcn,len;
	int l,chunk,copied;
	int s_vcn;
	int clustersize;
	int error;

	clustersize=ino->vol->clustersize;
	l=dest->size;
	if(l==0)
		return 0;
	if(dest->do_read)
	{
		/* if read _starts_ beyond end of stream, return nothing */
		if(offset>=attr->size){
			dest->size=0;
			return 0;
		}

		/* if read _extends_ beyond end of stream, return as much
			initialised data as we have */
		if(offset+l>=attr->size) 
			l=dest->size=attr->size-offset;

	}else {
		/* fixed by CSA: if writing beyond end, extend attribute */

		/* if write extends beyond _allocated_ size, extend attrib */
		if (offset+l>attr->allocated) {
			error=ntfs_resize_attr(ino,attr,offset+l);
			if(error)
				return error;
		}

		/* the amount of initialised data has increased; update */
		/* FIXME: shouldn't we zero-out the section between the old
			initialised length and the write start? */
		if (offset+l > attr->initialized) {
			attr->initialized = offset+l;
			attr->size = offset+l;
		}
	}
	if(attr->resident)
	{
		if(dest->do_read)
			dest->fn_put(dest,(ntfs_u8*)attr->d.data+offset,l);
		else
			dest->fn_get((ntfs_u8*)attr->d.data+offset,dest,l);
		dest->size=l;
		return 0;
	}
	/* read uninitialized data */
	if(offset>=attr->initialized && dest->do_read)
		return ntfs_read_zero(dest,l);
	if(offset+l>attr->initialized && dest->do_read)
	{
		dest->size = chunk = offset+l - attr->initialized;
		error = ntfs_readwrite_attr(ino,attr,offset,dest);
		if(error)
			return error;		
		return ntfs_read_zero(dest,l-chunk);
	}
	if(attr->compressed){
		if(dest->do_read)
			return ntfs_read_compressed(ino,attr,offset,dest);
		else
			return ntfs_write_compressed(ino,attr,offset,dest);
	}
	vcn=0;
	s_vcn = offset/clustersize;
	for(rnum=0;rnum<attr->d.r.len && 
		    vcn+attr->d.r.runlist[rnum].len<=s_vcn;rnum++)
		vcn+=attr->d.r.runlist[rnum].len;
	if(rnum==attr->d.r.len)
		/*FIXME: should extend runlist */
		return EOPNOTSUPP;
	
	copied=0;
	while(l)
	{
		s_vcn = offset/clustersize;
		cluster=attr->d.r.runlist[rnum].cluster;
		len=attr->d.r.runlist[rnum].len;

		s_cluster = cluster+s_vcn-vcn;
			
		chunk=min((vcn+len)*clustersize-offset,l);
		dest->size=chunk;
		error=ntfs_getput_clusters(ino->vol,s_cluster,
					   offset-s_vcn*clustersize,dest);
		if(error)
		{
			ntfs_error("Read error\n");
			dest->size=copied;
			return error;
		}
		l-=chunk;
		copied+=chunk;
		offset+=chunk;
		if(l && offset>=((vcn+len)*clustersize))
		{
			rnum++;
			vcn+=len;
			cluster = attr->d.r.runlist[rnum].cluster;
			len = attr->d.r.runlist[rnum].len;
		}
	}
	dest->size=copied;
	return 0;
}

int ntfs_read_attr(ntfs_inode *ino, int type, char *name, int offset,
        ntfs_io *buf)
{
	ntfs_attribute *attr;
	buf->do_read=1;
	attr=ntfs_find_attr(ino,type,name);
	if(!attr)
		return EINVAL;
	return ntfs_readwrite_attr(ino,attr,offset,buf);
}

int ntfs_write_attr(ntfs_inode *ino, int type, char *name, int offset,
		ntfs_io *buf)
{
	ntfs_attribute *attr;
	buf->do_read=0;
	attr=ntfs_find_attr(ino,type,name);
	if(!attr)
		return EINVAL;
	return ntfs_readwrite_attr(ino,attr,offset,buf);
}

int ntfs_vcn_to_lcn(ntfs_inode *ino,int vcn)
{
	int rnum;
	ntfs_attribute *data;
	data=ntfs_find_attr(ino,ino->vol->at_data,0);
	/* It's hard to give an error code */
	if(!data)return -1;
	if(data->resident)return -1;
	if(data->compressed)return -1;
	if(data->size <= vcn*ino->vol->clustersize)return -1;


	/* For Linux, block number 0 represents a hole.
	   Hopefully, nobody will attempt to bmap $Boot. */
	if(data->initialized <= vcn*ino->vol->clustersize)
		return 0;

	for(rnum=0;rnum<data->d.r.len && 
		    vcn>=data->d.r.runlist[rnum].len;rnum++)
		vcn-=data->d.r.runlist[rnum].len;
	
	return data->d.r.runlist[rnum].cluster+vcn;
}

static int 
allocate_store(ntfs_volume *vol,ntfs_disk_inode *store,int count)
{
	int i;
	if(store->count>count)
		return 0;
	if(store->size<count){
		ntfs_mft_record* n=ntfs_malloc((count+4)*sizeof(ntfs_mft_record));
		if(!n)
			return ENOMEM;
		if(store->size){
			for(i=0;i<store->size;i++)
				n[i]=store->records[i];
			ntfs_free(store->records);
		}
		store->size=count+4;
		store->records=n;
	}
	for(i=store->count;i<count;i++){
		store->records[i].record=ntfs_malloc(vol->mft_recordsize);
		if(!store->records[i].record)
			return ENOMEM;
		store->count++;
	}
	return 0;
}

static void 
deallocate_store(ntfs_disk_inode* store)
{
	int i;
	for(i=0;i<store->count;i++)
		ntfs_free(store->records[i].record);
	ntfs_free(store->records);
	store->count=store->size=0;
	store->records=0;
}

int 
layout_runs(ntfs_attribute *attr,char* rec,int* offs,int size)
{
	int i,len,offset,coffs;
	ntfs_cluster_t cluster,rclus;
	ntfs_runlist *rl=attr->d.r.runlist;
	cluster=0;
	offset=*offs;
	for(i=0;i<attr->d.r.len;i++){
		rclus=rl[i].cluster-cluster;
		len=rl[i].len;
		rec[offset]=0;
		if(offset+8>size)
			return E2BIG; /* it might still fit, but this simplifies testing */
		if(len<0x100){
			NTFS_PUTU8(rec+offset+1,len);
			coffs=1;
		}else if(len<0x10000){
			NTFS_PUTU16(rec+offset+1,len);
			coffs=2;
		}else if(len<0x1000000){
			NTFS_PUTU24(rec+offset+1,len);
			coffs=3;
		}else{
			NTFS_PUTU32(rec+offset+1,len);
			coffs=4;
		}
    
		*(rec+offset)|=coffs++;

		if(rl[i].cluster==MAX_CLUSTER_T) /*compressed run*/
			/*nothing*/;
		else if(rclus>-0x80 && rclus<0x7F){
			*(rec+offset)|=0x10;
			NTFS_PUTS8(rec+offset+coffs,rclus);
			coffs+=1;
		}else if(rclus>-0x8000 && rclus<0x7FFF){
			*(rec+offset)|=0x20;
			NTFS_PUTS16(rec+offset+coffs,rclus);
			coffs+=2;
		}else if(rclus>-0x800000 && rclus<0x7FFFFF){
			*(rec+offset)|=0x30;
			NTFS_PUTS24(rec+offset+coffs,rclus);
			coffs+=3;
		}else
#if 0 /* In case ntfs_cluster_t ever becomes 64bit */
	       	if (rclus>-0x80000000LL && rclus<0x7FFFFFFF)
#endif
		{
			*(rec+offset)|=0x40;
			NTFS_PUTS32(rec+offset+coffs,rclus);
			coffs+=4;
		}
#if 0 /* For 64-bit ntfs_cluster_t */
		else if (rclus>-0x8000000000 && rclus<0x7FFFFFFFFF){
			*(rec+offset)|=0x50;
			NTFS_PUTS40(rec+offset+coffs,rclus);
			coffs+=5;
		}else if (rclus>-0x800000000000 && rclus<0x7FFFFFFFFFFF){
			*(rec+offset)|=0x60;
			NTFS_PUTS48(rec+offset+coffs,rclus);
			coffs+=6;
		}else if (rclus>-0x80000000000000 && rclus<0x7FFFFFFFFFFFFF){
			*(rec+offset)|=0x70;
			NTFS_PUTS56(rec+offset+coffs,rclus);
			coffs+=7;
		}else{
			*(rec+offset)|=0x80;
			NTFS_PUTS64(rec+offset+coffs,rclus);
			coffs+=8;
		}
#endif
		offset+=coffs;
		if(rl[i].cluster)
			cluster=rl[i].cluster;
	}
	if(offset>=size)
		return E2BIG;
	/* terminating null */
	*(rec+offset++)=0;
	*offs=offset;
	return 0;
}

static void 
count_runs(ntfs_attribute *attr,char *buf)
{
	ntfs_u32 first,count,last,i;
	first=0;
	for(i=0,count=0;i<attr->d.r.len;i++)
		count+=attr->d.r.runlist[i].len;
	last=first+count-1;

	NTFS_PUTU64(buf+0x10,first);
	NTFS_PUTU64(buf+0x18,last);
} 

static int
layout_attr(ntfs_attribute* attr,char*buf, int size,int *psize)
{
	int asize,error;
	if(size<10)return E2BIG;
	NTFS_PUTU32(buf,attr->type);
	/* fill in length later */
	NTFS_PUTU8(buf+8,attr->resident ? 0:1);
	NTFS_PUTU8(buf+9,attr->namelen);
	/* fill in offset to name later */
	NTFS_PUTU16(buf+0xA,0);
	NTFS_PUTU16(buf+0xC,attr->compressed);
	/* FIXME: assign attribute ID??? */
	NTFS_PUTU16(buf+0xE,attr->attrno);
	if(attr->resident){
		if(size<attr->size+0x18+attr->namelen)return E2BIG;
		asize=0x18;
		NTFS_PUTU32(buf+0x10,attr->size);
		NTFS_PUTU16(buf+0x16,attr->indexed);
		if(attr->name){
			ntfs_memcpy(buf+asize,attr->name,2*attr->namelen);
			NTFS_PUTU16(buf+0xA,asize);
			asize+=2*attr->namelen;
			asize=(asize+7) & ~7;
		}
		NTFS_PUTU16(buf+0x14,asize);
		ntfs_memcpy(buf+asize,attr->d.data,attr->size);
		asize+=attr->size;
	}else{
		/* FIXME: fragments */
		count_runs(attr,buf);
		/* offset to data is added later */
		NTFS_PUTU16(buf+0x22,attr->cengine);
		NTFS_PUTU32(buf+0x24,0);
		NTFS_PUTU64(buf+0x28,attr->allocated);
		NTFS_PUTU64(buf+0x30,attr->size);
		NTFS_PUTU64(buf+0x38,attr->initialized);
		if(attr->compressed){
			NTFS_PUTU64(buf+0x40,attr->compsize);
			asize=0x48;
		}else
			asize=0x40;
		if(attr->name){
			NTFS_PUTU16(buf+0xA,asize);
			ntfs_memcpy(buf+asize,attr->name,2*attr->namelen);
			asize+=2*attr->namelen;
			/* SRD: you whaaa?
			asize=(asize+7) & ~7;*/
		}
		/* asize points at the beginning of the data */
		NTFS_PUTU16(buf+0x20,asize);
		error=layout_runs(attr,buf,&asize,size);
		/* now asize pointes at the end of the data */
		if(error)
			return error;
	}
	asize=(asize+7) & ~7;
	NTFS_PUTU32(buf+4,asize);
	*psize=asize;
	return 0;
}
		
		

/* Try to layout ino into store. Return 0 on success,
   E2BIG if it does not fit, 
   ENOMEM if memory allocation problem,
   EOPNOTSUP if beyond our capabilities 
*/
int 
layout_inode(ntfs_inode *ino,ntfs_disk_inode *store)
{
	int offset,i;
	ntfs_attribute *attr;
	unsigned char *rec;
	int size,psize;
	int error;

	if(ino->record_count>1)
	{
		ntfs_error("layout_inode: attribute lists not supported\n");
		return EOPNOTSUPP;
	}
	error=allocate_store(ino->vol,store,1);
	if(error)
		return error;
	rec=store->records[0].record;
	size=ino->vol->mft_recordsize;
	store->records[0].recno=ino->records[0];
	/* copy header */
	offset=NTFS_GETU16(ino->attr+0x14);
	ntfs_memcpy(rec,ino->attr,offset);
	for(i=0;i<ino->attr_count;i++){
		attr=ino->attrs+i;
		error=layout_attr(attr,rec+offset,size-offset,&psize);
		if(error)return error;
		offset+=psize;
#if 0
		/* copy attribute header */
		ntfs_memcpy(rec+offset,attr->header,
			    min(sizeof(attr->header),size-offset)); /* consider overrun */
		if(attr->namelen)
			/* named attributes are added later */
			return EOPNOTSUPP;
		/* FIXME: assign attribute ID??? */
		if(attr->resident){
			asize=attr->size;
			aoffset=NTFS_GETU16(rec+offset+0x14);
			if(offset+aoffset+asize>size)
				return E2BIG;
			ntfs_memcpy(rec+offset+aoffset,attr->d.data,asize);
			next=offset+aoffset+asize;
		}else{
			count_runs(attr,rec+offset);
			aoffset=NTFS_GETU16(rec+offset+0x20);
			next=offset+aoffset;
			error=layout_runs(attr,rec,&next,size);
			if(error)
				return error;
		}
		/* SRD: umm..
		next=(next+7) & ~7; */
		/* is this setting the length? if so maybe we could get
		   away with rounding up so long as we set the length first..
		   ..except, is the length the only way to get to the next attr?
		 */
		NTFS_PUTU16(rec+offset+4,next-offset);
		offset=next;
#endif
	}
	/* terminating attribute */
	if(offset+8<size){
		NTFS_PUTU32(rec+offset,0xFFFFFFFF);
		offset+=4;
		NTFS_PUTU32(rec+offset,0);
		offset+=4;
	}else
		return E2BIG;
	NTFS_PUTU32(rec+0x18,offset);
	return 0;
}
  
int ntfs_update_inode(ntfs_inode *ino)
{
	int error;
	ntfs_disk_inode store;
	ntfs_io io;
	int i;

	store.count=store.size=0;
	store.records=0;
	error=layout_inode(ino,&store);
	if(error==E2BIG){
		error = ntfs_split_indexroot(ino);
		if(!error)
			error = layout_inode(ino,&store);
	}
	if(error == E2BIG){
		error = ntfs_attr_allnonresident(ino);
		if(!error)
			error = layout_inode(ino,&store);
	}
	if(error == E2BIG){
		/* should try:
		   introduce extension records
		   */
		ntfs_error("cannot handle saving inode %x\n",ino->i_number);
		deallocate_store(&store);
		return EOPNOTSUPP;
	}
	if(error){
		deallocate_store(&store);
		return error;
	}
	io.fn_get=ntfs_get;
	io.fn_put=0;
	for(i=0;i<store.count;i++){
		ntfs_insert_fixups(store.records[i].record,ino->vol->blocksize);
		io.param=store.records[i].record;
		io.size=ino->vol->mft_recordsize;
		/* FIXME: is this the right way? */
		error=ntfs_write_attr(
			ino->vol->mft_ino,ino->vol->at_data,0,
			store.records[i].recno*ino->vol->mft_recordsize,&io);
		if(error || io.size!=ino->vol->mft_recordsize){
			/* big trouble, partially written file */
			ntfs_error("Please unmount: write error in inode %x\n",ino->i_number);
			deallocate_store(&store);
			return error?error:EIO;
		}
	}
	return 0;
}	


void ntfs_decompress(unsigned char *dest, unsigned char *src, ntfs_size_t l)
{
	int head,comp;
	int copied=0;
	unsigned char *stop;
	int bits;
	int tag=0;
	int clear_pos;
	while(1)
	{
		head = NTFS_GETU16(src) & 0xFFF;
		/* high bit indicates that compression was performed */
		comp = NTFS_GETU16(src) & 0x8000;
		src += 2;
		stop = src+head;
		bits = 0;
		clear_pos=0;
		if(head==0)
			/* block is not used */
			return;/* FIXME: copied */
		if(!comp) /* uncompressible */
		{
			ntfs_memcpy(dest,src,0x1000);
			dest+=0x1000;
			copied+=0x1000;
			src+=0x1000;
			if(l==copied)
				return;
			continue;
		}
		while(src<=stop)
		{
			if(clear_pos>4096)
			{
				ntfs_error("Error 1 in decompress\n");
				return;
			}
			if(!bits){
				tag=NTFS_GETU8(src);
				bits=8;
				src++;
				if(src>stop)
					break;
			}
			if(tag & 1){
				int i,len,delta,code,lmask,dshift;
				code = NTFS_GETU16(src);
				src+=2;
				if(!clear_pos)
				{
					ntfs_error("Error 2 in decompress\n");
					return;
				}
				for(i=clear_pos-1,lmask=0xFFF,dshift=12;i>=0x10;i>>=1)
				{
					lmask >>= 1;
					dshift--;
				}
				delta = code >> dshift;
				len = (code & lmask) + 3;
				for(i=0; i<len; i++)
				{
					dest[clear_pos]=dest[clear_pos-delta-1];
					clear_pos++;
					copied++;
					if(copied==l)
						return;
				}
			}else{
				dest[clear_pos++]=NTFS_GETU8(src);
				src++;
				copied++;
				if(copied==l)
					return;
			}
			tag>>=1;
			bits--;
		}
		dest+=clear_pos;
	}
}

/* Caveat: No range checking in either ntfs_set_bit or ntfs_clear_bit */
void 
ntfs_set_bit (unsigned char *byte, int bit)
{
	byte += (bit >> 3);
	bit &= 7;
	*byte |= (1 << bit);
}

void 
ntfs_clear_bit (unsigned char *byte, int bit)
{
	byte += (bit >> 3);
	bit &= 7;
	*byte &= ~(1 << bit);
}

/* We have to skip the 16 metafiles and the 8 reserved entries */
static int 
ntfs_new_inode (ntfs_volume* vol,int* result)
{
	int byte,error;
	int bit;
	int size,length;
	unsigned char value;
	ntfs_u8 *buffer;
	ntfs_io io;
	ntfs_attribute *data;

	buffer=ntfs_malloc(2048);
	if(!buffer)return ENOMEM;
	io.fn_put=ntfs_put;
	io.fn_get=ntfs_get;
	io.param=buffer;
	/* FIXME: bitmaps larger than 2048 bytes */
	io.size=2048;
	error=ntfs_read_attr(vol->mft_ino,vol->at_bitmap,0,0,&io);
	if(error){
		ntfs_free(buffer);
		return error;
	}
	size=io.size;
	data=ntfs_find_attr(vol->mft_ino,vol->at_data,0);
	length=data->size/vol->mft_recordsize;

	/* SRD: start at byte 0: bits for system files _are_ already set in bitmap */
	for (byte = 0; 8*byte < length; byte++)
	{
		value = buffer[byte];
		if(value==0xFF)
			continue;
		for (bit = 0; (bit < 8) && (8*byte+bit<length); 
		     bit++, value >>= 1)
		{
			if (!(value & 1)){
				*result=byte*8+bit;
				return 0;
			}
		}
	}
	/* There is no free space.  We must first extend the MFT. */
	return ENOSPC;
}

static int 
add_mft_header (ntfs_inode *ino)
{
	unsigned char* mft;
	ntfs_volume *vol=ino->vol;
	mft=ino->attr;

	ntfs_bzero(mft, vol->mft_recordsize);
	ntfs_fill_mft_header(mft,vol->mft_recordsize,vol->blocksize,
			ino->sequence_number);
	return 0;
}

/* We need 0x48 bytes in total */
static int 
add_standard_information (ntfs_inode *ino)
{
	ntfs_time64_t now;
	char data[0x30];
	char *position=data;
	int error;
	ntfs_attribute *si;

	now = ntfs_now();
	NTFS_PUTU64(position + 0x00, now);		/* File creation */
	NTFS_PUTU64(position + 0x08, now);		/* Last modification */
	NTFS_PUTU64(position + 0x10, now);		/* Last mod for MFT */
	NTFS_PUTU64(position + 0x18, now);		/* Last access */

	NTFS_PUTU64(position + 0x20, 0x00);		/* MSDOS file perms */
	NTFS_PUTU64(position + 0x28, 0);               /* unknown */
	error=ntfs_create_attr(ino,ino->vol->at_standard_information,0,
			       data,sizeof(data),&si);

	return error;
}

static int 
add_filename (ntfs_inode* ino, ntfs_inode* dir, 
	      const unsigned char *filename, int length, ntfs_u32 flags)
{
	unsigned char   *position;
	unsigned int    size;
	ntfs_time64_t   now;
	int		count;
	int error;
	unsigned char* data;
	ntfs_attribute *fn;

	/* work out the size */
	size = 0x42 + 2 * length;
	data = ntfs_malloc(size);
	if( !data )
		return ENOMEM;
	ntfs_bzero(data,size);

	/* search for a position */
	position = data;

	NTFS_PUTINUM(position, dir);	/* Inode num of dir */

	now = ntfs_now();
	NTFS_PUTU64(position + 0x08, now);		/* File creation */
	NTFS_PUTU64(position + 0x10, now);		/* Last modification */
	NTFS_PUTU64(position + 0x18, now);		/* Last mod for MFT */
	NTFS_PUTU64(position + 0x20, now);		/* Last access */

	/* Don't know */
	NTFS_PUTU32(position+0x38, flags);

	NTFS_PUTU8(position + 0x40, length);	      /* Filename length */
	NTFS_PUTU8(position + 0x41, 0x0);	      /* only long name */

	position += 0x42;
	for (count = 0; count < length; count++)
	{
		NTFS_PUTU16(position + 2 * count, filename[count]);
	}

	error=ntfs_create_attr(ino,ino->vol->at_file_name,0,data,size,&fn);
	if(!error)
		error=ntfs_dir_add(dir,ino,fn);
	ntfs_free(data);
	return error;
}

int 
add_security (ntfs_inode* ino, ntfs_inode* dir)
{
	int error;
	char *buf;
	int size;
	ntfs_attribute* attr;
	ntfs_io io;
	ntfs_attribute *se;

	attr=ntfs_find_attr(dir,ino->vol->at_security_descriptor,0);
	if(!attr)
		return EOPNOTSUPP; /* need security in directory */
	size = attr->size;
	if(size>512)
		return EOPNOTSUPP;
	buf=ntfs_malloc(size);
	if(!buf)
		return ENOMEM;
	io.fn_get=ntfs_get;
	io.fn_put=ntfs_put;
	io.param=buf;
	io.size=size;
	error=ntfs_read_attr(dir,ino->vol->at_security_descriptor,0,0,&io);
	if(!error && io.size!=size)ntfs_error("wrong size in add_security");
	if(error){
		ntfs_free(buf);
		return error;
	}
	/* FIXME: consider ACL inheritance */
	error=ntfs_create_attr(ino,ino->vol->at_security_descriptor,
			       0,buf,size,&se);
	ntfs_free(buf);
	return error;
}

static int 
add_data (ntfs_inode* ino, unsigned char *data, int length)
{
	int error;
	ntfs_attribute *da;
	error=ntfs_create_attr(ino,ino->vol->at_data,0,data,length,&da);
	return error;
}

/* We _could_ use 'dir' to help optimise inode allocation */
int ntfs_alloc_inode (ntfs_inode *dir, ntfs_inode *result, 
		      const char *filename, int namelen, ntfs_u32 flags)
{
	ntfs_io io;
	int error;
	ntfs_u8 buffer[2];
	ntfs_volume* vol=dir->vol;
	int byte,bit;

	error=ntfs_new_inode (vol,&(result->i_number));
	if(error==ENOSPC){
		error=ntfs_extend_mft(vol);
		if(error)return error;
		error=ntfs_new_inode(vol,&(result->i_number));
	}
	if(error){
		ntfs_error ("ntfs_get_empty_inode: no free inodes\n");
		return error;
	}
	byte=result->i_number/8;
	bit=result->i_number & 7;

	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	io.param = buffer;
	io.size=1;
	/* set a single bit */
	error=ntfs_read_attr(vol->mft_ino,vol->at_bitmap,0,byte,&io);
	if(error)return error;
	if(io.size!=1)
		return EIO;
	ntfs_set_bit (buffer, bit);
	io.param = buffer;
	io.size = 1;
 	error = ntfs_write_attr (vol->mft_ino, vol->at_bitmap, 0, byte, &io);
	if(error)return error;
	if (io.size != 1)
		return EIO;
	/*FIXME: Should change MFT on disk
	  error=ntfs_update_inode(vol->mft_ino);
	  if(error)return error;
	  */
	/* get the sequence number */
	io.param = buffer;
	io.size = 2;
	error = ntfs_read_attr(vol->mft_ino, vol->at_data, 0, 
			       result->i_number*vol->mft_recordsize+0x10,&io);
	if(error)
		return error;
	result->sequence_number=NTFS_GETU16(buffer)+1;
	result->vol=vol;
	result->attr=ntfs_malloc(vol->mft_recordsize);
	if( !result->attr )
		return ENOMEM;
	result->attr_count=0;
	result->attrs=0;
	result->record_count=1;
	result->records=ntfs_malloc(8*sizeof(int));
	if( !result->records ) {
		ntfs_free( result->attr );
		result->attr = 0;
		return ENOMEM;
	}
	result->records[0]=result->i_number;
	error=add_mft_header(result);
	if(error)
		return error;
	error=add_standard_information(result);
	if(error)
		return error;
	error=add_filename(result,dir,filename,namelen,flags);
	if(error)
		return error;
	error=add_security(result,dir);
	/*FIXME: check error */
	return 0;
}

int
ntfs_alloc_file(ntfs_inode *dir, ntfs_inode *result, char *filename,
		int namelen)
{
	int error = ntfs_alloc_inode(dir,result,filename,namelen,0);
	if(error)
		return error;
	error = add_data(result,0,0);
	return error;
}

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
