/*
 * IEEE 1394 for Linux
 *
 * kernel ISO transmission/reception
 *
 * Copyright (C) 2002 Maas Digital LLC
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#ifndef IEEE1394_ISO_H
#define IEEE1394_ISO_H

#include "hosts.h"
#include "dma.h"

/* high-level ISO interface */

/* per-packet data embedded in the ringbuffer */
struct hpsb_iso_packet_info {
	unsigned short len;
	unsigned short cycle;
	unsigned char channel; /* recv only */
	unsigned char tag;
	unsigned char sy;
};

/*
 * each packet in the ringbuffer consists of three things:
 * 1. the packet's data payload (no isochronous header)
 * 2. a struct hpsb_iso_packet_info
 * 3. some empty space before the next packet
 *
 * packets are separated by hpsb_iso.buf_stride bytes
 * an even number of packets fit on one page
 * no packet can be larger than one page
 */

enum hpsb_iso_type { HPSB_ISO_RECV = 0, HPSB_ISO_XMIT = 1 };

struct hpsb_iso {
	enum hpsb_iso_type type;

	/* pointer to low-level driver and its private data */
	struct hpsb_host *host;
	void *hostdata;
	
	/* function to be called (from interrupt context) when the iso status changes */
	void (*callback)(struct hpsb_iso*);

	int speed; /* SPEED_100, 200, or 400 */
	int channel;

	/* greatest # of packets between interrupts - controls
	   the maximum latency of the buffer */
	int irq_interval;
	
	/* the packet ringbuffer */
	struct dma_region buf;

	/* # of packets in the ringbuffer */
	unsigned int buf_packets;

	/* offset between successive packets, in bytes -
	   you can assume that this is a power of 2,
	   and less than or equal to the page size */	
	int buf_stride;
	
	/* largest possible packet size, in bytes */
	unsigned int max_packet_size;

	/* offset relative to (buf.kvirt + N*buf_stride) at which
	   the data payload begins for packet N */
	int packet_data_offset;
	
	/* offset relative to (buf.kvirt + N*buf_stride) at which the
	   struct hpsb_iso_packet_info is stored for packet N */
	int packet_info_offset;

	/* the index of the next packet that will be produced
	   or consumed by the user */
	int first_packet;

	/* number of packets owned by the low-level driver and
	   queued for transmission or reception.
	   this is related to the number of packets available
	   to the user process: n_ready = buf_packets - n_dma_packets */	
	atomic_t n_dma_packets;

	/* how many times the buffer has overflowed or underflowed */
	atomic_t overflows;

	/* private flags to track initialization progress */
#define HPSB_ISO_DRIVER_INIT     (1<<0)
#define HPSB_ISO_DRIVER_STARTED  (1<<1)
	unsigned int flags;

	/* # of packets left to prebuffer (xmit only) */
	int prebuffer;

	/* starting cycle (xmit only) */
	int start_cycle;
};

/* functions available to high-level drivers (e.g. raw1394) */

/* allocate the buffer and DMA context */

struct hpsb_iso* hpsb_iso_xmit_init(struct hpsb_host *host,
				    unsigned int buf_packets,
				    unsigned int max_packet_size,
				    int channel,
				    int speed,
				    int irq_interval,
				    void (*callback)(struct hpsb_iso*));

struct hpsb_iso* hpsb_iso_recv_init(struct hpsb_host *host,
				    unsigned int buf_packets,
				    unsigned int max_packet_size,
				    int channel,
				    int irq_interval,
				    void (*callback)(struct hpsb_iso*));

/* start/stop DMA */
int hpsb_iso_xmit_start(struct hpsb_iso *iso, int start_on_cycle, int prebuffer);
int hpsb_iso_recv_start(struct hpsb_iso *iso, int start_on_cycle);
void hpsb_iso_stop(struct hpsb_iso *iso);

/* deallocate buffer and DMA context */
void hpsb_iso_shutdown(struct hpsb_iso *iso);

/* N packets have been written to the buffer; queue them for transmission */
int  hpsb_iso_xmit_queue_packets(struct hpsb_iso *xmit, unsigned int n_packets);

/* N packets have been read out of the buffer, re-use the buffer space */
int  hpsb_iso_recv_release_packets(struct hpsb_iso *recv, unsigned int n_packets);

/* returns # of packets ready to send or receive */
int hpsb_iso_n_ready(struct hpsb_iso *iso);

/* returns a pointer to the payload of packet 'pkt' */
unsigned char* hpsb_iso_packet_data(struct hpsb_iso *iso, unsigned int pkt);

/* returns a pointer to the info struct of packet 'pkt' */
struct hpsb_iso_packet_info* hpsb_iso_packet_info(struct hpsb_iso *iso, unsigned int pkt);

#endif /* IEEE1394_ISO_H */
