/*
 *  linux/arch/arm/kernel/ecard.c
 *
 *  Copyright 1995-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Find all installed expansion cards, and handle interrupts from them.
 *
 *  Created from information from Acorns RiscOS3 PRMs
 *
 *  08-Dec-1996	RMK	Added code for the 9'th expansion card - the ether
 *			podule slot.
 *  06-May-1997	RMK	Added blacklist for cards whose loader doesn't work.
 *  12-Sep-1997	RMK	Created new handling of interrupt enables/disables
 *			- cards can now register their own routine to control
 *			interrupts (recommended).
 *  29-Sep-1997	RMK	Expansion card interrupt hardware not being re-enabled
 *			on reset from Linux. (Caused cards not to respond
 *			under RiscOS without hard reset).
 *  15-Feb-1998	RMK	Added DMA support
 *  12-Sep-1998	RMK	Added EASI support
 *  10-Jan-1999	RMK	Run loaders in a simulated RISC OS environment.
 *  17-Apr-1999	RMK	Support for EASI Type C cycles.
 */
#define ECARD_C
#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

#ifndef CONFIG_ARCH_RPC
#define HAVE_EXPMASK
#endif

enum req {
	req_readbytes,
	req_reset
};

struct ecard_request {
	enum req	req;
	ecard_t		*ec;
	unsigned int	address;
	unsigned int	length;
	unsigned int	use_loader;
	void		*buffer;
};

struct expcard_blacklist {
	unsigned short	 manufacturer;
	unsigned short	 product;
	const char	*type;
};

static ecard_t *cards;
static ecard_t *slot_to_expcard[MAX_ECARDS];
static unsigned int ectcr;
#ifdef HAS_EXPMASK
static unsigned int have_expmask;
#endif

/* List of descriptions of cards which don't have an extended
 * identification, or chunk directories containing a description.
 */
static struct expcard_blacklist __initdata blacklist[] = {
	{ MANU_ACORN, PROD_ACORN_ETHER1, "Acorn Ether1" }
};

asmlinkage extern int
ecard_loader_reset(volatile unsigned char *pa, loader_t loader);
asmlinkage extern int
ecard_loader_read(int off, volatile unsigned char *pa, loader_t loader);
extern int setup_arm_irq(int, struct irqaction *);
extern void do_ecard_IRQ(int, struct pt_regs *);


static void
ecard_irq_noexpmask(int intr_no, void *dev_id, struct pt_regs *regs);

static struct irqaction irqexpansioncard = {
	ecard_irq_noexpmask, SA_INTERRUPT, 0, "expansion cards", NULL, NULL
};

static inline unsigned short
ecard_getu16(unsigned char *v)
{
	return v[0] | v[1] << 8;
}

static inline signed long
ecard_gets24(unsigned char *v)
{
	return v[0] | v[1] << 8 | v[2] << 16 | ((v[2] & 0x80) ? 0xff000000 : 0);
}

static inline ecard_t *
slot_to_ecard(unsigned int slot)
{
	return slot < MAX_ECARDS ? slot_to_expcard[slot] : NULL;
}

/* ===================== Expansion card daemon ======================== */
/*
 * Since the loader programs on the expansion cards need to be run
 * in a specific environment, create a separate task with this
 * environment up, and pass requests to this task as and when we
 * need to.
 *
 * This should allow 99% of loaders to be called from Linux.
 *
 * From a security standpoint, we trust the card vendors.  This
 * may be a misplaced trust.
 */
#define BUS_ADDR(x) ((((unsigned long)(x)) << 2) + IO_BASE)
#define POD_INT_ADDR(x)	((volatile unsigned char *)\
			 ((BUS_ADDR((x)) - IO_BASE) + IO_START))

static void
ecard_task_reset(struct ecard_request *req)
{
	if (req->ec == NULL) {
		ecard_t *ec;

		for (ec = cards; ec; ec = ec->next)
			if (ec->loader)
				ecard_loader_reset(POD_INT_ADDR(ec->podaddr),
						   ec->loader);
	} else if (req->ec->loader)
		ecard_loader_reset(POD_INT_ADDR(req->ec->podaddr),
				   req->ec->loader);
}

