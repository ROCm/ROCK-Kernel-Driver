/*
 *  drivers/s390/net/iucv.c
 *    Network driver for VM using iucv
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Stefan Hegewald <hegewald@de.ibm.com>
 *               Hartmut Penner <hpenner@de.ibm.com>
 * 
 *    2.3 Updates Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *                Martin Schwidefsky (schwidefsky@de.ibm.com)
 *                

 */

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/kernel.h>              /* printk()                         */
#include <linux/malloc.h>              /* kmalloc()                        */
#include <linux/errno.h>               /* error codes                      */
#include <linux/types.h>               /* size_t                           */
#include <linux/interrupt.h>           /* mark_bh                          */
#include <linux/netdevice.h>           /* struct net_device, and other headers */
#include <linux/inetdevice.h>          /* struct net_device, and other headers */
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/ip.h>                  /* struct iphdr                     */
#include <linux/tcp.h>                 /* struct tcphdr                    */
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/string.h>

#include "iucv.h"




#define DEBUG123
#define MAX_DEVICES  10

extern char _ascebc[];

/* 
 *  global structures 
 */
static char iucv_userid[MAX_DEVICES][8];
static char iucv_ascii_userid[MAX_DEVICES][8];
static int  iucv_pathid[MAX_DEVICES] = {0};
static unsigned char iucv_ext_int_buffer[40] __attribute__((aligned (8))) ={0};
static unsigned char glob_command_buffer[40] __attribute__((aligned (8)));

#if LINUX_VERSION_CODE>=0x20300
typedef struct net_device  net_device;
#else
typedef struct device  net_device;
#endif
net_device iucv_devs[];


/* This structure is private to each device. It is used to pass */
/* packets in and out, so there is place for a packet           */
struct iucv_priv {
    struct net_device_stats stats;
    int packetlen;
    int status;
    u8 *packetdata;
    int pathid;                    /* used device */
    unsigned char command_buffer[40] __attribute__((aligned (8)));
    unsigned char ext_int_buffer[40] __attribute__((aligned (8)));
    u8* receive_buffer;
    int receive_buffer_len;
    u8* send_buffer;
    int send_buffer_len;    
    char * new_send_buf;       /* send buffer ptr */
    unsigned char recv_buf[2048];  /* size is just a guess */
    unsigned char userid[8];
};

struct iucv_header {
  short len;
};



static __inline__ int netif_is_busy(net_device *dev)
{
#if LINUX_VERSION_CODE<0x02032D
	return(dev->tbusy);
#else
	return(test_bit(__LINK_STATE_XOFF,&dev->flags));
#endif
}



#if LINUX_VERSION_CODE<0x02032D
#define netif_enter_interrupt(dev) dev->interrupt=1
#define netif_exit_interrupt(dev) dev->interrupt=0
#define netif_start(dev) dev->start=1
#define netif_stop(dev) dev->start=0

static __inline__ void netif_stop_queue(net_device *dev)
{
	dev->tbusy=1;
}

static __inline__ void netif_start_queue(net_device *dev)
{
	dev->tbusy=0;
}

static __inline__ void netif_wake_queue(net_device *dev)
{
	dev->tbusy=0;
	mark_bh(NET_BH);
}

#else
#define netif_enter_interrupt(dev)
#define netif_exit_interrupt(dev)
#define netif_start(dev)
#define netif_stop(dev)
#endif



/*
 * Following the iucv primitives 
 */


extern inline void b2f0(int code,void* parm)
{
  asm volatile ("LR    1,%1\n\tLR    0,%0\n\t.long 0xb2f01000" ::
                "d" (code) ,"a" (parm) :"0", "1");
}

int iucv_enable(void *parms)
{
  MASK_T *parm = parms;
  memset(parms,0,sizeof(parm));
  parm->ipmask = 0xF8;
  b2f0(SETMASK,parm);
  memset(parms,0,sizeof(parm));
  parm->ipmask = 0xF8;
  b2f0(SETCMASK,parm);
  return parm->iprcode;
}


