/*
 *  dir.c
 *
 *  Copyright (C) 1995-1997, 1999 Martin von Löwis
 *  Copyright (C) 1999 Steve Dodd
 *  Copyright (C) 1999 Joseph Malicki
 */

#include "ntfstypes.h"
#include "struct.h"
#include "dir.h"
#include "macros.h"

#include <linux/errno.h>
#include "super.h"
#include "inode.h"
#include "attr.h"
#include "support.h"
#include "util.h"

static char I30[]="$I30";

/* An index record should start with INDX, and the last word in each
   block should contain the check value. If it passes, the original
   values need to be restored */
int ntfs_check_index_record(ntfs_inode *ino, char *record)
{
	return ntfs_fixup_record(ino->vol, record, "INDX", 
				 ino->u.index.recordsize);
}

static inline int ntfs_is_top(ntfs_u64 stack)
{
	return stack==14;
}

static int ntfs_pop(ntfs_u64 *stack)
{
	static int width[16]={1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,-1};
	int res=-1;
	switch(width[*stack & 15])
	{
	case 1:res=(int)((*stack&15)>>1);
		*stack>>=4;
		break;
	case 2:res=(int)(((*stack&63)>>2)+7);
		*stack>>=6;
		break;
	case 3:res=(int)(((*stack & 255)>>3)+23);
		*stack>>=8;
		break;
	case 4:res=(int)(((*stack & 1023)>>4)+55);
		*stack>>=10;
		break;
	default:ntfs_error("Unknown encoding\n");
	}
	return res;
}

static inline unsigned int ntfs_top(void)
{
	return 14;
}

static ntfs_u64 ntfs_push(ntfs_u64 stack,int i)
{
	if(i<7)return (stack<<4)|(i<<1);
	if(i<23)return (stack<<6)|((i-7)<<2)|1;
	if(i<55)return (stack<<8)|((i-23)<<3)|3;
	if(i<120)return (stack<<10)|((i-55)<<4)|7;
	ntfs_error("Too many entries\n");
	return ~((ntfs_u64)0);
}

#if 0
static void ntfs_display_stack(ntfs_u64 stack)
{
	while(!ntfs_is_top(stack))
	{
		printf("%d ",ntfs_pop(&stack));
	}
	printf("\n");
}
#endif

/* True if the entry points to another block of entries */
static inline int ntfs_entry_has_subnodes(char* entry)
{
	return (int)NTFS_GETU8(entry+12)&1;
}

/* True if it is not the 'end of dir' entry */
static inline int ntfs_entry_is_used(char* entry)
{
	return (int)(NTFS_GETU8(entry+12)&2)==0;
}

