/* -*- c-basic-offset: 8 -*-
 *
 * amdtp.c - Audio and Music Data Transmission Protocol Driver
 * Copyright (C) 2001 Kristian Høgsberg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* OVERVIEW
 * --------
 *
 * The AMDTP driver is designed to expose the IEEE1394 bus as a
 * regular OSS soundcard, i.e. you can link /dev/dsp to /dev/amdtp and
 * then your favourite MP3 player, game or whatever sound program will
 * output to an IEEE1394 isochronous channel.  The signal destination
 * could be a set of IEEE1394 loudspeakers (if and when such things
 * become available) or an amplifier with IEEE1394 input (like the
 * Sony STR-LSA1).  The driver only handles the actual streaming, some
 * connection management is also required for this to actually work.
 * That is outside the scope of this driver, and furthermore it is not
 * really standardized yet.
 *
 * The Audio and Music Data Tranmission Protocol is avaiable at
 *
 *     http://www.1394ta.org/Download/Technology/Specifications/2001/AM20Final-jf2.pdf
 *
 *
 * TODO
 * ----
 *
 * - We should be able to change input sample format between LE/BE, as
 *   we already shift the bytes around when we construct the iso
 *   packets.
 *
 * - Fix DMA stop after bus reset!
 *
 * - Implement poll.
 *
 * - Clean up iso context handling in ohci1394.
 *
 *
 * MAYBE TODO
 * ----------
 *
 * - Receive data for local playback or recording.  Playback requires
 *   soft syncing with the sound card.
 *
 * - Signal processing, i.e. receive packets, do some processing, and
 *   transmit them again using the same packet structure and timestamps
 *   offset by processing time.
 *
 * - Maybe make an ALSA interface, that is, create a file_ops
 *   implementation that recognizes ALSA ioctls and uses defaults for
 *   things that can't be controlled through ALSA (iso channel).
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include "hosts.h"
#include "highlevel.h"
#include "ieee1394.h"
#include "ieee1394_core.h"
#include "ohci1394.h"

#include "amdtp.h"
#include "cmp.h"

#define FMT_AMDTP 0x10
#define FDF_AM824 0x00
#define FDF_SFC_32KHZ  0x00 /* 32kHz */
#define FDF_SFC_44K1HZ 0x01 /* 44.1kHz */
#define FDF_SFC_48KHZ  0x02 /* 44.1kHz */

struct descriptor_block {
	struct output_more_immediate {
		u32 control;
		u32 pad0;
		u32 skip;
		u32 pad1;
		u32 header[4];
	} header_desc;

	struct output_last {
		u32 control;
		u32 data_address;
		u32 branch;
		u32 status;
	} payload_desc;
};

struct packet {
	struct descriptor_block *db;
	dma_addr_t db_bus;
	quadlet_t *payload;
	dma_addr_t payload_bus;
};

struct fraction {
	int integer;
	int numerator;
	int denominator;
	int counter;
};

#define PACKET_LIST_SIZE 256
#define MAX_PACKET_LISTS 4

struct packet_list {
	struct list_head link;
	int last_cycle_count;
	struct packet packets[PACKET_LIST_SIZE];
};

#define BUFFER_SIZE 128

/* This implements a circular buffer for incoming samples. */

struct buffer {
	int head, tail, length, size;
	unsigned char data[0];
};

struct stream {
	int iso_channel;
	int format;
	int rate;
	int dimension;
	int fdf;
	struct cmp_pcr *opcr;

	/* Input samples are copied here. */
	struct buffer *input;

	/* ISO Packer state */
	unsigned char dbc;
	struct packet_list *current_packet_list;
	int current_packet;
	struct fraction packet_size_fraction;

	/* We use these to generate control bits when we are packing
	 * iec958 data.
	 */
	int iec958_frame_count;
	int iec958_rate_code;

	/* The cycle_count and cycle_offset fields are used for the
	 * synchronization timestamps (syt) in the cip header.  They
	 * are incremented by at least a cycle every time we put a
	 * time stamp in a packet.  As we dont time stamp all
	 * packages, cycle_count isn't updated in every cycle, and
	 * sometimes it's incremented by 2.  Thus, we have
	 * cycle_count2, which is simply incremented by one with each
	 * packet, so we can compare it to the transmission time
	 * written back in the dma programs.
	 */
	atomic_t cycle_count, cycle_count2;
	int cycle_offset;
	struct fraction syt_fraction;
	int syt_interval;
	int stale_count;