int iucv_declare_buffer(void *parms, DCLBFR_T *buffer)
{
  DCLBFR_T *parm = parms;
  memset(parms,0,sizeof(parm));
  parm->ipflags1= 0x00;
  parm->ipbfadr1 = virt_to_phys(buffer);
  b2f0(DECLARE_BUFFER, parm);
  return parm->iprcode;
}


int iucv_retrieve_buffer(void *parms)
{
  DCLBFR_T *parm = parms;
  memset(parms,0x0,sizeof(parm));
  parm->iprcode = 0x0;
  b2f0(RETRIEVE_BUFFER, parm);
  return parm->iprcode;
}


int iucv_connect(void *parms,
                 const char *userid,
                 const char *host,
                 const char *ipusr,
                 unsigned short * used_pathid)
{
  CONNECT_T *parm = parms;                              /* ipflags was 0x60*/
  memset(parms,0x0,sizeof(parm));
  parm->ipflags1 = 0x80;
  parm->ipmsglim = 0x0a;
  memcpy(parm->ipvmid,userid,8);
  if (ipusr)
    memcpy(parm->ipuser,ipusr,16);
  memcpy(parm->iptarget,host,8);
  b2f0(CONNECT, parm);
  *used_pathid = parm->ippathid;
  return parm->iprcode;
}



int iucv_accept(void *parms,int pathid)
{
#ifdef DEBUG
  int i=0;
#endif
  ACCEPT_T *parm = parms;
  memset(parms,0,sizeof(parm));
  parm->ippathid = pathid;
  parm->ipflags1 = 0x80;
  parm->ipmsglim = 0x0a;
#ifdef DEBUG
  printk("iucv: iucv_accept input.\n");
  for (i=0;i<40; i++)
  {
    printk("%02x ",((char *)parms)[i]);
  }
  printk("\n");
#endif
  b2f0(ACCEPT, parm);
  return parm->iprcode;
}



int iucv_receive(void *parms,void *bufferarray,int len)
{
#ifdef DEBUG
  int i=0;
#endif
  RECEIVE_T *parm = parms;
  memset(parms,0x0,sizeof(parm));
  /*parm->ipflags1 = 0x42;*/
  parm->ipflags1 = 0x0;
  parm->ipmsgid  = 0x0;
  parm->iptrgcls = 0x0;
  parm->ipbfadr1 = (ULONG) virt_to_phys(bufferarray);
  parm->ipbfln1f  = len;
  parm->ipbfln2f  = 0x0;
  b2f0(RECEIVE, parm);
  if (parm->iprcode == 0)
    len = parm->ipbfln1f;
//  len = len-parm->ipbfln1f;
#ifdef DEBUG
  printk("iucv: iucv_receive command input:\n");
   for (i=0;i<40;i++)  /* show iucv buffer before send */
    {
      printk("%02x ",((char *)parms)[i]);
    }
  printk("\n");

  printk("iucv: iucv_receive data buffer:\n");
  for (i=0;i<len;i++)  /* show data received */
   {
     printk("%02x ",((char *)bufferarray)[i]);
   }
  printk("\n");
  printk("received length: %02x ",len);

#endif
  return parm->iprcode;
}


int iucv_send(void *parms,int pathid,void *bufferarray,int len,
              void *recv_buf, int recv_len)
{
#ifdef DEBUG
  int i=0;
#endif
  SEND_T *parm = parms;
  memset(parms,0x0,sizeof(parm));
  /*  parm->ipflags1 = 0x48; ??*/
  parm->ippathid = pathid;
  parm->ipflags1 = 0x14;                       /* any options ?? */
  parm->ipmsgid  = 0x0;
  parm->iptrgcls = 0x0;
  parm->ipbfadr1 = virt_to_phys(bufferarray);
  parm->ipbfln1f = len;
  parm->ipsrccls = 0x0;
  parm->ipmsgtag = 0x0;
  parm->ipbfadr2 = virt_to_phys(recv_buf);
  parm->ipbfln2f = recv_len;


#ifdef DEBUG
  printk("iucv: iucv_send command input:\n");
  for (i=0;i<40;i++)  /* show iucv buffer before send */
   {
     printk("%02x ",((char *)parms)[i]);
   }
  printk("\n");

  printk("iucv: iucv_send data buffer:\n");
  for (i=0;i<len;i++)  /* show send data before send */
   {
     printk("%02x ",((char *)bufferarray)[i]);
   }
  printk("\n");
#endif

  b2f0(SEND, parm);

#ifdef DEBUGXX
 printk("iucv: iucv_send buffer after send:\n");
 for (i=0;i<len;i++) /* show send buffer after send */
   {
     printk("%1x",((char *)bufferarray)[i]);
   }
   printk("\n");
#endif

  return parm->iprcode;
}



