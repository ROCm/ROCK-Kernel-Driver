/*
 *  linux/drivers/mmc/mmc.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>

#include "mmc.h"

#ifdef CONFIG_MMC_DEBUG
#define DBG(x...)	printk(KERN_DEBUG x)
#else
#define DBG(x...)	do { } while (0)
#endif

#define CMD_RETRIES	3

/*
 * OCR Bit positions to 10s of Vdd mV.
 */
static const unsigned short mmc_ocr_bit_to_vdd[] = {
	150,	155,	160,	165,	170,	180,	190,	200,
	210,	220,	230,	240,	250,	260,	270,	280,
	290,	300,	310,	320,	330,	340,	350,	360
};

static const unsigned int tran_exp[] = {
	10000,		100000,		1000000,	10000000,
	0,		0,		0,		0
};

static const unsigned char tran_mant[] = {
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};

static const unsigned int tacc_exp[] = {
	1,	10,	100,	1000,	10000,	100000,	1000000, 10000000,
};

static const unsigned int tacc_mant[] = {
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};


/**
 *	mmc_request_done - finish processing an MMC command
 *	@host: MMC host which completed command
 *	@cmd: MMC command which completed
 *	@err: MMC error code
 *
 *	MMC drivers should call this function when they have completed
 *	their processing of a command.  This should be called before the
 *	data part of the command has completed.
 */
void mmc_request_done(struct mmc_host *host, struct mmc_request *req)
{
	struct mmc_command *cmd = req->cmd;
	int err = req->cmd->error;
	DBG("MMC: req done (%02x): %d: %08x %08x %08x %08x\n", cmd->opcode,
	    err, cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3]);

	if (err && cmd->retries) {
		cmd->retries--;
		cmd->error = 0;
		host->ops->request(host, req);
	} else if (req->done) {
		req->done(req);
	}
}

EXPORT_SYMBOL(mmc_request_done);

/**
 *	mmc_start_request - start a command on a host
 *	@host: MMC host to start command on
 *	@cmd: MMC command to start
 *
 *	Queue a command on the specified host.  We expect the
 *	caller to be holding the host lock with interrupts disabled.
 */
void
mmc_start_request(struct mmc_host *host, struct mmc_request *req)
{
	DBG("MMC: starting cmd %02x arg %08x flags %08x\n",
	    req->cmd->opcode, req->cmd->arg, req->cmd->flags);

	WARN_ON(host->card_busy == NULL);

	req->cmd->error = 0;
	req->cmd->req = req;
	if (req->data) {
		req->cmd->data = req->data;
		req->data->error = 0;
		req->data->req = req;
		if (req->stop) {
			req->data->stop = req->stop;
			req->stop->error = 0;
			req->stop->req = req;
		}
	}
	host->ops->request(host, req);
}

EXPORT_SYMBOL(mmc_start_request);

static void mmc_wait_done(struct mmc_request *req)
{
	complete(req->done_data);
}

int mmc_wait_for_req(struct mmc_host *host, struct mmc_request *req)
{
	DECLARE_COMPLETION(complete);

	req->done_data = &complete;
	req->done = mmc_wait_done;

	mmc_start_request(host, req);

	wait_for_completion(&complete);

	return 0;
}

EXPORT_SYMBOL(mmc_wait_for_req);

/**
 *	mmc_wait_for_cmd - start a command and wait for completion
 *	@host: MMC host to start command
 *	@cmd: MMC command to start
 *	@retries: maximum number of retries
 *
 *	Start a new MMC command for a host, and wait for the command
 *	to complete.  Return any error that occurred while the command
 *	was executing.  Do not attempt to parse the response.
 */
int mmc_wait_for_cmd(struct mmc_host *host, struct mmc_command *cmd, int retries)
{
	struct mmc_request req;

	BUG_ON(host->card_busy == NULL);

	memset(&req, 0, sizeof(struct mmc_request));

	memset(cmd->resp, 0, sizeof(cmd->resp));
	cmd->retries = retries;

	req.cmd = cmd;
	cmd->data = NULL;

	mmc_wait_for_req(host, &req);

	return cmd->error;
}

EXPORT_SYMBOL(mmc_wait_for_cmd);