	/* Theses fields control the sample output to the DMA engine.
	 * The dma_packet_lists list holds packet lists currently
	 * queued for dma; the head of the list is currently being
	 * processed.  The last program in a packet list generates an
	 * interrupt, which removes the head from dma_packet_lists and
	 * puts it back on the free list.
	 */
	struct list_head dma_packet_lists;
	struct list_head free_packet_lists;
        wait_queue_head_t packet_list_wait;
	spinlock_t packet_list_lock;
	int iso_context;
	struct pci_pool *descriptor_pool, *packet_pool;

	/* Streams at a host controller are chained through this field. */
	struct list_head link;
	struct amdtp_host *host;
};

struct amdtp_host {
	struct hpsb_host *host;
	struct ti_ohci *ohci;
	struct list_head stream_list;
	spinlock_t stream_list_lock;
	struct list_head link;
};

static struct hpsb_highlevel *amdtp_highlevel;
static LIST_HEAD(host_list);
static spinlock_t host_list_lock = SPIN_LOCK_UNLOCKED;

/* FIXME: This doesn't belong here... */

#define OHCI1394_CONTEXT_CYCLE_MATCH 0x80000000
#define OHCI1394_CONTEXT_RUN         0x00008000
#define OHCI1394_CONTEXT_WAKE        0x00001000
#define OHCI1394_CONTEXT_DEAD        0x00000800
#define OHCI1394_CONTEXT_ACTIVE      0x00000400

static inline int ohci1394_alloc_it_ctx(struct ti_ohci *ohci)
{
	int i;

	for (i = 0; i < ohci->nb_iso_xmit_ctx; i++)
		if (!test_and_set_bit(i, &ohci->it_ctx_usage))
			return i;

	return -EBUSY;
}
	
static inline void ohci1394_free_it_ctx(struct ti_ohci *ohci, int ctx)
{
	clear_bit(ctx, &ohci->it_ctx_usage);
}


void ohci1394_start_it_ctx(struct ti_ohci *ohci, int ctx,
			   dma_addr_t first_cmd, int z, int cycle_match)
{
	reg_write(ohci, OHCI1394_IsoXmitIntMaskSet, 1 << ctx);
	reg_write(ohci, OHCI1394_IsoXmitCommandPtr + ctx * 16, first_cmd | z);
	reg_write(ohci, OHCI1394_IsoXmitContextControlClear + ctx * 16, ~0);
	wmb();
	reg_write(ohci, OHCI1394_IsoXmitContextControlSet + ctx * 16,
		  OHCI1394_CONTEXT_CYCLE_MATCH | (cycle_match << 16) |
		  OHCI1394_CONTEXT_RUN);
}

void ohci1394_wake_it_ctx(struct ti_ohci *ohci, int ctx)
{
	reg_write(ohci, OHCI1394_IsoXmitContextControlSet + ctx * 16,
		  OHCI1394_CONTEXT_WAKE);
}

void ohci1394_stop_it_ctx(struct ti_ohci *ohci, int ctx)
{
	u32 control;
	int wait;

	reg_write(ohci, OHCI1394_IsoXmitIntMaskClear, 1 << ctx);
	reg_write(ohci, OHCI1394_IsoXmitContextControlClear + ctx * 16,
		  OHCI1394_CONTEXT_RUN);
	wmb();

	for (wait = 0; wait < 5; wait++) {
		control = reg_read(ohci, OHCI1394_IsoXmitContextControlSet + ctx * 16);
		if ((control & OHCI1394_CONTEXT_ACTIVE) == 0)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}
}

/* Note: we can test if free_packet_lists is empty without aquiring
 * the packet_list_lock.  The interrupt handler only adds to the free
 * list, there is no race condition between testing the list non-empty
 * and acquiring the lock.
 */

static struct packet_list *stream_get_free_packet_list(struct stream *s)
{
	struct packet_list *pl;
	unsigned long flags;

	if (list_empty(&s->free_packet_lists))
		return NULL;

	spin_lock_irqsave(&s->packet_list_lock, flags);
	pl = list_entry(s->free_packet_lists.next, struct packet_list, link);
	list_del(&pl->link);
	spin_unlock_irqrestore(&s->packet_list_lock, flags);

	return pl;
}

static void stream_put_dma_packet_list(struct stream *s,
				       struct packet_list *pl)
{
	unsigned long flags;
	struct packet_list *prev;

	/* Remember the cycle_count used for timestamping the last packet. */
	pl->last_cycle_count = atomic_read(&s->cycle_count2) - 1;
	pl->packets[PACKET_LIST_SIZE - 1].db->payload_desc.branch = 0;

