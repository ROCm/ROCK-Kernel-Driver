/* ibmtr.c:  A shared-memory IBM Token Ring 16/4 driver for linux
 *
 *	Written 1993 by Mark Swanson and Peter De Schrijver.
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 *	This device driver should work with Any IBM Token Ring Card that does
 *	not use DMA.
 *
 *	I used Donald Becker's (becker@cesdis.gsfc.nasa.gov) device driver work
 *	as a base for most of my initial work.
 *
 *	Changes by Peter De Schrijver (Peter.Deschrijver@linux.cc.kuleuven.ac.be) :
 *
 *	+ changed name to ibmtr.c in anticipation of other tr boards.
 *	+ changed reset code and adapter open code.
 *	+ added SAP open code.
 *	+ a first attempt to write interrupt, transmit and receive routines.
 *
 *	Changes by David W. Morris (dwm@shell.portal.com) :
 *	941003 dwm: - Restructure tok_probe for multiple adapters, devices.
 *	+ Add comments, misc reorg for clarity.
 *	+ Flatten interrupt handler levels.
 *
 *	Changes by Farzad Farid (farzy@zen.via.ecp.fr)
 *	and Pascal Andre (andre@chimay.via.ecp.fr) (March 9 1995) :
 *	+ multi ring support clean up.
 *	+ RFC1042 compliance enhanced.
 *
 *	Changes by Pascal Andre (andre@chimay.via.ecp.fr) (September 7 1995) :
 *	+ bug correction in tr_tx
 *	+ removed redundant information display
 *	+ some code reworking
 *
 *	Changes by Michel Lespinasse (walken@via.ecp.fr),
 *	Yann Doussot (doussot@via.ecp.fr) and Pascal Andre (andre@via.ecp.fr)
 *	(February 18, 1996) :
 *	+ modified shared memory and mmio access port the driver to
 *	  alpha platform (structure access -> readb/writeb)
 *
 *	Changes by Steve Kipisz (bungy@ibm.net or kipisz@vnet.ibm.com)
 *	(January 18 1996):
 *	+ swapped WWOR and WWCR in ibmtr.h
 *	+ moved some init code from tok_probe into trdev_init.  The
 *	  PCMCIA code can call trdev_init to complete initializing
 *	  the driver.
 *	+ added -DPCMCIA to support PCMCIA
 *	+ detecting PCMCIA Card Removal in interrupt handler.  If
 *	  ISRP is FF, then a PCMCIA card has been removed
 *
 *	Changes by Paul Norton (pnorton@cts.com) :
 *	+ restructured the READ.LOG logic to prevent the transmit SRB
 *	  from being rudely overwritten before the transmit cycle is
 *	  complete. (August 15 1996)
 *	+ completed multiple adapter support. (November 20 1996)
 *	+ implemented csum_partial_copy in tr_rx and increased receive 
 *        buffer size and count. Minor fixes. (March 15, 1997)
 *
 *	Changes by Christopher Turcksin <wabbit@rtfc.demon.co.uk>
 *	+ Now compiles ok as a module again.
 *
 *	Changes by Paul Norton (pnorton@ieee.org) :
 *      + moved the header manipulation code in tr_tx and tr_rx to
 *        net/802/tr.c. (July 12 1997)
 *      + add retry and timeout on open if cable disconnected. (May 5 1998)
 *      + lifted 2000 byte mtu limit. now depends on shared-RAM size.
 *        May 25 1998)
 *      + can't allocate 2k recv buff at 8k shared-RAM. (20 October 1998)
 *
 *      Changes by Joel Sloan (jjs@c-me.com) :
 *      + disable verbose debug messages by default - to enable verbose
 *	  debugging, edit the IBMTR_DEBUG_MESSAGES define below 
 *	
 *	Changes by Mike Phillips <phillim@amtrak.com> :
 *	+ Added extra #ifdef's to work with new PCMCIA Token Ring Code.
 *	  The PCMCIA code now just sets up the card so it can be recognized
 *        by ibmtr_probe. Also checks allocated memory vs. on-board memory
 *	  for correct figure to use.
 *
 *	Changes by Tim Hockin (thockin@isunix.it.ilstu.edu) :
 *	+ added spinlocks for SMP sanity (10 March 1999)
 *
 *      Changes by Jochen Friedrich to enable RFC1469 Option 2 multicasting
 *      i.e. using functional address C0 00 00 04 00 00 to transmit and 
 *      receive multicast packets.
 * 
 *      Changes by Mike Sullivan (based on original sram patch by Dave Grothe
 *      to support windowing into on adapter shared ram.
 *      i.e. Use LANAID to setup a PnP configuration with 16K RAM. Paging
 *      will shift this 16K window over the entire available shared RAM.
 *
 *      Changes by Peter De Schrijver (p2@mind.be) :
 *      + fixed a problem with PCMCIA card removal
 */

/* change the define of IBMTR_DEBUG_MESSAGES to a nonzero value 
in the event that chatty debug messages are desired - jjs 12/30/98 */

#define IBMTR_DEBUG_MESSAGES 0

#include <linux/module.h>

#define NO_AUTODETECT 1
#undef NO_AUTODETECT
/* #undef ENABLE_PAGING */
#define ENABLE_PAGING 1


#define FALSE 0
#define TRUE (!FALSE)

/* changes the output format of driver initialisation */
#define TR_NEWFORMAT	1
#define TR_VERBOSE	0

/* some 95 OS send many non UI frame; this allow removing the warning */
#define TR_FILTERNONUI	1

/* version and credits */
static char *version =
"ibmtr.c: v1.3.57   8/ 7/94 Peter De Schrijver and Mark Swanson\n"
"         v2.1.125 10/20/98 Paul Norton <pnorton@ieee.org>\n"
"         v2.2.0   12/30/98 Joel Sloan <jjs@c-me.com>\n"
"         v2.2.1   02/08/00 Mike Sullivan <sullivam@us.ibm.com>\n";

static char pcchannelid[] = {
	0x05, 0x00, 0x04, 0x09,
	0x04, 0x03, 0x04, 0x0f,
	0x03, 0x06, 0x03, 0x01,
	0x03, 0x01, 0x03, 0x00,
	0x03, 0x09, 0x03, 0x09,
	0x03, 0x00, 0x02, 0x00
};

static char mcchannelid[] = {
	0x04, 0x0d, 0x04, 0x01,
	0x05, 0x02, 0x05, 0x03,
	0x03, 0x06, 0x03, 0x03,
	0x05, 0x08, 0x03, 0x04,
	0x03, 0x05, 0x03, 0x01,
	0x03, 0x08, 0x02, 0x00
};

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/ibmtr.h>
#include <net/checksum.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>



#define DPRINTK(format, args...) printk("%s: " format, dev->name , ## args)
#define DPRINTD(format, args...) DummyCall("%s: " format, dev->name , ## args)
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

#if TR_NEWFORMAT
/* this allows displaying full adapter information */

const char *channel_def[] __initdata = { 
	"ISA", "MCA", "ISA P&P" 
};

char __init *adapter_def(char type)
{
	switch (type) 
	{
	      case 0xF : return "PC Adapter | PC Adapter II | Adapter/A";
	      case 0xE : return "16/4 Adapter | 16/4 Adapter/A (long)";
	      case 0xD : return "16/4 Adapter/A (short) | 16/4 ISA-16 Adapter";
	      case 0xC : return "Auto 16/4 Adapter";
	      default  : return "adapter (unknown type)";
	};
};
#endif

#if !TR_NEWFORMAT
unsigned char ibmtr_debug_trace=1;  /*  Patch or otherwise alter to
                                         control tokenring tracing.  */
#else
unsigned char ibmtr_debug_trace=0;
#endif
#define TRC_INIT 0x01              /*  Trace initialization & PROBEs */
#define TRC_INITV 0x02             /*  verbose init trace points     */

