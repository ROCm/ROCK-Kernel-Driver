/*
 *  linux/drivers/mmc/mmci.c - ARM PrimeCell MMCI PL180/1 driver
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware/amba.h>
#include <asm/mach/mmc.h>

#include "mmci.h"

#define DRIVER_NAME "mmci-pl18x"

#ifdef CONFIG_MMC_DEBUG
#define DBG(x...)	pr_debug(x)
#else
#define DBG(x...)	do { } while (0)
#endif

static unsigned int fmax = 515633;

static void
mmci_request_end(struct mmci_host *host, struct mmc_request *req)
{
	writel(0, host->base + MMCICOMMAND);
	host->req = NULL;
	host->cmd = NULL;
	host->data = NULL;
	host->buffer = NULL;

	if (req->data)
		req->data->bytes_xfered = host->data_xfered;

	/*
	 * Need to drop the host lock here; mmc_request_done may call
	 * back into the driver...
	 */
	spin_unlock(&host->lock);
	mmc_request_done(host->mmc, req);
	spin_lock(&host->lock);
}

static void mmci_start_data(struct mmci_host *host, struct mmc_data *data)
{
	unsigned int datactrl;

	DBG("%s: data: blksz %04x blks %04x flags %08x\n",
	    host->mmc->host_name, 1 << data->blksz_bits, data->blocks,
	    data->flags);

	datactrl = MCI_DPSM_ENABLE | data->blksz_bits << 4;

	if (data->flags & MMC_DATA_READ)
		datactrl |= MCI_DPSM_DIRECTION;

	host->data = data;
	host->buffer = data->rq->buffer;
	host->size = data->blocks << data->blksz_bits;
	host->data_xfered = 0;

	writel(0x800000, host->base + MMCIDATATIMER);
	writel(host->size, host->base + MMCIDATALENGTH);
	writel(datactrl, host->base + MMCIDATACTRL);
}

static void
mmci_start_command(struct mmci_host *host, struct mmc_command *cmd, u32 c)
{
	DBG("%s: cmd: op %02x arg %08x flags %08x\n",
	    host->mmc->host_name, cmd->opcode, cmd->arg, cmd->flags);

	if (readl(host->base + MMCICOMMAND) & MCI_CPSM_ENABLE) {
		writel(0, host->base + MMCICOMMAND);
		udelay(1);
	}

	c |= cmd->opcode | MCI_CPSM_ENABLE;
	switch (cmd->flags & MMC_RSP_MASK) {
	case MMC_RSP_NONE:
	default:
		break;
	case MMC_RSP_LONG:
		c |= MCI_CPSM_LONGRSP;
	case MMC_RSP_SHORT:
		c |= MCI_CPSM_RESPONSE;
		break;
	}
	if (/*interrupt*/0)
		c |= MCI_CPSM_INTERRUPT;

	host->cmd = cmd;

	writel(cmd->arg, host->base + MMCIARGUMENT);
	writel(c, host->base + MMCICOMMAND);
}

static void
mmci_data_irq(struct mmci_host *host, struct mmc_data *data,
	      unsigned int status)
{
	if (status & MCI_DATABLOCKEND) {
		host->data_xfered += 1 << data->blksz_bits;
	}
	if (status & (MCI_DATACRCFAIL|MCI_DATATIMEOUT|MCI_TXUNDERRUN|MCI_RXOVERRUN)) {
		if (status & MCI_DATACRCFAIL)
			data->error = MMC_ERR_BADCRC;
		else if (status & MCI_DATATIMEOUT)
			data->error = MMC_ERR_TIMEOUT;
		else if (status & (MCI_TXUNDERRUN|MCI_RXOVERRUN))
			data->error = MMC_ERR_FIFO;
		status |= MCI_DATAEND;
	}
	if (status & MCI_DATAEND) {
		host->data = NULL;
		if (!data->stop) {
			mmci_request_end(host, data->req);
		} else /*if (readl(host->base + MMCIDATACNT) > 6)*/ {
			mmci_start_command(host, data->stop, 0);
		}
	}
}

static void
mmci_cmd_irq(struct mmci_host *host, struct mmc_command *cmd,
	     unsigned int status)
{
	host->cmd = NULL;

	cmd->resp[0] = readl(host->base + MMCIRESPONSE0);
	cmd->resp[1] = readl(host->base + MMCIRESPONSE1);
	cmd->resp[2] = readl(host->base + MMCIRESPONSE2);
	cmd->resp[3] = readl(host->base + MMCIRESPONSE3);

	if (status & MCI_CMDTIMEOUT) {
		cmd->error = MMC_ERR_TIMEOUT;
	} else if (status & MCI_CMDCRCFAIL && cmd->flags & MMC_RSP_CRC) {
		cmd->error = MMC_ERR_BADCRC;
	}

	if (!cmd->data || cmd->error != MMC_ERR_NONE) {
		mmci_request_end(host, cmd->req);
	} else if (!(cmd->data->flags & MMC_DATA_READ)) {
		mmci_start_data(host, cmd->data);
	}
}