static int ntfs_allocate_index_block(ntfs_iterate_s *walk)
{
	ntfs_attribute *allocation=0,*bitmap=0;
	int error,size,i,bit;
	ntfs_u8 *bmap;
	ntfs_io io;
	ntfs_volume *vol=walk->dir->vol;

	/* check for allocation attribute */
	allocation=ntfs_find_attr(walk->dir,vol->at_index_allocation,I30);
	if(!allocation){
		ntfs_u8 bmp[8];
		/* create index allocation attribute */
		error=ntfs_create_attr(walk->dir,vol->at_index_allocation,I30,
				       0,0,&allocation);
		if(error)return error;
		ntfs_bzero(bmp,sizeof(bmp));
		error=ntfs_create_attr(walk->dir,vol->at_bitmap,I30,
				       bmp,sizeof(bmp),&bitmap);
		if(error)return error;
	}else
		bitmap=ntfs_find_attr(walk->dir,vol->at_bitmap,I30);
	if(!bitmap){
		ntfs_error("Directory w/o bitmap\n");
		return EINVAL;
	}
	size=bitmap->size;
	bmap=ntfs_malloc(size);
	if(!bmap)return ENOMEM;
	io.fn_put=ntfs_put;
	io.fn_get=ntfs_get;
	io.param=bmap;
	io.size=size;
	error=ntfs_read_attr(walk->dir,vol->at_bitmap,I30,0,&io);
	if(error){
		ntfs_free(bmap);
		return error;
	}
	if(io.size!=size){
		ntfs_free(bmap);
		return EIO;
	}
	
	/* allocate a bit */
	for(i=bit=0;i<size;i++){
		if(bmap[i]==0xFF)continue;
		for(bit=0;bit<8;bit++)
			if(((bmap[i]>>bit) & 1) == 0)
				break;
		if(bit!=8)break;
	}
	if(i==size)
		/* FIXME: extend bitmap */
		return EOPNOTSUPP;
	walk->newblock=(i*8+bit)*walk->dir->u.index.clusters_per_record;
	bmap[i]|= 1<<bit;
	io.param=bmap;
	io.size=size;
	error=ntfs_write_attr(walk->dir,vol->at_bitmap,I30,0,&io);
	if(error || io.size!=size){
		ntfs_free(bmap);
		return error?error:EIO;
	}
	ntfs_free(bmap);

	/* check whether record is out of allocated range */
	size=allocation->size;
	if(walk->newblock * vol->clustersize >= size){
		/* build index record */
		int s1=walk->dir->u.index.recordsize;
		int nr_fix = s1/vol->blocksize+1;
		int hsize;
		char *record=ntfs_malloc(s1);
		if( !record )
			return ENOMEM;
		ntfs_bzero(record,s1);
		/* magic */
		ntfs_memcpy(record,"INDX",4);
		/* offset to fixups */
		NTFS_PUTU16(record+4,0x28);
		/* number of fixups */
		NTFS_PUTU16(record+6,nr_fix);
		/* FIXME: log file number */
		/* VCN of buffer */
		NTFS_PUTU64(record+0x10,walk->newblock);
		/* header size. */
		hsize = 0x10+2*nr_fix;
		hsize = (hsize+7) & ~7; /* Align. */
		NTFS_PUTU16(record+0x18, hsize);
		/* total size of record */
		NTFS_PUTU32(record+0x20,s1-0x18);
		/* Writing the data will extend the attribute. */
		io.param=record;
		io.size=s1;
		io.do_read=0;
		error=ntfs_readwrite_attr(walk->dir, allocation, size, &io);
		if(error || io.size!=s1){
			ntfs_free(record);
			return error?error:EIO;
		}
		ntfs_free(record);
	}

	return 0;
}

/* Write an index block (root or allocation) back to storage.
   used is the total number of bytes in buf, including all headers. */

static int ntfs_index_writeback(ntfs_iterate_s *walk, ntfs_u8 *buf, int block,
				int used)
{
	ntfs_io io;
	int error;
	ntfs_attribute *a;
	ntfs_volume *vol = walk->dir->vol;
	
	io.fn_put=0;
	io.fn_get=ntfs_get;
	io.param=buf;
	if(block==-1){
		NTFS_PUTU16(buf+0x14,used-0x10);
		/* 0x18 is a copy thereof */
		NTFS_PUTU16(buf+0x18,used-0x10);
		io.size=used;
		error=ntfs_write_attr(walk->dir,vol->at_index_root,
				      I30,0,&io);
		if(error)return error;
		if(io.size!=used)return EIO;
		/* shrink if necessary */
		a = ntfs_find_attr(walk->dir, vol->at_index_root, I30);
		ntfs_resize_attr(walk->dir, a, used);
	}else{
		NTFS_PUTU16(buf+0x1C,used-0x18);
		ntfs_insert_fixups(buf,vol->blocksize);
		io.size=walk->dir->u.index.recordsize;
		error=ntfs_write_attr(walk->dir,vol->at_index_allocation,I30,
				      block*vol->clustersize,
				      &io);
		if(error)return error;
		if(io.size!=walk->dir->u.index.recordsize)
			return EIO;
	}
	return 0;
}