int		ibmtr_probe(struct net_device *dev);
static int	ibmtr_probe1(struct net_device *dev, int ioaddr);
static unsigned char	get_sram_size(struct tok_info *adapt_info);
#ifdef PCMCIA
extern unsigned char 	pcmcia_reality_check(unsigned char gss);
#endif
static int	tok_init_card(struct net_device *dev);
void		tok_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int	trdev_init(struct net_device *dev);
static void 	initial_tok_int(struct net_device *dev);
static void 	open_sap(unsigned char type,struct net_device *dev);
void		tok_open_adapter(unsigned long dev_addr);
static		void tr_rx(struct net_device *dev);
static		void tr_tx(struct net_device *dev);
static int	tok_open(struct net_device *dev);
static int	tok_close(struct net_device *dev);
static int	tok_send_packet(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats * tok_get_stats(struct net_device *dev);
static void	tok_set_multicast_list(struct net_device *dev);
void		ibmtr_readlog(struct net_device *dev);
void		ibmtr_reset_timer(struct timer_list *tmr, struct net_device *dev);
int             ibmtr_change_mtu(struct net_device *dev, int mtu);

static unsigned int ibmtr_portlist[] __initdata = {
	0xa20, 0xa24, 0
};

static __u32 ibmtr_mem_base = 0xd0000;

static void __init PrtChanID(char *pcid, short stride) 
{
	short i, j;
	for (i=0, j=0; i<24; i++, j+=stride)
		printk("%1x", ((int) pcid[j]) & 0x0f);
	printk("\n");
}

static void __init HWPrtChanID (__u32 pcid, short stride)
{
	short i, j;
	for (i=0, j=0; i<24; i++, j+=stride)
		printk("%1x", ((int)isa_readb(pcid + j)) & 0x0f);
	printk("\n");
}

/*
 *	ibmtr_probe():  Routine specified in the network device structure
 *	to probe for an IBM Token Ring Adapter.  Routine outline:
 *	I.    Interrogate hardware to determine if an adapter exists
 *	      and what the speeds and feeds are
 *	II.   Setup data structures to control execution based upon
 *	      adapter characteristics.
 *	III.  Initialize adapter operation
 *
 *	We expect ibmtr_probe to be called once for each device entry
 *	which references it.
 */
 
int __init ibmtr_probe(struct net_device *dev)
{
        int i;
        int base_addr = dev ? dev->base_addr : 0;

        if (base_addr > 0x1ff) 
        { 
        	/*
        	 *	Check a single specified location. 
 		 */
 		 
	        if (ibmtr_probe1(dev, base_addr)) 
		       return -ENODEV;
		else
		       return 0;
	}
        else if (base_addr != 0)   /* Don't probe at all. */
	        return -ENXIO;

	for (i = 0; ibmtr_portlist[i]; i++) 
	{
	        int ioaddr = ibmtr_portlist[i];
		if (check_region(ioaddr, IBMTR_IO_EXTENT))
		        continue;
		if (!ibmtr_probe1(dev, ioaddr))
			return 0;
        }

        return -ENODEV;
}

static int __init ibmtr_probe1(struct net_device *dev, int PIOaddr)
{
	unsigned char segment=0, intr=0, irq=0, i=0, j=0, cardpresent=NOTOK,temp=0;
	__u32 t_mmio=0;
	struct tok_info *ti=0;
	__u32 cd_chanid;
	unsigned char *tchanid, ctemp;
#ifndef PCMCIA
	unsigned long timeout;
#endif

#ifndef MODULE
#ifndef PCMCIA
	dev = init_trdev(dev,0);
#endif
#endif

	/*	Query the adapter PIO base port which will return
	 *	indication of where MMIO was placed. We also have a
	 *	coded interrupt number.
	 */

       	segment = inb(PIOaddr);

	/*
	 *	Out of range values so we'll assume non-existent IO device 
	 */
	 
	if (segment < 0x40 || segment > 0xe0)
                return -ENODEV;

	/*
	 *	Compute the linear base address of the MMIO area
	 *	as LINUX doesn't care about segments
	 */

	t_mmio=(((__u32)(segment & 0xfc) << 11) + 0x80000);
	intr = segment & 0x03;   /* low bits is coded interrupt # */
	if (ibmtr_debug_trace & TRC_INIT)
		DPRINTK("PIOaddr: %4hx seg/intr: %2x mmio base: %08X intr: %d\n",
                         PIOaddr, (int)segment, t_mmio, (int)intr);

	/*
	 *	Now we will compare expected 'channelid' strings with
	 *	what we is there to learn of ISA/MCA or not TR card
	 */
	 
	cd_chanid = (CHANNEL_ID + t_mmio);  /* for efficiency */
	tchanid=pcchannelid;
	cardpresent=TR_ISA;  /* try ISA */

	/*
	 *	Suboptimize knowing first byte different
	 */

	ctemp = isa_readb(cd_chanid) & 0x0f;
	if (ctemp != *tchanid) { /* NOT ISA card, try MCA */
		tchanid=mcchannelid;
		cardpresent=TR_MCA;
		if (ctemp != *tchanid)  /* Neither ISA nor MCA */
			cardpresent=NOTOK;
	}

	if (cardpresent != NOTOK) 
	{
		/* 
		 *	Know presumed type, try rest of ID 
		 */
		for (i=2,j=1; i<=46; i=i+2,j++) 
		{
			if ((isa_readb(cd_chanid+i) & 0x0f) != tchanid[j]) {
				cardpresent=NOTOK;   /* match failed, not TR card */
				break;
			}
		}
	}

	/* 
	 *	If we have an ISA board check for the ISA P&P version,
	 *	as it has different IRQ settings 
	 */

	if (cardpresent == TR_ISA && (isa_readb(AIPFID + t_mmio)==0x0e))
	        cardpresent=TR_ISAPNP;

	if (cardpresent == NOTOK) { /* "channel_id" did not match, report */
		if (ibmtr_debug_trace & TRC_INIT) {
			DPRINTK("Channel ID string not found for PIOaddr: %4hx\n", PIOaddr);
			DPRINTK("Expected for ISA: ");  PrtChanID(pcchannelid,1);
			DPRINTK("           found: ");  HWPrtChanID(cd_chanid,2);
			DPRINTK("Expected for MCA: ");  PrtChanID(mcchannelid,1);
		}
                return -ENODEV;
	}

	/* Now, allocate some of the pl0 buffers for this driver.. */

	/* If called from PCMCIA, ti is already set up, so no need to 
	   waste the memory, just use the existing structure */

#ifndef PCMCIA
	ti = (struct tok_info *)kmalloc(sizeof(struct tok_info), GFP_KERNEL);
	if (ti == NULL) 
		return -ENOMEM;

	memset(ti, 0, sizeof(struct tok_info));
#else
	ti = dev->priv ; 
#endif
	ti->mmio= t_mmio;
	ti->readlog_pending = 0;
	init_waitqueue_head(&ti->wait_for_tok_int);
	init_waitqueue_head(&ti->wait_for_reset);

	dev->priv = ti;     /* this seems like the logical use of the
                         field ... let's try some empirical tests
                         using the token-info structure -- that
                         should fit with out future hope of multiple
                         adapter support as well /dwm   */

	/* if PCMCIA, then the card is recognized as TR_ISAPNP 
	 * and there is no need to set up the interrupt, it is already done. */
        
#ifndef PCMCIA
	switch (cardpresent) 
	{
		case TR_ISA:
			if (intr==0)
				irq=9; /* irq2 really is irq9 */
			if (intr==1)
				irq=3;
			if (intr==2)
				irq=6;
			if (intr==3)
				irq=7;
			ti->global_int_enable=GLOBAL_INT_ENABLE+((irq==9) ? 2 : irq);
			ti->adapter_int_enable=PIOaddr+ADAPTINTREL;
			ti->sram=0;
#if !TR_NEWFORMAT
			DPRINTK("ti->global_int_enable: %04X\n",ti->global_int_enable);
#endif
			break;
		case TR_MCA:
			if (intr==0)
				irq=9;
			if (intr==1)
				irq=3;
			if (intr==2)
				irq=10;
			if (intr==3)
				irq=11;
			ti->global_int_enable=0;
			ti->adapter_int_enable=0;
			ti->sram=((__u32)(inb(PIOaddr+ADAPTRESETREL) & 0xfe) << 12);
			break;
		case TR_ISAPNP:
			if (intr==0)
				irq=9;
			if (intr==1)
				irq=3;
			if (intr==2)
				irq=10;
			if (intr==3)
				irq=11;
			timeout = jiffies + TR_SPIN_INTERVAL;
			while(!isa_readb(ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN))
				if (time_after(jiffies, timeout)) {
					DPRINTK("Hardware timeout during initialization.\n");
					kfree(ti);
					return -ENODEV;
			        }

			ti->sram=((__u32)isa_readb(ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN)<<12);
			ti->global_int_enable=PIOaddr+ADAPTINTREL;
			ti->adapter_int_enable=PIOaddr+ADAPTINTREL;
			break;
	}
#endif

	if (ibmtr_debug_trace & TRC_INIT) { /* just report int */
		DPRINTK("irq=%d",irq);
		if (ibmtr_debug_trace & TRC_INITV) { /* full chat in verbose only */
			DPRINTK(", ti->mmio=%08X",ti->mmio);
			printk(", segment=%02X",segment);
		}
		printk(".\n");
	}

	/* Get hw address of token ring card */
#if !TR_NEWFORMAT
	DPRINTK("hw address: ");
#endif
	j=0;
	for (i=0; i<0x18; i=i+2) 
	{
		/* technical reference states to do this */
		temp = isa_readb(ti->mmio + AIP + i) & 0x0f;
#if !TR_NEWFORMAT
		printk("%1X",ti->hw_address[j]=temp);
#else
		ti->hw_address[j]=temp;
#endif
		if(j&1)
			dev->dev_addr[(j/2)]=ti->hw_address[j]+(ti->hw_address[j-1]<<4);
		++j;
	}
#ifndef TR_NEWFORMAT
	printk("\n");
#endif

	/* get Adapter type:  'F' = Adapter/A, 'E' = 16/4 Adapter II,...*/
	ti->adapter_type = isa_readb(ti->mmio + AIPADAPTYPE);

	/* get Data Rate:  F=4Mb, E=16Mb, D=4Mb & 16Mb ?? */
	ti->data_rate = isa_readb(ti->mmio + AIPDATARATE);

	/* Get Early Token Release support?: F=no, E=4Mb, D=16Mb, C=4&16Mb */
	ti->token_release = isa_readb(ti->mmio + AIPEARLYTOKEN);

	/* How much shared RAM is on adapter ? */
#ifdef PCMCIA
	ti->avail_shared_ram = pcmcia_reality_check(get_sram_size(ti));
	ibmtr_mem_base = ti->sram_base << 12 ; 
#else
	ti->avail_shared_ram = get_sram_size(ti);
#endif
	/* We need to set or do a bunch of work here based on previous results.. */
	/* Support paging?  What sizes?:  F=no, E=16k, D=32k, C=16 & 32k */
	ti->shared_ram_paging = isa_readb(ti->mmio + AIPSHRAMPAGE);

        /* Available DHB  4Mb size:   F=2048, E=4096, D=4464 */
	switch (isa_readb(ti->mmio + AIP4MBDHB)) {
	case 0xe : 
		ti->dhb_size4mb = 4096;
		break; 
	case 0xd : 
		ti->dhb_size4mb = 4464;
		break; 
	default  : 
		ti->dhb_size4mb = 2048;
		break; 
	}

	/* Available DHB 16Mb size:  F=2048, E=4096, D=8192, C=16384, B=17960 */
	switch (isa_readb(ti->mmio + AIP16MBDHB)) {
	case 0xe : 
		ti->dhb_size16mb = 4096;
		break; 
	case 0xd : 
		ti->dhb_size16mb = 8192;
		break; 
	case 0xc : 
		ti->dhb_size16mb = 16384;
		break; 
	case 0xb : 
		ti->dhb_size16mb = 17960;
		break; 
	default  : 
		ti->dhb_size16mb = 2048;
		break; 
	}

#if !TR_NEWFORMAT
	DPRINTK("atype=%x, drate=%x, trel=%x, asram=%dK, srp=%x, "
		"dhb(4mb=%x, 16mb=%x)\n",ti->adapter_type,
		ti->data_rate, ti->token_release, ti->avail_shared_ram/2,
		ti->shared_ram_paging, ti->dhb_size4mb, ti->dhb_size16mb);
#endif

	/*	We must figure out how much shared memory space this adapter
	 *	will occupy so that if there are two adapters we can fit both
	 *	in.  Given a choice, we will limit this adapter to 32K.  The
	 *	maximum space will will use for two adapters is 64K so if the
	 *	adapter we are working on demands 64K (it also doesn't support
	 *	paging), then only one adapter can be supported.  
	 */

	/*
	 *	determine how much of total RAM is mapped into PC space 
	 */
	ti->mapped_ram_size=1<<((((isa_readb(ti->mmio+ ACA_OFFSET + ACA_RW + RRR_ODD)) >>2) & 0x03) + 4);
	ti->page_mask=0;
	if (ti->shared_ram_paging == 0xf) { /* No paging in adapter */
		ti->mapped_ram_size = ti->avail_shared_ram;
	} else {
#ifdef ENABLE_PAGING
		unsigned char pg_size=0;
#endif

#if !TR_NEWFORMAT
		DPRINTK("shared ram page size: %dK\n",ti->mapped_ram_size/2);
#endif
#ifdef ENABLE_PAGING
	switch(ti->shared_ram_paging) 
	{
		case 0xf:
			break;
		case 0xe:
			ti->page_mask=(ti->mapped_ram_size==32) ? 0xc0 : 0;
			pg_size=32;   /* 16KB page size */
			break;
		case 0xd:
			ti->page_mask=(ti->mapped_ram_size==64) ? 0x80 : 0;
			pg_size=64;   /* 32KB page size */
			break;
		case 0xc:
		        switch (ti->mapped_ram_size) {
			  case 32:
			    ti->page_mask=0xc0;
                            pg_size=32;
                            break;
			  case 64:
                            ti->page_mask=0x80;
                            pg_size=64;
                            break;
                        }
			break;
		default:
			DPRINTK("Unknown shared ram paging info %01X\n",ti->shared_ram_paging);
			kfree(ti);
			return -ENODEV;
			break;
	}

 	if (ibmtr_debug_trace & TRC_INIT)
 	  DPRINTK("Shared RAM paging code: "
 		 "%02X mapped RAM size: %dK shared RAM size: %dK page mask: %0xX\n:",
 		 ti->shared_ram_paging, ti->mapped_ram_size/2, ti->avail_shared_ram/2, ti->page_mask);

	if (ti->page_mask) {
		if (pg_size > ti->mapped_ram_size) {
			DPRINTK("Page size (%d) > mapped ram window (%d), can't page.\n",
				pg_size/2, ti->mapped_ram_size/2);
				ti->page_mask = 0;    /* reset paging */
                }
	} else if (pg_size > ti->mapped_ram_size) {
	                DPRINTK("Page size (%d) > mapped ram window (%d), can't page.\n",
				pg_size/2, ti->mapped_ram_size/2);
	}
#endif
	}
	/* finish figuring the shared RAM address */
	if (cardpresent==TR_ISA) {
		static __u32 ram_bndry_mask[]={0xffffe000, 0xffffc000, 0xffff8000, 0xffff0000};
		__u32 new_base, rrr_32, chk_base, rbm;

		rrr_32 = ((isa_readb(ti->mmio+ ACA_OFFSET + ACA_RW + RRR_ODD))>>2) & 0x00000003;
		rbm = ram_bndry_mask[rrr_32];
		new_base = (ibmtr_mem_base + (~rbm)) & rbm; /* up to boundary */
		chk_base = new_base + (ti->mapped_ram_size<<9);
		if (chk_base > (ibmtr_mem_base + IBMTR_SHARED_RAM_SIZE)) {
			DPRINTK("Shared RAM for this adapter (%05x) exceeds driver"
				" limit (%05x), adapter not started.\n",
				chk_base, ibmtr_mem_base + IBMTR_SHARED_RAM_SIZE);
			kfree(ti);
                        return -ENODEV;
		} else {  /* seems cool, record what we have figured out */
			ti->sram_base = new_base >> 12;
			ibmtr_mem_base = chk_base;
		}
	}

#if !TR_NEWFORMAT
	DPRINTK("Using %dK shared RAM\n",ti->mapped_ram_size/2);
#endif

	/* The PCMCIA has already got the interrupt line and the io port, 
	   so no chance of anybody else getting it - MLP */

#ifndef PCMCIA
	if (request_irq (dev->irq = irq, &tok_interrupt,0,"ibmtr", dev) != 0) {
		DPRINTK("Could not grab irq %d.  Halting Token Ring driver.\n",irq);
		kfree(ti);
		return -ENODEV;
	}
 
	/*?? Now, allocate some of the PIO PORTs for this driver.. */
	request_region(PIOaddr,IBMTR_IO_EXTENT,"ibmtr");  /* record PIOaddr range as busy */
#endif

#if !TR_NEWFORMAT
	DPRINTK("%s",version); /* As we have passed card identification,
                                  let the world know we're here! */
#else

	if (version) {
	        printk("%s",version);
                version = NULL;
        }
	DPRINTK("%s %s found\n",
		channel_def[cardpresent-1], adapter_def(ti->adapter_type));
	DPRINTK("using irq %d, PIOaddr %hx, %dK shared RAM.\n",
	        irq, PIOaddr, ti->mapped_ram_size/2);
	DPRINTK("Hardware address : %02X:%02X:%02X:%02X:%02X:%02X\n",
		dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
 	if (ti->page_mask)
 	  DPRINTK("Shared RAM paging enabled. Page size: %uK Shared Ram size %dK\n",
 		  ((ti->page_mask ^ 0xff)+1)>>2,ti->avail_shared_ram/2);
 	else
 	  DPRINTK("Shared RAM paging disabled. ti->page_mask %x\n",ti->page_mask);
#endif
	/* Calculate the maximum DHB we can use */
	if (!ti->page_mask) {
	  ti->avail_shared_ram=ti->mapped_ram_size;
	}
	switch (ti->avail_shared_ram) {
	case  16 : /* 8KB shared RAM */
		ti->dhb_size4mb  = MIN(ti->dhb_size4mb, 2048);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4 = 2;
		ti->dhb_size16mb = MIN(ti->dhb_size16mb, 2048);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16 = 2;
		break;
	case  32 : /* 16KB shared RAM */
		ti->dhb_size4mb  = MIN(ti->dhb_size4mb, 2048);
		ti->rbuf_len4 = 520;
		ti->rbuf_cnt4 = 9;
		ti->dhb_size16mb = MIN(ti->dhb_size16mb, 2048);
		ti->rbuf_len16 = 1032; /* 1024 usable */
		ti->rbuf_cnt16 = 4;
		break;
	case  64 : /* 32KB shared RAM */
		ti->dhb_size4mb  = MIN(ti->dhb_size4mb, 2048);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4 = 6;
		ti->dhb_size16mb = MIN(ti->dhb_size16mb, 2048);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16 = 10;
		break;
	case 127 : /* 63KB shared RAM */
		ti->dhb_size4mb  = MIN(ti->dhb_size4mb, 2048);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4 = 6;
		ti->dhb_size16mb = MIN(ti->dhb_size16mb, 2048);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16 = 16;
		break;
	case 128 : /* 64KB shared RAM */
		ti->dhb_size4mb  = MIN(ti->dhb_size4mb, 2048);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4 = 6;
		ti->dhb_size16mb = MIN(ti->dhb_size16mb, 2048);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16 = 18;
		break;
	default  :
		ti->dhb_size4mb  = 2048;
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4 = 2;
		ti->dhb_size16mb = 2048;
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16 = 2;
		break;
	}

	ti->maxmtu16 = (ti->rbuf_len16*ti->rbuf_cnt16)-((ti->rbuf_cnt16)<<3)-TR_HLEN;
	ti->maxmtu4  = (ti->rbuf_len4*ti->rbuf_cnt4)-((ti->rbuf_cnt4)<<3)-TR_HLEN;
	DPRINTK("Maximum MTU 16Mbps: %d, 4Mbps: %d\n",
		ti->maxmtu16, ti->maxmtu4);

	dev->base_addr=PIOaddr; /* set the value for device */

	trdev_init(dev);
	tok_init_card(dev);

	return 0;  /* Return 0 to indicate we have found a Token Ring card. */
}

/* query the adapter for the size of shared RAM  */

static unsigned char __init get_sram_size(struct tok_info *adapt_info)
{

	unsigned char avail_sram_code;
	static unsigned char size_code[]={ 0,16,32,64,127,128 };
	/* Adapter gives
	   'F' -- use RRR bits 3,2
	   'E' -- 8kb   'D' -- 16kb
	   'C' -- 32kb  'A' -- 64KB
	   'B' - 64KB less 512 bytes at top
	   (WARNING ... must zero top bytes in INIT */

	avail_sram_code=0xf-isa_readb(adapt_info->mmio + AIPAVAILSHRAM);
	if (avail_sram_code)
		return size_code[avail_sram_code];
	else  /* for code 'F', must compute size from RRR(3,2) bits */
		return 1<<((isa_readb(adapt_info->mmio+ ACA_OFFSET + ACA_RW + RRR_ODD)>>2)+4);
}

static int __init trdev_init(struct net_device *dev)
{
	struct tok_info *ti=(struct tok_info *)dev->priv;

	/* init the spinlock */
	spin_lock_init(&ti->lock);

	SET_PAGE(ti->srb_page);
	ti->open_status		= CLOSED;

	dev->init		= tok_init_card;
	dev->open		= tok_open;
	dev->stop		= tok_close;
	dev->hard_start_xmit	= tok_send_packet;
	dev->get_stats 		= tok_get_stats;
	dev->set_multicast_list = tok_set_multicast_list;
	dev->change_mtu         = ibmtr_change_mtu;

#ifndef MODULE
#ifndef PCMCIA
	tr_setup(dev);
#endif
#endif
	return 0;
}


static void tok_set_multicast_list(struct net_device *dev)
{
	struct tok_info *ti=(struct tok_info *)dev->priv;
	struct dev_mc_list *mclist;
	unsigned char address[4];

	int i;

	if(ti->open_status==CLOSED)
		return;

	address[0] = address[1] = address[2] = address[3] = 0;

	mclist = dev->mc_list;
	for (i=0; i< dev->mc_count; i++)
	{
		address[0] |= mclist->dmi_addr[2];
		address[1] |= mclist->dmi_addr[3];
		address[2] |= mclist->dmi_addr[4];
		address[3] |= mclist->dmi_addr[5];
		mclist = mclist->next;
	}
	SET_PAGE(ti->srb_page);
	for (i=0; i<sizeof(struct srb_set_funct_addr); i++)
		isa_writeb(0, ti->srb+i);

	isa_writeb(DIR_SET_FUNC_ADDR, 
               ti->srb + offsetof(struct srb_set_funct_addr, command));

	DPRINTK("Setting functional address: ");

	for (i=0; i<4; i++)
	{
		isa_writeb(address[i], 
		ti->srb + offsetof(struct srb_set_funct_addr, funct_address)+i);
		printk("%02X ", address[i]);
	}
	isa_writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	printk("\n");
}

static int tok_open(struct net_device *dev)
{
	struct tok_info *ti=(struct tok_info *)dev->priv;

	if (ti->open_status==CLOSED) tok_init_card(dev);

	if (ti->open_status==IN_PROGRESS) sleep_on(&ti->wait_for_reset);

	if (ti->open_status==SUCCESS) {
		/* NEED to see smem size *AND* reset high 512 bytes if needed */
		netif_start_queue(dev);
		MOD_INC_USE_COUNT;

		return 0;
	} else return -EAGAIN;

}

static int tok_close(struct net_device *dev)
{

	struct tok_info *ti=(struct tok_info *) dev->priv;

	if(ti->open_status!=CLOSED) {
		netif_stop_queue(dev);
		SET_PAGE(ti->srb_page);	
		isa_writeb(DIR_CLOSE_ADAPTER,
	       	ti->srb + offsetof(struct srb_close_adapter, command));
		isa_writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

		ti->open_status=CLOSED;

		sleep_on(&ti->wait_for_tok_int);

		SET_PAGE(ti->srb_page);
		if (isa_readb(ti->srb + offsetof(struct srb_close_adapter, ret_code)))
			DPRINTK("close adapter failed: %02X\n",
				(int)isa_readb(ti->srb + offsetof(struct srb_close_adapter, ret_code)));

#ifdef PCMCIA
		ti->sram = 0 ;
#endif
		DPRINTK("Adapter closed.\n");
		MOD_DEC_USE_COUNT;
	}

	return 0;
}

void tok_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char status;
	struct tok_info *ti;
	struct net_device *dev;
#ifdef ENABLE_PAGING
	unsigned char save_srpr;
#endif

	dev = dev_id;
#if TR_VERBOSE
	DPRINTK("Int from tok_driver, dev : %p\n",dev);
#endif
	ti  = (struct tok_info *) dev->priv;
	spin_lock(&(ti->lock));
#ifdef ENABLE_PAGING
	save_srpr=isa_readb(ti->mmio+ACA_OFFSET+ACA_RW+SRPR_EVEN);
#endif

      	/* Disable interrupts till processing is finished */
	isa_writeb((~INT_ENABLE), ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN);

	/* Reset interrupt for ISA boards */
        if (ti->adapter_int_enable)
                outb(0,ti->adapter_int_enable);
	else
		outb(0,ti->global_int_enable);


	switch (ti->do_tok_int) {

	      case NOT_FIRST:

		/*  Begin the regular interrupt handler HERE inline to avoid
		    the extra levels of logic and call depth for the
		    original solution.   */

		status=isa_readb(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_ODD);
#ifdef PCMCIA
      		/* Check if the PCMCIA card was pulled. */
    		if (status == 0xFF)
       		{
		          DPRINTK("PCMCIA card removed.\n");
                          ti->open_status=CLOSED;
			  goto return_point ; 
       		}

    	        /* Check ISRP EVEN too. */
      	        if ( isa_readb (ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN) == 0xFF)
    	        {
         		 DPRINTK("PCMCIA card removed.\n");
                         ti->open_status=CLOSED;
			 goto return_point ; 
      		}
#endif


		if (status & ADAP_CHK_INT) {

			int i;
			__u32 check_reason;
                        __u8  check_reason_page=0;

			check_reason=ntohs(isa_readw(ti->sram + ACA_OFFSET + ACA_RW +WWCR_EVEN));
			if (ti->page_mask) {
			  check_reason_page=(check_reason>>8) & ti->page_mask;
			  check_reason &= ~(ti->page_mask << 8);
			}
			check_reason += ti->sram;
			SET_PAGE(check_reason_page);

			DPRINTK("Adapter check interrupt\n");
			DPRINTK("8 reason bytes follow: ");
			for(i=0; i<8; i++, check_reason++)
				printk("%02X ", (int)isa_readb(check_reason));
			printk("\n");

			isa_writeb((~ADAP_CHK_INT), ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);
			isa_writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET  + ISRP_EVEN);

		} else if (isa_readb(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN)
				 & (TCR_INT | ERR_INT | ACCESS_INT)) {

			DPRINTK("adapter error: ISRP_EVEN : %02x\n",
				(int)isa_readb(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN));
			isa_writeb(~(TCR_INT | ERR_INT | ACCESS_INT),
			       ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN);
			isa_writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET  + ISRP_EVEN);

		} else if (status
			   & (SRB_RESP_INT | ASB_FREE_INT | ARB_CMD_INT | SSB_RESP_INT)) {
			/* SRB, ASB, ARB or SSB response */

			if (status & SRB_RESP_INT) { /* SRB response */
           		        SET_PAGE(ti->srb_page);
#if TR_VERBOSE
 				DPRINTK("SRB resp: cmd=%02X rsp=%02X\n",
					isa_readb(ti->srb),
					isa_readb(ti->srb + offsetof(struct srb_xmit, ret_code)));
#endif

				switch(isa_readb(ti->srb)) { /* SRB command check */

				      case XMIT_DIR_FRAME: {
					      unsigned char xmit_ret_code;

					      xmit_ret_code=isa_readb(ti->srb + offsetof(struct srb_xmit, ret_code));
					      if (xmit_ret_code != 0xff) {
						      DPRINTK("error on xmit_dir_frame request: %02X\n",
							      xmit_ret_code);
						      if (ti->current_skb) {
							      dev_kfree_skb_irq(ti->current_skb);
							      ti->current_skb=NULL;
						      }
						      netif_wake_queue(dev);
						      if (ti->readlog_pending) ibmtr_readlog(dev);
					      }
				      }
				      break;

				      case XMIT_UI_FRAME: {
					      unsigned char xmit_ret_code;

					      xmit_ret_code=isa_readb(ti->srb + offsetof(struct srb_xmit, ret_code));
					      if (xmit_ret_code != 0xff) {
						      DPRINTK("error on xmit_ui_frame request: %02X\n",
							      xmit_ret_code);
						      if (ti->current_skb) {
							      dev_kfree_skb_irq(ti->current_skb);
							      ti->current_skb=NULL;
						      }
						      netif_wake_queue(dev);
						      if (ti->readlog_pending)
						      		ibmtr_readlog(dev);
					      }
				      }
				      break;

				      case DIR_OPEN_ADAPTER: {
					      unsigned char open_ret_code;
					      __u16 open_error_code;

 					      ti->srb=ntohs(isa_readw(ti->init_srb +offsetof(struct srb_open_response, srb_addr)));
 					      ti->ssb=ntohs(isa_readw(ti->init_srb +offsetof(struct srb_open_response, ssb_addr)));
 					      ti->arb=ntohs(isa_readw(ti->init_srb +offsetof(struct srb_open_response, arb_addr)));
 					      ti->asb=ntohs(isa_readw(ti->init_srb +offsetof(struct srb_open_response, asb_addr)));
					      if (ti->page_mask) {
						ti->srb_page=(ti->srb>>8) & ti->page_mask;
						ti->srb &= ~(ti->page_mask<<8);
						ti->ssb_page=(ti->ssb>>8) & ti->page_mask;
						ti->ssb &= ~(ti->page_mask<<8);
						ti->arb_page=(ti->arb>>8) & ti->page_mask;
						ti->arb &= ~(ti->page_mask<<8);
						ti->asb_page=(ti->asb>>8) & ti->page_mask;
						ti->asb &= ~(ti->page_mask<<8);
					      }
					      ti->srb+=ti->sram;
					      ti->ssb+=ti->sram;
					      ti->arb+=ti->sram;
					      ti->asb+=ti->sram;

					      ti->current_skb=NULL;

					      open_ret_code = isa_readb(ti->init_srb +offsetof(struct srb_open_response, ret_code));
					      open_error_code = ntohs(isa_readw(ti->init_srb +offsetof(struct srb_open_response, error_code)));

					      if (open_ret_code==7) {

						      if (!ti->auto_ringspeedsave && (open_error_code==0x24)) {
							      DPRINTK("Open failed: Adapter speed must match ring "
								      "speed if Automatic Ring Speed Save is disabled.\n");
							      ti->open_status=FAILURE;
							      wake_up(&ti->wait_for_reset);
						      } else if (open_error_code==0x24)
							      DPRINTK("Retrying open to adjust to ring speed.\n");
						      else if ((open_error_code==0x2d) && ti->auto_ringspeedsave)
							      DPRINTK("No signal detected for Auto Speed Detection.\n");
						      else if (open_error_code==0x11)
						      {
							      if (ti->retry_count--) 
								      DPRINTK("Ring broken/disconnected, retrying...\n");
							      else {
								      DPRINTK("Ring broken/disconnected, open failed.\n");
								      ti->open_status = FAILURE;
							      }
						      }
						      else DPRINTK("Unrecoverable error: error code = %04x.\n",
								   open_error_code);

					      } else if (!open_ret_code) {
#if !TR_NEWFORMAT
						      DPRINTK("board opened...\n");
#else
						      DPRINTK("Adapter initialized and opened.\n");
#endif
						      isa_writeb(~(SRB_RESP_INT),
							     ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);
						      isa_writeb(~(CMD_IN_SRB),
							     ti->mmio + ACA_OFFSET + ACA_RESET + ISRA_ODD);
						      open_sap(EXTENDED_SAP,dev);

						      /* YdW probably hates me */
						      goto skip_reset;
					      } else
						      DPRINTK("open failed: ret_code = %02X, retrying\n",
							      open_ret_code);

					      if (ti->open_status != FAILURE) {
						      ibmtr_reset_timer(&(ti->tr_timer), dev);
					      }

				      }
				      break;

				      case DIR_CLOSE_ADAPTER:
					wake_up(&ti->wait_for_tok_int);
					break;

				      case DLC_OPEN_SAP:
					if (isa_readb(ti->srb+offsetof(struct dlc_open_sap, ret_code))) {
						DPRINTK("open_sap failed: ret_code = %02X,retrying\n",
							(int)isa_readb(ti->srb+offsetof(struct dlc_open_sap, ret_code)));
						ibmtr_reset_timer(&(ti->tr_timer), dev);
					} else {
						ti->exsap_station_id=
							isa_readw(ti->srb+offsetof(struct dlc_open_sap, station_id));
						ti->open_status=SUCCESS; /* TR adapter is now available */
						wake_up(&ti->wait_for_reset);
					}
					break;

				      case DIR_INTERRUPT:
				      case DIR_MOD_OPEN_PARAMS:
				      case DIR_SET_GRP_ADDR:
				      case DIR_SET_FUNC_ADDR:
				      case DLC_CLOSE_SAP:
					if (isa_readb(ti->srb+offsetof(struct srb_interrupt, ret_code)))
						DPRINTK("error on %02X: %02X\n",
							(int)isa_readb(ti->srb+offsetof(struct srb_interrupt, command)),
							(int)isa_readb(ti->srb+offsetof(struct srb_interrupt, ret_code)));
					break;

				      case DIR_READ_LOG:
					if (isa_readb(ti->srb+offsetof(struct srb_read_log, ret_code)))
						DPRINTK("error on dir_read_log: %02X\n",
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log, ret_code)));
					else
					    if (IBMTR_DEBUG_MESSAGES) {
						DPRINTK(
							"Line errors %02X, Internal errors %02X, Burst errors %02X\n"
							"A/C errors %02X, Abort delimiters %02X, Lost frames %02X\n"
							"Receive congestion count %02X, Frame copied errors %02X\n"
							"Frequency errors %02X, Token errors %02X\n",
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    line_errors)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    internal_errors)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    burst_errors)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log, A_C_errors)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    abort_delimiters)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    lost_frames)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
												    recv_congest_count)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    frame_copied_errors)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
										    frequency_errors)),
							(int)isa_readb(ti->srb+offsetof(struct srb_read_log,
												    token_errors)));
					    }
					netif_wake_queue(dev);
					break;

				      default:
					DPRINTK("Unknown command %02X encountered\n",
						(int)isa_readb(ti->srb));

				} /* SRB command check */

				isa_writeb(~CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_RESET + ISRA_ODD);
				isa_writeb(~SRB_RESP_INT, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);

			  skip_reset:
			} /* SRB response */

			if (status & ASB_FREE_INT) { /* ASB response */
			        SET_PAGE(ti->asb_page);
#if TR_VERBOSE
 				DPRINTK("ASB resp: cmd=%02X\n", isa_readb(ti->asb));
 #endif

				switch(isa_readb(ti->asb)) { /* ASB command check */

				      case REC_DATA:
				      case XMIT_UI_FRAME:
				      case XMIT_DIR_FRAME:
					break;

				      default:
					DPRINTK("unknown command in asb %02X\n",
						(int)isa_readb(ti->asb));

				} /* ASB command check */

				if (isa_readb(ti->asb+2)!=0xff) /* checks ret_code */
				    DPRINTK("ASB error %02X in cmd %02X\n",
					    (int)isa_readb(ti->asb+2),(int)isa_readb(ti->asb));
				isa_writeb(~ASB_FREE_INT, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);

			} /* ASB response */

			if (status & ARB_CMD_INT) { /* ARB response */
            		        SET_PAGE(ti->arb_page);
#if TR_VERBOSE 
 				DPRINTK("ARB resp: cmd=%02X rsp=%02X\n",
 					isa_readb(ti->arb),
 					isa_readb(ti->arb + offsetof(struct arb_dlc_status, status)));
#endif

				switch (isa_readb(ti->arb)) { /* ARB command check */

				      case DLC_STATUS:
					DPRINTK("DLC_STATUS new status: %02X on station %02X\n",
						ntohs(isa_readw(ti->arb + offsetof(struct arb_dlc_status, status))),
						ntohs(isa_readw(ti->arb
									    +offsetof(struct arb_dlc_status, station_id))));
					break;

				      case REC_DATA:
					tr_rx(dev);
					break;

				      case RING_STAT_CHANGE: {
					      unsigned short ring_status;

					      ring_status=ntohs(isa_readw(ti->arb
								      +offsetof(struct arb_ring_stat_change, ring_status)));

					      if (ring_status & (SIGNAL_LOSS | LOBE_FAULT)) {

						      DPRINTK("Signal loss/Lobe fault\n");
						      DPRINTK("We try to reopen the adapter.\n");
						      ibmtr_reset_timer(&(ti->tr_timer), dev);
					      } else if (ring_status & (HARD_ERROR | XMIT_BEACON
											| AUTO_REMOVAL | REMOVE_RECV | RING_RECOVER))
						      DPRINTK("New ring status: %02X\n", ring_status);

					      if (ring_status & LOG_OVERFLOW) {
						      if (netif_queue_stopped(dev))
                                                              ti->readlog_pending = 1;
						      else
                                                              ibmtr_readlog(dev);
					      }
				      }
				      break;

				      case XMIT_DATA_REQ:
					tr_tx(dev);
					break;

				      default:
					DPRINTK("Unknown command %02X in arb\n",
						(int)isa_readb(ti->arb));
					break;

				} /* ARB command check */

				isa_writeb(~ARB_CMD_INT, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);
				isa_writeb(ARB_FREE, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

			} /* ARB response */

			if (status & SSB_RESP_INT) { /* SSB response */
				unsigned char retcode;
 				SET_PAGE(ti->ssb_page);
#if TR_VERBOSE
 				DPRINTK("SSB resp: cmd=%02X rsp=%02X\n",
 					isa_readb(ti->ssb), isa_readb(ti->ssb+2));
#endif
				switch (isa_readb(ti->ssb)) { /* SSB command check */
				      
				      case XMIT_DIR_FRAME:
				      case XMIT_UI_FRAME:
					retcode = isa_readb(ti->ssb+2);
					if (retcode && (retcode != 0x22)) /* checks ret_code */
						DPRINTK("xmit ret_code: %02X xmit error code: %02X\n",
							(int)retcode, (int)isa_readb(ti->ssb+6));
					else ti->tr_stats.tx_packets++;
					break;

				      case XMIT_XID_CMD:
					DPRINTK("xmit xid ret_code: %02X\n", (int)isa_readb(ti->ssb+2));
                                        break;

				      default:
					DPRINTK("Unknown command %02X in ssb\n", (int)isa_readb(ti->ssb));

				} /* SSB command check */

				isa_writeb(~SSB_RESP_INT, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);
				isa_writeb(SSB_FREE, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

			} /* SSB response */

		}	 /* SRB, ARB, ASB or SSB response */

		isa_writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
		break;

	      case FIRST_INT:
		initial_tok_int(dev);
		break;

	      default:
		DPRINTK("Unexpected interrupt from tr adapter\n");

	}
