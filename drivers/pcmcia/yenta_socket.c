/*
 * Regular cardbus driver ("yenta_socket")
 *
 * (C) Copyright 1999, 2000 Linus Torvalds
 *
 * Changelog:
 * Aug 2002: Manfred Spraul <manfred@colorfullife.com>
 * 	Dynamically adjust the size of the bridge resource
 * 	
 * May 2003: Dominik Brodowski <linux@brodo.de>
 * 	Merge pci_socket.c and yenta.c into one file
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>

#include <asm/io.h>

#include "yenta_socket.h"
#include "i82365.h"


#if 0
#define DEBUG(x,args...)	printk("%s: " x, __FUNCTION__, ##args)
#else
#define DEBUG(x,args...)
#endif

/* Don't ask.. */
#define to_cycles(ns)	((ns)/120)
#define to_ns(cycles)	((cycles)*120)

/*
 * Generate easy-to-use ways of reading a cardbus sockets
 * regular memory space ("cb_xxx"), configuration space
 * ("config_xxx") and compatibility space ("exca_xxxx")
 */
static inline u32 cb_readl(struct yenta_socket *socket, unsigned reg)
{
	u32 val = readl(socket->base + reg);
	DEBUG("%p %04x %08x\n", socket, reg, val);
	return val;
}

static inline void cb_writel(struct yenta_socket *socket, unsigned reg, u32 val)
{
	DEBUG("%p %04x %08x\n", socket, reg, val);
	writel(val, socket->base + reg);
}

static inline u8 config_readb(struct yenta_socket *socket, unsigned offset)
{
	u8 val;
	pci_read_config_byte(socket->dev, offset, &val);
	DEBUG("%p %04x %02x\n", socket, offset, val);
	return val;
}

static inline u16 config_readw(struct yenta_socket *socket, unsigned offset)
{
	u16 val;
	pci_read_config_word(socket->dev, offset, &val);
	DEBUG("%p %04x %04x\n", socket, offset, val);
	return val;
}

static inline u32 config_readl(struct yenta_socket *socket, unsigned offset)
{
	u32 val;
	pci_read_config_dword(socket->dev, offset, &val);
	DEBUG("%p %04x %08x\n", socket, offset, val);
	return val;
}

static inline void config_writeb(struct yenta_socket *socket, unsigned offset, u8 val)
{
	DEBUG("%p %04x %02x\n", socket, offset, val);
	pci_write_config_byte(socket->dev, offset, val);
}

static inline void config_writew(struct yenta_socket *socket, unsigned offset, u16 val)
{
	DEBUG("%p %04x %04x\n", socket, offset, val);
	pci_write_config_word(socket->dev, offset, val);
}

static inline void config_writel(struct yenta_socket *socket, unsigned offset, u32 val)
{
	DEBUG("%p %04x %08x\n", socket, offset, val);
	pci_write_config_dword(socket->dev, offset, val);
}

static inline u8 exca_readb(struct yenta_socket *socket, unsigned reg)
{
	u8 val = readb(socket->base + 0x800 + reg);
	DEBUG("%p %04x %02x\n", socket, reg, val);
	return val;
}

static inline u8 exca_readw(struct yenta_socket *socket, unsigned reg)
{
	u16 val;
	val = readb(socket->base + 0x800 + reg);
	val |= readb(socket->base + 0x800 + reg + 1) << 8;
	DEBUG("%p %04x %04x\n", socket, reg, val);
	return val;
}

static inline void exca_writeb(struct yenta_socket *socket, unsigned reg, u8 val)
{
	DEBUG("%p %04x %02x\n", socket, reg, val);
	writeb(val, socket->base + 0x800 + reg);
}

static void exca_writew(struct yenta_socket *socket, unsigned reg, u16 val)
{
	DEBUG("%p %04x %04x\n", socket, reg, val);
	writeb(val, socket->base + 0x800 + reg);
	writeb(val >> 8, socket->base + 0x800 + reg + 1);
}

/*
 * Ugh, mixed-mode cardbus and 16-bit pccard state: things depend
 * on what kind of card is inserted..
 */