static int ntfs_split_record(ntfs_iterate_s *walk, char *start, int bsize,
	int usize)
{
	char *entry,*prev;
	ntfs_u8 *newbuf=0,*middle=0;
	int error,othersize,mlen;
	ntfs_io io;
	ntfs_volume *vol=walk->dir->vol;
	int oldblock;

	error=ntfs_allocate_index_block(walk);
	if(error)
		return error;
	/* This should not happen */
	if(walk->block == -1){
		ntfs_error("Trying to split root");
		return EOPNOTSUPP;
	}
	entry = start+NTFS_GETU16(start+0x18)+0x18; 
	for(prev=entry; entry-start<usize/2; entry += NTFS_GETU16(entry+8))
		prev=entry;

	newbuf=ntfs_malloc(vol->index_recordsize);
	if(!newbuf)
		return ENOMEM;
	io.fn_put=ntfs_put;
	io.fn_get=ntfs_get;
	io.param=newbuf;
	io.size=vol->index_recordsize;
	/* read in old header. FIXME: reading everything is overkill */
	error=ntfs_read_attr(walk->dir,vol->at_index_allocation,I30,
			     walk->newblock*vol->clustersize,&io);
	if(error)goto out;
	if(io.size!=vol->index_recordsize){
		error=EIO;
		goto out;
	}
	/* FIXME: adjust header */
	/* copy everything from entry to new block */
	othersize=usize-(entry-start);
	ntfs_memcpy(newbuf+NTFS_GETU16(newbuf+0x18)+0x18,entry,othersize);
	/* Copy flags. */
	NTFS_PUTU32(newbuf+0x24, NTFS_GETU32(start+0x24));
	error=ntfs_index_writeback(walk,newbuf,walk->newblock,
				   othersize+NTFS_GETU16(newbuf+0x18)+0x18);
	if(error)goto out;

	/* move prev to walk */
	mlen=NTFS_GETU16(prev+0x8);
	/* Remember old child node. */
	if(ntfs_entry_has_subnodes(prev))
		oldblock = NTFS_GETU32(prev+mlen-8);
	else
		oldblock = -1;
	/* allow for pointer to subnode */
	middle=ntfs_malloc(ntfs_entry_has_subnodes(prev)?mlen:mlen+8);
	if(!middle){
		error=ENOMEM;
		goto out;
	}
	ntfs_memcpy(middle,prev,mlen);
	/* set has_subnodes flag */
	NTFS_PUTU8(middle+0xC, NTFS_GETU8(middle+0xC) | 1);
	/* middle entry points to block, parent entry will point to newblock */
	NTFS_PUTU64(middle+mlen-8,walk->block);
	if(walk->new_entry)
		ntfs_error("entry not reset");
	walk->new_entry=middle;
	walk->u.flags|=ITERATE_SPLIT_DONE;
	/* Terminate old block. */
	othersize = usize-(prev-start);
	NTFS_PUTU64(prev, 0);
	if(oldblock==-1){
		NTFS_PUTU32(prev+8, 0x10);
		NTFS_PUTU32(prev+0xC, 2);
		othersize += 0x10;
	}else{
		NTFS_PUTU32(prev+8, 0x18);
		NTFS_PUTU32(prev+0xC, 3);
		NTFS_PUTU64(prev+0x10, oldblock);
		othersize += 0x18;
	}
	/* write back original block */
	error=ntfs_index_writeback(walk,start,walk->block,othersize);
 out:
	if(newbuf)ntfs_free(newbuf);
	if(middle)ntfs_free(middle);
	return error;
}

static int ntfs_dir_insert(ntfs_iterate_s *walk, char *start, char* entry)
{
	int blocksize,usedsize,error,offset;
	int do_split=0;
	offset=entry-start;
	if(walk->block==-1){ /*index root */
		blocksize=walk->dir->vol->mft_recordsize;
		usedsize=NTFS_GETU16(start+0x14)+0x10;
	}else{
		blocksize=walk->dir->u.index.recordsize;
		usedsize=NTFS_GETU16(start+0x1C)+0x18;
	}
	if(usedsize+walk->new_entry_size > blocksize){
		char* s1=ntfs_malloc(blocksize+walk->new_entry_size);
		if(!s1)return ENOMEM;
		ntfs_memcpy(s1,start,usedsize);
		do_split=1;
		/* adjust entry to s1 */
		entry=s1+(entry-start);
		start=s1;
	}
	ntfs_memmove(entry+walk->new_entry_size,entry,usedsize-offset);
	ntfs_memcpy(entry,walk->new_entry,walk->new_entry_size);
	usedsize+=walk->new_entry_size;
	ntfs_free(walk->new_entry);
	walk->new_entry=0;
	if(do_split){
		error=ntfs_split_record(walk,start,blocksize,usedsize);
		ntfs_free(start);
	}else{
		error=ntfs_index_writeback(walk,start,walk->block,usedsize);
		if(error)return error;
	}
	return 0;
}

/* Try to split INDEX_ROOT attributes. Return E2BIG if nothing changed. */

