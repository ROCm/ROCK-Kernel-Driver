/*
 * Device driver for the IIsi-style ADB on some Mac LC and II-class machines
 *
 * Based on via-cuda.c and via-macii.c, as well as the original
 * adb-bus.c, which in turn is somewhat influenced by (but uses no
 * code from) the NetBSD HWDIRECT ADB code.  Original IIsi driver work
 * was done by Robert Thompson and integrated into the old style
 * driver by Michael Schmitz.
 *
 * Original sources (c) Alan Cox, Paul Mackerras, and others.
 *
 * Rewritten for Unified ADB by David Huggins-Daines <dhd@debian.org> */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/delay.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>
#include <asm/mac_via.h>

static volatile unsigned char *via;

/* VIA registers - spaced 0x200 bytes apart - only the ones we actually use */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */

/* Bits in B data register: all active low */
#define TREQ		0x08		/* Transfer request (input) */
#define TACK		0x10		/* Transfer acknowledge (output) */
#define TIP		0x20		/* Transfer in progress (output) */
#define ST_MASK		0x30		/* mask for selecting ADB state bits */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define SR_DATA		0x08		/* Shift register data */
#define SR_CLOCK	0x10		/* Shift register clock */

#define ADB_DELAY 150

static struct adb_request* current_req = NULL;
static struct adb_request* last_req = NULL;
static unsigned char maciisi_rbuf[16];
static unsigned char *reply_ptr = NULL;
static int data_index;
static int reading_reply;
static int reply_len;

static enum maciisi_state {
    idle,
    sending,
    reading,
} maciisi_state;

static int maciisi_probe(void);
static int maciisi_init(void);
static int maciisi_send_request(struct adb_request* req, int sync);
static int maciisi_write(struct adb_request* req);
static void maciisi_interrupt(int irq, void* arg, struct pt_regs* regs);
static void maciisi_input(unsigned char *buf, int nb, struct pt_regs *regs);
static int maciisi_init_via(void);
static void maciisi_poll(void);
static void maciisi_start(void);

struct adb_driver via_maciisi_driver = {
	"Mac IIsi",
	maciisi_probe,
	maciisi_init,
	maciisi_send_request,
	NULL, /* maciisi_adb_autopoll, */
	maciisi_poll,
	NULL /* maciisi_reset_adb_bus */
};

static int
maciisi_probe(void)
{
	if (macintosh_config->adb_type != MAC_ADB_IISI)
		return -ENODEV;

	via = via1;
	return 0;
}

static int
maciisi_init(void)
{
	int err;

	if (via == NULL)
		return -ENODEV;

	if ((err = maciisi_init_via())) {
		printk(KERN_ERR "maciisi_init: maciisi_init_via() failed, code %d\n", err);
		via = NULL;
		return err;
	}

	if (request_irq(IRQ_MAC_ADB, maciisi_interrupt, IRQ_FLG_LOCK, 
			"ADB", maciisi_interrupt)) {
		printk(KERN_ERR "maciisi_init: can't get irq %d\n", IRQ_MAC_ADB);
		return -EAGAIN;
	}

	printk("adb: Mac IIsi driver v0.1 for Unified ADB.\n");
	return 0;
}

static void
maciisi_stfu(void)
{
	int status = via[B] & (TIP|TREQ);

	if (status & TREQ) {
		printk (KERN_DEBUG "maciisi_stfu called with TREQ high!\n");
		return;
	}

	/* start transfer */
	via[B] |= TIP;
	while (!(status & TREQ)) {
		int poll_timeout = ADB_DELAY * 5;
		/* Poll for SR interrupt */
		while (!(via[IFR] & SR_INT) && poll_timeout-- > 0)
			status = via[B] & (TIP|TREQ);
		via[SR]; /* Clear shift register */
		printk(KERN_DEBUG "maciisi_stfu: status %x timeout %d\n",
		       status, poll_timeout);
		
		/* ACK on-off */
		via[B] |= TACK;
		udelay(ADB_DELAY);
		via[B] &= ~TACK;
	}
	/* end frame */
	via[B] &= ~TIP;
}

/* All specifically VIA-related initialization goes here */
static int
maciisi_init_via(void)
{
	/* Set the lines up. We want TREQ as input TACK|TIP as output */
	via[DIRB] = (via[DIRB] | TACK | TIP) & ~TREQ;
	/* Shift register on input */
	via[ACR]  = (via[ACR] & ~SR_CTRL) | SR_EXT;
	printk(KERN_DEBUG "maciisi_init_via: initial status %x\n", via[B] & (TIP|TREQ));
	/* Set initial state: idle */
	via[B] &= ~(TACK|TIP);
	/* Wipe any pending data and int */
	via[SR];
	if (!(via[B] & TREQ))
		maciisi_stfu();
	via[IER] = IER_SET | SR_INT;
	maciisi_state = idle;
	return 0;
}