	spin_lock_irqsave(&s->packet_list_lock, flags);
	list_add_tail(&pl->link, &s->dma_packet_lists);
	spin_unlock_irqrestore(&s->packet_list_lock, flags);

	prev = list_entry(pl->link.prev, struct packet_list, link);
	if (pl->link.prev != &s->dma_packet_lists) {
		struct packet *last = &prev->packets[PACKET_LIST_SIZE - 1];
		last->db->payload_desc.branch = pl->packets[0].db_bus | 3;
		ohci1394_wake_it_ctx(s->host->ohci, s->iso_context);
	}
	else {
		u32 syt, cycle_count;

		cycle_count = reg_read(s->host->host->hostdata,
				       OHCI1394_IsochronousCycleTimer) >> 12;
		syt = (pl->packets[0].payload[1] >> 12) & 0x0f;
		cycle_count = (cycle_count & ~0x0f) + 32 + syt;
		if ((cycle_count & 0x1fff) >= 8000)
			cycle_count = cycle_count - 8000 + 0x2000;

		ohci1394_start_it_ctx(s->host->ohci, s->iso_context,
				      pl->packets[0].db_bus, 3,
				      cycle_count & 0x7fff);
	}
}

static void stream_shift_packet_lists(struct stream *s)
{
	struct packet_list *pl;
	struct packet *last;
	int diff;

	if (list_empty(&s->dma_packet_lists)) {
		HPSB_ERR("empty dma_packet_lists in %s", __FUNCTION__);
		return;
	}

	/* Now that we know the list is non-empty, we can get the head
	 * of the list without locking, because the process context
	 * only adds to the tail.  
	 */
	pl = list_entry(s->dma_packet_lists.next, struct packet_list, link);
	last = &pl->packets[PACKET_LIST_SIZE - 1];

	/* This is weird... if we stop dma processing in the middle of
	 * a packet list, the dma context immediately generates an
	 * interrupt if we enable it again later.  This only happens
	 * when amdtp_release is interrupted while waiting for dma to
	 * complete, though.  Anyway, we detect this by seeing that
	 * the status of the dma descriptor that we expected an
	 * interrupt from is still 0.
	 */
	if (last->db->payload_desc.status == 0) {
		HPSB_INFO("weird interrupt...");
		return;
	}		

	/* If the last descriptor block does not specify a branch
	 * address, we have a sample underflow.
	 */
	if (last->db->payload_desc.branch == 0)
		HPSB_INFO("FIXME: sample underflow...");

	/* Here we check when (which cycle) the last packet was sent
	 * and compare it to what the iso packer was using at the
	 * time.  If there is a mismatch, we adjust the cycle count in
	 * the iso packer.  However, there are still up to
	 * MAX_PACKET_LISTS packet lists queued with bad time stamps,
	 * so we disable time stamp monitoring for the next
	 * MAX_PACKET_LISTS packet lists.
	 */
	diff = (last->db->payload_desc.status - pl->last_cycle_count) & 0xf;
	if (diff > 0 && s->stale_count == 0) {
		atomic_add(diff, &s->cycle_count);
		atomic_add(diff, &s->cycle_count2);
		s->stale_count = MAX_PACKET_LISTS;
	}

	if (s->stale_count > 0)
		s->stale_count--;

	/* Finally, we move the packet list that was just processed
	 * back to the free list, and notify any waiters.
	 */
	spin_lock(&s->packet_list_lock);
	list_del(&pl->link);
	list_add_tail(&pl->link, &s->free_packet_lists);
	spin_unlock(&s->packet_list_lock);

	wake_up_interruptible(&s->packet_list_wait);
}

static struct packet *stream_current_packet(struct stream *s)
{
	if (s->current_packet_list == NULL &&
	    (s->current_packet_list = stream_get_free_packet_list(s)) == NULL)
		return NULL;

	return &s->current_packet_list->packets[s->current_packet];
}
	
static void stream_queue_packet(struct stream *s)
{
	s->current_packet++;
	if (s->current_packet == PACKET_LIST_SIZE) {
		stream_put_dma_packet_list(s, s->current_packet_list);
		s->current_packet_list = NULL;
		s->current_packet = 0;
	}
}

/* Integer fractional math.  When we transmit a 44k1Hz signal we must
 * send 5 41/80 samples per isochronous cycle, as these occur 8000
 * times a second.  Of course, we must send an integral number of
 * samples in a packet, so we use the integer math to alternate
 * between sending 5 and 6 samples per packet.
 */

