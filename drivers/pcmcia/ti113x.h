/*
 * ti113x.h 1.16 1999/10/25 20:03:34
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_TI113X_H
#define _LINUX_TI113X_H

#include <linux/config.h>

/* Register definitions for TI 113X PCI-to-CardBus bridges */

/* System Control Register */
#define TI113X_SYSTEM_CONTROL		0x0080	/* 32 bit */
#define  TI113X_SCR_SMIROUTE		0x04000000
#define  TI113X_SCR_SMISTATUS		0x02000000
#define  TI113X_SCR_SMIENB		0x01000000
#define  TI113X_SCR_VCCPROT		0x00200000
#define  TI113X_SCR_REDUCEZV		0x00100000
#define  TI113X_SCR_CDREQEN		0x00080000
#define  TI113X_SCR_CDMACHAN		0x00070000
#define  TI113X_SCR_SOCACTIVE		0x00002000
#define  TI113X_SCR_PWRSTREAM		0x00000800
#define  TI113X_SCR_DELAYUP		0x00000400
#define  TI113X_SCR_DELAYDOWN		0x00000200
#define  TI113X_SCR_INTERROGATE		0x00000100
#define  TI113X_SCR_CLKRUN_SEL		0x00000080
#define  TI113X_SCR_PWRSAVINGS		0x00000040
#define  TI113X_SCR_SUBSYSRW		0x00000020
#define  TI113X_SCR_CB_DPAR		0x00000010
#define  TI113X_SCR_CDMA_EN		0x00000008
#define  TI113X_SCR_ASYNC_IRQ		0x00000004
#define  TI113X_SCR_KEEPCLK		0x00000002
#define  TI113X_SCR_CLKRUN_ENA		0x00000001  

#define  TI122X_SCR_SER_STEP		0xc0000000
#define  TI122X_SCR_INTRTIE		0x20000000
#define  TI122X_SCR_CBRSVD		0x00400000
#define  TI122X_SCR_MRBURSTDN		0x00008000
#define  TI122X_SCR_MRBURSTUP		0x00004000
#define  TI122X_SCR_RIMUX		0x00000001

/* Multimedia Control Register */
#define TI1250_MULTIMEDIA_CTL		0x0084	/* 8 bit */
#define  TI1250_MMC_ZVOUTEN		0x80
#define  TI1250_MMC_PORTSEL		0x40
#define  TI1250_MMC_ZVEN1		0x02
#define  TI1250_MMC_ZVEN0		0x01

#define TI1250_GENERAL_STATUS		0x0085	/* 8 bit */
#define TI1250_GPIO0_CONTROL		0x0088	/* 8 bit */
#define TI1250_GPIO1_CONTROL		0x0089	/* 8 bit */
#define TI1250_GPIO2_CONTROL		0x008a	/* 8 bit */
#define TI1250_GPIO3_CONTROL		0x008b	/* 8 bit */
#define TI122X_IRQMUX			0x008c	/* 32 bit */

/* Retry Status Register */
#define TI113X_RETRY_STATUS		0x0090	/* 8 bit */
#define  TI113X_RSR_PCIRETRY		0x80
#define  TI113X_RSR_CBRETRY		0x40
#define  TI113X_RSR_TEXP_CBB		0x20
#define  TI113X_RSR_MEXP_CBB		0x10
#define  TI113X_RSR_TEXP_CBA		0x08
#define  TI113X_RSR_MEXP_CBA		0x04
#define  TI113X_RSR_TEXP_PCI		0x02
#define  TI113X_RSR_MEXP_PCI		0x01

/* Card Control Register */
#define TI113X_CARD_CONTROL		0x0091	/* 8 bit */
#define  TI113X_CCR_RIENB		0x80
#define  TI113X_CCR_ZVENABLE		0x40
#define  TI113X_CCR_PCI_IRQ_ENA		0x20
#define  TI113X_CCR_PCI_IREQ		0x10
#define  TI113X_CCR_PCI_CSC		0x08
#define  TI113X_CCR_SPKROUTEN		0x02
#define  TI113X_CCR_IFG			0x01