int
ntfs_split_indexroot(ntfs_inode *ino)
{
	ntfs_attribute *ra;
	ntfs_u8 *root=0, *index=0;
	ntfs_io io;
	int error, off, i, bsize, isize;
	ntfs_iterate_s walk;

	ra = ntfs_find_attr(ino, ino->vol->at_index_root, I30);
	if(!ra)
		return E2BIG;
	bsize = ino->vol->mft_recordsize;
	root = ntfs_malloc(bsize);
	if(!root)
		return E2BIG;
	io.fn_put = ntfs_put;
	io.param = root;
	io.size = bsize;
	error = ntfs_read_attr(ino, ino->vol->at_index_root, I30, 0, &io);
	if(error)
		goto out;
	off = 0x20;
	/* Count number of entries. */
	for(i = 0; ntfs_entry_is_used(root+off); i++)
		off += NTFS_GETU16(root+off+8);
	if(i<=2){
		/* We don't split small index roots. */
		error = E2BIG;
		goto out;
	}
	index = ntfs_malloc(ino->vol->index_recordsize);
	if(!index) {
		error = ENOMEM; goto out;
	}
	walk.dir = ino;
	walk.block = -1;
	walk.result = walk.new_entry = 0;
	walk.name = 0;
	error = ntfs_allocate_index_block(&walk);
	if(error)
		goto out;

	/* Write old root to new index block. */
	io.param = index;
	io.size = ino->vol->index_recordsize;
	error = ntfs_read_attr(ino, ino->vol->at_index_allocation, I30,
			       walk.newblock*ino->vol->clustersize, &io);
	if(error)
		goto out;
	isize = NTFS_GETU16(root+0x18) - 0x10;
	ntfs_memcpy(index+NTFS_GETU16(index+0x18)+0x18, root+0x20, isize);
	/* Copy flags. */
	NTFS_PUTU32(index+0x24, NTFS_GETU32(root+0x1C));

	error = ntfs_index_writeback(&walk, index, walk.newblock, 
				     isize+NTFS_GETU16(index+0x18)+0x18);
	if(error)
		goto out;

	/* Mark root as split. */
	NTFS_PUTU32(root+0x1C, 1);
	/* Truncate index root. */
	NTFS_PUTU64(root+0x20, 0);
	NTFS_PUTU32(root+0x28, 0x18);
	NTFS_PUTU32(root+0x2C, 3);
	NTFS_PUTU64(root+0x30, walk.newblock);
	error = ntfs_index_writeback(&walk,root,-1,0x38);
 out:
	ntfs_free(root);
	ntfs_free(index);
	return error;
}

/* The entry has been found. Copy the result in the caller's buffer */
static int ntfs_copyresult(char *dest,char *source)
{
	int length=NTFS_GETU16(source+8);
	ntfs_memcpy(dest,source,length);
	return 1;
}

/* use $UpCase some day */
static inline unsigned short ntfs_my_toupper(ntfs_volume *vol, ntfs_u16 x)
{
	/* we should read any pending rest of $UpCase here */
	if(x >= vol->upcase_length)
		return x;
	return vol->upcase[x];
}

/* everything passed in walk and entry */
static int ntfs_my_strcmp(ntfs_iterate_s *walk, const unsigned char *entry)
{
	int lu=*(entry+0x50);
	int i;

	ntfs_u16* name=(ntfs_u16*)(entry+0x52);
	ntfs_volume *vol=walk->dir->vol;
	for(i=0;i<lu && i<walk->namelen;i++)
		if(ntfs_my_toupper(vol,NTFS_GETU16(name+i))!=ntfs_my_toupper(vol,NTFS_GETU16(walk->name+i)))
			break;
	if(i==lu && i==walk->namelen)return 0;
	if(i==lu)return 1;
	if(i==walk->namelen)return -1;
	if(ntfs_my_toupper(vol,NTFS_GETU16(name+i))<ntfs_my_toupper(vol,NTFS_GETU16(walk->name+i)))return 1;
	return -1;
}

/* Necessary forward declaration */
static int ntfs_getdir_iterate(ntfs_iterate_s *walk, char *start, char *entry);

/* Parse a block of entries. Load the block, fix it up, and iterate
   over the entries. The block is given as virtual cluster number */