static void fraction_init(struct fraction *f, int numerator, int denominator)
{
	f->integer = numerator / denominator;
	f->numerator = numerator % denominator;
	f->denominator = denominator;
	f->counter = 0;
}

static int fraction_next_size(struct fraction *f)
{
	return f->integer + ((f->counter + f->numerator) / f->denominator);
}

static void fraction_inc(struct fraction *f)
{
	f->counter = (f->counter + f->numerator) % f->denominator;
}

static void amdtp_irq_handler(int card, quadlet_t isoRecvIntEvent,
			      quadlet_t isoXmitIntEvent, void *data)
{
	struct amdtp_host *host = data;
	struct list_head *lh;
	struct stream *s = NULL;

	spin_lock(&host->stream_list_lock);
	list_for_each(lh, &host->stream_list) {
		s = list_entry(lh, struct stream, link);
		if (isoXmitIntEvent & (1 << s->iso_context))
			break;
	}
	spin_unlock(&host->stream_list_lock);

	if (s != NULL)
		stream_shift_packet_lists(s);
}

void packet_initialize(struct packet *p, struct packet *next)
{
	/* Here we initialize the dma descriptor block for
	 * transferring one iso packet.  We use two descriptors per
	 * packet: an OUTPUT_MORE_IMMMEDIATE descriptor for the
	 * IEEE1394 iso packet header and an OUTPUT_LAST descriptor
	 * for the payload.
	 */

	p->db->header_desc.control =
		DMA_CTL_OUTPUT_MORE | DMA_CTL_IMMEDIATE | 8;
	p->db->header_desc.skip = 0;

	if (next) {
		p->db->payload_desc.control = 
			DMA_CTL_OUTPUT_LAST | DMA_CTL_BRANCH;
		p->db->payload_desc.branch = next->db_bus | 3;
	}
	else {
		p->db->payload_desc.control = 
			DMA_CTL_OUTPUT_LAST | DMA_CTL_BRANCH |
			DMA_CTL_UPDATE | DMA_CTL_IRQ;
		p->db->payload_desc.branch = 0;
	}
	p->db->payload_desc.data_address = p->payload_bus;
	p->db->payload_desc.status = 0;
}

struct packet_list *packet_list_alloc(struct stream *s)
{
	int i;
	struct packet_list *pl;
	struct packet *next;

	pl = kmalloc(sizeof *pl, SLAB_KERNEL);
	if (pl == NULL)
		return NULL;

	for (i = 0; i < PACKET_LIST_SIZE; i++) {
		struct packet *p = &pl->packets[i];
		p->db = pci_pool_alloc(s->descriptor_pool, SLAB_KERNEL,
				       &p->db_bus);
		p->payload = pci_pool_alloc(s->packet_pool, SLAB_KERNEL,
					    &p->payload_bus);
	}

	for (i = 0; i < PACKET_LIST_SIZE; i++) {
		if (i < PACKET_LIST_SIZE - 1)
			next = &pl->packets[i + 1];
		else 
			next = NULL;
		packet_initialize(&pl->packets[i], next);
	}

	return pl;
}

void packet_list_free(struct packet_list *pl, struct stream *s)
{
	int i;

	for (i = 0; i < PACKET_LIST_SIZE; i++) {
		struct packet *p = &pl->packets[i];
		pci_pool_free(s->descriptor_pool, p->db, p->db_bus);
		pci_pool_free(s->packet_pool, p->payload, p->payload_bus);
	}
	kfree(pl);
}

static struct buffer *buffer_alloc(int size)
{
	struct buffer *b;

	b = kmalloc(sizeof *b + size, SLAB_KERNEL);
	b->head = 0;
	b->tail = 0;
	b->length = 0;
	b->size = size;

	return b;
}

static unsigned char *buffer_get_bytes(struct buffer *buffer, int size)
{
	unsigned char *p;

	if (buffer->head + size > buffer->size)
		BUG();

	p = &buffer->data[buffer->head];
	buffer->head += size;
	if (buffer->head == buffer->size)
		buffer->head = 0;
	buffer->length -= size;

	return p;
}

static unsigned char *buffer_put_bytes(struct buffer *buffer,
				       int max, int *actual)
{
	int length;
	unsigned char *p;

	p = &buffer->data[buffer->tail];
	length = min(buffer->size - buffer->length, max);
	if (buffer->tail + length < buffer->size) {
		*actual = length;
		buffer->tail += length;
	}
	else {
		*actual = buffer->size - buffer->tail;
		 buffer->tail = 0;
	}

	buffer->length += *actual;
	return p;
}

