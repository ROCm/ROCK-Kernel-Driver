/* lasi_82596.c -- driver for the intel 82596 ethernet controller, as
   munged into HPPA boxen .

   This driver is based upon 82596.c, original credits are below...
   but there were too many hoops which HP wants jumped through to
   keep this code in there in a sane manner.

   3 primary sources of the mess -- 
   1) hppa needs *lots* of cacheline flushing to keep this kind of
   MMIO running.

   2) The 82596 needs to see all of its pointers as their physical
   address.  Thus virt_to_bus/bus_to_virt are *everywhere*.

   3) The implementation HP is using seems to be significantly pickier 
   about when and how the command and RX units are started.  some
   command ordering was changed.

   Examination of the mach driver leads one to believe that there
   might be a saner way to pull this off...  anyone who feels like a
   full rewrite can be my guest.

   Split 02/13/2000 Sam Creasey (sammy@oh.verio.com)
   
   02/01/2000  Initial modifications for parisc by Helge Deller (deller@gmx.de)
   03/02/2000  changes for better/correct(?) cache-flushing (deller)
*/

/* 82596.c: A generic 82596 ethernet driver for linux. */
/*
   Based on Apricot.c
   Written 1994 by Mark Evans.
   This driver is for the Apricot 82596 bus-master interface

   Modularised 12/94 Mark Evans


   Modified to support the 82596 ethernet chips on 680x0 VME boards.
   by Richard Hirst <richard@sleepie.demon.co.uk>
   Renamed to be 82596.c

   980825:  Changed to receive directly in to sk_buffs which are
   allocated at open() time.  Eliminates copy on incoming frames
   (small ones are still copied).  Shared data now held in a
   non-cached page, so we can run on 68060 in copyback mode.

   TBD:
   * look at deferring rx frames rather than discarding (as per tulip)
   * handle tx ring full as per tulip
   * performace test to tune rx_copybreak

   Most of my modifications relate to the braindead big-endian
   implementation by Intel.  When the i596 is operating in
   'big-endian' mode, it thinks a 32 bit value of 0x12345678
   should be stored as 0x56781234.  This is a real pain, when
   you have linked lists which are shared by the 680x0 and the
   i596.

   Driver skeleton
   Written 1993 by Donald Becker.
   Copyright 1993 United States Government as represented by the Director,
   National Security Agency. This software may only be used and distributed
   according to the terms of the GNU Public License as modified by SRC,
   incorporated herein by reference.

   The author may be reached as becker@super.org or
   C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715

 */

static const char *version = "82596.c $Revision: 1.14 $\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/irq.h>

#include <asm/pdc.h>
#include <asm/gsc.h>
#include <asm/cache.h>

/* DEBUG flags
 */

#define DEB_INIT	0x0001
#define DEB_PROBE	0x0002
#define DEB_SERIOUS	0x0004
#define DEB_ERRORS	0x0008
#define DEB_MULTI	0x0010
#define DEB_TDR		0x0020
#define DEB_OPEN	0x0040
#define DEB_RESET	0x0080
#define DEB_ADDCMD	0x0100
#define DEB_STATUS	0x0200
#define DEB_STARTTX	0x0400
#define DEB_RXADDR	0x0800
#define DEB_TXADDR	0x1000
#define DEB_RXFRAME	0x2000
#define DEB_INTS	0x4000
#define DEB_STRUCT	0x8000
#define DEB_ANY		0xffff


#define DEB(x,y)	if (i596_debug & (x)) y


#define  CHECK_WBACK(addr,len) \
	do { if (!dma_consistent) dma_cache_wback((unsigned long)addr,len); } while (0)

#define  CHECK_INV(addr,len) \
	do { if (!dma_consistent) dma_cache_inv((unsigned long)addr,len); } while(0)

#define  CHECK_WBACK_INV(addr,len) \
	do { if (!dma_consistent) dma_cache_wback_inv((unsigned long)addr,len); } while (0)


#define PA_I82596_RESET		0	/* Offsets relative to LASI-LAN-Addr.*/
#define PA_CPU_PORT_L_ACCESS	4
#define PA_CHANNEL_ATTENTION	8


/*
 * Define various macros for Channel Attention, word swapping etc., dependent
 * on architecture.  MVME and BVME are 680x0 based, otherwise it is Intel.
 */

#ifdef __BIG_ENDIAN
#define WSWAPrfd(x)  ((struct i596_rfd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPrbd(x)  ((struct i596_rbd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPiscp(x) ((struct i596_iscp *)(((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPscb(x)  ((struct i596_scb *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPcmd(x)  ((struct i596_cmd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPtbd(x)  ((struct i596_tbd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPchar(x) ((char *)            (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define ISCP_BUSY	0x00010000
#define MACH_IS_APRICOT	0
#else
#define WSWAPrfd(x)     ((struct i596_rfd *)(x))
#define WSWAPrbd(x)     ((struct i596_rbd *)(x))
#define WSWAPiscp(x)    ((struct i596_iscp *)(x))
#define WSWAPscb(x)     ((struct i596_scb *)(x))
#define WSWAPcmd(x)     ((struct i596_cmd *)(x))
#define WSWAPtbd(x)     ((struct i596_tbd *)(x))
#define WSWAPchar(x)    ((char *)(x))
#define ISCP_BUSY	0x0001
#define MACH_IS_APRICOT	1
#endif

/*
 * The MPU_PORT command allows direct access to the 82596. With PORT access
 * the following commands are available (p5-18). The 32-bit port command
 * must be word-swapped with the most significant word written first.
 * This only applies to VME boards.
 */
#define PORT_RESET		0x00	/* reset 82596 */
#define PORT_SELFTEST		0x01	/* selftest */
#define PORT_ALTSCP		0x02	/* alternate SCB address */
#define PORT_ALTDUMP		0x03	/* Alternate DUMP address */

static int i596_debug = (DEB_SERIOUS|DEB_PROBE);  

MODULE_AUTHOR("Richard Hirst");
MODULE_DESCRIPTION("i82596 driver");
MODULE_PARM(i596_debug, "i");


/* Copy frames shorter than rx_copybreak, otherwise pass on up in
 * a full sized sk_buff.  Value of 100 stolen from tulip.c (!alpha).
 */
static int rx_copybreak = 100;

#define PKT_BUF_SZ	1536
#define MAX_MC_CNT	64

#define I596_TOTAL_SIZE 17

#define I596_NULL ((void *)0xffffffff)

#define CMD_EOL		0x8000	/* The last command of the list, stop. */
#define CMD_SUSP	0x4000	/* Suspend after doing cmd. */
#define CMD_INTR	0x2000	/* Interrupt after doing cmd. */

#define CMD_FLEX	0x0008	/* Enable flexible memory model */

