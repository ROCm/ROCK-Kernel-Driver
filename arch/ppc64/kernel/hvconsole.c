/*
 * hvconsole.c
 * Copyright (C) 2004 Hollis Blanchard, IBM Corporation
 *
 * LPAR console support.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/hvconsole.h>
#include <asm/termios.h>
#include <asm/uaccess.h>

#define __ALIGNED__	__attribute__((__aligned__(8)))

struct vtty_struct {
	uint32_t vtermno;
	int (*get_chars)(struct vtty_struct *vtty, char *buf, int count);
	int (*put_chars)(struct vtty_struct *vtty, const char *buf, int count);
	int (*ioctl)(struct vtty_struct *vtty, unsigned int cmd, unsigned long val);
	uint16_t seqno; /* HVSI packet sequence number */
};
static struct vtty_struct vttys[MAX_NR_HVC_CONSOLES];

#define HVSI_VERSION 1

#define VS_DATA_PACKET_HEADER           0xff
#define VS_CONTROL_PACKET_HEADER        0xfe
#define VS_QUERY_PACKET_HEADER          0xfd
#define VS_QUERY_RESPONSE_PACKET_HEADER 0xfc

/* control verbs */
#define VSV_SET_MODEM_CTL    1 /* to service processor only */
#define VSV_MODEM_CTL_UPDATE 2 /* from service processor only */
#define VSV_CLOSE_PROTOCOL   3

/* query verbs */
#define VSV_SEND_VERSION_NUMBER 1

/* yes, these masks are not consecutive. */
#define TSDTR 0x1
#define TSCD 0x20

struct hvsi1_header {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
};

struct hvsi1_control {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
	uint8_t  version;
	uint8_t  verb;
	/* optional depending on verb: */
	uint32_t word;
	uint32_t mask;
};

struct hvsi1_query {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
	uint8_t  version;
	uint8_t  verb;
};

struct hvsi1_query_resp {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
	uint8_t  version;
	uint8_t  verb;
	uint16_t query_seqno;
	/* optional, depending on query type */
	union {
		uint8_t version;
	} response;
};

/* ring buffer stuff: */
struct packet_desc {
	struct hvsi1_header *pkt;
	int remaining;
};
#define N_PACKETS 4
static struct packet_desc ring[N_PACKETS];
static struct packet_desc *write=ring; /* next packet to write to */
static struct packet_desc *read=ring;  /* next packet to read from */

static struct packet_desc *next_desc(struct packet_desc *cur)
{
	if ((cur+1) > ring + (N_PACKETS-1))
		return ring;
	return (cur+1);
}

static inline int hdrlen(const struct hvsi1_header *pkt)
{
	const int lengths[] = { 4, 6, 6, 8, };
	int index = VS_DATA_PACKET_HEADER - pkt->type;

	if (index > sizeof(lengths)/sizeof(lengths[0]))
		panic("%s: unknown packet type\n", __FUNCTION__);

	return lengths[index];
}

static inline uint8_t *pktdata(const struct hvsi1_header *pkt)
{
	return (uint8_t *)pkt + hdrlen(pkt);
}

static inline int pktlen(const struct hvsi1_header *pkt)
{
	return (int)pkt->len;
}

static inline int datalen(const struct hvsi1_header *pkt)
{
	return pktlen(pkt) - hdrlen(pkt);
}

static void print_hdr(struct hvsi1_header *pkt)
{
	printk("type 0x%x, len %i, seqno %i\n", pkt->type, pkt->len, pkt->seqno);
}

/* normal hypervisor virtual console code */
int hvterm_get_chars(struct vtty_struct *vtty, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, vtty->vtermno, 0, 0, 0, &got,
		(unsigned long *)buf, (unsigned long *)buf+1) == H_Success) {
		/*
		 * Work around a HV bug where it gives us a null
		 * after every \r.  -- paulus
		 */
		if (got > 0) {
			int i;
			for (i = 1; i < got; ++i) {
				if (buf[i] == 0 && buf[i-1] == '\r') {
					--got;
					if (i < got)
						memmove(&buf[i], &buf[i+1],
							got - i);
				}
			}
		}
		return got;
	}
	return 0;
}

int hvterm_put_chars(struct vtty_struct *vtty, const char *buf, int count)
{
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, vtty->vtermno, count, lbuf[0],
				 lbuf[1]);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -1;
}

/* Host Virtual Serial Interface (HVSI) code */

static int hvsi1_read(struct vtty_struct *vtty, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, vtty->vtermno, 0, 0, 0, &got,
		(unsigned long *)buf, (unsigned long *)buf+1) == H_Success) {
		return got;
	}
	return 0;
}