static int yenta_get_status(struct pcmcia_socket *sock, unsigned int *value)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	unsigned int val;
	u32 state = cb_readl(socket, CB_SOCKET_STATE);

	val  = (state & CB_3VCARD) ? SS_3VCARD : 0;
	val |= (state & CB_XVCARD) ? SS_XVCARD : 0;
	val |= (state & (CB_CDETECT1 | CB_CDETECT2 | CB_5VCARD | CB_3VCARD
			 | CB_XVCARD | CB_YVCARD)) ? 0 : SS_PENDING;

	if (state & CB_CBCARD) {
		val |= SS_CARDBUS;	
		val |= (state & CB_CARDSTS) ? SS_STSCHG : 0;
		val |= (state & (CB_CDETECT1 | CB_CDETECT2)) ? 0 : SS_DETECT;
		val |= (state & CB_PWRCYCLE) ? SS_POWERON | SS_READY : 0;
	} else {
		u8 status = exca_readb(socket, I365_STATUS);
		val |= ((status & I365_CS_DETECT) == I365_CS_DETECT) ? SS_DETECT : 0;
		if (exca_readb(socket, I365_INTCTL) & I365_PC_IOCARD) {
			val |= (status & I365_CS_STSCHG) ? 0 : SS_STSCHG;
		} else {
			val |= (status & I365_CS_BVD1) ? 0 : SS_BATDEAD;
			val |= (status & I365_CS_BVD2) ? 0 : SS_BATWARN;
		}
		val |= (status & I365_CS_WRPROT) ? SS_WRPROT : 0;
		val |= (status & I365_CS_READY) ? SS_READY : 0;
		val |= (status & I365_CS_POWERON) ? SS_POWERON : 0;
	}

	*value = val;
	return 0;
}

static int yenta_Vcc_power(u32 control)
{
	switch (control & CB_SC_VCC_MASK) {
	case CB_SC_VCC_5V: return 50;
	case CB_SC_VCC_3V: return 33;
	default: return 0;
	}
}

static int yenta_Vpp_power(u32 control)
{
	switch (control & CB_SC_VPP_MASK) {
	case CB_SC_VPP_12V: return 120;
	case CB_SC_VPP_5V: return 50;
	case CB_SC_VPP_3V: return 33;
	default: return 0;
	}
}