enum commands {
	CmdNOp = 0, CmdSASetup = 1, CmdConfigure = 2, CmdMulticastList = 3,
	CmdTx = 4, CmdTDR = 5, CmdDump = 6, CmdDiagnose = 7
};

#define STAT_C		0x8000	/* Set to 0 after execution */
#define STAT_B		0x4000	/* Command being executed */
#define STAT_OK		0x2000	/* Command executed ok */
#define STAT_A		0x1000	/* Command aborted */

#define	 CUC_START	0x0100
#define	 CUC_RESUME	0x0200
#define	 CUC_SUSPEND    0x0300
#define	 CUC_ABORT	0x0400
#define	 RX_START	0x0010
#define	 RX_RESUME	0x0020
#define	 RX_SUSPEND	0x0030
#define	 RX_ABORT	0x0040

#define TX_TIMEOUT	5

#define OPT_SWAP_PORT	0x0001	/* Need to wordswp on the MPU port */


struct i596_reg {
	unsigned short porthi;
	unsigned short portlo;
	unsigned long ca;
};

#define EOF		0x8000
#define SIZE_MASK	0x3fff

struct i596_tbd {
	unsigned short size;
	unsigned short pad;
	struct i596_tbd *next;
	char *data;
	long cache_pad[5];		/* Total 32 bytes... */
};

/* The command structure has two 'next' pointers; v_next is the address of
 * the next command as seen by the CPU, b_next is the address of the next
 * command as seen by the 82596.  The b_next pointer, as used by the 82596
 * always references the status field of the next command, rather than the
 * v_next field, because the 82596 is unaware of v_next.  It may seem more
 * logical to put v_next at the end of the structure, but we cannot do that
 * because the 82596 expects other fields to be there, depending on command
 * type.
 */

struct i596_cmd {
	struct i596_cmd *v_next;	/* Address from CPUs viewpoint */
	unsigned short status;
	unsigned short command;
	struct i596_cmd *b_next;	/* Address from i596 viewpoint */
};

struct tx_cmd {
	struct i596_cmd cmd;
	struct i596_tbd *tbd;
	unsigned short size;
	unsigned short pad;
	struct sk_buff *skb;		/* So we can free it after tx */
	dma_addr_t dma_addr;
	long cache_pad[1];		/* Total 32 bytes... */
};

struct tdr_cmd {
	struct i596_cmd cmd;
	unsigned short status;
	unsigned short pad;
};

struct mc_cmd {
	struct i596_cmd cmd;
	short mc_cnt;
	char mc_addrs[MAX_MC_CNT*6];
};

struct sa_cmd {
	struct i596_cmd cmd;
	char eth_addr[8];
};

struct cf_cmd {
	struct i596_cmd cmd;
	char i596_config[16];
};

struct i596_rfd {
	unsigned short stat;
	unsigned short cmd;
	struct i596_rfd *b_next;	/* Address from i596 viewpoint */
	struct i596_rbd *rbd;
	unsigned short count;
	unsigned short size;
	struct i596_rfd *v_next;	/* Address from CPUs viewpoint */
	struct i596_rfd *v_prev;
	long cache_pad[2];		/* Total 32 bytes... */
};

struct i596_rbd {
    unsigned short count;
    unsigned short zero1;
    struct i596_rbd *b_next;
    unsigned char *b_data;		/* Address from i596 viewpoint */
    unsigned short size;
    unsigned short zero2;
    struct sk_buff *skb;
    struct i596_rbd *v_next;
    struct i596_rbd *b_addr;		/* This rbd addr from i596 view */
    unsigned char *v_data;		/* Address from CPUs viewpoint */
					/* Total 32 bytes... */
};

/* These values as chosen so struct i596_private fits in one page... */

#define TX_RING_SIZE 32
#define RX_RING_SIZE 16

struct i596_scb {
	unsigned short status;
	unsigned short command;
	struct i596_cmd *cmd;
	struct i596_rfd *rfd;
	unsigned long crc_err;
	unsigned long align_err;
	unsigned long resource_err;
	unsigned long over_err;
	unsigned long rcvdt_err;
	unsigned long short_err;
	unsigned short t_on;
	unsigned short t_off;
};

struct i596_iscp {
	unsigned long stat;
	struct i596_scb *scb;
};

struct i596_scp {
	unsigned long sysbus;
	unsigned long pad;
	struct i596_iscp *iscp;
};

struct i596_private {
	volatile struct i596_scp scp		__attribute__((aligned(32)));
	volatile struct i596_iscp iscp		__attribute__((aligned(32)));
	volatile struct i596_scb scb		__attribute__((aligned(32)));
	struct sa_cmd sa_cmd			__attribute__((aligned(32)));
	struct cf_cmd cf_cmd			__attribute__((aligned(32)));
	struct tdr_cmd tdr_cmd			__attribute__((aligned(32)));
	struct mc_cmd mc_cmd			__attribute__((aligned(32)));
	struct i596_rfd rfds[RX_RING_SIZE]	__attribute__((aligned(32)));
	struct i596_rbd rbds[RX_RING_SIZE]	__attribute__((aligned(32)));
	struct tx_cmd tx_cmds[TX_RING_SIZE]	__attribute__((aligned(32)));
	struct i596_tbd tbds[TX_RING_SIZE]	__attribute__((aligned(32)));
	unsigned long stat;
	int last_restart;
	struct i596_rfd *rfd_head;
	struct i596_rbd *rbd_head;
	struct i596_cmd *cmd_tail;
	struct i596_cmd *cmd_head;
	int cmd_backlog;
	unsigned long last_cmd;
	struct net_device_stats stats;
	int next_tx_cmd;
	int options;
	spinlock_t lock;
	dma_addr_t dma_addr;
};

char init_setup[] =
{
	0x8E,			/* length, prefetch on */
	0xC8,			/* fifo to 8, monitor off */
	0x80,			/* don't save bad frames */
	0x2E,			/* No source address insertion, 8 byte preamble */
	0x00,			/* priority and backoff defaults */
	0x60,			/* interframe spacing */
	0x00,			/* slot time LSB */
	0xf2,			/* slot time and retries */
	0x00,			/* promiscuous mode */
	0x00,			/* collision detect */
	0x40,			/* minimum frame length */
	0xff,
	0x00,
	0x7f /*  *multi IA */ };

static int dma_consistent = 1;	/* Zero if pci_alloc_consistent() fails */

static int i596_open(struct net_device *dev);
static int i596_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void i596_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int i596_close(struct net_device *dev);
static struct net_device_stats *i596_get_stats(struct net_device *dev);
static void i596_add_cmd(struct net_device *dev, struct i596_cmd *cmd);
static void i596_tx_timeout (struct net_device *dev);
static void print_eth(unsigned char *buf, char *str);
static void set_multicast_list(struct net_device *dev);

