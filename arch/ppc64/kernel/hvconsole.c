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

/* TODO:
 * finish DTR/CD ioctls
 * use #defines instead of "16" "12" etc
 * comment lack of locking
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/time.h>
#include <linux/ctype.h>
#include <linux/interrupt.h>
#include <asm/delay.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/hvconsole.h>
#include <asm/termios.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define __ALIGNED__	__attribute__((__aligned__(8)))

#define HVCS_LONG_INVALID	0xFFFFFFFFFFFFFFFF

struct vtty_struct {
	uint32_t vtermno;
	int (*get_chars)(struct vtty_struct *vtty, char *buf, int count);
	int (*put_chars)(struct vtty_struct *vtty, const char *buf, int count);
	int (*tiocmget)(struct vtty_struct *vtty);
	int (*tiocmset)(struct vtty_struct *vtty, uint16_t set, uint16_t clear);
	uint16_t seqno; /* HVSI packet sequence number */
	uint16_t mctrl;
	int irq;
};
static struct vtty_struct vttys[MAX_NR_HVC_CONSOLES];

#define WAIT_LOOPS 10000
#define WAIT_USECS 100

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
#define VSV_SEND_MODEM_CTL_STATUS 2

/* yes, these masks are not consecutive. */
#define HVSI_TSDTR 0x1
#define HVSI_TSCD  0x20

struct hvsi_header {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
} __attribute__((packed));

struct hvsi_control {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
	uint16_t verb;
	/* optional depending on verb: */
	uint32_t word;
	uint32_t mask;
} __attribute__((packed));

struct hvsi_query {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
	uint16_t verb;
} __attribute__((packed));

struct hvsi_query_response {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
	uint16_t verb;
	uint16_t query_seqno;
	union {
		uint8_t  version;
		uint32_t mctrl_word;
	} u;
} __attribute__((packed));

/* ring buffer stuff: */
struct packet_desc {
	union {
		struct hvsi_header hdr;
		char pkt[256]; /* console_initcall is pre-mem_init(), so no kmalloc */
	} data;
	unsigned int want;
	unsigned int got;
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

static int desc_hdr_done(struct packet_desc *desc)
{
	if (desc->got < sizeof(struct hvsi_header))
		return 0;
	return 1;
}

static unsigned int desc_want(struct packet_desc *desc)
{
	if (desc_hdr_done(desc))
		return desc->data.hdr.len;
	else
		return UINT_MAX;
}

static int desc_done(struct packet_desc *desc)
{
	if (!desc_hdr_done(desc) || (desc->got < desc->want))
		return 0;
	return 1;
}

static int desc_overflow(struct packet_desc *desc)
{
	int overflow = desc->got - desc->want;
	if (desc_hdr_done(desc) && (overflow > 0))
		return overflow;
	return 0;
}

static void desc_clear(struct packet_desc *desc)
{
	desc->got = desc->want = 0;
}

/* these only work on well-formed and complete packets */

static inline int hdrlen(const struct hvsi_header *pkt)
{
	const int lengths[] = { 4, 6, 6, 8, };
	int index = VS_DATA_PACKET_HEADER - pkt->type;

	return lengths[index];
}

static inline uint8_t *payload(const struct hvsi_header *pkt)
{
	return (uint8_t *)pkt + hdrlen(pkt);
}

static inline int len_packet(const struct hvsi_header *pkt)
{
	return (int)pkt->len;
}

static inline int len_payload(const struct hvsi_header *pkt)
{
	return len_packet(pkt) - hdrlen(pkt);
}

static void dump_packet(struct hvsi_header *pkt)
{
	int i;
	char *data = payload(pkt);

	printk("type 0x%x, len %i, seqno %i:", pkt->type, pkt->len, pkt->seqno);

	if (len_payload(pkt))
		printk("\n     ");
	for (i=0; i < len_payload(pkt); i++)
		printk("%.2x", data[i]);

	if (len_payload(pkt))
		printk("\n     ");
	for (i=0; i < len_payload(pkt); i++) {
		if (isprint(data[i]))
			printk(" %c", data[i]);
		else
			printk("..");
	}
	printk("\n");
}

#ifdef DEBUG
static void dump_ring(void)
{
	int i;
	for (i=0; i < N_PACKETS; i++) {
		struct packet_desc *desc = &ring[i];
		if (read == desc)
			printk("r");
		else
			printk(" ");
		if (write == desc)
			printk("w");
		else
			printk(" ");
		printk(" ");
		printk("desc %i: want %i got %i\n", i, desc->want, desc->got);
		printk("    ");
		dump_packet(&desc->data.hdr);
	}
}
#endif /* DEBUG */

/* normal hypervisor virtual console code */
int hvterm_get_chars(uint32_t vtermno, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, vtermno, 0, 0, 0, &got,
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
EXPORT_SYMBOL(hvterm_get_chars);

/* wrapper exists just so that hvterm_get_chars() is callable by outside
 * drivers without a vtty_struct */
int hvc_hvterm_get_chars(struct vtty_struct *vtty, char *buf, int count)
{
	return hvterm_get_chars(vtty->vtermno, buf, count);
}

int hvterm_put_chars(uint32_t vtermno, const char *buf, int count)
{
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, vtermno, count, lbuf[0],
				 lbuf[1]);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -EIO;
}
EXPORT_SYMBOL(hvterm_put_chars);

/* wrapper exists just so that hvterm_put_chars() is callable by outside
 * drivers without a vtty_struct */
int hvc_hvterm_put_chars(struct vtty_struct *vtty, const char *buf, int count)
{
	return hvterm_put_chars(vtty->vtermno, buf, count);
}

/* Host Virtual Serial Interface (HVSI) code */

static int hvsi_read(struct vtty_struct *vtty, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, vtty->vtermno, 0, 0, 0, &got,
		(unsigned long *)buf, (unsigned long *)buf+1) == H_Success) {
		return got;
	}
	return 0;
}