static int yenta_get_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	u8 reg;
	u32 control;

	control = cb_readl(socket, CB_SOCKET_CONTROL);

	state->Vcc = yenta_Vcc_power(control);
	state->Vpp = yenta_Vpp_power(control);
	state->io_irq = socket->io_irq;

	if (cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) {
		u16 bridge = config_readw(socket, CB_BRIDGE_CONTROL);
		if (bridge & CB_BRIDGE_CRST)
			state->flags |= SS_RESET;
		return 0;
	}

	/* 16-bit card state.. */
	reg = exca_readb(socket, I365_POWER);
	state->flags = (reg & I365_PWR_AUTO) ? SS_PWR_AUTO : 0;
	state->flags |= (reg & I365_PWR_OUT) ? SS_OUTPUT_ENA : 0;

	reg = exca_readb(socket, I365_INTCTL);
	state->flags |= (reg & I365_PC_RESET) ? 0 : SS_RESET;
	state->flags |= (reg & I365_PC_IOCARD) ? SS_IOCARD : 0;

	reg = exca_readb(socket, I365_CSCINT);
	state->csc_mask = (reg & I365_CSC_DETECT) ? SS_DETECT : 0;
	if (state->flags & SS_IOCARD) {
		state->csc_mask |= (reg & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	} else {
		state->csc_mask |= (reg & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		state->csc_mask |= (reg & I365_CSC_BVD2) ? SS_BATWARN : 0;
		state->csc_mask |= (reg & I365_CSC_READY) ? SS_READY : 0;
	}

	return 0;
}

static void yenta_set_power(struct yenta_socket *socket, socket_state_t *state)
{
	u32 reg = 0;	/* CB_SC_STPCLK? */
	switch (state->Vcc) {
	case 33: reg = CB_SC_VCC_3V; break;
	case 50: reg = CB_SC_VCC_5V; break;
	default: reg = 0; break;
	}
	switch (state->Vpp) {
	case 33:  reg |= CB_SC_VPP_3V; break;
	case 50:  reg |= CB_SC_VPP_5V; break;
	case 120: reg |= CB_SC_VPP_12V; break;
	}
	if (reg != cb_readl(socket, CB_SOCKET_CONTROL))
		cb_writel(socket, CB_SOCKET_CONTROL, reg);
}

static int yenta_set_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	u16 bridge;

	if (state->flags & SS_DEBOUNCED) {
		/* The insertion debounce period has ended.  Clear any pending insertion events */
		state->flags &= ~SS_DEBOUNCED;		/* SS_DEBOUNCED is oneshot */
	}
	yenta_set_power(socket, state);
	socket->io_irq = state->io_irq;
	bridge = config_readw(socket, CB_BRIDGE_CONTROL) & ~(CB_BRIDGE_CRST | CB_BRIDGE_INTR);
	if (cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) {
		u8 intr;
		bridge |= (state->flags & SS_RESET) ? CB_BRIDGE_CRST : 0;

		/* ISA interrupt control? */
		intr = exca_readb(socket, I365_INTCTL);
		intr = (intr & ~0xf);
		if (!socket->cb_irq) {
			intr |= state->io_irq;
			bridge |= CB_BRIDGE_INTR;
		}
		exca_writeb(socket, I365_INTCTL, intr);
	}  else {
		u8 reg;

		reg = exca_readb(socket, I365_INTCTL) & (I365_RING_ENA | I365_INTR_ENA);
		reg |= (state->flags & SS_RESET) ? 0 : I365_PC_RESET;
		reg |= (state->flags & SS_IOCARD) ? I365_PC_IOCARD : 0;
		if (state->io_irq != socket->cb_irq) {
			reg |= state->io_irq;
			bridge |= CB_BRIDGE_INTR;
		}
		exca_writeb(socket, I365_INTCTL, reg);

		reg = exca_readb(socket, I365_POWER) & (I365_VCC_MASK|I365_VPP1_MASK);
		reg |= I365_PWR_NORESET;
		if (state->flags & SS_PWR_AUTO) reg |= I365_PWR_AUTO;
		if (state->flags & SS_OUTPUT_ENA) reg |= I365_PWR_OUT;
		if (exca_readb(socket, I365_POWER) != reg)
			exca_writeb(socket, I365_POWER, reg);

		/* CSC interrupt: no ISA irq for CSC */
		reg = I365_CSC_DETECT;
		if (state->flags & SS_IOCARD) {
			if (state->csc_mask & SS_STSCHG) reg |= I365_CSC_STSCHG;
		} else {
			if (state->csc_mask & SS_BATDEAD) reg |= I365_CSC_BVD1;
			if (state->csc_mask & SS_BATWARN) reg |= I365_CSC_BVD2;
			if (state->csc_mask & SS_READY) reg |= I365_CSC_READY;
		}
		exca_writeb(socket, I365_CSCINT, reg);
		exca_readb(socket, I365_CSC);
	}
	config_writew(socket, CB_BRIDGE_CONTROL, bridge);
	/* Socket event mask: get card insert/remove events.. */
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CDMASK);
	return 0;
}

static int yenta_set_io_map(struct pcmcia_socket *sock, struct pccard_io_map *io)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	int map;
	unsigned char ioctl, addr, enable;

	map = io->map;

	if (map > 1)
		return -EINVAL;

	enable = I365_ENA_IO(map);
	addr = exca_readb(socket, I365_ADDRWIN);

	/* Disable the window before changing it.. */
	if (addr & enable) {
		addr &= ~enable;
		exca_writeb(socket, I365_ADDRWIN, addr);
	}

	exca_writew(socket, I365_IO(map)+I365_W_START, io->start);
	exca_writew(socket, I365_IO(map)+I365_W_STOP, io->stop);

	ioctl = exca_readb(socket, I365_IOCTL) & ~I365_IOCTL_MASK(map);
	if (io->flags & MAP_0WS) ioctl |= I365_IOCTL_0WS(map);
	if (io->flags & MAP_16BIT) ioctl |= I365_IOCTL_16BIT(map);
	if (io->flags & MAP_AUTOSZ) ioctl |= I365_IOCTL_IOCS16(map);
	exca_writeb(socket, I365_IOCTL, ioctl);

	if (io->flags & MAP_ACTIVE)
		exca_writeb(socket, I365_ADDRWIN, addr | enable);
	return 0;
}