/* Send a request, possibly waiting for a reply */
static int
maciisi_send_request(struct adb_request* req, int sync)
{
	int i;
	static int dump_packet = 1;

	if (via == NULL) {
		req->complete = 1;
		return -ENXIO;
	}

	if (dump_packet) {
		printk(KERN_DEBUG "maciisi_send_request:");
		for (i = 0; i < req->nbytes; i++) {
			printk(" %.2x", req->data[i]);
		}
		printk("\n");
	}
	req->reply_expected = 1;
	
	i = maciisi_write(req);
	if (i)
		return i;
	
	if (sync) {
		while (!req->complete) {
			maciisi_poll();
		}
	}
	return 0;
}

/* Enqueue a request, and run the queue if possible */
static int
maciisi_write(struct adb_request* req)
{
	unsigned long flags;

  	printk(KERN_DEBUG "maciisi_write called, state=%d ifr=%x\n", maciisi_state, via[IFR]);
	/* We will accept CUDA packets - the VIA sends them to us, so
           it figures that we should be able to send them to it */
	if (req->nbytes < 2 || req->data[0] > CUDA_PACKET) {
		printk(KERN_ERR "maciisi_write: packet too small or not an ADB or CUDA packet\n");
		req->complete = 1;
		return -EINVAL;
	}
	req->next = 0;
	req->sent = 0;
	req->complete = 0;
	req->reply_len = 0;
	save_flags(flags); cli();

	if (current_req) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
	}
	if (maciisi_state == idle)
		maciisi_start();
	else
		printk(KERN_DEBUG "maciisi_write: would start, but state is %d\n", maciisi_state);

	restore_flags(flags);
	return 0;
}

static void
maciisi_start(void)
{
	struct adb_request* req;
	int status;

	printk(KERN_DEBUG "maciisi_start called, state=%d, ifr=%x\n", maciisi_state, via[IFR]);
	if (maciisi_state != idle) {
		/* shouldn't happen */
		printk(KERN_ERR "maciisi_start: maciisi_start called when driver busy!\n");
		return;
	}

	req = current_req;
	if (req == NULL)
		return;

	status = via[B] & (TIP|TREQ);
	if (!(status & TREQ)) {
		/* Bus is busy, set up for reading */
		printk(KERN_DEBUG "maciisi_start: bus busy - aborting\n");
		return;
	}

	/* Okay, send */
	printk(KERN_DEBUG "maciisi_start: sending\n");
	/* Set state to active */
	via[B] |= TIP;
	/* ACK off */
	via[B] &= ~TACK;
	/* Shift out and send */
	via[ACR] |= SR_OUT;
	via[SR] = req->data[0];
	data_index = 1;
	/* ACK on */
	via[B] |= TACK;
	maciisi_state = sending;
}

void
maciisi_poll(void)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (via[IFR] & SR_INT) {
		maciisi_interrupt(0, 0, 0);
	}
	restore_flags(flags);
}

/* Shift register interrupt - this is *supposed* to mean that the
   register is either full or empty. In practice, I have no idea what
   it means :( */