/* like memcpy, but only copy at most a single packet from the src bytestream */
static int copy_packet(uint8_t *dest, uint8_t *src, uint8_t len)
{
	int copylen;

	if (len == 1) {
		/* we don't have the len header */
		*dest = *src;
		return 1;
	}

	/* if we have more than one packet here, only copy the first */
	copylen = min(len_packet((struct hvsi_header *)src), (int)len);
	memcpy(dest, src, copylen);
	return copylen;
}

/* load up ring buffers */
static int hvsi_load_chunk(struct vtty_struct *vtty)
{
	struct packet_desc *old = write;
	unsigned int chunklen;
	unsigned int overflow;

	/* copy up to 16 bytes into the write buffer */
	chunklen = hvsi_read(vtty, write->data.pkt + write->got, 16);
	if (!chunklen)
		return 0;
	write->got += chunklen;
	write->want = desc_want(write);

	overflow = desc_overflow(write);
	while (overflow) {
		/* copied too much into 'write'; memcpy it into the next buffers */
		int nextlen;
		write = next_desc(write);

		nextlen = copy_packet(write->data.pkt, old->data.pkt + old->want,
			overflow);
		write->got = nextlen;
		write->want = desc_want(write);
		overflow -= nextlen;
	}
	if (desc_done(write))
		write = next_desc(write);
	return 1;
}

/* keep reading from hypervisor until there's no more */
static void hvsi_load_buffers(struct vtty_struct *vtty)
{
	/* XXX perhaps we should limit this */
	while (hvsi_load_chunk(vtty)) {
		if (write == read) {
			/* we've filled all our ring buffers; let the hypervisor queue
			 * the rest for us */
			break;
		}
	}
}

static int hvsi_recv_control(struct vtty_struct *vtty, struct hvsi_control *pkt)
{
	int ret = 0;
	
	//dump_packet((struct hvsi_header *)pkt);
	
	switch (pkt->verb) {
		case VSV_MODEM_CTL_UPDATE:
			if ((pkt->word & HVSI_TSCD) == 0) {
				/* CD went away; no more connection */
				vtty->mctrl &= TIOCM_CD;
				ret = -EPIPE;
			}
			break;
		case VSV_CLOSE_PROTOCOL:
			/* XXX handle this by reopening on open/read/write() ? */
			panic("%s: service processor closed HVSI connection!\n", __FUNCTION__);
			break;
		default:
			printk(KERN_WARNING "unknown HVSI control packet: ");
			dump_packet((struct hvsi_header *)pkt);
			break;
	}
	return ret;
}