static int yenta_set_mem_map(struct pcmcia_socket *sock, struct pccard_mem_map *mem)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	int map;
	unsigned char addr, enable;
	unsigned int start, stop, card_start;
	unsigned short word;

	map = mem->map;
	start = mem->sys_start;
	stop = mem->sys_stop;
	card_start = mem->card_start;

	if (map > 4 || start > stop || ((start ^ stop) >> 24) ||
	    (card_start >> 26) || mem->speed > 1000)
		return -EINVAL;

	enable = I365_ENA_MEM(map);
	addr = exca_readb(socket, I365_ADDRWIN);
	if (addr & enable) {
		addr &= ~enable;
		exca_writeb(socket, I365_ADDRWIN, addr);
	}

	exca_writeb(socket, CB_MEM_PAGE(map), start >> 24);

	word = (start >> 12) & 0x0fff;
	if (mem->flags & MAP_16BIT)
		word |= I365_MEM_16BIT;
	if (mem->flags & MAP_0WS)
		word |= I365_MEM_0WS;
	exca_writew(socket, I365_MEM(map) + I365_W_START, word);

	word = (stop >> 12) & 0x0fff;
	switch (to_cycles(mem->speed)) {
		case 0: break;
		case 1:  word |= I365_MEM_WS0; break;
		case 2:  word |= I365_MEM_WS1; break;
		default: word |= I365_MEM_WS1 | I365_MEM_WS0; break;
	}
	exca_writew(socket, I365_MEM(map) + I365_W_STOP, word);

	word = ((card_start - start) >> 12) & 0x3fff;
	if (mem->flags & MAP_WRPROT)
		word |= I365_MEM_WRPROT;
	if (mem->flags & MAP_ATTRIB)
		word |= I365_MEM_REG;
	exca_writew(socket, I365_MEM(map) + I365_W_OFF, word);

	if (mem->flags & MAP_ACTIVE)
		exca_writeb(socket, I365_ADDRWIN, addr | enable);
	return 0;
}


