/*
 *  drivers/s390/net/ctc.c 
 *    CTC / ESCON network driver
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Dieter Wellerdiek (wel@de.ibm.com)
 *
 *     2.3 Updates Martin Schwidefsky (schwidefsky@de.ibm.com)
 *                 Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *
 *  Description of the Kernel Parameter
 *    Normally the CTC driver selects the channels in order (automatic channel 
 *    selection). If your installation needs to use the channels in a different 
 *    order or doesn't want to have automatic channel selection on, you can do 
 *    this with the "ctc= kernel keyword". 
 *
 *       ctc=0,0xrrrr,0xwwww,ddddd
 *
 *     Where:
 *
 *       "rrrr" is the read channel address
 *       "wwww" is the write channel address
 *       "dddd" is the network device (ctc0 to ctc7 for a parallel channel, escon0
 *              to escon7 for ESCON channels).
 *
 *     To switch the automatic channel selection off use the ctc= keyword with 
 *     parameter "noauto". This may be necessary if you 3271 devices or other devices 
 *     which use the ctc device type and model, but operate with a different protocol. 
 *     
 *       ctc=noauto
 *
 *  Change History
 *    0.50  Initial release shipped
 *    0.51  Bug fixes
 *          - CTC / ESCON network device can now handle up to 64 channels 
 *          - 3088-61 info message supperssed - CISCO 7206 - CLAW - ESCON 
 *          - 3088-62 info message suppressed - OSA/D   
 *          - channel: def ffffffed ... error message suppressed 
 *          - CTC / ESCON device was not recoverable after a lost connection with 
 *            IFCONFIG dev DOWN and IFCONFIG dev UP 
 *          - Possibility to switch the automatic selection off
 *          - Minor bug fixes 
 */
#include <linux/version.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/sched.h>

#include <linux/signal.h>
#include <linux/string.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/bitops.h>
 
#include <asm/irq.h>


//#define DEBUG 

/* Redefine message level, so that all messages occur on 3215 console in DEBUG mode */
#ifdef DEBUG                
        #undef  KERN_INFO
        #undef  KERN_WARNING
        #undef  KERN_DEBUG
        #define KERN_INFO    KERN_EMERG
        #define KERN_WARNING KERN_EMERG
        #define KERN_DEBUG   KERN_EMERG
#endif  
//#undef DEBUG

#define CCW_CMD_WRITE           0x01
#define CCW_CMD_READ            0x02
#define CCW_CMD_SET_EXTENDED    0xc3
#define CCW_CMD_PREPARE         0xe3

#define MAX_CHANNEL_DEVICES     64 
#define MAX_ADAPTERS            8
#define CTC_DEFAULT_MTU_SIZE    1500
#define READ                    0
#define WRITE                   1
#define CTC                     0
#define ESCON                   1
#define CHANNEL_MEDIA           2
#define CTC_BLOCKS              8          /* 8 blocks * 2 times * 64k = 1M */

#define TB_TX                   0          /* sk buffer handling in process */
#define TB_STOP                 1          /* network device stop in process */
#define TB_RETRY                2          /* retry in process */
#define TB_NOBUFFER             3          /* no buffer on free queue */ 

/* state machine codes used in ctc_irq_handler */
#define CTC_STOP                0
#define CTC_START_HALT_IO       1
#define CTC_START_SET_X_MODE    2
#define CTC_START_SELECT        4 
#define CTC_START_READ_TEST     32
#define CTC_START_READ          33
#define CTC_START_WRITE_TEST    64
#define CTC_START_WRITE         65


typedef enum { 
        channel_type_none,           /* Device is not a channel */
        channel_type_undefined,      /* Device is a channel but we don't know anything about it */
        channel_type_ctca,           /* Device is a CTC/A and we can deal with it */
        channel_type_escon,          /* Device is a ESCON channel and we can deal with it */
        channel_type_unsupported     /* Device is a unsupported model */
} channel_type_t; 
  


/* 
 *   Structures needed in the initial phase 
 *
 */  

static int channel_tab_initialized = 0;     /* channel[] structure initialized */

struct devicelist {  
        unsigned int  devno;
        __u8          flag;
#define CHANNEL_IN_USE   0x08               /* - Show that channel is in use */
}; 

static struct {
        struct devicelist  list[MAX_CHANNEL_DEVICES]; 
        int                count;
        int                left;
} channel[CHANNEL_MEDIA];



static int ctc_no_auto = 0;

struct adapterlist{ 
        unsigned int       devno[2];
        __u16              protocol;
};

static struct adapterlist ctc_adapter[CHANNEL_MEDIA][MAX_ADAPTERS];  /* 0 = CTC  / 1 = ESCON */


/* 
 *   Structure used after the initial phase 
 *
 */
    
struct buffer {
        struct buffer       *next;
        int                 packets;
        struct block        *block;
};

#if LINUX_VERSION_CODE>=0x020300
typedef struct net_device  net_device;
#else
typedef struct device  net_device;
typedef struct wait_queue* wait_queue_head_t;
#define DECLARE_WAITQUEUE(waitqname,waitqtask) struct wait_queue  waitqname = {waitqtask, NULL }
#define init_waitqueue_head(nothing)
#endif


struct channel {
        unsigned int        devno;
        int                 irq;
        unsigned long       IO_active;
        ccw1_t              ccw[3];
        __u32               state; 
        int                 buffer_count;
        struct buffer       *free_anchor;
        struct buffer       *proc_anchor;
        devstat_t           *devstat;
        net_device   *dev;      /* backward pointer to the network device */ 
	wait_queue_head_t   wait;
        struct tq_struct    tq;
        struct timer_list   timer;
        unsigned long       flag_a;    /* atomic flags */
#define CTC_BH_ACTIVE       0   
        __u8                last_dstat;
        __u8                flag;
#define CTC_WRITE            0x01      /* - Set if this is a write channel */
#define CTC_TIMER            0x80      /* - Set if timer made the wake_up  */ 
};


struct ctc_priv {                                                                    
        struct net_device_stats  stats;
#if LINUX_VERSION_CODE>=0x02032D
	int                      tbusy;
#endif
        struct channel           channel[2]; 
        __u16                    protocol;
};  

/*
 *   This structure works as shuttle between two systems 
 *    - A block can contain one or more packets 
 */

#define PACKET_HEADER_LENGTH  6
struct packet {
        __u16         length;
        __u16         type;
        __u16         unused;
        __u8          data;
}; 

#define BLOCK_HEADER_LENGTH   2
struct block {
        __u16         length;
        struct packet data;
};

#if LINUX_VERSION_CODE>=0x02032D
#define ctc_protect_busy(dev) \
s390irq_spin_lock(((struct ctc_priv *)dev->priv)->channel[WRITE].irq)
#define ctc_unprotect_busy(dev) \
s390irq_spin_unlock(((struct ctc_priv *)dev->priv)->channel[WRITE].irq)

