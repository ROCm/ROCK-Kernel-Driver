/* demux.h 
 *
 * Copyright (c) 2002 Convergence GmbH
 * 
 * based on code:
 * Copyright (c) 2000 Nokia Research Center
 *                    Tampere, FINLAND
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

#ifndef __DEMUX_H 
#define __DEMUX_H 

#ifndef __KERNEL__ 
#define __KERNEL__ 
#endif 

#include <linux/types.h>
#include <linux/list.h> 
#include <linux/time.h> 
#include <linux/errno.h>

/*--------------------------------------------------------------------------*/ 
/* Common definitions */ 
/*--------------------------------------------------------------------------*/ 

/*
 * DMX_MAX_FILTER_SIZE: Maximum length (in bytes) of a section/PES filter.
 */ 

#ifndef DMX_MAX_FILTER_SIZE 
#define DMX_MAX_FILTER_SIZE 18
#endif 

/*
 * dmx_success_t: Success codes for the Demux Callback API. 
 */ 

typedef enum { 
  DMX_OK = 0, /* Received Ok */ 
  DMX_LENGTH_ERROR, /* Incorrect length */ 
  DMX_OVERRUN_ERROR, /* Receiver ring buffer overrun */ 
  DMX_CRC_ERROR, /* Incorrect CRC */ 
  DMX_FRAME_ERROR, /* Frame alignment error */ 
  DMX_FIFO_ERROR, /* Receiver FIFO overrun */ 
  DMX_MISSED_ERROR /* Receiver missed packet */ 
} dmx_success_t; 

/*--------------------------------------------------------------------------*/ 
/* TS packet reception */ 
/*--------------------------------------------------------------------------*/

/* TS filter type for set() */

#define TS_PACKET       1   /* send TS packets (188 bytes) to callback (default) */ 
#define	TS_PAYLOAD_ONLY 2   /* in case TS_PACKET is set, only send the TS
			       payload (<=184 bytes per packet) to callback */
#define TS_DECODER      4   /* send stream to built-in decoder (if present) */

/* PES type for filters which write to built-in decoder */
/* these should be kept identical to the types in dmx.h */

typedef enum
{  /* also send packets to decoder (if it exists) */
        DMX_TS_PES_AUDIO0,
	DMX_TS_PES_VIDEO0,
	DMX_TS_PES_TELETEXT0,
	DMX_TS_PES_SUBTITLE0,
	DMX_TS_PES_PCR0,

        DMX_TS_PES_AUDIO1,
	DMX_TS_PES_VIDEO1,
	DMX_TS_PES_TELETEXT1,
	DMX_TS_PES_SUBTITLE1,
	DMX_TS_PES_PCR1,

        DMX_TS_PES_AUDIO2,
	DMX_TS_PES_VIDEO2,
	DMX_TS_PES_TELETEXT2,
	DMX_TS_PES_SUBTITLE2,
	DMX_TS_PES_PCR2,

        DMX_TS_PES_AUDIO3,
	DMX_TS_PES_VIDEO3,
	DMX_TS_PES_TELETEXT3,
	DMX_TS_PES_SUBTITLE3,
	DMX_TS_PES_PCR3,

	DMX_TS_PES_OTHER
} dmx_ts_pes_t;

#define DMX_TS_PES_AUDIO    DMX_TS_PES_AUDIO0
#define DMX_TS_PES_VIDEO    DMX_TS_PES_VIDEO0
#define DMX_TS_PES_TELETEXT DMX_TS_PES_TELETEXT0
#define DMX_TS_PES_SUBTITLE DMX_TS_PES_SUBTITLE0
#define DMX_TS_PES_PCR      DMX_TS_PES_PCR0