static int rx_ring_size = RX_RING_SIZE;
static int ticks_limit = 100;
static int max_cmd_backlog = TX_RING_SIZE-1;


static inline void CA(struct net_device *dev)
{
	gsc_writel(0, (void*)(dev->base_addr + PA_CHANNEL_ATTENTION));
}


static inline void MPU_PORT(struct net_device *dev, int c, volatile void *x)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;

	u32 v = (u32) (c) | (u32) (x);

	if (lp->options & OPT_SWAP_PORT)
		v = ((u32) (v) << 16) | ((u32) (v) >> 16);

	gsc_writel(v & 0xffff, (void*)(dev->base_addr + PA_CPU_PORT_L_ACCESS));
	udelay(1);
	gsc_writel(v >> 16,    (void*)(dev->base_addr + PA_CPU_PORT_L_ACCESS));
}


static inline int wait_istat(struct net_device *dev, struct i596_private *lp, int delcnt, char *str)
{
	CHECK_INV(&(lp->iscp), sizeof(struct i596_iscp));
	while (--delcnt && lp->iscp.stat) {
		udelay(10);
		CHECK_INV(&(lp->iscp), sizeof(struct i596_iscp));
	}
	if (!delcnt) {
		printk("%s: %s, iscp.stat %04lx, didn't clear\n",
		     dev->name, str, lp->iscp.stat);
		return -1;
	}
	else
		return 0;
}


static inline int wait_cmd(struct net_device *dev, struct i596_private *lp, int delcnt, char *str)
{
	CHECK_INV(&(lp->scb), sizeof(struct i596_scb));
	while (--delcnt && lp->scb.command) {
		udelay(10);
		CHECK_INV(&(lp->scb), sizeof(struct i596_scb));
	}
	if (!delcnt) {
		printk("%s: %s, status %4.4x, cmd %4.4x.\n",
		     dev->name, str, lp->scb.status, lp->scb.command);
		return -1;
	}
	else
		return 0;
}


static void i596_display_data(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	struct i596_cmd *cmd;
	struct i596_rfd *rfd;
	struct i596_rbd *rbd;

	printk("lp and scp at %p, .sysbus = %08lx, .iscp = %p\n",
	       &lp->scp, lp->scp.sysbus, lp->scp.iscp);
	printk("iscp at %p, iscp.stat = %08lx, .scb = %p\n",
	       &lp->iscp, lp->iscp.stat, lp->iscp.scb);
	printk("scb at %p, scb.status = %04x, .command = %04x,"
		" .cmd = %p, .rfd = %p\n",
	       &lp->scb, lp->scb.status, lp->scb.command,
		lp->scb.cmd, lp->scb.rfd);
	printk("   errors: crc %lx, align %lx, resource %lx,"
               " over %lx, rcvdt %lx, short %lx\n",
		lp->scb.crc_err, lp->scb.align_err, lp->scb.resource_err,
		lp->scb.over_err, lp->scb.rcvdt_err, lp->scb.short_err);
	cmd = lp->cmd_head;
	while (cmd != I596_NULL) {
		printk("cmd at %p, .status = %04x, .command = %04x, .b_next = %p\n",
		  cmd, cmd->status, cmd->command, cmd->b_next);
		cmd = cmd->v_next;
	}
	rfd = lp->rfd_head;
	printk("rfd_head = %p\n", rfd);
	do {
		printk ("   %p .stat %04x, .cmd %04x, b_next %p, rbd %p,"
                        " count %04x\n",
			rfd, rfd->stat, rfd->cmd, rfd->b_next, rfd->rbd,
			rfd->count);
		rfd = rfd->v_next;
	} while (rfd != lp->rfd_head);
	rbd = lp->rbd_head;
	printk("rbd_head = %p\n", rbd);
	do {
		printk("   %p .count %04x, b_next %p, b_data %p, size %04x\n",
			rbd, rbd->count, rbd->b_next, rbd->b_data, rbd->size);
		rbd = rbd->v_next;
	} while (rbd != lp->rbd_head);
	CHECK_INV(lp, sizeof(struct i596_private));
}


#if defined(ENABLE_MVME16x_NET) || defined(ENABLE_BVME6000_NET)
static void i596_error(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	volatile unsigned char *pcc2 = (unsigned char *) 0xfff42000;

	pcc2[0x28] = 1;
	pcc2[0x2b] = 0x1d;
	printk("%s: Error interrupt\n", dev->name);
	i596_display_data(dev);
}
#endif

#define virt_to_dma(lp,v) ((char *)(v)-(char *)(lp)+(char *)((lp)->dma_addr))

static inline void init_rx_bufs(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *)dev->priv;
	int i;
	struct i596_rfd *rfd;
	struct i596_rbd *rbd;

	/* First build the Receive Buffer Descriptor List */

	for (i = 0, rbd = lp->rbds; i < rx_ring_size; i++, rbd++) {
		dma_addr_t dma_addr;
		struct sk_buff *skb = dev_alloc_skb(PKT_BUF_SZ + 4);

		if (skb == NULL)
			panic("82596: alloc_skb() failed");
		skb_reserve(skb, 2);
		dma_addr = pci_map_single(NULL, skb->tail,PKT_BUF_SZ,
					PCI_DMA_FROMDEVICE);
		skb->dev = dev;
		rbd->v_next = rbd+1;
		rbd->b_next = WSWAPrbd(virt_to_dma(lp,rbd+1));
		rbd->b_addr = WSWAPrbd(virt_to_dma(lp,rbd));
		rbd->skb = skb;
		rbd->v_data = skb->tail;
		rbd->b_data = WSWAPchar(dma_addr);
		rbd->size = PKT_BUF_SZ;
	}
	lp->rbd_head = lp->rbds;
	rbd = lp->rbds + rx_ring_size - 1;
	rbd->v_next = lp->rbds;
	rbd->b_next = WSWAPrbd(virt_to_dma(lp,lp->rbds));

	/* Now build the Receive Frame Descriptor List */

	for (i = 0, rfd = lp->rfds; i < rx_ring_size; i++, rfd++) {
		rfd->rbd = I596_NULL;
		rfd->v_next = rfd+1;
		rfd->v_prev = rfd-1;
		rfd->b_next = WSWAPrfd(virt_to_dma(lp,rfd+1));
		rfd->cmd = CMD_FLEX;
	}
	lp->rfd_head = lp->rfds;
	lp->scb.rfd = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	rfd = lp->rfds;
	rfd->rbd = lp->rbd_head;
	rfd->v_prev = lp->rfds + rx_ring_size - 1;
	rfd = lp->rfds + rx_ring_size - 1;
	rfd->v_next = lp->rfds;
	rfd->b_next = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	rfd->cmd = CMD_EOL|CMD_FLEX;

	CHECK_WBACK_INV(lp, sizeof(struct i596_private));
}