#define ctc_protect_busy_irqsave(dev,flags) \
s390irq_spin_lock_irqsave(((struct ctc_priv *)dev->priv)->channel[WRITE].irq,flags)
#define ctc_unprotect_busy_irqrestore(dev,flags) \
s390irq_spin_unlock_irqrestore(((struct ctc_priv *)dev->priv)->channel[WRITE].irq,flags)

static __inline__ void ctc_set_busy(net_device *dev)
{
	((struct ctc_priv *)dev->priv)->tbusy=1;
	netif_stop_queue(dev);
}

static __inline__ void ctc_clear_busy(net_device *dev)
{
	((struct ctc_priv *)dev->priv)->tbusy=0;
	netif_start_queue(dev);
}

static __inline__ int ctc_check_busy(net_device *dev)
{
	eieio();
	return(((struct ctc_priv *)dev->priv)->tbusy);
}


static __inline__ void ctc_setbit_busy(int nr,net_device *dev)
{
	set_bit(nr,&(((struct ctc_priv *)dev->priv)->tbusy));
	netif_stop_queue(dev);	
}

static __inline__ void ctc_clearbit_busy(int nr,net_device *dev)
{
	clear_bit(nr,&(((struct ctc_priv *)dev->priv)->tbusy));
	if(((struct ctc_priv *)dev->priv)->tbusy==0)
		netif_start_queue(dev);
}

static __inline__ int ctc_test_and_setbit_busy(int nr,net_device *dev)
{
	netif_stop_queue(dev);
	return(test_and_set_bit(nr,&((struct ctc_priv *)dev->priv)->tbusy));
}
#else

#define ctc_protect_busy(dev)
#define ctc_unprotect_busy(dev)
#define ctc_protect_busy_irqsave(dev,flags)
#define ctc_unprotect_busy_irqrestore(dev,flags)

static __inline__ void ctc_set_busy(net_device *dev)
{
	dev->tbusy=1;
	eieio();
}

static __inline__ void ctc_clear_busy(net_device *dev)
{
	dev->tbusy=0;
	eieio();
}

static __inline__ int ctc_check_busy(net_device *dev)
{
	eieio();
	return(dev->tbusy);
}


static __inline__ void ctc_setbit_busy(int nr,net_device *dev)
{
	set_bit(nr,(void *)&dev->tbusy);
}

static __inline__ void ctc_clearbit_busy(int nr,net_device *dev)
{
	clear_bit(nr,(void *)&dev->tbusy);
}

static __inline__ int ctc_test_and_setbit_busy(int nr,net_device *dev)
{
	return(test_and_set_bit(nr,(void *)&dev->tbusy));
}
#endif





/* Interrupt handler */
static void ctc_irq_handler(int irq, void *initparm, struct pt_regs *regs);
static void ctc_irq_bh(struct channel *ctc); 
static void ctc_read_retry (struct channel *ctc);
static void ctc_write_retry (struct channel *ctc);


/* Functions for the DEV methods */
int ctc_probe(net_device *dev);
 

static int ctc_open(net_device *dev); 
static void ctc_timer (struct channel *ctc);
static int ctc_release(net_device *dev);
static int ctc_tx(struct sk_buff *skb, net_device *dev);
static int ctc_change_mtu(net_device *dev, int new_mtu);
struct net_device_stats* ctc_stats(net_device *dev); 


/*
 *   Channel Routines 
 *
 */ 

static void channel_init(void);
static void channel_scan(void);
static int channel_get(int media, int devno);
static int channel_get_next(int media); 
static int channel_free(int media, int devno);
static channel_type_t channel_check_for_type (senseid_t *id);
static void channel_sort(struct devicelist list[], int n);


/*
 * initialize the channel[].list 
 */   
static void channel_init(void) 
{
        int     m;
#ifdef DEBUG
        int     c;
#endif

        if (!test_and_set_bit(0, (void *)& channel_tab_initialized)){
                channel_scan(); 
                for (m = 0; m < CHANNEL_MEDIA; m++) { 
                        channel_sort (channel[m].list, MAX_CHANNEL_DEVICES); 
                        channel[m].left = channel[m].count;   
                }
                if (channel[CTC].count == 0 && channel[ESCON].count == 0) 
                        printk(KERN_INFO "channel: no Channel devices recognized\n");
                else
                        printk(KERN_INFO "channel: %d Parallel channel found - %d ESCON channel found\n",
                            channel[CTC].count, channel[ESCON].count);  
#ifdef DEBUG 
                for (m = 0; m < CHANNEL_MEDIA;  m++) { 
                        for (c = 0; c < MAX_CHANNEL_DEVICES; c++){
                                printk(KERN_DEBUG "channel: Adapter=%x Entry=%x devno=%04x\n", 
                                     m, c, channel[m].list[c].devno);
                        }
                }
#endif
         }
}


/*
* scan for all channels and put the device numbers into the channel[].list 
*/  
static void channel_scan(void)
{
        int        m;
        int        c;
        int        irq;
        dev_info_t temp;
        
        for (m = 0; m < CHANNEL_MEDIA;  m++) { 
                for (c = 0; c < MAX_CHANNEL_DEVICES; c++){
                        channel[m].list[c].devno = -ENODEV;
                }
        }
        
        for (irq = 0; irq < NR_IRQS; irq++) {
                /* CTC/A */
                if (channel[CTC].count < MAX_CHANNEL_DEVICES ) {
                        if (get_dev_info(irq, &temp) == 0 && 
                            channel_check_for_type(&temp.sid_data) == channel_type_ctca) {
                                channel[CTC].list[channel[CTC].count].devno = temp.devno; 
                                channel[CTC].count++; 
                        }
                }

                /* ESCON */
                if (channel[ESCON].count < MAX_CHANNEL_DEVICES ) {
                        if (get_dev_info(irq, &temp) == 0 && 
                            channel_check_for_type(&temp.sid_data) == channel_type_escon) {
                                channel[ESCON].list[channel[ESCON].count].devno = temp.devno; 
                                channel[ESCON].count++; 

                        }
                }
        }
} 
 

/*
 * free specific channel from the channel[].list 
 */  
static int channel_free(int media, int devno)
{
        int     i;

        for (i = 0; i < channel[media].count; i++) {       
                if ((devno == channel[media].list[i].devno) &&
                    ((channel[media].list[i].flag & CHANNEL_IN_USE) != 0x00)) {
                        channel[media].list[i].flag &= ~CHANNEL_IN_USE;
                        return 0;
                }
        }
        printk(KERN_WARNING "channel: dev %04x is not a channel or in use\n", devno);
        return -ENODEV; 
}