/* load up ring buffers */
static int hvsi1_load_chunk(struct vtty_struct *vtty)
{
	uint8_t localbuf[16];
	uint8_t *chunk = localbuf;
	struct hvsi1_header *pkt;
	int chunklen;

	chunklen = hvsi1_read(vtty, chunk, 16);
	if (!chunklen)
		return 0;

	printk("new chunk, len %i\n", chunklen);

	/* fill in unfinished packet */
	if (write->remaining) {
		int size = min(write->remaining, chunklen);

		printk("completing partial packet with %i bytes\n", size);
		memcpy((uint8_t *)write->pkt + pktlen(write->pkt) - write->remaining, chunk, size);
		write->remaining -= size;
		if (write->remaining == 0)
			write = next_desc(write);
		chunklen -= size;
		chunk += size;
	}

	/* new packet(s) */
	while (chunklen > 0) {
		int size;
		pkt = (struct hvsi1_header *)chunk;

		size = min(pktlen(pkt), chunklen);
		printk("%i bytes of new packet\n", size);

		/* XXX handle 1-byte read */
		if (size == 1) {
			panic("%s: can't handle 1-byte reads!\n", __FUNCTION__);
		}

		memcpy(write->pkt, chunk, size);
		write->remaining = pktlen(pkt) - size;

		if (write->remaining == 0)
			write = next_desc(write);
		chunklen -= size;
		chunk += size;
	}
	return 1;
}

static int hvsi1_load_buffers(struct vtty_struct *vtty)
{
	while (hvsi1_load_chunk(vtty))
		; /* keep reading from hypervisor until there's no more */
}

static int hvsi1_recv_control(struct vtty_struct *vtty,
	struct hvsi1_control *pkt)
{
	switch (pkt->verb) {
		case VSV_MODEM_CTL_UPDATE:
			if ((pkt->word & TSCD) == 0) {
				/* CD went away; no more connection */
				// XXX tty_hangup(hvc->tty);
				return -EPIPE;
			}
			break;
	}
	return 0;
}

/* transfer from ring buffers to caller's buffer */
static int hvsi1_deliver(struct vtty_struct *vtty, uint8_t *buf, int buflen)
{
	int written = 0;

	for (; (read != write) && buflen; read = next_desc(read)) {
		struct hvsi1_header *pkt = read->pkt;
		int size;

		switch (pkt->type) {
			case VS_DATA_PACKET_HEADER:
				size = min(datalen(pkt), buflen);
				printk("delivering %i-sized data packet\n", size);
				memcpy(buf, pktdata(pkt), size);
				buf += size;
				buflen -= size;
				written += size;
				break;
			case VS_CONTROL_PACKET_HEADER:
				hvsi1_recv_control(vtty, (struct hvsi1_control *)pkt);
				break;
			default:
				printk("%s: ignoring HVSI packet ", __FUNCTION__);
				print_hdr(pkt);
				break;
		}
	}

	return written;
}

static int hvsi1_get_chars(struct vtty_struct *vtty, char *databuf, int count)
{
	hvsi1_load_buffers(vtty); /* get pending data */
	return hvsi1_deliver(vtty, databuf, count); /* hand it up */
}

/* Handshaking step 3:
 * 
 * We're waiting for the service processor to query our version, at which point
 * we immediately respond and then we have an open HVSI connection. */
static int hvsi1_handshake3(struct vtty_struct *vtty, char *databuf, int count)
{
	struct hvsi1_query_resp response __ALIGNED__ = {
		.type = VS_QUERY_RESPONSE_PACKET_HEADER,
		.len = 9,
		.version = HVSI_VERSION,
		.verb = VSV_SEND_VERSION_NUMBER,
		.response.version = HVSI_VERSION,
	};
	int done;

	/* bring in queued packets */
	hvsi1_load_buffers(vtty);

	/* look for the version query packet */
	for (done = 0; (!done) && (read != write); read = next_desc(read)) {
		struct hvsi1_header *pkt = read->pkt;
		struct hvsi1_query *query;
		int wrote;

		switch (pkt->type) {
			case VS_QUERY_PACKET_HEADER:
				query = (struct hvsi1_query *)pkt;

				/* send query response */
				response.seqno = ++vtty->seqno;
				response.query_seqno = query->seqno+1,
				wrote = hvterm_put_chars(vtty, (char *)&response, response.len);
				if (wrote != response.len) {
					/* uh oh, command didn't go through? */
					printk(KERN_ERR "%s: couldn't send query response!\n",
						__FUNCTION__);
					return -EIO;
				}

				/* we're open for business */
				vtty->get_chars = hvsi1_get_chars;
				done = 1;
				break;
			default:
				printk("%s: ignoring HVSI packet ", __FUNCTION__);
				print_hdr(pkt);
				break;
		}
	}

	return 0; /* nothing written to databuf */
}

/* Handshaking step 2:
 *
 * We've sent a version query; now we're waiting for the service processor to
 * respond. Since we haven't established a connection yet, we won't be writing
 * anything into databuf. */
static int hvsi1_handshake2(struct vtty_struct *vtty, char *databuf, int count)
{
	int done;

	/* bring in queued packets */
	hvsi1_load_buffers(vtty);

	/* look for the version query response packet */
	for (done = 0; (!done) && (read != write); read = next_desc(read)) {
		struct hvsi1_header *pkt = read->pkt;

		switch (pkt->type) {
			case VS_QUERY_RESPONSE_PACKET_HEADER:
				/* XXX check response */
				vtty->get_chars = hvsi1_handshake3;
				done = 1;
				break;
			default:
				printk("%s: ignoring HVSI packet ", __FUNCTION__);
				print_hdr(pkt);
				break;
		}
	}

	return 0; /* nothing written to databuf */
}