static u32 get_iec958_header_bits(struct stream *s, int sub_frame, u32 sample)
{
	int csi, parity, shift;
	int block_start;
	u32 bits;

	switch (s->iec958_frame_count) {
	case 1:
		csi = s->format == AMDTP_FORMAT_IEC958_AC3;
		break;
	case 2:
	case 9:
		csi = 1;
		break;
	case 24 ... 27:
		csi = (s->iec958_rate_code >> (27 - s->iec958_frame_count)) & 0x01;
		break;
	default:
		csi = 0;
		break;
	}

	block_start = (s->iec958_frame_count == 0 && sub_frame == 0);

	/* The parity bit is the xor of the sample bits and the
	 * channel status info bit. */
	for (shift = 16, parity = sample ^ csi; shift > 0; shift >>= 1)
		parity ^= (parity >> shift);

	bits =  (block_start << 5) |		/* Block start bit */
		((sub_frame == 0) << 4) |	/* Subframe bit */
		((parity & 1) << 3) |		/* Parity bit */
		(csi << 2);			/* Channel status info bit */

	return bits;
}

static u32 get_header_bits(struct stream *s, int sub_frame, u32 sample)
{
	switch (s->format) {
	case AMDTP_FORMAT_IEC958_PCM:
	case AMDTP_FORMAT_IEC958_AC3:
		return get_iec958_header_bits(s, sub_frame, sample);
		
	case AMDTP_FORMAT_RAW:
		return 0x40000000;

	default:
		return 0;
	}
}


static void fill_packet(struct stream *s, struct packet *packet, int nevents)
{
	int size, node_id, i, j;
	quadlet_t *event;
	unsigned char *p;
	u32 control, sample, bits;
	int syt_index, syt, next;

	size = (nevents * s->dimension + 2) * sizeof(quadlet_t);
	node_id = s->host->host->node_id & 0x3f;

	/* Update DMA descriptors */
	packet->db->payload_desc.status = 0;
	control = packet->db->payload_desc.control & 0xffff0000;
	packet->db->payload_desc.control = control | size;

	/* Fill IEEE1394 headers */
	packet->db->header_desc.header[0] =
		(SPEED_100 << 16) | (0x01 << 14) | 
		(s->iso_channel << 8) | (TCODE_ISO_DATA << 4);
	packet->db->header_desc.header[1] = size << 16;
	
	/* Fill cip header */
	syt_index = s->dbc & (s->syt_interval - 1);
	if (syt_index == 0 || syt_index + nevents > s->syt_interval) {
		syt = ((atomic_read(&s->cycle_count) << 12) | 
		       s->cycle_offset) & 0xffff;
		next = fraction_next_size(&s->syt_fraction) + s->cycle_offset;
		/* This next addition should be modulo 8000 (0x1f40),
		 * but we only use the lower 4 bits of cycle_count, so
		 * we dont need the modulo. */
		atomic_add(next / 3072, &s->cycle_count);
		s->cycle_offset = next % 3072;
		fraction_inc(&s->syt_fraction);
	}
	else {
		syt = 0xffff;
		next = 0;
	}
	atomic_inc(&s->cycle_count2);

	packet->payload[0] = cpu_to_be32((node_id << 24) | (s->dimension << 16) | s->dbc);
	packet->payload[1] = cpu_to_be32((1 << 31) | (FMT_AMDTP << 24) | (s->fdf << 16) | syt);

	/* Fill payload */
	for (i = 0, event = &packet->payload[2]; i < nevents; i++) {

		for (j = 0; j < s->dimension; j++) {
			p = buffer_get_bytes(s->input, 2);
			sample = (p[1] << 16) | (p[0] << 8);
			bits = get_header_bits(s, j, sample);
			event[j] = cpu_to_be32((bits << 24) | sample);
		}

		event += s->dimension;
		if (++s->iec958_frame_count == 192)
			s->iec958_frame_count = 0;
	}

	s->dbc += nevents;
}

static void stream_flush(struct stream *s)
{
	struct packet *p;
	int nevents;

	while (nevents = fraction_next_size(&s->packet_size_fraction),
	       p = stream_current_packet(s),
	       nevents * s->dimension * 2 <= s->input->length && p != NULL) {
		fill_packet(s, p, nevents);
		fraction_inc(&s->packet_size_fraction);
		stream_queue_packet(s);
	}
}