/*
 * get specific channel from the channel[].list 
 */  
static int channel_get(int media, int devno)
{
        int     i;

        for (i = 0; i < channel[media].count; i++) {       
                if ((devno == channel[media].list[i].devno) &&
                    ((channel[media].list[i].flag & CHANNEL_IN_USE) == 0x00)) {
                        channel[media].list[i].flag |= CHANNEL_IN_USE;
                        return channel[media].list[i].devno;             
                }
        }
        printk(KERN_WARNING "channel: dev %04x is not a channel or in use\n", devno);
        return -ENODEV; 

}


/*
 * get the next free channel from the channel[].list 
 */  
static int channel_get_next(int media)
{
        int     i;

        for (i = 0; i < channel[media].count; i++) {
                if ((channel[media].list[i].flag & CHANNEL_IN_USE) == 0x00) {
#ifdef DEBUG
                        printk(KERN_DEBUG "channel: picked=%04x\n", channel[media].list[i].devno);
#endif
                        channel[media].list[i].flag |= CHANNEL_IN_USE;
                        return channel[media].list[i].devno;        
                }
        }
        return -ENODEV; 
}
 

/*
 * picks the next free channel from the channel[].list 
 */  
static int channel_left(int media)
{
        return channel[media].left; 
}


/*
 * defines all devices which are channels
 */
static channel_type_t channel_check_for_type (senseid_t *id)
 {
        channel_type_t type;

        switch (id->cu_type) {
                case 0x3088: 

                        switch (id->cu_model) {
                                case 0x08:    
                                        type = channel_type_ctca;  /* 3088-08  ==> CTCA */
                                        break; 

                                case 0x1F:   
                                        type = channel_type_escon; /* 3088-1F  ==> ESCON channel */
                                        break;
 
                                case 0x01:                         /* 3088-01  ==> P390 OSA emulation */
                                case 0x60:                         /* 3088-60  ==> OSA/2 adapter */
                                case 0x61:                         /* 3088-61  ==> CISCO 7206 CLAW protocol ESCON connected */
                                case 0x62:                         /* 3088-62  ==> OSA/D device */ 
                                        type = channel_type_unsupported;
                                         break; 

                                default:
                                        type = channel_type_undefined;
                                        printk(KERN_INFO "channel: Unknown model found 3088-%02x\n",id->cu_model);
                        }
                        break;

                default:
                        type = channel_type_none;

        }
        return type;
}


/*
 *  sort the channel[].list
 */
static void channel_sort(struct devicelist list[], int n)
{
        int               i;
        int               sorted = 0;
        struct devicelist tmp;

        while (!sorted) { 
                sorted = 1;

                for (i = 0; i < n-1; i++) {  
                        if (list[i].devno > list[i+1].devno) {  
                                tmp = list[i];
                                list[i] = list[i+1];
                                list[i+1] = tmp;
                                sorted = 0;
                        }
                }
        }
} 


/*
 *   General routines 
 *
 */

static int inline extract_channel_id(char *name)
{
        if (name[0] == 'c') 
                return (name[3]-'0');
        else
                return (name[5]-'0');
}


static int inline extract_channel_media(char *name)
{
        if (name[0] == 'c') 
                return CTC;
        else
                return ESCON;
}


static void ctc_tab_init(void)
{                                    
        int          m;
        int          i;
        static int   t;

        if (t == 0){
                for (m = 0; m < CHANNEL_MEDIA;  m++) {
                        for (i = 0; i < MAX_ADAPTERS;  i++) {
                                ctc_adapter[m][i].devno[WRITE] = -ENODEV;
                                ctc_adapter[m][i].devno[READ] = -ENODEV;
                        }
                }
                t = 1; 
        }
} 


static int ctc_buffer_alloc(struct channel *ctc) {
        
        struct buffer    *p;
        struct buffer    *q;

        p = kmalloc(sizeof(p), GFP_KERNEL);
        if (p == NULL) 
                return -ENOMEM;
        else {  
                p->next = NULL;  
                p->packets = 0;
                p->block = (struct block *) __get_free_pages(GFP_KERNEL+GFP_DMA, 4);
                if (p->block == NULL) {
                        kfree(p);
                        return -ENOMEM;
                }
        }
   
        if (ctc->free_anchor == NULL) 
                ctc->free_anchor = p;  
        else {  
                 q = ctc->free_anchor;
                 while (q->next != NULL) 
                        q = q->next;
                 q->next = p;
        }
        ctc->buffer_count++;   
   return 0;
}


static int ctc_buffer_free(struct channel *ctc) {
        
        struct buffer    *p;

           
        if (ctc->free_anchor == NULL)
                return -ENOMEM;
        
        p = ctc->free_anchor; 
        ctc->free_anchor = p->next;
        free_pages((__u32)p->block, 4);
        kfree(p);

        return 0;
}


static int inline ctc_buffer_swap(struct buffer **from, struct buffer **to) {
        
        struct buffer    *p = NULL;
        struct buffer    *q = NULL;
 
        if (*from == NULL)
                return -ENOMEM; 

        p = *from;
        *from = p->next;
        p->next = NULL;

        if (*to == NULL)
                *to = p;
        else {
                q = *to;
                while (q->next != NULL)
                        q = q->next;
                q->next = p;

        }
        return 0;
}


/*
 *   ctc_setup function 
 *     this function is called for each ctc= keyword passed into the kernel 
 *
 *     valid parameter are: ctc=n,0xnnnn,0xnnnn,ctcx 
 *     where n      is the channel protocol always 0 
 *           0xnnnn is the cu number  read 
 *           0xnnnn is the cu number  write 
 *           ctcx can be ctc0 to ctc7 or escon0 to escon7 
 */