static void
ecard_task_readbytes(struct ecard_request *req)
{
	unsigned char *buf = (unsigned char *)req->buffer;
	volatile unsigned char *base_addr =
		(volatile unsigned char *)POD_INT_ADDR(req->ec->podaddr);
	unsigned int len = req->length;

	if (req->ec->slot_no == 8) {
		/*
		 * The card maintains an index which
		 * increments the address into a 4096-byte
		 * page on each access.  We need to keep
		 * track of the counter.
		 */
		static unsigned int index;
		unsigned int offset, page;
		unsigned char byte = 0; /* keep gcc quiet */

		offset = req->address & 4095;
		page   = req->address >> 12;

		if (page > 256)
			return;

		page *= 4;

		if (offset == 0 || index > offset) {
			/*
			 * We need to reset the index counter.
			 */
			*base_addr = 0;
			index = 0;
		}

		while (index <= offset) {
			byte = base_addr[page];
			index += 1;
		}

		while (len--) {
			*buf++ = byte;
			if (len) {
				byte = base_addr[page];
				index += 1;
			}
		}
	} else {
		unsigned int off = req->address;

		if (!req->use_loader || !req->ec->loader) {
			off *= 4;
			while (len--) {
				*buf++ = base_addr[off];
				off += 4;
			}
		} else {
			while(len--) {
				/*
				 * The following is required by some
				 * expansion card loader programs.
				 */
				*(unsigned long *)0x108 = 0;
				*buf++ = ecard_loader_read(off++, base_addr,
							   req->ec->loader);
			}
		}
	}

}

#ifdef CONFIG_CPU_32
static pid_t ecard_pid;
static wait_queue_head_t ecard_wait;
static wait_queue_head_t ecard_done;
static struct ecard_request *ecard_req;

/* to be removed when exec_mmap becomes extern */
static int exec_mmap(void)
{
	struct mm_struct * mm, * old_mm;

	old_mm = current->mm;
	if (old_mm && atomic_read(&old_mm->mm_users) == 1) {
		flush_cache_mm(old_mm);
		mm_release();
		exit_mmap(old_mm);
		flush_tlb_mm(old_mm);
		return 0;
	}

	mm = mm_alloc();
	if (mm) {
		struct mm_struct *active_mm = current->active_mm;

		current->mm = mm;
		current->active_mm = mm;
		activate_mm(active_mm, mm);
		mm_release();
		if (old_mm) {
			if (active_mm != old_mm) BUG();
			mmput(old_mm);
			return 0;
		}
		mmdrop(active_mm);
		return 0;
	}
	return -ENOMEM;
}

/*
 * Set up the expansion card
 * daemon's environment.
 */
static void ecard_init_task(int force)
{
	/* We want to set up the page tables for the following mapping:
	 *  Virtual	Physical
	 *  0x03000000	0x03000000
	 *  0x03010000	unmapped
	 *  0x03210000	0x03210000
	 *  0x03400000	unmapped
	 *  0x08000000	0x08000000
	 *  0x10000000	unmapped
	 *
	 * FIXME: we don't follow this 100% yet.
	 */
	pgd_t *src_pgd, *dst_pgd;
	unsigned int dst_addr = IO_START;

	if (!force)
		exec_mmap();

	src_pgd = pgd_offset(current->mm, IO_BASE);
	dst_pgd = pgd_offset(current->mm, dst_addr);

	while (dst_addr < IO_START + IO_SIZE) {
		*dst_pgd++ = *src_pgd++;
		dst_addr += PGDIR_SIZE;
	}

	flush_tlb_range(current->mm, IO_START, IO_START + IO_SIZE);

	dst_addr = EASI_START;
	src_pgd = pgd_offset(current->mm, EASI_BASE);
	dst_pgd = pgd_offset(current->mm, dst_addr);

	while (dst_addr < EASI_START + EASI_SIZE) {
		*dst_pgd++ = *src_pgd++;
		dst_addr += PGDIR_SIZE;
	}

	flush_tlb_range(current->mm, EASI_START, EASI_START + EASI_SIZE);
}