struct dmx_ts_feed_s { 
        int is_filtering; /* Set to non-zero when filtering in progress */
        struct dmx_demux_s *parent; /* Back-pointer */
        void *priv; /* Pointer to private data of the API client */ 
        int (*set) (struct dmx_ts_feed_s *feed, 
		    uint16_t pid,
		    int type, 
		    dmx_ts_pes_t pes_type,
		    size_t callback_length, 
		    size_t circular_buffer_size, 
		    int descramble, 
		    struct timespec timeout); 
        int (*start_filtering) (struct dmx_ts_feed_s* feed); 
        int (*stop_filtering) (struct dmx_ts_feed_s* feed); 
};

typedef struct dmx_ts_feed_s dmx_ts_feed_t; 

/*--------------------------------------------------------------------------*/ 
/* Section reception */ 
/*--------------------------------------------------------------------------*/ 

typedef struct { 
        __u8 filter_value [DMX_MAX_FILTER_SIZE]; 
        __u8 filter_mask [DMX_MAX_FILTER_SIZE]; 
        __u8 filter_mode [DMX_MAX_FILTER_SIZE]; 
        struct dmx_section_feed_s* parent; /* Back-pointer */ 
        void* priv; /* Pointer to private data of the API client */ 
} dmx_section_filter_t;

struct dmx_section_feed_s { 
        int is_filtering; /* Set to non-zero when filtering in progress */ 
        struct dmx_demux_s* parent; /* Back-pointer */
        void* priv; /* Pointer to private data of the API client */ 

        int check_crc;
	u32 crc_val;

        u8 secbuf[4096];
        int secbufp;
        int seclen;

        int (*set) (struct dmx_section_feed_s* feed, 
		    __u16 pid, 
		    size_t circular_buffer_size, 
		    int descramble, 
		    int check_crc); 
        int (*allocate_filter) (struct dmx_section_feed_s* feed, 
				dmx_section_filter_t** filter); 
        int (*release_filter) (struct dmx_section_feed_s* feed, 
			       dmx_section_filter_t* filter); 
        int (*start_filtering) (struct dmx_section_feed_s* feed); 
        int (*stop_filtering) (struct dmx_section_feed_s* feed); 
};
typedef struct dmx_section_feed_s dmx_section_feed_t; 

/*--------------------------------------------------------------------------*/ 
/* Callback functions */ 
/*--------------------------------------------------------------------------*/ 

typedef int (*dmx_ts_cb) ( const u8 * buffer1, 
			   size_t buffer1_length,
			   const u8 * buffer2, 
			   size_t buffer2_length,
			   dmx_ts_feed_t* source, 
			   dmx_success_t success); 

typedef int (*dmx_section_cb) (	const u8 * buffer1,
				size_t buffer1_len,
				const u8 * buffer2, 
				size_t buffer2_len,
			       	dmx_section_filter_t * source,
			       	dmx_success_t success);

/*--------------------------------------------------------------------------*/ 
/* DVB Front-End */
/*--------------------------------------------------------------------------*/ 

typedef enum { 
	DMX_MEMORY_FE,
	DMX_FRONTEND_0,
	DMX_FRONTEND_1,
	DMX_FRONTEND_2,
	DMX_FRONTEND_3,
	DMX_STREAM_0,    /* external stream input, e.g. LVDS */
	DMX_STREAM_1,
	DMX_STREAM_2,
	DMX_STREAM_3
} dmx_frontend_source_t; 

typedef struct { 
        /* The following char* fields point to NULL terminated strings */ 
        char* id;                    /* Unique front-end identifier */ 
        char* vendor;                /* Name of the front-end vendor */ 
        char* model;                 /* Name of the front-end model */ 
        struct list_head connectivity_list; /* List of front-ends that can 
					       be connected to a particular 
					       demux */ 
        void* priv;     /* Pointer to private data of the API client */ 
        dmx_frontend_source_t source;
} dmx_frontend_t;

/*--------------------------------------------------------------------------*/ 
/* MPEG-2 TS Demux */ 
/*--------------------------------------------------------------------------*/ 

/* 
 * Flags OR'ed in the capabilites field of struct dmx_demux_s. 
 */ 