#ifdef PCMCIA
 return_point:
#endif
#ifdef ENABLE_PAGING
 	isa_writeb(save_srpr, ti->mmio+ACA_OFFSET+ACA_RW+SRPR_EVEN);
#endif

	spin_unlock(&(ti->lock));
}

static void initial_tok_int(struct net_device *dev)
{

	__u32 encoded_addr;
	__u32 hw_encoded_addr;
	struct tok_info *ti;
	ti=(struct tok_info *) dev->priv;

	ti->do_tok_int=NOT_FIRST;

#ifndef TR_NEWFORMAT
	DPRINTK("Initial tok int received\n");
#endif

	/* we assign the shared-ram address for ISA devices */
	if(!ti->sram) {
		isa_writeb(ti->sram_base, ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN);
		ti->sram=((__u32)ti->sram_base << 12);
	}
 	ti->init_srb=ntohs((unsigned short)isa_readw(ti->mmio+ ACA_OFFSET + WRBR_EVEN));
	if (ti->page_mask) {
	  ti->init_srb_page=(ti->init_srb>>8)&ti->page_mask;
	  ti->init_srb &= ~(ti->page_mask<<8);
	}
	ti->init_srb+=ti->sram;
	
	if (ti->avail_shared_ram == 127) {
	  int i;
	  int last_512=0xfe00;
 	  if (ti->page_mask) {
	    last_512 &= ~(ti->page_mask<<8);
	  }
	  /*  initialize high section of ram (if necessary) */
	  SET_PAGE(0xc0);
	  for (i=0; i<512; i++) {
	    isa_writeb(0,ti->sram+last_512+i);
	  }
	}  
	SET_PAGE(ti->init_srb_page);

	dev->mem_start = ti->sram;
	dev->mem_end = ti->sram + (ti->mapped_ram_size<<9) - 1;

#if TR_VERBOSE
	{
		int i;
		DPRINTK("init_srb(%lx):", (long)ti->init_srb);
		for (i=0;i<17;i++) printk("%02X ", (int)isa_readb(ti->init_srb+i));
		printk("\n");
	}
#endif

	hw_encoded_addr = isa_readw(ti->init_srb
				+ offsetof(struct srb_init_response, encoded_address));

#if !TR_NEWFORMAT
	DPRINTK("srb_init_response->encoded_address: %04X\n", hw_encoded_addr);
	DPRINTK("ntohs(srb_init_response->encoded_address): %04X\n",
		ntohs(hw_encoded_addr));
#endif

	encoded_addr=(ti->sram + ntohs(hw_encoded_addr));
	ti->ring_speed = isa_readb(ti->init_srb+offsetof(struct srb_init_response, init_status)) & 0x01 ? 16 : 4;
#if !TR_NEWFORMAT
	DPRINTK("encoded addr (%04X,%04X,%08X): ", hw_encoded_addr,
		ntohs(hw_encoded_addr), encoded_addr);
#else
	DPRINTK("Initial interrupt : %d Mbps, shared RAM base %08x.\n",
		ti->ring_speed, ti->sram);
#endif

	ti->auto_ringspeedsave=isa_readb(ti->init_srb
				     +offsetof(struct srb_init_response, init_status_2)) & 0x4 ? TRUE : FALSE;

#if !TR_NEWFORMAT
	for(i=0;i<TR_ALEN;i++) {
		dev->dev_addr[i]=isa_readb(encoded_addr + i);
		printk("%02X%s", dev->dev_addr[i], (i==TR_ALEN-1) ? "" : ":" );
	}
	printk("\n");
#endif

	tok_open_adapter((unsigned long)dev);
}

