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

#include <linux/slab.h>
#include "iso.h"

void hpsb_iso_stop(struct hpsb_iso *iso)
{
	if(!iso->flags & HPSB_ISO_DRIVER_STARTED)
		return;

	iso->host->driver->isoctl(iso, iso->type == HPSB_ISO_XMIT ? XMIT_STOP : RECV_STOP, 0);
	iso->flags &= ~HPSB_ISO_DRIVER_STARTED;
}

void hpsb_iso_shutdown(struct hpsb_iso *iso)
{
	if(iso->flags & HPSB_ISO_DRIVER_INIT) {
		hpsb_iso_stop(iso);
		iso->host->driver->isoctl(iso, iso->type == HPSB_ISO_XMIT ? XMIT_SHUTDOWN : RECV_SHUTDOWN, 0);
		iso->flags &= ~HPSB_ISO_DRIVER_INIT;
	}
	
	dma_region_free(&iso->buf);
	kfree(iso);
}

static struct hpsb_iso* hpsb_iso_common_init(struct hpsb_host *host, enum hpsb_iso_type type,
					     unsigned int buf_packets,
					     unsigned int max_packet_size,
					     int channel,
					     int irq_interval,
					     void (*callback)(struct hpsb_iso*))
{
	struct hpsb_iso *iso;
	unsigned int packet_plus_info;
	int dma_direction;
	int iso_header_bytes;
	const int info_bytes = sizeof(struct hpsb_iso_packet_info);
	
	/* make sure driver supports the ISO API */
	if(!host->driver->isoctl)
		return NULL;

	if(type == HPSB_ISO_RECV) {
		/* when receiving, leave 8 extra bytes in front
		   of the data payload for the iso header */
		iso_header_bytes = 8;
	} else {
		iso_header_bytes = 0;
	}
	
	/* sanitize parameters */
	
	if(buf_packets < 2)
		buf_packets = 2;
	
	if(irq_interval < 1 || irq_interval > buf_packets / 2)
		irq_interval = buf_packets / 2;
	
	if(max_packet_size + info_bytes + iso_header_bytes > PAGE_SIZE)
		return NULL;
	
	/* size of packet payload plus the per-packet info must be a power of 2
	   and at most equal to the page size */
	
	for(packet_plus_info = 256; packet_plus_info < PAGE_SIZE; packet_plus_info *= 2) {
		if(packet_plus_info >= (max_packet_size + info_bytes + iso_header_bytes)) {
			break;
		}
	}

	/* allocate and write the struct hpsb_iso */
	
	iso = kmalloc(sizeof(*iso), SLAB_KERNEL);
	if(!iso)
		return NULL;
	
	iso->type = type;
	iso->host = host;
	iso->hostdata = NULL;
	iso->callback = callback;
	iso->channel = channel;
	iso->irq_interval = irq_interval;
	dma_region_init(&iso->buf);
	iso->buf_packets = buf_packets;
	iso->buf_stride = packet_plus_info;
	iso->max_packet_size = max_packet_size;
	iso->packet_data_offset = iso_header_bytes;
	iso->packet_info_offset = iso_header_bytes + max_packet_size;
	iso->first_packet = 0;

	if(iso->type == HPSB_ISO_XMIT) {
		atomic_set(&iso->n_dma_packets, 0);
		dma_direction = PCI_DMA_TODEVICE;
	} else {
		atomic_set(&iso->n_dma_packets, iso->buf_packets);
		dma_direction = PCI_DMA_FROMDEVICE;
	}
	
	atomic_set(&iso->overflows, 0);
	iso->flags = 0;
	iso->prebuffer = 0;
	
	/* allocate the packet buffer */
	if(dma_region_alloc(&iso->buf, iso->buf_packets * iso->buf_stride,
			    host->pdev, dma_direction))
		goto err;

	return iso;

err:
	hpsb_iso_shutdown(iso);
	return NULL;
}

int hpsb_iso_n_ready(struct hpsb_iso* iso)
{
	return iso->buf_packets - atomic_read(&iso->n_dma_packets);
}
	

struct hpsb_iso* hpsb_iso_xmit_init(struct hpsb_host *host,
				    unsigned int buf_packets,
				    unsigned int max_packet_size,
				    int channel,
				    int speed,
				    int irq_interval,
				    void (*callback)(struct hpsb_iso*))
{
	struct hpsb_iso *iso = hpsb_iso_common_init(host, HPSB_ISO_XMIT,
						    buf_packets, max_packet_size,
						    channel, irq_interval, callback);
	if(!iso)
		return NULL;

	iso->speed = speed;
	
	/* tell the driver to start working */
	if(host->driver->isoctl(iso, XMIT_INIT, 0))
		goto err;

	iso->flags |= HPSB_ISO_DRIVER_INIT;
	return iso;

err:
	hpsb_iso_shutdown(iso);
	return NULL;
}