static int stream_alloc_packet_lists(struct stream *s)
{
	int max_nevents, max_packet_size, i;

	max_nevents = s->packet_size_fraction.integer;
	if (s->packet_size_fraction.numerator > 0)
		max_nevents++;

	max_packet_size = max_nevents * s->dimension * 4 + 8;
	s->packet_pool = pci_pool_create("packet pool", s->host->ohci->dev,
					 max_packet_size, 0, 0, SLAB_KERNEL);
	if (s->packet_pool == NULL)
		return -1;

	INIT_LIST_HEAD(&s->free_packet_lists);
	INIT_LIST_HEAD(&s->dma_packet_lists);
	for (i = 0; i < MAX_PACKET_LISTS; i++) {
		struct packet_list *pl = packet_list_alloc(s);
		if (pl == NULL)
			break;
		list_add_tail(&pl->link, &s->free_packet_lists);
	}

	return i < MAX_PACKET_LISTS ? -1 : 0;
}

static void stream_free_packet_lists(struct stream *s)
{
	struct list_head *lh, *next;

	if (s->current_packet_list != NULL)
		packet_list_free(s->current_packet_list, s);
	list_for_each_safe(lh, next, &s->dma_packet_lists)
		packet_list_free(list_entry(lh, struct packet_list, link), s);
	list_for_each_safe(lh, next, &s->free_packet_lists)
		packet_list_free(list_entry(lh, struct packet_list, link), s);
	if (s->packet_pool != NULL)
		pci_pool_destroy(s->packet_pool);

	s->current_packet_list = NULL;
	INIT_LIST_HEAD(&s->free_packet_lists);
	INIT_LIST_HEAD(&s->dma_packet_lists);
	s->packet_pool = NULL;
}

static void plug_update(struct cmp_pcr *plug, void *data)
{
	struct stream *s = data;

	HPSB_INFO("plug update: p2p_count=%d, channel=%d",
		  plug->p2p_count, plug->channel);
	s->iso_channel = plug->channel;
	if (plug->p2p_count > 0) {
		/* start streaming */
	}
	else {
		/* stop streaming */
	}
}

static int stream_configure(struct stream *s, int cmd, struct amdtp_ioctl *cfg)
{
	if (cfg->format <= AMDTP_FORMAT_IEC958_AC3)
		s->format = cfg->format;
	else
		return -EINVAL;

	switch (cfg->rate) {
	case 32000:
		s->syt_interval = 8;
		s->fdf = FDF_SFC_32KHZ;
		s->iec958_rate_code = 0x0c;
		s->rate = cfg->rate;
		break;
	case 44100:
		s->syt_interval = 8;
		s->fdf = FDF_SFC_44K1HZ;
		s->iec958_rate_code = 0x00;
		s->rate = cfg->rate;
		break;
	case 48000:
		s->syt_interval = 8;
		s->fdf = FDF_SFC_48KHZ;
		s->iec958_rate_code = 0x04;
		s->rate = cfg->rate;
		break;
	default:
		return -EINVAL;
	}

	fraction_init(&s->packet_size_fraction, s->rate, 8000);

	/* The syt_fraction is initialized to the number of ticks
	 * between syt_interval events.  The number of ticks per
	 * second is 24.576e6, so the number of ticks between
	 * syt_interval events is 24.576e6 * syt_interval / rate.
	 */
	fraction_init(&s->syt_fraction, 24576000 * s->syt_interval, s->rate);

	/* When using the AM824 raw subformat we can stream signals of
	 * any dimension.  The IEC958 subformat, however, only
	 * supports 2 channels.
	 */
	if (s->format == AMDTP_FORMAT_RAW || cfg->dimension == 2)
		s->dimension = cfg->dimension;
	else
		return -EINVAL;

	if (s->opcr != NULL) {
		cmp_unregister_opcr(s->host->host, s->opcr);
		s->opcr = NULL;
	}

	switch(cmd) {
	case AMDTP_IOC_PLUG:
		s->opcr = cmp_register_opcr(s->host->host, cfg->u.plug,
					   /*payload*/ 12, plug_update, s);
		if (s->opcr == NULL)
			return -EINVAL;
		s->iso_channel = s->opcr->channel;
		break;

	case AMDTP_IOC_CHANNEL:
		if (cfg->u.channel >= 0 && cfg->u.channel < 64)
			s->iso_channel = cfg->u.channel;
		else
			return -EINVAL;
		break;
	}

	/* The ioctl settings were all valid, so we realloc the packet
	 * lists to make sure the packet size is big enough.
	 */
	if (s->packet_pool != NULL)
		stream_free_packet_lists(s);

	if (stream_alloc_packet_lists(s) < 0) {
		stream_free_packet_lists(s);
		return -ENOMEM;
	}

	return 0;
}