/**
 *	__mmc_claim_host - exclusively claim a host
 *	@host: mmc host to claim
 *	@card: mmc card to claim host for
 *
 *	Claim a host for a set of operations.  If a valid card
 *	is passed and this wasn't the last card selected, select
 *	the card before returning.
 *
 *	Note: you should use mmc_card_claim_host or mmc_claim_host.
 */
int __mmc_claim_host(struct mmc_host *host, struct mmc_card *card)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int err = 0;

	add_wait_queue(&host->wq, &wait);
	spin_lock_irqsave(&host->lock, flags);
	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (host->card_busy == NULL)
			break;
		spin_unlock_irqrestore(&host->lock, flags);
		schedule();
		spin_lock_irqsave(&host->lock, flags);
	}
	set_current_state(TASK_RUNNING);
	host->card_busy = card;
	spin_unlock_irqrestore(&host->lock, flags);
	remove_wait_queue(&host->wq, &wait);

	if (card != (void *)-1 && host->card_selected != card) {
		struct mmc_command cmd;

		host->card_selected = card;

		cmd.opcode = MMC_SELECT_CARD;
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_SHORT | MMC_RSP_CRC;

		err = mmc_wait_for_cmd(host, &cmd, CMD_RETRIES);
	}

	return err;
}

EXPORT_SYMBOL(__mmc_claim_host);

/**
 *	mmc_release_host - release a host
 *	@host: mmc host to release
 *
 *	Release a MMC host, allowing others to claim the host
 *	for their operations.
 */
void mmc_release_host(struct mmc_host *host)
{
	unsigned long flags;

	BUG_ON(host->card_busy == NULL);

	spin_lock_irqsave(&host->lock, flags);
	host->card_busy = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	wake_up(&host->wq);
}

EXPORT_SYMBOL(mmc_release_host);

/*
 * Ensure that no card is selected.
 */
static void mmc_deselect_cards(struct mmc_host *host)
{
	struct mmc_command cmd;

	if (host->card_selected) {
		host->card_selected = NULL;

		cmd.opcode = MMC_SELECT_CARD;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE;

		mmc_wait_for_cmd(host, &cmd, 0);
	}
}


static inline void mmc_delay(unsigned int ms)
{
	if (ms < HZ / 1000) {
		yield();
		mdelay(ms);
	} else {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(ms * HZ / 1000);
	}
}

/*
 * Mask off any voltages we don't support and select
 * the lowest voltage
 */
static u32 mmc_select_voltage(struct mmc_host *host, u32 ocr)
{
	int bit;

	ocr &= host->ocr_avail;

	bit = ffs(ocr);
	if (bit) {
		bit -= 1;

		ocr = 3 << bit;

		host->ios.vdd = bit;
		host->ops->set_ios(host, &host->ios);
	} else {
		ocr = 0;
	}

	return ocr;
}

static void mmc_decode_cid(struct mmc_cid *cid, u32 *resp)
{
	memset(cid, 0, sizeof(struct mmc_cid));

	cid->manfid	  = resp[0] >> 8;
	cid->prod_name[0] = resp[0];
	cid->prod_name[1] = resp[1] >> 24;
	cid->prod_name[2] = resp[1] >> 16;
	cid->prod_name[3] = resp[1] >> 8;
	cid->prod_name[4] = resp[1];
	cid->prod_name[5] = resp[2] >> 24;
	cid->prod_name[6] = resp[2] >> 16;
	cid->prod_name[7] = '\0';
	cid->hwrev	  = (resp[2] >> 12) & 15;
	cid->fwrev	  = (resp[2] >> 8) & 15;
	cid->serial	  = (resp[2] & 255) << 16 | (resp[3] >> 16);
	cid->month	  = (resp[3] >> 12) & 15;
	cid->year	  = (resp[3] >> 8) & 15;
}

static void mmc_decode_csd(struct mmc_csd *csd, u32 *resp)
{
	unsigned int e, m;

	csd->mmc_prot	 = (resp[0] >> 26) & 15;
	m = (resp[0] >> 19) & 15;
	e = (resp[0] >> 16) & 7;
	csd->tacc_ns	 = (tacc_exp[e] * tacc_mant[m] + 9) / 10;
	csd->tacc_clks	 = ((resp[0] >> 8) & 255) * 100;

	m = (resp[0] >> 3) & 15;
	e = resp[0] & 7;
	csd->max_dtr	  = tran_exp[e] * tran_mant[m];
	csd->cmdclass	  = (resp[1] >> 20) & 0xfff;

	e = (resp[2] >> 15) & 7;
	m = (resp[1] << 2 | resp[2] >> 30) & 0x3fff;
	csd->capacity	  = (1 + m) << (e + 2);

	csd->read_blkbits = (resp[1] >> 16) & 15;
}

