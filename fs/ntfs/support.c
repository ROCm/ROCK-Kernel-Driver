/*
 *  support.c
 *  Specific support functions
 *
 *  Copyright (C) 1997 Martin von Löwis
 *  Copyright (C) 1997 Régis Duchesne
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ntfstypes.h"
#include "struct.h"
#include "support.h"

#include <stdarg.h>
#include <linux/slab.h>
#include <linux/locks.h>
#include <linux/nls.h>
#include "util.h"
#include "inode.h"
#include "macros.h"

static char print_buf[1024];

#ifdef DEBUG
#include "sysctl.h"
#include <linux/kernel.h>

/* Debugging output */
void ntfs_debug(int mask, const char *fmt, ...)
{
	va_list ap;

	/* Filter it with the debugging level required */
	if(ntdebug & mask){
		va_start(ap,fmt);
		strcpy(print_buf, KERN_DEBUG);
		vsprintf(print_buf + 3, fmt, ap);
		printk(print_buf);
		va_end(ap);
	}
}

#ifndef ntfs_malloc
/* Verbose kmalloc */
void *ntfs_malloc(int size)
{
	void *ret;

	ret = kmalloc(size, GFP_KERNEL);
	ntfs_debug(DEBUG_MALLOC, "Allocating %x at %p\n", size, ret);

	return ret;
}
#endif

#ifndef ntfs_free
/* Verbose kfree() */
void ntfs_free(void *block)
{
        ntfs_debug(DEBUG_MALLOC, "Freeing memory at %p\n", block);
	kfree(block);
}
#endif
#else
void ntfs_debug(int mask, const char *fmt, ...)
{
}

#ifndef ntfs_malloc
void *ntfs_malloc(int size)
{
	return kmalloc(size, GFP_KERNEL);
}
#endif

#ifndef ntfs_free
void ntfs_free(void *block)
{
	kfree(block);
}
#endif
#endif /* DEBUG */

void ntfs_bzero(void *s, int n)
{
	memset(s, 0, n);
}

/* These functions deliberately return no value. It is dest, anyway,
   and not used anywhere in the NTFS code.  */

void ntfs_memcpy(void *dest, const void *src, ntfs_size_t n)
{
	memcpy(dest, src, n);
}

void ntfs_memmove(void *dest, const void *src, ntfs_size_t n)
{
	memmove(dest, src, n);
}

/* Warn that an error occured. */
void ntfs_error(const char *fmt,...)
{
        va_list ap;

        va_start(ap, fmt);
        strcpy(print_buf, KERN_ERR);
        vsprintf(print_buf + 3, fmt, ap);
        printk(print_buf);
        va_end(ap);
}

int ntfs_read_mft_record(ntfs_volume *vol, int mftno, char *buf)
{
	int error;
	ntfs_io io;

	ntfs_debug(DEBUG_OTHER, "read_mft_record %x\n",mftno);
	if(mftno==FILE_MFT)
	{
		ntfs_memcpy(buf,vol->mft,vol->mft_recordsize);
		return 0;
	}
	if(!vol->mft_ino)
	{
		printk("ntfs:something is terribly wrong here\n");
		return ENODATA;
	}
 	io.fn_put=ntfs_put;
	io.fn_get=0;
	io.param=buf;
	io.size=vol->mft_recordsize;
	error=ntfs_read_attr(vol->mft_ino,vol->at_data,NULL,
			     mftno*vol->mft_recordsize,&io);
	if(error || (io.size!=vol->mft_recordsize))
	{
		ntfs_debug(DEBUG_OTHER, "read_mft_record: read %x failed (%d,%d,%d)\n",
			   mftno,error,io.size,vol->mft_recordsize);
		return error?error:ENODATA;
	}
	ntfs_debug(DEBUG_OTHER, "read_mft_record: finished read %x\n",mftno);
	if(!ntfs_check_mft_record(vol,buf))
	{
		printk("Invalid MFT record for %x\n",mftno);
		return EINVAL;
	}
	ntfs_debug(DEBUG_OTHER, "read_mft_record: Done %x\n",mftno);
	return 0;
}

int ntfs_getput_clusters(ntfs_volume *vol, int cluster,	ntfs_size_t start_offs,
	 ntfs_io *buf)
{
	struct super_block *sb=NTFS_SB(vol);
	struct buffer_head *bh;
	ntfs_size_t to_copy;
	int length=buf->size;
	if(buf->do_read)
		ntfs_debug(DEBUG_OTHER, "get_clusters %d %d %d\n",cluster,start_offs,length);
	else
		ntfs_debug(DEBUG_OTHER, "put_clusters %d %d %d\n",cluster,start_offs,length);
	while(length)
	{
		if(!(bh=bread(sb->s_dev,cluster,vol->clustersize)))
		{
			ntfs_debug(DEBUG_OTHER, "%s failed\n", buf->do_read?"Reading":"Writing");
			return EIO;
		}
		to_copy=min(vol->clustersize-start_offs,length);
		lock_buffer(bh);
		if(buf->do_read)
			buf->fn_put(buf,bh->b_data+start_offs,to_copy);
		else
		{
			buf->fn_get(bh->b_data+start_offs,buf,to_copy);
			mark_buffer_dirty(bh);
		}
		unlock_buffer(bh);
		length-=to_copy;
		start_offs=0;
		cluster++;
		brelse(bh);
	}
	return 0;
}