static int
ecard_task(void * unused)
{
	struct task_struct *tsk = current;

	tsk->session = 1;
	tsk->pgrp = 1;

	/*
	 * We don't want /any/ signals, not even SIGKILL
	 */
	sigfillset(&tsk->blocked);
	sigemptyset(&tsk->pending.signal);
	recalc_sigpending(tsk);

	strcpy(tsk->comm, "kecardd");

	/*
	 * Set up the environment
	 */
	ecard_init_task(0);

	while (1) {
		struct ecard_request *req;

		do {
			req = xchg(&ecard_req, NULL);

			if (req == NULL) {
				sigemptyset(&tsk->pending.signal);
				interruptible_sleep_on(&ecard_wait);
			}
		} while (req == NULL);

		switch (req->req) {
		case req_readbytes:
			ecard_task_readbytes(req);
			break;

		case req_reset:
			ecard_task_reset(req);
			break;
		}
		wake_up(&ecard_done);
	}
}

/*
 * Wake the expansion card daemon to action our request.
 *
 * FIXME: The test here is not sufficient to detect if the
 * kcardd is running.
 */
static inline void
ecard_call(struct ecard_request *req)
{
	/*
	 * If we're called from task 0, or from an
	 * interrupt (will be keyboard interrupt),
	 * we forcefully set up the memory map, and
	 * call the loader.  We can't schedule, or
	 * sleep for this call.
	 */
	if ((current == &init_task || in_interrupt()) &&
	    req->req == req_reset && req->ec == NULL) {
		ecard_init_task(1);
		ecard_task_reset(req);
	} else {
		if (ecard_pid <= 0)
			ecard_pid = kernel_thread(ecard_task, NULL,
					CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

		ecard_req = req;

		wake_up(&ecard_wait);

		sleep_on(&ecard_done);
	}
}
#else
/*
 * On 26-bit processors, we don't need the kcardd thread to access the
 * expansion card loaders.  We do it directly.
 */
static inline void
ecard_call(struct ecard_request *req)
{
	if (req->req == req_reset)
		ecard_task_reset(req);
	else
		ecard_task_readbytes(req);
}
#endif

/* ======================= Mid-level card control ===================== */
/*
 * This is called to reset the loaders for each expansion card on reboot.
 *
 * This is required to make sure that the card is in the correct state
 * that RiscOS expects it to be.
 */
void
ecard_reset(int slot)
{
	struct ecard_request req;

	req.req = req_reset;

	if (slot < 0)
		req.ec = NULL;
	else
		req.ec = slot_to_ecard(slot);

	ecard_call(&req);

#ifdef HAS_EXPMASK
	if (have_expmask && slot < 0) {
		have_expmask |= ~0;
		EXPMASK_ENABLE = have_expmask;
	}
#endif
}

static void
ecard_readbytes(void *addr, ecard_t *ec, int off, int len, int useld)
{
	struct ecard_request req;

	req.req		= req_readbytes;
	req.ec		= ec;
	req.address	= off;
	req.length	= len;
	req.use_loader	= useld;
	req.buffer	= addr;

	ecard_call(&req);
}

int ecard_readchunk(struct in_chunk_dir *cd, ecard_t *ec, int id, int num)
{
	struct ex_chunk_dir excd;
	int index = 16;
	int useld = 0;

	if (!ec->cid.cd)
		return 0;

	while(1) {
		ecard_readbytes(&excd, ec, index, 8, useld);
		index += 8;
		if (c_id(&excd) == 0) {
			if (!useld && ec->loader) {
				useld = 1;
				index = 0;
				continue;
			}
			return 0;
		}
		if (c_id(&excd) == 0xf0) { /* link */
			index = c_start(&excd);
			continue;
		}
		if (c_id(&excd) == 0x80) { /* loader */
			if (!ec->loader) {
				ec->loader = (loader_t)kmalloc(c_len(&excd),
							       GFP_KERNEL);
				if (ec->loader)
					ecard_readbytes(ec->loader, ec,
							(int)c_start(&excd),
							c_len(&excd), useld);
				else
					return 0;
			}
			continue;
		}
		if (c_id(&excd) == id && num-- == 0)
			break;
	}

	if (c_id(&excd) & 0x80) {
		switch (c_id(&excd) & 0x70) {
		case 0x70:
			ecard_readbytes((unsigned char *)excd.d.string, ec,
					(int)c_start(&excd), c_len(&excd),
					useld);
			break;
		case 0x00:
			break;
		}
	}
	cd->start_offset = c_start(&excd);
	memcpy(cd->d.string, excd.d.string, 256);
	return 1;
}

/* ======================= Interrupt control ============================ */

static void ecard_def_irq_enable(ecard_t *ec, int irqnr)
{
#ifdef HAS_EXPMASK
	if (irqnr < 4 && have_expmask) {
		have_expmask |= 1 << irqnr;
		EXPMASK_ENABLE = have_expmask;
	}
#endif
}

static void ecard_def_irq_disable(ecard_t *ec, int irqnr)
{
#ifdef HAS_EXPMASK
	if (irqnr < 4 && have_expmask) {
		have_expmask &= ~(1 << irqnr);
		EXPMASK_ENABLE = have_expmask;
	}
#endif
}

static int ecard_def_irq_pending(ecard_t *ec)
{
	return !ec->irqmask || ec->irqaddr[0] & ec->irqmask;
}

static void ecard_def_fiq_enable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_enable called - impossible");
}

