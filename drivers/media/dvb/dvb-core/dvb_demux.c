/* 
 * dvb_demux.c - DVB kernel demux API
 *
 * Copyright (C) 2000-2001 Ralph  Metzler <ralph@convergence.de>
 *                       & Marcus Metzler <marcus@convergence.de>
 *                         for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include "compat.h"
#include "dvb_demux.h"

#define NOBUFS  

LIST_HEAD(dmx_muxs);

int dmx_register_demux(dmx_demux_t *demux) 
{
	struct list_head *pos, *head=&dmx_muxs;
	
	if (!(demux->id && demux->vendor && demux->model)) 
	        return -EINVAL;
	list_for_each(pos, head) 
	{
	        if (!strcmp(DMX_DIR_ENTRY(pos)->id, demux->id))
		        return -EEXIST;
	}
	demux->users=0;
	list_add(&(demux->reg_list), head);
	MOD_INC_USE_COUNT;
	return 0;
}

int dmx_unregister_demux(dmx_demux_t* demux)
{
	struct list_head *pos, *n, *head=&dmx_muxs;

	list_for_each_safe (pos, n, head) 
	{
	        if (DMX_DIR_ENTRY(pos)==demux) 
		{
		        if (demux->users>0)
			        return -EINVAL;
		        list_del(pos);
		        MOD_DEC_USE_COUNT;
			return 0;
		}
	}
	return -ENODEV;
}


struct list_head *dmx_get_demuxes(void)
{
        if (list_empty(&dmx_muxs))
	        return NULL;

	return &dmx_muxs;
}

/******************************************************************************
 * static inlined helper functions
 ******************************************************************************/

static inline u16 
section_length(const u8 *buf)
{
        return 3+((buf[1]&0x0f)<<8)+buf[2];
}

static inline u16 
ts_pid(const u8 *buf)
{
        return ((buf[1]&0x1f)<<8)+buf[2];
}

static inline int
payload(const u8 *tsp)
{
        if (!(tsp[3]&0x10)) // no payload?
                return 0;
        if (tsp[3]&0x20) {  // adaptation field?
		if (tsp[4]>183)    // corrupted data?
			return 0;
		else
			return 184-1-tsp[4];
	}
        return 184;
}


static u32 
dvb_crc_table[256] = {
	0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
	0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
	0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
	0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
	0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
	0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
	0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
	0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
	0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
	0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
	0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
	0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
	0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
	0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
	0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
	0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
	0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
	0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
	0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
	0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
	0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
	0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
	0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
	0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
	0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
	0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
	0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
	0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
	0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
	0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
	0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
	0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
	0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
	0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};

u32 dvb_crc32(u8 *data, int len)
{
	int i;
	u32 crc = 0xffffffff;

	for (i=0; i<len; i++)
                crc = (crc << 8) ^ dvb_crc_table[((crc >> 24) ^ *data++) & 0xff];
	return crc;
}

void dvb_set_crc32(u8 *data, int length)
{
        u32 crc;

        crc=dvb_crc32(data,length);
        data[length]   = (crc>>24)&0xff;
        data[length+1] = (crc>>16)&0xff;
        data[length+2] = (crc>>8)&0xff;
        data[length+3] = (crc)&0xff;
}
  

/******************************************************************************
 * Software filter functions
 ******************************************************************************/

static inline int
dvb_dmx_swfilter_payload(struct dvb_demux_feed *dvbdmxfeed, const u8 *buf) 
{
        int p, count;
	//int ccok;
	//u8 cc;

        if (!(count=payload(buf)))
                return -1;
        p=188-count;
        /*
	cc=buf[3]&0x0f;
        ccok=((dvbdmxfeed->cc+1)&0x0f)==cc ? 1 : 0;
        dvbdmxfeed->cc=cc;
        if (!ccok)
          printk("missed packet!\n");
        */
        if (buf[1]&0x40)  // PUSI ?
                dvbdmxfeed->peslen=0xfffa;
        dvbdmxfeed->peslen+=count;

        return dvbdmxfeed->cb.ts((u8 *)&buf[p], count, 0, 0, 
                                 &dvbdmxfeed->feed.ts, DMX_OK); 
}


static int
dvb_dmx_swfilter_sectionfilter(struct dvb_demux_feed *dvbdmxfeed, 
                            struct dvb_demux_filter *f)
{
        dmx_section_filter_t *filter=&f->filter;
        int i;
	u8 xor, neq=0;