ntfs_time64_t ntfs_now(void)
{
	return ntfs_unixutc2ntutc(CURRENT_TIME);
}

/* when printing unicode characters base64, use this table.
   It is not strictly base64, but the Linux vfat encoding.
   base64 has the disadvantage of using the slash.
*/
static char uni2esc[64]=
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-";

static unsigned char
esc2uni(char c)
{
	if(c<'0')return 255;
	if(c<='9')return c-'0';
	if(c<'A')return 255;
	if(c<='Z')return c-'A'+10;
	if(c<'a')return 255;
	if(c<='z')return c-'a'+36;
	if(c=='+')return 62;
	if(c=='-')return 63;
	return 255;
}

int ntfs_dupuni2map(ntfs_volume *vol, ntfs_u16 *in, int in_len, char **out,
  int *out_len)
{
	int i,o,val,chl, chi;
	char *result,*buf,charbuf[NLS_MAX_CHARSET_SIZE];
	struct nls_table* nls=vol->nls_map;

	result=ntfs_malloc(in_len+1);
	if(!result)return ENOMEM;
	*out_len=in_len;
	result[in_len]='\0';
	for(i=o=0;i<in_len;i++){
		int cl,ch;
		/* FIXME: byte order? */
		cl=in[i] & 0xFF;
		ch=(in[i] >> 8) & 0xFF;
		if(!nls){
			if(!ch){
				result[o++]=cl;
				continue;
			}
		}else{
			/* FIXME: byte order? */
			wchar_t uni = in[i];
			if ( (chl = nls->uni2char(uni, charbuf, NLS_MAX_CHARSET_SIZE)) > 0){
				/* adjust result buffer */
				if (chl > 1){
					buf=ntfs_malloc(*out_len + chl - 1);
					memcpy(buf, result, o);
					ntfs_free(result);
					result=buf;
					*out_len+=(chl-1);
				}
				for (chi=0;chi<chl;chi++)
					result[o++] = charbuf[chi];
			} else
				result[o++] = '?';
			continue;

		}
		if(!(vol->nct & nct_uni_xlate))goto inval;
		/* realloc */
		buf=ntfs_malloc(*out_len+3);
		if( !buf ) {
			ntfs_free( result );
			return ENOMEM;
		}
		memcpy(buf,result,o);
		ntfs_free(result);
		result=buf;
		*out_len+=3;
		result[o++]=':';
		if(vol->nct & nct_uni_xlate_vfat){
			val=(cl<<8)+ch;
			result[o+2]=uni2esc[val & 0x3f];
			val>>=6;
			result[o+1]=uni2esc[val & 0x3f];
			val>>=6;
			result[o]=uni2esc[val & 0x3f];
			o+=3;
		}else{
			val=(ch<<8)+cl;
			result[o++]=uni2esc[val & 0x3f];
			val>>=6;
			result[o++]=uni2esc[val & 0x3f];
			val>>=6;
			result[o++]=uni2esc[val & 0x3f];
		}
	}
	*out=result;
	return 0;
 inval:
	ntfs_free(result);
	*out=0;
	return EILSEQ;
}

int ntfs_dupmap2uni(ntfs_volume *vol, char* in, int in_len, ntfs_u16 **out,
  int *out_len)
{
	int i,o;
	ntfs_u16* result;
	struct nls_table* nls=vol->nls_map;

	*out=result=ntfs_malloc(2*in_len);
	if(!result)return ENOMEM;
	*out_len=in_len;
	for(i=o=0;i<in_len;i++,o++){
		wchar_t uni;
		if(in[i]!=':' || (vol->nct & nct_uni_xlate)==0){
			int charlen;
			/* FIXME: is this error handling ok? */
			charlen = nls->char2uni(&in[i], in_len-i, &uni);
			if (charlen < 0)
				return charlen;
			*out_len -= (charlen-1);
			i += (charlen-1);
		}else{
			unsigned char c1,c2,c3;
			*out_len-=3;
			c1=esc2uni(in[++i]);
			c2=esc2uni(in[++i]);
			c3=esc2uni(in[++i]);
			if(c1==255 || c2==255 || c3==255)
				uni = 0;
			else if(vol->nct & nct_uni_xlate_vfat){
				uni = (((c2 & 0x3) << 6) + c3) << 8 |
					((c1 << 4) + (c2 >> 2));
			}else{
				uni = ((c3 << 4) + (c2 >> 2)) << 8 |
					(((c2 & 0x3) << 6) + c1);
			}
		}
		/* FIXME: byte order? */
		result[o] = uni;
		if(!result[o]){
			ntfs_free(result);
			return EILSEQ;
		}
	}
	return 0;
}

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