static void ecard_def_fiq_disable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_disable called - impossible");
}

static int ecard_def_fiq_pending(ecard_t *ec)
{
	return !ec->fiqmask || ec->fiqaddr[0] & ec->fiqmask;
}

static expansioncard_ops_t ecard_default_ops = {
	ecard_def_irq_enable,
	ecard_def_irq_disable,
	ecard_def_irq_pending,
	ecard_def_fiq_enable,
	ecard_def_fiq_disable,
	ecard_def_fiq_pending
};

/*
 * Enable and disable interrupts from expansion cards.
 * (interrupts are disabled for these functions).
 *
 * They are not meant to be called directly, but via enable/disable_irq.
 */
void ecard_enableirq(unsigned int irqnr)
{
	ecard_t *ec = slot_to_ecard(irqnr - 32);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->irqenable)
			ec->ops->irqenable(ec, irqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable IRQs for %d\n", irqnr);
	}
}

void ecard_disableirq(unsigned int irqnr)
{
	ecard_t *ec = slot_to_ecard(irqnr - 32);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops && ec->ops->irqdisable)
			ec->ops->irqdisable(ec, irqnr);
	}
}

void ecard_enablefiq(unsigned int fiqnr)
{
	ecard_t *ec = slot_to_ecard(fiqnr);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->fiqenable)
			ec->ops->fiqenable(ec, fiqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable FIQs for %d\n", fiqnr);
	}
}

void ecard_disablefiq(unsigned int fiqnr)
{
	ecard_t *ec = slot_to_ecard(fiqnr);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops->fiqdisable)
			ec->ops->fiqdisable(ec, fiqnr);
	}
}

static void
ecard_dump_irq_state(ecard_t *ec)
{
	printk("  %d: %sclaimed, ",
	       ec->slot_no,
	       ec->claimed ? "" : "not ");

	if (ec->ops && ec->ops->irqpending &&
	    ec->ops != &ecard_default_ops)
		printk("irq %spending\n",
		       ec->ops->irqpending(ec) ? "" : "not ");
	else
		printk("irqaddr %p, mask = %02X, status = %02X\n",
		       ec->irqaddr, ec->irqmask, *ec->irqaddr);
}