static int tok_init_card(struct net_device *dev)
{
	struct tok_info *ti;
	short PIOaddr;
	unsigned long i;
	PIOaddr = dev->base_addr;
	ti=(struct tok_info *) dev->priv;

	/* Special processing for first interrupt after reset */
	ti->do_tok_int=FIRST_INT;

	/* Reset adapter */
	netif_stop_queue(dev);

	isa_writeb(~INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN);

#if !TR_NEWFORMAT
	DPRINTK("resetting card\n");
#endif

	outb(0, PIOaddr+ADAPTRESET);
	for (i=jiffies+TR_RESET_INTERVAL; time_before_eq(jiffies, i);); /* wait 50ms */
	outb(0,PIOaddr+ADAPTRESETREL);
#ifdef ENABLE_PAGING
	if(ti->page_mask)
		isa_writeb(SRPR_ENABLE_PAGING, ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN);
#endif

#if !TR_NEWFORMAT
	DPRINTK("card reset\n");
#endif

	ti->open_status=IN_PROGRESS;
	isa_writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	return 0;
}

static void open_sap(unsigned char type,struct net_device *dev)
{
	int i;
	struct tok_info *ti=(struct tok_info *) dev->priv;

	SET_PAGE(ti->srb_page);
	for (i=0; i<sizeof(struct dlc_open_sap); i++)
		isa_writeb(0, ti->srb+i);

	isa_writeb(DLC_OPEN_SAP, ti->srb + offsetof(struct dlc_open_sap, command));
	isa_writew(htons(MAX_I_FIELD),
	       ti->srb + offsetof(struct dlc_open_sap, max_i_field));
	isa_writeb(SAP_OPEN_IND_SAP | SAP_OPEN_PRIORITY,
	       ti->srb + offsetof(struct dlc_open_sap, sap_options));
	isa_writeb(SAP_OPEN_STATION_CNT,
	       ti->srb + offsetof(struct dlc_open_sap, station_count));
	isa_writeb(type, ti->srb + offsetof(struct dlc_open_sap, sap_value));

	isa_writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

}