static inline void remove_rx_bufs(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *)dev->priv;
	struct i596_rbd *rbd;
	int i;

	for (i = 0, rbd = lp->rbds; i < rx_ring_size; i++, rbd++) {
		if (rbd->skb == NULL)
			break;
		pci_unmap_single(NULL,(dma_addr_t)WSWAPchar(rbd->b_data), PKT_BUF_SZ, PCI_DMA_FROMDEVICE);
		dev_kfree_skb(rbd->skb);
	}
}


static void rebuild_rx_bufs(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int i;

	/* Ensure rx frame/buffer descriptors are tidy */

	for (i = 0; i < rx_ring_size; i++) {
		lp->rfds[i].rbd = I596_NULL;
		lp->rfds[i].cmd = CMD_FLEX;
	}
	lp->rfds[rx_ring_size-1].cmd = CMD_EOL|CMD_FLEX;
	lp->rfd_head = lp->rfds;
	lp->scb.rfd = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	lp->rbd_head = lp->rbds;
	lp->rfds[0].rbd = WSWAPrbd(virt_to_dma(lp,lp->rbds));

	CHECK_WBACK_INV(lp, sizeof(struct i596_private));
}


static int init_i596_mem(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	unsigned long flags;

	disable_irq(dev->irq);	/* disable IRQs from LAN */
	DEB(DEB_INIT,
		printk("RESET 82596 port: %08lX (with IRQ%d disabled)\n",
		       dev->base_addr + PA_I82596_RESET,
		       dev->irq));
	
	gsc_writel(0, (void*)(dev->base_addr + PA_I82596_RESET)); /* Hard Reset */
	udelay(100);			/* Wait 100us - seems to help */

	/* change the scp address */

	lp->last_cmd = jiffies;


	lp->scp.sysbus = 0x0000006c;
	lp->scp.iscp = WSWAPiscp(virt_to_dma(lp,&(lp->iscp)));
	lp->iscp.scb = WSWAPscb(virt_to_dma(lp,&(lp->scb)));
	lp->iscp.stat = ISCP_BUSY;
	lp->cmd_backlog = 0;

	lp->cmd_head = lp->scb.cmd = I596_NULL;

	DEB(DEB_INIT,printk("%s: starting i82596.\n", dev->name));

	CHECK_WBACK(&(lp->scp), sizeof(struct i596_scp));
	CHECK_WBACK(&(lp->iscp), sizeof(struct i596_iscp));

	MPU_PORT(dev, PORT_ALTSCP, (void *)virt_to_dma(lp,&lp->scp));	

	CA(dev);

	if (wait_istat(dev,lp,1000,"initialization timed out"))
		goto failed;
	DEB(DEB_INIT,printk("%s: i82596 initialization successful\n", dev->name));

	/* Ensure rx frame/buffer descriptors are tidy */
	rebuild_rx_bufs(dev);

	lp->scb.command = 0;
	CHECK_WBACK(&(lp->scb), sizeof(struct i596_scb));

	enable_irq(dev->irq);	/* enable IRQs from LAN */

	DEB(DEB_INIT,printk("%s: queuing CmdConfigure\n", dev->name));
	memcpy(lp->cf_cmd.i596_config, init_setup, 14);
	lp->cf_cmd.cmd.command = CmdConfigure;
	CHECK_WBACK(&(lp->cf_cmd), sizeof(struct cf_cmd));
	i596_add_cmd(dev, &lp->cf_cmd.cmd);

	DEB(DEB_INIT,printk("%s: queuing CmdSASetup\n", dev->name));
	memcpy(lp->sa_cmd.eth_addr, dev->dev_addr, 6);
	lp->sa_cmd.cmd.command = CmdSASetup;
	CHECK_WBACK(&(lp->sa_cmd), sizeof(struct sa_cmd));
	i596_add_cmd(dev, &lp->sa_cmd.cmd);

	DEB(DEB_INIT,printk("%s: queuing CmdTDR\n", dev->name));
	lp->tdr_cmd.cmd.command = CmdTDR;
	CHECK_WBACK(&(lp->tdr_cmd), sizeof(struct tdr_cmd));
	i596_add_cmd(dev, &lp->tdr_cmd.cmd);

	spin_lock_irqsave (&lp->lock, flags);

	if (wait_cmd(dev,lp,1000,"timed out waiting to issue RX_START")) {
		spin_unlock_irqrestore (&lp->lock, flags);
		goto failed;
	}
	DEB(DEB_INIT,printk("%s: Issuing RX_START\n", dev->name));
	lp->scb.command = RX_START;
	lp->scb.rfd = WSWAPrfd(virt_to_dma(lp,lp->rfds));
	CHECK_WBACK(&(lp->scb), sizeof(struct i596_scb));

	CA(dev);

	spin_unlock_irqrestore (&lp->lock, flags);

	if (wait_cmd(dev,lp,1000,"RX_START not processed"))
		goto failed;
	DEB(DEB_INIT,printk("%s: Receive unit started OK\n", dev->name));

	return 0;

failed:
	printk("%s: Failed to initialise 82596\n", dev->name);
	MPU_PORT(dev, PORT_RESET, 0);
	return -1;
}