/* transfer from ring buffers to caller's buffer */
static int hvsi_deliver(struct vtty_struct *vtty, uint8_t *buf, int buflen)
{
	int written = 0;
	int ret;

	for (; (read != write) && (buflen > 0); read = next_desc(read)) {
		struct hvsi_header *pkt = &read->data.hdr;
		int size;

#ifdef DEBUG
		dump_ring();
#endif

		switch (pkt->type) {
			case VS_DATA_PACKET_HEADER:
				size = min(len_payload(pkt), buflen);
				memcpy(buf, payload(pkt), size);
				buf += size;
				buflen -= size;
				written += size;
				break;
			case VS_CONTROL_PACKET_HEADER:
				ret = hvsi_recv_control(vtty, (struct hvsi_control *)pkt);
				/* if we got an error (like CD dropped), stop now.
				 * otherwise keep dispatching packets */
				if (ret < 0) {
					desc_clear(read);
					read = next_desc(read);
					return ret;
				}
				break;
			default:
				printk(KERN_WARNING "unknown HVSI packet: ");
				dump_packet(pkt);
				break;
		}
		desc_clear(read);
	}

	return written;
}

static int hvsi_get_chars(struct vtty_struct *vtty, char *databuf, int count)
{
	hvsi_load_buffers(vtty); /* get pending data */
	return hvsi_deliver(vtty, databuf, count); /* hand it up */
}

static struct hvsi_header *search_for_packet(struct vtty_struct *vtty, int type)
{
	/* bring in queued packets */
	hvsi_load_buffers(vtty);

	/* look for the version query response packet */
	for (; read != write; read = next_desc(read)) {
		struct hvsi_header *pkt = &read->data.hdr;

		if (pkt->type == type) {
			desc_clear(read);
			read = next_desc(read);
			return pkt;
		}
		printk("%s: ignoring packet while waiting for type 0x%x:\n",
			__FUNCTION__, type);
		dump_packet(pkt);
	}

	return NULL;
}

static int wait_for_packet(struct vtty_struct *vtty, struct hvsi_header **hdr,
	int type)
{
	struct hvsi_header *found;
	int count = 0;

	do {
		if (count++ > WAIT_LOOPS)
			return -EIO;
		udelay(WAIT_USECS);
		found = search_for_packet(vtty, type);
	} while (!found);

	*hdr = found;
	return 0;
}

static int hvsi_query(struct vtty_struct *vtty, uint16_t verb)
{
	struct hvsi_query query __ALIGNED__ = {
		.type = VS_QUERY_PACKET_HEADER,
		.len = sizeof(struct hvsi_query),
	};
	int wrote;

	query.seqno = vtty->seqno++;
	query.verb = verb;
	wrote = hvc_hvterm_put_chars(vtty, (char *)&query, query.len);
	if (wrote != query.len) {
		printk(KERN_ERR "%s: couldn't send query!\n", __FUNCTION__);
		return -EIO;
	}

	return 0;
}

/* respond to service processor's version query */
static int hvsi_version_respond(struct vtty_struct *vtty, uint16_t query_seqno)
{
	struct hvsi_query_response response __ALIGNED__ = {
		.type = VS_QUERY_RESPONSE_PACKET_HEADER,
		.len = sizeof(struct hvsi_query_response),
		.verb = VSV_SEND_VERSION_NUMBER,
		.u.version = HVSI_VERSION,
	};
	int wrote;

	response.seqno = vtty->seqno++;
	response.query_seqno = query_seqno+1,
	wrote = hvc_hvterm_put_chars(vtty, (char *)&response, response.len);
	if (wrote != response.len) {
		printk(KERN_ERR "%s: couldn't send query response!\n", __FUNCTION__);
		return -EIO;
	}

	return 0;
}