void tok_open_adapter(unsigned long dev_addr)
{

	struct net_device *dev=(struct net_device *)dev_addr;
	struct tok_info *ti;
	int i;

	ti=(struct tok_info *) dev->priv;

#if !TR_NEWFORMAT
	DPRINTK("now opening the board...\n");
#endif

	isa_writeb(~SRB_RESP_INT, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);
	isa_writeb(~CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_RESET + ISRA_ODD);

	for (i=0; i<sizeof(struct dir_open_adapter); i++)
		isa_writeb(0, ti->init_srb+i);

	isa_writeb(DIR_OPEN_ADAPTER,
	       ti->init_srb + offsetof(struct dir_open_adapter, command));
	isa_writew(htons(OPEN_PASS_BCON_MAC),
	       ti->init_srb + offsetof(struct dir_open_adapter, open_options));
	if (ti->ring_speed == 16) {
		isa_writew(htons(ti->dhb_size16mb),
		       ti->init_srb + offsetof(struct dir_open_adapter, dhb_length));
		isa_writew(htons(ti->rbuf_cnt16),
		       ti->init_srb + offsetof(struct dir_open_adapter, num_rcv_buf));
		isa_writew(htons(ti->rbuf_len16),
		       ti->init_srb + offsetof(struct dir_open_adapter, rcv_buf_len));
	} else {
		isa_writew(htons(ti->dhb_size4mb),
		       ti->init_srb + offsetof(struct dir_open_adapter, dhb_length));
		isa_writew(htons(ti->rbuf_cnt4),
		       ti->init_srb + offsetof(struct dir_open_adapter, num_rcv_buf));
		isa_writew(htons(ti->rbuf_len4),
		       ti->init_srb + offsetof(struct dir_open_adapter, rcv_buf_len));
	}
	isa_writeb(NUM_DHB, /* always 2 */ 
	       ti->init_srb + offsetof(struct dir_open_adapter, num_dhb));
	isa_writeb(DLC_MAX_SAP,
	       ti->init_srb + offsetof(struct dir_open_adapter, dlc_max_sap));
	isa_writeb(DLC_MAX_STA,
	       ti->init_srb + offsetof(struct dir_open_adapter, dlc_max_sta));

	ti->srb=ti->init_srb; /* We use this one in the interrupt handler */
	ti->srb_page=ti->init_srb_page;
	DPRINTK("Opend adapter: Xmit bfrs: %d X %d, Rcv bfrs: %d X %d\n",
		isa_readb(ti->init_srb+offsetof(struct dir_open_adapter,num_dhb)),
		ntohs(isa_readw(ti->init_srb+offsetof(struct dir_open_adapter,dhb_length))),
		ntohs(isa_readw(ti->init_srb+offsetof(struct dir_open_adapter,num_rcv_buf))),
		ntohs(isa_readw(ti->init_srb+offsetof(struct dir_open_adapter,rcv_buf_len))) );

	isa_writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	isa_writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

}

