#include <linux/module.h>
#include <linux/videodev.h>
#include "dvb_filter.h"

unsigned int bitrates[3][16] =
{{0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
 {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},
 {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}};

uint32_t freq[4] = {441, 480, 320, 0};

unsigned int ac3_bitrates[32] =
    {32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640,
     0,0,0,0,0,0,0,0,0,0,0,0,0};

uint32_t ac3_freq[4] = {480, 441, 320, 0};
uint32_t ac3_frames[3][32] =
    {{64,80,96,112,128,160,192,224,256,320,384,448,512,640,768,896,1024,
      1152,1280,0,0,0,0,0,0,0,0,0,0,0,0,0},
     {69,87,104,121,139,174,208,243,278,348,417,487,557,696,835,975,1114,
      1253,1393,0,0,0,0,0,0,0,0,0,0,0,0,0},
     {96,120,144,168,192,240,288,336,384,480,576,672,768,960,1152,1344,
      1536,1728,1920,0,0,0,0,0,0,0,0,0,0,0,0,0}}; 



void pes2ts_init(pes2ts_t *p2ts, unsigned short pid, 
		 pes2ts_cb_t *cb, void *priv)
{
	unsigned char *buf=p2ts->buf;

	buf[0]=0x47;
	buf[1]=(pid>>8);
	buf[2]=pid&0xff;
	p2ts->cc=0;
	p2ts->cb=cb;
	p2ts->priv=priv;
}

int pes2ts(pes2ts_t *p2ts, unsigned char *pes, int len)
{
	unsigned char *buf=p2ts->buf;
	int ret=0, rest;
	
	//len=6+((pes[4]<<8)|pes[5]);

	buf[1]|=0x40;
	while (len>=184) {
		buf[3]=0x10|((p2ts->cc++)&0x0f);
		memcpy(buf+4, pes, 184);
		if ((ret=p2ts->cb(p2ts->priv, buf)))
			return ret;
		len-=184; pes+=184;
		buf[1]&=~0x40;
	}
	if (!len)
	        return 0;
	buf[3]=0x30|((p2ts->cc++)&0x0f);
	rest=183-len;
	if (rest) {
	        buf[5]=0x00;
		if (rest-1)
			memset(buf+6, 0xff, rest-1);
	}
	buf[4]=rest;
	memcpy(buf+5+rest, pes, len);
	return p2ts->cb(p2ts->priv, buf);
}

void reset_ipack(ipack *p)
{
	p->found = 0;
	p->cid = 0;
	p->plength = 0;
	p->flag1 = 0;
	p->flag2 = 0;
	p->hlength = 0;
	p->mpeg = 0;
	p->check = 0;
	p->which = 0;
	p->done = 0;
	p->count = 0;
}

void init_ipack(ipack *p, int size,
		void (*func)(u8 *buf, int size, void *priv))
{
	if ( !(p->buf = vmalloc(size*sizeof(u8))) ){
		printk ("Couldn't allocate memory for ipack\n");
	}
	p->size = size;
	p->func = func;
	p->repack_subids = 0;
	reset_ipack(p);
}

void free_ipack(ipack * p)
{
	if (p->buf) vfree(p->buf);
}

void send_ipack(ipack *p)
{
	int off;
	AudioInfo ai;
	int ac3_off = 0;
	int streamid=0;
	int nframes= 0;
	int f=0;

	switch ( p->mpeg ){
	case 2:		
		if (p->count < 10) return;
		p->buf[3] = p->cid;
		
		p->buf[4] = (u8)(((p->count-6) & 0xFF00) >> 8);
		p->buf[5] = (u8)((p->count-6) & 0x00FF);
		if (p->repack_subids && p->cid == PRIVATE_STREAM1){
			
			off = 9+p->buf[8];
			streamid = p->buf[off];
			if ((streamid & 0xF8) == 0x80){
				ai.off = 0;
				ac3_off = ((p->buf[off+2] << 8)| 
					   p->buf[off+3]);
				if (ac3_off < p->count)
					f=get_ac3info(p->buf+off+3+ac3_off, 
						      p->count-ac3_off, &ai,0);
				if ( !f ){
					nframes = (p->count-off-3-ac3_off)/ 
						ai.framesize + 1;
					p->buf[off+2] = (ac3_off >> 8)& 0xFF;
					p->buf[off+3] = (ac3_off)& 0xFF;
					p->buf[off+1] = nframes;
					
					ac3_off +=  nframes * ai.framesize - 
						p->count;
				}
			}
		} 
		p->func(p->buf, p->count, p->data);
	
		p->buf[6] = 0x80;
		p->buf[7] = 0x00;
		p->buf[8] = 0x00;
		p->count = 9;
		if (p->repack_subids && p->cid == PRIVATE_STREAM1 
		    && (streamid & 0xF8)==0x80 ){
			p->count += 4;
			p->buf[9] = streamid;
			p->buf[10] = (ac3_off >> 8)& 0xFF;
			p->buf[11] = (ac3_off)& 0xFF;
			p->buf[12] = 0;
		}

		break;
	case 1:
		if (p->count < 8) return;
		p->buf[3] = p->cid;
		
		p->buf[4] = (u8)(((p->count-6) & 0xFF00) >> 8);
		p->buf[5] = (u8)((p->count-6) & 0x00FF);
		p->func(p->buf, p->count, p->data);
	
		p->buf[6] = 0x0F;
		p->count = 7;
		break;
	}
}

void send_ipack_rest(ipack *p)
{
	if (p->plength != MMAX_PLENGTH-6 || p->found<=6)
		return;
	p->plength = p->found-6;
	p->found = 0;
	send_ipack(p);
	reset_ipack(p);
}

static void write_ipack(ipack *p, u8 *data, int count)
{
	u8 headr[3] = { 0x00, 0x00, 0x01} ;

	if (p->count < 6){
		memcpy(p->buf, headr, 3);
		p->count = 6;
	}

	if (p->count + count < p->size){
		memcpy(p->buf+p->count, data, count);
		p->count += count;
	} else {
		int rest = p->size - p->count;
		memcpy(p->buf+p->count, data, rest);
		p->count += rest;
		send_ipack(p);
		if (count - rest > 0)
			write_ipack(p, data+rest, count-rest);
	}
}

int instant_repack(u8 *buf, int count, ipack *p)
{
	int l;
	int c=0;

	while (c < count && (p->mpeg == 0 ||
			     (p->mpeg == 1 && p->found < 7) ||
			     (p->mpeg == 2 && p->found < 9))
	       &&  (p->found < 5 || !p->done)){
		switch ( p->found ){
		case 0:
		case 1:
			if (buf[c] == 0x00) p->found++;
			else p->found = 0;
			c++;
			break;
		case 2:
			if (buf[c] == 0x01) p->found++;
			else if (buf[c] == 0) {
				p->found = 2;
			} else p->found = 0;
			c++;
			break;
		case 3:
			p->cid = 0;
			switch (buf[c]){
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
				p->done = 1;
			case PRIVATE_STREAM1:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
				p->found++;
				p->cid = buf[c];
				c++;
				break;
			default:
				p->found = 0;
				break;
			}
			break;
			
		case 4:
			if (count-c > 1){
				p->plen[0] = buf[c];
				c++;
				p->plen[1] = buf[c];
				c++;
				p->found+=2;
				p->plength=(p->plen[0]<<8)|p->plen[1];
 			} else {
				p->plen[0] = buf[c];
				p->found++;
				return count;
			}
			break;
		case 5:
			p->plen[1] = buf[c];
			c++;
			p->found++;
			p->plength=(p->plen[0]<<8)|p->plen[1];
			break;
		case 6:
			if (!p->done){
				p->flag1 = buf[c];
				c++;
				p->found++;
				if ( (p->flag1 & 0xC0) == 0x80 ) p->mpeg = 2;
				else {
					p->hlength = 0;
					p->which = 0;
					p->mpeg = 1;
					p->flag2 = 0;
				}
			}
			break;

		case 7:
			if ( !p->done && p->mpeg == 2) {
				p->flag2 = buf[c];
				c++;
				p->found++;
			}	
			break;

		case 8:
			if ( !p->done && p->mpeg == 2) {
				p->hlength = buf[c];
				c++;
				p->found++;
			}
			break;
			
		default:

			break;
		}
	}

	if (c == count) return count;

	if (!p->plength) p->plength = MMAX_PLENGTH-6;

	if ( p->done || ((p->mpeg == 2 && p->found >= 9) || 
	     (p->mpeg == 1 && p->found >= 7)) ){
		switch (p->cid){
			
		case AUDIO_STREAM_S ... AUDIO_STREAM_E:			
		case VIDEO_STREAM_S ... VIDEO_STREAM_E:
		case PRIVATE_STREAM1:
			
			if (p->mpeg == 2 && p->found == 9) {
				write_ipack(p, &p->flag1, 1);
				write_ipack(p, &p->flag2, 1);
				write_ipack(p, &p->hlength, 1);
			}

			if (p->mpeg == 1 && p->found == 7) 
				write_ipack(p, &p->flag1, 1);
			
			if (p->mpeg == 2 && (p->flag2 & PTS_ONLY) &&  
			    p->found < 14) {
				while (c < count && p->found < 14) {
					p->pts[p->found-9] = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
				}
				if (c == count) return count;
			}

			if (p->mpeg == 1 && p->which < 2000) {

				if (p->found == 7) {
					p->check = p->flag1;
					p->hlength = 1;
				}

				while (!p->which && c < count && 
				       p->check == 0xFF){
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
				}
				
				if ( c == count) return count;
				
				if ( (p->check & 0xC0) == 0x40 && !p->which){
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
					
					p->which = 1;
					if ( c == count) return count;
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
					p->which = 2;
					if ( c == count) return count;
				}
				
				if (p->which == 1){
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
					p->which = 2;
					if ( c == count) return count;
				}
				
				if ( (p->check & 0x30) && p->check != 0xFF){
					p->flag2 = (p->check & 0xF0) << 2;
					p->pts[0] = p->check;
					p->which = 3;
				} 
			
				if ( c == count) return count;
				if (p->which > 2){
					if ((p->flag2 & PTS_DTS_FLAGS)
					    == PTS_ONLY){
						while (c < count && 
						       p->which < 7){
							p->pts[p->which-2] =
								buf[c];
							write_ipack(p,buf+c,1);
							c++;
							p->found++;
							p->which++;
							p->hlength++;
						}
						if ( c == count) return count;
					} else if ((p->flag2 & PTS_DTS_FLAGS) 
						   == PTS_DTS){
						while (c < count && 
						       p->which< 12){
							if (p->which< 7)
								p->pts[p->which
								      -2] =
									buf[c];
							write_ipack(p,buf+c,1);
							c++;
							p->found++;
							p->which++;
							p->hlength++;
						}
						if ( c == count) return count;
					}
					p->which = 2000;
				}
				
			}
			
			while (c < count && p->found < p->plength+6){
				l = count -c;
				if (l+p->found > p->plength+6)
					l = p->plength+6-p->found;
				write_ipack(p, buf+c, l);
				p->found += l;
				c += l;
			}	
			
			break;
		}


		if ( p->done ){
			if( p->found + count - c < p->plength+6){
				p->found += count-c;
				c = count;
			} else {
				c += p->plength+6 - p->found;
				p->found = p->plength+6;
			}
		}

		if (p->plength && p->found == p->plength+6) {
			send_ipack(p);
			reset_ipack(p);
			if (c < count)
				instant_repack(buf+c, count-c, p);
		}
	}
	return count;
}



void setup_ts2pes(ipack *pa, ipack *pv, u16 *pida, u16 *pidv, 
		  void (*pes_write)(u8 *buf, int count, void *data),
		  void *priv)
{
	init_ipack(pa, IPACKS, pes_write);
	init_ipack(pv, IPACKS, pes_write);
	pa->pid = pida;
	pv->pid = pidv;
	pa->data = priv;
	pv->data = priv;
}

void ts_to_pes(ipack *p, u8 *buf) // don't need count (=188)
{
	u8 off = 0;

	if (!buf || !p ){
		printk("NULL POINTER IDIOT\n");
		return;
	}
	if (buf[1]&PAY_START) {
		if (p->plength == MMAX_PLENGTH-6 && p->found>6){
			p->plength = p->found-6;
			p->found = 0;
			send_ipack(p);
			reset_ipack(p);
		}
	}
	if (buf[3] & ADAPT_FIELD) {  // adaptation field?
		off = buf[4] + 1;
		if (off+4 > 187) return;
	}
	instant_repack(buf+4+off, TS_SIZE-4-off, p);
}

/* needs 5 byte input, returns picture coding type*/
int read_picture_header(uint8_t *headr, mpg_picture *pic, int field, int pr)
{
	uint8_t pct;

	if (pr) printk( "Pic header: ");
        pic->temporal_reference[field] = (( headr[0] << 2 ) | 
					  (headr[1] & 0x03) )& 0x03ff;
	if (pr) printk( " temp ref: 0x%04x", pic->temporal_reference[field]);

	pct = ( headr[1] >> 2 ) & 0x07;
        pic->picture_coding_type[field] = pct;
	if (pr) {
		switch(pct){
			case I_FRAME:
				printk( "  I-FRAME");
				break;
			case B_FRAME:
				printk( "  B-FRAME");
				break;
			case P_FRAME:
				printk( "  P-FRAME");
				break;
		}
	}


        pic->vinfo.vbv_delay  = (( headr[1] >> 5 ) | ( headr[2] << 3) | 
				 ( (headr[3] & 0x1F) << 11) ) & 0xffff;

	if (pr) printk( " vbv delay: 0x%04x", pic->vinfo.vbv_delay);

        pic->picture_header_parameter = ( headr[3] & 0xe0 ) | 
		((headr[4] & 0x80) >> 3);

        if ( pct == B_FRAME ){
                pic->picture_header_parameter |= ( headr[4] >> 3 ) & 0x0f;
        }
	if (pr) printk( " pic head param: 0x%x", 
			pic->picture_header_parameter);

	return pct;
} 


/* needs 4 byte input */
int read_gop_header(uint8_t *headr, mpg_picture *pic, int pr)
{
	if (pr) printk("GOP header: "); 

	pic->time_code  = (( headr[0] << 17 ) | ( headr[1] << 9) | 
			   ( headr[2] << 1 ) | (headr[3] &0x01)) & 0x1ffffff;

	if (pr) printk(" time: %d:%d.%d ", (headr[0]>>2)& 0x1F,
		       ((headr[0]<<4)& 0x30)| ((headr[1]>>4)& 0x0F),
		       ((headr[1]<<3)& 0x38)| ((headr[2]>>5)& 0x0F));

        if ( ( headr[3] & 0x40 ) != 0 ){
                pic->closed_gop = 1;
        } else {
                pic->closed_gop = 0;
        }
	if (pr) printk("closed: %d", pic->closed_gop); 

        if ( ( headr[3] & 0x20 ) != 0 ){
                pic->broken_link = 1;
        } else {
                pic->broken_link = 0;
        }
	if (pr) printk(" broken: %d\n", pic->broken_link); 

	return 0;
}

/* needs 8 byte input */
int read_sequence_header(uint8_t *headr, VideoInfo *vi, int pr)
{
        int sw;
	int form = -1;

	if (pr) printk("Reading sequence header\n");

	vi->horizontal_size	= ((headr[1] &0xF0) >> 4) | (headr[0] << 4);
	vi->vertical_size	= ((headr[1] &0x0F) << 8) | (headr[2]);
    
        sw = (int)((headr[3]&0xF0) >> 4) ;

        switch( sw ){
	case 1:
		if (pr)
			printk("Videostream: ASPECT: 1:1");
		vi->aspect_ratio = 100;        
		break;
	case 2:
		if (pr)
			printk("Videostream: ASPECT: 4:3");
                vi->aspect_ratio = 133;        
		break;
	case 3:
		if (pr)
			printk("Videostream: ASPECT: 16:9");
                vi->aspect_ratio = 177;        
		break;
	case 4:
		if (pr)
			printk("Videostream: ASPECT: 2.21:1");
                vi->aspect_ratio = 221;        
		break;

        case 5 ... 15:
		if (pr)
			printk("Videostream: ASPECT: reserved");
                vi->aspect_ratio = 0;        
		break;

        default:
                vi->aspect_ratio = 0;        
                return -1;
	}

	if (pr)
		printk("  Size = %dx%d",vi->horizontal_size,vi->vertical_size);

        sw = (int)(headr[3]&0x0F);

        switch ( sw ) {
	case 1:
		if (pr)
			printk("  FRate: 23.976 fps");
                vi->framerate = 23976;
		form = -1;
		break;
	case 2:
		if (pr)
			printk("  FRate: 24 fps");
                vi->framerate = 24000;
		form = -1;
		break;
	case 3:
		if (pr)
			printk("  FRate: 25 fps");
                vi->framerate = 25000;
		form = VIDEO_MODE_PAL;
		break;
	case 4:
		if (pr)
			printk("  FRate: 29.97 fps");
                vi->framerate = 29970;
		form = VIDEO_MODE_NTSC;
		break;
	case 5:
		if (pr)
			printk("  FRate: 30 fps");
                vi->framerate = 30000;
		form = VIDEO_MODE_NTSC;
		break;
	case 6:
		if (pr)
			printk("  FRate: 50 fps");
                vi->framerate = 50000;
		form = VIDEO_MODE_PAL;
		break;
	case 7:
		if (pr)
			printk("  FRate: 60 fps");
                vi->framerate = 60000;
		form = VIDEO_MODE_NTSC;
		break;
	}

	vi->bit_rate = (headr[4] << 10) | (headr[5] << 2) | (headr[6] & 0x03);
	
        vi->vbv_buffer_size
                = (( headr[6] & 0xF8) >> 3 ) | (( headr[7] & 0x1F )<< 5);

	if (pr){
		printk("  BRate: %d Mbit/s",4*(vi->bit_rate)/10000);
		printk("  vbvbuffer %d",16*1024*(vi->vbv_buffer_size));
		printk("\n");
	}

        vi->video_format = form;

	return 0;
}

int get_vinfo(uint8_t *mbuf, int count, VideoInfo *vi, int pr)
{
	uint8_t *headr;
	int found = 0;
	int c = 0;

	while (found < 4 && c+4 < count){
		uint8_t *b;

		b = mbuf+c;
		if ( b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01
		     && b[3] == 0xb3) found = 4;
		else {
			c++;
		}
	}

	if (! found) return -1;
	c += 4;
	if (c+12 >= count) return -1;
	headr = mbuf+c;
	if (read_sequence_header(headr, vi, pr) < 0) return -1;
	vi->off = c-4;
	return 0;
}

int get_ainfo(uint8_t *mbuf, int count, AudioInfo *ai, int pr)
{
	uint8_t *headr;
	int found = 0;
	int c = 0;
	int fr = 0;

	while (found < 2 && c < count){
		uint8_t b[2];
		memcpy( b, mbuf+c, 2);

		if ( b[0] == 0xff && (b[1] & 0xf8) == 0xf8)
			found = 2;
		else {
			c++;
		}
	}	

	if (!found) return -1;

	if (c+3 >= count) return -1;
        headr = mbuf+c;

	ai->layer = (headr[1] & 0x06) >> 1;

	if (pr)
		printk("Audiostream: Layer: %d", 4-ai->layer);


	ai->bit_rate = bitrates[(3-ai->layer)][(headr[2] >> 4 )]*1000;

	if (pr){
		if (ai->bit_rate == 0)
			printk("  Bit rate: free");
		else if (ai->bit_rate == 0xf)
			printk("  BRate: reserved");
		else
			printk("  BRate: %d kb/s", ai->bit_rate/1000);
	}

	fr = (headr[2] & 0x0c ) >> 2;
	ai->frequency = freq[fr]*100;
	if (pr){
		if (ai->frequency == 3)
			printk("  Freq: reserved\n");
		else
			printk("  Freq: %d kHz\n",ai->frequency); 
			       
	}
	ai->off = c;
	return 0;
}

int get_ac3info(uint8_t *mbuf, int count, AudioInfo *ai, int pr)
{
	uint8_t *headr;
	int found = 0;
	int c = 0;
	uint8_t frame = 0;
	int fr = 0;
	
	while ( !found  && c < count){
		uint8_t *b = mbuf+c;

		if ( b[0] == 0x0b &&  b[1] == 0x77 )
			found = 1;
		else {
			c++;
		}
	}	

	if (!found) return -1;
	if (pr)
		printk("Audiostream: AC3");

	ai->off = c;
	if (c+5 >= count) return -1;

	ai->layer = 0;  // 0 for AC3
        headr = mbuf+c+2;

	frame = (headr[2]&0x3f);
	ai->bit_rate = ac3_bitrates[frame >> 1]*1000;

	if (pr)
		printk("  BRate: %d kb/s", ai->bit_rate/1000);

	ai->frequency = (headr[2] & 0xc0 ) >> 6;
	fr = (headr[2] & 0xc0 ) >> 6;
	ai->frequency = freq[fr]*100;
	if (pr) printk ("  Freq: %d Hz\n", ai->frequency);


	ai->framesize = ac3_frames[fr][frame >> 1];
	if ((frame & 1) &&  (fr == 1)) ai->framesize++;
	ai->framesize = ai->framesize << 1;
	if (pr) printk ("  Framesize %d\n", ai->framesize);


	return 0;
}

uint8_t *skip_pes_header(uint8_t **bufp)
{
        uint8_t *inbuf = *bufp;
        uint8_t *buf = inbuf;
        uint8_t *pts = NULL;
        int skip = 0;

int mpeg1_skip_table[16] = {
        1, 0xffff,      5,     10, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};


        if ((inbuf[6] & 0xc0) == 0x80){ /* mpeg2 */
                if (buf[7] & PTS_ONLY)
                        pts = buf+9;
                else pts = NULL;
                buf = inbuf + 9 + inbuf[8];
        } else {        /* mpeg1 */
                for (buf = inbuf + 6; *buf == 0xff; buf++)
                        if (buf == inbuf + 6 + 16) {
                                break;
                        }
                if ((*buf & 0xc0) == 0x40)
                        buf += 2;
                skip = mpeg1_skip_table [*buf >> 4];
                if (skip == 5 || skip == 10) pts = buf;
                else pts = NULL;

                buf += mpeg1_skip_table [*buf >> 4];
        }

        *bufp = buf;
        return pts;
}


void initialize_quant_matrix( uint32_t *matrix )
{
        int i;

        matrix[0]  = 0x08101013;
        matrix[1]  = 0x10131616;
        matrix[2]  = 0x16161616;
        matrix[3]  = 0x1a181a1b;
        matrix[4]  = 0x1b1b1a1a;
        matrix[5]  = 0x1a1a1b1b;
        matrix[6]  = 0x1b1d1d1d;
        matrix[7]  = 0x2222221d;
        matrix[8]  = 0x1d1d1b1b;
        matrix[9]  = 0x1d1d2020;
        matrix[10] = 0x22222526;
        matrix[11] = 0x25232322;
        matrix[12] = 0x23262628;
        matrix[13] = 0x28283030;
        matrix[14] = 0x2e2e3838;
        matrix[15] = 0x3a454553;

        for ( i = 16 ; i < 32 ; i++ )
                matrix[i] = 0x10101010;
}

void initialize_mpg_picture(mpg_picture *pic)
{
        int i;

        /* set MPEG1 */
        pic->mpeg1_flag = 1;
        pic->profile_and_level = 0x4A ;        /* MP@LL */
        pic->progressive_sequence = 1;
        pic->low_delay = 0;

        pic->sequence_display_extension_flag = 0;
        for ( i = 0 ; i < 4 ; i++ ){
                pic->frame_centre_horizontal_offset[i] = 0;
                pic->frame_centre_vertical_offset[i] = 0;
        }
        pic->last_frame_centre_horizontal_offset = 0;
        pic->last_frame_centre_vertical_offset = 0;

        pic->picture_display_extension_flag[0] = 0;
        pic->picture_display_extension_flag[1] = 0;
        pic->sequence_header_flag = 0;      
	pic->gop_flag = 0;              
        pic->sequence_end_flag = 0;	
}


void mpg_set_picture_parameter( int32_t field_type, mpg_picture *pic )
{
        int16_t last_h_offset;
        int16_t last_v_offset;

        int16_t *p_h_offset;
        int16_t *p_v_offset;

        if ( pic->mpeg1_flag ){
                pic->picture_structure[field_type] = VIDEO_FRAME_PICTURE;
                pic->top_field_first = 0;
                pic->repeat_first_field = 0;
                pic->progressive_frame = 1;
                pic->picture_coding_parameter = 0x000010;
        }

        /* Reset flag */
        pic->picture_display_extension_flag[field_type] = 0;

        last_h_offset = pic->last_frame_centre_horizontal_offset;
        last_v_offset = pic->last_frame_centre_vertical_offset;
        if ( field_type == FIRST_FIELD ){
                p_h_offset = pic->frame_centre_horizontal_offset;
                p_v_offset = pic->frame_centre_vertical_offset;
                *p_h_offset = last_h_offset;
                *(p_h_offset + 1) = last_h_offset;
                *(p_h_offset + 2) = last_h_offset;
                *p_v_offset = last_v_offset;
                *(p_v_offset + 1) = last_v_offset;
                *(p_v_offset + 2) = last_v_offset;
        } else {
                pic->frame_centre_horizontal_offset[3] = last_h_offset;
                pic->frame_centre_vertical_offset[3] = last_v_offset;
        }
}

void init_mpg_picture( mpg_picture *pic, int chan, int32_t field_type)
{
        pic->picture_header = 0;
        pic->sequence_header_data
                = ( INIT_HORIZONTAL_SIZE << 20 )
                        | ( INIT_VERTICAL_SIZE << 8 )
                        | ( INIT_ASPECT_RATIO << 4 )
                        | ( INIT_FRAME_RATE );
        pic->mpeg1_flag = 0;
        pic->vinfo.horizontal_size
                = INIT_DISP_HORIZONTAL_SIZE;
        pic->vinfo.vertical_size
                = INIT_DISP_VERTICAL_SIZE;
        pic->picture_display_extension_flag[field_type]
                = 0;
        pic->pts_flag[field_type] = 0;

        pic->sequence_gop_header = 0;
        pic->picture_header = 0;
        pic->sequence_header_flag = 0;
        pic->gop_flag = 0;
        pic->sequence_end_flag = 0;
        pic->sequence_display_extension_flag = 0;
        pic->last_frame_centre_horizontal_offset = 0;
        pic->last_frame_centre_vertical_offset = 0;
	pic->channel = chan;
}