#if LINUX_VERSION_CODE>=0x020300
static int __init ctc_setup(char *dev_name)
#else
__initfunc(void ctc_setup(char *dev_name,int *ints))
#endif
{
        struct adapterlist tmp;
#if  LINUX_VERSION_CODE>=0x020300
	#define CTC_MAX_PARMS 4
	int ints[CTC_MAX_PARMS+1];	
	get_options(dev_name,CTC_MAX_PARMS,ints);
	#define ctc_setup_return return(1)
#else
	#define ctc_setup_return return
#endif
        ctc_tab_init();
        
        ctc_no_auto = 1;

        if (!strcmp(dev_name,"noauto")) { 
                printk(KERN_INFO "ctc: automatic channel selection deactivated\n"); 
                ctc_setup_return;
        }

        tmp.devno[WRITE] = -ENODEV;
        tmp.devno[READ] = -ENODEV; 

        switch (ints[0]) {
              
                case 3: /* write channel passed */
                        tmp.devno[WRITE] = ints[3]; 
                        
                case 2: /* read channel passed */
                        tmp.devno[READ] = ints[2];
                        if (tmp.devno[WRITE] == -ENODEV)
                                tmp.devno[WRITE] = tmp.devno[READ] + 1; 

                case 1: /* protocol type passed */
                        tmp.protocol    = ints[1];
                        if (tmp.protocol == 0) {
                                break;    
                        } else {
                                printk(KERN_WARNING "%s: wrong Channel protocol type passed\n", dev_name);
                                ctc_setup_return;
                        }
			break;

                default: 
                        printk(KERN_WARNING "ctc: wrong number of parameter passed\n");
                        ctc_setup_return;
        }
        ctc_adapter[extract_channel_media(dev_name)][extract_channel_id(dev_name)] = tmp; 
#ifdef DEBUG
        printk(DEBUG "%s: protocol=%x read=%04x write=%04x\n",
             dev_name, tmp.protocol, tmp.devno[READ], tmp.devno[WRITE]);
#endif  
        ctc_setup_return;
        
}
#if LINUX_VERSION_CODE>=0x020300
__setup("ctc=", ctc_setup);
#endif

/*
 *   ctc_probe 
 *      this function is called for each channel network device, 
 *      which is defined in the /init/main.c 
 */
int ctc_probe(net_device *dev)
{       
        int                rc;
        int                c;
        int                i;
        int                m;

        struct ctc_priv    *privptr;

        /* Only the first time the ctc_probe gets control */
        if (channel_tab_initialized == 0) {  
                channel_init();  


        }
        
        ctc_tab_init();

        m = extract_channel_media(dev->name);
        i = extract_channel_id(dev->name);
        
        if (channel_left(m) <=1) 
                return -ENODEV;

        if (ctc_no_auto == 1 && (ctc_adapter[m][i].devno[READ] == -ENODEV || ctc_adapter[m][i].devno[WRITE] == -ENODEV))
                return -ENODEV;

        dev->priv = kmalloc(sizeof(struct ctc_priv), GFP_KERNEL);
        if (dev->priv == NULL)
                return -ENOMEM;
        memset(dev->priv, 0, sizeof(struct ctc_priv));  
        privptr = (struct ctc_priv *) (dev->priv);

        
        for (c = 0; c < 2; c++) {

                privptr->channel[c].devstat = kmalloc(sizeof(devstat_t), GFP_KERNEL);
                if (privptr->channel[c].devstat == NULL){
                        if (i == WRITE)
                                kfree(privptr->channel[READ].devstat);
                        return -ENOMEM;  
                }
                memset(privptr->channel[c].devstat, 0, sizeof(devstat_t));

                if (ctc_no_auto == 0) 
                         ctc_adapter[m][i].devno[c] = channel_get_next(m);
                else 
                         ctc_adapter[m][i].devno[c] = channel_get(m, ctc_adapter[m][i].devno[c]);

                if ( ctc_adapter[m][i].devno[c] != -ENODEV){
                         rc = request_irq(get_irq_by_devno(ctc_adapter[m][i].devno[c]),
                              (void *)ctc_irq_handler, SA_INTERRUPT, dev->name, 
                              privptr->channel[c].devstat);
                         if (rc) { 
                                 printk(KERN_WARNING "%s: requested device busy %02x\n", dev->name, rc);
                                 return -EBUSY;
                         }
                } else {        
                        if (i == WRITE) {
                                free_irq(get_irq_by_devno(ctc_adapter[m][i].devno[c]), privptr->channel[i].devstat);
                                channel_free(m, ctc_adapter[m][i].devno[READ]);
                                kfree(privptr->channel[READ].devstat);
                        } 
                        kfree(privptr->channel[i].devstat);
                        return -ENODEV;
                }
        }

        privptr->channel[READ].devno = ctc_adapter[m][i].devno[READ];
        privptr->channel[READ].irq = get_irq_by_devno(ctc_adapter[m][i].devno[READ]);
        privptr->channel[WRITE].devno = ctc_adapter[m][i].devno[WRITE];
        privptr->channel[WRITE].irq = get_irq_by_devno(ctc_adapter[m][i].devno[WRITE]);
        privptr->protocol = ctc_adapter[m][i].protocol;
        channel[m].left = channel[m].left - 2;

        printk(KERN_INFO "%s: read dev: %04x irq: %04x - write dev: %04x irq: %04x \n",
            dev->name, privptr->channel[READ].devno,   privptr->channel[READ].irq,
            privptr->channel[WRITE].devno,  privptr->channel[WRITE].irq); 

        dev->mtu             = CTC_DEFAULT_MTU_SIZE;
        dev->hard_start_xmit = ctc_tx;
        dev->open            = ctc_open;
        dev->stop            = ctc_release;
        dev->get_stats       = ctc_stats;
        dev->change_mtu      = ctc_change_mtu;
        dev->hard_header_len = 0;
        dev->addr_len        = 0;
        dev->type            = ARPHRD_SLIP;
        dev->tx_queue_len    = 100;
        dev_init_buffers(dev);
        dev->flags           = IFF_POINTOPOINT | IFF_NOARP;   

        return 0;
} 


/*
 *   Interrupt processing 
 *
 */

static void inline ccw_check_return_code (net_device *dev, int return_code)
{
        if (return_code != 0) {
                switch (return_code) {
                        case -EBUSY:  
                                printk(KERN_INFO "%s: Busy !\n", dev->name);
                                break;
                        case -ENODEV:
                                printk(KERN_EMERG "%s: Invalid device called for IO\n", dev->name);
                                break;
                        case -EIO:
                                printk(KERN_EMERG "%s: Status pending... \n", dev->name);
                                break;
                        default:
                                printk(KERN_EMERG "%s: Unknown error in Do_IO %04x\n", 
                                    dev->name, return_code);
                }
        }
} 


static void inline ccw_check_unit_check (net_device *dev, char sense)
{
#ifdef DEBUG
        printk(KERN_INFO "%s: Unit Check with sense code: %02x\n",
            dev->name, sense);
#endif

        if (sense & 0x40) {
#ifdef DEBUG
                if (sense & 0x01) 
                        printk(KERN_DEBUG "%s: Interface disconnect or Selective reset occurred (remote side)\n", dev->name);
                else 
                        printk(KERN_DEBUG "%s: System reset occured (remote side)\n", dev->name);
#endif
        } else if (sense & 0x20) {
                if (sense & 0x04)
                        printk(KERN_WARNING "%s: Data-streaming timeout)\n", dev->name);
                else 
                        printk(KERN_WARNING "%s: Data-transfer parity error\n", dev->name);
        } else if (sense & 0x10) {
                if (sense & 0x20)
                        printk(KERN_WARNING "%s: Hardware malfunction (remote side)\n", dev->name);
                else 
                        printk(KERN_WARNING "%s: Read-data parity error (remote side)\n", dev->name);
        }

} 