#define  TI1220_CCR_PORT_SEL		0x20
#define  TI122X_CCR_AUD2MUX		0x04

/* Device Control Register */
#define TI113X_DEVICE_CONTROL		0x0092	/* 8 bit */
#define  TI113X_DCR_5V_FORCE		0x40
#define  TI113X_DCR_3V_FORCE		0x20
#define  TI113X_DCR_IMODE_MASK		0x06
#define  TI113X_DCR_IMODE_ISA		0x02
#define  TI113X_DCR_IMODE_SERIAL	0x04

#define  TI12XX_DCR_IMODE_PCI_ONLY	0x00
#define  TI12XX_DCR_IMODE_ALL_SERIAL	0x06

/* Buffer Control Register */
#define TI113X_BUFFER_CONTROL		0x0093	/* 8 bit */
#define  TI113X_BCR_CB_READ_DEPTH	0x08
#define  TI113X_BCR_CB_WRITE_DEPTH	0x04
#define  TI113X_BCR_PCI_READ_DEPTH	0x02
#define  TI113X_BCR_PCI_WRITE_DEPTH	0x01

/* Diagnostic Register */
#define TI1250_DIAGNOSTIC		0x0093	/* 8 bit */
#define  TI1250_DIAG_TRUE_VALUE		0x80
#define  TI1250_DIAG_PCI_IREQ		0x40
#define  TI1250_DIAG_PCI_CSC		0x20
#define  TI1250_DIAG_ASYNC_CSC		0x01

/* DMA Registers */
#define TI113X_DMA_0			0x0094	/* 32 bit */
#define TI113X_DMA_1			0x0098	/* 32 bit */

/* ExCA IO offset registers */
#define TI113X_IO_OFFSET(map)		(0x36+((map)<<1))

#ifdef CONFIG_CARDBUS

/*
 * Texas Instruments CardBus controller overrides.
 */
#define ti_sysctl(socket)	((socket)->private[0])
#define ti_cardctl(socket)	((socket)->private[1])
#define ti_devctl(socket)	((socket)->private[2])
#define ti_diag(socket)		((socket)->private[3])
#define ti_irqmux(socket)	((socket)->private[4])

/*
 * These are the TI specific power management handlers.
 */
static void ti_save_state(struct yenta_socket *socket)
{
	ti_sysctl(socket) = config_readl(socket, TI113X_SYSTEM_CONTROL);
	ti_irqmux(socket) = config_readl(socket, TI122X_IRQMUX);
	ti_cardctl(socket) = config_readb(socket, TI113X_CARD_CONTROL);
	ti_devctl(socket) = config_readb(socket, TI113X_DEVICE_CONTROL);
	ti_diag(socket) = config_readb(socket, TI1250_DIAGNOSTIC);
}

static void ti_restore_state(struct yenta_socket *socket)
{
	config_writel(socket, TI113X_SYSTEM_CONTROL, ti_sysctl(socket));
	config_writel(socket, TI122X_IRQMUX, ti_irqmux(socket));
	config_writeb(socket, TI113X_CARD_CONTROL, ti_cardctl(socket));
	config_writeb(socket, TI113X_DEVICE_CONTROL, ti_devctl(socket));
	config_writeb(socket, TI1250_DIAGNOSTIC, ti_diag(socket));
}

/*
 *	Zoom video control for TI122x/113x chips
 */

static void ti_zoom_video(struct pcmcia_socket *sock, int onoff)
{
	u8 reg;
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	/* If we don't have a Zoom Video switch this is harmless,
	   we just tristate the unused (ZV) lines */
	reg = config_readb(socket, TI113X_CARD_CONTROL);
	if (onoff)
		/* Zoom zoom, we will all go together, zoom zoom, zoom zoom */
		reg |= TI113X_CCR_ZVENABLE;
	else
		reg &= ~TI113X_CCR_ZVENABLE;
	config_writeb(socket, TI113X_CARD_CONTROL, reg);
}