        for (i=0; i<DVB_DEMUX_MASK_MAX; i++) {
		xor=filter->filter_value[i]^dvbdmxfeed->secbuf[i];
		if (f->maskandmode[i]&xor)
			return 0;
		neq|=f->maskandnotmode[i]&xor;
	}
	if (f->doneq && !neq)
		return 0;

        return dvbdmxfeed->cb.sec(dvbdmxfeed->secbuf, dvbdmxfeed->seclen, 
                                  0, 0, filter, DMX_OK); 
}

static inline int
dvb_dmx_swfilter_section_feed(struct dvb_demux_feed *dvbdmxfeed)
{
        u8 *buf=dvbdmxfeed->secbuf;
        struct dvb_demux_filter *f;

        if (dvbdmxfeed->secbufp!=dvbdmxfeed->seclen)
                return -1;
        if (!dvbdmxfeed->feed.sec.is_filtering)
                return 0;
        if (!(f=dvbdmxfeed->filter))
                return 0;
        do 
                if (dvb_dmx_swfilter_sectionfilter(dvbdmxfeed, f)<0)
                        return -1;
        while ((f=f->next) && dvbdmxfeed->feed.sec.is_filtering);

        dvbdmxfeed->secbufp=dvbdmxfeed->seclen=0;
        memset(buf, 0, DVB_DEMUX_MASK_MAX);
        return 0;
}

static inline int
dvb_dmx_swfilter_section_packet(struct dvb_demux_feed *dvbdmxfeed, const u8 *buf) 
{
        int p, count;
        int ccok, rest;
	u8 cc;

        if (!(count=payload(buf)))
                return -1;
        p=188-count;

	cc=buf[3]&0x0f;
        ccok=((dvbdmxfeed->cc+1)&0x0f)==cc ? 1 : 0;
        dvbdmxfeed->cc=cc;

        if (buf[1]&0x40) { // PUSI set
                // offset to start of first section is in buf[p] 
		if (p+buf[p]>187) // trash if it points beyond packet
			return -1;
                if (buf[p] && ccok) { // rest of previous section?
                        // did we have enough data in last packet to calc length?
			int tmp=3-dvbdmxfeed->secbufp;
                        if (tmp>0 && tmp!=3) {
				if (p+tmp>=187)
					return -1;
                                memcpy(dvbdmxfeed->secbuf+dvbdmxfeed->secbufp,
                                       buf+p+1, tmp);
                                dvbdmxfeed->seclen=section_length(dvbdmxfeed->secbuf);
				if (dvbdmxfeed->seclen>4096) 
					return -1;
                        }
                        rest=dvbdmxfeed->seclen-dvbdmxfeed->secbufp;
                        if (rest==buf[p] && dvbdmxfeed->seclen) {
                                memcpy(dvbdmxfeed->secbuf+dvbdmxfeed->secbufp,
                                       buf+p+1, buf[p]);
                                dvbdmxfeed->secbufp+=buf[p];
                                dvb_dmx_swfilter_section_feed(dvbdmxfeed);
                        }
                }
                p+=buf[p]+1; 		// skip rest of last section
                count=188-p;
                while (count>0) {
                        if ((count>2) &&  // enough data to determine sec length?
                            ((dvbdmxfeed->seclen=section_length(buf+p))<=count)) {
				if (dvbdmxfeed->seclen>4096) 
					return -1;
                                memcpy(dvbdmxfeed->secbuf, buf+p, 
                                       dvbdmxfeed->seclen);
                                dvbdmxfeed->secbufp=dvbdmxfeed->seclen;
                                p+=dvbdmxfeed->seclen;
                                count=188-p;
                                dvb_dmx_swfilter_section_feed(dvbdmxfeed);

                                // filling bytes until packet end?
                                if (count && buf[p]==0xff) 
                                        count=0;
                        } else { // section continues to following TS packet
                                memcpy(dvbdmxfeed->secbuf, buf+p, count);
                                dvbdmxfeed->secbufp+=count;
                                count=0;
                        }
                }
		return 0;
	}

	// section continued below
	if (!ccok)
		return -1;
	if (!dvbdmxfeed->secbufp) // any data in last ts packet?
		return -1;
	// did we have enough data in last packet to calc section length?
	if (dvbdmxfeed->secbufp<3) {
		int tmp=3-dvbdmxfeed->secbufp;
		
		if (tmp>count)
			return -1;
		memcpy(dvbdmxfeed->secbuf+dvbdmxfeed->secbufp, buf+p, tmp);
		dvbdmxfeed->seclen=section_length(dvbdmxfeed->secbuf);
		if (dvbdmxfeed->seclen>4096) 
			return -1;
	}
	rest=dvbdmxfeed->seclen-dvbdmxfeed->secbufp;
	if (rest<0)
		return -1;
	if (rest<=count) {	// section completed in this TS packet
		memcpy(dvbdmxfeed->secbuf+dvbdmxfeed->secbufp, buf+p, rest);
		dvbdmxfeed->secbufp+=rest;
		dvb_dmx_swfilter_section_feed(dvbdmxfeed);
	} else 	{	// section continues in following ts packet
		memcpy(dvbdmxfeed->secbuf+dvbdmxfeed->secbufp, buf+p, count);
		dvbdmxfeed->secbufp+=count;
	}
        return 0;
}