struct stream *stream_alloc(struct amdtp_host *host)
{
	struct stream *s;
	unsigned long flags;
	const int transfer_delay = 8651; /* approx 352 us */

        s = kmalloc(sizeof(struct stream), SLAB_KERNEL);
        if (s == NULL)
                return NULL;

        memset(s, 0, sizeof(struct stream));
	s->host = host;

	s->input = buffer_alloc(BUFFER_SIZE);
	if (s->input == NULL) {
		kfree(s);
		return NULL;
	}

	s->cycle_offset = transfer_delay % 3072;
	atomic_set(&s->cycle_count, transfer_delay / 3072);
	atomic_set(&s->cycle_count2, 0);

	s->descriptor_pool = pci_pool_create("descriptor pool", host->ohci->dev,
					     sizeof(struct descriptor_block),
					     16, 0, SLAB_KERNEL);
	if (s->descriptor_pool == NULL) {
		kfree(s->input);
		kfree(s);
		return NULL;
	}

	INIT_LIST_HEAD(&s->free_packet_lists);
	INIT_LIST_HEAD(&s->dma_packet_lists);

        init_waitqueue_head(&s->packet_list_wait);
        spin_lock_init(&s->packet_list_lock);

	s->iso_context = ohci1394_alloc_it_ctx(host->ohci);
	if (s->iso_context < 0) {
		pci_pool_destroy(s->descriptor_pool);
		kfree(s->input);
		kfree(s);
		return NULL;
	}

	spin_lock_irqsave(&host->stream_list_lock, flags);
	list_add_tail(&s->link, &host->stream_list);
	spin_unlock_irqrestore(&host->stream_list_lock, flags);

	return s;
}

void stream_free(struct stream *s)
{
	unsigned long flags;

	/* Stop the DMA.  We wait for the dma packet list to become
	 * empty and let the dma controller run out of programs.  This
	 * seems to be more reliable than stopping it directly, since
	 * that sometimes generates an it transmit interrupt if we
	 * later re-enable the context.
	 */
	wait_event_interruptible(s->packet_list_wait, 
				 list_empty(&s->dma_packet_lists));

	ohci1394_stop_it_ctx(s->host->ohci, s->iso_context);
	ohci1394_free_it_ctx(s->host->ohci, s->iso_context);

	if (s->opcr != NULL)
		cmp_unregister_opcr(s->host->host, s->opcr);

	spin_lock_irqsave(&s->host->stream_list_lock, flags);
	list_del(&s->link);
	spin_unlock_irqrestore(&s->host->stream_list_lock, flags);

	kfree(s->input);

	stream_free_packet_lists(s);
	pci_pool_destroy(s->descriptor_pool);

	kfree(s);
}

/* File operations */

static ssize_t amdtp_write(struct file *file, const char *buffer, size_t count,
			   loff_t *offset_is_ignored)
{
	struct stream *s = file->private_data;
	unsigned char *p;
	int i, length;
	
	if (s->packet_pool == NULL)
		return -EBADFD;

	/* Fill the circular buffer from the input buffer and call the
	 * iso packer when the buffer is full.  The iso packer may
	 * leave bytes in the buffer for two reasons: either the
	 * remaining bytes wasn't enough to build a new packet, or
	 * there were no free packet lists.  In the first case we
	 * re-fill the buffer and call the iso packer again or return
	 * if we used all the data from userspace.  In the second
	 * case, the wait_event_interruptible will block until the irq
	 * handler frees a packet list.
	 */

	for (i = 0; i < count; i += length) {
		p = buffer_put_bytes(s->input, count, &length);
		copy_from_user(p, buffer + i, length);
		if (s->input->length < s->input->size)
			continue;
		
		stream_flush(s);
		
		if (s->current_packet_list == NULL &&
		    wait_event_interruptible(s->packet_list_wait, 
					     !list_empty(&s->free_packet_lists)))
			return -EINTR;
	}

	return count;
}