/*
 *	The 145x series can also use this. They have an additional
 *	ZV autodetect mode we don't use but don't actually need.
 *	FIXME: manual says its in func0 and func1 but disagrees with
 *	itself about this - do we need to force func0, if so we need
 *	to know a lot more about socket pairings in pcmcia_socket than
 *	we do now.. uggh.
 */
 
static void ti1250_zoom_video(struct pcmcia_socket *sock, int onoff)
{	
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	int shift = 0;
	u8 reg;

	ti_zoom_video(sock, onoff);

	reg = config_readb(socket, TI1250_MULTIMEDIA_CTL);
	reg |= TI1250_MMC_ZVOUTEN;	/* ZV bus enable */

	if(PCI_FUNC(socket->dev->devfn)==1)
		shift = 1;
	
	if(onoff)
	{
		reg &= ~(1<<6); 	/* Clear select bit */
		reg |= shift<<6;	/* Favour our socket */
		reg |= 1<<shift;	/* Socket zoom video on */
	}
	else
	{
		reg &= ~(1<<6); 	/* Clear select bit */
		reg |= (1^shift)<<6;	/* Favour other socket */
		reg &= ~(1<<shift);	/* Socket zoon video off */
	}

	config_writeb(socket, TI1250_MULTIMEDIA_CTL, reg);
}

static void ti_set_zv(struct yenta_socket *socket)
{
	if(socket->dev->vendor == PCI_VENDOR_ID_TI)
	{
		switch(socket->dev->device)
		{
			/* There may be more .. */
			case PCI_DEVICE_ID_TI_1220:
			case PCI_DEVICE_ID_TI_1221:
			case PCI_DEVICE_ID_TI_1225:
				socket->socket.zoom_video = ti_zoom_video;
				break;	
			case PCI_DEVICE_ID_TI_1250:
			case PCI_DEVICE_ID_TI_1251A:
			case PCI_DEVICE_ID_TI_1251B:
			case PCI_DEVICE_ID_TI_1450:
				socket->socket.zoom_video = ti1250_zoom_video;
		}
	}
}


/*
 * Generic TI init - TI has an extension for the
 * INTCTL register that sets the PCI CSC interrupt.
 * Make sure we set it correctly at open and init
 * time
 * - open: disable the PCI CSC interrupt. This makes
 *   it possible to use the CSC interrupt to probe the
 *   ISA interrupts.
 * - init: set the interrupt to match our PCI state.
 *   This makes us correctly get PCI CSC interrupt
 *   events.
 */
static int ti_init(struct yenta_socket *socket)
{
	u8 new, reg = exca_readb(socket, I365_INTCTL);

	new = reg & ~I365_INTR_ENA;
	if (socket->cb_irq)
		new |= I365_INTR_ENA;
	if (new != reg)
		exca_writeb(socket, I365_INTCTL, new);
	return 0;
}

static int ti_override(struct yenta_socket *socket)
{
	u8 new, reg = exca_readb(socket, I365_INTCTL);

	new = reg & ~I365_INTR_ENA;
	if (new != reg)
		exca_writeb(socket, I365_INTCTL, new);

	ti_set_zv(socket);

#if 0
	/*
	 * If ISA interrupts don't work, then fall back to routing card
	 * interrupts to the PCI interrupt of the socket.
	 *
	 * Tweaking this when we are using serial PCI IRQs causes hangs
	 *   --rmk
	 */
	if (!socket->socket.irq_mask) {
		u8 irqmux, devctl;

		devctl = config_readb(socket, TI113X_DEVICE_CONTROL);
		if ((devctl & TI113X_DCR_IMODE_MASK) != TI12XX_DCR_IMODE_ALL_SERIAL) {
			printk (KERN_INFO "ti113x: Routing card interrupts to PCI\n");

			devctl &= ~TI113X_DCR_IMODE_MASK;

			irqmux = config_readl(socket, TI122X_IRQMUX);
			irqmux = (irqmux & ~0x0f) | 0x02; /* route INTA */
			irqmux = (irqmux & ~0xf0) | 0x20; /* route INTB */

			config_writel(socket, TI122X_IRQMUX, irqmux);
			config_writeb(socket, TI113X_DEVICE_CONTROL, devctl);
		}
	}
#endif

	return 0;
}