static void ctc_irq_handler (int irq, void *initparm, struct pt_regs *regs)
{
        int               rc = 0;
        __u32             parm;
        __u8              flags = 0x00;
        struct  channel   *ctc = NULL;
        struct  ctc_priv  *privptr = NULL;
        net_device        *dev = NULL;    
        
        ccw1_t            ccw_set_x_mode[2] = {{CCW_CMD_SET_EXTENDED, CCW_FLAG_SLI | CCW_FLAG_CC, 0, NULL},
                                               {CCW_CMD_NOOP, CCW_FLAG_SLI, 0, NULL}}; 

        devstat_t *devstat = ((devstat_t *)initparm);

        /* Bypass all 'unsolited interrupts' */
        if (devstat->intparm == 0) {
#ifdef DEBUG
                printk(KERN_DEBUG "ctc: unsolited interrupt for device: %04x received c-%02x d-%02x f-%02x\n",
                    devstat->devno, devstat->cstat, devstat->dstat, devstat->flag);
#endif 
                /* FIXME - find the related intparm!!! No IO outstanding!!!! */
                return;
        }

        ctc = (struct channel *) (devstat->intparm);
        dev = (net_device *) ctc->dev;
        privptr = dev->priv;

#ifdef DEBUG
        printk(KERN_DEBUG "%s: interrupt for device: %04x received c-%02x d-%02x f-%02x state-%02x\n",
            dev->name, ctc->devno, devstat->cstat, devstat->dstat, devstat->flag, ctc->state);
#endif 

        /* Check for good subchannel return code, otherwise error message */
        if (devstat->cstat) {
                printk(KERN_WARNING "%s: subchannel check for device: %04x - %02x\n", 
                    dev->name, ctc->devno, devstat->cstat);
                return;
        }


        /* Check the reason-code of a unit check */
        if (devstat->dstat & DEV_STAT_UNIT_CHECK)
                ccw_check_unit_check(dev, devstat->ii.sense.data[0]);


        /* State machine to bring the connection up / down and to restart */ 

        ctc->last_dstat = devstat->dstat;

        switch (ctc->state) {   

                case CTC_STOP:           /* HALT_IO issued by ctc_release (halt sequence) */
                        if (!devstat->flag & DEVSTAT_FINAL_STATUS)
                                return;
                        wake_up(&ctc->wait);   /* wake up ctc_release */
                        return;
 

                case CTC_START_HALT_IO:  /* HALT_IO issued by ctc_open (start sequence) */
                        if (!devstat->flag & DEVSTAT_FINAL_STATUS)
                                return;

                        ctc->state = CTC_START_SET_X_MODE;
                        parm = (__u32) ctc;
                        rc = do_IO (ctc->irq, &ccw_set_x_mode[0], parm, 0xff, flags);
                        if (rc != 0)
                                ccw_check_return_code(dev, rc);
                        return;
 
        
                case CTC_START_SET_X_MODE:
                        if (devstat->dstat & DEV_STAT_UNIT_CHECK) {
                                if ((devstat->ii.sense.data[0] & 0x41) != 0x41 ||
                                    (devstat->ii.sense.data[0] & 0x40) != 0x40) {
                                        wake_up(&ctc->wait);  /* wake up ctc_open (READ or WRITE) */
                                        return; 
                                }        
                        }
                        if (!devstat->flag & DEVSTAT_FINAL_STATUS)
                                return;
                        ctc->state =  CTC_START_SELECT;
 

                case CTC_START_SELECT:
                        if (!ctc->flag & CTC_WRITE) {
                                ctc->state = CTC_START_READ_TEST;
                                ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->free_anchor->block);
                                parm = (__u32) ctc;
                                rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
                                if (rc != 0) 
                                        ccw_check_return_code(dev, rc);
                                wake_up(&ctc->wait);  /* wake up ctc_open (READ) */

                        } else {
                                ctc->state = CTC_START_WRITE_TEST;
                                /* ADD HERE THE RIGHT PACKET TO ISSUE A ROUND TRIP - PART 1 */
                                ctc->ccw[1].count = 0;
                                ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->free_anchor->block);
                                parm = (__u32) ctc; 
                                rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags);
                                if (rc != 0)
                                        ccw_check_return_code(dev, rc);
                        }
                        return;


                case CTC_START_READ_TEST:
                        if (devstat->dstat & DEV_STAT_UNIT_CHECK) {
                                if ((devstat->ii.sense.data[0] & 0x41) == 0x41 ||
                                    (devstat->ii.sense.data[0] & 0x40) == 0x40 ||
                                    devstat->ii.sense.data[0] == 0                ) {
                                        init_timer(&ctc->timer);
                                        ctc->timer.function = (void *)ctc_read_retry; 
                                        ctc->timer.data = (__u32)ctc;
                                        ctc->timer.expires = jiffies + 10*HZ;
                                        add_timer(&ctc->timer);
#ifdef DEBUG 
                                        printk(KERN_DEBUG "%s: read connection restarted\n",dev->name); 
#endif
                                }
                                return;
                        }

                        if ((devstat->dstat &  ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) != 0x00) {
                                if ((devstat->dstat & DEV_STAT_ATTENTION) && 
                                    (devstat->dstat & DEV_STAT_BUSY)) {
                                        printk(KERN_WARNING "%s: read channel is connected with the remote side read channel\n", dev->name);
                                } 
                                wake_up(&privptr->channel[WRITE].wait);  /* wake up ctc_open (WRITE) */
                                return;
                        }

                        ctc->state = CTC_START_READ;
                        set_bit(0, (void *)&ctc->IO_active);

                        /* ADD HERE THE RIGHT PACKET TO ISSUE A ROUND TRIP - PART 2 */
                        /* wake_up(&privptr->channel[WRITE].wait);*/  /* wake up ctc_open (WRITE) */


                case CTC_START_READ: 
                        if (devstat->dstat & DEV_STAT_UNIT_CHECK) {
                                if ((devstat->ii.sense.data[0] & 0x41) == 0x41 ||
                                    (devstat->ii.sense.data[0] & 0x40) == 0x40 ||
                                    devstat->ii.sense.data[0] == 0               ) {
                                        privptr->stats.rx_errors++;
					/* Need protection here cos we are in the read irq */
					/*  handler the tbusy is for the write subchannel */
					ctc_protect_busy(dev);
				        ctc_setbit_busy(TB_RETRY,dev);
					ctc_unprotect_busy(dev);
				        init_timer(&ctc->timer);
                                        ctc->timer.function = (void *)ctc_read_retry; 
                                        ctc->timer.data = (__u32)ctc;
                                        ctc->timer.expires = jiffies + 30*HZ;
                                        add_timer(&ctc->timer); 
                                        printk(KERN_INFO "%s: connection restarted!! problem on remote side\n",dev->name);
                                }
                                return;
                        }

                        if(!devstat->flag & DEVSTAT_FINAL_STATUS)
                                return; 
			ctc_protect_busy(dev);
			ctc_clearbit_busy(TB_RETRY,dev);
			ctc_unprotect_busy(dev);
                        ctc_buffer_swap(&ctc->free_anchor, &ctc->proc_anchor);

                        if (ctc->free_anchor != NULL) {  
                                ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->free_anchor->block);
                                parm = (__u32) ctc;
                                rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
                                if (rc != 0) 
                                        ccw_check_return_code(dev, rc);
                        } else {
                                clear_bit(0, (void *)&ctc->IO_active);    
#ifdef DEBUG
                                printk(KERN_DEBUG "%s: No HOT READ started in IRQ\n",dev->name);
#endif
                        }
                        
                        if (test_and_set_bit(CTC_BH_ACTIVE, (void *)&ctc->flag_a) == 0) {
                                queue_task(&ctc->tq, &tq_immediate);
                                mark_bh(IMMEDIATE_BH);
                        }
                        return;


                case CTC_START_WRITE_TEST:
                        if (devstat->dstat & DEV_STAT_UNIT_CHECK) {
                                if ((devstat->ii.sense.data[0] & 0x41) == 0x41 ||
                                    (devstat->ii.sense.data[0] & 0x40) == 0x40 ||
                                    devstat->ii.sense.data[0] == 0                ) {
                                        init_timer(&ctc->timer);
                                        ctc->timer.function = (void *)ctc_write_retry; 
                                        ctc->timer.data = (__u32)ctc;
                                        ctc->timer.expires = jiffies + 10*HZ;
                                        add_timer(&ctc->timer);
#ifdef DEBUG
                                        printk(KERN_DEBUG "%s: write connection restarted\n",dev->name);
#endif
                                }
                                return;
                        }

                        ctc->state = CTC_START_WRITE;
                        wake_up(&ctc->wait);  /* wake up ctc_open (WRITE) */
                        return;
 

                case CTC_START_WRITE:
                        if (devstat->dstat & DEV_STAT_UNIT_CHECK) {
                                privptr->stats.tx_errors += ctc->proc_anchor->packets;
#ifdef DEBUG
                                printk(KERN_DEBUG "%s: Unit Check on write channel\n",dev->name);
#endif
                        } else { 
                                if (!devstat->flag & DEVSTAT_FINAL_STATUS)
                                        return; 
                                privptr->stats.tx_packets += ctc->proc_anchor->packets;
                        } 

                        ctc->proc_anchor->block->length = 0;
                        ctc_buffer_swap(&ctc->proc_anchor, &ctc->free_anchor);
                        ctc_clearbit_busy(TB_NOBUFFER,dev);      
                        if (ctc->proc_anchor != NULL) {  
#ifdef DEBUG
                                printk(KERN_DEBUG "%s: IRQ early swap buffer\n",dev->name); 
#endif
                                ctc->ccw[1].count = ctc->proc_anchor->block->length;
                                ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->proc_anchor->block);
                                parm = (__u32) ctc;
                                rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
                                if (rc != 0) 
                                        ccw_check_return_code(dev, rc);
                                dev->trans_start = jiffies;
                                return;

                        }

                        if (ctc->free_anchor->block->length != 0) {
                                if (ctc_test_and_setbit_busy(TB_TX,dev) == 0) {     
                                       /* set transmission to busy */
                                        ctc_buffer_swap(&ctc->free_anchor, &ctc->proc_anchor);
                                        ctc_clearbit_busy(TB_TX,dev);
#ifdef DEBUG
                                        printk(KERN_DEBUG "%s: last buffer move in IRQ\n",dev->name); 
#endif
                                        ctc->ccw[1].count = ctc->proc_anchor->block->length;
                                        ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->proc_anchor->block);
                                        parm = (__u32) ctc;
                                        rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
                                        if (rc != 0) 
                                                ccw_check_return_code(dev, rc);
                                        dev->trans_start = jiffies;
                                        return;
                                }
                        } 

                        clear_bit(0, (void *)&ctc->IO_active);              /* set by ctc_tx or ctc_bh */
                        return;

 
                default: 
                        printk(KERN_WARNING "%s: wrong selection code - irq\n",dev->name);
                        return;  
        }
} 