static void tr_tx(struct net_device *dev)
{
	struct tok_info *ti=(struct tok_info *) dev->priv;
	struct trh_hdr *trhdr=(struct trh_hdr *)ti->current_skb->data;
	unsigned int hdr_len;
	__u32 dhb;
	unsigned char xmit_command;
	int i;
	struct trllc	*llc;
	struct srb_xmit  xsrb;
	__u8  dhb_page=0;
	__u8  llc_ssap;

	SET_PAGE(ti->asb_page);
	if (isa_readb(ti->asb + offsetof(struct asb_xmit_resp, ret_code))!=0xFF)
		DPRINTK("ASB not free !!!\n");

	/* in providing the transmit interrupts,
	   is telling us it is ready for data and
	   providing a shared memory address for us
	   to stuff with data.  Here we compute the
	   effective address where we will place data.*/
        SET_PAGE(ti->arb_page);
 	dhb=ntohs(isa_readw(ti->arb + offsetof(struct arb_xmit_req, dhb_address)));
        if (ti->page_mask) {
	  dhb_page=(dhb >> 8) & ti->page_mask;
	  dhb &= ~(ti->page_mask << 8);
        }
        dhb+=ti->sram;
	
	/* Figure out the size of the 802.5 header */
	if (!(trhdr->saddr[0] & 0x80)) /* RIF present? */
		hdr_len=sizeof(struct trh_hdr)-TR_MAXRIFLEN;
	else 
		hdr_len=((ntohs(trhdr->rcf) & TR_RCF_LEN_MASK)>>8)
			+sizeof(struct trh_hdr)-TR_MAXRIFLEN;

	llc = (struct trllc *)(ti->current_skb->data + hdr_len);

	llc_ssap=llc->ssap;
 	SET_PAGE(ti->srb_page);
 	isa_memcpy_fromio(&xsrb, ti->srb, sizeof(xsrb));
 	SET_PAGE(ti->asb_page);
 	xmit_command=xsrb.command;
	
	isa_writeb(xmit_command, ti->asb + offsetof(struct asb_xmit_resp, command));
	isa_writew(xsrb.station_id,
	       ti->asb + offsetof(struct asb_xmit_resp, station_id));
	isa_writeb(llc_ssap, ti->asb + offsetof(struct asb_xmit_resp, rsap_value));
 	isa_writeb(xsrb.cmd_corr,
	       ti->asb + offsetof(struct asb_xmit_resp, cmd_corr));
	isa_writeb(0, ti->asb + offsetof(struct asb_xmit_resp, ret_code));

	if ((xmit_command==XMIT_XID_CMD) || (xmit_command==XMIT_TEST_CMD)) {

		isa_writew(htons(0x11),
		       ti->asb + offsetof(struct asb_xmit_resp, frame_length));
		isa_writeb(0x0e, ti->asb + offsetof(struct asb_xmit_resp, hdr_length));
		SET_PAGE(dhb_page);
		isa_writeb(AC, dhb);
		isa_writeb(LLC_FRAME, dhb+1);

		for (i=0; i<TR_ALEN; i++) isa_writeb((int)0x0FF, dhb+i+2);
		for (i=0; i<TR_ALEN; i++) isa_writeb(0, dhb+i+TR_ALEN+2);

		isa_writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
		return;

	}

	/*
	 *      the token ring packet is copied from sk_buff to the adapter
	 *      buffer identified in the command data received with the interrupt.
	 */
	isa_writeb(hdr_len, ti->asb + offsetof(struct asb_xmit_resp, hdr_length));
	isa_writew(htons(ti->current_skb->len),
	       ti->asb + offsetof(struct asb_xmit_resp, frame_length));

	SET_PAGE(dhb_page);
	isa_memcpy_toio(dhb, ti->current_skb->data, ti->current_skb->len);

	isa_writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	ti->tr_stats.tx_bytes+=ti->current_skb->len;
	dev_kfree_skb_irq(ti->current_skb);
	ti->current_skb=NULL;
	netif_wake_queue(dev);
	if (ti->readlog_pending) ibmtr_readlog(dev);
}