static unsigned int yenta_events(struct yenta_socket *socket)
{
	u8 csc;
	u32 cb_event;
	unsigned int events;

	/* Clear interrupt status for the event */
	cb_event = cb_readl(socket, CB_SOCKET_EVENT);
	cb_writel(socket, CB_SOCKET_EVENT, cb_event);

	csc = exca_readb(socket, I365_CSC);

	events = (cb_event & (CB_CD1EVENT | CB_CD2EVENT)) ? SS_DETECT : 0 ;
	events |= (csc & I365_CSC_DETECT) ? SS_DETECT : 0;
	if (exca_readb(socket, I365_INTCTL) & I365_PC_IOCARD) {
		events |= (csc & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	} else {
		events |= (csc & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		events |= (csc & I365_CSC_BVD2) ? SS_BATWARN : 0;
		events |= (csc & I365_CSC_READY) ? SS_READY : 0;
	}
	return events;
}


static irqreturn_t yenta_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int events;
	struct yenta_socket *socket = (struct yenta_socket *) dev_id;

	events = yenta_events(socket);
	if (events) {
		pcmcia_parse_events(&socket->socket, events);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static void yenta_interrupt_wrapper(unsigned long data)
{
	struct yenta_socket *socket = (struct yenta_socket *) data;

	yenta_interrupt(0, (void *)socket, NULL);
	socket->poll_timer.expires = jiffies + HZ;
	add_timer(&socket->poll_timer);
}

/*
 * Only probe "regular" interrupts, don't
 * touch dangerous spots like the mouse irq,
 * because there are mice that apparently
 * get really confused if they get fondled
 * too intimately.
 *
 * Default to 11, 10, 9, 7, 6, 5, 4, 3.
 */
static u32 isa_interrupts = 0x0ef8;

static unsigned int yenta_probe_irq(struct yenta_socket *socket, u32 isa_irq_mask)
{
	int i;
	unsigned long val;
	u16 bridge_ctrl;
	u32 mask;

	/* Set up ISA irq routing to probe the ISA irqs.. */
	bridge_ctrl = config_readw(socket, CB_BRIDGE_CONTROL);
	if (!(bridge_ctrl & CB_BRIDGE_INTR)) {
		bridge_ctrl |= CB_BRIDGE_INTR;
		config_writew(socket, CB_BRIDGE_CONTROL, bridge_ctrl);
	}

	/*
	 * Probe for usable interrupts using the force
	 * register to generate bogus card status events.
	 */
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CSTSMASK);
	exca_writeb(socket, I365_CSCINT, 0);
	val = probe_irq_on() & isa_irq_mask;
	for (i = 1; i < 16; i++) {
		if (!((val >> i) & 1))
			continue;
		exca_writeb(socket, I365_CSCINT, I365_CSC_STSCHG | (i << 4));
		cb_writel(socket, CB_SOCKET_FORCE, CB_FCARDSTS);
		udelay(100);
		cb_writel(socket, CB_SOCKET_EVENT, -1);
	}
	cb_writel(socket, CB_SOCKET_MASK, 0);
	exca_writeb(socket, I365_CSCINT, 0);
	
	mask = probe_irq_mask(val) & 0xffff;

	bridge_ctrl &= ~CB_BRIDGE_INTR;
	config_writew(socket, CB_BRIDGE_CONTROL, bridge_ctrl);

	return mask;
}

/*
 * Set static data that doesn't need re-initializing..
 */
static void yenta_get_socket_capabilities(struct yenta_socket *socket, u32 isa_irq_mask)
{
	socket->socket.features |= SS_CAP_PAGE_REGS | SS_CAP_PCCARD | SS_CAP_CARDBUS;
	socket->socket.map_size = 0x1000;
	socket->socket.pci_irq = socket->cb_irq;
	socket->socket.irq_mask = yenta_probe_irq(socket, isa_irq_mask);
	socket->socket.cb_dev = socket->dev;

	printk("Yenta IRQ list %04x, PCI irq%d\n", socket->socket.irq_mask, socket->cb_irq);
}


static void yenta_clear_maps(struct yenta_socket *socket)
{
	int i;
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { 0, 0, 0, 0, 0, 0 };

	mem.sys_stop = 0x0fff;
	yenta_set_socket(&socket->socket, &dead_socket);
	for (i = 0; i < 2; i++) {
		io.map = i;
		yenta_set_io_map(&socket->socket, &io);
	}
	for (i = 0; i < 5; i++) {
		mem.map = i;
		yenta_set_mem_map(&socket->socket, &mem);
	}
}

/*
 * Initialize the standard cardbus registers
 */
static void yenta_config_init(struct yenta_socket *socket)
{
	u16 bridge;
	struct pci_dev *dev = socket->dev;

	pci_set_power_state(socket->dev, 0);

	config_writel(socket, CB_LEGACY_MODE_BASE, 0);
	config_writel(socket, PCI_BASE_ADDRESS_0, dev->resource[0].start);
	config_writew(socket, PCI_COMMAND,
			PCI_COMMAND_IO |
			PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER |
			PCI_COMMAND_WAIT);

	/* MAGIC NUMBERS! Fixme */
	config_writeb(socket, PCI_CACHE_LINE_SIZE, L1_CACHE_BYTES / 4);
	config_writeb(socket, PCI_LATENCY_TIMER, 168);
	config_writel(socket, PCI_PRIMARY_BUS,
		(176 << 24) |			   /* sec. latency timer */
		(dev->subordinate->subordinate << 16) | /* subordinate bus */
		(dev->subordinate->secondary << 8) |  /* secondary bus */
		dev->subordinate->primary);		   /* primary bus */

	/*
	 * Set up the bridging state:
	 *  - enable write posting.
	 *  - memory window 0 prefetchable, window 1 non-prefetchable
	 *  - PCI interrupts enabled if a PCI interrupt exists..
	 */
	bridge = config_readw(socket, CB_BRIDGE_CONTROL);
	bridge &= ~(CB_BRIDGE_CRST | CB_BRIDGE_PREFETCH1 | CB_BRIDGE_INTR | CB_BRIDGE_ISAEN | CB_BRIDGE_VGAEN);
	bridge |= CB_BRIDGE_PREFETCH0 | CB_BRIDGE_POSTEN;
	if (!socket->cb_irq)
		bridge |= CB_BRIDGE_INTR;
	config_writew(socket, CB_BRIDGE_CONTROL, bridge);

	exca_writeb(socket, I365_GBLCTL, 0x00);
	exca_writeb(socket, I365_GENCTL, 0x00);

	/* Redo card voltage interrogation */
	cb_writel(socket, CB_SOCKET_FORCE, CB_CVSTEST);
}

/* Called at resume and initialization events */
static int yenta_init(struct pcmcia_socket *sock)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	yenta_config_init(socket);
	yenta_clear_maps(socket);

	/* Re-enable interrupts */
	cb_writel(socket, CB_SOCKET_MASK, CB_CDMASK);
	return 0;
}

static int yenta_suspend(struct pcmcia_socket *sock)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	yenta_set_socket(sock, &dead_socket);

	/* Disable interrupts */
	cb_writel(socket, CB_SOCKET_MASK, 0x0);

	/*
	 * This does not work currently. The controller
	 * loses too much information during D3 to come up
	 * cleanly. We should probably fix yenta_init()
	 * to update all the critical registers, notably
	 * the IO and MEM bridging region data.. That is
	 * something that pci_set_power_state() should
	 * probably know about bridges anyway.
	 *
	pci_set_power_state(socket->dev, 3);
	 */

	return 0;
}