static int hvsi_get_mctrl(struct vtty_struct *vtty)
{
	struct hvsi_header *hdr;
	int ret = 0;
	uint16_t mctrl;

	if (hvsi_query(vtty, VSV_SEND_MODEM_CTL_STATUS)) {
		ret = -EIO;
		goto out;
	}
	if (wait_for_packet(vtty, &hdr, VS_QUERY_RESPONSE_PACKET_HEADER)) {
		ret = -EIO;
		goto out;
	}
	/* XXX see if it's the right response */

	vtty->mctrl = 0;

	mctrl = ((struct hvsi_query_response *)hdr)->u.mctrl_word;
	if (mctrl & HVSI_TSDTR)
		vtty->mctrl |= TIOCM_DTR;
	if (mctrl & HVSI_TSCD)
		vtty->mctrl |= TIOCM_CD;
	pr_debug("%s: mctrl 0x%x\n", __FUNCTION__, vtty->mctrl);

out:
	return ret;
}

static int hvsi_handshake(struct vtty_struct *vtty)
{
	struct hvsi_header *hdr;
	int ret = 0;

	if (hvsi_query(vtty, VSV_SEND_VERSION_NUMBER)) {
		ret = -EIO;
		goto out;
	}
	if (wait_for_packet(vtty, &hdr, VS_QUERY_RESPONSE_PACKET_HEADER)) {
		ret = -EIO;
		goto out;
	}
	/* XXX see if it's the right response */

	if (wait_for_packet(vtty, &hdr, VS_QUERY_PACKET_HEADER)) {
		ret = -EIO;
		goto out;
	}
	/* XXX see if it's the right query */
	if (hvsi_version_respond(vtty, hdr->seqno)) {
		ret = -EIO;
		goto out;
	}

	if (hvsi_get_mctrl(vtty)) {
		ret = -EIO;
		goto out;
	}

out:
	if (ret < 0)
		printk(KERN_ERR "HVSI handshaking failed\n");
	return ret;
}

static int hvsi_put_chars(struct vtty_struct *vtty, const char *buf, int count)
{
	char packet[16] __ALIGNED__;
	uint64_t *lbuf = (uint64_t *)packet;
	struct hvsi_header *hdr = (struct hvsi_header *)packet;
	int ret;

	hdr->type = VS_DATA_PACKET_HEADER;
	hdr->seqno = vtty->seqno++;

	if (count > 12)
		count = 12; /* we'll leave some chars behind in buf */
	hdr->len = count + sizeof(struct hvsi_header);
	memcpy(packet + sizeof(struct hvsi_header), buf, count);

	/* note: we can't use hvc_hvterm_put_chars() here, as it would return
	 * _packet_ length, not _payload_ length */
	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, vtty->vtermno, hdr->len,
			lbuf[0], lbuf[1]);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -EIO;
}

/* note that we can only set DTR */
static int hvsi_set_mctrl(struct vtty_struct *vtty, uint16_t mctrl)
{
	struct hvsi_control command __ALIGNED__ = {
		.type = VS_CONTROL_PACKET_HEADER,
		.len = sizeof(struct hvsi_control),
		.verb = VSV_SET_MODEM_CTL,
		.mask = HVSI_TSDTR,
	};
	int wrote;

	command.seqno = vtty->seqno++;
	if (mctrl & TIOCM_DTR)
		command.word = HVSI_TSDTR;

	wrote = hvc_hvterm_put_chars(vtty, (char *)&command, command.len);
	if (wrote != command.len) {
		printk(KERN_ERR "%s: couldn't set DTR!\n", __FUNCTION__);
		return -EIO;
	}

	return 0;
}

static int hvsi_tiocmset(struct vtty_struct *vtty, uint16_t set, uint16_t clear)
{
	uint16_t old_mctrl;

	/* we can only set DTR */
	if (set & ~TIOCM_DTR)
		return -EINVAL;

	old_mctrl = vtty->mctrl;
	vtty->mctrl = (old_mctrl & ~clear) | set;

	pr_debug("%s: new mctrl 0x%x\n", __FUNCTION__, vtty->mctrl);
	if (old_mctrl != vtty->mctrl) {
		if (hvsi_set_mctrl(vtty, vtty->mctrl) < 0)
			return -EIO;
	} else {
		pr_debug("  (not writing to SP)\n");
	}

	return 0;
}