/*
 * Locate a MMC card on this MMC host given a CID.
 */
static struct mmc_card *
mmc_find_card(struct mmc_host *host, struct mmc_cid *cid)
{
	struct mmc_card *card;

	list_for_each_entry(card, &host->cards, node) {
		if (memcmp(&card->cid, cid, sizeof(struct mmc_cid)) == 0)
			return card;
	}
	return NULL;
}

/*
 * Allocate a new MMC card, and assign a unique RCA.
 */
static struct mmc_card *
mmc_alloc_card(struct mmc_host *host, struct mmc_cid *cid, unsigned int *frca)
{
	struct mmc_card *card, *c;
	unsigned int rca = *frca;

	card = kmalloc(sizeof(struct mmc_card), GFP_KERNEL);
	if (!card)
		return ERR_PTR(-ENOMEM);

	mmc_init_card(card, host);
	memcpy(&card->cid, cid, sizeof(struct mmc_cid));

 again:
	list_for_each_entry(c, &host->cards, node)
		if (c->rca == rca) {
			rca++;
			goto again;
		}

	card->rca = rca;

	*frca = rca;

	return card;
}

/*
 * Apply power to the MMC stack.
 */
static void mmc_power_up(struct mmc_host *host)
{
	struct mmc_command cmd;
	int bit = fls(host->ocr_avail) - 1;

	host->ios.vdd = bit;
	host->ios.bus_mode = MMC_BUSMODE_OPENDRAIN;
	host->ios.power_mode = MMC_POWER_UP;
	host->ops->set_ios(host, &host->ios);

	mmc_delay(1);

	host->ios.clock = host->f_min;
	host->ios.power_mode = MMC_POWER_ON;
	host->ops->set_ios(host, &host->ios);

	mmc_delay(2);

	cmd.opcode = MMC_GO_IDLE_STATE;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_NONE;

	mmc_wait_for_cmd(host, &cmd, 0);
}

static void mmc_power_off(struct mmc_host *host)
{
	host->ios.clock = 0;
	host->ios.vdd = 0;
	host->ios.bus_mode = MMC_BUSMODE_OPENDRAIN;
	host->ios.power_mode = MMC_POWER_OFF;
	host->ops->set_ios(host, &host->ios);
}

static int mmc_send_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr)
{
	struct mmc_command cmd;
	int i, err = 0;

	cmd.opcode = MMC_SEND_OP_COND;
	cmd.arg = ocr;
	cmd.flags = MMC_RSP_SHORT;

	for (i = 100; i; i--) {
		err = mmc_wait_for_cmd(host, &cmd, 0);
		if (err != MMC_ERR_NONE)
			break;

		if (cmd.resp[0] & MMC_CARD_BUSY || ocr == 0)
			break;

		err = MMC_ERR_TIMEOUT;
	}

	if (rocr)
		*rocr = cmd.resp[0];

	return err;
}

/*
 * Discover cards by requesting their CID.  If this command
 * times out, it is not an error; there are no further cards
 * to be discovered.  Add new cards to the list.
 *
 * Create a mmc_card entry for each discovered card, assigning
 * it an RCA, and save the CID.
 */
static void mmc_discover_cards(struct mmc_host *host)
{
	struct mmc_card *card;
	unsigned int first_rca = 1, err;

	while (1) {
		struct mmc_command cmd;
		struct mmc_cid cid;

		cmd.opcode = MMC_ALL_SEND_CID;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_LONG | MMC_RSP_CRC;

		err = mmc_wait_for_cmd(host, &cmd, CMD_RETRIES);
		if (err == MMC_ERR_TIMEOUT) {
			err = MMC_ERR_NONE;
			break;
		}
		if (err != MMC_ERR_NONE) {
			printk(KERN_ERR "%s: error requesting CID: %d\n",
				host->host_name, err);
			break;
		}

		mmc_decode_cid(&cid, cmd.resp);

		card = mmc_find_card(host, &cid);
		if (!card) {
			card = mmc_alloc_card(host, &cid, &first_rca);
			if (IS_ERR(card)) {
				err = PTR_ERR(card);
				break;
			}
			list_add(&card->node, &host->cards);
		}

		card->state &= ~MMC_STATE_DEAD;

		cmd.opcode = MMC_SET_RELATIVE_ADDR;
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_SHORT | MMC_RSP_CRC;

		err = mmc_wait_for_cmd(host, &cmd, CMD_RETRIES);
		if (err != MMC_ERR_NONE)
			card->state |= MMC_STATE_DEAD;
	}
}