/*
 * Use an adaptive allocation for the memory resource,
 * sometimes the memory behind pci bridges is limited:
 * 1/8 of the size of the io window of the parent.
 * max 4 MB, min 16 kB.
 */
#define BRIDGE_MEM_MAX 4*1024*1024
#define BRIDGE_MEM_MIN 16*1024

#define BRIDGE_IO_MAX 256
#define BRIDGE_IO_MIN 32

static void yenta_allocate_res(struct yenta_socket *socket, int nr, unsigned type)
{
	struct pci_bus *bus;
	struct resource *root, *res;
	u32 start, end;
	u32 align, size, min;
	unsigned offset;
	unsigned mask;

	/* The granularity of the memory limit is 4kB, on IO it's 4 bytes */
	mask = ~0xfff;
	if (type & IORESOURCE_IO)
		mask = ~3;

	offset = 0x1c + 8*nr;
	bus = socket->dev->subordinate;
	res = socket->dev->resource + PCI_BRIDGE_RESOURCES + nr;
	res->name = bus->name;
	res->flags = type;
	res->start = 0;
	res->end = 0;
	root = pci_find_parent_resource(socket->dev, res);

	if (!root)
		return;

	start = config_readl(socket, offset) & mask;
	end = config_readl(socket, offset+4) | ~mask;
	if (start && end > start) {
		res->start = start;
		res->end = end;
		if (request_resource(root, res) == 0)
			return;
		printk(KERN_INFO "yenta %s: Preassigned resource %d busy, reconfiguring...\n",
				socket->dev->slot_name, nr);
		res->start = res->end = 0;
	}

	if (type & IORESOURCE_IO) {
		align = 1024;
		size = BRIDGE_IO_MAX;
		min = BRIDGE_IO_MIN;
		start = PCIBIOS_MIN_IO;
		end = ~0U;
	} else {
		unsigned long avail = root->end - root->start;
		int i;
		size = BRIDGE_MEM_MAX;
		if (size > avail/8) {
			size=(avail+1)/8;
			/* round size down to next power of 2 */
			i = 0;
			while ((size /= 2) != 0)
				i++;
			size = 1 << i;
		}
		if (size < BRIDGE_MEM_MIN)
			size = BRIDGE_MEM_MIN;
		min = BRIDGE_MEM_MIN;
		align = size;
		start = PCIBIOS_MIN_MEM;
		end = ~0U;
	}
	
	do {
		if (allocate_resource(root, res, size, start, end, align, NULL, NULL)==0) {
			config_writel(socket, offset, res->start);
			config_writel(socket, offset+4, res->end);
			return;
		}
		size = size/2;
		align = size;
	} while (size >= min);
	printk(KERN_INFO "yenta %s: no resource of type %x available, trying to continue...\n",
			socket->dev->slot_name, type);
	res->start = res->end = 0;
}

