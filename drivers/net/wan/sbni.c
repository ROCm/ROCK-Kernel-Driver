/*
 * Driver for Granch SBNI-12 leased line network adapters.
 * 
 * Copyright 1997 - 1999, Granch ltd.
 * Written 1999 by Yaroslav Polyakov (xenon@granch.ru).
 *
 * This software may be used and distributed according to the terms
 * of the GNU Public License, incorporated herein by reference.
 * 
 *   // Whole developers team:
 *   //   Yaroslav Polyakov (xenon@granch.ru)
 *   //      - main developer of this version
 *   //   Alexey Zverev (zverev@granch.ru)
 *   //      - previous SBNI driver for linux
 *   //   Alexey Chirkov (chirkov@granch.ru)
 *   //      - all the hardware work and consulting
 *   //   Max Khon (max@iclub.nsu.ru)
 *   //      - first SBNI driver for linux
 *   // --------------------------------------------
 *   // also I thank: 
 *   //   Max Krasnyansky (max@uznet.net)
 *   //      - for bug hunting and many ideas
 *   //   Alan Cox (Alan.Cox@linux.org)
 *   //	     - for consulting in some hardcore questions
 *   //   Donald Becker (becker@cesdis.gsfc.nasa.gov)
 *   //      - for pretty nice skeleton 
 * 
 *   More info and useful utilities to work w/ SBNI you can find at 
 *   http://www.granch.ru.
 *
 *  3.0.0 = Initial Revision, Yaroslav Polyakov (24 Feb 1999)
 *        - added pre-calculation for CRC, fixed bug with "len-2" frames, 
 *        - removed outbound fragmentation (MTU=1000), written CRC-calculation 
 *        - on asm, added work with hard_headers and now we have our own cache 
 *        - for them, optionally supported word-interchange on some chipsets,
 *        - something else I cant remember ;) 
 * 
 *  3.0.1 = just fixed some bugs (14 apr 1999).
 * 	  - fixed statistical tx bug 
 *        - fixed wrong creation dates (1998 -> 1999) in driver source code ;)
 * 	  - fixed source address bug.
 *        - fixed permanent nirvana bug 
 * 
 *  3.1.0 = (Katyusha) (26 apr 1999)
 *        - Added balancing feature
 * 
 *  3.1.1 = (Medea) (5 aug 1999)
 *        - Fixed mac.raw bug
 *	  - Thanks to tolix@olviko.ru and 
 *        - to Barnaul Brewery, producers of my favorite beer "Medea".
 *
 *
 */


#undef GOODBUS16
#define CRCASM
#define KATYUSHA

#include <linux/version.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/types.h>
#include <asm/byteorder.h>


#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/config.h>	/* for CONFIG_INET. do we need this?*/

#include <net/arp.h>

#include <asm/uaccess.h>
#include <linux/init.h>

#include "sbni.h"


static const char *version = 
"sbni.c: ver. 3.1.1 Medea 5 Aug 1999 Yaroslav Polyakov (xenon@granch.ru)\n";

int sbni_probe(struct net_device *dev);
static int  sbni_probe1(struct net_device *dev, int ioaddr);
static int  sbni_open(struct net_device *dev);
static int  sbni_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void sbni_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int  sbni_close(struct net_device *dev);
static void sbni_drop_tx_queue(struct net_device *dev);
static struct net_device_stats *sbni_get_stats(struct net_device *dev);
static void card_start(struct net_device *dev);
static inline unsigned short sbni_recv(struct net_device *dev);
void change_level(struct net_device *dev);
static inline void sbni_xmit(struct net_device *dev);
static inline void sbni_get_packet(struct net_device* dev);
static void sbni_watchdog(unsigned long arg);
static void set_multicast_list(struct net_device *dev);
static int sbni_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static int sbni_set_mac_address(struct net_device *dev, void *addr);
unsigned long calc_crc(char *mem, int len, unsigned initial);
void sbni_nirvana(struct net_device *dev);
static int sbni_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
		void *daddr, void *saddr, unsigned len);

static int sbni_rebuild_header(struct sk_buff *skb);
static int sbni_header_cache(struct neighbour *neigh, struct hh_cache *hh);

static inline void sbni_outs(int port, void *data, int len);
static inline void sbni_ins(int port, void *data, int len);



#define SIZE_OF_TIMEOUT_RXL_TAB 4
static u_char timeout_rxl_tab[] = {
  0x03, 0x05, 0x08, 0x0b
};

static u_char rxl_tab[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 
  0x0a, 0x0c, 0x0f, 0x16, 0x18, 0x1a, 0x1c, 0x1f
};

/* A zero-terminated list of I/O addresses to be probed */
static unsigned int netcard_portlist[] =  { 
	0x210, 0x2c0, 0x2d0, 0x2f0, 0x220, 0x230, 0x240, 0x250, 
	0x260, 0x290, 0x2a0, 0x2b0, 0x224, 0x234, 0x244, 0x254, 
	0x264, 0x294, 0x2a4, 0x2b4, 0};

static unsigned char magic_reply[] = {
	0x5a,0x06,0x30,0x00,0x00,0x50,0x65,0x44,0x20
};

static int def_baud = DEF_RATE;
static int def_rxl = DEF_RXL_DELTA;
static long def_mac = 0;


/*
 * CRC-32 stuff
 */

#define CRC32(c,crc) (crc32tab[((size_t)(crc) ^ (c)) & 0xff] ^ (((crc) >> 8) & 0x00FFFFFF))
/* CRC generator 0xEDB88320 */
/* CRC remainder 0x2144DF1C */
/* CRC initial value 0x00000000 */
#define CRC32_REMAINDER 0x2144DF1C
#define CRC32_INITIAL 0x00000000