static inline void 
dvb_dmx_swfilter_packet_type(struct dvb_demux_feed *dvbdmxfeed, const u8 *buf)
{
        switch(dvbdmxfeed->type) {
        case DMX_TYPE_TS:
                if (!dvbdmxfeed->feed.ts.is_filtering)
                        break;
                if (dvbdmxfeed->ts_type & TS_PACKET) {
                        if (dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)
                                dvb_dmx_swfilter_payload(dvbdmxfeed, buf);
                        else
                                dvbdmxfeed->cb.ts((u8 *)buf, 188, 0, 0, 
                                                  &dvbdmxfeed->feed.ts, DMX_OK); 
                }
                if (dvbdmxfeed->ts_type & TS_DECODER) 
                        if (dvbdmxfeed->demux->write_to_decoder)
                                dvbdmxfeed->demux->
                                  write_to_decoder(dvbdmxfeed, (u8 *)buf, 188);
                break;

        case DMX_TYPE_SEC:
                if (!dvbdmxfeed->feed.sec.is_filtering)
                        break;
                if (dvb_dmx_swfilter_section_packet(dvbdmxfeed, buf)<0)
                        dvbdmxfeed->seclen=dvbdmxfeed->secbufp=0;
                break;

        default:
                break;
        }
}

void
dvb_dmx_swfilter_packet(struct dvb_demux *dvbdmx, const u8 *buf)
{
        struct dvb_demux_feed *dvbdmxfeed;

        if (!(dvbdmxfeed=dvbdmx->pid2feed[ts_pid(buf)]))
                return;
        dvb_dmx_swfilter_packet_type(dvbdmxfeed, buf);
}

void
dvb_dmx_swfilter_packets(struct dvb_demux *dvbdmx, const u8 *buf, int count)
{
        struct dvb_demux_feed *dvbdmxfeed;

	spin_lock(&dvbdmx->lock);
	if ((dvbdmxfeed=dvbdmx->pid2feed[0x2000]))
		dvbdmxfeed->cb.ts((u8 *)buf, count*188, 0, 0, 
				  &dvbdmxfeed->feed.ts, DMX_OK); 
        while (count) {
		dvb_dmx_swfilter_packet(dvbdmx, buf);
		count--;
		buf+=188;
	}
	spin_unlock(&dvbdmx->lock);
}

static inline void
dvb_dmx_swfilter(struct dvb_demux *dvbdmx, const u8 *buf, size_t count)
{
        int p=0,i, j;
        
        if ((i=dvbdmx->tsbufp)) {
                if (count<(j=188-i)) {
                        memcpy(&dvbdmx->tsbuf[i], buf, count);
                        dvbdmx->tsbufp+=count;
                        return;
                }
                memcpy(&dvbdmx->tsbuf[i], buf, j);
                dvb_dmx_swfilter_packet(dvbdmx, dvbdmx->tsbuf);
                dvbdmx->tsbufp=0;
                p+=j;
        }
        
        while (p<count) {
                if (buf[p]==0x47) {
                        if (count-p>=188) {
                                dvb_dmx_swfilter_packet(dvbdmx, buf+p);
                                p+=188;
                        } else {
                                i=count-p;
                                memcpy(dvbdmx->tsbuf, buf+p, i);
                                dvbdmx->tsbufp=i;
				return;
                        }
                } else 
                        p++;
        }
}


/******************************************************************************
 ******************************************************************************
 * DVB DEMUX API LEVEL FUNCTIONS
 ******************************************************************************
 ******************************************************************************/

static struct dvb_demux_filter *
dvb_dmx_filter_alloc(struct dvb_demux *dvbdmx)
{
        int i;

        for (i=0; i<dvbdmx->filternum; i++)
                if (dvbdmx->filter[i].state==DMX_STATE_FREE)
                        break;
        if (i==dvbdmx->filternum)
                return 0;
        dvbdmx->filter[i].state=DMX_STATE_ALLOCATED;
        return &dvbdmx->filter[i];
}