/*
 * Allocate the bridge mappings for the device..
 */
static void yenta_allocate_resources(struct yenta_socket *socket)
{
	yenta_allocate_res(socket, 0, IORESOURCE_MEM|IORESOURCE_PREFETCH);
	yenta_allocate_res(socket, 1, IORESOURCE_MEM);
	yenta_allocate_res(socket, 2, IORESOURCE_IO);
	yenta_allocate_res(socket, 3, IORESOURCE_IO);	/* PCI isn't clever enough to use this one yet */
}


/*
 * Free the bridge mappings for the device..
 */
static void yenta_free_resources(struct yenta_socket *socket)
{
	int i;
	for (i=0;i<4;i++) {
		struct resource *res;
		res = socket->dev->resource + PCI_BRIDGE_RESOURCES + i;
		if (res->start != 0 && res->end != 0)
			release_resource(res);
		res->start = res->end = 0;
	}
}


/*
 * Close it down - release our resources and go home..
 */
static void yenta_close(struct pci_dev *dev)
{
	struct yenta_socket *sock = pci_get_drvdata(dev);

	/* we don't want a dying socket registered */
	pcmcia_unregister_socket(&sock->socket);
	
	/* Disable all events so we don't die in an IRQ storm */
	cb_writel(sock, CB_SOCKET_MASK, 0x0);
	exca_writeb(sock, I365_CSCINT, 0);

	if (sock->cb_irq)
		free_irq(sock->cb_irq, sock);
	else
		del_timer_sync(&sock->poll_timer);

	if (sock->base)
		iounmap(sock->base);
	yenta_free_resources(sock);

	pci_set_drvdata(dev, NULL);
}


static struct pccard_operations yenta_socket_operations = {
	.init			= yenta_init,
	.suspend		= yenta_suspend,
	.get_status		= yenta_get_status,
	.get_socket		= yenta_get_socket,
	.set_socket		= yenta_set_socket,
	.set_io_map		= yenta_set_io_map,
	.set_mem_map		= yenta_set_mem_map,
};


#include "ti113x.h"
#include "ricoh.h"

/*
 * Different cardbus controllers have slightly different
 * initialization sequences etc details. List them here..
 */
#define PD(x,y) PCI_VENDOR_ID_##x, PCI_DEVICE_ID_##x##_##y
struct cardbus_override_struct {
	unsigned short vendor;
	unsigned short device;
	int (*override) (struct yenta_socket *socket);
} cardbus_override[] = {
	{ PD(TI,1031),	&ti_override },

	/* TBD: Check if these TI variants can use more
	 * advanced overrides instead */
	{ PD(TI,1210),	&ti_override },
	{ PD(TI,1211),	&ti_override },
	{ PD(TI,1251A),	&ti_override },
	{ PD(TI,1251B),	&ti_override },
	{ PD(TI,1420),	&ti_override },
	{ PD(TI,1450),	&ti_override },
	{ PD(TI,4410),	&ti_override },
	{ PD(TI,4451),	&ti_override },

	{ PD(TI,1130),	&ti113x_override },
	{ PD(TI,1131),	&ti113x_override },

	{ PD(TI,1220),	&ti12xx_override },
	{ PD(TI,1221),	&ti12xx_override },
	{ PD(TI,1225),	&ti12xx_override },
	{ PD(TI,1520),  &ti12xx_override },

	{ PD(TI,1250),	&ti1250_override },
	{ PD(TI,1410),	&ti1250_override },

	{ PD(RICOH,RL5C465), &ricoh_override },
	{ PD(RICOH,RL5C466), &ricoh_override },
	{ PD(RICOH,RL5C475), &ricoh_override },
	{ PD(RICOH,RL5C476), &ricoh_override },
	{ PD(RICOH,RL5C478), &ricoh_override },

	{ }, /* all zeroes */
};