int iucv_sever(void *parms)
{
  SEVER_T *parm = parms;
  memset(parms,0x0,sizeof(parm));
  parm->ippathid = 0x0;
  parm->ipflags1 = 0x0;
  parm->iprcode  = 0xF;
  memset(parm->ipuser,0,16);
  b2f0(SEVER, parm);
  return parm->iprcode;
}


#ifdef DEBUG
/*--------------------------*/
/* Dump buffer formatted    */
/*--------------------------*/
static void dumpit(char* buf, int len)
{
  int i;
  for (i=0;i<len;i++) {
    if (!(i%16)&&i!=0)
      printk("\n");
    else if (!(i%4)&&i!=0)
      printk(" ");
    printk(  "%02X",buf[i]);
  }
  if (len%16)
    printk(  "\n");
}
#endif



/*--------------------------*/
/* Get device from pathid   */
/*--------------------------*/
net_device * get_device_from_pathid(int pathid)
{
   int i;
    for (i=0;i<=MAX_DEVICES;i++)
    {
      if (iucv_pathid[i] == pathid)
        return &iucv_devs[i];
    }
    printk("iucv: get_device_from_pathid: no device for pathid %X\n",pathid);
 return 0;
}



/*--------------------------*/
/* Get device from userid   */
/*--------------------------*/
net_device * get_device_from_userid(char * userid)
{
   int i;
   net_device * dev;
   struct iucv_priv *privptr;
      for (i=0;i<=MAX_DEVICES;i++)
      {
        dev = &iucv_devs[i];
        privptr = (struct iucv_priv *)(dev->priv);
        if (memcmp(privptr->userid,userid,8)==0)
          return &iucv_devs[i];
      }
      printk("iucv: get_device_from_uid: no device for userid %s\n",userid);
   return 0;
}