static irqreturn_t mmci_pio_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mmci_host *host = dev_id;
	u32 status;
	int ret = 0;

	do {
		status = readl(host->base + MMCISTATUS);

		if (!(status & (MCI_RXDATAAVLBL|MCI_RXFIFOHALFFULL|
				MCI_TXFIFOEMPTY|MCI_TXFIFOHALFEMPTY)))
			break;

		DBG("%s: irq1 %08x\n", host->mmc->host_name, status);

		if (status & (MCI_RXDATAAVLBL|MCI_RXFIFOHALFFULL)) {
			int count = host->size - (readl(host->base + MMCIFIFOCNT) << 2);
			if (count < 0)
				count = 0;
			if (count && host->buffer) {
				readsl(host->base + MMCIFIFO, host->buffer, count >> 2);
				host->buffer += count;
				host->size -= count;
				if (host->size == 0)
					host->buffer = NULL;
			} else {
				static int first = 1;
				if (first) {
					first = 0;
					printk(KERN_ERR "MMCI: sinking excessive data\n");
				}
				readl(host->base + MMCIFIFO);
			}
		}
		if (status & (MCI_TXFIFOEMPTY|MCI_TXFIFOHALFEMPTY)) {
			int count = host->size;
			if (count > MCI_FIFOHALFSIZE)
				count = MCI_FIFOHALFSIZE;
			if (count && host->buffer) {
				writesl(host->base + MMCIFIFO, host->buffer, count >> 2);
				host->buffer += count;
				host->size -= count;
				if (host->size == 0)
					host->buffer = NULL;
			} else {
				static int first = 1;
				if (first) {
					first = 0;
					printk(KERN_ERR "MMCI: ran out of source data\n");
				}
			}
		}
		ret = 1;
	} while (status);

	return IRQ_RETVAL(ret);
}

static irqreturn_t mmci_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mmci_host *host = dev_id;
	u32 status;
	int ret = 0;

	spin_lock(&host->lock);

	do {
		struct mmc_command *cmd;
		struct mmc_data *data;

		status = readl(host->base + MMCISTATUS);
		writel(status, host->base + MMCICLEAR);

		if (!(status & MCI_IRQMASK))
			break;

		DBG("%s: irq0 %08x\n", host->mmc->host_name, status);

		data = host->data;
		if (status & (MCI_DATACRCFAIL|MCI_DATATIMEOUT|MCI_TXUNDERRUN|
			      MCI_RXOVERRUN|MCI_DATAEND|MCI_DATABLOCKEND))
			mmci_data_irq(host, data, status);

		cmd = host->cmd;
		if (status & (MCI_CMDCRCFAIL|MCI_CMDTIMEOUT|MCI_CMDSENT|MCI_CMDRESPEND) && cmd)
			mmci_cmd_irq(host, cmd, status);

		ret = 1;
	} while (status);

	spin_unlock(&host->lock);

	return IRQ_RETVAL(ret);
}

static void mmci_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mmci_host *host = mmc_priv(mmc);

	WARN_ON(host->req != NULL);

	spin_lock_irq(&host->lock);

	host->req = req;

	if (req->data && req->data->flags & MMC_DATA_READ)
		mmci_start_data(host, req->data);

	mmci_start_command(host, req->cmd, 0);

	spin_unlock_irq(&host->lock);
}

static void mmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmci_host *host = mmc_priv(mmc);
	u32 clk = 0, pwr = 0;

	DBG("%s: set_ios: clock %dHz busmode %d powermode %d Vdd %d.%02d\n",
	    mmc->host_name, ios->clock, ios->bus_mode, ios->power_mode,
	    ios->vdd / 100, ios->vdd % 100);

	if (ios->clock) {
		clk = host->mclk / (2 * ios->clock) - 1;
		if (clk > 256)
			clk = 255;
		clk |= MCI_CLK_ENABLE;
	}

	if (host->plat->translate_vdd)
		pwr |= host->plat->translate_vdd(mmc_dev(mmc), ios->vdd);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		break;
	case MMC_POWER_UP:
		pwr |= MCI_PWR_UP;
		break;
	case MMC_POWER_ON:
		pwr |= MCI_PWR_ON;
		break;
	}

	if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN)
		pwr |= MCI_ROD;

	writel(clk, host->base + MMCICLOCK);

	if (host->pwr != pwr) {
		host->pwr = pwr;
		writel(pwr, host->base + MMCIPOWER);
	}
}

static struct mmc_host_ops mmci_ops = {
	.request	= mmci_request,
	.set_ios	= mmci_set_ios,
};