static int ntfs_getdir_record(ntfs_iterate_s *walk, int block)
{
	int length=walk->dir->u.index.recordsize;
	char *record=(char*)ntfs_malloc(length);
	char *offset;
	int retval,error;
	int oldblock;
	ntfs_io io;

	if( !record )
		return ENOMEM;

	io.fn_put=ntfs_put;
	io.param=record;
	io.size=length;
	/* Read the block from the index allocation attribute */
	error=ntfs_read_attr(walk->dir,walk->dir->vol->at_index_allocation,I30,
			     block*walk->dir->vol->clustersize,&io);
	if(error || io.size!=length){
		ntfs_error("read failed\n");
		ntfs_free(record);
		return 0;
	}
	if(!ntfs_check_index_record(walk->dir,record)){
		ntfs_error("%x is not an index record\n",block);
		ntfs_free(record);
		return 0;
	}
	offset=record+NTFS_GETU16(record+0x18)+0x18;
	oldblock=walk->block;
	walk->block=block;
	retval=ntfs_getdir_iterate(walk,record,offset);
	walk->block=oldblock;
	ntfs_free(record);
	return retval;
}

/* go down to the next block of entries. These collate before
   the current entry */
static int ntfs_descend(ntfs_iterate_s *walk, ntfs_u8 *start, ntfs_u8 *entry)
{
	int length=NTFS_GETU16(entry+8);
	int nextblock=NTFS_GETU32(entry+length-8);
	int error;

	if(!ntfs_entry_has_subnodes(entry)) {
		ntfs_error("illegal ntfs_descend call\n");
		return 0;
	}
	error=ntfs_getdir_record(walk,nextblock);
	if(!error && walk->type==DIR_INSERT && 
	   (walk->u.flags & ITERATE_SPLIT_DONE)){
		/* Split has occurred. Adjust entry, insert new_entry. */
		NTFS_PUTU32(entry+length-8,walk->newblock);
		/* Reset flags, as the current block might be split again. */
		walk->u.flags &= ~ITERATE_SPLIT_DONE;
		error=ntfs_dir_insert(walk,start,entry);
	}
	return error;
}

static int 
ntfs_getdir_iterate_byposition(ntfs_iterate_s *walk,char* start,char *entry)
{
	int retval=0;
	int curpos=0,destpos=0;
	int length;
	if(walk->u.pos!=0){
		if(ntfs_is_top(walk->u.pos))return 0;
		destpos=ntfs_pop(&walk->u.pos);
	}
	while(1){
		if(walk->u.pos==0)
		{
			if(ntfs_entry_has_subnodes(entry))
				ntfs_descend(walk,start,entry);
			else
				walk->u.pos=ntfs_top();
			if(ntfs_is_top(walk->u.pos) && !ntfs_entry_is_used(entry))
			{
				return 1;
			}
			walk->u.pos=ntfs_push(walk->u.pos,curpos);
			return 1;
		}
		if(curpos==destpos)
		{
			if(!ntfs_is_top(walk->u.pos) && ntfs_entry_has_subnodes(entry))
			{
				retval=ntfs_descend(walk,start,entry);
				if(retval){
					walk->u.pos=ntfs_push(walk->u.pos,curpos);
					return retval;
				}else{
					if(!ntfs_entry_is_used(entry))
						return 0;
					walk->u.pos=0;
				}
			}
			if(ntfs_entry_is_used(entry))
			{
				retval=ntfs_copyresult(walk->result,entry);
				walk->u.pos=0;
			}else{
				walk->u.pos=ntfs_top();
				return 0;
			}
		}
		curpos++;
		if(!ntfs_entry_is_used(entry))break;
		length=NTFS_GETU16(entry+8);
		if(!length){
			ntfs_error("infinite loop\n");
			break;
		}
		entry+=length;
	}
	return -1;
}
	
/* Iterate over a list of entries, either from an index block, or from
   the index root. 
   If searching BY_POSITION, pop the top index from the position. If the
   position stack is empty then, return the item at the index and set the
   position to the next entry. If the position stack is not empty, 
   recursively proceed for subnodes. If the entry at the position is the
   'end of dir' entry, return 'not found' and the empty stack.
   If searching BY_NAME, walk through the items until found or until
   one item is collated after the requested item. In the former case, return
   the result. In the latter case, recursively proceed to the subnodes.
   If 'end of dir' is reached, the name is not in the directory */