static int amdtp_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct stream *s = file->private_data;
	struct amdtp_ioctl cfg;
	int new;

	switch(cmd)
	{
	case AMDTP_IOC_PLUG:
	case AMDTP_IOC_CHANNEL:
		if (copy_from_user(&cfg, (struct amdtp_ioctl *) arg, sizeof cfg))
			return -EFAULT;
		else 
			return stream_configure(s, cmd, &cfg);

	case AMDTP_IOC_PING:
		HPSB_INFO("ping: offsetting timpestamps %ld ticks", arg);
		new = s->cycle_offset + arg;
		s->cycle_offset = new % 3072;
		atomic_add(new / 3072, &s->cycle_count);
		return 0;

	case AMDTP_IOC_ZAP:
		while (MOD_IN_USE)
			MOD_DEC_USE_COUNT;
		return 0;

	default:
		return -EINVAL;
	}
}

static int amdtp_open(struct inode *inode, struct file *file)
{
	struct amdtp_host *host;

	/* FIXME: We just grab the first registered host */
	spin_lock(&host_list_lock);
	if (!list_empty(&host_list))
		host = list_entry(host_list.next, struct amdtp_host, link);
	else
		host = NULL;
	spin_unlock(&host_list_lock);

	if (host == NULL)
		return -ENODEV;

	file->private_data = stream_alloc(host);
	if (file->private_data == NULL)
		return -ENOMEM;

	return 0;
}

static int amdtp_release(struct inode *inode, struct file *file)
{
	struct stream *s = file->private_data;

	stream_free(s);

	return 0;
}

static struct file_operations amdtp_fops =
{
	owner:		THIS_MODULE,
	write:		amdtp_write,
	ioctl:		amdtp_ioctl,
	open:		amdtp_open,
	release:	amdtp_release
};

/* IEEE1394 Subsystem functions */

static void amdtp_add_host(struct hpsb_host *host)
{
	struct amdtp_host *ah;

	/* FIXME: check it's an ohci host. */

	ah = kmalloc(sizeof *ah, SLAB_KERNEL);
	ah->host = host;
	ah->ohci = host->hostdata;
	INIT_LIST_HEAD(&ah->stream_list);
	spin_lock_init(&ah->stream_list_lock);

	spin_lock_irq(&host_list_lock);
	list_add_tail(&ah->link, &host_list);
	spin_unlock_irq(&host_list_lock);

	ohci1394_hook_irq(ah->ohci, amdtp_irq_handler, ah);
}

static void amdtp_remove_host(struct hpsb_host *host)
{
	struct list_head *lh;
	struct amdtp_host *ah;

	spin_lock_irq(&host_list_lock);
	list_for_each(lh, &host_list) {
		if (list_entry(lh, struct amdtp_host, link)->host == host) {
			list_del(lh);
			break;
		}
	}
	spin_unlock_irq(&host_list_lock);
	
	if (lh != &host_list) {
		ah = list_entry(lh, struct amdtp_host, link);
		ohci1394_unhook_irq(ah->ohci, amdtp_irq_handler, ah);
		kfree(ah);
	}
	else
		HPSB_ERR("remove_host: bogus ohci host: %p", host);
}

static struct hpsb_highlevel_ops amdtp_highlevel_ops = {
	add_host:	amdtp_add_host,
	remove_host:	amdtp_remove_host,
};

/* Module interface */

MODULE_AUTHOR("Kristian Hogsberg <hogsberg@users.sf.net>");
MODULE_DESCRIPTION("Driver for Audio & Music Data Transmission Protocol "
		   "on OHCI boards.");
MODULE_SUPPORTED_DEVICE("amdtp");
MODULE_LICENSE("GPL");

static int __init amdtp_init_module (void)
{
	if (ieee1394_register_chardev(IEEE1394_MINOR_BLOCK_EXPERIMENTAL,
				      THIS_MODULE, &amdtp_fops)) {
		HPSB_ERR("amdtp: unable to get minor device block");
 		return -EIO;
 	}

	amdtp_highlevel = hpsb_register_highlevel ("amdtp",
						   &amdtp_highlevel_ops);
	if (amdtp_highlevel == NULL) {
		HPSB_ERR("amdtp: unable to register highlevel ops");
		ieee1394_unregister_chardev(IEEE1394_MINOR_BLOCK_EXPERIMENTAL);
		return -EIO;
	}

	HPSB_INFO("Loaded AMDTP driver");

	return 0;
}

static void __exit amdtp_exit_module (void)
{
        hpsb_unregister_highlevel(amdtp_highlevel);
        ieee1394_unregister_chardev(IEEE1394_MINOR_BLOCK_EXPERIMENTAL);

	HPSB_INFO("Unloaded AMDTP driver");
}

module_init(amdtp_init_module);
module_exit(amdtp_exit_module);