static void
ecard_check_lockup(void)
{
	static int last, lockup;
	ecard_t *ec;

	/*
	 * If the timer interrupt has not run since the last million
	 * unrecognised expansion card interrupts, then there is
	 * something seriously wrong.  Disable the expansion card
	 * interrupts so at least we can continue.
	 *
	 * Maybe we ought to start a timer to re-enable them some time
	 * later?
	 */
	if (last == jiffies) {
		lockup += 1;
		if (lockup > 1000000) {
			printk(KERN_ERR "\nInterrupt lockup detected - "
			       "disabling all expansion card interrupts\n");

			disable_irq(IRQ_EXPANSIONCARD);

			printk("Expansion card IRQ state:\n");

			for (ec = cards; ec; ec = ec->next)
				ecard_dump_irq_state(ec);
		}
	} else
		lockup = 0;

	/*
	 * If we did not recognise the source of this interrupt,
	 * warn the user, but don't flood the user with these messages.
	 */
	if (!last || time_after(jiffies, last + 5*HZ)) {
		last = jiffies;
		printk(KERN_WARNING "Unrecognised interrupt from backplane\n");
	}
}

static void
ecard_irq_noexpmask(int intr_no, void *dev_id, struct pt_regs *regs)
{
	ecard_t *ec;
	int called = 0;

	for (ec = cards; ec; ec = ec->next) {
		int pending;

		if (!ec->claimed || ec->irq == NO_IRQ || ec->slot_no == 8)
			continue;

		if (ec->ops && ec->ops->irqpending)
			pending = ec->ops->irqpending(ec);
		else
			pending = ecard_default_ops.irqpending(ec);

		if (pending) {
			do_ecard_IRQ(ec->irq, regs);
			called ++;
		}
	}
	cli();

	if (called == 0)
		ecard_check_lockup();
}

#ifdef HAS_EXPMASK
static unsigned char priority_masks[] =
{
	0xf0, 0xf1, 0xf3, 0xf7, 0xff, 0xff, 0xff, 0xff
};

static unsigned char first_set[] =
{
	0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
	0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00
};

static void
ecard_irq_expmask(int intr_no, void *dev_id, struct pt_regs *regs)
{
	const unsigned int statusmask = 15;
	unsigned int status;

	status = EXPMASK_STATUS & statusmask;
	if (status) {
		unsigned int slot;
		ecard_t *ec;
again:
		slot = first_set[status];
		ec = slot_to_ecard(slot);
		if (ec->claimed) {
			unsigned int oldexpmask;
			/*
			 * this ugly code is so that we can operate a
			 * prioritorising system:
			 *
			 * Card 0 	highest priority
			 * Card 1
			 * Card 2
			 * Card 3	lowest priority
			 *
			 * Serial cards should go in 0/1, ethernet/scsi in 2/3
			 * otherwise you will lose serial data at high speeds!
			 */
			oldexpmask = have_expmask;
			EXPMASK_ENABLE = (have_expmask &= priority_masks[slot]);
			sti();
			do_ecard_IRQ(ec->irq, regs);
			cli();
			EXPMASK_ENABLE = have_expmask = oldexpmask;
			status = EXPMASK_STATUS & statusmask;
			if (status)
				goto again;
		} else {
			printk(KERN_WARNING "card%d: interrupt from unclaimed "
			       "card???\n", slot);
			EXPMASK_ENABLE = (have_expmask &= ~(1 << slot));
		}
	} else
		printk(KERN_WARNING "Wild interrupt from backplane (masks)\n");
}

static void __init
ecard_probeirqhw(void)
{
	ecard_t *ec;
	int found;

	EXPMASK_ENABLE = 0x00;
	EXPMASK_STATUS = 0xff;
	found = ((EXPMASK_STATUS & 15) == 0);
	EXPMASK_ENABLE = 0xff;

	if (!found)
		return;

	printk(KERN_DEBUG "Expansion card interrupt "
	       "management hardware found\n");

	irqexpansioncard.handler = ecard_irq_expmask;

	/* for each card present, set a bit to '1' */
	have_expmask = 0x80000000;

	for (ec = cards; ec; ec = ec->next)
		have_expmask |= 1 << ec->slot_no;

	EXPMASK_ENABLE = have_expmask;
}
#else
#define ecard_probeirqhw()
#endif

#ifndef IO_EC_MEMC8_BASE
#define IO_EC_MEMC8_BASE 0
#endif