static inline int i596_rx(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *)dev->priv;
	struct i596_rfd *rfd;
	struct i596_rbd *rbd;
	int frames = 0;

	DEB(DEB_RXFRAME,printk ("i596_rx(), rfd_head %p, rbd_head %p\n",
			lp->rfd_head, lp->rbd_head));


	rfd = lp->rfd_head;		/* Ref next frame to check */

	CHECK_INV(rfd, sizeof(struct i596_rfd));
	while ((rfd->stat) & STAT_C) {	/* Loop while complete frames */
		if (rfd->rbd == I596_NULL)
			rbd = I596_NULL;
		else if (rfd->rbd == lp->rbd_head->b_addr) {
			rbd = lp->rbd_head;
			CHECK_INV(rbd, sizeof(struct i596_rbd));
		}
		else {
			printk("%s: rbd chain broken!\n", dev->name);
			/* XXX Now what? */
			rbd = I596_NULL;
		}
		DEB(DEB_RXFRAME, printk("  rfd %p, rfd.rbd %p, rfd.stat %04x\n",
			rfd, rfd->rbd, rfd->stat));
		
		if (rbd != I596_NULL && ((rfd->stat) & STAT_OK)) {
			/* a good frame */
			int pkt_len = rbd->count & 0x3fff;
			struct sk_buff *skb = rbd->skb;
			int rx_in_place = 0;

			DEB(DEB_RXADDR,print_eth(rbd->v_data, "received"));
			frames++;

			/* Check if the packet is long enough to just accept
			 * without copying to a properly sized skbuff.
			 */

			if (pkt_len > rx_copybreak) {
				struct sk_buff *newskb;
				dma_addr_t dma_addr;

				pci_unmap_single(NULL,(dma_addr_t)WSWAPchar(rbd->b_data), PKT_BUF_SZ, PCI_DMA_FROMDEVICE);
				/* Get fresh skbuff to replace filled one. */
				newskb = dev_alloc_skb(PKT_BUF_SZ + 4);
				if (newskb == NULL) {
					skb = NULL;	/* drop pkt */
					goto memory_squeeze;
				}
				skb_reserve(newskb, 2);

				/* Pass up the skb already on the Rx ring. */
				skb_put(skb, pkt_len);
				rx_in_place = 1;
				rbd->skb = newskb;
				newskb->dev = dev;
				dma_addr = pci_map_single(NULL, newskb->tail, PKT_BUF_SZ, PCI_DMA_FROMDEVICE);
				rbd->v_data = newskb->tail;
				rbd->b_data = WSWAPchar(dma_addr);
				CHECK_WBACK_INV(rbd, sizeof(struct i596_rbd));
			}
			else
				skb = dev_alloc_skb(pkt_len + 2);
memory_squeeze:
			if (skb == NULL) {
				/* XXX tulip.c can defer packets here!! */
				printk ("%s: i596_rx Memory squeeze, dropping packet.\n", dev->name);
				lp->stats.rx_dropped++;
			}
			else {
				skb->dev = dev;
				if (!rx_in_place) {
					/* 16 byte align the data fields */
					pci_dma_sync_single(NULL, (dma_addr_t)WSWAPchar(rbd->b_data), PKT_BUF_SZ, PCI_DMA_FROMDEVICE);
					skb_reserve(skb, 2);
					memcpy(skb_put(skb,pkt_len), rbd->v_data, pkt_len);
				}
				skb->len = pkt_len;
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
				lp->stats.rx_packets++;
				lp->stats.rx_bytes+=pkt_len;
			}
		}
		else {
			DEB(DEB_ERRORS, printk("%s: Error, rfd.stat = 0x%04x\n",
					dev->name, rfd->stat));
			lp->stats.rx_errors++;
			if ((rfd->stat) & 0x0001)
				lp->stats.collisions++;
			if ((rfd->stat) & 0x0080)
				lp->stats.rx_length_errors++;
			if ((rfd->stat) & 0x0100)
				lp->stats.rx_over_errors++;
			if ((rfd->stat) & 0x0200)
				lp->stats.rx_fifo_errors++;
			if ((rfd->stat) & 0x0400)
				lp->stats.rx_frame_errors++;
			if ((rfd->stat) & 0x0800)
				lp->stats.rx_crc_errors++;
			if ((rfd->stat) & 0x1000)
				lp->stats.rx_length_errors++;
		}

		/* Clear the buffer descriptor count and EOF + F flags */

		if (rbd != I596_NULL && (rbd->count & 0x4000)) {
			rbd->count = 0;
			lp->rbd_head = rbd->v_next;
			CHECK_WBACK_INV(rbd, sizeof(struct i596_rbd));
		}

		/* Tidy the frame descriptor, marking it as end of list */

		rfd->rbd = I596_NULL;
		rfd->stat = 0;
		rfd->cmd = CMD_EOL|CMD_FLEX;
		rfd->count = 0;

		/* Remove end-of-list from old end descriptor */

		rfd->v_prev->cmd = CMD_FLEX;

		/* Update record of next frame descriptor to process */

		lp->scb.rfd = rfd->b_next;
		lp->rfd_head = rfd->v_next;
		CHECK_WBACK_INV(rfd->v_prev, sizeof(struct i596_rfd));
		CHECK_WBACK_INV(rfd, sizeof(struct i596_rfd));
		rfd = lp->rfd_head;
		CHECK_INV(rfd, sizeof(struct i596_rfd));
	}

	DEB(DEB_RXFRAME,printk ("frames %d\n", frames));

	return 0;
}


static inline void i596_cleanup_cmd(struct net_device *dev, struct i596_private *lp)
{
	struct i596_cmd *ptr;

	while (lp->cmd_head != I596_NULL) {
		ptr = lp->cmd_head;
		lp->cmd_head = ptr->v_next;
		lp->cmd_backlog--;

		switch ((ptr->command) & 0x7) {
		case CmdTx:
			{
				struct tx_cmd *tx_cmd = (struct tx_cmd *) ptr;
				struct sk_buff *skb = tx_cmd->skb;
				pci_unmap_single(NULL, tx_cmd->dma_addr, skb->len, PCI_DMA_TODEVICE);

				dev_kfree_skb(skb);

				lp->stats.tx_errors++;
				lp->stats.tx_aborted_errors++;

				ptr->v_next = ptr->b_next = I596_NULL;
				tx_cmd->cmd.command = 0;  /* Mark as free */
				break;
			}
		default:
			ptr->v_next = ptr->b_next = I596_NULL;
		}
		CHECK_WBACK_INV(ptr, sizeof(struct i596_cmd));
	}

	wait_cmd(dev,lp,100,"i596_cleanup_cmd timed out");
	lp->scb.cmd = I596_NULL;
	CHECK_WBACK(&(lp->scb), sizeof(struct i596_scb));
}


static inline void i596_reset(struct net_device *dev, struct i596_private *lp, int ioaddr)
{
	unsigned long flags;

	DEB(DEB_RESET,printk("i596_reset\n"));

	spin_lock_irqsave (&lp->lock, flags);

	wait_cmd(dev,lp,100,"i596_reset timed out");

	netif_stop_queue(dev);

	/* FIXME: this command might cause an lpmc */
	lp->scb.command = CUC_ABORT | RX_ABORT;
	CHECK_WBACK(&(lp->scb), sizeof(struct i596_scb));
	CA(dev);

	/* wait for shutdown */
	wait_cmd(dev,lp,1000,"i596_reset 2 timed out");
	spin_unlock_irqrestore (&lp->lock, flags);

	i596_cleanup_cmd(dev,lp);
	i596_rx(dev);

	netif_start_queue(dev);
	init_i596_mem(dev);
}


static void i596_add_cmd(struct net_device *dev, struct i596_cmd *cmd)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;

	DEB(DEB_ADDCMD,printk("i596_add_cmd cmd_head %p\n", lp->cmd_head));

	cmd->status = 0;
	cmd->command |= (CMD_EOL | CMD_INTR);
	cmd->v_next = cmd->b_next = I596_NULL;
	CHECK_WBACK(cmd, sizeof(struct i596_cmd));

	spin_lock_irqsave (&lp->lock, flags);

	if (lp->cmd_head != I596_NULL) {
		lp->cmd_tail->v_next = cmd;
		lp->cmd_tail->b_next = WSWAPcmd(virt_to_dma(lp,&cmd->status));
		CHECK_WBACK(lp->cmd_tail, sizeof(struct i596_cmd));
	} else {
		lp->cmd_head = cmd;
		wait_cmd(dev,lp,100,"i596_add_cmd timed out");
		lp->scb.cmd = WSWAPcmd(virt_to_dma(lp,&cmd->status));
		lp->scb.command = CUC_START;
		CHECK_WBACK(&(lp->scb), sizeof(struct i596_scb));
		CA(dev);
	}
	lp->cmd_tail = cmd;
	lp->cmd_backlog++;

	spin_unlock_irqrestore (&lp->lock, flags);

	if (lp->cmd_backlog > max_cmd_backlog) {
		unsigned long tickssofar = jiffies - lp->last_cmd;

		if (tickssofar < ticks_limit)
			return;

		printk("%s: command unit timed out, status resetting.\n", dev->name);
#if 1
		i596_reset(dev, lp, ioaddr);
#endif
	}
}