static struct dvb_demux_feed *
dvb_dmx_feed_alloc(struct dvb_demux *dvbdmx)
{
        int i;

        for (i=0; i<dvbdmx->feednum; i++)
                if (dvbdmx->feed[i].state==DMX_STATE_FREE)
                        break;
        if (i==dvbdmx->feednum)
                return 0;
        dvbdmx->feed[i].state=DMX_STATE_ALLOCATED;
        return &dvbdmx->feed[i];
}


/******************************************************************************
 * dmx_ts_feed API calls
 ******************************************************************************/

static int 
dmx_pid_set(u16 pid, struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	struct dvb_demux_feed **pid2feed=dvbdmx->pid2feed;

	if (pid>DMX_MAX_PID)
                return -EINVAL;
        if (dvbdmxfeed->pid!=0xffff) {
                if (dvbdmxfeed->pid<=DMX_MAX_PID)
                        pid2feed[dvbdmxfeed->pid]=0;
                dvbdmxfeed->pid=0xffff;
        }
        if (pid2feed[pid]) {
                return -EBUSY;
	}
        pid2feed[pid]=dvbdmxfeed;
        dvbdmxfeed->pid=pid;
	return 0;
}


static int 
dmx_ts_feed_set(struct dmx_ts_feed_s* feed,
                u16 pid,
		int ts_type, 
		dmx_ts_pes_t pes_type,
		size_t callback_length, 
                size_t circular_buffer_size, 
                int descramble, 
                struct timespec timeout
                )
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	int ret;
	
        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (ts_type & TS_DECODER) {
                if (pes_type >= DMX_TS_PES_OTHER) {
			up(&dvbdmx->mutex);
                        return -EINVAL;
		}
                if (dvbdmx->pesfilter[pes_type] && 
                    (dvbdmx->pesfilter[pes_type]!=dvbdmxfeed)) {
			up(&dvbdmx->mutex);
                        return -EINVAL;
		}
		if ((pes_type != DMX_TS_PES_PCR0) && 
		    (pes_type != DMX_TS_PES_PCR1) && 
		    (pes_type != DMX_TS_PES_PCR2) && 
		    (pes_type != DMX_TS_PES_PCR3)) {
			if ((ret=dmx_pid_set(pid, dvbdmxfeed))<0) {
				up(&dvbdmx->mutex);
				return ret;
			} else
				dvbdmxfeed->pid=pid;
		}
                dvbdmx->pesfilter[pes_type]=dvbdmxfeed;
	        dvbdmx->pids[pes_type]=dvbdmxfeed->pid;
        } else
		if ((ret=dmx_pid_set(pid, dvbdmxfeed))<0) {
			up(&dvbdmx->mutex);
			return ret;
		}

        dvbdmxfeed->buffer_size=circular_buffer_size;
        dvbdmxfeed->descramble=descramble;
        dvbdmxfeed->timeout=timeout;
        dvbdmxfeed->cb_length=callback_length;
        dvbdmxfeed->ts_type=ts_type;
        dvbdmxfeed->pes_type=pes_type;

        if (dvbdmxfeed->descramble) {
		up(&dvbdmx->mutex);
                return -ENOSYS;
	}

        if (dvbdmxfeed->buffer_size) {
#ifdef NOBUFS
                dvbdmxfeed->buffer=0;
#else
                dvbdmxfeed->buffer=vmalloc(dvbdmxfeed->buffer_size);
                if (!dvbdmxfeed->buffer) {
			up(&dvbdmx->mutex);
			return -ENOMEM;
		}
#endif
        }
        dvbdmxfeed->state=DMX_STATE_READY;
        up(&dvbdmx->mutex);
        return 0;
}

static int 
dmx_ts_feed_start_filtering(struct dmx_ts_feed_s* feed)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	int ret;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (dvbdmxfeed->state!=DMX_STATE_READY ||
	    dvbdmxfeed->type!=DMX_TYPE_TS) {
		up(&dvbdmx->mutex);
                return -EINVAL;
	}
        if (!dvbdmx->start_feed) {
		up(&dvbdmx->mutex);
                return -1;
	}
        ret=dvbdmx->start_feed(dvbdmxfeed); 
        if (ret<0) {
		up(&dvbdmx->mutex);
		return ret;
	}
	spin_lock_irq(&dvbdmx->lock);
        feed->is_filtering=1;
        dvbdmxfeed->state=DMX_STATE_GO;
	spin_unlock_irq(&dvbdmx->lock);
        up(&dvbdmx->mutex);
	return 0;
}
 