static int hvsi_tiocmget(struct vtty_struct *vtty)
{
	if (hvsi_get_mctrl(vtty))
		return -EIO;
	pr_debug("%s: mctrl 0x%x\n", __FUNCTION__, vtty->mctrl);
	return vtty->mctrl;
}

/* external (hvc_console.c) interface: */

int hvc_arch_get_chars(int index, char *buf, int count)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -ENODEV;

	return vtty->get_chars(vtty, buf, count);
}

int hvc_arch_put_chars(int index, const char *buf, int count)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -ENODEV;

	return vtty->put_chars(vtty, buf, count);
}

int hvc_arch_tiocmset(int index, unsigned int set, unsigned int clear)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -ENODEV;

	if (vtty->tiocmset)
		return vtty->tiocmset(vtty, set, clear);
	return -EINVAL;
}

int hvc_arch_tiocmget(int index)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -ENODEV;

	if (vtty->tiocmset)
		return vtty->tiocmget(vtty);
	return -EINVAL;
}

int hvc_arch_find_vterms(void)
{
	struct device_node *vty;
	int count = 0;
	unsigned int *irq_p;

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
		vtty->irq = NO_IRQ;
		if (device_is_compatible(vty, "hvterm1")) {
			vtty->vtermno = *vtermno;
			vtty->get_chars = hvc_hvterm_get_chars;
			vtty->put_chars = hvc_hvterm_put_chars;
			vtty->tiocmget = NULL;
			vtty->tiocmset = NULL;
			irq_p = (unsigned int *)get_property(vty, "interrupts", 0);
			if (irq_p) {
				int virq = virt_irq_create_mapping(*irq_p);
				if (virq != NO_IRQ)
					vtty->irq = irq_offset_up(virq);
			}
			hvc_instantiate();
			count++;
		} else if (device_is_compatible(vty, "hvterm-protocol")) {
			vtty->vtermno = *vtermno;
			vtty->seqno = 0;
			vtty->get_chars = hvsi_get_chars;
			vtty->put_chars = hvsi_put_chars;
			vtty->tiocmget = hvsi_tiocmget;
			vtty->tiocmset = hvsi_tiocmset;
			if (hvsi_handshake(vtty)) {
				continue;
			}
			vtty->put_chars(vtty, "\nHVSI\n", 6);
			hvc_instantiate();
			count++;
		}
	}

	return count;
}

int hvc_interrupt(int index)
{
	struct vtty_struct *vtty = &vttys[index];

	/* If not interruptible then it'll return NO_IRQ */
	return vtty->irq;
} 

/* Convert arch specific return codes into relevant errnos.  The hvcs
 * functions aren't performance sensitive, so this conversion isn't an
 * issue. */
int hvcs_convert(long to_convert)
{
	switch (to_convert) {
		case H_Success:
			return 0;
		case H_Parameter:
			return -EINVAL;
		case H_Hardware:
			return -EIO;
		case H_Busy:
		case H_LongBusyOrder1msec:
		case H_LongBusyOrder10msec:
		case H_LongBusyOrder100msec:
		case H_LongBusyOrder1sec:
		case H_LongBusyOrder10sec:
		case H_LongBusyOrder100sec:
			return -EBUSY;
		case H_Function: /* fall through */
		default:
			return -EPERM;
	}
}

int hvcs_free_partner_info(struct list_head *head)
{
	struct hvcs_partner_info *pi;
	struct list_head *element;

	if(!head) {
		return -EINVAL;
	}

	while (!list_empty(head)) {
		element = head->next;
		pi = list_entry(element,struct hvcs_partner_info,node);
		list_del(element);
		kfree(pi);
	}

	return 0;
}
EXPORT_SYMBOL(hvcs_free_partner_info);

/* Helper function for hvcs_get_partner_info */
int hvcs_next_partner(unsigned int unit_address, unsigned long last_p_partition_ID, unsigned long last_p_unit_address, unsigned long *pi_buff)
{
	long retval;
	retval = plpar_hcall_norets(H_VTERM_PARTNER_INFO, unit_address,
			last_p_partition_ID,
				last_p_unit_address, virt_to_phys(pi_buff));
	return hvcs_convert(retval);
}