static void
maciisi_interrupt(int irq, void* arg, struct pt_regs* regs)
{
	int status;
	struct adb_request *req;
	static int dump_reply = 1;

	if (!(via[IFR] & SR_INT)) {
		/* Shouldn't happen, we hope */
		printk(KERN_DEBUG "maciisi_interrupt: called without interrupt flag set\n");
		return;
	}

	status = via[B] & (TIP|TREQ);
	printk(KERN_DEBUG "state %d status %x ifr %x\n", maciisi_state, status, via[IFR]);

 switch_start:
	switch (maciisi_state) {
	case idle:
		printk(KERN_DEBUG "maciisi_interrupt: state=idle, status %x\n", status);
		if (status & TIP)
			printk(KERN_DEBUG "maciisi_interrupt: state is idle but TIP asserted!\n");

		udelay(ADB_DELAY);
		/* Shift in */
		via[ACR] &= ~SR_OUT;
 		/* Signal start of frame */
		via[B] |= TIP;
		/* Clear the interrupt (throw this value on the floor, it's useless) */
		via[SR];
		/* ACK adb chip, high-low */
		via[B] |= TACK;
		udelay(ADB_DELAY);
		via[B] &= ~TACK;
		reply_len = 0;
		maciisi_state = reading;
		if (reading_reply) {
			reply_ptr = current_req->reply;
		} else {
			printk(KERN_DEBUG "maciisi_interrupt: received unsolicited packet\n");
			reply_ptr = maciisi_rbuf;
		}
		break;

	case sending:
		printk(KERN_DEBUG "maciisi_interrupt: state=sending, status=%x\n", status);
		/* Clear interrupt */
		via[SR];
		/* Set ACK off */
		via[B] &= ~TACK;
		req = current_req;

		if (!(status & TREQ)) {
			/* collision */
			printk(KERN_DEBUG "maciisi_interrupt: send collision\n");
			/* Set idle and input */
			via[B] &= ~TIP;
			via[ACR] &= ~SR_OUT;
			/* Must re-send */
			reading_reply = 0;
			reply_len = 0;
			maciisi_state = idle;
			/* process this now, because the IFR has been cleared */
			goto switch_start;
		}

		if (data_index >= req->nbytes) {
			/* Sent the whole packet, put the bus back in idle state */
			/* Shift in, we are about to read a reply (hopefully) */
			via[ACR] &= ~SR_OUT;
			/* End of frame */
			via[B] &= ~TIP;
			req->sent = 1;
			maciisi_state = idle;
			if (req->reply_expected) {
				/* Note: only set this once we've
                                   successfully sent the packet */
				reading_reply = 1;
			} else {
				current_req = req->next;
				if (req->done)
					(*req->done)(req);
			}
		} else {
			/* Sending more stuff */
			/* Shift out */
			via[ACR] |= SR_OUT;
			/* Delay */
			udelay(ADB_DELAY);
			/* Write */
			via[SR] = req->data[data_index++];
			/* Signal 'byte ready' */
			via[B] |= TACK;
		}
		break;

	case reading:
		printk(KERN_DEBUG "maciisi_interrupt: state=reading, status=%x\n", status);
		/* Shift in */
		via[ACR] &= ~SR_OUT;
		if (reply_len++ > 16) {
			printk(KERN_ERR "maciisi_interrupt: reply too long, aborting read\n");
			via[B] |= TACK;
			udelay(ADB_DELAY);
			via[B] &= ~(TACK|TIP);
			maciisi_state = idle;
			maciisi_start();
			break;
		}
		*reply_ptr++ = via[SR];
		status = via[B] & (TIP|TREQ);
		/* ACK on/off */
		via[B] |= TACK;
		udelay(ADB_DELAY);
		via[B] &= ~TACK;	
		if (!(status & TREQ))
			break; /* more stuff to deal with */
		
		/* end of frame */
		via[B] &= ~TIP;

		/* end of packet, deal with it */
		if (reading_reply) {
			req = current_req;
			req->reply_len = reply_ptr - req->reply;
			if (req->data[0] == ADB_PACKET) {
				/* Have to adjust the reply from ADB commands */
				if (req->reply_len <= 2 || (req->reply[1] & 2) != 0) {
					/* the 0x2 bit indicates no response */
					req->reply_len = 0;
				} else {
					/* leave just the command and result bytes in the reply */
					req->reply_len -= 2;
					memmove(req->reply, req->reply + 2, req->reply_len);
				}
			}
			if (dump_reply) {
				int i;
				printk(KERN_DEBUG "maciisi_interrupt: reply is ");
				for (i = 0; i < req->reply_len; ++i)
					printk(" %.2x", req->reply[i]);
				printk("\n");
			}
			req->complete = 1;
			current_req = req->next;
			if (req->done)
				(*req->done)(req);
			/* Obviously, we got it */
			reading_reply = 0;
		} else {
			maciisi_input(maciisi_rbuf, reply_ptr - maciisi_rbuf, regs);
		}
		maciisi_state = idle;
		status = via[B] & (TIP|TREQ);
		if (!(status & TREQ)) {
			/* Timeout?! */
			printk(KERN_DEBUG "extra data after packet: status %x ifr %x\n",
			       status, via[IFR]);
			maciisi_stfu();
		}
		/* Do any queued requests now if possible */
		maciisi_start();
		break;

	default:
		printk("maciisi_interrupt: unknown maciisi_state %d?\n", maciisi_state);
	}
}

static void
maciisi_input(unsigned char *buf, int nb, struct pt_regs *regs)
{
    int i;

    switch (buf[0]) {
    case ADB_PACKET:
	    adb_input(buf+2, nb-2, regs, buf[1] & 0x40);
	    break;
    default:
	    printk(KERN_DEBUG "data from IIsi ADB (%d bytes):", nb);
	    for (i = 0; i < nb; ++i)
		    printk(" %.2x", buf[i]);
	    printk("\n");

    }
}