static int 
dmx_ts_feed_stop_filtering(struct dmx_ts_feed_s* feed)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	int ret;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (dvbdmxfeed->state<DMX_STATE_GO) {
		up(&dvbdmx->mutex);
                return -EINVAL;
	}
        if (!dvbdmx->stop_feed) {
		up(&dvbdmx->mutex);
                return -1;
	}
        ret=dvbdmx->stop_feed(dvbdmxfeed); 
	spin_lock_irq(&dvbdmx->lock);
        feed->is_filtering=0;
        dvbdmxfeed->state=DMX_STATE_ALLOCATED;

	spin_unlock_irq(&dvbdmx->lock);
        up(&dvbdmx->mutex);
        return ret;
}

static int dvbdmx_allocate_ts_feed(dmx_demux_t *demux,
                                   dmx_ts_feed_t **feed, 
                                   dmx_ts_cb callback)
{
        struct dvb_demux *dvbdmx=(struct dvb_demux *) demux;
        struct dvb_demux_feed *dvbdmxfeed;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (!(dvbdmxfeed=dvb_dmx_feed_alloc(dvbdmx))) {
		up(&dvbdmx->mutex);
                return -EBUSY;
	}
        dvbdmxfeed->type=DMX_TYPE_TS;
        dvbdmxfeed->cb.ts=callback;
        dvbdmxfeed->demux=dvbdmx;
        dvbdmxfeed->pid=0xffff;
        dvbdmxfeed->peslen=0xfffa;
        dvbdmxfeed->buffer=0;

        (*feed)=&dvbdmxfeed->feed.ts;
        (*feed)->is_filtering=0;
        (*feed)->parent=demux;
        (*feed)->priv=0;
        (*feed)->set=dmx_ts_feed_set;
        (*feed)->start_filtering=dmx_ts_feed_start_filtering;
        (*feed)->stop_filtering=dmx_ts_feed_stop_filtering;


        if (!(dvbdmxfeed->filter=dvb_dmx_filter_alloc(dvbdmx))) {
                dvbdmxfeed->state=DMX_STATE_FREE;
		up(&dvbdmx->mutex);
                return -EBUSY;
        }

        dvbdmxfeed->filter->type=DMX_TYPE_TS;
        dvbdmxfeed->filter->feed=dvbdmxfeed;
        dvbdmxfeed->filter->state=DMX_STATE_READY;
        
        up(&dvbdmx->mutex);
        return 0;
}

static int dvbdmx_release_ts_feed(dmx_demux_t *demux, dmx_ts_feed_t *feed)
{
        struct dvb_demux *dvbdmx=(struct dvb_demux *) demux;
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (dvbdmxfeed->state==DMX_STATE_FREE) {
		up(&dvbdmx->mutex);
                return -EINVAL;
	}
#ifndef NOBUFS
        if (dvbdmxfeed->buffer) { 
                vfree(dvbdmxfeed->buffer);
                dvbdmxfeed->buffer=0;
        }
#endif
        dvbdmxfeed->state=DMX_STATE_FREE;
        dvbdmxfeed->filter->state=DMX_STATE_FREE;
	if (dvbdmxfeed->pid<=DMX_MAX_PID) {
		dvbdmxfeed->demux->pid2feed[dvbdmxfeed->pid]=0;
                dvbdmxfeed->pid=0xffff;
        }

        up(&dvbdmx->mutex);
        return 0;
}


/******************************************************************************
 * dmx_section_feed API calls
 ******************************************************************************/

static int 
dmx_section_feed_allocate_filter(struct dmx_section_feed_s* feed, 
                                 dmx_section_filter_t** filter) 
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdemux=dvbdmxfeed->demux;
        struct dvb_demux_filter *dvbdmxfilter;

	if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

        dvbdmxfilter=dvb_dmx_filter_alloc(dvbdemux);
        if (!dvbdmxfilter) {
		up(&dvbdemux->mutex);
                return -ENOSPC;
	}
	spin_lock_irq(&dvbdemux->lock);
        *filter=&dvbdmxfilter->filter;
        (*filter)->parent=feed;
        (*filter)->priv=0;
        dvbdmxfilter->feed=dvbdmxfeed;
        dvbdmxfilter->type=DMX_TYPE_SEC;
        dvbdmxfilter->state=DMX_STATE_READY;

        dvbdmxfilter->next=dvbdmxfeed->filter;
        dvbdmxfeed->filter=dvbdmxfilter;
	spin_unlock_irq(&dvbdemux->lock);
        up(&dvbdemux->mutex);
        return 0;
}