static unsigned long crc32tab[] = {
	0xD202EF8D,  0xA505DF1B,  0x3C0C8EA1,  0x4B0BBE37,
	0xD56F2B94,  0xA2681B02,  0x3B614AB8,  0x4C667A2E,
	0xDCD967BF,  0xABDE5729,  0x32D70693,  0x45D03605,
	0xDBB4A3A6,  0xACB39330,  0x35BAC28A,  0x42BDF21C,
	0xCFB5FFE9,  0xB8B2CF7F,  0x21BB9EC5,  0x56BCAE53,
	0xC8D83BF0,  0xBFDF0B66,  0x26D65ADC,  0x51D16A4A,
	0xC16E77DB,  0xB669474D,  0x2F6016F7,  0x58672661,
	0xC603B3C2,  0xB1048354,  0x280DD2EE,  0x5F0AE278,
	0xE96CCF45,  0x9E6BFFD3,  0x0762AE69,  0x70659EFF,
	0xEE010B5C,  0x99063BCA,  0x000F6A70,  0x77085AE6,
	0xE7B74777,  0x90B077E1,  0x09B9265B,  0x7EBE16CD,
	0xE0DA836E,  0x97DDB3F8,  0x0ED4E242,  0x79D3D2D4,
	0xF4DBDF21,  0x83DCEFB7,  0x1AD5BE0D,  0x6DD28E9B,
	0xF3B61B38,  0x84B12BAE,  0x1DB87A14,  0x6ABF4A82,
	0xFA005713,  0x8D076785,  0x140E363F,  0x630906A9,
	0xFD6D930A,  0x8A6AA39C,  0x1363F226,  0x6464C2B0,
	0xA4DEAE1D,  0xD3D99E8B,  0x4AD0CF31,  0x3DD7FFA7,
	0xA3B36A04,  0xD4B45A92,  0x4DBD0B28,  0x3ABA3BBE,
	0xAA05262F,  0xDD0216B9,  0x440B4703,  0x330C7795,
	0xAD68E236,  0xDA6FD2A0,  0x4366831A,  0x3461B38C,
	0xB969BE79,  0xCE6E8EEF,  0x5767DF55,  0x2060EFC3,
	0xBE047A60,  0xC9034AF6,  0x500A1B4C,  0x270D2BDA,
	0xB7B2364B,  0xC0B506DD,  0x59BC5767,  0x2EBB67F1,
	0xB0DFF252,  0xC7D8C2C4,  0x5ED1937E,  0x29D6A3E8,
	0x9FB08ED5,  0xE8B7BE43,  0x71BEEFF9,  0x06B9DF6F,
	0x98DD4ACC,  0xEFDA7A5A,  0x76D32BE0,  0x01D41B76,
	0x916B06E7,  0xE66C3671,  0x7F6567CB,  0x0862575D,
	0x9606C2FE,  0xE101F268,  0x7808A3D2,  0x0F0F9344,
	0x82079EB1,  0xF500AE27,  0x6C09FF9D,  0x1B0ECF0B,
	0x856A5AA8,  0xF26D6A3E,  0x6B643B84,  0x1C630B12,	
	0x8CDC1683,  0xFBDB2615,  0x62D277AF,  0x15D54739,
	0x8BB1D29A,  0xFCB6E20C,  0x65BFB3B6,  0x12B88320,
	0x3FBA6CAD,  0x48BD5C3B,  0xD1B40D81,  0xA6B33D17,
	0x38D7A8B4,  0x4FD09822,  0xD6D9C998,  0xA1DEF90E,
	0x3161E49F,  0x4666D409,  0xDF6F85B3,  0xA868B525,
	0x360C2086,  0x410B1010,  0xD80241AA,  0xAF05713C,
	0x220D7CC9,  0x550A4C5F,  0xCC031DE5,  0xBB042D73,
	0x2560B8D0,  0x52678846,  0xCB6ED9FC,  0xBC69E96A,
	0x2CD6F4FB,  0x5BD1C46D,  0xC2D895D7,  0xB5DFA541,
	0x2BBB30E2,  0x5CBC0074,  0xC5B551CE,  0xB2B26158,
	0x04D44C65,  0x73D37CF3,  0xEADA2D49,  0x9DDD1DDF,
	0x03B9887C,  0x74BEB8EA,  0xEDB7E950,  0x9AB0D9C6,
	0x0A0FC457,  0x7D08F4C1,  0xE401A57B,  0x930695ED,
	0x0D62004E,  0x7A6530D8,  0xE36C6162,  0x946B51F4,
	0x19635C01,  0x6E646C97,  0xF76D3D2D,  0x806A0DBB,
	0x1E0E9818,  0x6909A88E,  0xF000F934,  0x8707C9A2,
	0x17B8D433,  0x60BFE4A5,  0xF9B6B51F,  0x8EB18589,
	0x10D5102A,  0x67D220BC,  0xFEDB7106,  0x89DC4190,
	0x49662D3D,  0x3E611DAB,  0xA7684C11,  0xD06F7C87,
	0x4E0BE924,  0x390CD9B2,  0xA0058808,  0xD702B89E,
	0x47BDA50F,  0x30BA9599,  0xA9B3C423,  0xDEB4F4B5,
	0x40D06116,  0x37D75180,  0xAEDE003A,  0xD9D930AC,
	0x54D13D59,  0x23D60DCF,  0xBADF5C75,  0xCDD86CE3,
	0x53BCF940,  0x24BBC9D6,  0xBDB2986C,  0xCAB5A8FA,
	0x5A0AB56B,  0x2D0D85FD,  0xB404D447,  0xC303E4D1,
	0x5D677172,  0x2A6041E4,  0xB369105E,  0xC46E20C8,
	0x72080DF5,  0x050F3D63,  0x9C066CD9,  0xEB015C4F,
	0x7565C9EC,  0x0262F97A,  0x9B6BA8C0,  0xEC6C9856,
	0x7CD385C7,  0x0BD4B551,  0x92DDE4EB,  0xE5DAD47D,
	0x7BBE41DE,  0x0CB97148,  0x95B020F2,  0xE2B71064,
	0x6FBF1D91,  0x18B82D07,  0x81B17CBD,  0xF6B64C2B,
	0x68D2D988,  0x1FD5E91E,  0x86DCB8A4,  0xF1DB8832,
	0x616495A3,  0x1663A535,  0x8F6AF48F,  0xF86DC419,
	0x660951BA,  0x110E612C,  0x88073096,  0xFF000000
};

static inline void sbni_outs(int port, void *data, int len)
{
#ifdef GOODBUS16
	outsw(port,data,len/2);
	if(len & 1)
		outb(((char*)data)[len - 1],port);
#else
	outsb(port,data,len);
#endif
}

static inline void sbni_ins(int port, void *data, int len)
{
#ifdef GOODBUS16
	insw(port,data,len/2);
	if(len & 1)
		((char*)data)[len - 1] = inb(port);
#else
	insb(port,data,len);
#endif
}