static void ctc_irq_bh (struct channel *ctc)
{
        int                rc = 0;
        __u16              data_len;
        __u32              parm;

        __u8               flags = 0x00;
        __u32              saveflags;
        net_device  *dev;
        struct ctc_priv    *privptr;             
        struct packet      *lp;
        struct sk_buff     *skb;

        dev = (net_device *) ctc->dev; 
        privptr = (struct ctc_priv *) dev->priv; 
   
#ifdef DEBUG
        printk(KERN_DEBUG "%s: bh routine - state-%02x\n" ,dev->name, ctc->state);
#endif 

        while (ctc->proc_anchor != NULL) { 

                lp = &ctc->proc_anchor->block->data;

                while ((__u8 *) lp < (__u8 *) &ctc->proc_anchor->block->length + ctc->proc_anchor->block->length) {
                        data_len = lp->length - PACKET_HEADER_LENGTH;
                        skb = dev_alloc_skb(data_len); 
                        if (skb) { 
                                memcpy(skb_put(skb, data_len),&lp->data, data_len);
                                skb->mac.raw = skb->data;
                                skb->dev = dev;
                                skb->protocol = htons(ETH_P_IP);
                                skb->ip_summed = CHECKSUM_UNNECESSARY; /* no UC happened!!! */
                                netif_rx(skb);
                                privptr->stats.rx_packets++;
                        } else {
                                privptr->stats.rx_dropped++; 
                                printk(KERN_WARNING "%s: is low on memory\n",dev->name);
                        }
                        (__u8 *)lp += lp->length;
                }

                s390irq_spin_lock_irqsave(ctc->irq, saveflags);
                ctc_buffer_swap(&ctc->proc_anchor, &ctc->free_anchor);

                if (test_and_set_bit(0, (void *)&ctc->IO_active) == 0) {
#ifdef DEBUG
                        printk(KERN_DEBUG "%s: HOT READ started in bh routine\n" ,dev->name);
#endif 
                        ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->free_anchor->block);
                        parm = (__u32) ctc; 
                        rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
                        if (rc != 0) 
                                 ccw_check_return_code(dev, rc);
                } 
                s390irq_spin_unlock_irqrestore(ctc->irq, saveflags); 
        }
        clear_bit(CTC_BH_ACTIVE, (void *)&ctc->flag_a);
        return;
}  