static int 
dmx_section_feed_set(struct dmx_section_feed_s* feed, 
                     u16 pid, size_t circular_buffer_size, 
                     int descramble, int check_crc) 
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;

        if (pid>0x1fff)
                return -EINVAL;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;
	
        if (dvbdmxfeed->pid!=0xffff) {
                dvbdmx->pid2feed[dvbdmxfeed->pid]=0;
                dvbdmxfeed->pid=0xffff;
        }
        if (dvbdmx->pid2feed[pid]) {
		up(&dvbdmx->mutex);
		return -EBUSY;
	}
        dvbdmx->pid2feed[pid]=dvbdmxfeed;
        dvbdmxfeed->pid=pid;

        dvbdmxfeed->buffer_size=circular_buffer_size;
        dvbdmxfeed->descramble=descramble;
        if (dvbdmxfeed->descramble) {
		up(&dvbdmx->mutex);
                return -ENOSYS;
	}

        dvbdmxfeed->check_crc=check_crc;
#ifdef NOBUFS
        dvbdmxfeed->buffer=0;
#else
        dvbdmxfeed->buffer=vmalloc(dvbdmxfeed->buffer_size);
        if (!dvbdmxfeed->buffer) {
		up(&dvbdmx->mutex);
		return -ENOMEM;
	}
#endif
        dvbdmxfeed->state=DMX_STATE_READY;
        up(&dvbdmx->mutex);
        return 0;
}

static void prepare_secfilters(struct dvb_demux_feed *dvbdmxfeed)
{
	int i;
        dmx_section_filter_t *sf;
	struct dvb_demux_filter *f;
	u8 mask, mode, doneq;
		
	if (!(f=dvbdmxfeed->filter))
                return;
        do {
		sf=&f->filter;
		doneq=0;
		for (i=0; i<DVB_DEMUX_MASK_MAX; i++) {
			mode=sf->filter_mode[i];
			mask=sf->filter_mask[i];
			f->maskandmode[i]=mask&mode;
			doneq|=f->maskandnotmode[i]=mask&~mode;
		}
		f->doneq=doneq ? 1 : 0;
	} while ((f=f->next));
}


static int 
dmx_section_feed_start_filtering(dmx_section_feed_t *feed)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	int ret;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;
	
        if (feed->is_filtering) {
		up(&dvbdmx->mutex);
		return -EBUSY;
	}
        if (!dvbdmxfeed->filter) {
		up(&dvbdmx->mutex);
                return -EINVAL;
	}
        dvbdmxfeed->secbufp=0;
        dvbdmxfeed->seclen=0;
        
        if (!dvbdmx->start_feed) {
		up(&dvbdmx->mutex);
                return -1;
	}
	prepare_secfilters(dvbdmxfeed);
        ret=dvbdmx->start_feed(dvbdmxfeed); 
	if (ret<0) {
		up(&dvbdmx->mutex);
		return ret;
	}
	spin_lock_irq(&dvbdmx->lock);
        feed->is_filtering=1;
        dvbdmxfeed->state=DMX_STATE_GO;
	spin_unlock_irq(&dvbdmx->lock);
        up(&dvbdmx->mutex);
	return 0;
}

static int 
dmx_section_feed_stop_filtering(struct dmx_section_feed_s* feed)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        int ret;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (!dvbdmx->stop_feed) {
		up(&dvbdmx->mutex);
                return -1;
	}
	ret=dvbdmx->stop_feed(dvbdmxfeed); 
	spin_lock_irq(&dvbdmx->lock);
        dvbdmxfeed->state=DMX_STATE_READY;
        feed->is_filtering=0;
	spin_unlock_irq(&dvbdmx->lock);
        up(&dvbdmx->mutex);
	return ret;
}