static void tr_rx(struct net_device *dev)
{
	struct tok_info *ti=(struct tok_info *) dev->priv;
	__u32 rbuffer, rbufdata;
	__u8  rbuffer_page=0;
	__u32 llc;
	unsigned char *data;
	unsigned int rbuffer_len, lan_hdr_len, hdr_len, ip_len, length;
	struct sk_buff *skb;
	unsigned int skb_size = 0;
	int	IPv4_p = 0;
	unsigned int chksum = 0;
	struct iphdr *iph;
	struct arb_rec_req rarb;

	SET_PAGE(ti->arb_page);
 	isa_memcpy_fromio(&rarb, ti->arb, sizeof(rarb));
 	rbuffer=ntohs(rarb.rec_buf_addr)+2;
	if (ti->page_mask) {
	  rbuffer_page=(rbuffer >> 8) & ti->page_mask;
	  rbuffer &= ~(ti->page_mask<<8);
	}
	rbuffer += ti->sram;
	
	SET_PAGE(ti->asb_page);
	if(isa_readb(ti->asb + offsetof(struct asb_rec, ret_code))!=0xFF)
		DPRINTK("ASB not free !!!\n");

	isa_writeb(REC_DATA,
	       ti->asb + offsetof(struct asb_rec, command));
	isa_writew(rarb.station_id,
	       ti->asb + offsetof(struct asb_rec, station_id));
	isa_writew(rarb.rec_buf_addr,
	       ti->asb + offsetof(struct asb_rec, rec_buf_addr));

	lan_hdr_len=rarb.lan_hdr_len;
	hdr_len = lan_hdr_len + sizeof(struct trllc) + sizeof(struct iphdr);

	SET_PAGE(rbuffer_page);
	llc=(rbuffer + offsetof(struct rec_buf, data) + lan_hdr_len);

#if TR_VERBOSE
	DPRINTK("offsetof data: %02X lan_hdr_len: %02X\n",
		(unsigned int)offsetof(struct rec_buf,data), (unsigned int)lan_hdr_len);
	DPRINTK("llc: %08X rec_buf_addr: %04X ti->sram: %lx\n", llc,
		ntohs(rarb.rec_buf_addr),
		(long)ti->sram);
	DPRINTK("dsap: %02X, ssap: %02X, llc: %02X, protid: %02X%02X%02X, "
		"ethertype: %04X\n",
		(int)isa_readb(llc + offsetof(struct trllc, dsap)),
		(int)isa_readb(llc + offsetof(struct trllc, ssap)),
		(int)isa_readb(llc + offsetof(struct trllc, llc)),
		(int)isa_readb(llc + offsetof(struct trllc, protid)),
		(int)isa_readb(llc + offsetof(struct trllc, protid)+1),
		(int)isa_readb(llc + offsetof(struct trllc, protid)+2),
		(int)isa_readw(llc + offsetof(struct trllc, ethertype)));
#endif
	if (isa_readb(llc + offsetof(struct trllc, llc))!=UI_CMD) {
                SET_PAGE(ti->asb_page);
		isa_writeb(DATA_LOST, ti->asb + offsetof(struct asb_rec, ret_code));
		ti->tr_stats.rx_dropped++;
		isa_writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
		return;
	}

	length = ntohs(rarb.frame_len);
	if ((isa_readb(llc + offsetof(struct trllc, dsap))==EXTENDED_SAP) &&
       	    (isa_readb(llc + offsetof(struct trllc, ssap))==EXTENDED_SAP) &&
		(length>=hdr_len)) {
       		IPv4_p = 1;
       	}

#if TR_VERBOSE
       	if (!IPv4_p){

       		__u32 trhhdr;

       		trhhdr=(rbuffer+offsetof(struct rec_buf,data));

       		DPRINTK("Probably non-IP frame received.\n");
       		DPRINTK("ssap: %02X dsap: %02X saddr: %02X:%02X:%02X:%02X:%02X:%02X "
       			"daddr: %02X:%02X:%02X:%02X:%02X:%02X\n",
       			(int)isa_readb(llc + offsetof(struct trllc, ssap)),
       			(int)isa_readb(llc + offsetof(struct trllc, dsap)),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, saddr)),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, saddr)+1),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, saddr)+2),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, saddr)+3),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, saddr)+4),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, saddr)+5),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, daddr)),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, daddr)+1),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, daddr)+2),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, daddr)+3),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, daddr)+4),
       			(int)isa_readb(trhhdr + offsetof(struct trh_hdr, daddr)+5));
       	}
