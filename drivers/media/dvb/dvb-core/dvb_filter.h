#ifndef _DVB_FILTER_H_
#define _DVB_FILTER_H_

#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "demux.h"

typedef int (dvb_filter_pes2ts_cb_t) (void *, unsigned char *);

typedef struct dvb_filter_pes2ts_s {
	unsigned char buf[188];
        unsigned char cc;
        dvb_filter_pes2ts_cb_t *cb;
	void *priv;
} dvb_filter_pes2ts_t;

void dvb_filter_pes2ts_init(dvb_filter_pes2ts_t *p2ts, unsigned short pid, 
		 	    dvb_filter_pes2ts_cb_t *cb, void *priv);

int dvb_filter_pes2ts(dvb_filter_pes2ts_t *p2ts, unsigned char *pes, int len);


#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM   0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S   0xC0
#define AUDIO_STREAM_E   0xDF
#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF
#define ECM_STREAM       0xF0
#define EMM_STREAM       0xF1
#define DSM_CC_STREAM    0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

#define PICTURE_START    0x00
#define USER_START       0xb2
#define SEQUENCE_HEADER  0xb3
#define SEQUENCE_ERROR   0xb4
#define EXTENSION_START  0xb5
#define SEQUENCE_END     0xb7
#define GOP_START        0xb8
#define EXCEPT_SLICE     0xb0

#define SEQUENCE_EXTENSION           0x01
#define SEQUENCE_DISPLAY_EXTENSION   0x02
#define PICTURE_CODING_EXTENSION     0x08
#define QUANT_MATRIX_EXTENSION       0x03
#define PICTURE_DISPLAY_EXTENSION    0x07

#define I_FRAME 0x01 
#define B_FRAME 0x02 
#define P_FRAME 0x03

/* Initialize sequence_data */
#define INIT_HORIZONTAL_SIZE        720
#define INIT_VERTICAL_SIZE          576
#define INIT_ASPECT_RATIO          0x02
#define INIT_FRAME_RATE            0x03
#define INIT_DISP_HORIZONTAL_SIZE   540
#define INIT_DISP_VERTICAL_SIZE     576


//flags2
#define PTS_DTS_FLAGS    0xC0
#define ESCR_FLAG        0x20
#define ES_RATE_FLAG     0x10
#define DSM_TRICK_FLAG   0x08
#define ADD_CPY_FLAG     0x04
#define PES_CRC_FLAG     0x02
#define PES_EXT_FLAG     0x01

//pts_dts flags 
#define PTS_ONLY         0x80
#define PTS_DTS          0xC0

#define TS_SIZE        188
#define TRANS_ERROR    0x80
#define PAY_START      0x40
#define TRANS_PRIO     0x20
#define PID_MASK_HI    0x1F
//flags
#define TRANS_SCRMBL1  0x80
#define TRANS_SCRMBL2  0x40
#define ADAPT_FIELD    0x20
#define PAYLOAD        0x10
#define COUNT_MASK     0x0F

// adaptation flags
#define DISCON_IND     0x80
#define RAND_ACC_IND   0x40
#define ES_PRI_IND     0x20
#define PCR_FLAG       0x10
#define OPCR_FLAG      0x08
#define SPLICE_FLAG    0x04
#define TRANS_PRIV     0x02
#define ADAP_EXT_FLAG  0x01

// adaptation extension flags
#define LTW_FLAG       0x80
#define PIECE_RATE     0x40
#define SEAM_SPLICE    0x20


#define MAX_PLENGTH 0xFFFF
#define MMAX_PLENGTH (256*MAX_PLENGTH)

#ifndef IPACKS
#define IPACKS 2048
#endif

typedef struct ipack_s {
	int size;
	int found;
	u8 *buf;
	u8 cid;
	uint32_t plength;
	u8 plen[2];
	u8 flag1;
	u8 flag2;
	u8 hlength;
	u8 pts[5];
	u16 *pid;
	int mpeg;
	u8 check;
	int which;
	int done;
	void *data;
	void (*func)(u8 *buf,  int size, void *priv);
	int count;
	int repack_subids;
} ipack;

typedef struct video_i{
	uint32_t horizontal_size;
	uint32_t vertical_size;
	uint32_t aspect_ratio;
	uint32_t framerate;
	uint32_t video_format;
	uint32_t bit_rate;
	uint32_t comp_bit_rate;
	uint32_t vbv_buffer_size;
        int16_t  vbv_delay;
	uint32_t CSPF;
	uint32_t off;
} VideoInfo;            


#define OFF_SIZE 4
#define FIRST_FIELD 0
#define SECOND_FIELD 1
#define VIDEO_FRAME_PICTURE 0x03

typedef struct mpg_picture_s{
        int       channel;
	VideoInfo vinfo;
        uint32_t  *sequence_gop_header;
        uint32_t  *picture_header;
        int32_t   time_code;
        int       low_delay;
        int       closed_gop;
        int       broken_link;
        int       sequence_header_flag;      
        int       gop_flag;              
        int       sequence_end_flag;
                                                                
        uint8_t   profile_and_level;
        int32_t   picture_coding_parameter;
        uint32_t  matrix[32];
        int8_t    matrix_change_flag;

        uint8_t   picture_header_parameter;
  /* bit 0 - 2: bwd f code
     bit 3    : fpb vector
     bit 4 - 6: fwd f code
     bit 7    : fpf vector */

        int       mpeg1_flag;
        int       progressive_sequence;
        int       sequence_display_extension_flag;
        uint32_t  sequence_header_data;
        int16_t   last_frame_centre_horizontal_offset;
        int16_t   last_frame_centre_vertical_offset;

        uint32_t  pts[2]; /* [0] 1st field, [1] 2nd field */
        int       top_field_first;
        int       repeat_first_field;
        int       progressive_frame;
        int       bank;
        int       forward_bank;
        int       backward_bank;
        int       compress;
        int16_t   frame_centre_horizontal_offset[OFF_SIZE];                   
                  /* [0-2] 1st field, [3] 2nd field */
        int16_t   frame_centre_vertical_offset[OFF_SIZE];
                  /* [0-2] 1st field, [3] 2nd field */
        int16_t   temporal_reference[2];                               
                  /* [0] 1st field, [1] 2nd field */

        int8_t    picture_coding_type[2];
                 /* [0] 1st field, [1] 2nd field */
        int8_t    picture_structure[2];
                 /* [0] 1st field, [1] 2nd field */
        int8_t    picture_display_extension_flag[2];
                 /* [0] 1st field, [1] 2nd field */
                 /* picture_display_extenion() 0:no 1:exit*/
        int8_t    pts_flag[2];
                 /* [0] 1st field, [1] 2nd field */
} mpg_picture;




typedef struct audio_i{
	int layer               ;
	uint32_t bit_rate    ;
	uint32_t frequency   ;
	uint32_t mode                ;
	uint32_t mode_extension ;
	uint32_t emphasis    ;
	uint32_t framesize;
	uint32_t off;
} AudioInfo;

int dvb_filter_get_ac3info(uint8_t *mbuf, int count, AudioInfo *ai, int pr);


#endif