/*
 * Initialize a cardbus controller. Make sure we have a usable
 * interrupt, and that we can map the cardbus area. Fill in the
 * socket information structure..
 */
static int __devinit yenta_probe (struct pci_dev *dev, const struct pci_device_id *id)
{
	struct yenta_socket *socket;
	struct cardbus_override_struct *d;
	
	socket = kmalloc(sizeof(struct yenta_socket), GFP_KERNEL);
	if (!socket)
		return -ENOMEM;
	memset(socket, 0, sizeof(*socket));

	/* prepare pcmcia_socket */
	socket->socket.ss_entry = &yenta_socket_operations;
	socket->socket.dev.dev = &dev->dev;
	socket->socket.driver_data = socket;
	socket->socket.owner = THIS_MODULE;

	/* prepare struct yenta_socket */
	socket->dev = dev;
	pci_set_drvdata(dev, socket);

	/*
	 * Do some basic sanity checking..
	 */
	if (pci_enable_device(dev))
		return -1;
	if (!pci_resource_start(dev, 0)) {
		printk("No cardbus resource!\n");
		return -1;
	}

	/*
	 * Ok, start setup.. Map the cardbus registers,
	 * and request the IRQ.
	 */
	socket->base = ioremap(pci_resource_start(dev, 0), 0x1000);
	if (!socket->base)
		return -1;

	yenta_config_init(socket);

	/* Disable all events */
	cb_writel(socket, CB_SOCKET_MASK, 0x0);

	/* Set up the bridge regions.. */
	yenta_allocate_resources(socket);

	socket->cb_irq = dev->irq;

	/* Do we have special options for the device? */
	d = cardbus_override;
	while (d->override) {
		if ((dev->vendor == d->vendor) && (dev->device == d->device)) {
			int retval = d->override(socket);
			if (retval < 0)
				return retval;
		}
		d++;
	}

	/* We must finish initialization here */

	if (!socket->cb_irq || request_irq(socket->cb_irq, yenta_interrupt, SA_SHIRQ, socket->dev->dev.name, socket)) {
		/* No IRQ or request_irq failed. Poll */
		socket->cb_irq = 0; /* But zero is a valid IRQ number. */
		init_timer(&socket->poll_timer);
		socket->poll_timer.function = yenta_interrupt_wrapper;
		socket->poll_timer.data = (unsigned long)socket;
		socket->poll_timer.expires = jiffies + HZ;
		add_timer(&socket->poll_timer);
	}

	/* Figure out what the dang thing can do for the PCMCIA layer... */
	yenta_get_socket_capabilities(socket, isa_interrupts);
	printk("Socket status: %08x\n", cb_readl(socket, CB_SOCKET_STATE));

	/* Register it with the pcmcia layer.. */
	return pcmcia_register_socket(&socket->socket);
}


static int yenta_dev_suspend (struct pci_dev *dev, u32 state)
{
	return pcmcia_socket_dev_suspend(&dev->dev, state, 0);
}


static int yenta_dev_resume (struct pci_dev *dev)
{
	return pcmcia_socket_dev_resume(&dev->dev, RESUME_RESTORE_STATE);
}


static struct pci_device_id yenta_table [] __devinitdata = { {
	.class		= PCI_CLASS_BRIDGE_CARDBUS << 8,
	.class_mask	= ~0,

	.vendor		= PCI_ANY_ID,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
}, { /* all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, yenta_table);


static struct pci_driver yenta_cardbus_driver = {
	.name		= "yenta_cardbus",
	.id_table	= yenta_table,
	.probe		= yenta_probe,
	.remove		= __devexit_p(yenta_close),
	.suspend	= yenta_dev_suspend,
	.resume		= yenta_dev_resume,
};


static int __init yenta_socket_init(void)
{
	return pci_register_driver (&yenta_cardbus_driver);
}


static void __exit yenta_socket_exit (void)
{
	pci_unregister_driver (&yenta_cardbus_driver);
}


module_init(yenta_socket_init);
module_exit(yenta_socket_exit);

MODULE_LICENSE("GPL");