#if 0
/* this function makes a perfectly adequate probe...  but we have a
   device list */
static int i596_test(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	volatile int *tint;
	u32 data;

	tint = (volatile int *)(&(lp->scp));
	data = virt_to_dma(lp,tint);
	
	tint[1] = -1;
	CHECK_WBACK(tint,PAGE_SIZE);

	MPU_PORT(dev, 1, data);

	for(data = 1000000; data; data--) {
		CHECK_INV(tint,PAGE_SIZE);
		if(tint[1] != -1)
			break;

	}

	printk("i596_test result %d\n", tint[1]);

}
#endif


static int i596_open(struct net_device *dev)
{
	int res = 0;

	DEB(DEB_OPEN,printk("%s: i596_open() irq %d.\n", dev->name, dev->irq));

	if (request_irq(dev->irq, &i596_interrupt, 0, "i82596", dev)) {
		printk("%s: IRQ %d not free\n", dev->name, dev->irq);
		return -EAGAIN;
	}

	request_region(dev->base_addr, 12, dev->name);

	init_rx_bufs(dev);

	netif_start_queue(dev);

	MOD_INC_USE_COUNT;

	/* Initialize the 82596 memory */
	if (init_i596_mem(dev)) {
		res = -EAGAIN;
		free_irq(dev->irq, dev);
	}

	return res;
}

static void i596_tx_timeout (struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int ioaddr = dev->base_addr;

	/* Transmitter timeout, serious problems. */
	DEB(DEB_ERRORS,printk("%s: transmit timed out, status resetting.\n",
			dev->name));

	lp->stats.tx_errors++;

	/* Try to restart the adaptor */
	if (lp->last_restart == lp->stats.tx_packets) {
		DEB(DEB_ERRORS,printk ("Resetting board.\n"));
		/* Shutdown and restart */
		i596_reset (dev, lp, ioaddr);
	} else {
		/* Issue a channel attention signal */
		DEB(DEB_ERRORS,printk ("Kicking board.\n"));
		lp->scb.command = CUC_START | RX_START;
		CHECK_WBACK_INV(&(lp->scb), sizeof(struct i596_scb));
		CA (dev);
		lp->last_restart = lp->stats.tx_packets;
	}

	dev->trans_start = jiffies;
	netif_wake_queue (dev);
}


static int i596_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	struct tx_cmd *tx_cmd;
	struct i596_tbd *tbd;
	short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	dev->trans_start = jiffies;

	DEB(DEB_STARTTX,printk("%s: i596_start_xmit(%x,%x) called\n", dev->name,
				skb->len, (unsigned int)skb->data));

	netif_stop_queue(dev);

	tx_cmd = lp->tx_cmds + lp->next_tx_cmd;
	tbd = lp->tbds + lp->next_tx_cmd;

	if (tx_cmd->cmd.command) {
		DEB(DEB_ERRORS,printk ("%s: xmit ring full, dropping packet.\n",
				dev->name));
		lp->stats.tx_dropped++;

		dev_kfree_skb(skb);
	} else {
		if (++lp->next_tx_cmd == TX_RING_SIZE)
			lp->next_tx_cmd = 0;
		tx_cmd->tbd = WSWAPtbd(virt_to_dma(lp,tbd));
		tbd->next = I596_NULL;

		tx_cmd->cmd.command = CMD_FLEX | CmdTx;
		tx_cmd->skb = skb;

		tx_cmd->pad = 0;
		tx_cmd->size = 0;
		tbd->pad = 0;
		tbd->size = EOF | length;

		tx_cmd->dma_addr = pci_map_single(NULL, skb->data, skb->len, 
				PCI_DMA_TODEVICE);
		tbd->data = WSWAPchar(tx_cmd->dma_addr);

		DEB(DEB_TXADDR,print_eth(skb->data, "tx-queued"));
		CHECK_WBACK_INV(tx_cmd, sizeof(struct tx_cmd));
		CHECK_WBACK_INV(tbd, sizeof(struct i596_tbd));
		i596_add_cmd(dev, &tx_cmd->cmd);

		lp->stats.tx_packets++;
		lp->stats.tx_bytes += length;
	}

	netif_start_queue(dev);

	return 0;
}

static void print_eth(unsigned char *add, char *str)
{
	int i;

	printk("i596 0x%p, ", add);
	for (i = 0; i < 6; i++)
		printk(" %02X", add[i + 6]);
	printk(" -->");
	for (i = 0; i < 6; i++)
		printk(" %02X", add[i]);
	printk(" %02X%02X, %s\n", add[12], add[13], str);
}


#define LAN_PROM_ADDR	0xF0810000