/* Handshaking step 1:
 *
 * Send a version query to SP. */
static int hvsi1_handshake1(uint32_t vtermno)
{
	struct hvsi1_query packet __ALIGNED__ = {
		.type = VS_QUERY_PACKET_HEADER,
		.len = sizeof(struct hvsi1_query),
		.seqno = 0,
		.version = HVSI_VERSION,
		.verb = VSV_SEND_VERSION_NUMBER,
	};
	uint64_t *lbuf = (uint64_t *)&packet;
	uint64_t dummy;

	plpar_hcall(H_PUT_TERM_CHAR, vtermno, sizeof(struct hvsi1_query),
	    lbuf[0], lbuf[1], &dummy, &dummy, &dummy);

	return 0;
}

static int hvsi1_put_chars(struct vtty_struct *vtty, const char *buf, int count)
{
	char packet[16] __ALIGNED__;
	uint64_t dummy;
	uint64_t *lbuf = (uint64_t *)packet;
	struct hvsi1_header *hdr = (struct hvsi1_header *)packet;
	long ret;

	hdr->type = VS_DATA_PACKET_HEADER;
	hdr->seqno = ++vtty->seqno;

	if (count > 12)
		count = 12; /* we'll leave some chars behind in buf */
	hdr->len = count + sizeof(struct hvsi1_header);
	memcpy(packet + sizeof(struct hvsi1_header), buf, count);

	ret = plpar_hcall(H_PUT_TERM_CHAR, vtty->vtermno, count, lbuf[0], lbuf[1],
			  &dummy, &dummy, &dummy);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -1;
}

#if 0
static int hvsi1_send_dtr(struct vtty_struct *vtty, int set)
{
	struct hvsi1_control command __ALIGNED__ = {
		.type = VS_CONTROL_PACKET_HEADER,
		.len = 16,
		.version = HVSI_VERSION,
		.verb = VSV_SET_MODEM_CTL,
		.mask = TSDTR,
	};
	int wrote;

	command.seqno = ++vtty->seqno;

	if (set)
		command.word = TSDTR;
	else
		command.word = 0;

	wrote = hvterm_put_chars(vtty, (char *)&command, command.len);
	if (wrote != command.len) {
		/* uh oh, command didn't go through? */
		printk(KERN_ERR "%s: couldn't set DTR!\n", __FUNCTION__);
		return -1;
	}

	return 0;
}

/* XXX 2.6 tty layer turned TIO* into separate tty_struct functions, not
 * passed to tty_struct->ioctl() */
static int hvsi1_ioctl(struct vtty_struct *vtty, unsigned int cmd,
	unsigned long arg)
{
	unsigned long *ptr = (unsigned long *)arg;
	unsigned long val;
	int newdtr = -1;

	switch (cmd) {
		case TIOCMBIS:
			if (get_user(val, ptr))
				return -EFAULT;
			if (val & TIOCM_DTR) {
				newdtr = 1;
			}
			break;
		case TIOCMBIC:
			if (get_user(val, ptr))
				return -EFAULT;
			if (val & TIOCM_DTR) {
				newdtr = 0;
			}
			break;
		case TIOCMSET:
			if (get_user(val, ptr))
				return -EFAULT;
			newdtr = val & TIOCM_DTR;
			break;
		default:
			return -ENOIOCTLCMD;
	}

	if (newdtr != -1) {
		if (0 > hvsi1_send_dtr(vtty, newdtr))
			return -EIO;
	}

	return 0;
}
#endif

/* external (hvc_console.c) interface: */

int hvc_get_chars(int index, char *buf, int count)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -1;

	return vtty->get_chars(vtty, buf, count);
}

int hvc_put_chars(int index, const char *buf, int count)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -1;

	return vtty->put_chars(vtty, buf, count);
}

int hvc_find_vterms(void)
{
	struct device_node *vty;
	int count = 0;

	for (vty = of_find_node_by_name(NULL, "vty"); vty != NULL;
			vty = of_find_node_by_name(vty, "vty")) {
		struct vtty_struct *vtty;
		uint32_t *vtermno;

		vtermno = (uint32_t *)get_property(vty, "reg", NULL);
		if (!vtermno)
			continue;

		if (count >= MAX_NR_HVC_CONSOLES)
			break;

		vtty = &vttys[count];
		if (device_is_compatible(vty, "hvterm1")) {
			vtty->vtermno = *vtermno;
			vtty->get_chars = hvterm_get_chars;
			vtty->put_chars = hvterm_put_chars;
			vtty->ioctl = NULL;
			hvc_instantiate();
			count++;
		} else if (device_is_compatible(vty, "hvterm-protocol")) {
			vtty->vtermno = *vtermno;
			vtty->seqno = 0;
			vtty->get_chars = hvsi1_handshake2;
			vtty->put_chars = hvsi1_put_chars;
			//vtty->ioctl = hvsi1_ioctl;
			hvsi1_handshake1(vtty->vtermno);
			hvc_instantiate();
			count++;
		}
	}

	return count;
}