static int sbni_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
	   void *daddr, void *saddr, unsigned len)
{
	struct sbni_hard_header *hh = (struct sbni_hard_header *) 
			skb_push(skb, sizeof(struct sbni_hard_header));
  
  
	if(type!=ETH_P_802_3) 
		hh->h_proto = htons(type);
	else
		hh->h_proto = htons(len);
  
	if(saddr)
		memcpy(hh->h_source,saddr,dev->addr_len);
	else
		memcpy(hh->h_source,dev->dev_addr,dev->addr_len);

	if(daddr)
	{
		memcpy(hh->h_dest,daddr,dev->addr_len);
		return dev->hard_header_len;
	} 
	return -dev->hard_header_len;
}


int sbni_header_cache(struct neighbour *neigh, struct hh_cache *hh)
{
	unsigned short type = hh->hh_type;
	struct sbni_hard_header *sbni = (struct sbni_hard_header*)
						(((u8*)hh->hh_data) - 8);
	struct net_device *dev = neigh->dev;
  
  
	if (type == __constant_htons(ETH_P_802_3))
		return -1;
  
	sbni->h_proto = type;
	memcpy(sbni->h_source, dev->dev_addr, dev->addr_len);
	memcpy(sbni->h_dest, neigh->ha, dev->addr_len);
	return 0;
}

static int sbni_rebuild_header(struct sk_buff *skb)
{
	struct sbni_hard_header *hh = (struct sbni_hard_header *)skb;
	/*
	 *	Only ARP/IP is currently supported
	 */

	/*
	 *	Try to get ARP to resolve the header.
	 */
  
#ifdef CONFIG_INET
  	return arp_find((unsigned char*)hh->h_dest, skb)? 1 : 0;  
#else
	return 0;	
#endif	
}

static void sbni_header_cache_update(struct hh_cache *hh, struct net_device *dev, unsigned char * haddr)
{
	memcpy(((u8*)hh->hh_data) + 2, haddr, dev->addr_len);
}



#ifdef HAVE_DEVLIST
struct netdev_entry sbni_drv = {
	"sbni", sbni_probe1, SBNI_IO_EXTENT, netcard_portlist 
};

#else