unsigned int ecard_address(ecard_t *ec, card_type_t type, card_speed_t speed)
{
	unsigned long address = 0;
	int slot = ec->slot_no;

	if (ec->slot_no == 8)
		return IO_EC_MEMC8_BASE;

	ectcr &= ~(1 << slot);

	switch (type) {
	case ECARD_MEMC:
		if (slot < 4)
			address = IO_EC_MEMC_BASE + (slot << 12);
		break;

	case ECARD_IOC:
		if (slot < 4)
			address = IO_EC_IOC_BASE + (slot << 12);
#ifdef IO_EC_IOC4_BASE
		else
			address = IO_EC_IOC4_BASE + ((slot - 4) << 12);
#endif
		if (address)
			address +=  speed << 17;
		break;

#ifdef IO_EC_EASI_BASE
	case ECARD_EASI:
		address = IO_EC_EASI_BASE + (slot << 22);
		if (speed == ECARD_FAST)
			ectcr |= 1 << slot;
		break;
#endif
	default:
		break;
	}

#ifdef IOMD_ECTCR
	outb(ectcr, IOMD_ECTCR);
#endif
	return address;
}

static int ecard_prints(char *buffer, ecard_t *ec)
{
	char *start = buffer;

	buffer += sprintf(buffer, "  %d: %s ", ec->slot_no,
			  ec->type == ECARD_EASI ? "EASI" : "    ");

	if (ec->cid.id == 0) {
		struct in_chunk_dir incd;

		buffer += sprintf(buffer, "[%04X:%04X] ",
			ec->cid.manufacturer, ec->cid.product);

		if (!ec->card_desc && ec->cid.cd &&
		    ecard_readchunk(&incd, ec, 0xf5, 0)) {
			ec->card_desc = kmalloc(strlen(incd.d.string)+1, GFP_KERNEL);

			if (ec->card_desc)
				strcpy((char *)ec->card_desc, incd.d.string);
		}

		buffer += sprintf(buffer, "%s\n", ec->card_desc ? ec->card_desc : "*unknown*");
	} else
		buffer += sprintf(buffer, "Simple card %d\n", ec->cid.id);

	return buffer - start;
}

