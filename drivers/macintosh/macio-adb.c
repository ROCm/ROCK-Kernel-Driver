/*
 * Driver for the ADB controller in the Mac I/O (Hydra) chip.
 */
#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/prom.h>
#include <linux/adb.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/hydra.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <linux/init.h>

struct preg {
	unsigned char r;
	char pad[15];
};

struct adb_regs {
	struct preg intr;
	struct preg data[9];
	struct preg intr_enb;
	struct preg dcount;
	struct preg error;
	struct preg ctrl;
	struct preg autopoll;
	struct preg active_hi;
	struct preg active_lo;
	struct preg test;
};

/* Bits in intr and intr_enb registers */
#define DFB	1		/* data from bus */
#define TAG	2		/* transfer access grant */

/* Bits in dcount register */
#define HMB	0x0f		/* how many bytes */
#define APD	0x10		/* auto-poll data */

/* Bits in error register */
#define NRE	1		/* no response error */
#define DLE	2		/* data lost error */

/* Bits in ctrl register */
#define TAR	1		/* transfer access request */
#define DTB	2		/* data to bus */
#define CRE	4		/* command response expected */
#define ADB_RST	8		/* ADB reset */

/* Bits in autopoll register */
#define APE	1		/* autopoll enable */

static volatile struct adb_regs *adb;
static struct adb_request *current_req, *last_req;
static unsigned char adb_rbuf[16];

static int macio_probe(void);
static int macio_init(void);
static void macio_adb_interrupt(int irq, void *arg, struct pt_regs *regs);
static int macio_send_request(struct adb_request *req, int sync);
static int macio_adb_autopoll(int devs);
static void macio_adb_poll(void);
static int macio_adb_reset_bus(void);
static void completed(void);

struct adb_driver macio_adb_driver = {
	"MACIO",
	macio_probe,
	macio_init,
	macio_send_request,
	/*macio_write,*/
	macio_adb_autopoll,
	macio_adb_poll,
	macio_adb_reset_bus
};

int macio_probe(void)
{
	return find_compatible_devices("adb", "chrp,adb0")? 0: -ENODEV;
}

int macio_init(void)
{
	struct device_node *adbs;

	adbs = find_compatible_devices("adb", "chrp,adb0");
	if (adbs == 0)
		return -ENXIO;

#if 0
	{ int i;

	printk("macio_adb_init: node = %p, addrs =", adbs->node);
	for (i = 0; i < adbs->n_addrs; ++i)
		printk(" %x(%x)", adbs->addrs[i].address, adbs->addrs[i].size);
	printk(", intrs =");
	for (i = 0; i < adbs->n_intrs; ++i)
		printk(" %x", adbs->intrs[i].line);
	printk("\n"); }
#endif
	
	adb = (volatile struct adb_regs *)
		ioremap(adbs->addrs->address, sizeof(struct adb_regs));

	if (request_irq(adbs->intrs[0].line, macio_adb_interrupt,
			0, "ADB", (void *)0)) {
		printk(KERN_ERR "ADB: can't get irq %d\n",
		       adbs->intrs[0].line);
		return -EAGAIN;
	}

	out_8(&adb->ctrl.r, 0);
	out_8(&adb->intr.r, 0);
	out_8(&adb->error.r, 0);
	out_8(&adb->active_hi.r, 0xff); /* for now, set all devices active */
	out_8(&adb->active_lo.r, 0xff);
	out_8(&adb->autopoll.r, APE);
	out_8(&adb->intr_enb.r, DFB | TAG);

	printk("adb: mac-io driver 1.0 for unified ADB\n");

	return 0;
}

static int macio_adb_autopoll(int devs)
{
	out_8(&adb->active_hi.r, devs >> 8);
	out_8(&adb->active_lo.r, devs);
	out_8(&adb->autopoll.r, devs? APE: 0);
	return 0;
}

static int macio_adb_reset_bus(void)
{
	int timeout = 1000000;

	out_8(&adb->ctrl.r, in_8(&adb->ctrl.r) | ADB_RST);
	while ((in_8(&adb->ctrl.r) & ADB_RST) != 0) {
		if (--timeout == 0) {
			out_8(&adb->ctrl.r, in_8(&adb->ctrl.r) & ~ADB_RST);
			return -1;
		}
	}
	return 0;
}

/* Send an ADB command */
static int macio_send_request(struct adb_request *req, int sync)
{
	unsigned long mflags;
	int i;
	
	if (req->data[0] != ADB_PACKET)
		return -EINVAL;
	
	for (i = 0; i < req->nbytes - 1; ++i)
		req->data[i] = req->data[i+1];
	--req->nbytes;
	
	req->next = 0;
	req->sent = 0;
	req->complete = 0;
	req->reply_len = 0;

	save_flags(mflags);
	cli();
	if (current_req != 0) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = last_req = req;
		out_8(&adb->ctrl.r, in_8(&adb->ctrl.r) | TAR);
	}
	restore_flags(mflags);
	
	if (sync) {
		while (!req->complete)
			macio_adb_poll();
	}

	return 0;
}

static void macio_adb_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	int i, n, err;
	struct adb_request *req;

	if (in_8(&adb->intr.r) & TAG) {
		if ((req = current_req) != 0) {
			/* put the current request in */
			for (i = 0; i < req->nbytes; ++i)
				out_8(&adb->data[i].r, req->data[i]);
			out_8(&adb->dcount.r, req->nbytes & HMB);
			req->sent = 1;
			if (req->reply_expected) {
				out_8(&adb->ctrl.r, DTB + CRE);
			} else {
				out_8(&adb->ctrl.r, DTB);
				completed();
			}
		}
		out_8(&adb->intr.r, 0);
	}

	if (in_8(&adb->intr.r) & DFB) {
		err = in_8(&adb->error.r);
		if (current_req && current_req->sent) {
			/* this is the response to a command */
			req = current_req;
			if (err == 0) {
				req->reply_len = in_8(&adb->dcount.r) & HMB;
				for (i = 0; i < req->reply_len; ++i)
					req->reply[i] = in_8(&adb->data[i].r);
			}
			completed();
		} else if (err == 0) {
			/* autopoll data */
			n = in_8(&adb->dcount.r) & HMB;
			for (i = 0; i < n; ++i)
				adb_rbuf[i] = in_8(&adb->data[i].r);
			adb_input(adb_rbuf, n, regs,
				  in_8(&adb->dcount.r) & APD);
		}
		out_8(&adb->error.r, 0);
		out_8(&adb->intr.r, 0);
	}
}

static void completed(void)
{
	struct adb_request *req = current_req;

	req->complete = 1;
	current_req = req->next;
	if (current_req)
		out_8(&adb->ctrl.r, in_8(&adb->ctrl.r) | TAR);
	if (req->done)
		(*req->done)(req);
}

static void macio_adb_poll(void)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (in_8(&adb->intr.r) != 0)
		macio_adb_interrupt(0, 0, 0);
	restore_flags(flags);
}