static int __init i82596_probe(struct net_device *dev, int options)
{
	int i;
	struct i596_private *lp;
	char eth_addr[6];
	dma_addr_t dma_addr;

	/* This lot is ensure things have been cache line aligned. */
	if (sizeof(struct i596_rfd) != 32) {
	    printk("82596: sizeof(struct i596_rfd) = %d\n",
			    sizeof(struct i596_rfd));
	    return -ENODEV;
	}
	if (sizeof(struct i596_rbd) != 32) {
	    printk("82596: sizeof(struct i596_rbd) = %d\n",
			    sizeof(struct i596_rbd));
	    return -ENODEV;
	}
	if (sizeof(struct tx_cmd) != 32) {
	    printk("82596: sizeof(struct tx_cmd) = %d\n",
			    sizeof(struct tx_cmd));
	    return -ENODEV;
	}
	if (sizeof(struct i596_tbd) != 32) {
	    printk("82596: sizeof(struct i596_tbd) = %d\n",
			    sizeof(struct i596_tbd));
	    return -ENODEV;
	}
	if (sizeof(struct i596_private) > 4096) {
	    printk("82596: sizeof(struct i596_private) = %d\n",
			    sizeof(struct i596_private));
	    return -ENODEV;
	}

	/* FIXME:
	    Currently this works only, if set-up from lasi.c.
	    This should be changed to use probing too !
	*/

	if (!dev->base_addr || !dev->irq)
	    return -ENODEV;

	if (!pdc_lan_station_id( (char*)&eth_addr, (void*)dev->base_addr)) {
	    for(i=0;i<6;i++)
		eth_addr[i] = gsc_readb(LAN_PROM_ADDR+i);
	    printk("82596.c: MAC of HP700 LAN blindely read from the prom!\n");
	}

	dev->mem_start = (int)pci_alloc_consistent( NULL, 
		sizeof(struct i596_private), &dma_addr);
	if (!dev->mem_start) {
		printk("%s: Couldn't get consistent shared memory\n", dev->name);
		dma_consistent = 0;
		dev->mem_start = (int)__get_free_pages(GFP_ATOMIC, 0);
		if (!dev->mem_start) {
			printk("%s: Couldn't get shared memory\n", dev->name);
#ifdef ENABLE_APRICOT
			release_region(dev->base_addr, I596_TOTAL_SIZE);
#endif
			return -ENOMEM;
		}
		dma_addr = virt_to_bus(dev->mem_start);
	}

	ether_setup(dev);
	DEB(DEB_PROBE,printk("%s: 82596 at %#3lx,", dev->name, dev->base_addr));

	for (i = 0; i < 6; i++)
		DEB(DEB_PROBE,printk(" %2.2X", dev->dev_addr[i] = eth_addr[i]));

	DEB(DEB_PROBE,printk(" IRQ %d.\n", dev->irq));

	DEB(DEB_PROBE,printk(version));

	/* The 82596-specific entries in the device structure. */
	dev->open = i596_open;
	dev->stop = i596_close;
	dev->hard_start_xmit = i596_start_xmit;
	dev->get_stats = i596_get_stats;
	dev->set_multicast_list = set_multicast_list;
	dev->tx_timeout = i596_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->priv = (void *)(dev->mem_start);

	lp = (struct i596_private *) dev->priv;
	DEB(DEB_INIT,printk ("%s: lp at 0x%08lx (%d bytes), lp->scb at 0x%08lx\n",
			dev->name, (unsigned long)lp,
			sizeof(struct i596_private), (unsigned long)&lp->scb));
	memset((void *) lp, 0, sizeof(struct i596_private));

#if 0
	kernel_set_cachemode((void *)(dev->mem_start), 4096, IOMAP_NOCACHE_SER);
#endif
	lp->options = options;
	lp->scb.command = 0;
	lp->scb.cmd = I596_NULL;
	lp->scb.rfd = I596_NULL;
	lp->lock = SPIN_LOCK_UNLOCKED;
	lp->dma_addr = dma_addr;

	CHECK_WBACK_INV(dev->mem_start, sizeof(struct i596_private));

	return 0;
}


int __init lasi_i82596_probe(struct net_device *dev)
{
	return i82596_probe(dev, 0);
}


int __init asp_i82596_probe(struct net_device *dev)
{
	return i82596_probe(dev, OPT_SWAP_PORT);
}