static int get_ecard_dev_info(char *buf, char **start, off_t pos, int count)
{
	ecard_t *ec = cards;
	off_t at = 0;
	int len, cnt;

	cnt = 0;
	while (ec && count > cnt) {
		len = ecard_prints(buf, ec);
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
		ec = ec->next;
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry *proc_bus_ecard_dir = NULL;

static void ecard_proc_init(void)
{
	proc_bus_ecard_dir = proc_mkdir("ecard", proc_bus);
	create_proc_info_entry("devices", 0, proc_bus_ecard_dir,
		get_ecard_dev_info);
}

/*
 * Probe for an expansion card.
 *
 * If bit 1 of the first byte of the card is set, then the
 * card does not exist.
 */
static int __init
ecard_probe(int slot, card_type_t type)
{
	ecard_t **ecp;
	ecard_t *ec;
	struct ex_ecid cid;
	int i, rc = -ENOMEM;

	ec = kmalloc(sizeof(ecard_t), GFP_KERNEL);

	if (!ec)
		goto nodev;

	memset(ec, 0, sizeof(ecard_t));

	ec->slot_no	= slot;
	ec->type	= type;
	ec->irq		= NO_IRQ;
	ec->fiq		= NO_IRQ;
	ec->dma		= NO_DMA;
	ec->card_desc	= NULL;
	ec->ops		= &ecard_default_ops;

	rc = -ENODEV;
	if ((ec->podaddr = ecard_address(ec, type, ECARD_SYNC)) == 0)
		goto nodev;

	cid.r_zero = 1;
	ecard_readbytes(&cid, ec, 0, 16, 0);
	if (cid.r_zero)
		goto nodev;

	ec->cid.id	= cid.r_id;
	ec->cid.cd	= cid.r_cd;
	ec->cid.is	= cid.r_is;
	ec->cid.w	= cid.r_w;
	ec->cid.manufacturer = ecard_getu16(cid.r_manu);
	ec->cid.product = ecard_getu16(cid.r_prod);
	ec->cid.country = cid.r_country;
	ec->cid.irqmask = cid.r_irqmask;
	ec->cid.irqoff  = ecard_gets24(cid.r_irqoff);
	ec->cid.fiqmask = cid.r_fiqmask;
	ec->cid.fiqoff  = ecard_gets24(cid.r_fiqoff);
	ec->fiqaddr	=
	ec->irqaddr	= (unsigned char *)ioaddr(ec->podaddr);

	if (ec->cid.is) {
		ec->irqmask = ec->cid.irqmask;
		ec->irqaddr += ec->cid.irqoff;
		ec->fiqmask = ec->cid.fiqmask;
		ec->fiqaddr += ec->cid.fiqoff;
	} else {
		ec->irqmask = 1;
		ec->fiqmask = 4;
	}

	for (i = 0; i < sizeof(blacklist) / sizeof(*blacklist); i++)
		if (blacklist[i].manufacturer == ec->cid.manufacturer &&
		    blacklist[i].product == ec->cid.product) {
			ec->card_desc = blacklist[i].type;
			break;
		}

	ec->irq = 32 + slot;
#ifdef IO_EC_MEMC8_BASE
	if (slot == 8)
		ec->irq = 11;
#endif
#ifdef CONFIG_ARCH_RPC
	/* On RiscPC, only first two slots have DMA capability */
	if (slot < 2)
		ec->dma = 2 + slot;
#endif
#if 0	/* We don't support FIQs on expansion cards at the moment */
	ec->fiq = 96 + slot;
#endif

	rc = 0;

	for (ecp = &cards; *ecp; ecp = &(*ecp)->next);

	*ecp = ec;

nodev:
	if (rc && ec)
		kfree(ec);
	else
		slot_to_expcard[slot] = ec;

	return rc;
}

static ecard_t *finding_pos;

void ecard_startfind(void)
{
	finding_pos = NULL;
}

ecard_t *ecard_find(int cid, const card_ids *cids)
{
	if (!finding_pos)
		finding_pos = cards;
	else
		finding_pos = finding_pos->next;

	for (; finding_pos; finding_pos = finding_pos->next) {
		if (finding_pos->claimed)
			continue;

		if (!cids) {
			if ((finding_pos->cid.id ^ cid) == 0)
				break;
		} else {
			unsigned int manufacturer, product;
			int i;

			manufacturer = finding_pos->cid.manufacturer;
			product = finding_pos->cid.product;

			for (i = 0; cids[i].manufacturer != 65535; i++)
				if (manufacturer == cids[i].manufacturer &&
				    product == cids[i].product)
					break;

			if (cids[i].manufacturer != 65535)
				break;
		}
	}

	return finding_pos;
}

static void __init ecard_free_all(void)
{
	ecard_t *ec, *ecn;

	for (ec = cards; ec; ec = ecn) {
		ecn = ec->next;

		kfree(ec);
	}

	cards = NULL;

	memset(slot_to_expcard, 0, sizeof(slot_to_expcard));
}

/*
 * Initialise the expansion card system.
 * Locate all hardware - interrupt management and
 * actual cards.
 */
void __init ecard_init(void)
{
	int slot;

#ifdef CONFIG_CPU_32
	init_waitqueue_head(&ecard_wait);
	init_waitqueue_head(&ecard_done);
#endif

	printk("Probing expansion cards\n");

	for (slot = 0; slot < 8; slot ++) {
		if (ecard_probe(slot, ECARD_EASI) == -ENODEV)
			ecard_probe(slot, ECARD_IOC);
	}

#ifdef IO_EC_MEMC8_BASE
	ecard_probe(8, ECARD_IOC);
#endif

	ecard_probeirqhw();

	if (setup_arm_irq(IRQ_EXPANSIONCARD, &irqexpansioncard)) {
		printk(KERN_ERR "Unable to claim IRQ%d for expansion cards\n",
		       IRQ_EXPANSIONCARD);
		ecard_free_all();
	}

	ecard_proc_init();
}

EXPORT_SYMBOL(ecard_startfind);
EXPORT_SYMBOL(ecard_find);
EXPORT_SYMBOL(ecard_readchunk);
EXPORT_SYMBOL(ecard_address);