static int 
dmx_section_feed_release_filter(dmx_section_feed_t *feed, 
                                dmx_section_filter_t* filter)
{
        struct dvb_demux_filter *dvbdmxfilter=(struct dvb_demux_filter *) filter, *f;
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (dvbdmxfilter->feed!=dvbdmxfeed) {
		up(&dvbdmx->mutex);
                return -EINVAL;
	}
        if (feed->is_filtering) 
                feed->stop_filtering(feed);
	
	spin_lock_irq(&dvbdmx->lock);
        f=dvbdmxfeed->filter;
        if (f==dvbdmxfilter)
                dvbdmxfeed->filter=dvbdmxfilter->next;
        else {
                while(f->next!=dvbdmxfilter)
                        f=f->next;
                f->next=f->next->next;
        }
        dvbdmxfilter->state=DMX_STATE_FREE;
	spin_unlock_irq(&dvbdmx->lock);
        up(&dvbdmx->mutex);
        return 0;
}

static int dvbdmx_allocate_section_feed(dmx_demux_t *demux, 
                                        dmx_section_feed_t **feed,
                                        dmx_section_cb callback)
{
        struct dvb_demux *dvbdmx=(struct dvb_demux *) demux;
        struct dvb_demux_feed *dvbdmxfeed;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (!(dvbdmxfeed=dvb_dmx_feed_alloc(dvbdmx))) {
		up(&dvbdmx->mutex);
                return -EBUSY;
	}
        dvbdmxfeed->type=DMX_TYPE_SEC;
        dvbdmxfeed->cb.sec=callback;
        dvbdmxfeed->demux=dvbdmx;
        dvbdmxfeed->pid=0xffff;
        dvbdmxfeed->secbufp=0;
        dvbdmxfeed->filter=0;
        dvbdmxfeed->buffer=0;

        (*feed)=&dvbdmxfeed->feed.sec;
        (*feed)->is_filtering=0;
        (*feed)->parent=demux;
        (*feed)->priv=0;
        (*feed)->set=dmx_section_feed_set;
        (*feed)->allocate_filter=dmx_section_feed_allocate_filter;
        (*feed)->release_filter=dmx_section_feed_release_filter;
        (*feed)->start_filtering=dmx_section_feed_start_filtering;
        (*feed)->stop_filtering=dmx_section_feed_stop_filtering;

        up(&dvbdmx->mutex);
        return 0;
}

static int dvbdmx_release_section_feed(dmx_demux_t *demux, 
                                       dmx_section_feed_t *feed)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
        struct dvb_demux *dvbdmx=(struct dvb_demux *) demux;

        if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

        if (dvbdmxfeed->state==DMX_STATE_FREE) {
		up(&dvbdmx->mutex);
                return -EINVAL;
	}
#ifndef NOBUFS
        if (dvbdmxfeed->buffer) {
                vfree(dvbdmxfeed->buffer);
                dvbdmxfeed->buffer=0;
        }
#endif
        dvbdmxfeed->state=DMX_STATE_FREE;
        dvbdmxfeed->demux->pid2feed[dvbdmxfeed->pid]=0;
        if (dvbdmxfeed->pid!=0xffff)
                dvbdmxfeed->demux->pid2feed[dvbdmxfeed->pid]=0;
        up(&dvbdmx->mutex);
        return 0;
}


/******************************************************************************
 * dvb_demux kernel data API calls
 ******************************************************************************/

static int dvbdmx_open(dmx_demux_t *demux)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        if (dvbdemux->users>=MAX_DVB_DEMUX_USERS)
                return -EUSERS;
        dvbdemux->users++;
        return 0;
}

static int dvbdmx_close(struct dmx_demux_s *demux)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        if (dvbdemux->users==0)
                return -ENODEV;
        dvbdemux->users--;
        //FIXME: release any unneeded resources if users==0
        return 0;
}

static int dvbdmx_write(dmx_demux_t *demux, const char *buf, size_t count)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        if ((!demux->frontend) ||
            (demux->frontend->source!=DMX_MEMORY_FE))
                return -EINVAL;

        if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

        dvb_dmx_swfilter(dvbdemux, buf, count);
        up(&dvbdemux->mutex);
        return count;
}


static int dvbdmx_add_frontend(dmx_demux_t *demux, 
                               dmx_frontend_t *frontend)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;
        struct list_head *pos, *head=&dvbdemux->frontend_list;
	
	if (!(frontend->id && frontend->vendor && frontend->model)) 
	        return -EINVAL;
	list_for_each(pos, head) 
	{
	        if (!strcmp(DMX_FE_ENTRY(pos)->id, frontend->id))
		        return -EEXIST;
	}

	list_add(&(frontend->connectivity_list), head);
        return 0;
}

static int 
dvbdmx_remove_frontend(dmx_demux_t *demux, 
                       dmx_frontend_t *frontend)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;
        struct list_head *pos, *n, *head=&dvbdemux->frontend_list;

	list_for_each_safe (pos, n, head) 
	{
	        if (DMX_FE_ENTRY(pos)==frontend) 
                {
		        list_del(pos);
			return 0;
		}
	}
	return -ENODEV;
}