static void i596_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct i596_private *lp;
	unsigned short status, ack_cmd = 0;

	if (dev == NULL) {
		printk("i596_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	lp = (struct i596_private *) dev->priv;

	spin_lock (&lp->lock);

	wait_cmd(dev,lp,100,"i596 interrupt, timeout");
	status = lp->scb.status;

	DEB(DEB_INTS,printk("%s: i596 interrupt, IRQ %d, status %4.4x.\n",
			dev->name, irq, status));

	ack_cmd = status & 0xf000;

	if (!ack_cmd) {
		DEB(DEB_ERRORS, printk("%s: interrupt with no events\n", dev->name));
		spin_unlock (&lp->lock);
		return;
	}

	if ((status & 0x8000) || (status & 0x2000)) {
		struct i596_cmd *ptr;

		if ((status & 0x8000))
			DEB(DEB_INTS,printk("%s: i596 interrupt completed command.\n", dev->name));
		if ((status & 0x2000))
			DEB(DEB_INTS,printk("%s: i596 interrupt command unit inactive %x.\n", dev->name, status & 0x0700));

		while (lp->cmd_head != I596_NULL) {
			CHECK_INV(lp->cmd_head, sizeof(struct i596_cmd));
			if (!(lp->cmd_head->status & STAT_C))
				break;

			ptr = lp->cmd_head;

			DEB(DEB_STATUS,printk("cmd_head->status = %04x, ->command = %04x\n",
				       lp->cmd_head->status, lp->cmd_head->command));
			lp->cmd_head = ptr->v_next;
			lp->cmd_backlog--;

			switch ((ptr->command) & 0x7) {
			case CmdTx:
			    {
				struct tx_cmd *tx_cmd = (struct tx_cmd *) ptr;
				struct sk_buff *skb = tx_cmd->skb;

				if ((ptr->status) & STAT_OK) {
					DEB(DEB_TXADDR,print_eth(skb->data, "tx-done"));
				} else {
					lp->stats.tx_errors++;
					if ((ptr->status) & 0x0020)
						lp->stats.collisions++;
					if (!((ptr->status) & 0x0040))
						lp->stats.tx_heartbeat_errors++;
					if ((ptr->status) & 0x0400)
						lp->stats.tx_carrier_errors++;
					if ((ptr->status) & 0x0800)
						lp->stats.collisions++;
					if ((ptr->status) & 0x1000)
						lp->stats.tx_aborted_errors++;
				}
				pci_unmap_single(NULL, tx_cmd->dma_addr, skb->len, PCI_DMA_TODEVICE);
				dev_kfree_skb_irq(skb);

				tx_cmd->cmd.command = 0; /* Mark free */
				break;
			    }
			case CmdTDR:
			    {
				unsigned short status = ((struct tdr_cmd *)ptr)->status;

				if (status & 0x8000) {
					DEB(DEB_ANY,printk("%s: link ok.\n", dev->name));
				} else {
					if (status & 0x4000)
						printk("%s: Transceiver problem.\n", dev->name);
					if (status & 0x2000)
						printk("%s: Termination problem.\n", dev->name);
					if (status & 0x1000)
						printk("%s: Short circuit.\n", dev->name);

					DEB(DEB_TDR,printk("%s: Time %d.\n", dev->name, status & 0x07ff));
				}
				break;
			    }
			case CmdConfigure:
				/* Zap command so set_multicast_list() knows it is free */
				ptr->command = 0;
				break;
			}
			ptr->v_next = ptr->b_next = I596_NULL;
			CHECK_WBACK(ptr, sizeof(struct i596_cmd));
			lp->last_cmd = jiffies;
		}

		/* This mess is arranging that only the last of any outstanding
		 * commands has the interrupt bit set.  Should probably really
		 * only add to the cmd queue when the CU is stopped.
		 */
		ptr = lp->cmd_head;
		while ((ptr != I596_NULL) && (ptr != lp->cmd_tail)) {
			struct i596_cmd *prev = ptr;

			ptr->command &= 0x1fff;
			ptr = ptr->v_next;
			CHECK_WBACK_INV(prev, sizeof(struct i596_cmd));
		}

		if ((lp->cmd_head != I596_NULL))
			ack_cmd |= CUC_START;
		lp->scb.cmd = WSWAPcmd(virt_to_dma(lp,&lp->cmd_head->status));
		CHECK_WBACK_INV(&lp->scb, sizeof(struct i596_scb));
	}
	if ((status & 0x1000) || (status & 0x4000)) {
		if ((status & 0x4000))
			DEB(DEB_INTS,printk("%s: i596 interrupt received a frame.\n", dev->name));
		i596_rx(dev);
		/* Only RX_START if stopped - RGH 07-07-96 */
		if (status & 0x1000) {
			if (netif_running(dev)) {
				DEB(DEB_ERRORS,printk("%s: i596 interrupt receive unit inactive, status 0x%x\n", dev->name, status));
				ack_cmd |= RX_START;
				lp->stats.rx_errors++;
				lp->stats.rx_fifo_errors++;
				rebuild_rx_bufs(dev);
			}
		}
	}
	wait_cmd(dev,lp,100,"i596 interrupt, timeout");
	lp->scb.command = ack_cmd;
	CHECK_WBACK(&lp->scb, sizeof(struct i596_scb));

	/* DANGER: I suspect that some kind of interrupt
	 acknowledgement aside from acking the 82596 might be needed 
	 here...  but it's running acceptably without */

	CA(dev);

	wait_cmd(dev,lp,100,"i596 interrupt, exit timeout");
	DEB(DEB_INTS,printk("%s: exiting interrupt.\n", dev->name));

	spin_unlock (&lp->lock);
	return;
}

static int i596_close(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	unsigned long flags;

	netif_stop_queue(dev);

	DEB(DEB_INIT,printk("%s: Shutting down ethercard, status was %4.4x.\n",
		       dev->name, lp->scb.status));

	save_flags(flags);
	cli();

	wait_cmd(dev,lp,100,"close1 timed out");
	lp->scb.command = CUC_ABORT | RX_ABORT;
	CHECK_WBACK(&lp->scb, sizeof(struct i596_scb));

	CA(dev);

	wait_cmd(dev,lp,100,"close2 timed out");
	restore_flags(flags);
	DEB(DEB_STRUCT,i596_display_data(dev));
	i596_cleanup_cmd(dev,lp);

	disable_irq(dev->irq);

	free_irq(dev->irq, dev);
	remove_rx_bufs(dev);

	release_region(dev->base_addr, 12);

	MOD_DEC_USE_COUNT;

	return 0;
}

static struct net_device_stats *
 i596_get_stats(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;

	return &lp->stats;
}

/*
 *    Set or clear the multicast filter for this adaptor.
 */

static void set_multicast_list(struct net_device *dev)
{
	struct i596_private *lp = (struct i596_private *) dev->priv;
	int config = 0, cnt;

	DEB(DEB_MULTI,printk("%s: set multicast list, %d entries, promisc %s, allmulti %s\n", dev->name, dev->mc_count, dev->flags & IFF_PROMISC ? "ON" : "OFF", dev->flags & IFF_ALLMULTI ? "ON" : "OFF"));

	if ((dev->flags & IFF_PROMISC) && !(lp->cf_cmd.i596_config[8] & 0x01)) {
		lp->cf_cmd.i596_config[8] |= 0x01;
		config = 1;
	}
	if (!(dev->flags & IFF_PROMISC) && (lp->cf_cmd.i596_config[8] & 0x01)) {
		lp->cf_cmd.i596_config[8] &= ~0x01;
		config = 1;
	}
	if ((dev->flags & IFF_ALLMULTI) && (lp->cf_cmd.i596_config[11] & 0x20)) {
		lp->cf_cmd.i596_config[11] &= ~0x20;
		config = 1;
	}
	if (!(dev->flags & IFF_ALLMULTI) && !(lp->cf_cmd.i596_config[11] & 0x20)) {
		lp->cf_cmd.i596_config[11] |= 0x20;
		config = 1;
	}
	if (config) {
		if (lp->cf_cmd.cmd.command)
			printk("%s: config change request already queued\n",
			       dev->name);
		else {
			lp->cf_cmd.cmd.command = CmdConfigure;
			CHECK_WBACK_INV(&lp->cf_cmd, sizeof(struct cf_cmd));
			i596_add_cmd(dev, &lp->cf_cmd.cmd);
		}
	}

	cnt = dev->mc_count;
	if (cnt > MAX_MC_CNT)
	{
		cnt = MAX_MC_CNT;
		printk("%s: Only %d multicast addresses supported",
			dev->name, cnt);
	}
	
	if (dev->mc_count > 0) {
		struct dev_mc_list *dmi;
		unsigned char *cp;
		struct mc_cmd *cmd;

		cmd = &lp->mc_cmd;
		cmd->cmd.command = CmdMulticastList;
		cmd->mc_cnt = dev->mc_count * 6;
		cp = cmd->mc_addrs;
		for (dmi = dev->mc_list; cnt && dmi != NULL; dmi = dmi->next, cnt--, cp += 6) {
			memcpy(cp, dmi->dmi_addr, 6);
			if (i596_debug > 1)
				DEB(DEB_MULTI,printk("%s: Adding address %02x:%02x:%02x:%02x:%02x:%02x\n",
						dev->name, cp[0],cp[1],cp[2],cp[3],cp[4],cp[5]));
		}
		CHECK_WBACK_INV(&lp->mc_cmd, sizeof(struct mc_cmd));
		i596_add_cmd(dev, &cmd->cmd);
	}
}

#ifdef HAVE_DEVLIST
static unsigned int i596_portlist[] __initdata =
{0x300, 0};
struct netdev_entry i596_drv =
{"lasi_i82596", lasi_i82596_probe, I596_TOTAL_SIZE, i596_portlist};
#endif

#ifdef MODULE
static char devicename[9] =
{0,};
static struct net_device dev_82596 =
{
	devicename,	/* device name inserted by drivers/net/net_init.c */
	0, 0, 0, 0,
	0, 0,		/* base, irq */
	0, 0, 0, NULL, lasi_i82596_probe};


MODULE_PARM(debug, "i");
static int debug = -1;

int init_module(void)
{
	if (debug >= 0)
		i596_debug = debug;
	if (register_netdev(&dev_82596) != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(&dev_82596);
	lp = (struct i596_private *) dev_82596.priv;

	if (dma_consistent)
		pci_free_consistent( NULL, sizeof( struct i596_private), 
			dev_82596.mem_start, lp->dma_addr);
	else
		free_page ((u32)(dev_82596.mem_start));

	dev_82596.priv = NULL;
}

#endif				/* MODULE */