/* The unit_address parameter is the unit address of the vty-server@ vdevice
 * in whose partner information the caller is interested.  This function
 * uses a pointer to a list_head instance in which to store the partner info.
 * This function returns Non-Zero on success, or if there is no partner info.
 *
 * Invocation of this function should always be followed by an invocation of
 * hvcs_free_partner_info() using a pointer to the SAME list head instance
 * that was used to store the partner_info list.
 */
int hvcs_get_partner_info(unsigned int unit_address, struct list_head *head)
{
	/* This is a page sized buffer to be passed to hvcall per invocation.
	 * NOTE: the first long returned is unit_address.  The second long
	 * returned is the partition ID and starting with pi_buff[2] are
	 * HVCS_CLC_LENGTH characters, which are diff size than the unsigned
	 * long, hence the casting mumbojumbo you see later. */
	unsigned long	*pi_buff;
	unsigned long	last_p_partition_ID;
	unsigned long	last_p_unit_address;
	struct hvcs_partner_info *next_partner_info = NULL;
	int more = 1;
	int retval;

	/* invalid parameters */
	if (!head)
		return -EINVAL;

	last_p_partition_ID = last_p_unit_address = HVCS_LONG_INVALID;
	INIT_LIST_HEAD(head);

	pi_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);

	if(!pi_buff)
		return -ENOMEM;

	do {
		retval = hvcs_next_partner(unit_address, last_p_partition_ID,
				last_p_unit_address, pi_buff);
		if(retval) {
			kfree(pi_buff);
			pi_buff = 0;
			/* don't indicate that we've failed if we have
			 * any list elements. */
			if(!list_empty(head))
				return 0;
			return retval;
		}

		last_p_partition_ID = pi_buff[0];
		last_p_unit_address = pi_buff[1];

		/* This indicates that there are no further partners */
		if (last_p_partition_ID == HVCS_LONG_INVALID
				&& last_p_unit_address == HVCS_LONG_INVALID)
			break;

		next_partner_info = kmalloc(sizeof(struct hvcs_partner_info),
				GFP_KERNEL);

		if (!next_partner_info) {
			printk(KERN_WARNING "HVCONSOLE: kmalloc() failed to"
				" allocate partner info struct.\n");
			hvcs_free_partner_info(head);
			kfree(pi_buff);
			pi_buff = 0;
			return -ENOMEM;
		}

		next_partner_info->unit_address
			= (unsigned int)last_p_unit_address;
		next_partner_info->partition_ID
			= (unsigned int)last_p_partition_ID;

		/* copy the Null-term char too */
		strncpy(&next_partner_info->location_code[0],
			(char *)&pi_buff[2],
			strlen((char *)&pi_buff[2]) + 1);

		list_add_tail(&(next_partner_info->node), head);
		next_partner_info = NULL;

	} while (more);

	kfree(pi_buff);
	pi_buff = 0;

	return 0;
}
EXPORT_SYMBOL(hvcs_get_partner_info);

/* If this function is called once and -EINVAL is returned it may
 * indicate that the partner info needs to be refreshed for the
 * target unit address at which point the caller must invoke
 * hvcs_get_partner_info() and then call this function again.  If,
 * for a second time, -EINVAL is returned then it indicates that
 * there is probably already a partner connection registered to a
 * different vty-server@ vdevice.  It is also possible that a second
 * -EINVAL may indicate that one of the parms is not valid, for
 * instance if the link was removed between the vty-server@ vdevice
 * and the vty@ vdevice that you are trying to open.  Don't shoot the
 * messenger.  Firmware implemented it this way.
 */
int hvcs_register_connection( unsigned int unit_address, unsigned int p_partition_ID, unsigned int p_unit_address)
{
	long retval;
	retval = plpar_hcall_norets(H_REGISTER_VTERM, unit_address,
				p_partition_ID, p_unit_address);
	return hvcs_convert(retval);
}
EXPORT_SYMBOL(hvcs_register_connection);

/* If -EBUSY is returned continue to call this function
 * until 0 is returned */
int hvcs_free_connection(unsigned int unit_address)
{
	long retval;
	retval = plpar_hcall_norets(H_FREE_VTERM, unit_address);
	return hvcs_convert(retval);
}
EXPORT_SYMBOL(hvcs_free_connection);