static struct list_head *
dvbdmx_get_frontends(dmx_demux_t *demux)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        if (list_empty(&dvbdemux->frontend_list))
	        return NULL;
        return &dvbdemux->frontend_list;
}

static int dvbdmx_connect_frontend(dmx_demux_t *demux, 
                                   dmx_frontend_t *frontend)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        if (demux->frontend)
                return -EINVAL;
        
        if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

        demux->frontend=frontend;
        up(&dvbdemux->mutex);
        return 0;
}

static int dvbdmx_disconnect_frontend(dmx_demux_t *demux)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

        demux->frontend=NULL;
        up(&dvbdemux->mutex);
        return 0;
}

static int dvbdmx_get_pes_pids(dmx_demux_t *demux, u16 *pids)
{
        struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

        memcpy(pids, dvbdemux->pids, 5*sizeof(u16));
        return 0;
}

int 
dvb_dmx_init(struct dvb_demux *dvbdemux)
{
        int i;
        dmx_demux_t *dmx=&dvbdemux->dmx;

        dvbdemux->users=0;
	dvbdemux->filter=vmalloc(dvbdemux->filternum*sizeof(struct dvb_demux_filter));
	if (!dvbdemux->filter)
                return -ENOMEM;

	dvbdemux->feed=vmalloc(dvbdemux->feednum*sizeof(struct dvb_demux_feed));
	if (!dvbdemux->feed) {
	        vfree(dvbdemux->filter);
                return -ENOMEM;
	}
        for (i=0; i<dvbdemux->filternum; i++) {
                dvbdemux->filter[i].state=DMX_STATE_FREE;
                dvbdemux->filter[i].index=i;
        }
        for (i=0; i<dvbdemux->feednum; i++)
                dvbdemux->feed[i].state=DMX_STATE_FREE;
        dvbdemux->frontend_list.next=
          dvbdemux->frontend_list.prev=
            &dvbdemux->frontend_list;
        for (i=0; i<DMX_TS_PES_OTHER; i++) {
                dvbdemux->pesfilter[i]=NULL;
                dvbdemux->pids[i]=0xffff;
	}
        dvbdemux->playing=dvbdemux->recording=0;
        memset(dvbdemux->pid2feed, 0, (DMX_MAX_PID+1)*sizeof(struct dvb_demux_feed *));
        dvbdemux->tsbufp=0;

        dmx->frontend=0;
        dmx->reg_list.next=dmx->reg_list.prev=&dmx->reg_list;
        dmx->priv=(void *) dvbdemux;
        //dmx->users=0;                  // reset in dmx_register_demux() 
        dmx->open=dvbdmx_open;
        dmx->close=dvbdmx_close;
        dmx->write=dvbdmx_write;
        dmx->allocate_ts_feed=dvbdmx_allocate_ts_feed;
        dmx->release_ts_feed=dvbdmx_release_ts_feed;
        dmx->allocate_section_feed=dvbdmx_allocate_section_feed;
        dmx->release_section_feed=dvbdmx_release_section_feed;

        dmx->descramble_mac_address=NULL;
        dmx->descramble_section_payload=NULL;
        
        dmx->add_frontend=dvbdmx_add_frontend;
        dmx->remove_frontend=dvbdmx_remove_frontend;
        dmx->get_frontends=dvbdmx_get_frontends;
        dmx->connect_frontend=dvbdmx_connect_frontend;
        dmx->disconnect_frontend=dvbdmx_disconnect_frontend;
        dmx->get_pes_pids=dvbdmx_get_pes_pids;
        sema_init(&dvbdemux->mutex, 1);
	spin_lock_init(&dvbdemux->lock);

        if (dmx_register_demux(dmx)<0) 
                return -1;

        return 0;
}

int 
dvb_dmx_release(struct dvb_demux *dvbdemux)
{
        dmx_demux_t *dmx=&dvbdemux->dmx;

        dmx_unregister_demux(dmx);
	if (dvbdemux->filter)
                vfree(dvbdemux->filter);
	if (dvbdemux->feed)
                vfree(dvbdemux->feed);
        return 0;
}

#if 0
MODULE_DESCRIPTION("Software MPEG Demultiplexer");
MODULE_AUTHOR("Ralph Metzler, Markus Metzler");
MODULE_LICENSE("GPL");
#endif