#endif

       	skb_size = length;
 
       	if (!(skb=dev_alloc_skb(skb_size))) {
       		DPRINTK("out of memory. frame dropped.\n");
       		ti->tr_stats.rx_dropped++;
		SET_PAGE(ti->asb_page);
       		isa_writeb(DATA_LOST, ti->asb + offsetof(struct asb_rec, ret_code));
       		isa_writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
       		return;
       	}

      	skb_put(skb, length);
	skb_reserve(skb, sizeof(struct trh_hdr)-lan_hdr_len+sizeof(struct trllc));
       	skb->dev=dev;
       	data=skb->data;
	rbuffer_len=ntohs(isa_readw(rbuffer + offsetof(struct rec_buf, buf_len)));
	rbufdata = rbuffer + offsetof(struct rec_buf,data);

	if (IPv4_p) {
                /* Copy the headers without checksumming */
		isa_memcpy_fromio(data, rbufdata, hdr_len);

		/* Watch for padded packets and bogons */
		iph=(struct iphdr*)(data + lan_hdr_len + sizeof(struct trllc));
		ip_len = ntohs(iph->tot_len) - sizeof(struct iphdr);
		length -= hdr_len;
		if ((ip_len <= length) && (ip_len > 7))
			length = ip_len;
		data += hdr_len;
		rbuffer_len -= hdr_len;
		rbufdata += hdr_len;
        }

	/* Copy the payload... */
	for (;;) {
		if (IPv4_p)
			chksum = csum_partial_copy_nocheck(bus_to_virt(rbufdata), data,
						   length < rbuffer_len ? length : rbuffer_len,
						   chksum);
		else
			isa_memcpy_fromio(data, rbufdata, rbuffer_len);
		rbuffer = ntohs(isa_readw(rbuffer));
		if (!rbuffer)
			break;
		length -= rbuffer_len;
		data += rbuffer_len;
		if (ti->page_mask) {
		  rbuffer_page=(rbuffer>>8) & ti->page_mask;
		  rbuffer &= ~(ti->page_mask << 8);
		}
		rbuffer += ti->sram;
		SET_PAGE(rbuffer_page);
		rbuffer_len = ntohs(isa_readw(rbuffer + offsetof(struct rec_buf, buf_len)));
		rbufdata = rbuffer + offsetof(struct rec_buf, data);
	}

	SET_PAGE(ti->asb_page);
       	isa_writeb(0, ti->asb + offsetof(struct asb_rec, ret_code));

       	isa_writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

	ti->tr_stats.rx_bytes += skb->len;
       	ti->tr_stats.rx_packets++;

	skb->protocol = tr_type_trans(skb,dev);
 	if (IPv4_p && (skb->protocol == ETH_P_IP)) { 
		skb->csum      = chksum;
		skb->ip_summed = 1;
	}
	netif_rx(skb);
}

static int tok_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct tok_info *ti;
	unsigned long flags;
	ti=(struct tok_info *) dev->priv;

	netif_stop_queue(dev);
	
	/* lock against other CPUs */
	spin_lock_irqsave(&(ti->lock), flags);

	/* Save skb; we'll need it when the adapter asks for the data */
	ti->current_skb=skb;
	SET_PAGE(ti->srb_page);
	isa_writeb(XMIT_UI_FRAME, ti->srb + offsetof(struct srb_xmit, command));
	isa_writew(ti->exsap_station_id, ti->srb
	       +offsetof(struct srb_xmit, station_id));
	isa_writeb(CMD_IN_SRB, (ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD));
	spin_unlock_irqrestore(&(ti->lock), flags);
	dev->trans_start=jiffies;
	return 0;
}

void ibmtr_reset_timer(struct timer_list *tmr, struct net_device *dev) {
	tmr->expires  = jiffies + TR_RETRY_INTERVAL;
	tmr->data     = (unsigned long) dev;
	tmr->function = tok_open_adapter;
	init_timer(tmr);
	add_timer(tmr);
}

void ibmtr_readlog(struct net_device *dev) {
	 struct tok_info *ti;
	 ti=(struct tok_info *) dev->priv;

	 ti->readlog_pending = 0;
	 SET_PAGE(ti->srb_page);
	 isa_writeb(DIR_READ_LOG, ti->srb);
	 isa_writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	 isa_writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	 
	 netif_stop_queue(dev);
}

/* tok_get_stats():  Basically a scaffold routine which will return
   the address of the tr_statistics structure associated with
   this device -- the tr.... structure is an ethnet look-alike
   so at least for this iteration may suffice.   */

static struct net_device_stats * tok_get_stats(struct net_device *dev) {

	struct tok_info *toki;
	toki=(struct tok_info *) dev->priv;
	return (struct net_device_stats *) &toki->tr_stats;
}

int ibmtr_change_mtu(struct net_device *dev, int mtu) {
	struct tok_info *ti = (struct tok_info *) dev->priv;
	
	if (ti->ring_speed == 16 && mtu > ti->maxmtu16)
		return -EINVAL;
	if (ti->ring_speed == 4 && mtu > ti->maxmtu4)
		return -EINVAL;
	dev->mtu = mtu;
	return 0;
}

#ifndef PCMCIA
#ifdef MODULE

/* 3COM 3C619C supports 8 interrupts, 32 I/O ports */
static struct net_device* dev_ibmtr[IBMTR_MAX_ADAPTERS];
static int io[IBMTR_MAX_ADAPTERS] = {0xa20,0xa24};
static int irq[IBMTR_MAX_ADAPTERS];
static int mem[IBMTR_MAX_ADAPTERS];

MODULE_PARM(io, "1-" __MODULE_STRING(IBMTR_MAX_ADAPTERS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(IBMTR_MAX_ADAPTERS) "i");
MODULE_PARM(mem, "1-" __MODULE_STRING(IBMTR_MAX_ADAPTERS) "i");

int init_module(void)
{
        int i;
        for (i = 0; io[i] && (i<IBMTR_MAX_ADAPTERS); i++) {
	        irq[i] = 0;
		mem[i] = 0;
		dev_ibmtr[i] = NULL;
                dev_ibmtr[i] = init_trdev(dev_ibmtr[i], 0);
                if (dev_ibmtr[i] == NULL)
                        return -ENOMEM;

	        dev_ibmtr[i]->base_addr = io[i];
	        dev_ibmtr[i]->irq       = irq[i];
		dev_ibmtr[i]->mem_start = mem[i];
		dev_ibmtr[i]->init      = &ibmtr_probe;

	        if (register_trdev(dev_ibmtr[i]) != 0) {
			kfree(dev_ibmtr[i]);
			dev_ibmtr[i] = NULL;
		        if (i == 0) {
			        printk("ibmtr: register_trdev() returned non-zero.\n");
		                return -EIO;
			} else {
			        return 0;
			}
	        }
	}
	return 0;
}

void cleanup_module(void)
{
        int i;

        for (i = 0; i < IBMTR_MAX_ADAPTERS; i++)
	        if (dev_ibmtr[i]) {
			 unregister_trdev(dev_ibmtr[i]);
			 free_irq(dev_ibmtr[i]->irq, dev_ibmtr[i]);
			 release_region(dev_ibmtr[i]->base_addr, IBMTR_IO_EXTENT);
			 kfree(dev_ibmtr[i]->priv);
			 kfree(dev_ibmtr[i]);
			 dev_ibmtr[i] = NULL;
                }
}
#endif /* MODULE */
#endif