static void mmci_check_status(unsigned long data)
{
	struct mmci_host *host = (struct mmci_host *)data;
	unsigned int status;

	status = host->plat->status(mmc_dev(host->mmc));
	if (status ^ host->oldstat)
		mmc_detect_change(host->mmc);

	host->oldstat = status;
	mod_timer(&host->timer, jiffies + HZ);
}

static int mmci_probe(struct amba_device *dev, void *id)
{
	struct mmc_platform_data *plat = dev->dev.platform_data;
	struct mmci_host *host = NULL;
	struct mmc_host *mmc;
	int ret;

	/* must have platform data */
	if (!plat)
		return -EINVAL;

	ret = amba_request_regions(dev, DRIVER_NAME);
	if (ret)
		return ret;

	mmc = mmc_alloc_host(sizeof(struct mmci_host), &dev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto out;
	}

	mmc->ops = &mmci_ops;
	mmc->f_min = (plat->mclk + 511) / 512;
	mmc->f_max = max(plat->mclk / 2, fmax);
	mmc->ocr_avail = plat->ocr_mask;

	host = mmc_priv(mmc);
	host->plat = plat;
	host->mclk = plat->mclk;
	host->mmc = mmc;
	host->base = ioremap(dev->res.start, SZ_4K);
	if (!host->base) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock_init(&host->lock);

	writel(0, host->base + MMCIMASK0);
	writel(0, host->base + MMCIMASK1);
	writel(0xfff, host->base + MMCICLEAR);

	ret = request_irq(dev->irq[0], mmci_irq, SA_SHIRQ, DRIVER_NAME " (cmd)", host);
	if (ret)
		goto out;

	ret = request_irq(dev->irq[1], mmci_pio_irq, SA_SHIRQ, DRIVER_NAME " (pio)", host);
	if (ret) {
		free_irq(dev->irq[0], host);
		goto out;
	}

	writel(MCI_IRQENABLE, host->base + MMCIMASK0);
	writel(MCI_TXFIFOHALFEMPTYMASK|MCI_RXFIFOHALFFULLMASK, host->base + MMCIMASK1);

	amba_set_drvdata(dev, mmc);

	mmc_add_host(mmc);

	printk(KERN_INFO "%s: MMCI rev %x cfg %02x at 0x%08lx irq %d,%d\n",
		mmc->host_name, amba_rev(dev), amba_config(dev),
		dev->res.start, dev->irq[0], dev->irq[1]);

	init_timer(&host->timer);
	host->timer.data = (unsigned long)host;
	host->timer.function = mmci_check_status;
	host->timer.expires = jiffies + HZ;
	add_timer(&host->timer);

	return 0;

 out:
	if (host && host->base)
		iounmap(host->base);
	if (mmc)
		mmc_free_host(mmc);
	amba_release_regions(dev);
	return ret;
}

static int mmci_remove(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		del_timer_sync(&host->timer);

		mmc_remove_host(mmc);

		writel(0, host->base + MMCIMASK0);
		writel(0, host->base + MMCIMASK1);

		writel(0, host->base + MMCICOMMAND);
		writel(0, host->base + MMCIDATACTRL);

		free_irq(dev->irq[0], host);
		free_irq(dev->irq[1], host);

		iounmap(host->base);

		mmc_free_host(mmc);

		amba_release_regions(dev);
	}

	return 0;
}

#ifdef CONFIG_PM
static int mmci_suspend(struct amba_device *dev, u32 state)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		ret = mmc_suspend_host(mmc, state);
		if (ret == 0)
			writel(0, host->base + MMCIMASK0);
	}

	return ret;
}

static int mmci_resume(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		writel(MCI_IRQENABLE, host->base + MMCIMASK0);

		ret = mmc_resume_host(mmc);
	}

	return ret;
}
#else
#define mmci_suspend	NULL
#define mmci_resume	NULL
#endif

static struct amba_id mmci_ids[] = {
	{
		.id	= 0x00041180,
		.mask	= 0x000fffff,
	},
	{
		.id	= 0x00041181,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver mmci_driver = {
	.drv		= {
		.name	= DRIVER_NAME,
	},
	.probe		= mmci_probe,
	.remove		= mmci_remove,
	.suspend	= mmci_suspend,
	.resume		= mmci_resume,
	.id_table	= mmci_ids,
};

static int __init mmci_init(void)
{
	return amba_driver_register(&mmci_driver);
}

static void __exit mmci_exit(void)
{
	amba_driver_unregister(&mmci_driver);
}

module_init(mmci_init);
module_exit(mmci_exit);
module_param(fmax, uint, 0444);

MODULE_DESCRIPTION("ARM PrimeCell PL180/181 Multimedia Card Interface driver");
MODULE_LICENSE("GPL");