int __init sbni_probe(struct net_device *dev)
{
	int i;
	int base_addr = dev ? dev->base_addr : 0;
	
	DP( printk("%s: sbni_probe\n", dev->name); )

	if(base_addr > 0x1ff)	/* Check a single specified location. */
		return sbni_probe1(dev, base_addr);
	else if(base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;
	for(i = 0; (base_addr = netcard_portlist[i]); i++)
	{ 
		if(!check_region(base_addr, SBNI_IO_EXTENT) && base_addr != 1)
		{
			/* Lock this address, or later we'll try it again */
			netcard_portlist[i] = 1;
			if(sbni_probe1(dev, base_addr) == 0)
				return 0;
		}
	}
	return -ENODEV;
}

#endif /* have devlist*/

/*
 *	The actual probe. 
 */

/*
	Valid combinations in CSR0 (for probing):

	VALID_DECODER	0000,0011,1011,1010

				    	; 0   ; -
				TR_REQ	; 1   ; +
			TR_RDY	    	; 2   ; -
			TR_RDY	TR_REQ	; 3   ; +
		BU_EMP		    	; 4   ; +
		BU_EMP	     	TR_REQ	; 5   ; +
		BU_EMP	TR_RDY	    	; 6   ; -
		BU_EMP	TR_RDY	TR_REQ	; 7   ; +
	RC_RDY 		     		; 8   ; +
	RC_RDY			TR_REQ	; 9   ; +
	RC_RDY		TR_RDY		; 10  ; -
	RC_RDY		TR_RDY	TR_REQ	; 11  ; -
	RC_RDY	BU_EMP			; 12  ; -
	RC_RDY	BU_EMP		TR_REQ	; 13  ; -
	RC_RDY	BU_EMP	TR_RDY		; 14  ; -
	RC_RDY	BU_EMP	TR_RDY	TR_REQ	; 15  ; -
*/
#define VALID_DECODER (2 + 8 + 0x10 + 0x20 + 0x80 + 0x100 + 0x200)

static int __init sbni_probe1(struct net_device *dev, int ioaddr)

{
	int autoirq = 0;
	int bad_card = 0;
	unsigned char csr0;
	struct net_local* lp;
	static int version_printed = 0;

	DP( printk("%s: sbni_probe1 ioaddr=%d\n", dev->name, ioaddr); )
       
	if(check_region(ioaddr, SBNI_IO_EXTENT) < 0)
		return -ENODEV;
	if(version_printed++ == 0)
		printk(version);
     
	/* check for valid combination in CSR0 */
	csr0 = inb(ioaddr + CSR0);
	if(csr0 == 0xff || csr0 == 0)
		bad_card = 1;
	else 
	{
		csr0 &= ~EN_INT;
		if(csr0 & BU_EMP)
			csr0 |= EN_INT;
		if((VALID_DECODER & (1 << (csr0 >> 4))) == 0)
			bad_card = 1;
	}

	if(bad_card)
		return -ENODEV;
	else
		outb(0, ioaddr + CSR0); 
	if(dev->irq < 2)
	{
		DP( printk("%s: autoprobing\n", dev->name); );
		autoirq_setup(5);
		outb(EN_INT | TR_REQ, ioaddr + CSR0);
		outb(PR_RES, ioaddr + CSR1);
		autoirq = autoirq_report(5);

		if(autoirq == 0)
		{
			printk("sbni probe at %#x failed to detect IRQ line\n", ioaddr);
			return -EAGAIN;
		}
	}
	/* clear FIFO buffer */
	outb(0, ioaddr + CSR0);
   
	if(autoirq)
		dev->irq = autoirq;

	{
   		int irqval=request_irq(dev->irq, sbni_interrupt, 0, dev->name, dev);
		if (irqval) 
		{
			printk (" unable to get IRQ %d (irqval=%d).\n", dev->irq, irqval);
			return -EAGAIN;
		}
	}
     
	/* 
	 *	Initialize the device structure. 
	 */

	dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	if(dev->priv == NULL)
	{
		DP( printk("%s: cannot allocate memory\n", dev->name); )
		free_irq(dev->irq, dev);
		return -ENOMEM;
	}
   
	memset(dev->priv, 0, sizeof(struct net_local));
	dev->base_addr = ioaddr;
	request_region(ioaddr, SBNI_IO_EXTENT, "sbni");

	/* 
	 * generate Ethernet address (0x00ff01xxxxxx)
	 */

	*(u16*)dev->dev_addr = htons(0x00ff);
	*(u32*)(dev->dev_addr+2) = htonl(((def_mac ? def_mac : (u32) dev->priv) & 0x00ffffff) | 0x01000000);
   
	lp = dev->priv;
	if(def_rxl < 0)
	{
		/* autodetect receive level */
		lp->rxl_curr = 0xf;
		lp->rxl_delta = -1;
	} else {
		/* fixed receive level */
		lp->rxl_curr = def_rxl & 0xf;
		lp->rxl_delta = 0;
	}
	lp->csr1.rxl = rxl_tab[lp->rxl_curr];
	lp->csr1.rate = def_baud & 3;
	lp->frame_len = DEF_FRAME_LEN;
	printk("%s: sbni adapter at %#lx, using %sIRQ %d, MAC: 00:ff:01:%x:%x:%x\n", 
		dev->name, dev->base_addr, autoirq ? "auto":"assigned ", dev->irq,
	       *(unsigned char*)(dev->dev_addr+3),
	       *(unsigned char*)(dev->dev_addr+4),
	       *(unsigned char*)(dev->dev_addr+5)
	);

	printk("%s: receive level: ", dev->name);
	if(lp->rxl_delta == 0)
		printk ("%#1x (fixed)", lp->rxl_curr); 
	else
		printk ("autodetect");
	printk(", baud rate: %u\n", (unsigned)lp->csr1.rate);
   
	/*
	 *	The SBNI-specific entries in the device structure. 
	 */
	dev->open = &sbni_open;
	dev->hard_start_xmit = &sbni_start_xmit;
	dev->stop = &sbni_close;
	dev->get_stats = &sbni_get_stats;
	dev->set_multicast_list = &set_multicast_list;
	dev->set_mac_address = &sbni_set_mac_address;
	dev->do_ioctl = &sbni_ioctl;
   
	/*
	 *	Setup the generic properties 
	 */

	ether_setup(dev);
   
	dev->hard_header = sbni_header;
	dev->hard_header_len = sizeof(struct sbni_hard_header);
	dev->rebuild_header=sbni_rebuild_header;
	dev->mtu = DEF_FRAME_LEN;

	dev->hard_header_cache = sbni_header_cache;
	dev->header_cache_update = sbni_header_cache_update;
  
  	spin_lock_init(&lp->lock);
	lp->m=dev;
	lp->me=dev;
	lp->next_lp=NULL;
  
	return 0;
}

/*
 *	Open/initialize the board. 
 */

static int sbni_open(struct net_device *dev)
{
	struct net_local* lp = (struct net_local*)dev->priv;
	struct timer_list* watchdog = &lp->watchdog;
	unsigned long flags;   
      
	DP( printk("%s: sbni_open\n", dev->name); )
     
     	save_flags(flags);
	cli();
	lp->currframe = NULL;
   
	card_start(dev);
	/* set timer  watchdog */
	init_timer(watchdog);
	watchdog->expires = jiffies + SBNI_TIMEOUT;
	watchdog->data = (unsigned long)dev;
	watchdog->function = sbni_watchdog;
	add_timer(watchdog);
	DP( printk("%s: sbni timer watchdog initialized\n", dev->name); );
   
	restore_flags(flags);
	   
	netif_start_queue(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static int sbni_close(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct net_local* lp = (struct net_local*) dev->priv;
	struct timer_list* watchdog = &lp->watchdog;
	unsigned long flags;
	
	DP( printk("%s: sbni_close\n", dev->name); )

	netif_stop_queue(dev);

	save_flags(flags);
	cli();
	sbni_drop_tx_queue(dev);	
	del_timer(watchdog);
	outb(0, ioaddr + CSR0);
	restore_flags(flags);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int sbni_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local*)dev->priv;
	struct sbni_hard_header *hh=(struct sbni_hard_header *)skb->data;
	unsigned long flags;
  
#ifdef KATYUSHA   
	struct net_local *nl;
	int stop;
#endif
  
	DP( printk("%s: sbni_start_xmit In \n", dev->name); );
  
  
	if(lp->me != dev)
		panic("sbni: lp->me != dev !!!\nMail to developer (xenon@granch.ru) if you noticed this error\n");
  
	hh->number = 1;
	hh->reserv = 0;
  
	hh->packetlen =  (skb->len - sizeof (unsigned short) - 
			(sizeof(struct sbni_hard_header) - SBNI_HH_SZ)) 
			| PACKET_SEND_OK | PACKET_FIRST_FRAME;
  
	/* we should use hairy method to calculate crc because of extra bytes are 
	  livin between hard header and data*/
	hh->crc = calc_crc((void*)&hh->packetlen, SBNI_HH_SZ - sizeof(unsigned), CRC32_INITIAL);
	hh->crc = calc_crc(skb->data + sizeof(struct sbni_hard_header),
		       skb->len - sizeof(struct sbni_hard_header),
		       hh->crc);
  
  	spin_lock_irqsave(&lp->lock, flags);
#ifdef KATYUSHA
	/* looking for first idle device */
	for (stop=0,nl=lp; nl && !stop; nl=nl->next_lp)
	{
		if((!nl->currframe) && (nl->carrier)) /* if idle */
		{
			skb->dev = lp->me;
			nl->currframe = skb;
			/* set request for transmit */
			outb(inb(nl->me->base_addr + CSR0) | TR_REQ, 
				nl->me->base_addr + CSR0);
			stop=1;
		}
	}
  
	if(!stop) /* we havent found any idle.*/
	{
		skb_queue_tail(&lp->queue,skb);
		outb(inb(dev->base_addr + CSR0) | TR_REQ, dev->base_addr + CSR0);
      
	}		
#else 
	if (lp->currframe || 1)
	{
		skb_queue_tail(&lp->queue,skb);
		  
	}
	else
	{
		lp->currframe = skb;
	}
	/* set request for transmit */
	outb(inb(dev->base_addr + CSR0) | TR_REQ, dev->base_addr + CSR0);
#endif
	spin_unlock_irqrestore(&lp->lock, flags);
	return 0;
}

static void card_start(struct net_device *dev)
{
	struct net_local *lp = (struct net_local*)dev->priv;
   
	DP( printk("%s: card_start\n",dev->name); )
	lp->wait_frame_number = 0;
	lp->inppos = lp->outpos = 0;
	lp->eth_trans_buffer_len = 0;
	lp->tr_err = TR_ERROR_COUNT;
	lp->last_receive_OK = FALSE;
	lp->tr_resend = FALSE;
	lp->timer_ticks = CHANGE_LEVEL_START_TICKS;
	lp->timeout_rxl = 0;

	lp->waitack=0;
	skb_queue_head_init(&lp->queue);
	sbni_drop_tx_queue(dev);
	/* Reset the card and set start parameters */
	outb(PR_RES | *(char*)&lp->csr1, dev->base_addr + CSR1);
	outb(EN_INT, dev->base_addr + CSR0);
}

void sbni_nirvana(struct net_device *dev)
{
	sbni_outs(dev->base_addr+DAT,magic_reply,9);
}

static inline unsigned short sbni_recv(struct net_device *dev)
{
	struct net_local *lp = (struct net_local*)dev->priv;
	unsigned long crc;
	unsigned short packetlen = 0;
	unsigned short packetinf, packetfirst, receiveframeresend;
	unsigned char current_frame;
	unsigned int i, j;
	unsigned char delme,rcv_res=RCV_WR;
  
	lp->in_stats.all_rx_number++;
  
	if((delme=inb(dev->base_addr + DAT)) == SBNI_SIG)
	{
		crc = CRC32_INITIAL;
		*(((unsigned char *)&packetlen) + 0) = inb(dev->base_addr + DAT);
		crc = CRC32(*(((unsigned char *)&packetlen) + 0), crc);
		*(((unsigned char *)&packetlen) + 1) = inb(dev->base_addr + DAT);
		crc = CRC32(*(((unsigned char *)&packetlen) + 1), crc);
		packetinf = packetlen & PACKET_INF_MASK;
		packetfirst = packetlen & PACKET_FIRST_FRAME;
		receiveframeresend = packetlen & RECEIVE_FRAME_RESEND;
		packetlen = packetlen & PACKET_LEN_MASK;
    
    
		if((packetlen <= SB_MAX_BUFFER_ARRAY - 3) && (packetlen >= 6))
		{
			/* read frame number */
			current_frame = inb(dev->base_addr + DAT);
			crc = CRC32(current_frame, crc);
			/* read HandShake counter */
			lp->HSCounter = inb(dev->base_addr + DAT);
			crc = CRC32(lp->HSCounter, crc);
			packetlen -= 2;
      
			sbni_ins(dev->base_addr + DAT, lp->eth_rcv_buffer + lp->inppos, packetlen);
      
			for(i = lp->inppos; i < (packetlen + lp->inppos); i++)
			{
				crc = CRC32(lp->eth_rcv_buffer[i], crc);
			}
      
			if(crc == CRC32_REMAINDER)
			{
				if(packetlen > 4) 
					rcv_res=RCV_OK;
				else if(packetlen == 4) 
					rcv_res=RCV_NO;
      		
				if(lp->waitack && packetinf == PACKET_RESEND)
					lp->in_stats.resend_tx_number++;
	
	
				switch(packetinf)
				{
				case PACKET_SEND_OK:
				{
					lp->tr_err = TR_ERROR_COUNT;
					lp->tr_resend = FALSE;
					/* if(lp->trans_frame_number){ */
					lp->outpos += lp->realframelen;
	      
					/* SendComplete
					 * not supported
					 */
					DP( printk("%s: sbni_recv SendComplete\n",dev->name); );
					/*
					 *	We sucessfully sent current packet
					 */
	      
					if(lp->waitack)
					{
						dev_kfree_skb(lp->currframe);
						lp->stats.tx_packets++;
#ifdef KATYUSHA
						lp->currframe=skb_dequeue(&(((struct net_local*) (lp->m->priv))->queue));
#else
						lp->currframe=skb_dequeue(&lp->queue);
#endif		       
						lp->in_stats.all_tx_number++;
						lp->waitack=0;
					}
	      
					/*
	      				 * reset output active flags
					 */
					netif_wake_queue(dev);
					/*} if */
				}
				case PACKET_RESEND:
				{
					if(lp->tr_err) /**/
						lp->tr_err--;
					if(lp->ok_curr < 0xffffffff)
						lp->ok_curr++;
					if(packetlen > 4 && !(lp->last_receive_OK && receiveframeresend))
					{
						if(packetfirst)
						{
							if(lp->wait_frame_number)
							{
								for(i = lp->inppos, j = 0; 
									i < (lp->inppos + packetlen - 4); 
									i++, j++)
								lp->eth_rcv_buffer[j] = lp->eth_rcv_buffer[i];
							}
							lp->wait_frame_number = current_frame;
							lp->inppos = 0;
						}
						if(current_frame == lp->wait_frame_number)
						{
							lp->inppos += (packetlen - 4);
							if(lp->wait_frame_number == 1)
		  					{
								sbni_get_packet(dev);
								lp->inppos = 0;
							}
							lp->wait_frame_number--;
						}
					}
					lp->last_receive_OK = TRUE;
					break;
				}
				default:
					break;
				}
			}
			else 
			{
				DP(printk("%s: bad CRC32\n",dev->name));
				change_level(dev);
			}
		}
		else 
		{
			DP(printk("%s: bad len\n ",dev->name));
			change_level(dev);
			lp->stats.rx_over_errors++;
		}
	}
	else 
	{
		DP(printk("%s: bad sig\n",dev->name));
		change_level(dev);
	}
	outb(inb(dev->base_addr + CSR0) ^ CT_ZER, dev->base_addr + CSR0);
	return (rcv_res);
}

void change_level(struct net_device *dev)
{
	struct net_local *lp = (struct net_local*)dev->priv;

	lp->in_stats.bad_rx_number++;
	lp->stats.tx_errors++;
	if(lp->rxl_delta == 0)
		return;
	/* 
	 * set new rxl_delta value
	 */
	if(lp->rxl_curr == 0)
		lp->rxl_delta = 1;
	else if(lp->rxl_curr == 0xf)
		lp->rxl_delta = -1;
	else if(lp->ok_curr < lp->ok_prev)
		lp->rxl_delta = -lp->rxl_delta;
	/*
	 * set new rxl_curr value
	 */
	lp->csr1.rxl = rxl_tab[lp->rxl_curr += lp->rxl_delta];
	outb(*(char*)&lp->csr1, dev->base_addr + CSR1);
  
  
	/*
	 * update ok_prev/ok_curr counters
	 */
	lp->ok_prev = lp->ok_curr;
	lp->ok_curr = 0;

	DP( printk("%s: receive error, rxl_curr = %d, rxl_delta = %d\n",\
		   dev->name,lp->rxl_curr, lp->rxl_delta); )
     
}

static inline void sbni_xmit(struct net_device *dev)
{
	struct net_local* lp = (struct net_local *)dev->priv;
	struct sk_buff *skb;
 
	skb=lp->currframe;
  
	DP( printk("%s: sbni_xmit CSR0=%02x\n",dev->name, (unsigned char)inb(dev->base_addr + CSR0)); );
	  
	/* push signature*/  
	outb(SBNI_SIG, dev->base_addr + DAT);
	
	/* push frame w/o crc [HAiRY]*/
	sbni_outs(dev->base_addr + DAT,
	      &((struct sbni_hard_header *)(skb->data))->packetlen,
	      SBNI_HH_SZ - sizeof(unsigned)); 
	
	sbni_outs(dev->base_addr + DAT,
	      skb->data + sizeof(struct sbni_hard_header),
	      skb->len - sizeof(struct sbni_hard_header)); /* успеем еще */

	/* push crc */
	sbni_outs(dev->base_addr + DAT, skb->data, sizeof(unsigned));
	
	lp->waitack=1;
}

/*
 *	The typical workload of the driver:
 *	Handle the ether interface interrupts. 
 */
static void sbni_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct net_local* lp;
	u_char csr0;
	unsigned short rcv_res = RCV_NO;
  
  
	if(dev == NULL || dev->irq != irq)
	{
		printk("sbni: irq %d for unknown device\n", irq);
		return;
	}
   
	csr0 = inb(dev->base_addr + CSR0);
	DP( printk("%s: entering interrupt handler, CSR0 = %02x\n", dev->name, csr0); )
     
	lp=dev->priv;

   	spin_lock(&lp->lock);
  
	if(!lp->carrier)
		lp->carrier=1;
  
	/*
	 * Disable adapter interrupts
	 */
	outb((csr0 & ~EN_INT) | TR_REQ, dev->base_addr + CSR0);
	lp->timer_ticks = CHANGE_LEVEL_START_TICKS;
	csr0 = inb(dev->base_addr + CSR0);
   
	if(csr0 & (TR_RDY | RC_RDY))
	{
		if(csr0 & RC_RDY)
			rcv_res = sbni_recv(dev);
	   
		if((lp->currframe) && (rcv_res != RCV_WR))
			sbni_xmit(dev);
		else if (rcv_res == RCV_OK)
			sbni_nirvana(dev);
       
		csr0 = inb(dev->base_addr + CSR0);
		DP( printk("%s: CSR0 = %02x\n",dev->name, (u_int)csr0); );
	}
   
  
	DP( printk("%s: leaving interrupt handler, CSR0 = %02x\n",dev->name, csr0 | EN_INT); );
     
	/* here we should send pong */
	outb(inb(dev->base_addr+CSR0) & ~TR_REQ, dev->base_addr + CSR0);
	if(lp->currframe)
		outb(inb(dev->base_addr+CSR0) | TR_REQ, dev->base_addr + CSR0);
	else
		csr0 = inb(dev->base_addr + CSR0);
  
	/*
	 * Enable adapter interrupts
	 */
  
	outb(csr0 | EN_INT, dev->base_addr + CSR0);
	spin_unlock(&lp->lock);
}

static struct net_device_stats *sbni_get_stats(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	return &lp->stats;
}

static inline void sbni_get_packet(struct net_device* dev)
{
	struct net_local* lp = (struct net_local*)dev->priv;
	struct sk_buff* skb;
	unsigned char *rawp;
    
   
     
	skb = dev_alloc_skb(lp->inppos - ETH_HLEN + sizeof(struct sbni_hard_header));
   
	if(skb == NULL)
	{
		DP( printk("%s: Memory squeeze, dropping packet.\n", dev->name); )
		lp->stats.rx_dropped++;
		return;
	} else {
#ifdef KATYUSHA
		skb->dev = lp->m;
#else
		skb->dev = dev;
#endif      
		memcpy((unsigned char*)skb_put(skb, lp->inppos + 8)+8,
			lp->eth_rcv_buffer,
			lp->inppos);
      
      
		skb->mac.raw = skb->data + 8;
    
		if((*(char*)lp->eth_rcv_buffer) & 1)
		{
			if(memcmp(lp->eth_rcv_buffer,dev->broadcast, ETH_ALEN)==0)
				skb->pkt_type=PACKET_BROADCAST;
			else
				skb->pkt_type=PACKET_MULTICAST;
		}
		else if(dev->flags&(IFF_PROMISC|IFF_ALLMULTI))
		{
			if(memcmp(lp->eth_rcv_buffer,dev->dev_addr, ETH_ALEN))
				skb->pkt_type=PACKET_OTHERHOST;
		}
      
		if( htons(*((unsigned short*)(&lp->eth_rcv_buffer[2*ETH_ALEN]))) >= 1536)
			skb->protocol =  *((unsigned short*)(&lp->eth_rcv_buffer[2*ETH_ALEN]));
		else
		{
			rawp = (unsigned char*)(&lp->eth_rcv_buffer[2*ETH_ALEN]);
			if (*(unsigned short *)rawp == 0xFFFF)
				skb->protocol=htons(ETH_P_802_3);
			else
				skb->protocol=htons(ETH_P_802_2);
		}
            

		skb_pull(skb,SBNI_HH_SZ);
   
		netif_rx(skb);
		lp->stats.rx_packets++;
	}
	return;
}

static void sbni_watchdog(unsigned long arg)
{
	struct net_device* dev = (struct net_device*)arg;
	struct net_local* lp = (struct net_local *)dev->priv;
	u_char csr0;


  
	DP( printk("%s: watchdog start\n",dev->name); )	
	/*
	 * if no pong received and transmission is not in progress
	 * then assume error
	 */
	cli();
	csr0 = inb(dev->base_addr + CSR0);
	if(csr0 & (RC_CHK | TR_REQ))
	{
		if(lp->timer_ticks)
		{
			if(csr0 & (RC_RDY | BU_EMP))
			{
				lp->timer_ticks--;
			}
		}
		else 
		{
			if(lp->rxl_delta)
			{
				lp->ok_prev = lp->ok_curr;
				lp->ok_curr = 0;
				lp->rxl_curr = timeout_rxl_tab[lp->timeout_rxl];
				lp->timeout_rxl++;
				if(lp->timeout_rxl > SIZE_OF_TIMEOUT_RXL_TAB - 1)
					lp->timeout_rxl = 0; 
				lp->csr1.rxl = rxl_tab[lp->rxl_curr];
				/*
				 * update ok_prev/ok_curr counters
				 */
				lp->ok_prev = lp->ok_curr;
				lp->ok_curr = 0;
			}
			if(lp->tr_err)
				lp->tr_err--;
			else 
			{
				/* Drop the queue of tx packets */
				sbni_drop_tx_queue(dev);
				lp->carrier=0;
			}
	     
			/*
			 * send pong
			 */

			csr0 = inb(dev->base_addr + CSR0);
			outb(csr0 & ~TR_REQ, dev->base_addr + CSR0);
			outb(*(char*)(&lp->csr1) | PR_RES, dev->base_addr + CSR1);
			lp->in_stats.timeout_number++;
		}
	}
	sti();
	outb(csr0 | RC_CHK, dev->base_addr + CSR0);
	if(netif_running(dev))
	{
		struct timer_list* watchdog = &lp->watchdog; 
		init_timer(watchdog);
		watchdog->expires = jiffies + SBNI_TIMEOUT;
		watchdog->data = arg;
		watchdog->function = sbni_watchdog;
		add_timer(watchdog);
	}
}

static void sbni_drop_tx_queue(struct net_device *dev)
{
	struct net_local* lp = (struct net_local *)dev->priv,*nl;
	struct sk_buff *tmp;
   
	/* first of all, we should try to gift our packets to another interface */
  
	nl=(struct net_local *)lp->m->priv;
	if(nl==lp)
		nl=lp->next_lp;
  
	if(nl)
	{
		/* we found device*/
		if(lp->currframe)
		{
			if(!nl->currframe)
			{
				nl->currframe=lp->currframe;
			}
			else
			{
				skb_queue_head(&((struct net_local*)(lp->m->priv))->queue,lp->currframe);
			}
		}
		lp->currframe=NULL;

		if(!nl->currframe)
			nl->currframe=skb_dequeue(&(((struct net_local*)(lp->m->priv))->queue));

		/* set request for transmit */
		outb(inb(nl->me->base_addr + CSR0) | TR_REQ,  nl->me->base_addr + CSR0);
    
	}
	else
	{
		/* *sigh*, we should forget this packets */
		nl=lp->m->priv;
    
		while((tmp = skb_dequeue(&nl->queue)) != NULL)
		{
			dev_kfree_skb(tmp);
			lp->stats.tx_packets++;
		}
    
		if (lp->currframe)
		{
			dev_kfree_skb(lp->currframe);
			lp->currframe = NULL;
			lp->stats.tx_packets++;
		}
	}
	lp->waitack=0;
	netif_wake_queue(dev);
	
	DP( printk("%s: queue dropping stoped\n",dev->name); );	
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
 
static void set_multicast_list(struct net_device *dev)
{
	/*
	 * always enabled promiscuous mode.
	*/
	return;
}

static int sbni_set_mac_address(struct net_device *dev, void *addr)
{	
	/* struct net_local *lp = (struct net_local *)dev->priv; */
	struct sockaddr *saddr = addr;
	
	if(netif_running(dev))
	{
		/* Only possible while card isn't started */
		return -EBUSY;
	}
	memcpy(dev->dev_addr, saddr->sa_data, dev->addr_len);
	return (0);
}

static int sbni_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_local* lp = (struct net_local *)dev->priv,*tlp; 
	struct net_device *slave;
	int error = 0;
	char tmpstr[6];
  
  
	switch(cmd)
	{
		case SIOCDEVGETINSTATS:
		{
			struct sbni_in_stats *in_stats = (struct sbni_in_stats *)ifr->ifr_data;
			DP( printk("%s: SIOCDEVGETINSTATS %08x\n",dev->name,(unsigned)in_stats);)
			if(copy_to_user((void *)in_stats, (void *)(&(lp->in_stats)), sizeof(struct sbni_in_stats)))
				return -EFAULT;
			break;
		}
		case SIOCDEVRESINSTATS:
		{
			if(!capable(CAP_NET_ADMIN))
				return -EPERM;
			DP( printk("%s: SIOCDEVRESINSTATS\n",dev->name); )
			lp->in_stats.all_rx_number = 0;
			lp->in_stats.bad_rx_number = 0;
			lp->in_stats.timeout_number = 0;
			lp->in_stats.all_tx_number = 0;
			lp->in_stats.resend_tx_number = 0;
			break;
		}
		case SIOCDEVGHWSTATE:
		{
			struct sbni_flags flags;
			flags.rxl = lp->rxl_curr;
			flags.rate = lp->csr1.rate;
			flags.fixed_rxl = (lp->rxl_delta == 0);
			flags.fixed_rate = 1;
			ifr->ifr_data = *(caddr_t*)&flags;
			DP( printk("%s: get flags (0x%02x)\n",dev->name, (unsigned char)ifr->ifr_data); )
			break;
		}
		case SIOCDEVSHWSTATE:
		{
			struct sbni_flags flags;
			DP( printk("%s: SIOCDEVSHWSTATE flags=0x%02x\n",dev->name, (unsigned char)ifr->ifr_data); )
			/* root only */
			if(!capable(CAP_NET_ADMIN))
				return -EPERM;
			flags = *(struct sbni_flags*)&ifr->ifr_data;
			if(flags.fixed_rxl)
			{
				lp->rxl_delta = 0;
				lp->rxl_curr = flags.rxl;
			}
			else
			{
				lp->rxl_delta = DEF_RXL_DELTA;
				lp->rxl_curr = DEF_RXL;
			}
			lp->csr1.rxl = rxl_tab[lp->rxl_curr];
			if(flags.fixed_rate)
				lp->csr1.rate = flags.rate;
			else
				lp->csr1.rate = DEF_RATE;
			/*
			 * Don't be afraid...
			 */
			outb(*(char*)(&lp->csr1) | PR_RES, dev->base_addr + CSR1);

			DP( printk("%s: set flags (0x%02x)\n receive level: %u, baud rate: %u\n",\
				dev->name, (unsigned char)ifr->ifr_data, (unsigned)lp->rxl_curr, (unsigned)lp->csr1.rate); )
			break;
		}

		case SIOCDEVENSLAVE:
			if(!capable(CAP_NET_ADMIN))
				return -EPERM;
			if(copy_from_user( tmpstr, ifr->ifr_data, 6))
				return -EFAULT;
			slave = dev_get_by_name(tmpstr);
			if(!(slave && slave->flags & IFF_UP && dev->flags & IFF_UP))
			{
				printk("%s: Both devices should be UP to enslave!\n",dev->name);
				if (slave)
					dev_put(slave);
				return -EINVAL;
			}
		
			if(slave)
			{
				if(!((dev->flags & IFF_SLAVE) || (slave->flags & IFF_SLAVE)))
				{
					/* drop queue*/
					sbni_drop_tx_queue(slave);
					slave->flags |= IFF_SLAVE;
					((struct net_local *)(slave->priv))->m=dev;
					while(lp->next_lp)	//tail it after last slave
						lp=lp->next_lp;
					lp->next_lp=slave->priv;
					lp=(struct net_local *)dev->priv;
					dev->flags |= IFF_MASTER;
					}
				else
				{
					printk("%s: one of devices is already slave!\n",dev->name);
					error = -EBUSY;
				}
				dev_put(slave);
			}
			else
			{
				printk("%s: can't find device %s to enslave\n",dev->name,ifr->ifr_data);
				return -ENOENT;
			}
			break;    

		case SIOCDEVEMANSIPATE:
			if(!capable(CAP_NET_ADMIN))
				return -EPERM;

			if(dev->flags & IFF_SLAVE)
			{
				dev->flags &= ~IFF_SLAVE;
				/* exclude us from masters slavelist*/
				for(tlp=lp->m->priv;tlp->next_lp!=lp && tlp->next_lp;tlp=tlp->next_lp);
				if(tlp->next_lp)
				{
					tlp->next_lp = lp->next_lp;
					if(!((struct net_local *)lp->m->priv)->next_lp)
					{
						lp->m->flags &= ~IFF_MASTER;	
					}
					lp->next_lp=NULL;
					lp->m=dev;     	
				}
				else
				{
					printk("%s: Ooops. drivers structure is mangled!\n",dev->name);
					return -EIO;
				}      
			}
			else
			{
				printk("%s: isn't slave device!\n",dev->name);
				return -EINVAL;
			}
			break;    

		default:
			DP( printk("%s: invalid ioctl: 0x%x\n",dev->name, cmd); )
			error = -EINVAL;
	}
	return (error);
}



#ifdef CRCASM

unsigned long calc_crc(char *mem, int len, unsigned initial)
{
	unsigned crc, dummy_len;
	__asm__ (
		"xorl %%eax,%%eax\n\t"
		"1:\n\t"
		"lodsb\n\t"
		"xorb %%dl,%%al\n\t"
		"shrl $8,%%edx\n\t"
		"xorl (%%edi,%%eax,4),%%edx\n\t"
		"loop 1b"
		: "=d" (crc), "=c" (dummy_len)
		: "S" (mem), "D" (&crc32tab[0]), "1" (len), "0" (initial)
		: "eax"
	);
	return crc;
}

#else

unsigned long calc_crc(char *mem, int len, unsigned initial)
{
	unsigned crc;
	crc = initial;
   
	for(;len;mem++,len--)
	{
		crc = CRC32(*mem, crc);
	}
	return(crc);
}
#endif /* CRCASM */
#ifdef MODULE

static int io[SBNI_MAX_NUM_CARDS] = { 0 };
static int irq[SBNI_MAX_NUM_CARDS] = { 0 };
static int rxl[SBNI_MAX_NUM_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int baud[SBNI_MAX_NUM_CARDS] = { 0 };
static long mac[SBNI_MAX_NUM_CARDS] = { 0 };

MODULE_PARM(io, "1-" __MODULE_STRING(SBNI_MAX_NUM_CARDS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(SBNI_MAX_NUM_CARDS) "i");
MODULE_PARM(rxl, "1-" __MODULE_STRING(SBNI_MAX_NUM_CARDS) "i");
MODULE_PARM(baud, "1-" __MODULE_STRING(SBNI_MAX_NUM_CARDS) "i");
MODULE_PARM(mac, "1-" __MODULE_STRING(SBNI_MAX_NUM_CARDS) "i");


static int sbniautodetect = -1;

static struct net_device dev_sbni[SBNI_MAX_NUM_CARDS] = {
	{
		"sbni0",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni1",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni2",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni3",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni4",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni5",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni6",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	},
	{
		"sbni7",
		0, 0, 0, 0,		/* memory */
		0, 0,			/* base, irq */
		0, 0, 0, NULL, sbni_probe 
	}
};

int init_module(void)
{
	int devices = 0;
	int installed = 0;
	int i;

	/* My simple plug for this huge init_module. "XenON */
      
	if(sbniautodetect != -1)
	{
		/* Autodetect mode */
		printk("sbni: Autodetect mode (not recommended!) ...\n");
		if(!sbniautodetect)
			sbniautodetect=SBNI_MAX_NUM_CARDS;
		printk("Trying to find %d SBNI cards...\n", sbniautodetect);
		if(sbniautodetect > SBNI_MAX_NUM_CARDS)
		{
			sbniautodetect = SBNI_MAX_NUM_CARDS;
			printk("sbni: You want to detect too many cards. Truncated to %d\n", SBNI_MAX_NUM_CARDS);
		}
		for(i = 0; i < sbniautodetect; i++)
		{
			if(!register_netdev(&dev_sbni[i]))
				installed++;
		}
		if(installed)
			return 0;
		else
		    return -EIO;
	}
	
	/* Manual mode */
	for(i = 0; i < SBNI_MAX_NUM_CARDS; i++)
	{
		if((io[i] != 0) || (irq[i] != 0))
			devices++;
	}
	for(i = 0; i < devices; i++)
	{
		dev_sbni[i].irq = irq[i];
		dev_sbni[i].base_addr = io[i];
		def_rxl = rxl[i];
		def_baud = baud[i];
		def_mac = mac[i];
		if(register_netdev(&dev_sbni[i]))
			printk("sbni: card not found!\n");
		else
			installed++;
	}
	if(installed)
		return 0;
	else
		return -EIO;
}

void cleanup_module(void)
{
	int i;
	for(i = 0; i < 4; i++)
	{
		if(dev_sbni[i].priv)
		{
			free_irq(dev_sbni[i].irq, &dev_sbni[i]);
			release_region(dev_sbni[i].base_addr, SBNI_IO_EXTENT);
			unregister_netdev(&dev_sbni[i]);
			kfree(dev_sbni[i].priv);
			dev_sbni[i].priv = NULL;
		}
	}
}
#endif /* MODULE */