static int ntfs_getdir_iterate(ntfs_iterate_s *walk, char *start, char *entry)
{
	int length;
	int cmp;

	if(walk->type==BY_POSITION)
		return ntfs_getdir_iterate_byposition(walk,start,entry);
	do{
		/* if the current entry is a real one, compare with the
		   requested item. If the current entry is the last item,
		   it is always larger than the requested item */
		cmp = ntfs_entry_is_used(entry) ? ntfs_my_strcmp(walk,entry) : -1;
		switch(walk->type){
		case BY_NAME:
			switch(cmp)
			{
			case -1:return ntfs_entry_has_subnodes(entry)?
				       ntfs_descend(walk,start,entry):0;
			case  0:return ntfs_copyresult(walk->result,entry);
			case  1:break;
			}
			break;
		case DIR_INSERT:
			switch(cmp){
			case -1:return ntfs_entry_has_subnodes(entry)?
				       ntfs_descend(walk,start,entry):
					       ntfs_dir_insert(walk,start,entry);
			case  0:return EEXIST;
			case  1:break;
			}
			break;
		default:
			ntfs_error("TODO\n");
		}
		if(!ntfs_entry_is_used(entry))break;
		length=NTFS_GETU16(entry+8);
		if(!length){
			ntfs_error("infinite loop\n");
			break;
		}
		entry+=length;
	}while(1);
	return 0;
}

/*	Tree walking is done using position numbers. The following numbers have
    a special meaning:
        0    start (.)
        -1   no more entries
        -2   ..
    All other numbers encode sequences of indices. The sequence a,b,c is 
    encoded as <stop><c><b><a>, where <foo> is the encoding of foo. The
    first few integers are encoded as follows:
        0:    0000    1:    0010    2:    0100    3:    0110
        4:    1000    5:    1010    6:    1100 stop:    1110
        7:  000001    8:  000101	9:  001001   10:  001101
    The least significant bits give the width of this encoding, the
    other bits encode the value, starting from the first value of the 
    interval.
     tag     width  first value  last value
     0       3      0            6
     01      4      7            22
     011     5      23           54
     0111    6      55           119
     More values are hopefully not needed, as the file position has currently
     64 bits in total.
*/

/* Find an entry in the directory. Return 0 if not found, otherwise copy
   the entry to the result buffer. */
int ntfs_getdir(ntfs_iterate_s* walk)
{
	int length=walk->dir->vol->mft_recordsize;
	int retval,error;
	/* start at the index root.*/
	char *root=ntfs_malloc(length);
	ntfs_io io;

	if( !root )
		return ENOMEM;

	io.fn_put=ntfs_put;
	io.param=root;
	io.size=length;
	error=ntfs_read_attr(walk->dir,walk->dir->vol->at_index_root,
			     I30,0,&io);
	if(error)
	{
		ntfs_error("Not a directory\n");
		return 0;
	}
	walk->block=-1;
	/* FIXME: move these to walk */
	walk->dir->u.index.recordsize = NTFS_GETU32(root+0x8);
	walk->dir->u.index.clusters_per_record = NTFS_GETU32(root+0xC);
	/* FIXME: consistency check */
	/* skip header */
	retval = ntfs_getdir_iterate(walk,root,root+0x20);
	ntfs_free(root);
	return retval;
}

/* Find an entry in the directory by its position stack. Iteration starts
   if the stack is 0, in which case the position is set to the first item
   in the directory. If the position is nonzero, return the item at the
   position and change the position to the next item. The position is -1
   if there are no more items */
int ntfs_getdir_byposition(ntfs_iterate_s *walk)
{
	walk->type=BY_POSITION;
	return ntfs_getdir(walk);
}

/* Find an entry in the directory by its name. Return 0 if not found */
int ntfs_getdir_byname(ntfs_iterate_s *walk)
{
	walk->type=BY_NAME;
	return ntfs_getdir(walk);
}