#define DMX_TS_FILTERING                        1 
#define DMX_PES_FILTERING                       2 
#define DMX_SECTION_FILTERING                   4 
#define DMX_MEMORY_BASED_FILTERING              8    /* write() available */ 
#define DMX_CRC_CHECKING                        16 
#define DMX_TS_DESCRAMBLING                     32 
#define DMX_SECTION_PAYLOAD_DESCRAMBLING        64 
#define DMX_MAC_ADDRESS_DESCRAMBLING            128 

/* 
 * Demux resource type identifier. 
*/ 

/* 
 * DMX_FE_ENTRY(): Casts elements in the list of registered 
 * front-ends from the generic type struct list_head
 * to the type * dmx_frontend_t
 *. 
*/

#define DMX_FE_ENTRY(list) list_entry(list, dmx_frontend_t, connectivity_list) 

struct dmx_demux_s { 
        /* The following char* fields point to NULL terminated strings */ 
        char* id;                    /* Unique demux identifier */ 
        char* vendor;                /* Name of the demux vendor */ 
        char* model;                 /* Name of the demux model */ 
        __u32 capabilities;          /* Bitfield of capability flags */ 
        dmx_frontend_t* frontend;    /* Front-end connected to the demux */ 
        struct list_head reg_list;   /* List of registered demuxes */
        void* priv;                  /* Pointer to private data of the API client */ 
        int users;                   /* Number of users */
        int (*open) (struct dmx_demux_s* demux); 
        int (*close) (struct dmx_demux_s* demux); 
        int (*write) (struct dmx_demux_s* demux, const char* buf, size_t count); 
        int (*allocate_ts_feed) (struct dmx_demux_s* demux, 
				 dmx_ts_feed_t** feed, 
				 dmx_ts_cb callback); 
        int (*release_ts_feed) (struct dmx_demux_s* demux, 
				dmx_ts_feed_t* feed); 
        int (*allocate_section_feed) (struct dmx_demux_s* demux, 
				      dmx_section_feed_t** feed, 
				      dmx_section_cb callback); 
        int (*release_section_feed) (struct dmx_demux_s* demux,
				     dmx_section_feed_t* feed); 
        int (*descramble_mac_address) (struct dmx_demux_s* demux, 
				       __u8* buffer1, 
				       size_t buffer1_length, 
				       __u8* buffer2, 
				       size_t buffer2_length,
				       __u16 pid); 
        int (*descramble_section_payload) (struct dmx_demux_s* demux,
					   __u8* buffer1, 
					   size_t buffer1_length,
					   __u8* buffer2, size_t buffer2_length,
					   __u16 pid); 
        int (*add_frontend) (struct dmx_demux_s* demux, 
			     dmx_frontend_t* frontend); 
        int (*remove_frontend) (struct dmx_demux_s* demux,
				dmx_frontend_t* frontend); 
        struct list_head* (*get_frontends) (struct dmx_demux_s* demux); 
        int (*connect_frontend) (struct dmx_demux_s* demux, 
				 dmx_frontend_t* frontend); 
        int (*disconnect_frontend) (struct dmx_demux_s* demux); 

        int (*get_pes_pids) (struct dmx_demux_s* demux, __u16 *pids);

        int (*get_stc) (struct dmx_demux_s* demux, unsigned int num,
			uint64_t *stc, unsigned int *base);
}; 
typedef struct dmx_demux_s dmx_demux_t; 

/*--------------------------------------------------------------------------*/ 
/* Demux directory */ 
/*--------------------------------------------------------------------------*/ 

/* 
 * DMX_DIR_ENTRY(): Casts elements in the list of registered 
 * demuxes from the generic type struct list_head* to the type dmx_demux_t
 *. 
 */ 

#define DMX_DIR_ENTRY(list) list_entry(list, dmx_demux_t, reg_list)

int dmx_register_demux (dmx_demux_t* demux); 
int dmx_unregister_demux (dmx_demux_t* demux); 
struct list_head* dmx_get_demuxes (void); 

#endif /* #ifndef __DEMUX_H */