/*--------------------------*/
/* Open iucv Device Driver */
/*--------------------------*/
int iucv_open(net_device *dev)
{
    int rc;
    unsigned short iucv_used_pathid;
    struct iucv_priv *privptr;
    char iucv_host[8]   ={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; 
    char vmident[16]    ={0xf0,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
                          0xf0,0x40,0x40,0x40,0x40,0x40,0x40,0x40};

#ifdef DEBUG
    printk(  "iucv: iucv_open, device: %s\n",dev->name);
#endif

    privptr = (struct iucv_priv *)(dev->priv);
    if(privptr->pathid != -1) {
       netif_start(dev);
       netif_start_queue(dev);
       return 0;
    }
    if ((rc = iucv_connect(privptr->command_buffer,
                           privptr->userid,
                           iucv_host,
                           vmident,
                           &iucv_used_pathid))!=0) {
      printk(  "iucv: iucv connect failed with rc %X\n",rc);
      iucv_retrieve_buffer(privptr->command_buffer);
      return -ENODEV;
    }

    privptr->pathid = iucv_used_pathid;
    iucv_pathid[dev-iucv_devs]=privptr->pathid;

#ifdef DEBUG
    printk(  "iucv: iucv_connect ended with rc: %X\n",rc);
    printk(  "iucv[%d] pathid %X \n",(int)(dev-iucv_devs),privptr->pathid);
#endif
    netif_start(dev);
    netif_start_queue(dev);
    return 0;
}



/*-----------------------------------------------------------------------*/
/* Receive a packet: retrieve, encapsulate and pass over to upper levels */
/*-----------------------------------------------------------------------*/
void iucv_rx(net_device *dev, int len, unsigned char *buf)
{

  struct sk_buff *skb;
  struct iucv_priv *privptr = (struct iucv_priv *)dev->priv;

#ifdef DEBUG
  printk(  "iucv: iucv_rx len: %X, device %s\n",len,dev->name);
  printk(  "iucv rx: received orig:\n");
  dumpit(buf,len);
#endif

  /* strip iucv header now */
  len = len - 2;   /* short header */
  buf = buf + 2;   /* short header */

  skb = dev_alloc_skb(len+2); /* why +2 ? alignment ? */
  if (!skb) {
    printk(  "iucv rx: low on mem, returning...\n");
    return;
  }
  skb_reserve(skb, 2);                        /* align IP on 16B boundary*/
  memcpy(skb_put(skb, len), buf, len);
#ifdef DEBUG
  printk(  "iucv rx: data before netif_rx()\n");
  dumpit(buf,len);
#endif

  /* Write metadata, and then pass to the receive level */
  skb->mac.raw = skb->data;
  skb->pkt_type = PACKET_HOST;
  skb->dev = dev;
  skb->protocol = htons(ETH_P_IP);
  skb->ip_summed = CHECKSUM_UNNECESSARY;                /* don't check it*/
  privptr->stats.rx_packets++;
  netif_rx(skb);

  return;
} /* end  iucv_rx() */




/*----------------------------*/
/* handle interrupts          */
/*----------------------------*/
void do_iucv_interrupt(void)
{
  int rc;
  struct in_device *indev;
  struct in_ifaddr *inaddr;
  unsigned long len=0;
  net_device *dev=0;
  struct iucv_priv *privptr;
  INTERRUPT_T * extern_int_buffer;
  unsigned short iucv_data_len=0;
  unsigned short iucv_next=0;
  unsigned char * rcvptr;
  
  /* get own buffer: */
  extern_int_buffer = (INTERRUPT_T*) iucv_ext_int_buffer;
  
  netif_enter_interrupt(dev);        /* lock ! */
  
#ifdef DEBUG
  printk(  "iucv: do_iucv_interrupt %x received; pathid: %02X\n",
	   extern_int_buffer->iptype,extern_int_buffer->ippathid);
  printk(   "iucv: extern_int_buffer:\n");
  dumpit((char *)&extern_int_buffer[0],40);
#endif
  
  switch (extern_int_buffer->iptype)
    {
    case 0x01: /* connection pending ext interrrupt */
#ifdef DEBUG
      printk(  "iucv: connection pending IRQ.\n");
#endif
      
      rc = iucv_accept(glob_command_buffer,
		       extern_int_buffer->ippathid);
      if (rc != 0) {
	printk(  "iucv: iucv_accept failed with rc: %X\n",rc);
	iucv_retrieve_buffer(glob_command_buffer);
	break;
      }
#ifdef DEBUG
      dumpit(&((char *)extern_int_buffer)[8],8);
#endif
      dev = get_device_from_userid(&((char*)extern_int_buffer)[8]);
      privptr = (struct iucv_priv *)(dev->priv);
      privptr->pathid =  extern_int_buffer->ippathid;
      
#ifdef DEBUG
      printk(  "iucv: iucv_accept ended with rc: %X\n",rc);
      printk(  "iucv: device %s found.\n",dev->name);
#endif
      break;
      
    case 0x02: /* connection completed ext interrrupt */
      /* set own global IP address */
      /* & set global routing addr */
#ifdef DEBUG
      printk(  "connection completed.\n");
#endif
      
      if( extern_int_buffer->ipmsgtag !=0)
	{
	  /* get ptr's to kernel struct with local & broadcast address */
	  dev = get_device_from_pathid(extern_int_buffer->ippathid);
	  privptr = (struct iucv_priv *)(dev->priv);
	  indev = dev->ip_ptr;
	  inaddr = (struct in_ifaddr*) indev->ifa_list;
	}
      break;
      
      
    case 0x03: /* connection severed ext interrrupt */
      /* we do not handle this one at this time */
#ifdef DEBUG
      printk(  "connection severed.\n");
#endif
      break;
      
      
    case 0x04: /* connection quiesced ext interrrupt */
      /* we do not handle this one at this time */
#ifdef DEBUG
      printk(  "connection quiesced.\n");
#endif
      break;
      
      
    case 0x05: /* connection resumed ext interrrupt */
      /* we do not handle this one at this time */
#ifdef DEBUG
      printk(  "connection resumed.\n");
#endif
      break;
      
      
    case 0x06: /* priority message complete ext interrupt */
    case 0x07: /* non priority message complete ext interrupt */
      /* send it to iucv_rx for handling */
#ifdef DEBUG
      printk(  "message completed.\n");
#endif
      
      if (extern_int_buffer->ipaudit ==0)  /* ok case */
	{
#ifdef DEBUG
	  printk(  "iucv: msg complete interrupt successful, rc: %X\n",
		   (unsigned int)extern_int_buffer->ipaudit);
#endif
	  ;
	}
      else
	{
	  printk(  "iucv: msg complete interrupt error, rc: %X\n",
		   (unsigned int)extern_int_buffer->ipaudit);
	}
      /* a transmission is over: tell we are no more busy */
      dev = get_device_from_pathid(extern_int_buffer->ippathid);
      privptr = (struct iucv_priv *)(dev->priv);
      privptr->stats.tx_packets++;
      netif_wake_queue(dev);                /* transmission is no longer busy*/
      break;
      
      
    case 0x08: /* priority message pending */
    case 0x09: /* non priority message pending */
#ifdef DEBUG
      printk(  "message pending.\n");
#endif
      dev = get_device_from_pathid(extern_int_buffer->ippathid);
      privptr = (struct iucv_priv *)(dev->priv);
      rcvptr = &privptr->receive_buffer[0];
      
      /* re-set receive buffer */
      memset(privptr->receive_buffer,0,privptr->receive_buffer_len);
      len = privptr->receive_buffer_len;
      
        /* get data now */
        if (extern_int_buffer->ipflags1 & 0x80)
	  {  /* data is in the message */
#ifdef DEBUG
	    printk(  "iucv: iucv_receive data is in header!\n");
#endif
	    memcpy(privptr->receive_buffer,
		   (char *)extern_int_buffer->iprmmsg1,
		   (unsigned long)(extern_int_buffer->iprmmsg2));
	  }
        else /* data is in buffer, do a receive */
	  {
	    rc = iucv_receive(privptr->command_buffer,rcvptr,len);
	    if (rc != 0  || len == 0)
	      {
		printk(  "iucv: iucv_receive failed with rc: %X, length: %lX\n",rc,len);
		iucv_retrieve_buffer(privptr->command_buffer);
		break;
	      }
	  } /* end else */
	
      iucv_next = 0; 
      /* get next packet offset */  
      iucv_data_len= *((unsigned short*)rcvptr); 
        do{ /* until receive buffer is empty, i.e. iucv_next == 0 ! */

        /* get data length:    */
        iucv_data_len= iucv_data_len - iucv_next;
	
#ifdef DEBUG
        printk(  "iucv: iucv_receive: len is %02X, last: %02X\n",
		 iucv_data_len,iucv_next);
#endif
        /* transmit upstairs */
        iucv_rx(dev,(iucv_data_len),rcvptr);
	
#ifdef DEBUG
        printk(  "iucv: transaction complete now.\n");
#endif
        iucv_next = *((unsigned short*)rcvptr);
        rcvptr = rcvptr + iucv_data_len;
        /* get next packet offset */  
        iucv_data_len= *((unsigned short*)rcvptr);
	
      } while (iucv_data_len != 0);
      netif_start_queue(dev);                 /* transmission is no longer busy*/
      break;
      
    default:
      printk(  "unknown iucv interrupt \n");
      break;
      
    } /* end switch */
  netif_exit_interrupt(dev);              /* release lock*/
  
#ifdef DEBUG
  printk(  "iucv: leaving do_iucv_interrupt.\n");
#endif
  
}  /* end    do_iucv_interrupt()  */



/*-------------------------------------------*/
/*   Transmit a packet (low level interface) */
/*-------------------------------------------*/
int iucv_hw_tx(char *send_buf, int len,net_device *dev)
{
  /* This function deals with hw details.                         */
  /* This interface strips off the ethernet header details.       */
  /* In other words, this function implements the iucv behaviour,*/
  /* while all other procedures are rather device-independent     */
  struct iucv_priv *privptr;
  int rc, recv_len=2000;
  
  privptr = (struct iucv_priv *)(dev->priv);
  
#ifdef DEBUG
  printk(  "iucv: iucv_hw_tx, device %s\n",dev->name);
  printk(  "iucv: hw_TX_data len: %X\n",len);
  dumpit(send_buf,len);
#endif
  
  /* I am paranoid. Ain't I? */
  if (len < sizeof(struct iphdr))
    {
      printk(  "iucv: Hmm... packet too short (%i octets)\n",len);
      return -EINVAL;
    }
  
  /*
   * build IUCV header (preceeding halfword offset)   
   * works as follows: Each packet is preceded by the 
   * halfword offset to the next one. 
   * The last packet is followed by an offset of zero.
   * E.g., AL2(12),10-byte packet, AL2(34), 32-byte packet, AL2(0)
   */
  
  memcpy(&privptr->send_buffer[2],send_buf,len+2);
  privptr->send_buffer[len+2] = 0;
  privptr->send_buffer[len+3] = 0;
  *((unsigned short*) &privptr->send_buffer[0]) = len + 2;
  
#ifdef DEBUG
  printk(  "iucv: iucv_hw_tx, device %s\n",dev->name);
  printk(  "iucv: send len: %X\n",len+4);
  dumpit(privptr->send_buffer,len+4);
#endif
  *((unsigned short*) &privptr->send_buffer[0]) = len + 2;
  
  /* Ok, now the packet is ready for transmission: send it. */
  if ((rc = iucv_send(privptr->command_buffer,
		      privptr->pathid,
		      &privptr->send_buffer[0],len+4,
		      privptr->recv_buf,recv_len))!=0) {
    printk(  "iucv: send_iucv failed, rc: %X\n",rc);
    iucv_retrieve_buffer(privptr->command_buffer);
  }
#ifdef DEBUG
  printk(  "iucv: send_iucv ended, rc: %X\n",rc);
#endif
  return rc;
} /* end   iucv_hw_tx()  */






/*------------------------------------------*/
/* Transmit a packet (called by the kernel) */
/*------------------------------------------*/
int iucv_tx(struct sk_buff *skb, net_device *dev)
{
    int retval=0;

    struct iucv_priv *privptr;

    if (dev == NULL)
    {
      printk("iucv: NULL dev passed\n");
      return 0;
    }

    privptr = (struct iucv_priv *) (dev->priv);

    if (skb == NULL)
    {
      printk("iucv: %s: NULL buffer passed\n", dev->name);
      privptr->stats.tx_errors++;
      return 0;
    }

#ifdef DEBUG
    printk(  "iucv: enter iucv_tx, using %s\n",dev->name);
#endif

    if (netif_is_busy(dev))                        /* shouldn't happen */
    {
      privptr->stats.tx_errors++;
      dev_kfree_skb(skb);
      printk("iucv: %s: transmit access conflict ! leaving iucv_tx.\n", dev->name);
    }

    netif_stop_queue(dev);                                   /* transmission is busy*/
    dev->trans_start = jiffies;                       /* save the timestamp*/

    /* actual deliver of data is device-specific, and not shown here */
    retval = iucv_hw_tx(skb->data, skb->len, dev);

    dev_kfree_skb(skb);                               /* release it*/

#ifdef DEBUG
    printk(  "iucv:leaving iucv_tx, device %s\n",dev->name);
#endif

    return retval;              /* zero == done; nonzero == fail*/
}   /* end  iucv_tx( struct sk_buff *skb, struct device *dev)  */






/*---------------*/
/* iucv_release */
/*---------------*/
int iucv_release(net_device *dev)
{
    int rc =0;
    struct iucv_priv *privptr;
    privptr = (struct iucv_priv *) (dev->priv);

    netif_stop(dev);
    netif_stop_queue(dev);           /* can't transmit any more*/
    rc = iucv_sever(privptr->command_buffer);
    if (rc!=0)
    {
       printk("iucv: %s: iucv_release pending...rc:%02x\n",dev->name,rc);
    }

#ifdef DEBUG
      printk("iucv: iucv_sever ended with rc: %X\n",rc);
#endif

    return rc;
} /* end  iucv_release() */





/*-----------------------------------------------*/
/* Configuration changes (passed on by ifconfig) */
/*-----------------------------------------------*/
int iucv_config(net_device *dev, struct ifmap *map)
{
   if (dev->flags & IFF_UP)        /* can't act on a running interface*/
        return -EBUSY;

   /* ignore other fields */
   return 0;
}
/*  end  iucv_config()  */





/*----------------*/
/* Ioctl commands */
/*----------------*/
int iucv_ioctl(net_device *dev, struct ifreq *rq, int cmd)
{
#ifdef DEBUG
    printk(  "iucv: device %s; iucv_ioctl\n",dev->name);
#endif
    return 0;
}

/*---------------------------------*/
/* Return statistics to the caller */
/*---------------------------------*/
struct net_device_stats *iucv_stats(net_device *dev)
{
    struct iucv_priv *priv = (struct iucv_priv *)dev->priv;
#ifdef DEBUG
    printk(  "iucv: device %s; iucv_stats\n",dev->name);
#endif
    return &priv->stats;
}


/*
 * iucv_change_mtu     
 * IUCV can handle MTU sizes from 576 to approx. 32000    
 */

static int iucv_change_mtu(net_device *dev, int new_mtu)
{
#ifdef DEBUG
    printk(  "iucv: device %s; iucv_change_mtu\n",dev->name);
#endif
       if ((new_mtu < 64) || (new_mtu > 32000))
	 return -EINVAL;
       dev->mtu = new_mtu;
       return 0;
}




/*--------------------------------------------*/
/* The init function (sometimes called probe).*/
/* It is invoked by register_netdev()         */
/*--------------------------------------------*/
int iucv_init(net_device *dev)
{
    int rc;
    struct iucv_priv *privptr;

#ifdef DEBUG
    printk(  "iucv: iucv_init, device: %s\n",dev->name);
#endif

    dev->open            = iucv_open;
    dev->stop            = iucv_release;
    dev->set_config      = iucv_config;
    dev->hard_start_xmit = iucv_tx;
    dev->do_ioctl        = iucv_ioctl;
    dev->get_stats       = iucv_stats;
    dev->change_mtu      = iucv_change_mtu;

    /* keep the default flags, just add NOARP */

    dev->hard_header_len = 0;
    dev->addr_len        = 0;
    dev->type            = ARPHRD_SLIP;
    dev->tx_queue_len    = 100;
    dev->flags           = IFF_NOARP|IFF_POINTOPOINT;
    dev->mtu    = 4092;

    dev_init_buffers(dev);

    /* Then, allocate the priv field. This encloses the statistics */
    /* and a few private fields.*/
    dev->priv = kmalloc(sizeof(struct iucv_priv), GFP_KERNEL);
    if (dev->priv == NULL){
       printk(  "iucv: no memory for dev->priv.\n");
       return -ENOMEM;
    }
    memset(dev->priv, 0, sizeof(struct iucv_priv));
    privptr = (struct iucv_priv *)(dev->priv);


    privptr->send_buffer = (u8*) __get_free_pages(GFP_KERNEL+GFP_DMA,8);
    if (privptr->send_buffer == NULL) {
      printk(KERN_INFO "%s: could not get pages for send buffer\n",
	     dev->name);
      return -ENOMEM;
    }
    memset(privptr->send_buffer, 0, 8*PAGE_SIZE);
    privptr->send_buffer_len=8*PAGE_SIZE;
    
    privptr->receive_buffer = (u8*) __get_free_pages(GFP_KERNEL+GFP_DMA,8);
    if (privptr->receive_buffer == NULL) {
      printk(KERN_INFO "%s: could not get pages for receive buffer\n",
	     dev->name);
      return -ENOMEM;
    }
    memset(privptr->receive_buffer, 0, 8*PAGE_SIZE);
    privptr->receive_buffer_len=8*PAGE_SIZE;

    /* now use the private fields ... */
    /* init pathid                    */
    privptr->pathid = -1;

    /* init private userid from global userid */
    memcpy(privptr->userid,iucv_userid[dev-iucv_devs],8);


    /* we can use only ONE buffer for external interrupt ! */
    rc=iucv_declare_buffer(privptr->command_buffer,
                           (DCLBFR_T *)iucv_ext_int_buffer);
    if (rc!=0 && rc!=19)   /* ignore existing buffer */
      {
         printk(  "iucv:iucv_declare failed, rc: %X\n",rc);
         return -ENODEV;
      }

    rc = iucv_enable(privptr->command_buffer);
    if (rc!=0)
    {
            printk(  "iucv:iucv_enable failed, rc: %x\n",rc);
            iucv_retrieve_buffer(privptr->command_buffer);
            return -ENODEV;
    }
#ifdef DEBUG
    printk(  "iucv: iucv_init endend OK for device %s.\n",dev->name);
#endif
    return 0;
}


/*
 * setup iucv devices
 * 
 * string passed: iucv=userid1,...,useridn 
 */
#if LINUX_VERSION_CODE>=0x020300
static int  __init iucv_setup(char *str)
#else
__initfunc(void iucv_setup(char *str,int *ints))
#endif
{
    int result=0, i=0,j=0, k=0, device_present=0;
    char *s = str;
    net_device * dev ={0};

#ifdef DEBUG
    printk(  "iucv: start registering device(s)... \n");
#endif

    /*
     * scan device userids
     */

    while(*s != 0x20 && *s != '\0'){
       if(*s == ','){
          /* fill userid up to 8 chars */
          for(k=i;k<8;k++){
             iucv_userid[j][k] = 0x40;
          } /* end for */
          /* new device  */
          j++;
          s++; /* ignore current char */
          i=0;
          if (j>MAX_DEVICES) {
             printk("iucv: setup devices: max devices %d reached.\n",
		    MAX_DEVICES);
             break;
          } /* end if */
          continue;
       } /* end if */
       iucv_ascii_userid[j][i] = (int)*s;
       iucv_userid[j][i] = _ascebc[(int)*s++];
       i++;
    } /* end while */

    /* 
     * fill last userid up to 8 chars
     */
    for(k=i;k<8;k++) {
      iucv_userid[j][k] = 0x40;
    }

    /*
     * set device name and register
     */

    for (k=0;k<=j;k++) {
      memcpy(iucv_devs[k].name, "iucv0", 4);
      dev = &iucv_devs[k];
      dev->name[4] = k + '0';

#ifdef DEBUGX
      printk("iucv: (ASCII- )Userid:%s\n",&iucv_ascii_userid[k][0]);
      printk("iucv: (ASCII-)Userid: ");
      for (i=0;i<8;i++) {
        printk(  "%02X ",(int)iucv_ascii_userid[k][i]);
      }
      printk("\n");
      printk("iucv: (EBCDIC-)Userid: ");
      for (i=0;i<8;i++) {
         printk(  "%02X ",(int)iucv_userid[k][i]);
      }
      printk("\n");
      printk("iucv: device name :%s\n",iucv_devs[k].name);
#endif

      if ( (result = register_netdev(iucv_devs + k)) )
          printk("iucv: error %i registering device \"%s\"\n",
                 result, iucv_devs[k].name);
      else
      {
              device_present++;
      }
    } /* end for */

#ifdef DEBUG
    printk(  "iucv: end register devices, %d devices present\n",device_present);
#endif
    /* return device_present ? 0 : -ENODEV; */
#if LINUX_VERSION_CODE>=0x020300
    return 1;
#else
    return;
#endif
}

#if LINUX_VERSION_CODE>=0x020300
__setup("iucv=", iucv_setup);
#endif


/*-------------*/
/* The devices */
/*-------------*/
char iucv_names[MAX_DEVICES*8]; /* MAX_DEVICES eight-byte buffers */
net_device iucv_devs[MAX_DEVICES] = {
    {
        iucv_names, /* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+8,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+16,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+24,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+32,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+40,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+48,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+56,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+64,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    },
    {
        iucv_names+72,/* name -- set at load time */
        0, 0, 0, 0,  /* shmem addresses */
        0x000,       /* ioport */
        0,           /* irq line */
        0, 0, 0,     /* various flags: init to 0 */
        NULL,        /* next ptr */
        iucv_init,  /* init function, fill other fields with NULL's */
    }
};