struct hpsb_iso* hpsb_iso_recv_init(struct hpsb_host *host,
				    unsigned int buf_packets,
				    unsigned int max_packet_size,
				    int channel,
				    int irq_interval,
				    void (*callback)(struct hpsb_iso*))
{
	struct hpsb_iso *iso = hpsb_iso_common_init(host, HPSB_ISO_RECV,
						    buf_packets, max_packet_size,
						    channel, irq_interval, callback);
	if(!iso)
		return NULL;

	/* tell the driver to start working */
	if(host->driver->isoctl(iso, RECV_INIT, 0))
		goto err;

	iso->flags |= HPSB_ISO_DRIVER_INIT;
	return iso;

err:
	hpsb_iso_shutdown(iso);
	return NULL;
}

static int do_iso_xmit_start(struct hpsb_iso *iso, int cycle)
{
	int retval = iso->host->driver->isoctl(iso, XMIT_START, cycle);
	if(retval)
		return retval;

	iso->flags |= HPSB_ISO_DRIVER_STARTED;
	return retval;
}

int hpsb_iso_xmit_start(struct hpsb_iso *iso, int cycle, int prebuffer)
{
	if(iso->type != HPSB_ISO_XMIT)
		return -1;
	
	if(iso->flags & HPSB_ISO_DRIVER_STARTED)
		return 0;

	if(prebuffer < 1)
		prebuffer = 1;

	if(prebuffer > iso->buf_packets)
		prebuffer = iso->buf_packets;

	iso->prebuffer = prebuffer;

	if(cycle != -1) {
		/* pre-fill info->cycle */
		int pkt = iso->first_packet;
		int c, i;

		cycle %= 8000;

		c = cycle;
		for(i = 0; i < iso->buf_packets; i++) {
			struct hpsb_iso_packet_info *info = hpsb_iso_packet_info(iso, pkt);

			info->cycle = c;
			
			c = (c+1) % 8000;
			pkt = (pkt+1) % iso->buf_packets;
		}
	}
	
	/* remember the starting cycle; DMA will commence from xmit_queue_packets() */
	iso->start_cycle = cycle;

	return 0;
}

int hpsb_iso_recv_start(struct hpsb_iso *iso, int cycle)
{
	int retval = 0;

	if(iso->type != HPSB_ISO_RECV)
		return -1;
	
	if(iso->flags & HPSB_ISO_DRIVER_STARTED)
		return 0;

	retval = iso->host->driver->isoctl(iso, RECV_START, cycle);
	if(retval)
		return retval;

	iso->flags |= HPSB_ISO_DRIVER_STARTED;
	return retval;
}

int hpsb_iso_xmit_queue_packets(struct hpsb_iso *iso, unsigned int n_packets)
{
	int i, retval;
	int pkt = iso->first_packet;

	if(iso->type != HPSB_ISO_XMIT)
		return -1;
	
	/* check packet sizes for sanity */
	for(i = 0; i < n_packets; i++) {
		struct hpsb_iso_packet_info *info = hpsb_iso_packet_info(iso, pkt);
		if(info->len > iso->max_packet_size) {
			printk(KERN_ERR "hpsb_iso_xmit_queue_packets: packet too long (%u, max is %u)\n",
			       info->len, iso->max_packet_size);
			return -EINVAL;
		}
							     
		pkt = (pkt+1) % iso->buf_packets;
	}

	retval = iso->host->driver->isoctl(iso, XMIT_QUEUE, n_packets);
	if(retval)
		return retval;

	if(iso->prebuffer != 0) {
		iso->prebuffer -= n_packets;
		if(iso->prebuffer <= 0) {
			iso->prebuffer = 0;
			return do_iso_xmit_start(iso,
						 iso->start_cycle);
		}
	}

	return 0;
}

int hpsb_iso_recv_release_packets(struct hpsb_iso *iso, unsigned int n_packets)
{
	if(iso->type != HPSB_ISO_RECV)
		return -1;
	
	return iso->host->driver->isoctl(iso, RECV_RELEASE, n_packets);
}

unsigned char* hpsb_iso_packet_data(struct hpsb_iso *iso, unsigned int pkt)
{
	return (iso->buf.kvirt + pkt * iso->buf_stride)
		+ iso->packet_data_offset;
}

struct hpsb_iso_packet_info* hpsb_iso_packet_info(struct hpsb_iso *iso, unsigned int pkt)
{
	return (struct hpsb_iso_packet_info*) ((iso->buf.kvirt + pkt * iso->buf_stride)
					       + iso->packet_info_offset);
}