static void mmc_read_csds(struct mmc_host *host)
{
	struct mmc_card *card;

	list_for_each_entry(card, &host->cards, node) {
		struct mmc_command cmd;
		int err;

		if (card->state & (MMC_STATE_DEAD|MMC_STATE_PRESENT))
			continue;

		cmd.opcode = MMC_SEND_CSD;
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_LONG | MMC_RSP_CRC;

		err = mmc_wait_for_cmd(host, &cmd, CMD_RETRIES);
		if (err != MMC_ERR_NONE) {
			card->state |= MMC_STATE_DEAD;
			continue;
		}

		mmc_decode_csd(&card->csd, cmd.resp);
	}
}

static unsigned int mmc_calculate_clock(struct mmc_host *host)
{
	struct mmc_card *card;
	unsigned int max_dtr = host->f_max;

	list_for_each_entry(card, &host->cards, node)
		if (!mmc_card_dead(card) && max_dtr > card->csd.max_dtr)
			max_dtr = card->csd.max_dtr;

	DBG("MMC: selected %d.%03dMHz transfer rate\n",
	    max_dtr / 1000000, (max_dtr / 1000) % 1000);

	return max_dtr;
}

/*
 * Check whether cards we already know about are still present.
 * We do this by requesting status, and checking whether a card
 * responds.
 *
 * A request for status does not cause a state change in data
 * transfer mode.
 */
static void mmc_check_cards(struct mmc_host *host)
{
	struct list_head *l, *n;

	mmc_deselect_cards(host);

	list_for_each_safe(l, n, &host->cards) {
		struct mmc_card *card = mmc_list_to_card(l);
		struct mmc_command cmd;
		int err;

		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_SHORT | MMC_RSP_CRC;

		err = mmc_wait_for_cmd(host, &cmd, CMD_RETRIES);
		if (err == MMC_ERR_NONE)
			continue;

		card->state |= MMC_STATE_DEAD;
	}
}

static void mmc_setup(struct mmc_host *host)
{
	if (host->ios.power_mode != MMC_POWER_ON) {
		int err;
		u32 ocr;

		mmc_power_up(host);

		err = mmc_send_op_cond(host, 0, &ocr);
		if (err != MMC_ERR_NONE)
			return;

		host->ocr = mmc_select_voltage(host, ocr);
	} else {
		host->ios.bus_mode = MMC_BUSMODE_OPENDRAIN;
		host->ios.clock = host->f_min;
		host->ops->set_ios(host, &host->ios);

		/*
		 * We should remember the OCR mask from the existing
		 * cards, and detect the new cards OCR mask, combine
		 * the two and re-select the VDD.  However, if we do
		 * change VDD, we should do an idle, and then do a
		 * full re-initialisation.  We would need to notify
		 * drivers so that they can re-setup the cards as
		 * well, while keeping their queues at bay.
		 *
		 * For the moment, we take the easy way out - if the
		 * new cards don't like our currently selected VDD,
		 * they drop off the bus.
		 */
	}

	if (host->ocr == 0)
		return;

	/*
	 * Send the selected OCR multiple times... until the cards
	 * all get the idea that they should be ready for CMD2.
	 * (My SanDisk card seems to need this.)
	 */
	mmc_send_op_cond(host, host->ocr, NULL);

	mmc_discover_cards(host);

	/*
	 * Ok, now switch to push-pull mode.
	 */
	host->ios.bus_mode = MMC_BUSMODE_PUSHPULL;
	host->ops->set_ios(host, &host->ios);

	mmc_read_csds(host);
}


/**
 *	mmc_detect_change - process change of state on a MMC socket
 *	@host: host which changed state.
 *
 *	All we know is that card(s) have been inserted or removed
 *	from the socket(s).  We don't know which socket or cards.
 */