int ntfs_getdir_unsorted(ntfs_inode *ino,ntfs_u32 *p_high,ntfs_u32* p_low,
			 int(*cb)(ntfs_u8*,void*),void *param)
{
	char *buf=0,*entry=0;
	ntfs_io io;
	int length;
	int block;
	int start;
	ntfs_attribute *attr;
	ntfs_volume *vol=ino->vol;
	int byte,bit;
	int error=0;

	if(!ino){
		ntfs_error("No inode passed to getdir_unsorted\n");
		return EINVAL;
	}
	if(!vol){
		ntfs_error("Inode %d has no volume\n",ino->i_number);
		return EINVAL;
	}
	ntfs_debug(DEBUG_DIR3,"unsorted 1\n");
	/* are we still in the index root */
	if(*p_high==0){
		buf=ntfs_malloc(length=vol->mft_recordsize);
		if( !buf )
			return ENOMEM;
		io.fn_put=ntfs_put;
		io.param=buf;
		io.size=length;
		error=ntfs_read_attr(ino,vol->at_index_root,I30,0,&io);
		if(error){
			ntfs_free(buf);
			return error;
		}
		ino->u.index.recordsize = NTFS_GETU32(buf+0x8);
		ino->u.index.clusters_per_record = NTFS_GETU32(buf+0xC);
		entry=buf+0x20;
		ntfs_debug(DEBUG_DIR3,"unsorted 2\n");
	}else{ /* we are in an index record */
		length=ino->u.index.recordsize;
		buf=ntfs_malloc(length);
		if( !buf )
			return ENOMEM;
		io.fn_put=ntfs_put;
		io.param=buf;
		io.size=length;
		/* 0 is index root, index allocation starts with 4 */
		block = *p_high - ino->u.index.clusters_per_record;
		error=ntfs_read_attr(ino,vol->at_index_allocation,I30,
				     block*vol->clustersize,&io);
		if(!error && io.size!=length)error=EIO;
		if(error){
			ntfs_error("read failed\n");
			ntfs_free(buf);
			return error;
		}
		if(!ntfs_check_index_record(ino,buf)){
			ntfs_error("%x is not an index record\n",block);
			ntfs_free(buf);
			return ENOTDIR;
		}
		entry=buf+NTFS_GETU16(buf+0x18)+0x18;
		ntfs_debug(DEBUG_DIR3,"unsorted 3\n");
	}

	/* process the entries */
	start=*p_low;
	while(ntfs_entry_is_used(entry)){
		ntfs_debug(DEBUG_DIR3,"unsorted 4\n");
		if(start)
			start--; /* skip entries that were already processed */
		else{
			ntfs_debug(DEBUG_DIR3,"unsorted 5\n");
			if((error=cb(entry,param)))
				/* the entry could not be processed */
				break;
			(*p_low)++;
		}
		entry+=NTFS_GETU16(entry+8);
	}

	ntfs_debug(DEBUG_DIR3,"unsorted 6\n");
	/* caller did not process all entries */
	if(error){
		ntfs_free(buf);
		ntfs_debug(DEBUG_DIR3,"unsorted 7\n");
		return error;
	}

	/* we have to locate the next record */
	ntfs_free(buf);
	buf=0;
	*p_low=0;
	attr=ntfs_find_attr(ino,vol->at_bitmap,I30);
	if(!attr){
		/* directory does not have index allocation */
		*p_high=0xFFFFFFFF;
		*p_low=0;
		ntfs_debug(DEBUG_DIR3,"unsorted 8\n");
		return 0;
	}
	buf=ntfs_malloc(length=attr->size);
	if( !buf )
		return ENOMEM;

	io.param=buf;
	io.size=length;
	error=ntfs_read_attr(ino,vol->at_bitmap,I30,0,&io);
	if(!error && io.size!=length)error=EIO;
	if(error){
		ntfs_free(buf);
		ntfs_debug(DEBUG_DIR3,"unsorted 9\n");
		return EIO;
	}
	attr=ntfs_find_attr(ino,vol->at_index_allocation,I30);
	while(1){
		if(*p_high*vol->clustersize > attr->size){
			/* no more index records */
			*p_high=0xFFFFFFFF;
			ntfs_free(buf);
			ntfs_debug(DEBUG_DIR3,"unsorted 10\n");
			return 0;
		}
		*p_high+=ino->u.index.clusters_per_record;
		byte=*p_high/ino->u.index.clusters_per_record-1;
		bit  = 1 << (byte & 7);
		byte = byte >> 3;
		/* this record is allocated */
		if(buf[byte] & bit)
			break;
	}
	ntfs_debug(DEBUG_DIR3,"unsorted 11\n");
	ntfs_free(buf);
	return 0;
}

