/*
 *  drivers/s390/char/sclp.c
 *     core function to access sclp interface
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/s390_ext.h>
#include <asm/processor.h>

#include "sclp.h"

#define SCLP_CORE_PRINT_HEADER "sclp low level driver: "

/* Structure for register_early_external_interrupt. */
static ext_int_info_t ext_int_info_hwc;

/* spinlock to protect global variables of sclp_core */
static spinlock_t sclp_lock;

/* Mask of valid sclp events */
static sccb_mask_t sclp_receive_mask;
static sccb_mask_t sclp_send_mask;

/* List of registered event types */
static struct list_head sclp_reg_list;

/* sccb queue */
static struct list_head sclp_req_queue;

/* sccb for unconditional read */
static struct sclp_req sclp_read_req;
static char sclp_read_sccb[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));
/* sccb for write mask sccb */
static char sclp_init_sccb[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

/* Timer for init mask retries. */
static struct timer_list retry_timer;

static volatile unsigned long sclp_status = 0;
/* some status flags */
#define SCLP_INIT		0
#define SCLP_RUNNING		1
#define SCLP_READING		2

#define SCLP_INIT_POLL_INTERVAL	1

#define SCLP_COMMAND_INITIATED	0
#define SCLP_BUSY		2
#define SCLP_NOT_OPERATIONAL	3

/*
 * assembler instruction for Service Call
 */
static int
__service_call(sclp_cmdw_t command, void *sccb)
{
	int cc;

	/*
	 *  Mnemonic:	SERVC	Rx, Ry	[RRE]
	 *
	 *  Rx: SCLP command word
	 *  Ry: address of SCCB
	 */
	__asm__ __volatile__(
		"   .insn rre,0xb2200000,%1,%2\n"  /* servc %1,%2 */
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=&d" (cc)
		: "d" (command), "a" (__pa(sccb))
		: "cc", "memory" );
	/*
	 * cc == 0:   Service Call succesful initiated
	 * cc == 2:   SCLP busy, new Service Call not initiated,
	 *	      new SCCB unchanged
	 * cc == 3:   SCLP function not operational
	 */
	if (cc == SCLP_NOT_OPERATIONAL)
		return -EIO;
	/*
	 * We set the SCLP_RUNNING bit for cc 2 as well because if
	 * service_call returns cc 2 some old request is running
	 * that has to complete first
	 */
	set_bit(SCLP_RUNNING, &sclp_status);
	if (cc == SCLP_BUSY)
		return -EBUSY;
	return 0;
}

static int
sclp_start_request(void)
{
	struct sclp_req *req;
	int rc;
	unsigned long flags;

	/* quick exit if sclp is already in use */
	if (test_bit(SCLP_RUNNING, &sclp_status))
		return -EBUSY;
	spin_lock_irqsave(&sclp_lock, flags);
	/* Get first request on queue if available */
	req = NULL;
	if (!list_empty(&sclp_req_queue))
		req = list_entry(sclp_req_queue.next, struct sclp_req, list);
	if (req) {
		rc = __service_call(req->command, req->sccb);
		if (rc) {
			req->status = SCLP_REQ_FAILED;
			list_del(&req->list);
		} else
			req->status = SCLP_REQ_RUNNING;
	} else
		rc = -EINVAL;
	spin_unlock_irqrestore(&sclp_lock, flags);
	if (rc == -EIO && req->callback != NULL)
		req->callback(req, req->callback_data);
	return rc;
}

static int
sclp_process_evbufs(struct sccb_header *sccb)
{
	int result;
	unsigned long flags;
	struct evbuf_header *evbuf;
	struct list_head *l;
	struct sclp_register *t;

	spin_lock_irqsave(&sclp_lock, flags);
	evbuf = (struct evbuf_header *) (sccb + 1);
	result = 0;
	while ((addr_t) evbuf < (addr_t) sccb + sccb->length) {
		/* check registered event */
		t = NULL;
		list_for_each(l, &sclp_reg_list) {
			t = list_entry(l, struct sclp_register, list);
			if (t->receive_mask & (1 << (32 - evbuf->type))) {
				if (t->receiver_fn != NULL) {
					spin_unlock_irqrestore(&sclp_lock,
							       flags);
					t->receiver_fn(evbuf);
					spin_lock_irqsave(&sclp_lock, flags);
				}
				break;
			}
			else
				t = NULL;
		}
		/* Check for unrequested event buffer */
		if (t == NULL)
			result = -ENOSYS;
		evbuf = (struct evbuf_header *)
				((addr_t) evbuf + evbuf->length);
	}
	spin_unlock_irqrestore(&sclp_lock, flags);
	return result;
}

char *
sclp_error_message(u16 rc)
{
	static struct {
		u16 code; char *msg;
	} sclp_errors[] = {
		{ 0x0000, "No response code stored (machine malfunction)" },
		{ 0x0020, "Normal Completion" },
		{ 0x0040, "SCLP equipment check" },
		{ 0x0100, "SCCB boundary violation" },
		{ 0x01f0, "Invalid command" },
		{ 0x0220, "Normal Completion; suppressed buffers pending" },
		{ 0x0300, "Insufficient SCCB length" },
		{ 0x0340, "Contained SCLP equipment check" },
		{ 0x05f0, "Target resource in improper state" },
		{ 0x40f0, "Invalid function code/not installed" },
		{ 0x60f0, "No buffers stored" },
		{ 0x62f0, "No buffers stored; suppressed buffers pending" },
		{ 0x70f0, "Invalid selection mask" },
		{ 0x71f0, "Event buffer exceeds available space" },
		{ 0x72f0, "Inconsistent lengths" },
		{ 0x73f0, "Event buffer syntax error" }
	};
	int i;
	for (i = 0; i < sizeof(sclp_errors)/sizeof(sclp_errors[0]); i++)
		if (rc == sclp_errors[i].code)
			return sclp_errors[i].msg;
	return "Invalid response code";
}

/*
 * postprocessing of unconditional read service call
 */
static void
sclp_unconditional_read_cb(struct sclp_req *read_req, void *data)
{
	struct sccb_header *sccb;

	sccb = read_req->sccb;
	if (sccb->response_code == 0x0020 ||
	    sccb->response_code == 0x0220) {
		if (sclp_process_evbufs(sccb) != 0)
			printk(KERN_WARNING SCLP_CORE_PRINT_HEADER
			       "unconditional read: "
			       "unrequested event buffer received.\n");
	}

	if (sccb->response_code != 0x0020)
		printk(KERN_WARNING SCLP_CORE_PRINT_HEADER
		       "unconditional read: %s (response code=0x%x).\n",
		       sclp_error_message(sccb->response_code),
		       sccb->response_code);

	clear_bit(SCLP_READING, &sclp_status);
}

/*
 * Function to queue Read Event Data/Unconditional Read
 */
static void
__sclp_unconditional_read(void)
{
	struct sccb_header *sccb;
	struct sclp_req *read_req;

	/*
	 * Don't try to initiate Unconditional Read if we are not able to
	 * receive anything
	 */
	if (sclp_receive_mask == 0)
		return;
	/* Don't try reading if a read is already outstanding */
	if (test_and_set_bit(SCLP_READING, &sclp_status))
		return;
	/* Initialize read sccb */
	sccb = (struct sccb_header *) sclp_read_sccb;
	clear_page(sccb);
	sccb->length = PAGE_SIZE;
	sccb->function_code = 0;	/* unconditional read */
	sccb->control_mask[2] = 0x80;	/* variable length response */
	/* Initialize request structure */
	read_req = &sclp_read_req;
	read_req->command = SCLP_CMDW_READDATA;
	read_req->status = SCLP_REQ_QUEUED;
	read_req->callback = sclp_unconditional_read_cb;
	read_req->sccb = sccb;
	/* Add read request to the head of queue */
	list_add(&read_req->list, &sclp_req_queue);
}

/* Bit masks to interpret external interruption parameter contents. */
#define EXT_INT_SCCB_MASK		0xfffffff8
#define EXT_INT_STATECHANGE_PENDING	0x00000002
#define EXT_INT_EVBUF_PENDING		0x00000001

/*
 * Handler for service-signal external interruptions
 */
static void
sclp_interrupt_handler(struct pt_regs *regs, __u16 code)
{
	u32 ext_int_param, finished_sccb, evbuf_pending;
	struct list_head *l;
	struct sclp_req *req, *tmp;

	spin_lock(&sclp_lock);
	/*
	 * Only process interrupt if sclp is initialized.
	 * This avoids strange effects for a pending request
	 * from before the last re-ipl.
	 */
	if (!test_bit(SCLP_INIT, &sclp_status)) {
		/* Now clear the running bit */
		clear_bit(SCLP_RUNNING, &sclp_status);
		spin_unlock(&sclp_lock);
		return;
	}
	ext_int_param = S390_lowcore.ext_params;
	finished_sccb = ext_int_param & EXT_INT_SCCB_MASK;
	evbuf_pending = ext_int_param & (EXT_INT_EVBUF_PENDING |
					 EXT_INT_STATECHANGE_PENDING);
	req = NULL;
	if (finished_sccb != 0U) {
		list_for_each(l, &sclp_req_queue) {
			tmp = list_entry(l, struct sclp_req, list);
			if (finished_sccb == (u32)(addr_t) tmp->sccb) {
				list_del(&tmp->list);
				req = tmp;
				break;
			}
		}
	}
	spin_unlock(&sclp_lock);
	/* Perform callback */
	if (req != NULL) {
		req->status = SCLP_REQ_DONE;
		if (req->callback != NULL)
			req->callback(req, req->callback_data);
	}
	spin_lock(&sclp_lock);
	/* Head queue a read sccb if an event buffer is pending */
	if (evbuf_pending)
		__sclp_unconditional_read();
	/* Now clear the running bit */
	clear_bit(SCLP_RUNNING, &sclp_status);
	spin_unlock(&sclp_lock);
	/* and start next request on the queue */
	sclp_start_request();
}

/*
 * Wait synchronously for external interrupt of sclp. We may not receive
 * any other external interrupt, so we disable all other external interrupts
 * in control register 0.
 */
void
sclp_sync_wait(void)
{
	unsigned long psw_mask;
	unsigned long cr0, cr0_sync;

	/*
	 * save cr0
	 * enable service signal external interruption (cr0.22)
	 * disable cr0.20-21, cr0.25, cr0.27, cr0.30-31
	 * don't touch any other bit in cr0
	 */
	__ctl_store(cr0, 0, 0);
	cr0_sync = cr0;
	cr0_sync |= 0x00000200;
	cr0_sync &= 0xFFFFF3AC;
	__ctl_load(cr0_sync, 0, 0);

	/* enable external interruptions (PSW-mask.7) */
	asm volatile ("STOSM 0(%1),0x01"
		      : "=m" (psw_mask) : "a" (&psw_mask) : "memory");

	/* wait until ISR signals receipt of interrupt */
	while (test_bit(SCLP_RUNNING, &sclp_status)) {
		barrier();
		cpu_relax();
	}

	/* disable external interruptions */
	asm volatile ("SSM 0(%0)"
		      : : "a" (&psw_mask) : "memory");

	/* restore cr0 */
	__ctl_load(cr0, 0, 0);
}

/*
 * Queue an SCLP request. Request will immediately be processed if queue is
 * empty.
 */
void
sclp_add_request(struct sclp_req *req)
{
	unsigned long flags;

	if (!test_bit(SCLP_INIT, &sclp_status)) {
		req->status = SCLP_REQ_FAILED;
		if (req->callback != NULL)
			req->callback(req, req->callback_data);
		return;
	}
	spin_lock_irqsave(&sclp_lock, flags);
	/* queue the request */
	req->status = SCLP_REQ_QUEUED;
	list_add_tail(&req->list, &sclp_req_queue);
	spin_unlock_irqrestore(&sclp_lock, flags);
	/* try to start the first request on the queue */
	sclp_start_request();
}

/* state change notification */
struct sclp_statechangebuf {
	struct evbuf_header	header;
	u8		validity_sclp_active_facility_mask : 1;
	u8		validity_sclp_receive_mask : 1;
	u8		validity_sclp_send_mask : 1;
	u8		validity_read_data_function_mask : 1;
	u16		_zeros : 12;
	u16		mask_length;
	u64		sclp_active_facility_mask;
	sccb_mask_t	sclp_receive_mask;
	sccb_mask_t	sclp_send_mask;
	u32		read_data_function_mask;
} __attribute__((packed));

static inline void
__sclp_notify_state_change(void)
{
	struct list_head *l;
	struct sclp_register *t;
	sccb_mask_t receive_mask, send_mask;

	list_for_each(l, &sclp_reg_list) {
		t = list_entry(l, struct sclp_register, list);
		receive_mask = t->receive_mask & sclp_receive_mask;
		send_mask = t->send_mask & sclp_send_mask;
		if (t->sclp_receive_mask != receive_mask ||
		    t->sclp_send_mask != send_mask) {
			t->sclp_receive_mask = receive_mask;
			t->sclp_send_mask = send_mask;
			if (t->state_change_fn != NULL)
				t->state_change_fn(t);
		}
	}
}

static void
sclp_state_change(struct evbuf_header *evbuf)
{
	unsigned long flags;
	struct sclp_statechangebuf *scbuf;

	spin_lock_irqsave(&sclp_lock, flags);
	scbuf = (struct sclp_statechangebuf *) evbuf;

	if (scbuf->validity_sclp_receive_mask) {
		if (scbuf->mask_length != sizeof(sccb_mask_t))
			printk(KERN_WARNING SCLP_CORE_PRINT_HEADER
			       "state change event with mask length %i\n",
			       scbuf->mask_length);
		else
			/* set new receive mask */
			sclp_receive_mask = scbuf->sclp_receive_mask;
	}

	if (scbuf->validity_sclp_send_mask) {
		if (scbuf->mask_length != sizeof(sccb_mask_t))
			printk(KERN_WARNING SCLP_CORE_PRINT_HEADER
			       "state change event with mask length %i\n",
			       scbuf->mask_length);
		else
			/* set new send mask */
			sclp_send_mask = scbuf->sclp_send_mask;
	}

	__sclp_notify_state_change();
	spin_unlock_irqrestore(&sclp_lock, flags);
}

static struct sclp_register sclp_state_change_event = {
	.receive_mask = EvTyp_StateChange_Mask,
	.receiver_fn = sclp_state_change
};


/*
 * SCLP quiesce event handler
 */
#ifdef CONFIG_SMP
static cpumask_t cpu_quiesce_map;

static void
do_load_quiesce_psw(void * __unused)
{
	psw_t quiesce_psw;

	cpu_clear(smp_processor_id(), cpu_quiesce_map);
	if (smp_processor_id() == 0) {
		/* Wait for all other cpus to enter do_load_quiesce_psw */
		while (!cpus_empty(cpu_quiesce_map));
		/* Quiesce the last cpu with the special psw */
		quiesce_psw.mask = PSW_BASE_BITS | PSW_MASK_WAIT;
		quiesce_psw.addr = 0xfff;
		__load_psw(quiesce_psw);
	}
	signal_processor(smp_processor_id(), sigp_stop);
}

static void
do_machine_quiesce(void)
{
	cpu_quiesce_map = cpu_online_map;
	on_each_cpu(do_load_quiesce_psw, NULL, 0, 0);
}
#else
static void
do_machine_quiesce(void)
{
	psw_t quiesce_psw;

	quiesce_psw.mask = PSW_BASE_BITS | PSW_MASK_WAIT;
	quiesce_psw.addr = 0xfff;
	__load_psw(quiesce_psw);
}
#endif

extern void ctrl_alt_del(void);

static void
sclp_quiesce(struct evbuf_header *evbuf)
{
	/*
	 * We got a "shutdown" request.
	 * Add a call to an appropriate "shutdown" routine here. This
	 * routine should set all PSWs to 'disabled-wait', 'stopped'
	 * or 'check-stopped' - except 1 PSW which needs to carry a
	 * special bit pattern called 'quiesce PSW'.
	 */
	_machine_restart = (void *) do_machine_quiesce;
	_machine_halt = do_machine_quiesce;
	_machine_power_off = do_machine_quiesce;
	ctrl_alt_del();
}

static struct sclp_register sclp_quiesce_event = {
	.receive_mask = EvTyp_SigQuiesce_Mask,
	.receiver_fn = sclp_quiesce
};

/* initialisation of SCLP */
struct init_sccb {
	struct sccb_header header;
	u16 _reserved;
	u16 mask_length;
	sccb_mask_t receive_mask;
	sccb_mask_t send_mask;
	sccb_mask_t sclp_send_mask;
	sccb_mask_t sclp_receive_mask;
} __attribute__((packed));

static void sclp_init_mask_retry(unsigned long);

static int
sclp_init_mask(void)
{
	unsigned long flags;
	struct init_sccb *sccb;
	struct sclp_req *req;
	struct list_head *l;
	struct sclp_register *t;
	int rc;

	sccb = (struct init_sccb *) sclp_init_sccb;
	/* stick the request structure to the end of the init sccb page */
	req = (struct sclp_req *) ((addr_t) sccb + PAGE_SIZE) - 1;

	/* SCLP setup concerning receiving and sending Event Buffers */
	req->command = SCLP_CMDW_WRITEMASK;
	req->status = SCLP_REQ_QUEUED;
	req->callback = NULL;
	req->sccb = sccb;
	/* setup sccb for writemask command */
	memset(sccb, 0, sizeof(struct init_sccb));
	sccb->header.length = sizeof(struct init_sccb);
	sccb->mask_length = sizeof(sccb_mask_t);
	/* copy in the sccb mask of the registered event types */
	spin_lock_irqsave(&sclp_lock, flags);
	list_for_each(l, &sclp_reg_list) {
		t = list_entry(l, struct sclp_register, list);
		sccb->receive_mask |= t->receive_mask;
		sccb->send_mask |= t->send_mask;
	}
	sccb->sclp_receive_mask = 0;
	sccb->sclp_send_mask = 0;
	if (test_bit(SCLP_INIT, &sclp_status)) {
		/* add request to sclp queue */
		list_add_tail(&req->list, &sclp_req_queue);
		spin_unlock_irqrestore(&sclp_lock, flags);
		/* and start if SCLP is idle */
		sclp_start_request();
		/* now wait for completion */
		while (req->status != SCLP_REQ_DONE &&
		       req->status != SCLP_REQ_FAILED)
			sclp_sync_wait();
		spin_lock_irqsave(&sclp_lock, flags);
	} else {
		/*
		 * Special case for the very first write mask command.
		 * The interrupt handler is not removing request from
		 * the request queue and doesn't call callbacks yet
		 * because there might be an pending old interrupt
		 * after a Re-IPL. We have to receive and ignore it.
		 */
		do {
			rc = __service_call(req->command, req->sccb);
			spin_unlock_irqrestore(&sclp_lock, flags);
			if (rc == -EIO)
				return -ENOSYS;
			sclp_sync_wait();
			spin_lock_irqsave(&sclp_lock, flags);
		} while (rc == -EBUSY);
	}
	if (sccb->header.response_code != 0x0020) {
		/* WRITEMASK failed - we cannot rely on receiving a state
		   change event, so initially, polling is the only alternative
		   for us to ever become operational. */
		if (!timer_pending(&retry_timer) ||
		    !mod_timer(&retry_timer,
			       jiffies + SCLP_INIT_POLL_INTERVAL*HZ)) {
			retry_timer.function = sclp_init_mask_retry;
			retry_timer.data = 0;
			retry_timer.expires = jiffies +
				SCLP_INIT_POLL_INTERVAL*HZ;
			add_timer(&retry_timer);
		}
	} else {
		sclp_receive_mask = sccb->sclp_receive_mask;
		sclp_send_mask = sccb->sclp_send_mask;
		__sclp_notify_state_change();
	}
	spin_unlock_irqrestore(&sclp_lock, flags);
	return 0;
}

static void
sclp_init_mask_retry(unsigned long data) 
{
	sclp_init_mask();
}

/*
 * sclp setup function. Called early (no kmalloc!) from sclp_console_init().
 */
static int
sclp_init(void)
{
	int rc;

	if (test_bit(SCLP_INIT, &sclp_status))
		/* Already initialized. */
		return 0;

	spin_lock_init(&sclp_lock);
	INIT_LIST_HEAD(&sclp_req_queue);

	/* init event list */
	INIT_LIST_HEAD(&sclp_reg_list);
	list_add(&sclp_state_change_event.list, &sclp_reg_list);
	list_add(&sclp_quiesce_event.list, &sclp_reg_list);

	/*
	 * request the 0x2401 external interrupt
	 * The sclp driver is initialized early (before kmalloc works). We
	 * need to use register_early_external_interrupt.
	 */
	if (register_early_external_interrupt(0x2401, sclp_interrupt_handler,
					      &ext_int_info_hwc) != 0)
		return -EBUSY;

	/* enable service-signal external interruptions,
	 * Control Register 0 bit 22 := 1
	 * (besides PSW bit 7 must be set to 1 sometimes for external
	 * interruptions)
	 */
	ctl_set_bit(0, 9);

	init_timer(&retry_timer);
	/* do the initial write event mask */
	rc = sclp_init_mask();
	if (rc == 0) {
		/* Ok, now everything is setup right. */
		set_bit(SCLP_INIT, &sclp_status);
		return 0;
	}

	/* The sclp_init_mask failed. SCLP is broken, unregister and exit. */
	ctl_clear_bit(0,9);
	unregister_early_external_interrupt(0x2401, sclp_interrupt_handler,
					    &ext_int_info_hwc);

	return rc;
}

/*
 * Register the SCLP event listener identified by REG. Return 0 on success.
 * Some error codes and their meaning:
 *
 *  -ENODEV = SCLP interface is not supported on this machine
 *   -EBUSY = there is already a listener registered for the requested
 *            event type
 *     -EIO = SCLP interface is currently not operational
 */
int
sclp_register(struct sclp_register *reg)
{
	unsigned long flags;
	struct list_head *l;
	struct sclp_register *t;

	if (!MACHINE_HAS_SCLP)
		return -ENODEV;

	if (!test_bit(SCLP_INIT, &sclp_status))
		sclp_init();
	spin_lock_irqsave(&sclp_lock, flags);
	/* check already registered event masks for collisions */
	list_for_each(l, &sclp_reg_list) {
		t = list_entry(l, struct sclp_register, list);
		if (t->receive_mask & reg->receive_mask ||
		    t->send_mask & reg->send_mask) {
			spin_unlock_irqrestore(&sclp_lock, flags);
			return -EBUSY;
		}
	}
	/*
	 * set present mask to 0 to trigger state change
	 * callback in sclp_init_mask
	 */
	reg->sclp_receive_mask = 0;
	reg->sclp_send_mask = 0;
	list_add(&reg->list, &sclp_reg_list);
	spin_unlock_irqrestore(&sclp_lock, flags);
	sclp_init_mask();
	return 0;
}

/*
 * Unregister the SCLP event listener identified by REG.
 */
void
sclp_unregister(struct sclp_register *reg)
{
	unsigned long flags;

	spin_lock_irqsave(&sclp_lock, flags);
	list_del(&reg->list);
	spin_unlock_irqrestore(&sclp_lock, flags);
	sclp_init_mask();
}

#define	SCLP_EVBUF_PROCESSED	0x80

/*
 * Traverse array of event buffers contained in SCCB and remove all buffers
 * with a set "processed" flag. Return the number of unprocessed buffers.
 */
int
sclp_remove_processed(struct sccb_header *sccb)
{
	struct evbuf_header *evbuf;
	int unprocessed;
	u16 remaining;

	evbuf = (struct evbuf_header *) (sccb + 1);
	unprocessed = 0;
	remaining = sccb->length - sizeof(struct sccb_header);
	while (remaining > 0) {
		remaining -= evbuf->length;
		if (evbuf->flags & SCLP_EVBUF_PROCESSED) {
			sccb->length -= evbuf->length;
			memcpy((void *) evbuf,
			       (void *) ((addr_t) evbuf + evbuf->length),
			       remaining);
		} else {
			unprocessed++;
			evbuf = (struct evbuf_header *)
					((addr_t) evbuf + evbuf->length);
		}
	}

	return unprocessed;
}

module_init(sclp_init);

EXPORT_SYMBOL(sclp_add_request);
EXPORT_SYMBOL(sclp_sync_wait);
EXPORT_SYMBOL(sclp_register);
EXPORT_SYMBOL(sclp_unregister);
EXPORT_SYMBOL(sclp_error_message);