static int ti113x_override(struct yenta_socket *socket)
{
	u8 cardctl;

	cardctl = config_readb(socket, TI113X_CARD_CONTROL);
	cardctl &= ~(TI113X_CCR_PCI_IRQ_ENA | TI113X_CCR_PCI_IREQ | TI113X_CCR_PCI_CSC);
	if (socket->cb_irq)
		cardctl |= TI113X_CCR_PCI_IRQ_ENA | TI113X_CCR_PCI_CSC | TI113X_CCR_PCI_IREQ;
	config_writeb(socket, TI113X_CARD_CONTROL, cardctl);

	return ti_override(socket);
}


static int ti12xx_override(struct yenta_socket *socket)
{
	u32 val;

	/* make sure that memory burst is active */
	val = config_readl(socket, TI113X_SYSTEM_CONTROL);
	if (!(val & TI122X_SCR_MRBURSTUP)) {
		printk(KERN_INFO "Yenta: Enabling burst memory read transactions\n");
		val |= TI122X_SCR_MRBURSTUP;
		config_writel(socket, TI113X_SYSTEM_CONTROL, val);
	}

	/*
	 * Yenta expects controllers to use CSCINT to route
	 * CSC interrupts to PCI rather than INTVAL.
	 */
	val = config_readb(socket, TI1250_DIAGNOSTIC);
	printk(KERN_INFO "Yenta: Using %s to route CSC interrupts to PCI\n",
		(val & TI1250_DIAG_PCI_CSC) ? "CSCINT" : "INTVAL");
	printk(KERN_INFO "Yenta: Routing CardBus interrupts to %s\n",
		(val & TI1250_DIAG_PCI_IREQ) ? "PCI" : "ISA");

	return ti_override(socket);
}


static int ti1250_override(struct yenta_socket *socket)
{
	u8 old, diag;

	old = config_readb(socket, TI1250_DIAGNOSTIC);
	diag = old & ~(TI1250_DIAG_PCI_CSC | TI1250_DIAG_PCI_IREQ);
	if (socket->cb_irq)
		diag |= TI1250_DIAG_PCI_CSC | TI1250_DIAG_PCI_IREQ;

	if (diag != old) {
		printk(KERN_INFO "Yenta: adjusting diagnostic: %02x -> %02x\n",
			old, diag);
		config_writeb(socket, TI1250_DIAGNOSTIC, diag);
	}

#if 0
	/*
	 * This is highly machine specific, and we should NOT touch
	 * this register - we have no knowledge how the hardware
	 * is actually wired.
	 *
	 * If we're going to do this, we should probably look into
	 * using the subsystem IDs.
	 *
	 * On ThinkPad 380XD, this changes MFUNC0 from the ISA IRQ3
	 * output (which it is) to IRQ2.  We also change MFUNC1
	 * from ISA IRQ4 to IRQ6.
	 */
	irqmux = config_readl(socket, TI122X_IRQMUX);
	irqmux = (irqmux & ~0x0f) | 0x02; /* route INTA */
	if (!(ti_sysctl(socket) & TI122X_SCR_INTRTIE))
		irqmux = (irqmux & ~0xf0) | 0x20; /* route INTB */
	config_writel(socket, TI122X_IRQMUX, irqmux);
#endif

	return ti12xx_override(socket);
}

#endif /* CONFIG_CARDBUS */

#endif /* _LINUX_TI113X_H */