int ntfs_dir_add(ntfs_inode *dir, ntfs_inode *new, ntfs_attribute *name)
{
	ntfs_iterate_s walk;
	int nsize,esize;
	ntfs_u8* entry,*ndata;
	int error;

	walk.type=DIR_INSERT;
	walk.dir=dir;
	walk.u.flags=0;
	nsize = name->size;
	ndata = name->d.data;
	walk.name=(ntfs_u16*)(ndata+0x42);
	walk.namelen=NTFS_GETU8(ndata+0x40);
	walk.new_entry_size = esize = ((nsize+0x18)/8)*8;
	walk.new_entry=entry=ntfs_malloc(esize);
	if(!entry)return ENOMEM;
	ntfs_bzero(entry,esize);
	NTFS_PUTINUM(entry,new);
	NTFS_PUTU16(entry+0x8,esize); /* size of entry */
	NTFS_PUTU16(entry+0xA,nsize); /* size of original name attribute */
	NTFS_PUTU32(entry+0xC,0);     /* FIXME: D-F? */
	ntfs_memcpy(entry+0x10,ndata,nsize);
	error=ntfs_getdir(&walk);
	if(walk.new_entry)
		ntfs_free(walk.new_entry);
	return error;
}

#if 0
int ntfs_dir_add1(ntfs_inode *dir,const char* name,int namelen,ntfs_inode *ino)
{
	ntfs_iterate_s walk;
	int error;
	int nsize;
	char *entry;
	ntfs_attribute *name_attr;
	error=ntfs_decodeuni(dir->vol,name,namelen,&walk.name,&walk.namelen);
	if(error)
		return error;
	/* FIXME: set flags */
	walk.type=DIR_INSERT;
	walk.dir=dir;
	/*walk.new=ino;*/
	/* prepare new entry */
	/* round up to a multiple of 8 */
	walk.new_entry_size = nsize = ((0x52+2*walk.namelen+7)/8)*8;
	walk.new_entry=entry=ntfs_malloc(nsize);
	if(!entry)
		return ENOMEM;
	ntfs_bzero(entry,nsize);
	NTFS_PUTINUM(entry,ino);
	NTFS_PUTU16(entry+8,nsize);
	NTFS_PUTU16(entry+0xA,0x42+2*namelen); /*FIXME: size of name attr*/
	NTFS_PUTU32(entry+0xC,0); /*FIXME: D-F? */
	name_attr=ntfs_find_attr(ino,vol->at_file_name,0); /* FIXME:multiple names */
	if(!name_attr || !name_attr->resident)
		return EIDRM;
	/* directory, file stamps, sizes, filename */
	ntfs_memcpy(entry+0x10,name_attr->d.data,0x42+2*namelen);
	error=ntfs_getdir(&walk);
	ntfs_free(walk.name);
	return error;
}
#endif

/* Fills out and creates an INDEX_ROOT attribute. */

int
ntfs_add_index_root (ntfs_inode *ino, int type)
{
	ntfs_attribute *da;
	ntfs_u8 data[0x30]; /* 0x20 header, 0x10 last entry */
	char name[10];

	NTFS_PUTU32(data, type);
	/* ??? */
	NTFS_PUTU32(data+4, 1);
	NTFS_PUTU32(data+8, ino->vol->index_recordsize);
	NTFS_PUTU32(data+0xC, ino->vol->index_clusters_per_record);
	/* ??? */
	NTFS_PUTU32(data+0x10, 0x10);
	/* Size of entries, including header. */
	NTFS_PUTU32(data+0x14, 0x20);
	NTFS_PUTU32(data+0x18, 0x20);
	/* No index allocation, yet. */
	NTFS_PUTU32(data+0x1C, 0);
	/* add last entry. */
	/* indexed MFT record. */
	NTFS_PUTU64(data+0x20, 0);
	/* size of entry */
	NTFS_PUTU32(data+0x28, 0x10);
	/* flags: last entry, no child nodes. */
	NTFS_PUTU32(data+0x2C, 2);
	/* compute name */
	ntfs_indexname(name, type);
	return ntfs_create_attr(ino, ino->vol->at_index_root, name,
				data, sizeof(data), &da);
}

int
ntfs_mkdir(ntfs_inode* dir,const char* name,int namelen, ntfs_inode *result)
{
	int error;
	error = ntfs_alloc_inode(dir, result, name, namelen, NTFS_AFLAG_DIR);
	if(error)
		goto out;
	error = ntfs_add_index_root(result, 0x30);
	if (error)
		goto out;
	/* Set directory bit */
	result->attr[0x16] |= 2;
	error = ntfs_update_inode (dir);
	if (error)
		goto out;
	error = ntfs_update_inode (result);
	if (error)
		goto out;
 out:
	return error;
}

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