void mmc_detect_change(struct mmc_host *host)
{
	schedule_work(&host->detect);
}

EXPORT_SYMBOL(mmc_detect_change);


static void mmc_rescan(void *data)
{
	struct mmc_host *host = data;
	struct list_head *l, *n;

	mmc_claim_host(host);

	if (host->ios.power_mode == MMC_POWER_ON)
		mmc_check_cards(host);

	mmc_setup(host);

	if (!list_empty(&host->cards)) {
		/*
		 * (Re-)calculate the fastest clock rate which the
		 * attached cards and the host support.
		 */
		host->ios.clock = mmc_calculate_clock(host);
		host->ops->set_ios(host, &host->ios);
	}

	mmc_release_host(host);

	list_for_each_safe(l, n, &host->cards) {
		struct mmc_card *card = mmc_list_to_card(l);

		/*
		 * If this is a new and good card, register it.
		 */
		if (!mmc_card_present(card) && !mmc_card_dead(card)) {
			if (mmc_register_card(card))
				card->state |= MMC_STATE_DEAD;
			else
				card->state |= MMC_STATE_PRESENT;
		}

		/*
		 * If this card is dead, destroy it.
		 */
		if (mmc_card_dead(card)) {
			list_del(&card->node);
			mmc_remove_card(card);
		}
	}

	/*
	 * If we discover that there are no cards on the
	 * bus, turn off the clock and power down.
	 */
	if (list_empty(&host->cards))
		mmc_power_off(host);
}


/**
 *	mmc_alloc_host - initialise the per-host structure.
 *	@extra: sizeof private data structure
 *	@dev: pointer to host device model structure
 *
 *	Initialise the per-host structure.
 */
struct mmc_host *mmc_alloc_host(int extra, struct device *dev)
{
	struct mmc_host *host;

	host = kmalloc(sizeof(struct mmc_host) + extra, GFP_KERNEL);
	if (host) {
		memset(host, 0, sizeof(struct mmc_host) + extra);

		host->priv = host + 1;

		spin_lock_init(&host->lock);
		init_waitqueue_head(&host->wq);
		INIT_LIST_HEAD(&host->cards);
		INIT_WORK(&host->detect, mmc_rescan, host);

		host->dev = dev;
	}

	return host;
}

EXPORT_SYMBOL(mmc_alloc_host);

/**
 *	mmc_add_host - initialise host hardware
 *	@host: mmc host
 */
int mmc_add_host(struct mmc_host *host)
{
	static unsigned int host_num;

	snprintf(host->host_name, sizeof(host->host_name),
		 "mmc%d", host_num++);

	mmc_power_off(host);
	mmc_detect_change(host);

	return 0;
}

EXPORT_SYMBOL(mmc_add_host);

/**
 *	mmc_remove_host - remove host hardware
 *	@host: mmc host
 *
 *	Unregister and remove all cards associated with this host,
 *	and power down the MMC bus.
 */
void mmc_remove_host(struct mmc_host *host)
{
	struct list_head *l, *n;

	list_for_each_safe(l, n, &host->cards) {
		struct mmc_card *card = mmc_list_to_card(l);

		mmc_remove_card(card);
	}

	mmc_power_off(host);
}

EXPORT_SYMBOL(mmc_remove_host);

/**
 *	mmc_free_host - free the host structure
 *	@host: mmc host
 *
 *	Free the host once all references to it have been dropped.
 */
void mmc_free_host(struct mmc_host *host)
{
	flush_scheduled_work();
	kfree(host);
}

EXPORT_SYMBOL(mmc_free_host);

#ifdef CONFIG_PM

/**
 *	mmc_suspend_host - suspend a host
 *	@host: mmc host
 *	@state: suspend mode (PM_SUSPEND_xxx)
 */
int mmc_suspend_host(struct mmc_host *host, u32 state)
{
	mmc_claim_host(host);
	mmc_deselect_cards(host);
	mmc_power_off(host);
	mmc_release_host(host);

	return 0;
}

EXPORT_SYMBOL(mmc_suspend_host);

/**
 *	mmc_resume_host - resume a previously suspended host
 *	@host: mmc host
 */
int mmc_resume_host(struct mmc_host *host)
{
	mmc_detect_change(host);

	return 0;
}

EXPORT_SYMBOL(mmc_resume_host);

#endif

MODULE_LICENSE("GPL");