static void ctc_read_retry (struct channel *ctc)
{
        int                rc = 0;
        __u32              parm;
        __u8               flags = 0x00;
        __u32              saveflags;
        net_device  *dev;

        dev = (net_device *) ctc->dev; 
   
#ifdef DEBUG
        printk(KERN_DEBUG "%s: read retry - state-%02x\n" ,dev->name, ctc->state);
#endif 
        s390irq_spin_lock_irqsave(ctc->irq, saveflags);
        ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->free_anchor->block);
        parm = (__u32) ctc; 
        rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
        s390irq_spin_unlock_irqrestore(ctc->irq, saveflags);
        if (rc != 0) 
                ccw_check_return_code(dev, rc);
        return;
}  
 

static void ctc_write_retry (struct channel *ctc)
{
        int                rc = 0;
        __u32              parm;
        __u8               flags = 0x00;
        __u32              saveflags;
        net_device  *dev;

        dev = (net_device *) ctc->dev; 
   
#ifdef DEBUG
        printk(KERN_DEBUG "%s: write retry - state-%02x\n" ,dev->name, ctc->state);
#endif 
        s390irq_spin_lock_irqsave(ctc->irq, saveflags);
        ctc->ccw[1].count = 0;
        ctc->ccw[1].cda  = (char *)virt_to_phys(ctc->proc_anchor->block);
        parm = (__u32) ctc; 
        rc = do_IO (ctc->irq, &ctc->ccw[0], parm, 0xff, flags );
        s390irq_spin_unlock_irqrestore(ctc->irq, saveflags);
        if (rc != 0) 
                ccw_check_return_code(dev, rc);
        return;
}  



/*
 *   ctc_open
 *
 */
static int ctc_open(net_device *dev)
{
        int                rc;
        int                i;
        int                j;
        __u8               flags = 0x00;
        __u32              saveflags;
        __u32              parm;
        struct ctc_priv    *privptr;
	DECLARE_WAITQUEUE(wait, current);
        struct timer_list  timer;


        ctc_set_busy(dev);

        privptr = (struct ctc_priv *) (dev->priv);
        
        privptr->channel[READ].flag  = 0x00;
        privptr->channel[WRITE].flag = CTC_WRITE;

        for (i = 0; i < 2;  i++) { 
                for (j = 0; j < CTC_BLOCKS;  j++) { 
                        rc = ctc_buffer_alloc(&privptr->channel[i]);
                        if (rc != 0)
                                return -ENOMEM;
                }
                init_waitqueue_head(&privptr->channel[i].wait);
                INIT_LIST_HEAD(&privptr->channel[i].tq.list);
                privptr->channel[i].tq.sync = 0;
                privptr->channel[i].tq.routine = (void *)(void *)ctc_irq_bh;
                privptr->channel[i].tq.data = &privptr->channel[i]; 

                privptr->channel[i].dev = dev;
                
                privptr->channel[i].flag_a = 0;
                privptr->channel[i].IO_active = 0;

                privptr->channel[i].ccw[0].cmd_code  = CCW_CMD_PREPARE;
                privptr->channel[i].ccw[0].flags     = CCW_FLAG_SLI | CCW_FLAG_CC;
                privptr->channel[i].ccw[0].count     = 0;
                privptr->channel[i].ccw[0].cda       = NULL;
                if (i == READ) {  
                        privptr->channel[i].ccw[1].cmd_code  = CCW_CMD_READ;
                        privptr->channel[i].ccw[1].flags     = CCW_FLAG_SLI;
                        privptr->channel[i].ccw[1].count     = 0xffff;   /* MAX size */
                        privptr->channel[i].ccw[1].cda       = NULL;
                } else {
                        privptr->channel[i].ccw[1].cmd_code = CCW_CMD_WRITE;
                        privptr->channel[i].ccw[1].flags    = CCW_FLAG_SLI | CCW_FLAG_CC;
                        privptr->channel[i].ccw[1].count    = 0;
                        privptr->channel[i].ccw[1].cda      = NULL;
                }
                privptr->channel[i].ccw[2].cmd_code = CCW_CMD_NOOP;      /* jointed CE+DE */
                privptr->channel[i].ccw[2].flags    = CCW_FLAG_SLI;
                privptr->channel[i].ccw[2].count    = 0;
                privptr->channel[i].ccw[2].cda      = NULL;
                
                privptr->channel[i].flag  &= ~CTC_TIMER;
                init_timer(&timer);
                timer.function = (void *)ctc_timer; 
                timer.data = (__u32)&privptr->channel[i];
                timer.expires = jiffies + 150*HZ;                        /* time to connect with the remote side */
                add_timer(&timer);

                s390irq_spin_lock_irqsave(privptr->channel[i].irq, saveflags);
                parm = (unsigned long) &privptr->channel[i]; 
                privptr->channel[i].state = CTC_START_HALT_IO;
                rc = halt_IO(privptr->channel[i].irq, parm, flags);
                add_wait_queue(&privptr->channel[i].wait, &wait);
                current->state = TASK_INTERRUPTIBLE;
                s390irq_spin_unlock_irqrestore(privptr->channel[i].irq, saveflags);
                schedule();
                remove_wait_queue(&privptr->channel[i].wait, &wait);
                if(rc != 0)
                        ccw_check_return_code(dev, rc);
                if((privptr->channel[i].flag & CTC_TIMER) == 0x00)
                        del_timer(&timer);
        }

        if ((((privptr->channel[READ].last_dstat | privptr->channel[WRITE].last_dstat) & 
               ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) != 0x00) ||
            (((privptr->channel[READ].flag | privptr->channel[WRITE].flag) & CTC_TIMER) != 0x00)) {
#ifdef DEBUG
                printk(KERN_DEBUG "%s: channel problems during open - read: %02x -  write: %02x\n",
                    dev->name, privptr->channel[READ].last_dstat, privptr->channel[WRITE].last_dstat);
#endif 
                printk(KERN_INFO "%s: remote side is currently not ready\n", dev->name);
                
                for (i = 0; i < 2;  i++) {
                        s390irq_spin_lock_irqsave(privptr->channel[i].irq, saveflags);
                        parm = (unsigned long) &privptr->channel[i];
                        privptr->channel[i].state = CTC_STOP;
                        rc = halt_IO(privptr->channel[i].irq, parm, flags);
                        s390irq_spin_unlock_irqrestore(privptr->channel[i].irq, saveflags);
                        if (rc != 0)
                                ccw_check_return_code(dev, rc);
                        for (j = 0; j < CTC_BLOCKS;  j++) 
                                ctc_buffer_free(&privptr->channel[i]);
                }
                return -EIO;
        }

        printk(KERN_INFO "%s: connected with remote side\n",dev->name);
        ctc_clear_busy(dev);
        return 0;
}


