/******************************************************************************
 * cdromif.h
 *
 * Shared definitions between backend driver and Xen guest Virtual CDROM
 * block device.
 *
 * Copyright (c) 2008, Pat Campell  plc@novell.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __XEN_PUBLIC_IO_CDROMIF_H__
#define __XEN_PUBLIC_IO_CDROMIF_H__

/*
 * Queries backend for CDROM support
 */
#define XEN_TYPE_CDROM_SUPPORT         _IO('c', 1)

struct xen_cdrom_support
{
	uint32_t type;
	int8_t ret;                  /* returned, 0 succeded, -1 error */
	int8_t err;                  /* returned, backend errno */
	int8_t supported;            /* returned, 1 supported */
};

/*
 * Opens backend device, returns drive geometry or
 * any encountered errors
 */
#define XEN_TYPE_CDROM_OPEN            _IO('c', 2)

struct xen_cdrom_open
{
	uint32_t type;
	int8_t ret;
	int8_t err;
	int8_t pad;
	int8_t media_present;        /* returned */
	uint32_t sectors;            /* returned */
	uint32_t sector_size;        /* returned */
	int32_t payload_offset;      /* offset to backend node name payload */
};

/*
 * Queries backend for media changed status
 */
#define XEN_TYPE_CDROM_MEDIA_CHANGED   _IO('c', 3)

struct xen_cdrom_media_changed
{
	uint32_t type;
	int8_t ret;
	int8_t err;
	int8_t media_changed;        /* returned */
};

/*
 * Sends vcd generic CDROM packet to backend, followed
 * immediately by the vcd_generic_command payload
 */
#define XEN_TYPE_CDROM_PACKET          _IO('c', 4)

struct xen_cdrom_packet
{
	uint32_t type;
	int8_t ret;
	int8_t err;
	int8_t pad[2];
	int32_t payload_offset;      /* offset to vcd_generic_command payload */
};

/* CDROM_PACKET_COMMAND, payload for XEN_TYPE_CDROM_PACKET */
struct vcd_generic_command
{
	uint8_t  cmd[CDROM_PACKET_SIZE];
	uint8_t  pad[4];
	uint32_t buffer_offset;
	uint32_t buflen;
	int32_t  stat;
	uint32_t sense_offset;
	uint8_t  data_direction;
	uint8_t  pad1[3];
	int32_t  quiet;
	int32_t  timeout;
};

union xen_block_packet
{
	uint32_t type;
	struct xen_cdrom_support xcs;
	struct xen_cdrom_open xco;
	struct xen_cdrom_media_changed xcmc;
	struct xen_cdrom_packet xcp;
};

#define PACKET_PAYLOAD_OFFSET (sizeof(struct xen_cdrom_packet))
#define PACKET_SENSE_OFFSET (PACKET_PAYLOAD_OFFSET + sizeof(struct vcd_generic_command))
#define PACKET_BUFFER_OFFSET (PACKET_SENSE_OFFSET + sizeof(struct request_sense))
#define MAX_PACKET_DATA (PAGE_SIZE - sizeof(struct xen_cdrom_packet) - \
            sizeof(struct vcd_generic_command) - sizeof(struct request_sense))

#endif