static void ctc_timer (struct channel *ctc)
{
#ifdef DEBUG
        net_device  *dev;

        dev = (net_device *) ctc->dev; 
        printk(KERN_DEBUG "%s: timer return\n" ,dev->name);
#endif
        ctc->flag |= CTC_TIMER;
        wake_up(&ctc->wait);  
        return;
}  

/*
 *   ctc_release 
 *
 */
static int ctc_release(net_device *dev)
{   
        int                rc;
        int                i;
        int                j;
        __u8               flags = 0x00;
        __u32              saveflags;
        __u32              parm;
        struct ctc_priv    *privptr;
	DECLARE_WAITQUEUE(wait, current);

        privptr = (struct ctc_priv *) dev->priv;  
   
	ctc_protect_busy_irqsave(dev,saveflags);
        ctc_setbit_busy(TB_STOP,dev);    
	ctc_unprotect_busy_irqrestore(dev,flags);
        for (i = 0; i < 2;  i++) {
                s390irq_spin_lock_irqsave(privptr->channel[i].irq, saveflags);
                privptr->channel[i].state = CTC_STOP;
                parm = (__u32) &privptr->channel[i]; 
                rc = halt_IO (privptr->channel[i].irq, parm, flags );
                add_wait_queue(&privptr->channel[i].wait, &wait);
                current->state = TASK_INTERRUPTIBLE;
                s390irq_spin_unlock_irqrestore(privptr->channel[i].irq, saveflags); 
                schedule();
                remove_wait_queue(&privptr->channel[i].wait, &wait);
                if (rc != 0) {
                        ccw_check_return_code(dev, rc);
                }
                 
                for (j = 0; j < CTC_BLOCKS;  j++) {  
                        ctc_buffer_swap(&privptr->channel[i].proc_anchor, &privptr->channel[i].free_anchor);
                        ctc_buffer_free(&privptr->channel[i]); 
                }
        }

        if (((privptr->channel[READ].last_dstat | privptr->channel[WRITE].last_dstat) &
            ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) != 0x00) {
                printk(KERN_WARNING "%s: channel problems during close - read: %02x -  write: %02x\n",
                    dev->name, privptr->channel[READ].last_dstat, privptr->channel[WRITE].last_dstat);
                return -EIO;
        }

        return 0;
}  


/*
 *   ctc_tx 
 *
 *
 */
static int ctc_tx(struct sk_buff *skb, net_device *dev)
{
        int                rc=0,rc2;
        __u32              parm;
        __u8               flags = 0x00;
        __u32              saveflags;
        struct ctc_priv    *privptr;
        struct packet      *lp;

   
        privptr = (struct ctc_priv *) (dev->priv);

        if (skb == NULL) { 
                printk(KERN_WARNING "%s: NULL pointer as sk_buffer passed\n", dev->name);
                privptr->stats.tx_dropped++;
                return -EIO;
        }
        
        s390irq_spin_lock_irqsave(privptr->channel[WRITE].irq, saveflags);
        if (ctc_check_busy(dev)) {
                rc=-EBUSY;
		goto Done;
        } 

        if (ctc_test_and_setbit_busy(TB_TX,dev)) {                /* set transmission to busy */
                rc=-EBUSY;
		goto Done;
        } 

        if (65535 - privptr->channel[WRITE].free_anchor->block->length - PACKET_HEADER_LENGTH <= skb->len + PACKET_HEADER_LENGTH + 2) {
#ifdef DEBUG
                printk(KERN_DEBUG "%s: early swap\n", dev->name);
#endif
               
                ctc_buffer_swap(&privptr->channel[WRITE].free_anchor, &privptr->channel[WRITE].proc_anchor);
                if (privptr->channel[WRITE].free_anchor == NULL){
                        ctc_setbit_busy(TB_NOBUFFER,dev);
                        rc=-EBUSY;
			goto Done2;
                }
        }
        
        if (privptr->channel[WRITE].free_anchor->block->length == 0) {
                privptr->channel[WRITE].free_anchor->block->length = BLOCK_HEADER_LENGTH; 
                privptr->channel[WRITE].free_anchor->packets = 0;
        } 


        (__u8 *)lp = (__u8 *) &privptr->channel[WRITE].free_anchor->block->length + privptr->channel[WRITE].free_anchor->block->length;
        privptr->channel[WRITE].free_anchor->block->length += skb->len + PACKET_HEADER_LENGTH;
        lp->length = skb->len + PACKET_HEADER_LENGTH; 
        lp->type = 0x0800; 
        lp->unused = 0;
        memcpy(&lp->data, skb->data, skb->len); 
        (__u8 *) lp += lp->length; 
        lp->length = 0;
        dev_kfree_skb(skb);
        privptr->channel[WRITE].free_anchor->packets++;

        if (test_and_set_bit(0, (void *)&privptr->channel[WRITE].IO_active) == 0) {
	       ctc_buffer_swap(&privptr->channel[WRITE].free_anchor,&privptr->channel[WRITE].proc_anchor); 
                privptr->channel[WRITE].ccw[1].count = privptr->channel[WRITE].proc_anchor->block->length;
                privptr->channel[WRITE].ccw[1].cda   = (char *)virt_to_phys(privptr->channel[WRITE].proc_anchor->block);
                parm = (__u32) &privptr->channel[WRITE];  
                rc2 = do_IO (privptr->channel[WRITE].irq, &privptr->channel[WRITE].ccw[0], parm, 0xff, flags );
                if (rc2 != 0) 
                        ccw_check_return_code(dev, rc2);
                dev->trans_start = jiffies;
        }
        if (privptr->channel[WRITE].free_anchor == NULL)
                ctc_setbit_busy(TB_NOBUFFER,dev);
Done2:
        ctc_clearbit_busy(TB_TX,dev);
Done:
	s390irq_spin_unlock_irqrestore(privptr->channel[WRITE].irq, saveflags);
        return(rc);
} 


/*
 *   ctc_change_mtu 
 *
 *   S/390 can handle MTU sizes from 576 to 32760 for VM, VSE
 *                                   576 to 65527 for OS/390
 *
 */
static int ctc_change_mtu(net_device *dev, int new_mtu)
{
        if ((new_mtu < 576) || (new_mtu > 65528))
                return -EINVAL;
        dev->mtu = new_mtu;
        return 0;
}


/*
 *   ctc_stats
 *
 */
struct net_device_stats *ctc_stats(net_device *dev)
{
         struct ctc_priv *privptr;
   
         privptr = dev->priv;
         return &privptr->stats;
}  


/* Module code goes here */

/*
                free_irq(privptr->channel[i].irq, privptr->channel[i].devstat);
                kfree(privptr->channel[i].devstat); 

*/
/* --- This is the END my friend --- */
