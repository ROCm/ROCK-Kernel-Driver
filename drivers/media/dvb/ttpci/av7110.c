/*
 * av7110.c: driver for the SAA7146 based AV110 cards (like the Fujitsu-Siemens DVB)
 *
 * Copyright (C) 1999-2002 Ralph  Metzler 
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#define NEW_CI 1

/* for debugging ARM communication: */
//#define COM_DEBUG

#define __KERNEL_SYSCALLS__
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/unistd.h>
#include <linux/byteorder/swabb.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <stdarg.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/semaphore.h>

#include <linux/dvb/frontend.h>

#include "dvb_i2c.h"
#include "dvb_frontend.h"
#include "dvb_functions.h"


#if 1 
	#define DEBUG_VARIABLE av7110_debug
#else
	#define DEB_S(x) 
	#define DEB_D(x) 
	#define DEB_EE(x)
#endif

#include "ttpci-eeprom.h"
#include "av7110.h"
#include "av7110_ipack.h"

static int AV_StartPlay(struct av7110 *av7110, int av);
static void restart_feeds(struct av7110 *av7110);
static int bootarm(struct av7110 *av7110);
static inline int i2c_writereg(struct av7110 *av7110, u8 id, u8 reg, u8 val);
static inline u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg);
static int  outcom(struct av7110 *av7110, int type, int com, int num, ...);
static void SetMode(struct av7110 *av7110, int mode);
static void dvb_video_add_event (struct av7110 *av7110, struct video_event *event);

void pes_to_ts(u8 const *buf, long int length, u16 pid, struct av7110_p2t *p);
void p_to_t(u8 const *buf, long int length, u16 pid, u8 *counter, struct dvb_demux_feed *feed);

static int av7110_debug = 0;

static int vidmode=CVBS_RGB_OUT;
static int pids_off;
static int adac=DVB_ADAC_TI;
static int hw_sections = 1;

int av7110_num = 0;

#define FW_CI_LL_SUPPORT(arm_app) ((arm_app) & 0x80000000)
#define FW_VERSION(arm_app)       ((arm_app) & 0x0000FFFF)

/****************************************************************************
 * DEBI functions
 ****************************************************************************/

/* This DEBI code is based on the Stradis driver 
   by Nathan Laredo <laredo@gnu.org> */

static int wait_for_debi_done(struct av7110 *av7110)
{
	struct saa7146_dev *dev = av7110->dev;
	int start;

	/* wait for registers to be programmed */
	start = jiffies;
	while (1) {
                if (saa7146_read(dev, MC2) & 2)
                        break;
		if (jiffies-start > HZ/20) {
			printk ("%s: timed out while waiting for registers "
				"getting programmed\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
	}

	/* wait for transfer to complete */
	start = jiffies;
	while (1) {
		if (!(saa7146_read(dev, PSR) & SPCI_DEBI_S))
			break;
		saa7146_read(dev, MC2);
		if (jiffies-start > HZ/4) {
			printk ("%s: timed out while waiting for transfer "
				"completion\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int debiwrite(struct av7110 *av7110, u32 config, 
                     int addr, u32 val, int count)
{
        struct saa7146_dev *dev = av7110->dev;
	u32 cmd;

	if (count <= 0 || count > 32764)
		return -1;
	if (wait_for_debi_done(av7110) < 0)
		return -1;
	saa7146_write(dev, DEBI_CONFIG, config);
	if (count <= 4)		/* immediate transfer */
		saa7146_write(dev, DEBI_AD, val );
	else			/* block transfer */
		saa7146_write(dev, DEBI_AD, av7110->debi_bus);
	saa7146_write(dev, DEBI_COMMAND, (cmd = (count << 17) | (addr & 0xffff)));
	saa7146_write(dev, MC2, (2 << 16) | 2);
	return 0;
}

static u32 debiread(struct av7110 *av7110, u32 config, int addr, int count)
{
        struct saa7146_dev *dev = av7110->dev;
	u32 result = 0;

	if (count > 32764 || count <= 0)
		return 0;
	if (wait_for_debi_done(av7110) < 0)
		return 0;
	saa7146_write(dev, DEBI_AD, av7110->debi_bus);
	saa7146_write(dev, DEBI_COMMAND, (count << 17) | 0x10000 | (addr & 0xffff));

	saa7146_write(dev, DEBI_CONFIG, config);
	saa7146_write(dev, MC2, (2 << 16) | 2);
	if (count > 4)	
		return count;
	wait_for_debi_done(av7110);
	result = saa7146_read(dev, DEBI_AD);
        result &= (0xffffffffUL >> ((4-count)*8));
	return result;
}


/* DEBI during interrupt */
/* single word writes */
static inline void iwdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
        debiwrite(av7110, config, addr, val, count);
}

/* buffer writes */
static inline void mwdebi(struct av7110 *av7110, u32 config, int addr, char *val, int count)
{
	memcpy(av7110->debi_virt, val, count);
        debiwrite(av7110, config, addr, 0, count);
}


static inline u32 irdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
        u32 res;

        res=debiread(av7110, config, addr, count);
        if (count<=4) 
                memcpy(av7110->debi_virt, (char *) &res, count);
        return res;
}

/* DEBI outside interrupts, only for count<=4! */

static inline void wdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
        unsigned long flags;

        spin_lock_irqsave(&av7110->debilock, flags);
        debiwrite(av7110, config, addr, val, count);
        spin_unlock_irqrestore(&av7110->debilock, flags);
}

static inline u32 rdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
        unsigned long flags;
        u32 res;

        spin_lock_irqsave(&av7110->debilock, flags);
        res=debiread(av7110, config, addr, count);
        spin_unlock_irqrestore(&av7110->debilock, flags);
        return res;
}


static inline char chtrans(char c)
{
        if (c<32 || c>126)
                c=0x20;
        return c;
}


/* handle mailbox registers of the dual ported RAM */

static inline void ARM_ResetMailBox(struct av7110 *av7110)
{
        unsigned long flags;

	DEB_EE(("av7110: %p\n",av7110));

        spin_lock_irqsave(&av7110->debilock, flags);
        debiread(av7110, DEBINOSWAP, IRQ_RX, 2);
        //printk("dvb: IRQ_RX=%d\n", debiread(av7110, DEBINOSWAP, IRQ_RX, 2));
        debiwrite(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
        spin_unlock_irqrestore(&av7110->debilock, flags);
}

static inline void ARM_ClearMailBox(struct av7110 *av7110)
{
        iwdebi(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
}

static inline void ARM_ClearIrq(struct av7110 *av7110)
{
	irdebi(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
}

static void reset_arm(struct av7110 *av7110)
{
        saa7146_setgpio(av7110->dev, RESET_LINE, SAA7146_GPIO_OUTLO);

        /* Disable DEBI and GPIO irq */
	IER_DISABLE(av7110->dev, (MASK_19 | MASK_03));
//        saa7146_write(av7110->dev, IER, 
//                      saa7146_read(av7110->dev, IER) & ~(MASK_19 | MASK_03));
        saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));

        mdelay(800);
        saa7146_setgpio(av7110->dev, RESET_LINE, SAA7146_GPIO_OUTHI);
        mdelay(800);

        ARM_ResetMailBox(av7110); 

        saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));

	IER_ENABLE(av7110->dev, MASK_03);
//        saa7146_write(av7110->dev, IER, 
//                      saa7146_read(av7110->dev, IER) | MASK_03 );

        av7110->arm_ready=1;
        printk("av7110: ARM RESET\n");
}

static void recover_arm(struct av7110 *av7110)
{
	DEB_EE(("av7110: %p\n",av7110));

        if (current->files)
                bootarm(av7110);
        else {
                printk("OOPS, no current->files\n");
                reset_arm(av7110);
        }

        dvb_delay(100); 
        restart_feeds(av7110);
        outcom(av7110, COMTYPE_PIDFILTER, SetIR, 1, av7110->ir_config);
}

static void arm_error(struct av7110 *av7110)
{
	DEB_EE(("av7110: %p\n",av7110));

        av7110->arm_errors++;
        av7110->arm_ready=0;
        recover_arm(av7110);
}

static int arm_thread(void *data)
{
	struct av7110 *av7110 = data;
        u16 newloops = 0;

	DEB_EE(("av7110: %p\n",av7110));
	
	dvb_kernel_thread_setup ("arm_mon");
	av7110->arm_thread = current;

	while (!av7110->arm_rmmod && !signal_pending(current)) {
                interruptible_sleep_on_timeout(&av7110->arm_wait, 5*HZ);

                if (!av7110->arm_ready)
                        continue;

                if (down_interruptible(&av7110->dcomlock))
                        break;

                newloops=rdebi(av7110, DEBINOSWAP, STATUS_LOOPS, 0, 2);
                up(&av7110->dcomlock);

                if (newloops==av7110->arm_loops) {
                        printk(KERN_ERR "av7110%d: ARM crashed!\n",
				av7110->dvb_adapter->num);

			arm_error(av7110);

                        if (down_interruptible(&av7110->dcomlock))
                                break;

                        newloops=rdebi(av7110, DEBINOSWAP, STATUS_LOOPS, 0, 2)-1;
                        up(&av7110->dcomlock);
                }
                av7110->arm_loops=newloops;
	}

	av7110->arm_thread = NULL;
	return 0;
}


static int record_cb(struct dvb_filter_pes2ts *p2t, u8 *buf, size_t len)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) p2t->priv;

	DEB_EE(("struct dvb_filter_pes2ts:%p\n",p2t));

        if (!(dvbdmxfeed->ts_type & TS_PACKET)) 
                return 0;
	if (buf[3]==0xe0)        // video PES do not have a length in TS
                buf[4]=buf[5]=0;
        if (dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY) 
                return dvbdmxfeed->cb.ts(buf, len, 0, 0, 
                                         &dvbdmxfeed->feed.ts, DMX_OK); 
        else
                return dvb_filter_pes2ts(p2t, buf, len);
}

static int dvb_filter_pes2ts_cb(void *priv, unsigned char *data)
{
        struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) priv;

	DEB_EE(("dvb_demux_feed:%p\n",dvbdmxfeed));
        
        dvbdmxfeed->cb.ts(data, 188, 0, 0,
                          &dvbdmxfeed->feed.ts,
                          DMX_OK); 
        return 0;
}

static int AV_StartRecord(struct av7110 *av7110, int av,
               struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
  
	DEB_EE(("av7110: %p, dvb_demux_feed:%p\n",av7110,dvbdmxfeed));

        if (av7110->playing||(av7110->rec_mode&av))
                return -EBUSY;
        outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);
        dvbdmx->recording=1;
        av7110->rec_mode|=av;

        switch (av7110->rec_mode) {
        case RP_AUDIO:
                dvb_filter_pes2ts_init (&av7110->p2t[0],
					dvbdmx->pesfilter[0]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[0]);
                outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
                break;

	case RP_VIDEO:
                dvb_filter_pes2ts_init (&av7110->p2t[1],
					dvbdmx->pesfilter[1]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[1]);
                outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
                break;

	case RP_AV:
                dvb_filter_pes2ts_init (&av7110->p2t[0],
					dvbdmx->pesfilter[0]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[0]);
                dvb_filter_pes2ts_init (&av7110->p2t[1],
					dvbdmx->pesfilter[1]->pid,
					dvb_filter_pes2ts_cb,
					(void *)dvbdmx->pesfilter[1]);
                outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, AV_PES, 0);
                break;
        }
        return 0;
}

static int AV_StartPlay(struct av7110 *av7110, int av)
{
	DEB_EE(("av7110: %p\n",av7110));
	
        if (av7110->rec_mode)
                return -EBUSY;
        if (av7110->playing&av)
                return -EBUSY;

        outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);

        if (av7110->playing == RP_NONE) {
                av7110_ipack_reset(&av7110->ipack[0]);
                av7110_ipack_reset(&av7110->ipack[1]);
        }

        av7110->playing|=av;
        switch (av7110->playing) {
        case RP_AUDIO:
                outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
                break;
        case RP_VIDEO:
                outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
                av7110->sinfo=0;
                break;
        case RP_AV:
                av7110->sinfo=0;
                outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, AV_PES, 0);
                break;
        }
        return av7110->playing;
}

static void AV_Stop(struct av7110 *av7110, int av)
{
	DEB_EE(("av7110: %p\n",av7110));

	if (!(av7110->playing&av) && !(av7110->rec_mode&av))
                return;

        outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);
        if (av7110->playing) {
                av7110->playing&=~av;
                switch (av7110->playing) {
                case RP_AUDIO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
                        break;
                case RP_VIDEO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
                        break;
                case RP_NONE:
                        SetMode(av7110, av7110->vidmode);
                        break;
                }
        } else {
                av7110->rec_mode&=~av;
                switch (av7110->rec_mode) {
                case RP_AUDIO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
                        break;
                case RP_VIDEO:
                        outcom(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
                        break;
                case RP_NONE:
                        break;
                }
        }
}

/**
 *  Hack! we save the last av7110 ptr. This should be ok, since
 *  you rarely will use more then one IR control. 
 *
 *  If we want to support multiple controls we would have to do much more...
 */
void av7110_setup_irc_config (struct av7110 *av7110, u32 ir_config)
{
	static struct av7110 *last;

	DEB_EE(("av7110: %p\n",av7110));

	if (!av7110)
		av7110 = last;
	else
		last = av7110;

	if (av7110) {
		outcom(av7110, COMTYPE_PIDFILTER, SetIR, 1, ir_config);
		av7110->ir_config = ir_config;
	}
}

static void (*irc_handler)(u32);

void av7110_register_irc_handler(void (*func)(u32)) 
{
        //DEB_EE(("registering %08x\n",func));
        irc_handler = func;
}

void av7110_unregister_irc_handler(void (*func)(u32)) 
{
        //DEB_EE(("unregistering %08x\n",func));
        irc_handler = NULL;
}

void run_handlers(unsigned long ircom) 
{
        if (irc_handler != NULL)
                (*irc_handler)((u32) ircom);
}

DECLARE_TASKLET(irtask,run_handlers,0);

void IR_handle(struct av7110 *av7110, u32 ircom)
{
	DEB_S(("av7110: ircommand = %08x\n", ircom));
        irtask.data = (unsigned long) ircom;
        tasklet_schedule(&irtask);
}

/****************************************************************************
 * IRQ handling
 ****************************************************************************/

void CI_handle(struct av7110 *av7110, u8 *data, u16 len) 
{
        //CI_out(av7110, data, len);

	DEB_EE(("av7110: %p\n",av7110));

        if (len<3)
                return;
        switch (data[0]) {
        case CI_MSG_CI_INFO:
                if (data[2]!=1 && data[2]!=2)
                        break;
                switch (data[1]) {
                case 0:
                        av7110->ci_slot[data[2]-1].flags=0;
                        break;
                case 1:
                        av7110->ci_slot[data[2]-1].flags|=CA_CI_MODULE_PRESENT;
                        break;
                case 2:
                        av7110->ci_slot[data[2]-1].flags|=CA_CI_MODULE_READY;
                        break;
                }
                break;
        case CI_SWITCH_PRG_REPLY:
                //av7110->ci_stat=data[1];
                break;
        default:
                break;
        }

}

static inline int DvbDmxFilterCallback(u8 * buffer1, size_t buffer1_len,
                     u8 * buffer2, size_t buffer2_len,
                     struct dvb_demux_filter *dvbdmxfilter,
                     enum dmx_success success,
                     struct av7110 *av7110)
{
	DEB_INT(("av7110: %p\n",av7110));

        if (!dvbdmxfilter->feed->demux->dmx.frontend)
                return 0;
        if (dvbdmxfilter->feed->demux->dmx.frontend->source==DMX_MEMORY_FE)
                return 0;
        
        switch(dvbdmxfilter->type) {
        case DMX_TYPE_SEC:
                if ((((buffer1[1]<<8)|buffer1[2])&0xfff)+3!=buffer1_len)
                        return 0;
                if (dvbdmxfilter->doneq) {
                        struct dmx_section_filter *filter=&dvbdmxfilter->filter;
                        int i;
                        u8 xor, neq=0;
                        
                        for (i=0; i<DVB_DEMUX_MASK_MAX; i++) {
                                xor=filter->filter_value[i]^buffer1[i];
                                neq|=dvbdmxfilter->maskandnotmode[i]&xor;
                        }
                        if (!neq)
                                return 0;
                }
                return dvbdmxfilter->feed->cb.sec(buffer1, buffer1_len,
						  buffer2, buffer2_len,
						  &dvbdmxfilter->filter,
						  DMX_OK); 
        case DMX_TYPE_TS:
                if (!(dvbdmxfilter->feed->ts_type & TS_PACKET)) 
                        return 0;
                if (dvbdmxfilter->feed->ts_type & TS_PAYLOAD_ONLY) 
                        return dvbdmxfilter->feed->cb.ts(buffer1, buffer1_len,
                                                         buffer2, buffer2_len,
                                                         &dvbdmxfilter->feed->feed.ts,
                                                         DMX_OK); 
                else
                        pes_to_ts(buffer1, buffer1_len, 
                                  dvbdmxfilter->feed->pid, 
                                  &av7110->p2t_filter[dvbdmxfilter->index]);
	default:
	        return 0;
        }
}


u8 pshead[0x26] = {
        0x00, 0x00, 0x01, 0xba, 0x5f, 0xff, 0xfe, 0xe6, 
        0xc4, 0x01, 0x01, 0x89, 0xc3, 0xf8, 0x00, 0x00,
        0x01, 0xbb, 0x00, 0x12, 0x80, 0xc4, 0xe1, 0x00,
        0xe1, 0xff, 0xb9, 0xe0, 0xe8, 0xb8, 0xc0, 0x20,
        0xbd, 0xe0, 0x44, 0xbf, 0xe0, 0x02,
};								


//#define DEBUG_TIMING
static inline void print_time(char *s)
{
#ifdef DEBUG_TIMING
        struct timeval tv;
        do_gettimeofday(&tv);
        printk("%s: %d.%d\n", s, (int)tv.tv_sec, (int)tv.tv_usec);
#endif
}

static void ci_get_data(struct dvb_ringbuffer *cibuf, u8 *data, int len)
{
        if (dvb_ringbuffer_free(cibuf) < len+2)
                return;

        DVB_RINGBUFFER_WRITE_BYTE(cibuf,len>>8);
        DVB_RINGBUFFER_WRITE_BYTE(cibuf,len&0xff);   

        dvb_ringbuffer_write(cibuf,data,len,0);

        wake_up_interruptible(&cibuf->queue);
}

static void debiirq (unsigned long data)
{
	struct av7110 *av7110 = (struct av7110*) data;
        int type=av7110->debitype;
        int handle=(type>>8)&0x1f;
	
//	DEB_EE(("av7110: %p\n",av7110));

        print_time("debi");
        saa7146_write(av7110->dev, IER, 
                      saa7146_read(av7110->dev, IER) & ~MASK_19 );
        saa7146_write(av7110->dev, ISR, MASK_19 );

        if (type==-1) {
                printk("DEBI irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",jiffies,saa7146_read(av7110->dev,PSR),saa7146_read(av7110->dev,SSR));
		spin_lock(&av7110->debilock);
                ARM_ClearMailBox(av7110);
                ARM_ClearIrq(av7110);
		spin_unlock(&av7110->debilock);
                return;
        }
        av7110->debitype=-1;

        switch (type&0xff) {

        case DATA_TS_RECORD:
                dvb_dmx_swfilter_packets(&av7110->demux, 
                                      (const u8 *)av7110->debi_virt, 
                                      av7110->debilen/188);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_PES_RECORD:
                if (av7110->demux.recording) 
                        record_cb(&av7110->p2t[handle], 
                                  (u8 *)av7110->debi_virt,
                                  av7110->debilen);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_IPMPE:
        case DATA_FSECTION:
        case DATA_PIPING:
                if (av7110->handle2filter[handle]) 
                        DvbDmxFilterCallback((u8 *)av7110->debi_virt, 
                                             av7110->debilen, 0, 0, 
                                             av7110->handle2filter[handle], 
                                             DMX_OK, av7110); 
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_CI_GET:
        {
                u8 *data=av7110->debi_virt;

                if ((data[0]<2) && data[2]==0xff) {
                        int flags=0;
                        if (data[5]>0) 
                                flags|=CA_CI_MODULE_PRESENT;
                        if (data[5]>5) 
                                flags|=CA_CI_MODULE_READY;
                        av7110->ci_slot[data[0]].flags=flags;
                } else
                        ci_get_data(&av7110->ci_rbuffer, 
                                    av7110->debi_virt, 
                                    av7110->debilen);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;
        }

        case DATA_COMMON_INTERFACE:
                CI_handle(av7110, (u8 *)av7110->debi_virt, av7110->debilen);
#if 0
        {
                int i;

                printk("av7110%d: ", av7110->num);
                printk("%02x ", *(u8 *)av7110->debi_virt);
                printk("%02x ", *(1+(u8 *)av7110->debi_virt));
                for (i=2; i<av7110->debilen; i++)
                  printk("%02x ", (*(i+(unsigned char *)av7110->debi_virt)));
                for (i=2; i<av7110->debilen; i++)
                  printk("%c", chtrans(*(i+(unsigned char *)av7110->debi_virt)));

                printk("\n");
        }
#endif
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_DEBUG_MESSAGE:
                ((s8*)av7110->debi_virt)[Reserved_SIZE-1]=0;
                printk("%s\n", (s8 *)av7110->debi_virt);
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_CI_PUT:
        case DATA_MPEG_PLAY:
        case DATA_BMP_LOAD:
                spin_lock(&av7110->debilock);
                iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                ARM_ClearMailBox(av7110);
                spin_unlock(&av7110->debilock);
                return;
        default:
                break;
        }
        spin_lock(&av7110->debilock);
        ARM_ClearMailBox(av7110);
        spin_unlock(&av7110->debilock);
}

static int pes_play(void *dest, struct dvb_ringbuffer *buf, int dlen)
{
        int len;
        u32 sync;
        u16 blen;

	DEB_EE(("dvb_ring_buffer_t: %p\n",buf));

        if (!dlen) {
                wake_up(&buf->queue);
                return -1;
        }
        while (1) {
                if ((len=dvb_ringbuffer_avail(buf)) < 6)
                        return -1;
                sync= DVB_RINGBUFFER_PEEK(buf,0)<<24;
                sync|=DVB_RINGBUFFER_PEEK(buf,1)<<16;
                sync|=DVB_RINGBUFFER_PEEK(buf,2)<<8;
                sync|=DVB_RINGBUFFER_PEEK(buf,3);
                
                if (((sync&~0x0f)==0x000001e0) ||
                    ((sync&~0x1f)==0x000001c0) ||
                    (sync==0x000001bd))
                        break;
                printk("resync\n");
                DVB_RINGBUFFER_SKIP(buf,1);
        }
        blen= DVB_RINGBUFFER_PEEK(buf,4)<<8;
        blen|=DVB_RINGBUFFER_PEEK(buf,5);
        blen+=6;
        if (len<blen || blen>dlen) {
                //printk("buffer empty - avail %d blen %u dlen %d\n",len,blen,dlen);
                wake_up(&buf->queue);
                return -1;
        }

        (void)dvb_ringbuffer_read(buf,dest,(size_t)blen,0);

        DEB_S(("pread=0x%08lx, pwrite=0x%08lx\n",(unsigned long)buf->pread, (unsigned long)buf->pwrite));
        wake_up(&buf->queue);
        return blen;
}


static void gpioirq (unsigned long data)
{
	struct av7110 *av7110 = (struct av7110*) data;
        u32 rxbuf, txbuf;
        int len;
        
        //printk("GPIO0 irq\n");        

        if (av7110->debitype !=-1)
                printk("GPIO0 irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",jiffies,saa7146_read(av7110->dev,PSR),saa7146_read(av7110->dev,SSR));
       
        spin_lock(&av7110->debilock);

	ARM_ClearIrq(av7110);

        saa7146_write(av7110->dev, IER, 
                      saa7146_read(av7110->dev, IER) & ~MASK_19 );
        saa7146_write(av7110->dev, ISR, MASK_19 );

        av7110->debitype = irdebi(av7110, DEBINOSWAP, IRQ_STATE, 0, 2);
        av7110->debilen  = irdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
        av7110->debibuf  = 0;
        rxbuf=irdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
        txbuf=irdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
        len=(av7110->debilen+3)&(~3);

        DEB_D(("GPIO0 irq %d %d\n", av7110->debitype, av7110->debilen));
        print_time("gpio");

        DEB_D(("GPIO0 irq %02x\n", av7110->debitype&0xff));        
        switch (av7110->debitype&0xff) {

        case DATA_TS_PLAY:
        case DATA_PES_PLAY:
                break;

	case DATA_MPEG_VIDEO_EVENT:
	{
		u32 h_ar;
		struct video_event event;

                av7110->video_size.w = irdebi(av7110, DEBINOSWAP, STATUS_MPEG_WIDTH, 0, 2);
                h_ar = irdebi(av7110, DEBINOSWAP, STATUS_MPEG_HEIGHT_AR, 0, 2);

                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);

		av7110->video_size.h = h_ar & 0xfff;
		DEB_D(("GPIO0 irq: DATA_MPEG_VIDEO_EVENT: w/h/ar = %u/%u/%u\n",
				av7110->video_size.w,
				av7110->video_size.h,
				av7110->video_size.aspect_ratio));

		event.type = VIDEO_EVENT_SIZE_CHANGED;
		event.u.size.w = av7110->video_size.w;
		event.u.size.h = av7110->video_size.h;
		switch ((h_ar >> 12) & 0xf)
		{
		case 3:
			av7110->video_size.aspect_ratio = VIDEO_FORMAT_16_9;
			event.u.size.aspect_ratio = VIDEO_FORMAT_16_9;
			av7110->videostate.video_format = VIDEO_FORMAT_16_9;
			break;
		case 4:
			av7110->video_size.aspect_ratio = VIDEO_FORMAT_221_1;
			event.u.size.aspect_ratio = VIDEO_FORMAT_221_1;
			av7110->videostate.video_format = VIDEO_FORMAT_221_1;
			break;
		default:
			av7110->video_size.aspect_ratio = VIDEO_FORMAT_4_3;
			event.u.size.aspect_ratio = VIDEO_FORMAT_4_3;
			av7110->videostate.video_format = VIDEO_FORMAT_4_3;
		}
		dvb_video_add_event(av7110, &event);
		break;
	}

        case DATA_CI_PUT:
        {
                int avail;
                struct dvb_ringbuffer *cibuf=&av7110->ci_wbuffer;

                avail=dvb_ringbuffer_avail(cibuf);
                if (avail<=2) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                } 
                len= DVB_RINGBUFFER_PEEK(cibuf,0)<<8;
                len|=DVB_RINGBUFFER_PEEK(cibuf,1);
                if (avail<len+2) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                } 
                DVB_RINGBUFFER_SKIP(cibuf,2); 

                dvb_ringbuffer_read(cibuf,av7110->debi_virt,len,0);

                wake_up(&cibuf->queue);
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
                wait_for_debi_done(av7110);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19 );
                if (len<5) len=5; /* we want a real DEBI DMA */
                iwdebi(av7110, DEBISWAB, DPRAM_BASE+txbuf, 0, (len+3)&~3);
                spin_unlock(&av7110->debilock);
                return;
        }

        case DATA_MPEG_PLAY:
                if (!av7110->playing) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                }
                len=0;
                if (av7110->debitype&0x100) {
                        spin_lock(&av7110->aout.lock);
                        len=pes_play(av7110->debi_virt, &av7110->aout, 2048);
                        spin_unlock(&av7110->aout.lock);
                }
                if (len<=0 && (av7110->debitype&0x200)
                        &&av7110->videostate.play_state!=VIDEO_FREEZED) {
                        spin_lock(&av7110->avout.lock);
                        len=pes_play(av7110->debi_virt, &av7110->avout, 2048);
                        spin_unlock(&av7110->avout.lock);
                }
                if (len<=0) {
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        break;
                } 
                DEB_D(("GPIO0 PES_PLAY len=%04x\n", len));        
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
                wait_for_debi_done(av7110);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19 );

                iwdebi(av7110, DEBISWAB, DPRAM_BASE+txbuf, 0, (len+3)&~3);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_BMP_LOAD:
                len=av7110->debilen;
                if (!len) {
                        av7110->bmp_state=BMP_LOADED;
                        iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_LEN, 0, 2);
                        iwdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
                        wake_up(&av7110->bmpq);
                        break;
                }
                if (len>av7110->bmplen)
                        len=av7110->bmplen;
                if (len>2*1024)
                        len=2*1024;
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
                memcpy(av7110->debi_virt, av7110->bmpbuf+av7110->bmpp, len);
                av7110->bmpp+=len;
                av7110->bmplen-=len;
                wait_for_debi_done(av7110);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19 );
                if (len<5) len=5; /* we want a real DEBI DMA */
                iwdebi(av7110, DEBISWAB, DPRAM_BASE+txbuf, 0, (len+3)&~3);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_CI_GET:
        case DATA_COMMON_INTERFACE:
        case DATA_FSECTION:
        case DATA_IPMPE:
        case DATA_PIPING:
                if (!len || len>4*1024) {
                        iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                        break;
                }                  /* yes, fall through */
        case DATA_TS_RECORD:
        case DATA_PES_RECORD:
                wait_for_debi_done(av7110);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19);
                irdebi(av7110, DEBISWAB, DPRAM_BASE+rxbuf, 0, len);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_DEBUG_MESSAGE:
                wait_for_debi_done(av7110);
                if (!len || len>0xff) {
                        iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                        break;
                }
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19);
                irdebi(av7110, DEBISWAB, Reserved, 0, len);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_IRCOMMAND: 
                IR_handle(av7110, 
                          swahw32(irdebi(av7110, DEBINOSWAP, Reserved, 0, 4)));
                iwdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
                break;

        default:
                printk("gpioirq unknown type=%d len=%d\n", 
                       av7110->debitype, av7110->debilen);
                break;
        }      
        ARM_ClearMailBox(av7110);
        av7110->debitype=-1;
        spin_unlock(&av7110->debilock);
}


/****************************************************************************
 * DEBI command polling 
 ****************************************************************************/


static int OutCommand(struct av7110 *av7110, u16* buf, int length)
{
        int i;
        u32 start;
#ifdef COM_DEBUG
        u32 stat;
#endif

//	DEB_EE(("av7110: %p\n",av7110));

	if (!av7110->arm_ready) {
		DEB_D(("arm not ready.\n"));
		return -1;
	}

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2 ) )
        {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_FREE) {
			printk(KERN_ERR "%s: timeout waiting for COMMAND idle\n", __FUNCTION__);
                        return -1;
                }
        }

#ifndef _NOHANDSHAKE
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 ) )
        {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_SHAKE) {
			printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n", __FUNCTION__);
                        return -1;
                }
        }
#endif

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2) & OSDQFull )
        {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_OSD) {
			printk(KERN_ERR "%s: timeout waiting for !OSDQFull\n", __FUNCTION__);
			return -1;
                }
        }
        for (i=2; i<length; i++)
                wdebi(av7110, DEBINOSWAP, COMMAND + 2*i, (u32) buf[i], 2);

        if (length)
                wdebi(av7110, DEBINOSWAP, COMMAND + 2, (u32) buf[1], 2);
        else
                wdebi(av7110, DEBINOSWAP, COMMAND + 2, 0, 2);

        wdebi(av7110, DEBINOSWAP, COMMAND, (u32) buf[0], 2);

#ifdef COM_DEBUG
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2 ) )
        {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_FREE) {
                        printk(KERN_ERR "%s: timeout waiting for COMMAND to complete\n", __FUNCTION__);
                        return -1;
                }
        }

	stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
	if (stat & GPMQOver) {
		printk(KERN_ERR "%s: GPMQOver\n", __FUNCTION__);
		return -1;
	}
	else if (stat & OSDQOver) {
		printk(KERN_ERR "%s: OSDQOver\n", __FUNCTION__);
		return -1;
	}
#endif

        return 0;
}

static inline int SOutCommand(struct av7110 *av7110, u16* buf, int length)
{
        int ret;
        
// 	DEB_EE(("av7110: %p\n",av7110));

        if (!av7110->arm_ready) {
		DEB_D(("arm not ready.\n"));
		return -1;
	}
	
        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

        ret=OutCommand(av7110, buf, length);
        up(&av7110->dcomlock);
	if (ret)
                printk("SOutCommand error\n");
        return ret;
}


static int outcom(struct av7110 *av7110, int type, int com, int num, ...)
{
	va_list args;
        u16 buf[num+2];
        int i, ret;

// 	DEB_EE(("av7110: %p\n",av7110));

        buf[0]=(( type << 8 ) | com);
        buf[1]=num;

        if (num) {
                va_start(args, num);
                for (i=0; i<num; i++)
                        buf[i+2]=va_arg(args, u32);
                va_end(args);
        }

        ret = SOutCommand(av7110, buf, num+2);
	if (ret)
                printk("outcom error\n");
	return ret;
}

int SendCICommand(struct av7110 *av7110, u8 subcom, u8 *Params, u8 ParamLen)
{
        int i, ret;
        u16 CommandBuffer[18] = { ((COMTYPE_COMMON_IF << 8) + subcom),
                                  16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        
 	DEB_EE(("av7110: %p\n",av7110));

	for(i=0; (i<ParamLen)&&(i<32); i++)	
	{
		if(i%2 == 0)
			CommandBuffer[(i/2)+2] = (u16)(Params[i]) << 8;
		else
			CommandBuffer[(i/2)+2] |= Params[i];
	}

        ret = SOutCommand(av7110, CommandBuffer, 18);
	if (ret)
                printk("SendCICommand error\n");
	return ret;
}


static int CommandRequest(struct av7110 *av7110, u16 *Buff, int length, u16 *buf, int n)
{
	int err;
        s16 i;
        u32 start;
#ifdef COM_DEBUG
        u32 stat;
#endif

	DEB_EE(("av7110: %p\n",av7110));

        if (!av7110->arm_ready) {
		DEB_D(("arm not ready.\n"));
		return -1;
	}

        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

        if ((err = OutCommand(av7110, Buff, length)) < 0) {
		up(&av7110->dcomlock);
		printk("CommandRequest error\n");
		return err;
	}

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2) )
        {
#ifdef _NOHANDSHAKE
                dvb_delay(1);
#endif
                if ((jiffies - start) > ARM_WAIT_FREE) {
			printk("%s: timeout waiting for COMMAND to complete\n", __FUNCTION__);
                        up(&av7110->dcomlock);
                        return -1;
                }
        }

#ifndef _NOHANDSHAKE
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 ) ) {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_SHAKE) {
			printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n", __FUNCTION__);
                        up(&av7110->dcomlock);
                        return -1;
                }
        }
#endif

#ifdef COM_DEBUG
	stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
	if (stat & GPMQOver) {
		printk(KERN_ERR "%s: GPMQOver\n", __FUNCTION__);
                up(&av7110->dcomlock);
		return -1;
	}
	else if (stat & OSDQOver) {
		printk(KERN_ERR "%s: OSDQOver\n", __FUNCTION__);
                up(&av7110->dcomlock);
		return -1;
	}
#endif

        for (i=0; i<n; i++)
                buf[i] = rdebi(av7110, DEBINOSWAP, COM_BUFF + 2*i, 0, 2);

	up(&av7110->dcomlock);
        return 0;
}


static inline int  RequestParameter(struct av7110 *av7110, u16 tag, u16* Buff, s16 length)
{
	int ret;
        ret = CommandRequest(av7110, &tag, 0, Buff, length);
	if (ret)
		printk("RequestParameter error\n");
	return ret;
}


/****************************************************************************
 * Firmware commands 
 ****************************************************************************/

/* msp3400 i2c subaddresses */
#define MSP_WR_DEM 0x10
#define MSP_RD_DEM 0x11
#define MSP_WR_DSP 0x12
#define MSP_RD_DSP 0x13

static inline int msp_writereg(struct av7110 *av7110, u8 dev, u16 reg, u16 val)
{
        u8 msg[5]={ dev, reg>>8, reg&0xff, val>>8 , val&0xff }; 
        struct dvb_i2c_bus *i2c = av7110->i2c_bus;
        struct i2c_msg msgs = { .flags = 0, .addr = 0x40, .len = 5, .buf = msg};

        if (i2c->xfer(i2c, &msgs, 1) != 1) {
		printk("av7110(%d): %s(%u = %u) failed\n",
				av7110->dvb_adapter->num, __FUNCTION__, reg, val);
		return -EIO;
	}
	return 0;
}

static inline int msp_readreg(struct av7110 *av7110, u8 dev, u16 reg, u16 *val)
{
        u8 msg1[3]={ dev, reg>>8, reg&0xff };
        u8 msg2[2];
        struct dvb_i2c_bus *i2c = av7110->i2c_bus;
        struct i2c_msg msgs[2] = {
		{ .flags = 0,        .addr = 0x40, .len = 3, .buf = msg1},
		{ .flags = I2C_M_RD, .addr = 0x40, .len = 2, .buf = msg2}
	};

        if (i2c->xfer(i2c, msgs, 2) != 2) {
		printk("av7110(%d): %s(%u) failed\n",
				av7110->dvb_adapter->num, __FUNCTION__, reg);
		return -EIO;
	}
	*val = (msg2[0] << 8) | msg2[1];
	return 0;
}

static inline int SendDAC(struct av7110 *av7110, u8 addr, u8 data)
{
// 	DEB_EE(("av7110: %p\n",av7110));

        return outcom(av7110, COMTYPE_AUDIODAC, AudioDAC, 2, addr, data);
}

static int SetVolume(struct av7110 *av7110, int volleft, int volright)
{
        int err, vol, val, balance = 0;
        
 	DEB_EE(("av7110: %p\n",av7110));

        switch (av7110->adac_type) {
        case DVB_ADAC_TI:
                volleft = (volleft * 256) / 1036;
                volright = (volright * 256) / 1036;
                if (volleft > 0x3f)
                        volleft = 0x3f;
                if (volright > 0x3f)
                        volright = 0x3f;
                if ((err = SendDAC(av7110, 3, 0x80 + volleft)))
                        return err;
                return SendDAC(av7110, 4, volright);
                
        case DVB_ADAC_CRYSTAL:
                volleft=127-volleft/2;
                volright=127-volright/2;
                i2c_writereg(av7110, 0x20, 0x03, volleft);
                i2c_writereg(av7110, 0x20, 0x04, volright);
                return 0;

        case DVB_ADAC_MSP:
                vol  = (volleft > volright) ? volleft : volright;
		val     = (vol * 0x73 / 255) << 8;
		if (vol > 0) {
		       balance = ((volright-volleft) * 127) / vol;
		}
		msp_writereg(av7110, MSP_WR_DSP, 0x0001, balance << 8);
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, val); /* loudspeaker */
		msp_writereg(av7110, MSP_WR_DSP, 0x0006, val); /* headphonesr */
		return 0;
        }
        return 0;
}

#ifdef CONFIG_DVB_AV7110_OSD

static inline int ResetBlend(struct av7110 *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, SetNonBlend, 1, windownr);
}

static inline int SetColorBlend(struct av7110 *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, SetCBlend, 1, windownr); 
}

static inline int SetWindowBlend(struct av7110 *av7110, u8 windownr, u8 blending)
{
        return outcom(av7110, COMTYPE_OSD, SetWBlend, 2, windownr, blending); 
}

static inline int SetBlend_(struct av7110 *av7110, u8 windownr,
                     enum av7110_osd_palette_type colordepth, u16 index, u8 blending)
{
        return outcom(av7110, COMTYPE_OSD, SetBlend, 4,
                      windownr, colordepth, index, blending);
} 

static inline int SetColor_(struct av7110 *av7110, u8 windownr,
                     enum av7110_osd_palette_type colordepth, u16 index, u16 colorhi, u16 colorlo)
{
        return outcom(av7110, COMTYPE_OSD, SetColor, 5,
                      windownr, colordepth, index, colorhi, colorlo);
} 

static inline int BringToTop(struct av7110 *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, WTop, 1, windownr);
} 

static inline int SetFont(struct av7110 *av7110, u8 windownr, u8 fontsize,
                   u16 colorfg, u16 colorbg)
{
        return outcom(av7110, COMTYPE_OSD, Set_Font, 4,
                      windownr, fontsize, colorfg, colorbg);
} 

static int FlushText(struct av7110 *av7110)
{
        u32 start;

        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2 ) ) {
                dvb_delay(1); 
                if ((jiffies - start) > ARM_WAIT_OSD) {
                        printk(KERN_ERR "%s: timeout waiting for BUFF1_BASE == 0\n", __FUNCTION__);
                        up(&av7110->dcomlock);
                        return -1;
                }
        }
        up(&av7110->dcomlock);
        return 0;
}

static int WriteText(struct av7110 *av7110, u8 win, u16 x, u16 y, u8* buf)
{
        int i, ret;
        u32 start;
        int length=strlen(buf)+1;
        u16 cbuf[5] = { (COMTYPE_OSD<<8) + DText, 3, win, x, y };
        
        if (down_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2 ) ) {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_OSD) {
                        printk(KERN_ERR "%s: timeout waiting for BUFF1_BASE == 0\n", __FUNCTION__);
                        up(&av7110->dcomlock);
                        return -1;
                }
        }
#ifndef _NOHANDSHAKE
        start = jiffies;
        while ( rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2 ) ) {
                dvb_delay(1);
                if ((jiffies - start) > ARM_WAIT_SHAKE) {
                        printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n", __FUNCTION__);
                        up(&av7110->dcomlock);
                        return -1;
                }
        }
#endif
        for (i=0; i<length/2; i++)
                wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i*2, 
                      swab16(*(u16 *)(buf+2*i)), 2);
        if (length&1)
                wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i*2, 0, 2);
        ret=OutCommand(av7110, cbuf, 5);
        up(&av7110->dcomlock);
	if (ret)
		printk("WriteText error\n");
        return ret;
}

static inline int DrawLine(struct av7110 *av7110, u8 windownr, 
                    u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
        return outcom(av7110, COMTYPE_OSD, DLine, 6,
                      windownr, x, y, dx, dy, color);
} 

static inline int DrawBlock(struct av7110 *av7110, u8 windownr, 
                    u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
        return outcom(av7110, COMTYPE_OSD, DBox, 6,
                      windownr, x, y, dx, dy, color);
} 

static inline int HideWindow(struct av7110 *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, WHide, 1, windownr);
} 

static inline int MoveWindowRel(struct av7110 *av7110, u8 windownr, u16 x, u16 y)
{
        return outcom(av7110, COMTYPE_OSD, WMoveD, 3, windownr, x, y);
} 

static inline int MoveWindowAbs(struct av7110 *av7110, u8 windownr, u16 x, u16 y)
{
        return outcom(av7110, COMTYPE_OSD, WMoveA, 3, windownr, x, y);
} 

static inline int DestroyOSDWindow(struct av7110 *av7110, u8 windownr)
{
        return outcom(av7110, COMTYPE_OSD, WDestroy, 1, windownr);
} 

#if 0
static void DestroyOSDWindows(struct av7110 *av7110)
{
        int i;

        for (i=1; i<7; i++)
                outcom(av7110, COMTYPE_OSD, WDestroy, 1, i);
} 
#endif

static inline int CreateOSDWindow(struct av7110 *av7110, u8 windownr,
                           enum av7110_window_display_type disptype, u16 width, u16 height)
{
        return outcom(av7110, COMTYPE_OSD, WCreate, 4,
                      windownr, disptype, width, height);
} 


static enum av7110_osd_palette_type bpp2pal[8]={Pal1Bit, Pal2Bit, 0, Pal4Bit, 0, 0, 0, Pal8Bit}; 
static enum av7110_window_display_type   bpp2bit[8]={BITMAP1, BITMAP2, 0, BITMAP4, 0, 0, 0, BITMAP8}; 

static inline int LoadBitmap(struct av7110 *av7110, u16 format, u16 dx, u16 dy, int inc, u8* data)
{
        int bpp;
        int i;
        int d, delta; 
        u8 c;
        DECLARE_WAITQUEUE(wait, current);
        
 	DEB_EE(("av7110: %p\n",av7110));

        if (av7110->bmp_state==BMP_LOADING) {
                add_wait_queue(&av7110->bmpq, &wait);
                while (1) {
                        set_current_state(TASK_INTERRUPTIBLE);
                        if (av7110->bmp_state!=BMP_LOADING
                            || signal_pending(current))
                                break;
                        schedule();
                }
                set_current_state(TASK_RUNNING);
                remove_wait_queue(&av7110->bmpq, &wait);
        }
        if (av7110->bmp_state==BMP_LOADING)
                return -1;
        av7110->bmp_state=BMP_LOADING;
        if      (format==BITMAP8) { bpp=8; delta = 1; } 
        else if (format==BITMAP4) { bpp=4; delta = 2; }
        else if (format==BITMAP2) { bpp=2; delta = 4; }
        else if (format==BITMAP1) { bpp=1; delta = 8; }
        else {
                av7110->bmp_state=BMP_NONE;
                return -1;
        }
        av7110->bmplen= ((dx*dy*bpp+7)&~7)/8; 
        av7110->bmpp=0;
        if (av7110->bmplen>32768) {
                av7110->bmp_state=BMP_NONE;
                return -1;
        }
        for (i=0; i<dy; i++) {
                if (copy_from_user(av7110->bmpbuf+1024+i*dx, data+i*inc, dx)) { 
                        av7110->bmp_state=BMP_NONE;
                        return -1;
                }
        }
        if (format != BITMAP8) {
                for (i=0; i<dx*dy/delta; i++) {
                        c = ((u8 *)av7110->bmpbuf)[1024+i*delta+delta-1];
                        for (d=delta-2; d>=0; d--) {
                                c |= (((u8 *)av7110->bmpbuf)[1024+i*delta+d] 
                                      << ((delta-d-1)*bpp));
                                ((u8 *)av7110->bmpbuf)[1024+i] = c;
                        }
                }
        }
        av7110->bmplen+=1024;
        return outcom(av7110, COMTYPE_OSD, LoadBmp, 3, format, dx, dy);
} 

static int BlitBitmap(struct av7110 *av7110, u16 win, u16 x, u16 y, u16 trans)
{
        DECLARE_WAITQUEUE(wait, current);
        
  	DEB_EE(("av7110: %p\n",av7110));

       if (av7110->bmp_state==BMP_NONE)
                return -1;
        if (av7110->bmp_state==BMP_LOADING) {
                add_wait_queue(&av7110->bmpq, &wait);
                while (1) {
                        set_current_state(TASK_INTERRUPTIBLE);
                        if (av7110->bmp_state!=BMP_LOADING
                            || signal_pending(current))
                                break;
                        schedule();
                }
                set_current_state(TASK_RUNNING);
                remove_wait_queue(&av7110->bmpq, &wait);
        }
        if (av7110->bmp_state==BMP_LOADED)
                return outcom(av7110, COMTYPE_OSD, BlitBmp, 4, win, x, y, trans);
        return -1;
} 

static inline int  ReleaseBitmap(struct av7110 *av7110)
{
 	DEB_EE(("av7110: %p\n",av7110));

        if (av7110->bmp_state!=BMP_LOADED)
                return -1;
        av7110->bmp_state=BMP_NONE;
        return outcom(av7110, COMTYPE_OSD, ReleaseBmp, 0);
} 

static u32 RGB2YUV(u16 R, u16 G, u16 B)
{
        u16 y, u, v;
        u16 Y, Cr, Cb;

        y = R * 77 + G * 150 + B * 29;  // Luma=0.299R+0.587G+0.114B 0..65535
        u = 2048+B * 8 -(y>>5);    // Cr 0..4095
        v = 2048+R * 8 -(y>>5);    // Cb 0..4095

        Y=y/256;
        Cb=u/16;
        Cr=v/16;

        return Cr|(Cb<<16)|(Y<<8);
}

static void OSDSetColor(struct av7110 *av7110, u8 color, u8 r, u8 g, u8 b, u8 blend)
{
        u16 ch, cl;
        u32 yuv;

        yuv=blend ? RGB2YUV(r,g,b) : 0;
        cl=(yuv&0xffff);
        ch=((yuv>>16)&0xffff);
        SetColor_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
                  color, ch, cl);
        SetBlend_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
                  color, ((blend>>4)&0x0f));
}

static int OSDSetPalette(struct av7110 *av7110, u32 *colors, u8 first, u8 last)
{
       int i;
       int length = last - first + 1;

       if (length * 4 > DATA_BUFF3_SIZE)
               return -1;

       for (i=0; i<length; i++) {
               u32 blend = (colors[i] & 0xF0000000) >> 4;
               u32 yuv = blend ? RGB2YUV(colors[i] & 0xFF, (colors[i] >> 8) & 0xFF, (colors[i] >> 16) & 0xFF) | blend : 0;
               yuv = ((yuv & 0xFFFF0000) >> 16) | ((yuv & 0x0000FFFF) << 16); // TODO kls2003-06-15: not sure if this is endian-proof
               wdebi(av7110, DEBINOSWAP, DATA_BUFF3_BASE + i*4, yuv, 4);
       }
       return outcom(av7110, COMTYPE_OSD, Set_Palette, 4,
               av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]], first, last);
}

static int OSDSetBlock(struct av7110 *av7110, int x0, int y0, int x1, int y1, int inc, u8 *data)
{
        uint w, h, bpp, bpl, size, lpb, bnum, brest;
        int i;

        w=x1-x0+1; h=y1-y0+1;
        if (inc<=0)
                inc=w; 
        if (w<=0 || w>720 || h<=0 || h>576) 
                return -1;
        bpp=av7110->osdbpp[av7110->osdwin]+1; 
        bpl=((w*bpp+7)&~7)/8; 
        size=h*bpl;
        lpb=(32*1024)/bpl; 
        bnum=size/(lpb*bpl);
        brest=size-bnum*lpb*bpl;

        for (i=0; i<bnum; i++) {
                LoadBitmap(av7110, bpp2bit[av7110->osdbpp[av7110->osdwin]], w, lpb, inc, data); 
                BlitBitmap(av7110, av7110->osdwin, x0, y0+i*lpb, 0);
                data+=lpb*inc; 
        }
        if (brest) {
                LoadBitmap(av7110, bpp2bit[av7110->osdbpp[av7110->osdwin]], w, brest/bpl, inc, data); 
                BlitBitmap(av7110, av7110->osdwin, x0, y0+bnum*lpb, 0);
        }
        ReleaseBitmap(av7110);
        return 0;
}

static int OSD_DrawCommand(struct av7110 *av7110, osd_cmd_t *dc)
{
        switch (dc->cmd) {
        case OSD_Close:
                DestroyOSDWindow(av7110, av7110->osdwin);
                return 0;
        case OSD_Open:
                av7110->osdbpp[av7110->osdwin]=(dc->color-1)&7;
                CreateOSDWindow(av7110, av7110->osdwin, bpp2bit[av7110->osdbpp[av7110->osdwin]],
                                dc->x1-dc->x0+1, dc->y1-dc->y0+1);
                if (!dc->data) {
                        MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
                        SetColorBlend(av7110, av7110->osdwin);
                }
                return 0;
        case OSD_Show:
                MoveWindowRel(av7110, av7110->osdwin, 0, 0);
                return 0;
        case OSD_Hide:
                HideWindow(av7110, av7110->osdwin);
                return 0;
        case OSD_Clear:
                DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, 0);
                return 0;
        case OSD_Fill:
                DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, dc->color);
                return 0;
        case OSD_SetColor:
                OSDSetColor(av7110, dc->color, dc->x0, dc->y0, dc->x1, dc->y1); 
                return 0;
        case OSD_SetPalette:
        {      
                if (FW_VERSION(av7110->arm_app) >= 0x2618)
                        OSDSetPalette(av7110, (u32 *)dc->data, dc->color, dc->x0);
                else {
                int i, len=dc->x0-dc->color+1;
                u8 *colors=(u8 *)dc->data;

                for (i=0; i<len; i++)
                        OSDSetColor(av7110, dc->color+i,
                                    colors[i*4]  , colors[i*4+1],
                                    colors[i*4+2], colors[i*4+3]);
                }
                return 0;
        }
        case OSD_SetTrans: 
                return 0;
        case OSD_SetPixel:
                DrawLine(av7110, av7110->osdwin,
                         dc->x0, dc->y0, 0, 0,
                         dc->color);
                return 0;
        case OSD_GetPixel: 
                return 0;

        case OSD_SetRow:   
                dc->y1=dc->y0;
        case OSD_SetBlock:
                OSDSetBlock(av7110, dc->x0, dc->y0, dc->x1, dc->y1, dc->color, dc->data);
                return 0;

        case OSD_FillRow:
                DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
                          dc->x1-dc->x0+1, dc->y1,
                          dc->color);
                return 0;
        case OSD_FillBlock:
                DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
                          dc->x1-dc->x0+1, dc->y1-dc->y0+1,
                          dc->color);
                return 0;
        case OSD_Line:
                DrawLine(av7110, av7110->osdwin,
                         dc->x0, dc->y0, dc->x1-dc->x0, dc->y1-dc->y0,
                         dc->color);
                return 0;
        case OSD_Query: 
                return 0;
        case OSD_Test:
                return 0;
        case OSD_Text:
        {
                char textbuf[240];
                
                if (strncpy_from_user(textbuf, dc->data, 240)<0)
                        return -EFAULT;
                textbuf[239]=0;
                if (dc->x1>3) 
                        dc->x1=3;
                SetFont(av7110, av7110->osdwin, dc->x1,
                        (u16) (dc->color&0xffff), (u16) (dc->color>>16));
                FlushText(av7110);
                WriteText(av7110, av7110->osdwin, dc->x0, dc->y0, textbuf);
                return 0;
        }
        case OSD_SetWindow:
                if (dc->x0<1 || dc->x0>7)
                        return -EINVAL;
                av7110->osdwin=dc->x0;
                return 0;
        case OSD_MoveWindow:
                MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
                SetColorBlend(av7110, av7110->osdwin);
                return 0;
        default:
                return -EINVAL;
        }
}


static int dvb_osd_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;

 	DEB_EE(("av7110: %p\n",av7110));

        if (cmd==OSD_SEND_CMD)
                return OSD_DrawCommand(av7110, (osd_cmd_t *)parg);

	return -EINVAL;
}


static struct file_operations dvb_osd_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_generic_open,
	.release	= dvb_generic_release,
};

static struct dvb_device dvbdev_osd = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_osd_fops,
	.kernel_ioctl	= dvb_osd_ioctl,
};

#endif /* CONFIG_DVB_AV7110_OSD */


/* get version of the firmware ROM, RTSL, video ucode and ARM application  */

static void firmversion(struct av7110 *av7110)
{
        u16 buf[20];

        u16 tag = ((COMTYPE_REQUEST << 8) + ReqVersion);
        
 	DEB_EE(("av7110: %p\n",av7110));

        RequestParameter(av7110, tag, buf, 16);
        
        av7110->arm_fw=(buf[0] << 16) + buf[1];
        av7110->arm_rtsl=(buf[2] << 16) + buf[3];
        av7110->arm_vid=(buf[4] << 16) + buf[5];
        av7110->arm_app=(buf[6] << 16) + buf[7];
        av7110->avtype=(buf[8] << 16) + buf[9];

        printk ("DVB: AV711%d(%d) - firm %08x, rtsl %08x, vid %08x, app %08x\n",
		av7110->avtype, av7110->dvb_adapter->num, av7110->arm_fw, 
                av7110->arm_rtsl, av7110->arm_vid, av7110->arm_app);

	/* print firmware capabilities */
	if (FW_CI_LL_SUPPORT(av7110->arm_app))
		printk ("DVB: AV711%d(%d) - firmware supports CI link layer interface\n",
				av7110->avtype, av7110->dvb_adapter->num);
	else
		printk ("DVB: AV711%d(%d) - no firmware support for CI link layer interface\n",
				av7110->avtype, av7110->dvb_adapter->num);

        return;
}

static int waitdebi(struct av7110 *av7110, int adr, int state)
{
        int k;
        
 	DEB_EE(("av7110: %p\n",av7110));

        for (k=0; k<100; k++, udelay(500)) {
                if (irdebi(av7110, DEBINOSWAP, adr, 0, 2) == state) 
                        return 0;
        }
        return -1;
}


static int load_dram(struct av7110 *av7110, u32 *data, int len)
{
        int i;
        int blocks, rest;
        u32 base, bootblock=BOOT_BLOCK;
        
 	DEB_EE(("av7110: %p\n",av7110));

        blocks=len/BOOT_MAX_SIZE;
        rest=len % BOOT_MAX_SIZE;
        base=DRAM_START_CODE;
        
        for (i=0; i<blocks; i++) {
                if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
                        return -1;
                DEB_D(("Writing DRAM block %d\n",i));
                mwdebi(av7110, DEBISWAB, bootblock,
                       ((char*)data) + i*(BOOT_MAX_SIZE),
                       BOOT_MAX_SIZE);
                bootblock^=0x1400;
                iwdebi(av7110, DEBISWAB, BOOT_BASE, swab32(base), 4);
                iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, BOOT_MAX_SIZE, 2);
                iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
                base+=BOOT_MAX_SIZE;
        }
        
        if (rest > 0) {
                if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
                        return -1;
                if (rest>4)
                        mwdebi(av7110, DEBISWAB, bootblock, ((char*)data) + i*(BOOT_MAX_SIZE), rest);
                else
                        mwdebi(av7110, DEBISWAB, bootblock, ((char*)data) + i*(BOOT_MAX_SIZE) - 4, rest+4);
                
                iwdebi(av7110, DEBISWAB, BOOT_BASE, swab32(base), 4);
                iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, rest, 2);
                iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
        }
        if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0)
                return -1;
        iwdebi(av7110, DEBINOSWAP, BOOT_SIZE, 0, 2);
        iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
        if (waitdebi(av7110, BOOT_STATE, BOOTSTATE_BOOT_COMPLETE) < 0)
                return -1;
        return 0;
}


static u8 bootcode[] = {
        0xea, 0x00, 0x00, 0x0e, 0xe1, 0xb0, 0xf0, 0x0e, /* 0x0000 */
        0xe2, 0x5e, 0xf0, 0x04, 0xe2, 0x5e, 0xf0, 0x04,
        0xe2, 0x5e, 0xf0, 0x08, 0xe2, 0x5e, 0xf0, 0x04,
        0xe2, 0x5e, 0xf0, 0x04, 0xe2, 0x5e, 0xf0, 0x04,
        0x2c, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x0c,
        0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x34,
        0x00, 0x00, 0x00, 0x00, 0xa5, 0xa5, 0x5a, 0x5a,
        0x00, 0x1f, 0x15, 0x55, 0x00, 0x00, 0x00, 0x09,
        0xe5, 0x9f, 0xd0, 0x5c, 0xe5, 0x9f, 0x40, 0x54, /* 0x0040 */
        0xe3, 0xa0, 0x00, 0x00, 0xe5, 0x84, 0x00, 0x00,
        0xe5, 0x84, 0x00, 0x04, 0xe1, 0xd4, 0x10, 0xb0,
        0xe3, 0x51, 0x00, 0x00, 0x0a, 0xff, 0xff, 0xfc,
        0xe1, 0xa0, 0x10, 0x0d, 0xe5, 0x94, 0x30, 0x04,
        0xe1, 0xd4, 0x20, 0xb2, 0xe2, 0x82, 0x20, 0x3f,
        0xe1, 0xb0, 0x23, 0x22, 0x03, 0xa0, 0x00, 0x02,
        0xe1, 0xc4, 0x00, 0xb0, 0x0a, 0xff, 0xff, 0xf4,
        0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0, /* 0x0080 */
        0xe8, 0xb1, 0x1f, 0xe0, 0xe8, 0xa3, 0x1f, 0xe0,
        0xe2, 0x52, 0x20, 0x01, 0x1a, 0xff, 0xff, 0xf9,
        0xe2, 0x2d, 0xdb, 0x05, 0xea, 0xff, 0xff, 0xec,
        0x2c, 0x00, 0x03, 0xf8, 0x2c, 0x00, 0x04, 0x00,
};

#include "av7110_firm.h"

static int bootarm(struct av7110 *av7110)
{
	struct saa7146_dev *dev= av7110->dev;
        u32 ret;
        int i;

 	DEB_EE(("av7110: %p\n",av7110));

        saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTLO);

        /* Disable DEBI and GPIO irq */
	IER_DISABLE(av7110->dev, MASK_03|MASK_19);
/*
        saa7146_write(av7110->dev, IER, 
                      saa7146_read(av7110->dev, IER) & 
                      ~(MASK_19 | MASK_03));
*/
        saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));

        /* enable DEBI */
        saa7146_write(av7110->dev, MC1, 0x08800880);
        saa7146_write(av7110->dev, DD1_STREAM_B, 0x00000000);
        saa7146_write(av7110->dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));
        
        /* test DEBI */
        iwdebi(av7110, DEBISWAP, DPRAM_BASE, 0x76543210, 4);
        if ((ret=irdebi(av7110, DEBINOSWAP, DPRAM_BASE, 0, 4))!=0x10325476) {
                printk(KERN_ERR "dvb: debi test in bootarm() failed: "
                       "%08x != %08x\n", ret, 0x10325476);;
                return -1;
        }
        for (i=0; i<8192; i+=4) 
                iwdebi(av7110, DEBISWAP, DPRAM_BASE+i, 0x00, 4);
        DEB_D(("bootarm: debi test OK\n"));

        /* boot */
        DEB_D(("bootarm: load boot code\n"));

        saa7146_setgpio(dev, ARM_IRQ_LINE, SAA7146_GPIO_IRQLO);
        //saa7146_setgpio(dev, DEBI_DONE_LINE, SAA7146_GPIO_INPUT);
        //saa7146_setgpio(dev, 3, SAA7146_GPIO_INPUT);

	mwdebi(av7110, DEBISWAB, DPRAM_BASE, bootcode, sizeof(bootcode));
        iwdebi(av7110, DEBINOSWAP, BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
        
        wait_for_debi_done(av7110);
        saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTHI);
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(HZ);
        
        DEB_D(("bootarm: load dram code\n"));

	if (load_dram(av7110, (u32 *)Root, sizeof(Root))<0)
		return -1;

	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTLO);
        mdelay(1);
        
        DEB_D(("bootarm: load dpram code\n"));

	mwdebi(av7110, DEBISWAB, DPRAM_BASE, Dpram, sizeof(Dpram));

	wait_for_debi_done(av7110);

        saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTHI);
        mdelay(800);

        //ARM_ClearIrq(av7110); 
        ARM_ResetMailBox(av7110); 
        saa7146_write(av7110->dev, ISR, (MASK_19 | MASK_03));
	IER_ENABLE(av7110->dev, MASK_03);
//      saa7146_write(av7110->dev, IER, 
//                      saa7146_read(av7110->dev, IER) | MASK_03 );

        av7110->arm_errors=0;
        av7110->arm_ready=1;
        return 0;
}

static inline int SetPIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid, 
        u16 subpid, u16 pcrpid)
{
 	DEB_EE(("av7110: %p\n",av7110));

	if (vpid == 0x1fff || apid == 0x1fff ||
	    ttpid == 0x1fff || subpid == 0x1fff || pcrpid == 0x1fff) {
		vpid = apid = ttpid = subpid = pcrpid = 0;
		av7110->pids[DMX_PES_VIDEO] = 0;
		av7110->pids[DMX_PES_AUDIO] = 0;
		av7110->pids[DMX_PES_TELETEXT] = 0;
		av7110->pids[DMX_PES_PCR] = 0;
	}

        return outcom(av7110, COMTYPE_PIDFILTER, MultiPID, 5, 
                      pcrpid, vpid, apid, ttpid, subpid);
}

static void ChangePIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid, 
        u16 subpid, u16 pcrpid)
{
 	DEB_EE(("av7110: %p\n",av7110));

        if (down_interruptible(&av7110->pid_mutex))
		return;

        if (!(vpid&0x8000))  av7110->pids[DMX_PES_VIDEO]=vpid;
        if (!(apid&0x8000))  av7110->pids[DMX_PES_AUDIO]=apid;
        if (!(ttpid&0x8000)) av7110->pids[DMX_PES_TELETEXT]=ttpid;
        if (!(pcrpid&0x8000)) av7110->pids[DMX_PES_PCR]=pcrpid;

        av7110->pids[DMX_PES_SUBTITLE]=0;

        if (av7110->fe_synced) {
                pcrpid = av7110->pids[DMX_PES_PCR];
                SetPIDs(av7110, vpid, apid, ttpid, subpid, pcrpid);
        }

        up(&av7110->pid_mutex);
}


static void SetMode(struct av7110 *av7110, int mode)
{
 	DEB_EE(("av7110: %p\n",av7110));

        outcom(av7110, COMTYPE_ENCODER, LoadVidCode, 1, mode);
        
        if (!av7110->playing) {
                ChangePIDs(av7110, av7110->pids[DMX_PES_VIDEO], 
                           av7110->pids[DMX_PES_AUDIO], 
                           av7110->pids[DMX_PES_TELETEXT],  
                           0, av7110->pids[DMX_PES_PCR]);
                outcom(av7110, COMTYPE_PIDFILTER, Scan, 0);
        }
}

static inline void TestMode(struct av7110 *av7110, int mode)
{
//	DEB_EE(("av7110: %p\n",av7110));
        outcom(av7110, COMTYPE_ENCODER, SetTestMode, 1, mode);
}

static inline void VidMode(struct av7110 *av7110, int mode)
{
// 	DEB_EE(("av7110: %p\n",av7110));
        outcom(av7110, COMTYPE_ENCODER, SetVidMode, 1, mode);
}
           

static int inline vidcom(struct av7110 *av7110, u32 com, u32 arg)
{
// 	DEB_EE(("av7110: %p\n",av7110));
        return outcom(av7110, 0x80, 0x02, 4, 
                      (com>>16), (com&0xffff), 
                      (arg>>16), (arg&0xffff));
}

static int inline audcom(struct av7110 *av7110, u32 com)
{
//	DEB_EE(("av7110: %p\n",av7110));
	return outcom(av7110, 0x80, 0x03, 4, 
                      (com>>16), (com&0xffff));
}

static inline void Set22K(struct av7110 *av7110, int state)
{
 	DEB_EE(("av7110: %p\n",av7110));
	outcom(av7110, COMTYPE_AUDIODAC, (state ? ON22K : OFF22K), 0);
}


static int SendDiSEqCMsg(struct av7110 *av7110, int len, u8 *msg, unsigned long burst)
{
        int i;
	u16 buf[18] = { ((COMTYPE_AUDIODAC << 8) + SendDiSEqC),
                        16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

 	DEB_EE(("av7110: %p\n",av7110));

	if (len>10)
		len=10;

	buf[1] = len+2;
	buf[2] = len;

	if (burst!=-1)
		buf[3]=burst ? 0x01 : 0x00;
	else
		buf[3]=0xffff;

	for (i=0; i<len; i++)
		buf[i+4]=msg[i];

	if (SOutCommand(av7110, buf, 18))
		printk("SendDiSEqCMsg error\n");

        return 0;
}

/****************************************************************************
 * I2C client commands
 ****************************************************************************/

static inline int i2c_writereg(struct av7110 *av7110, u8 id, u8 reg, u8 val)
{
        u8 msg[2]={ reg, val }; 
        struct dvb_i2c_bus *i2c = av7110->i2c_bus;
        struct i2c_msg msgs;

        msgs.flags=0;
        msgs.addr=id/2;
        msgs.len=2;
        msgs.buf=msg;
        return i2c->xfer (i2c, &msgs, 1);
}

static inline u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg)
{
        struct dvb_i2c_bus *i2c = av7110->i2c_bus;
        u8 mm1[] = {0x00};
        u8 mm2[] = {0x00};
        struct i2c_msg msgs[2];

        msgs[0].flags=0;
        msgs[1].flags=I2C_M_RD;
        msgs[0].addr=msgs[1].addr=id/2;
        mm1[0]=reg;
        msgs[0].len=1; msgs[1].len=1;
        msgs[0].buf=mm1; msgs[1].buf=mm2;
        i2c->xfer(i2c, msgs, 2);

        return mm2[0];
}


/****************************************************************************
 * I/O buffer management and control
 ****************************************************************************/

static int sw2mode[16] = {
        VIDEO_MODE_PAL, VIDEO_MODE_NTSC, VIDEO_MODE_NTSC, VIDEO_MODE_PAL,
        VIDEO_MODE_NTSC, VIDEO_MODE_NTSC, VIDEO_MODE_PAL, VIDEO_MODE_NTSC,
        VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL,
        VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL,
};

static void get_video_format(struct av7110 *av7110, u8 *buf, int count)
{
        int i;
	int hsize,vsize;
        int sw;
        u8 *p;

 	DEB_EE(("av7110: %p\n",av7110));

        if (av7110->sinfo)
                return;
        for (i=7; i<count-10; i++) {
                p=buf+i;
                if (p[0] || p[1] || p[2]!=0x01 || p[3]!=0xb3)
                        continue;
                p+=4;
                hsize = ((p[1] &0xF0) >> 4) | (p[0] << 4);
                vsize = ((p[1] &0x0F) << 8) | (p[2]);
                sw = (p[3]&0x0F);
                SetMode(av7110, sw2mode[sw]);
                DEB_S(("dvb: playback %dx%d fr=%d\n", hsize, vsize, sw));
                av7110->sinfo=1;
                break;
        }
}

static inline long aux_ring_buffer_write(struct dvb_ringbuffer *rbuf, const char *buf, unsigned long count)
{
        unsigned long todo = count;
        int free;
    
        while (todo > 0) {
                if (dvb_ringbuffer_free(rbuf)<2048) {
                        if (wait_event_interruptible(rbuf->queue,
                                                     (dvb_ringbuffer_free(rbuf)>=2048)))
                        	return count-todo;
                }   
                free = dvb_ringbuffer_free(rbuf);
                if (free > todo)
                        free = todo;
                (void)dvb_ringbuffer_write(rbuf,buf,free,0);
                todo -= free;
                buf += free;
        }

	return count-todo;
}

static void play_video_cb(u8 *buf, int count, void *priv)
{
        struct av7110 *av7110=(struct av7110 *) priv;
 	DEB_EE(("av7110: %p\n",av7110));

        if ((buf[3]&0xe0)==0xe0) {
                get_video_format(av7110, buf, count);
                aux_ring_buffer_write(&av7110->avout, buf, count);
        } else
                aux_ring_buffer_write(&av7110->aout, buf, count);
}

static void play_audio_cb(u8 *buf, int count, void *priv)
{
        struct av7110 *av7110=(struct av7110 *) priv;
 	DEB_EE(("av7110: %p\n",av7110));
        
        aux_ring_buffer_write(&av7110->aout, buf, count);
}

#define FREE_COND (dvb_ringbuffer_free(&av7110->avout)>=20*1024 && dvb_ringbuffer_free(&av7110->aout)>=20*1024)

static ssize_t dvb_play(struct av7110 *av7110, const u8 *buf,
         unsigned long count, int nonblock, int type, int umem)
{
        unsigned long todo = count, n;
 	DEB_EE(("av7110: %p\n",av7110));

        if (!av7110->kbuf[type])
                return -ENOBUFS;

	if (nonblock && !FREE_COND)
                return -EWOULDBLOCK;
                
        while (todo>0) {
                if (!FREE_COND) {
                        if (nonblock)
                                return count-todo;
                        if (wait_event_interruptible(av7110->avout.queue,
                                                     FREE_COND))
                        	return count-todo;
                } 
		n=todo;
                if (n>IPACKS*2)
                        n=IPACKS*2;
                if (umem) {
                        if (copy_from_user(av7110->kbuf[type], buf, n)) 
                                return -EFAULT;
                        av7110_ipack_instant_repack(av7110->kbuf[type], n,
						    &av7110->ipack[type]);
                } else {
                        av7110_ipack_instant_repack(buf, n,
						    &av7110->ipack[type]);
		}
                todo -= n;
                buf += n;
        }
	return count-todo;
}

static ssize_t dvb_aplay(struct av7110 *av7110, const u8 *buf,
         unsigned long count, int nonblock, int type)
{
        unsigned long todo = count, n;
	DEB_EE(("av7110: %p\n",av7110));

        if (!av7110->kbuf[type])
                return -ENOBUFS;
        if (nonblock && dvb_ringbuffer_free(&av7110->aout)<20*1024)
                return -EWOULDBLOCK;
                
        while (todo>0) {
                if (dvb_ringbuffer_free(&av7110->aout)<20*1024) {
                        if (nonblock)
                                return count-todo;
                        if (wait_event_interruptible(av7110->aout.queue,
                                                     (dvb_ringbuffer_free(&av7110->aout)>=
                                                      20*1024)))
                        	return count-todo;
                } 
		n=todo;
                if (n>IPACKS*2)
                        n=IPACKS*2;
                if (copy_from_user(av7110->kbuf[type], buf, n)) 
                        return -EFAULT;
                av7110_ipack_instant_repack(av7110->kbuf[type], n,
					    &av7110->ipack[type]);
//                        memcpy(dvb->kbuf[type], buf, n); 
                todo -= n;
                buf += n;
        }
	return count-todo;
}

void init_p2t(struct av7110_p2t *p, struct dvb_demux_feed *feed)
{
	memset(p->pes,0,TS_SIZE);
	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
	if (feed) p->feed = feed;
}

void clear_p2t(struct av7110_p2t *p)
{
	memset(p->pes,0,TS_SIZE);
//	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
}


long int find_pes_header(u8 const *buf, long int length, int *frags)
{
	int c = 0;
	int found = 0;

	*frags = 0;

	while (c < length-3 && !found) {
		if (buf[c] == 0x00 && buf[c+1] == 0x00 && 
		    buf[c+2] == 0x01) {
			switch ( buf[c+3] ) {
				
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				found = 1;
				break;
				
			default:
				c++;
				break;
			}	
		} else c++;
	}
	if (c == length-3 && !found){
		if (buf[length-1] == 0x00) *frags = 1;
		if (buf[length-2] == 0x00 &&
		    buf[length-1] == 0x00) *frags = 2;
		if (buf[length-3] == 0x00 &&
		    buf[length-2] == 0x00 &&
		    buf[length-1] == 0x01) *frags = 3;
		return -1;
	}

	return c;
}

void pes_to_ts( u8 const *buf, long int length, u16 pid, struct av7110_p2t *p)
{
	int c,c2,l,add;
	int check,rest;

	c = 0;
	c2 = 0;
	if (p->frags){
		check = 0;
		switch(p->frags){
		case 1:
			if ( buf[c] == 0x00 && buf[c+1] == 0x01 ){
				check = 1;
				c += 2;
			}
			break;
		case 2:
			if ( buf[c] == 0x01 ){
				check = 1;
				c++;
			}
			break;
		case 3:
			check = 1;
		}
		if(check){
			switch ( buf[c] ) {
				
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				p->pes[0] = 0x00;
				p->pes[1] = 0x00;
				p->pes[2] = 0x01;
				p->pes[3] = buf[c];
				p->pos=4;
				memcpy(p->pes+p->pos,buf+c,(TS_SIZE-4)-p->pos);
				c += (TS_SIZE-4)-p->pos;
				p_to_t(p->pes,(TS_SIZE-4),pid,&p->counter,
				       p->feed);
				clear_p2t(p);
				break;
				
			default:
				c=0;
				break;
			}
		}
		p->frags = 0;
	}
		
	if (p->pos){
		c2 = find_pes_header(buf+c,length-c,&p->frags);
		if (c2 >= 0 && c2 < (TS_SIZE-4)-p->pos){
			l = c2+c;
		} else l = (TS_SIZE-4)-p->pos;
		memcpy(p->pes+p->pos,buf,l);
		c += l;
		p->pos += l;
		p_to_t(p->pes,p->pos,pid,&p->counter, p->feed);
		clear_p2t(p);
	}
			
	add = 0;
	while (c < length){
		c2 = find_pes_header(buf+c+add,length-c-add,&p->frags);
		if (c2 >= 0) {
			c2 += c+add;
			if (c2 > c){
				p_to_t(buf+c,c2-c,pid,&p->counter,
				       p->feed);
				c = c2;
				clear_p2t(p);
				add = 0;
			} else add = 1;
		} else {
			l = length-c;
			rest = l % (TS_SIZE-4);
			l -= rest;
			p_to_t(buf+c,l,pid,&p->counter,
			       p->feed);
			memcpy(p->pes,buf+c+l,rest);
			p->pos = rest;
			c = length;
		}
	}
}


int write_ts_header2(u16 pid, u8 *counter, int pes_start, u8 *buf, u8 length)
{
	int i;
	int c = 0;
	int fill;
	u8 tshead[4] = { 0x47, 0x00, 0x00, 0x10}; 
        
	fill = (TS_SIZE-4)-length;
        if (pes_start) tshead[1] = 0x40;
	if (fill) tshead[3] = 0x30;
        tshead[1] |= (u8)((pid & 0x1F00) >> 8);
        tshead[2] |= (u8)(pid & 0x00FF);
        tshead[3] |= ((*counter)++ & 0x0F) ;
        memcpy(buf,tshead,4);
	c+=4;


	if (fill){
		buf[4] = fill-1;
		c++;
		if (fill >1){
			buf[5] = 0x00;
			c++;
		}
		for ( i = 6; i < fill+4; i++){
			buf[i] = 0xFF;
			c++;
		}
	}

        return c;
}


void p_to_t(u8 const *buf, long int length, u16 pid, u8 *counter, 
            struct dvb_demux_feed *feed)
{
  
	int l, pes_start;
	u8 obuf[TS_SIZE];
	long int c = 0;

	pes_start = 0;
	if ( length > 3 && 
             buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 )
		switch (buf[3]){
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				pes_start = 1;
				break;
				
			default:
				break;
		}			

	while ( c < length ){
		memset(obuf,0,TS_SIZE);
		if (length - c >= (TS_SIZE-4)){
			l = write_ts_header2(pid, counter, pes_start
					     , obuf, (TS_SIZE-4));
			memcpy(obuf+l, buf+c, TS_SIZE-l);
			c += TS_SIZE-l;
		} else { 
			l = write_ts_header2(pid, counter, pes_start
					     , obuf, length-c);
			memcpy(obuf+l, buf+c, TS_SIZE-l);
			c = length;
		}
                feed->cb.ts(obuf, 188, 0, 0, &feed->feed.ts, DMX_OK); 
		pes_start = 0;
	}
}

/****************************************************************************
 * V4L SECTION
 ****************************************************************************/

static struct v4l2_input inputs[2] = {
	{	
		.index 		= 0,
		.name 		= "DVB",
		.type		= V4L2_INPUT_TYPE_CAMERA,
		.audioset 	= 1,
		.tuner		= 0, /* ignored */
		.std		= V4L2_STD_PAL_BG|V4L2_STD_NTSC_M,
		.status		= 0,
	}, { 
		.index 		= 1,
		.name 		= "Television",
		.type		= V4L2_INPUT_TYPE_TUNER,
		.audioset 	= 2,
		.tuner		= 0,
		.std		= V4L2_STD_PAL_BG|V4L2_STD_NTSC_M,
		.status		= 0,
	}
};

/* taken from ves1820.c */
static int ves1820_writereg(struct saa7146_dev *dev, u8 reg, u8 data)
{
	u8 addr = 0x09;
        u8 buf[] = { 0x00, reg, data };
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = 3 };

  	DEB_EE(("av7710: dev: %p\n",dev));

	if( 1 != saa7146_i2c_transfer(dev, &msg, 1, 1)) {
		return -1;
	}
	return 0;
}

static int tuner_write(struct saa7146_dev *dev, u8 addr, u8 data [4])
{
        struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = data, .len = 4 };

  	DEB_EE(("av7710: dev: %p\n",dev));

	if( 1 != saa7146_i2c_transfer(dev, &msg, 1, 1)) {
		return -1;
	}
	return 0;
}


/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 62.5 kHz.
 */
static int tuner_set_tv_freq (struct saa7146_dev *dev, u32 freq)
{
        u32 div;
	u8 config;
        u8 buf [4];

 	DEB_EE(("av7710: freq: 0x%08x\n",freq));

	/* magic number: 614. tuning with the frequency given by v4l2
	   is always off by 614*62.5 = 38375 kHz...*/
	div = freq + 614;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x8e;

	if (freq < (u32) (16*168.25) ) 
		config = 0xa0;
	else if (freq < (u32) (16*447.25)) 
		config = 0x90;
	else
		config = 0x30;
	config &= ~0x02;

	buf[3] = config;

        return tuner_write (dev, 0x61, buf);
}

static struct saa7146_standard analog_standard[];
static struct saa7146_standard dvb_standard[];
static struct saa7146_standard standard[];

static struct v4l2_audio msp3400_v4l2_audio = {
	.index = 0,
	.name = "Television",
	.capability = V4L2_AUDCAP_STEREO
};

int av7110_dvb_c_switch(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;
	u16 adswitch;
	u8 band = 0;
	int source, sync;
	struct saa7146_fh *ov_fh = NULL;
	int restart_overlay = 0;

	DEB_EE(("av7110: %p\n",av7110));

	if( vv->ov_data != NULL ) {
		ov_fh = vv->ov_data->fh;
		saa7146_stop_preview(ov_fh);
		restart_overlay = 1;
	}

	if( 0 != av7110->current_input ) {
		adswitch = 1;
		band = 0x68; /* analog band */
		source = SAA7146_HPS_SOURCE_PORT_B;
		sync = SAA7146_HPS_SYNC_PORT_B;
		memcpy(standard,analog_standard,sizeof(struct saa7146_standard)*2);
		printk("av7110: switching to analog TV\n");
		msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0000); // loudspeaker source
		msp_writereg(av7110, MSP_WR_DSP, 0x0009, 0x0000); // headphone source
		msp_writereg(av7110, MSP_WR_DSP, 0x000a, 0x0000); // SCART 1 source
		msp_writereg(av7110, MSP_WR_DSP, 0x000e, 0x3000); // FM matrix, mono
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x4f00); // loudspeaker + headphone
		msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x4f00); // SCART 1 volume
	} else {
		adswitch = 0;
		band = 0x28; /* digital band */	
		source = SAA7146_HPS_SOURCE_PORT_A;
		sync = SAA7146_HPS_SYNC_PORT_A;
		memcpy(standard,dvb_standard,sizeof(struct saa7146_standard)*2);
		printk("av7110: switching DVB mode\n");
		msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0220); // loudspeaker source
		msp_writereg(av7110, MSP_WR_DSP, 0x0009, 0x0220); // headphone source
		msp_writereg(av7110, MSP_WR_DSP, 0x000a, 0x0220); // SCART 1 source
		msp_writereg(av7110, MSP_WR_DSP, 0x000e, 0x3000); // FM matrix, mono
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x7f00); // loudspeaker + headphone
		msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x7f00); // SCART 1 volume
	}

	/* hmm, this does not do anything!? */
	if (outcom(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, adswitch))
		printk("ADSwitch error\n");

	if( 0 != ves1820_writereg(dev, 0x0f, band )) {
		printk("setting band in demodulator failed.\n");
	}
	saa7146_set_hps_source_and_sync(dev, source, sync);

	/* restart overlay if it was active before */
	if( 0 != restart_overlay ) {
		saa7146_start_preview(ov_fh);
	}

	return 0;
}

int av7110_ioctl(struct saa7146_fh *fh, unsigned int cmd, void *arg) 
{
	struct saa7146_dev *dev = fh->dev;
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;
 	DEB_EE(("saa7146_dev: %p\n",dev));

	switch(cmd) {
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;
		u16 stereo_det;
		s8 stereo;

		DEB_EE(("VIDIOC_G_TUNER: %d\n", t->index));

		if( 0 == av7110->has_analog_tuner || t->index != 0 ) {
			return -EINVAL;
		}

		memset(t,0,sizeof(*t));
		strcpy(t->name, "Television");

		t->type = V4L2_TUNER_ANALOG_TV;
		t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;
		t->rangelow = 772;	/* 48.25 MHZ / 62.5 kHz = 772, see fi1216mk2-specs, page 2 */
		t->rangehigh = 13684;	/* 855.25 MHz / 62.5 kHz = 13684 */
		/* FIXME: add the real signal strength here */
		t->signal = 0xffff;
		t->afc = 0;		

msp_readreg(av7110, MSP_RD_DEM, 0x007e, &stereo_det);
printk("VIDIOC_G_TUNER: msp3400 TV standard detection: 0x%04x\n", stereo_det);

		msp_readreg(av7110, MSP_RD_DSP, 0x0018, &stereo_det);
		printk("VIDIOC_G_TUNER: msp3400 stereo detection: 0x%04x\n", stereo_det);
		stereo = (s8)(stereo_det >> 8);
		if (stereo > 0x10) {
			/* stereo */
		t->rxsubchans 	= V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
			t->audmode = V4L2_TUNER_MODE_STEREO;
		}
		else if (stereo < -0x10) {
			/* bilingual*/
			t->rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
			 t->audmode = V4L2_TUNER_MODE_LANG1;
		}
		else /* mono */
			t->rxsubchans = V4L2_TUNER_SUB_MONO;

		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *t = arg;
		u16 fm_matrix, src;
		DEB_EE(("VIDIOC_S_TUNER: %d\n", t->index));

		if( 0 == av7110->has_analog_tuner || av7110->current_input != 1 ) {
			return -EINVAL;
		}


		switch(t->audmode) {
		case V4L2_TUNER_MODE_STEREO:
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_STEREO\n"));
			fm_matrix = 0x3001; // stereo
			src = 0x0020;
				break;
		case V4L2_TUNER_MODE_LANG1:
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG1\n"));
			fm_matrix = 0x3000; // mono
			src = 0x0000;
				break;
		case V4L2_TUNER_MODE_LANG2:
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG2\n"));
			fm_matrix = 0x3000; // mono
			src = 0x0010;
				break;
		default: /* case V4L2_TUNER_MODE_MONO: {*/
				DEB_D(("VIDIOC_S_TUNER: TDA9840_SET_MONO\n"));
			fm_matrix = 0x3000; // mono
			src = 0x0030;
				break;
			}
		msp_writereg(av7110, MSP_WR_DSP, 0x000e, fm_matrix);
		msp_writereg(av7110, MSP_WR_DSP, 0x0008, src);
		msp_writereg(av7110, MSP_WR_DSP, 0x0009, src);
		msp_writereg(av7110, MSP_WR_DSP, 0x000a, src);

		return 0;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		DEB_EE(("VIDIOC_G_FREQ: freq:0x%08x.\n", f->frequency));

		if( 0 == av7110->has_analog_tuner || av7110->current_input != 1 ) {
			return -EINVAL;
		}

		memset(f,0,sizeof(*f));
		f->type = V4L2_TUNER_ANALOG_TV;
		f->frequency =  av7110->current_freq;

		return 0;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		DEB_EE(("VIDIOC_S_FREQUENCY: freq:0x%08x.\n",f->frequency));

		if( 0 == av7110->has_analog_tuner || av7110->current_input != 1 ) {
			return -EINVAL;
		}

		if (V4L2_TUNER_ANALOG_TV != f->type)
			return -EINVAL;

		msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0xffe0); // fast mute
		msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0xffe0);

		/* tune in desired frequency */			
		tuner_set_tv_freq(dev, f->frequency);
		av7110->current_freq = f->frequency;

		msp_writereg(av7110, MSP_WR_DSP, 0x0015, 0x003f); // start stereo detection
		msp_writereg(av7110, MSP_WR_DSP, 0x0015, 0x0000);
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x4f00); // loudspeaker + headphone
		msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x4f00); // SCART 1 volume

		return 0;
	}
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;
		
		DEB_EE(("VIDIOC_ENUMINPUT: %d\n", i->index));

		if( 0 != av7110->has_analog_tuner ) {
			if( i->index < 0 || i->index >= 2) {
				return -EINVAL;
			}
		} else {
		if( i->index != 0 ) {
			return -EINVAL;
		}
		}		

		memcpy(i, &inputs[i->index], sizeof(struct v4l2_input));
		
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *input = (int *)arg;
		*input = av7110->current_input;
		DEB_EE(("VIDIOC_G_INPUT: %d\n", *input));
		return 0;		
	}	
	case VIDIOC_S_INPUT:
	{
		int input = *(int *)arg;

		DEB_EE(("VIDIOC_S_INPUT: %d\n", input));

		if( 0 == av7110->has_analog_tuner ) {
		return 0;		
	}	
		
		if( input < 0 || input >= 2) {
			return -EINVAL;
		}
		
		/* fixme: switch inputs here */
		av7110->current_input = input;
		return av7110_dvb_c_switch(fh);
	}	
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		DEB_EE(("VIDIOC_G_AUDIO: %d\n", a->index));
		if (a->index != 0)
			return -EINVAL;
		memcpy(a, &msp3400_v4l2_audio, sizeof(struct v4l2_audio));
		break;
	}
	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *a = arg;
		DEB_EE(("VIDIOC_S_AUDIO: %d\n", a->index));
		break;
	}
	default:
		printk("no such ioctl\n");
		return -ENOIOCTLCMD;
	}
	return 0;
}	

static unsigned int dvb_audio_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
        struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
        unsigned int mask = 0;

	DEB_EE(("av7110: %p\n",av7110));

	poll_wait(file, &av7110->aout.queue, wait);

	if (av7110->playing) {
                if (dvb_ringbuffer_free(&av7110->aout)>=20*1024)
                        mask |= (POLLOUT | POLLWRNORM);
        } else /* if not playing: may play if asked for */
		mask = (POLLOUT | POLLWRNORM);

	return mask;
}


/****************************************************************************
 * END OF V4L SECTION
 ****************************************************************************/


/****************************************************************************
 * DVB API SECTION
 ****************************************************************************/


/******************************************************************************
 * hardware filter functions
 ******************************************************************************/

static int StartHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
        struct dvb_demux_feed *dvbdmxfeed=dvbdmxfilter->feed;
        struct av7110 *av7110=(struct av7110 *) dvbdmxfeed->demux->priv;
        u16 buf[20];
        int ret, i;
        u16 handle;
//        u16 mode=0x0320;
        u16 mode=0xb96a;
        
 	DEB_EE(("av7110: %p\n",av7110));

        if (dvbdmxfilter->type==DMX_TYPE_SEC) {
		if (hw_sections) {
	                buf[4]=(dvbdmxfilter->filter.filter_value[0]<<8)|
        	                dvbdmxfilter->maskandmode[0];
                	for (i=3; i<18; i++)
                        	buf[i+4-2]=(dvbdmxfilter->filter.filter_value[i]<<8)|
                                	dvbdmxfilter->maskandmode[i];
	                mode=4;
		}
        } else
        if ((dvbdmxfeed->ts_type & TS_PACKET) &&
            !(dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)) 
                init_p2t(&av7110->p2t_filter[dvbdmxfilter->index], dvbdmxfeed);

        buf[0] = (COMTYPE_PID_FILTER << 8) + AddPIDFilter; 
        buf[1] = 16;	
        buf[2] = dvbdmxfeed->pid;
        buf[3] = mode;

        ret=CommandRequest(av7110, buf, 20, &handle, 1);          
        if (ret<0) {
		printk("StartHWFilter error\n");
                return ret;
	}

        av7110->handle2filter[handle]=dvbdmxfilter;
        dvbdmxfilter->hw_handle=handle;

        return ret;
}

static int StopHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
        struct av7110 *av7110=(struct av7110 *) dvbdmxfilter->feed->demux->priv;
        u16 buf[3];
        u16 answ[2];
        int ret;
        u16 handle;
                
 	DEB_EE(("av7110: %p\n",av7110));

        handle=dvbdmxfilter->hw_handle;
        if (handle>32) {
                DEB_S(("dvb: StopHWFilter tried to stop invalid filter %d.\n",
                       handle));
                DEB_S(("dvb: filter type = %d\n",  dvbdmxfilter->type));
                return 0;
        }

        av7110->handle2filter[handle]=NULL;

        buf[0] = (COMTYPE_PID_FILTER << 8) + DelPIDFilter; 
        buf[1] = 1;	
        buf[2] = handle;
        ret=CommandRequest(av7110, buf, 3, answ, 2);          
	if (ret)
		printk("StopHWFilter error\n");

        if (answ[1] != handle) {
                DEB_S(("dvb: filter %d shutdown error :%d\n", handle, answ[1]));
                ret=-1;
        }
        return ret;
}


static int av7110_write_to_decoder(struct dvb_demux_feed *feed, const u8 *buf, size_t len)
{
        struct dvb_demux *demux = feed->demux;
        struct av7110 *av7110 = (struct av7110 *) demux->priv;
        struct ipack *ipack = &av7110->ipack[feed->pes_type];

	DEB_EE(("av7110: %p\n",av7110));

        switch (feed->pes_type) {
        case 0:
                if (av7110->audiostate.stream_source==AUDIO_SOURCE_MEMORY)
                        return -EINVAL;
                break;
        case 1:
                if (av7110->videostate.stream_source==VIDEO_SOURCE_MEMORY)
                        return -EINVAL;
                break;
        default:
                return -1;
        }

        if (!(buf[3] & 0x10)) { // no payload?
                return -1;
        }
        if (buf[1] & 0x40)
                av7110_ipack_flush(ipack);

        if (buf[3] & 0x20) {  // adaptation field?
                len -= buf[4]+1;
                buf += buf[4]+1;
                if (!len)
                        return 0;
        }
        
        av7110_ipack_instant_repack(buf+4, len-4, &av7110->ipack[feed->pes_type]);
        return 0;
}


static void dvb_feed_start_pid(struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        struct av7110 *av7110=(struct av7110 *) dvbdmx->priv;
        u16 *pid=dvbdmx->pids, npids[5];
        int i;
        
	DEB_EE(("av7110: %p\n",av7110));

        npids[0]=npids[1]=npids[2]=npids[3]=0xffff;
        npids[4]=0xffff;
        i=dvbdmxfeed->pes_type;
        npids[i]=(pid[i]&0x8000) ? 0 : pid[i];
        if ((i==2) && npids[i] && (dvbdmxfeed->ts_type & TS_PACKET)) {
                npids[i]=0;
                ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
                StartHWFilter(dvbdmxfeed->filter); 
                return;
        }
        if (dvbdmxfeed->pes_type<=2 || dvbdmxfeed->pes_type==4)
                ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);

        if (dvbdmxfeed->pes_type<2 && npids[0])
                if (av7110->fe_synced) 
                        outcom(av7110, COMTYPE_PIDFILTER, Scan, 0);

        if ((dvbdmxfeed->ts_type & TS_PACKET)) { 
                if (dvbdmxfeed->pes_type == 0 && 
                    !(dvbdmx->pids[0]&0x8000))
                        AV_StartRecord(av7110, RP_AUDIO, 
                                       dvbdmxfeed);
                if (dvbdmxfeed->pes_type == 1 && 
                    !(dvbdmx->pids[1]&0x8000))
                        AV_StartRecord(av7110, RP_VIDEO, 
                                       dvbdmxfeed);
        }
}

static void dvb_feed_stop_pid(struct dvb_demux_feed *dvbdmxfeed)
{
        struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
        struct av7110 *av7110=(struct av7110 *) dvbdmx->priv;
        u16 *pid=dvbdmx->pids, npids[5];
        int i;
        
	DEB_EE(("av7110: %p\n",av7110));

        if (dvbdmxfeed->pes_type<=1) {
                AV_Stop(av7110, dvbdmxfeed->pes_type ? 
                        RP_VIDEO : RP_AUDIO);
                if (!av7110->rec_mode)
                        dvbdmx->recording=0;
                if (!av7110->playing)
                        dvbdmx->playing=0;
        }
        npids[0]=npids[1]=npids[2]=npids[3]=0xffff;
        npids[4]=0xffff;
        i=dvbdmxfeed->pes_type;
        switch (i) {
        case 2: //teletext
                if (dvbdmxfeed->ts_type & TS_PACKET)
                        StopHWFilter(dvbdmxfeed->filter); 
                npids[2]=0;
                break;
        case 0:
        case 1:
        case 4:
                if (!pids_off) 
                        return;
                npids[i]=(pid[i]&0x8000) ? 0 : pid[i];
                break;
        }
        ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
}

static int av7110_start_feed(struct dvb_demux_feed *feed)
{
        struct dvb_demux *demux = feed->demux;
        struct av7110 *av7110 = (struct av7110 *) demux->priv;

	DEB_EE(("av7110: %p\n",av7110));

        if (!demux->dmx.frontend)
                return -EINVAL;

        if (feed->pid > 0x1fff) 
                return -EINVAL;

        if (feed->type == DMX_TYPE_TS) {
	        if ((feed->ts_type & TS_DECODER) &&
		    (feed->pes_type < DMX_TS_PES_OTHER)) {
		        switch (demux->dmx.frontend->source) {
			case DMX_MEMORY_FE: 
			        if (feed->ts_type & TS_DECODER)
				       if (feed->pes_type < 2 && 
                                           !(demux->pids[0] & 0x8000) &&
					   !(demux->pids[1] & 0x8000)) {
                                               dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
                                               dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
                                               AV_StartPlay(av7110,RP_AV);
                                               demux->playing = 1;
					}
				break;
			default:
                                dvb_feed_start_pid(feed);
				break;
			}
		} else
		        if ((feed->ts_type & TS_PACKET) &&
                            (demux->dmx.frontend->source!=DMX_MEMORY_FE))
                                StartHWFilter(feed->filter); 
        }
        
        if (feed->type == DMX_TYPE_SEC) {
                int i;

	        for (i=0; i<demux->filternum; i++) {
		        if (demux->filter[i].state!=DMX_STATE_READY)
			        continue;
			if (demux->filter[i].type!=DMX_TYPE_SEC)
			        continue;
			if (demux->filter[i].filter.parent!=&feed->feed.sec)
			        continue;
			demux->filter[i].state=DMX_STATE_GO;
                        if (demux->dmx.frontend->source!=DMX_MEMORY_FE)
                                StartHWFilter(&demux->filter[i]); 
                }
	}

        return 0;
}


static int av7110_stop_feed(struct dvb_demux_feed *feed)
{
        struct dvb_demux *demux = feed->demux;
        struct av7110 *av7110 = (struct av7110 *) demux->priv;

	DEB_EE(("av7110: %p\n",av7110));

        if (feed->type == DMX_TYPE_TS) {
                if (feed->ts_type & TS_DECODER) {
                        if (feed->pes_type >= DMX_TS_PES_OTHER ||
                            !demux->pesfilter[feed->pes_type]) 
                                return -EINVAL;
                        demux->pids[feed->pes_type]|=0x8000;
                        demux->pesfilter[feed->pes_type]=0;
                }
                if (feed->ts_type & TS_DECODER &&
		    feed->pes_type < DMX_TS_PES_OTHER) {
                        dvb_feed_stop_pid(feed);
                } else 
                        if ((feed->ts_type & TS_PACKET) &&
                            (demux->dmx.frontend->source != DMX_MEMORY_FE))
                                StopHWFilter(feed->filter); 
        }
        
        if (feed->type == DMX_TYPE_SEC) {
                int i;

	        for (i=0; i<demux->filternum; i++)
		        if (demux->filter[i].state==DMX_STATE_GO && 
			    demux->filter[i].filter.parent==&feed->feed.sec) {
			        demux->filter[i].state=DMX_STATE_READY;
                                if (demux->dmx.frontend->source!=DMX_MEMORY_FE)
                                        StopHWFilter(&demux->filter[i]); 
                }
	}

        return 0;
}


static void restart_feeds(struct av7110 *av7110)
{
        struct dvb_demux *dvbdmx=&av7110->demux;
        struct dvb_demux_feed *feed;
        int mode;
        int i;

	DEB_EE(("av7110: %p\n",av7110));

        mode=av7110->playing;
        av7110->playing=0;
        av7110->rec_mode=0;

        for (i=0; i<dvbdmx->filternum; i++) {
                feed=&dvbdmx->feed[i];
                if (feed->state==DMX_STATE_GO)
                        av7110_start_feed(feed);
        }

        if (mode)
                AV_StartPlay(av7110, mode);
}

static int dvb_get_stc(struct dmx_demux *demux, unsigned int num,
		uint64_t *stc, unsigned int *base)
{
	int ret;
        u16 fwstc[4];
        u16 tag = ((COMTYPE_REQUEST << 8) + ReqSTC);
	struct dvb_demux *dvbdemux;
	struct av7110 *av7110;

	/* pointer casting paranoia... */
	if (!demux)
		BUG();
	dvbdemux = (struct dvb_demux *) demux->priv;
	if (!dvbdemux)
		BUG();
	av7110 = (struct av7110 *) dvbdemux->priv;

	DEB_EE(("av7110: %p\n",av7110));

	if (num != 0)
		return -EINVAL;

        ret = CommandRequest(av7110, &tag, 0, fwstc, 4);
	if (ret) {
		printk(KERN_ERR "%s: CommandRequest error\n", __FUNCTION__);
		return -EIO;
	}
	DEB_EE(("av7110: fwstc = %04hx %04hx %04hx %04hx\n",
			fwstc[0], fwstc[1], fwstc[2], fwstc[3]));

	*stc =  (((uint64_t) ((fwstc[3] & 0x8000) >> 15)) << 32) |
		(((uint64_t)fwstc[1])     << 16) | ((uint64_t)fwstc[0]);
	*base = 1;

	DEB_EE(("av7110: stc = %lu\n", (unsigned long)*stc));

	return 0;
}


/******************************************************************************
 * SEC device file operations
 ******************************************************************************/

static int av7110_diseqc_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct av7110 *av7110 = fe->before_after_data;

	DEB_EE(("av7110: %p\n",av7110));

	switch (cmd) {
	case FE_SET_TONE:
		switch ((fe_sec_tone_mode_t) arg) {
		case SEC_TONE_ON:
			Set22K (av7110, 1);
			break;
		case SEC_TONE_OFF:
			Set22K (av7110, 0);
			break;
		default:
			return -EINVAL;
		};
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
	{
		struct dvb_diseqc_master_cmd *cmd = arg;
		SendDiSEqCMsg (av7110, cmd->msg_len, cmd->msg, -1);
		break;
	}

	case FE_DISEQC_SEND_BURST:
		SendDiSEqCMsg (av7110, 0, NULL, (unsigned long)arg);
		break;

	default:
		return -EOPNOTSUPP;
	};

	return 0;
}

/******************************************************************************
 * CI link layer file ops (FIXME: move this to separate module later)
 ******************************************************************************/

int ci_ll_init(struct dvb_ringbuffer *cirbuf, struct dvb_ringbuffer *ciwbuf, int size)
{
        dvb_ringbuffer_init(cirbuf, vmalloc(size), size);
        dvb_ringbuffer_init(ciwbuf, vmalloc(size), size);
        return 0;
}

void ci_ll_flush(struct dvb_ringbuffer *cirbuf, struct dvb_ringbuffer *ciwbuf)
{
        dvb_ringbuffer_flush_spinlock_wakeup(cirbuf);
        dvb_ringbuffer_flush_spinlock_wakeup(ciwbuf);
}

void ci_ll_release(struct dvb_ringbuffer *cirbuf, struct dvb_ringbuffer *ciwbuf)
{
        vfree(cirbuf->data);
        cirbuf->data=0;
        vfree(ciwbuf->data);
        ciwbuf->data=0;
}


int ci_ll_reset(struct dvb_ringbuffer *cibuf, struct file *file, 
                int slots, ca_slot_info_t *slot)
{
	int i;
	int len=0;
	u8 msg[8]={0x00,0x06,0,0x00,0xff,0x02,0x00,0x00};

	for (i=0; i<2; i++) {
		if (slots & (1<<i)) 
			len+=8;
	}

	if (dvb_ringbuffer_free(cibuf) < len)
		return -EBUSY;

	for (i=0; i<2; i++) {
		if (slots & (1<<i)) {
			msg[2]=i;
			dvb_ringbuffer_write(cibuf,msg,8,0);
			slot[i].flags=0;
		}
	}

	return 0;
}

static ssize_t ci_ll_write(struct dvb_ringbuffer *cibuf, struct file *file, const char *buf, size_t count, loff_t *ppos)
{
        int free;
        int non_blocking=file->f_flags&O_NONBLOCK;

        if (count>2048)
                return -EINVAL;
        free=dvb_ringbuffer_free(cibuf);
        if (count+2>free) { 
                if (non_blocking)
                        return -EWOULDBLOCK;
                if (wait_event_interruptible(cibuf->queue,
                                             (dvb_ringbuffer_free(cibuf)>=count+2)))
                        return 0;
        }

        DVB_RINGBUFFER_WRITE_BYTE(cibuf,count>>8);   
        DVB_RINGBUFFER_WRITE_BYTE(cibuf,count&0xff); 

        return dvb_ringbuffer_write(cibuf,buf,count,1);
}

static ssize_t ci_ll_read(struct dvb_ringbuffer *cibuf, struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int avail;
	int non_blocking=file->f_flags&O_NONBLOCK;
	ssize_t len;

	if (!cibuf->data || !count)
	        return 0;
	if (non_blocking && (dvb_ringbuffer_empty(cibuf)))
	        return -EWOULDBLOCK;
	if (wait_event_interruptible(cibuf->queue, 
				     !dvb_ringbuffer_empty(cibuf)))
		return 0;
	avail=dvb_ringbuffer_avail(cibuf);
	if (avail<4) 
		return 0;
	len= DVB_RINGBUFFER_PEEK(cibuf,0)<<8;
	len|=DVB_RINGBUFFER_PEEK(cibuf,1);
	if (avail<len+2 || count<len) 
		return -EINVAL;
	DVB_RINGBUFFER_SKIP(cibuf,2);

	return dvb_ringbuffer_read(cibuf,buf,len,1);
}

static int dvb_ca_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        int err=dvb_generic_open(inode, file);

	DEB_EE(("av7110: %p\n",av7110));

        if (err<0)
                return err;
        ci_ll_flush(&av7110->ci_rbuffer, &av7110->ci_wbuffer);
        return 0;
}

static unsigned int dvb_ca_poll (struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
        struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
        struct dvb_ringbuffer *rbuf = &av7110->ci_rbuffer;
        struct dvb_ringbuffer *wbuf = &av7110->ci_wbuffer;
        unsigned int mask = 0;

	DEB_EE(("av7110: %p\n",av7110));

        poll_wait (file, &rbuf->queue, wait);
        
        if (!dvb_ringbuffer_empty(rbuf))
                mask |= POLLIN;

	if (dvb_ringbuffer_avail(wbuf)>1024)
                mask |= POLLOUT;

        return mask;
}

static int dvb_ca_ioctl(struct inode *inode, struct file *file, 
                 unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        unsigned long arg=(unsigned long) parg;

	DEB_EE(("av7110: %p\n",av7110));

        switch (cmd) {
        case CA_RESET:
#ifdef NEW_CI
                
                return ci_ll_reset(&av7110->ci_wbuffer, file, arg, &av7110->ci_slot[0]);
#endif                        
                break;
                
        case CA_GET_CAP:
        {
                ca_caps_t cap;
                
                cap.slot_num=2;
#ifdef NEW_CI
                cap.slot_type=(FW_CI_LL_SUPPORT(av7110->arm_app) ? CA_CI_LINK : CA_CI) | CA_DESCR;
#else
                cap.slot_type=CA_CI|CA_DESCR;
#endif
                cap.descr_num=16;
                cap.descr_type=CA_ECD;
                memcpy(parg, &cap, sizeof(cap));
        }
        break;

        case CA_GET_SLOT_INFO:
        {
                ca_slot_info_t *info=(ca_slot_info_t *)parg;

                if (info->num>1)
                        return -EINVAL;
                av7110->ci_slot[info->num].num = info->num;
#ifdef NEW_CI
                av7110->ci_slot[info->num].type = FW_CI_LL_SUPPORT(av7110->arm_app) ? CA_CI_LINK : CA_CI;
#else
                av7110->ci_slot[info->num].type = CA_CI;
#endif
                memcpy(info, &av7110->ci_slot[info->num], sizeof(ca_slot_info_t));
        }
        break;

        case CA_GET_MSG:
                break;

        case CA_SEND_MSG:
                break;
                
        case CA_GET_DESCR_INFO:
        {
                ca_descr_info_t info;

                info.num=16;
                info.type=CA_ECD;
                memcpy (parg, &info, sizeof (info));
        }
        break;

        case CA_SET_DESCR:
        {
                ca_descr_t *descr=(ca_descr_t*) parg;

                if (descr->index>=16)
                        return -EINVAL;
                if (descr->parity>1)
                        return -EINVAL;
                outcom(av7110, COMTYPE_PIDFILTER, SetDescr, 5,
                       (descr->index<<8)|descr->parity,
                       (descr->cw[0]<<8)|descr->cw[1],
                       (descr->cw[2]<<8)|descr->cw[3],
                       (descr->cw[4]<<8)|descr->cw[5],
                       (descr->cw[6]<<8)|descr->cw[7]);
        }
        break;

        default:
                return -EINVAL;
        }
        return 0;
}

static ssize_t dvb_ca_write(struct file *file, const char *buf, 
             size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        
	DEB_EE(("av7110: %p\n",av7110));
        return ci_ll_write(&av7110->ci_wbuffer, file, buf, count, ppos);
}

static ssize_t dvb_ca_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;

	DEB_EE(("av7110: %p\n",av7110));
        return ci_ll_read(&av7110->ci_rbuffer, file, buf, count, ppos);
}



/******************************************************************************
 * Video MPEG decoder events
 ******************************************************************************/
static void dvb_video_add_event (struct av7110 *av7110, struct video_event *event)
{
	struct dvb_video_events *events = &av7110->video_events;
	int wp;

	DEB_D(("\n"));

	spin_lock_bh(&events->lock);

	wp = (events->eventw + 1) % MAX_VIDEO_EVENT;

	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_VIDEO_EVENT;
	}

	//FIXME: timestamp?
	memcpy(&events->events[events->eventw], event, sizeof(struct video_event));

	events->eventw = wp;

	spin_unlock_bh(&events->lock);

	wake_up_interruptible (&events->wait_queue);
}


static int dvb_video_get_event (struct av7110 *av7110, struct video_event *event, int flags)
{
	struct dvb_video_events *events = &av7110->video_events;

	DEB_D(("\n"));

	if (events->overflow) {
                events->overflow = 0;
                return -EOVERFLOW;
        }

        if (events->eventw == events->eventr) {
		int ret;

                if (flags & O_NONBLOCK)
                        return -EWOULDBLOCK;

                ret = wait_event_interruptible (events->wait_queue,
                                                events->eventw != events->eventr);
                if (ret < 0)
                        return ret;
        }

	spin_lock_bh(&events->lock);

	memcpy (event, &events->events[events->eventr],
		sizeof(struct video_event));

        events->eventr = (events->eventr + 1) % MAX_VIDEO_EVENT;

	spin_unlock_bh(&events->lock);

        return 0;
}


/******************************************************************************
 * DVB device file operations
 ******************************************************************************/

static unsigned int dvb_video_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
        struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
        unsigned int mask = 0;

	DEB_EE(("av7110: %p\n",av7110));

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	poll_wait(file, &av7110->avout.queue, wait);
	}
	
	poll_wait(file, &av7110->video_events.wait_queue, wait);

	if (av7110->video_events.eventw != av7110->video_events.eventr)
		mask = POLLPRI;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	if (av7110->playing) {
                if (FREE_COND)
                        mask |= (POLLOUT | POLLWRNORM);
        } else /* if not playing: may play if asked for */
                mask |= (POLLOUT | POLLWRNORM);
	}

        return mask;
}

static ssize_t dvb_video_write(struct file *file, const char *buf, 
                size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;

	DEB_EE(("av7110: %p\n",av7110));

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		return -EPERM;
	}

        if (av7110->videostate.stream_source!=VIDEO_SOURCE_MEMORY) 
                return -EPERM;

	return dvb_play(av7110, buf, count, file->f_flags&O_NONBLOCK, 1, 1);
}

static ssize_t dvb_audio_write(struct file *file, const char *buf, 
                size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;

	DEB_EE(("av7110: %p\n",av7110));

        if (av7110->audiostate.stream_source!=AUDIO_SOURCE_MEMORY) {
                printk(KERN_ERR "not audio source memory\n");
                return -EPERM;
        }
        return dvb_aplay(av7110, buf, count, file->f_flags&O_NONBLOCK, 0);
}

u8 iframe_header[] = { 0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x00, 0x00 };

#define MIN_IFRAME 400000

static int play_iframe(struct av7110 *av7110, u8 *buf, unsigned int len, int nonblock)
{
        int i, n=1;
       
	DEB_EE(("av7110: %p\n",av7110));

        if (!(av7110->playing&RP_VIDEO)) {
                if (AV_StartPlay(av7110, RP_VIDEO) < 0)
			return -EBUSY;
                n=MIN_IFRAME/len+1;
        }

	/* setting n always > 1, fixes problems when playing stillframes
	   consisting of I- and P-Frames */
	n=MIN_IFRAME/len+1;
	
	/* FIXME: nonblock? */
	dvb_play(av7110, iframe_header, sizeof(iframe_header), 0, 1, 0);

	for (i=0; i<n; i++) 
                dvb_play(av7110, buf, len, 0, 1, 1);

	av7110_ipack_flush(&av7110->ipack[1]);
	return 0;
}


static int dvb_video_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        unsigned long arg=(unsigned long) parg;
        int ret=0;
        
	DEB_EE(("av7110: %p\n",av7110));

        if ((file->f_flags&O_ACCMODE)==O_RDONLY) {
		if ( cmd!=VIDEO_GET_STATUS && cmd!=VIDEO_GET_EVENT && 
		     cmd!=VIDEO_GET_SIZE ) {
                return -EPERM;
		}
	}
        
        switch (cmd) {
        case VIDEO_STOP:
                av7110->videostate.play_state=VIDEO_STOPPED;
                if (av7110->videostate.stream_source==VIDEO_SOURCE_MEMORY)
                        AV_Stop(av7110, RP_VIDEO);
                else
                        vidcom(av7110, 0x000e, 
                               av7110->videostate.video_blank ? 0 : 1);
                av7110->trickmode=TRICK_NONE;
                break; 
                
        case VIDEO_PLAY:
                av7110->trickmode=TRICK_NONE;
                if (av7110->videostate.play_state==VIDEO_FREEZED) {
                        av7110->videostate.play_state=VIDEO_PLAYING;
                        vidcom(av7110, 0x000d, 0);
                } 
                
                if (av7110->videostate.stream_source==VIDEO_SOURCE_MEMORY) {
                        if (av7110->playing==RP_AV) {
                                outcom(av7110, COMTYPE_REC_PLAY, __Stop, 0);
                                av7110->playing&=~RP_VIDEO;
                        }
                        AV_StartPlay(av7110,RP_VIDEO);
                        vidcom(av7110, 0x000d, 0);
                } else {
                        //AV_Stop(av7110, RP_VIDEO);
                        vidcom(av7110, 0x000d, 0);
                }
                av7110->videostate.play_state=VIDEO_PLAYING;
                break;
                
        case VIDEO_FREEZE:
                av7110->videostate.play_state=VIDEO_FREEZED;
                if (av7110->playing&RP_VIDEO) 
                        outcom(av7110, COMTYPE_REC_PLAY, __Pause, 0);
                else
                        vidcom(av7110, 0x0102, 1);
                av7110->trickmode=TRICK_FREEZE;
                break;
                
        case VIDEO_CONTINUE:
                if (av7110->playing&RP_VIDEO) 
                        outcom(av7110, COMTYPE_REC_PLAY, __Continue, 0);
                vidcom(av7110, 0x000d, 0);
                av7110->videostate.play_state=VIDEO_PLAYING;
                av7110->trickmode=TRICK_NONE;
                break;
                
        case VIDEO_SELECT_SOURCE:
                av7110->videostate.stream_source=(video_stream_source_t) arg;
                break;
                
        case VIDEO_SET_BLANK:
                av7110->videostate.video_blank=(int) arg;
		break;
                
        case VIDEO_GET_STATUS:
                memcpy(parg, &av7110->videostate, sizeof(struct video_status));
                break;
                
        case VIDEO_GET_EVENT:
                ret=dvb_video_get_event(av7110, parg, file->f_flags);
		break;

	case VIDEO_GET_SIZE:
                memcpy(parg, &av7110->video_size, sizeof(video_size_t));
		break;
                
        case VIDEO_SET_DISPLAY_FORMAT:
        {
                video_displayformat_t format=(video_displayformat_t) arg;
                u16 val=0;
                
                switch(format) {
                case VIDEO_PAN_SCAN:
                        val=VID_PAN_SCAN_PREF;
                        break;
                        
                case VIDEO_LETTER_BOX:
                        val=VID_VC_AND_PS_PREF;
                        break;
                        
                case VIDEO_CENTER_CUT_OUT:
                        val=VID_CENTRE_CUT_PREF;
                        break;
                        
                default:
                        ret=-EINVAL;
                        break;
                }
                if (ret<0)
                        break;
                av7110->videostate.video_format=format;
                ret=outcom(av7110, COMTYPE_ENCODER, SetPanScanType, 
                           1, (u16) val);
                break;
        }
        
        case VIDEO_SET_FORMAT:
                if (arg>1) {
                        ret=-EINVAL;
                        break;
                }
                av7110->display_ar=arg;
                ret=outcom(av7110, COMTYPE_ENCODER, SetMonitorType, 
                           1, (u16) arg);
                break;
                
        case VIDEO_STILLPICTURE:
        { 
                struct video_still_picture *pic=
                        (struct video_still_picture *) parg;
                av7110->videostate.stream_source = VIDEO_SOURCE_MEMORY;
                dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
                ret = play_iframe(av7110, pic->iFrame, pic->size,
                            file->f_flags&O_NONBLOCK);
                break;
        }
        
        case VIDEO_FAST_FORWARD:
                //note: arg is ignored by firmware
                if (av7110->playing&RP_VIDEO) 
                        outcom(av7110, COMTYPE_REC_PLAY, 
                               __Scan_I, 2, AV_PES, 0);
                else 
                        vidcom(av7110, 0x16, arg); 
                av7110->trickmode=TRICK_FAST;
                av7110->videostate.play_state=VIDEO_PLAYING;
                break;
                
        case VIDEO_SLOWMOTION:
                if (av7110->playing&RP_VIDEO) {
                        outcom(av7110, COMTYPE_REC_PLAY, __Slow, 2, 0, 0);
                        vidcom(av7110, 0x22, arg);
                } else {
                        vidcom(av7110, 0x0d, 0);
                        vidcom(av7110, 0x0e, 0);
                        vidcom(av7110, 0x22, arg);
                }
                av7110->trickmode=TRICK_SLOW;
                av7110->videostate.play_state=VIDEO_PLAYING;
                break;
                
        case VIDEO_GET_CAPABILITIES:
                *(int *)parg=VIDEO_CAP_MPEG1|
                        VIDEO_CAP_MPEG2|
                        VIDEO_CAP_SYS|
                        VIDEO_CAP_PROG;
                break;
        
        case VIDEO_CLEAR_BUFFER:
                dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
                av7110_ipack_reset(&av7110->ipack[1]);
                
                if (av7110->playing==RP_AV) {
                        outcom(av7110, COMTYPE_REC_PLAY, 
                               __Play, 2, AV_PES, 0);
                        if (av7110->trickmode==TRICK_FAST)
                                outcom(av7110, COMTYPE_REC_PLAY, 
                                       __Scan_I, 2, AV_PES, 0);
                        if (av7110->trickmode==TRICK_SLOW) {
                                outcom(av7110, COMTYPE_REC_PLAY, __Slow, 2, 0, 0);
                                vidcom(av7110, 0x22, arg);
                        }
                        if (av7110->trickmode==TRICK_FREEZE)
                                vidcom(av7110, 0x000e, 1);
                }
                break;
                
        case VIDEO_SET_STREAMTYPE:
                
                break;
                
        default:
                ret=-ENOIOCTLCMD;
                break;
        }
        return ret;
}

static int dvb_audio_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        unsigned long arg=(unsigned long) parg;
        int ret=0;

	DEB_EE(("av7110: %p\n",av7110));

        if (((file->f_flags&O_ACCMODE)==O_RDONLY) &&
            (cmd!=AUDIO_GET_STATUS))
                return -EPERM;
        
        switch (cmd) {
        case AUDIO_STOP:
                if (av7110->audiostate.stream_source==AUDIO_SOURCE_MEMORY)
                        AV_Stop(av7110, RP_AUDIO);
                else
                        audcom(av7110, 1);
                av7110->audiostate.play_state=AUDIO_STOPPED;
                break;
                
        case AUDIO_PLAY:
                if (av7110->audiostate.stream_source==AUDIO_SOURCE_MEMORY)
                        AV_StartPlay(av7110, RP_AUDIO);
                audcom(av7110, 2);
                av7110->audiostate.play_state=AUDIO_PLAYING;
                break;
                
        case AUDIO_PAUSE:
                audcom(av7110, 1);
                av7110->audiostate.play_state=AUDIO_PAUSED;
                break;
                
        case AUDIO_CONTINUE:
                if (av7110->audiostate.play_state==AUDIO_PAUSED) {
                        av7110->audiostate.play_state=AUDIO_PLAYING;
                        audcom(av7110, 0x12);
                } 
                break;
                
        case AUDIO_SELECT_SOURCE:
                av7110->audiostate.stream_source=(audio_stream_source_t) arg;
                break;
                
        case AUDIO_SET_MUTE:
        {
                audcom(av7110, arg ? 1 : 2);
                av7110->audiostate.mute_state=(int) arg;
                break;
        }
        
        case AUDIO_SET_AV_SYNC:
                av7110->audiostate.AV_sync_state=(int) arg;
                audcom(av7110, arg ? 0x0f : 0x0e);
                break;
                
        case AUDIO_SET_BYPASS_MODE:
                ret=-EINVAL;
                break;
                
        case AUDIO_CHANNEL_SELECT:
                av7110->audiostate.channel_select=(audio_channel_select_t) arg;
                
                switch(av7110->audiostate.channel_select) {
                case AUDIO_STEREO:
                        audcom(av7110, 0x80);
                        break;
                        
                case AUDIO_MONO_LEFT:
                        audcom(av7110, 0x100);
                        break;
                        
                case AUDIO_MONO_RIGHT:
                        audcom(av7110, 0x200);
                        break;
                        
                default:
                        ret=-EINVAL;
                        break;
                }
                break;
                
        case AUDIO_GET_STATUS:
                memcpy(parg, &av7110->audiostate, sizeof(struct audio_status));
                break;
                
        case AUDIO_GET_CAPABILITIES:
                *(int *)parg=AUDIO_CAP_LPCM|
                        AUDIO_CAP_MP1|
                        AUDIO_CAP_MP2;
                break;

        case AUDIO_CLEAR_BUFFER:
                dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
                av7110_ipack_reset(&av7110->ipack[0]);
                if (av7110->playing==RP_AV)
                        outcom(av7110, COMTYPE_REC_PLAY, 
                               __Play, 2, AV_PES, 0);
                break;
        case AUDIO_SET_ID:
                
                break;
        case AUDIO_SET_MIXER:
        {
                struct audio_mixer *amix=(struct audio_mixer *)parg;

                SetVolume(av7110, amix->volume_left, amix->volume_right);
                break;
        }
        case AUDIO_SET_STREAMTYPE:
                break;
        default:
                ret=-ENOIOCTLCMD;
                break;
        }
        return ret;
}


static int dvb_video_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        int err;

	DEB_EE(("av7110: %p\n",av7110));

        if ((err=dvb_generic_open(inode, file))<0)
                return err;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
        dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
        dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
        av7110->video_blank=1;
        av7110->audiostate.AV_sync_state=1;
        av7110->videostate.stream_source=VIDEO_SOURCE_DEMUX;

		/*  empty event queue */
		av7110->video_events.eventr = av7110->video_events.eventw = 0;
	}

        return 0;
}

static int dvb_video_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;

	DEB_EE(("av7110: %p\n",av7110));

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
        AV_Stop(av7110, RP_VIDEO);
	}

        return dvb_generic_release(inode, file);
}

static int dvb_audio_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        int err=dvb_generic_open(inode, file);

	DEB_EE(("av7110: %p\n",av7110));

        if (err<0)
                return err;
        dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
        av7110->audiostate.stream_source=AUDIO_SOURCE_DEMUX;
        return 0;
}

static int dvb_audio_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
        struct av7110 *av7110=(struct av7110 *) dvbdev->priv;
        
	DEB_EE(("av7110: %p\n",av7110));

        AV_Stop(av7110, RP_AUDIO);
        return dvb_generic_release(inode, file);
}



/******************************************************************************
 * driver registration 
 ******************************************************************************/

static struct file_operations dvb_video_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_video_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_video_open,
	.release	= dvb_video_release,
	.poll		= dvb_video_poll,
};

static struct dvb_device dvbdev_video = {
	.priv		= 0,
	.users		= 6,
	.readers	= 5,	/* arbitrary */
	.writers	= 1,
	.fops		= &dvb_video_fops,
	.kernel_ioctl	= dvb_video_ioctl,
};

static struct file_operations dvb_audio_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_audio_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_audio_open,
	.release	= dvb_audio_release,
	.poll		= dvb_audio_poll,
};

static struct dvb_device dvbdev_audio = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_audio_fops,
	.kernel_ioctl	= dvb_audio_ioctl,
};

static struct file_operations dvb_ca_fops = {
	.owner		= THIS_MODULE,
	.read		= dvb_ca_read,
	.write		= dvb_ca_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_ca_open,
	.release	= dvb_generic_release,
	.poll		= dvb_ca_poll,
};

static struct dvb_device dvbdev_ca = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_ca_fops,
	.kernel_ioctl	= dvb_ca_ioctl,
};


static void av7110_before_after_tune (fe_status_t s, void *data)
{
	struct av7110 *av7110 = data;

	DEB_EE(("av7110: %p\n",av7110));

        av7110->fe_synced = (s & FE_HAS_LOCK) ? 1 : 0;

        if (av7110->playing)
                return;

        if (down_interruptible(&av7110->pid_mutex))
                return;

	if (av7110->fe_synced) {
                SetPIDs(av7110, av7110->pids[DMX_PES_VIDEO], 
                        av7110->pids[DMX_PES_AUDIO], 
                        av7110->pids[DMX_PES_TELETEXT], 0, 
                        av7110->pids[DMX_PES_PCR]);
        	outcom(av7110, COMTYPE_PIDFILTER, Scan, 0);
	} else 
		SetPIDs(av7110, 0, 0, 0, 0, 0);

        up(&av7110->pid_mutex);
}


static int av7110_register(struct av7110 *av7110)
{
        int ret, i;
        struct dvb_demux *dvbdemux=&av7110->demux;

	DEB_EE(("av7110: %p\n",av7110));

        if (av7110->registered)
                return -1;

        av7110->registered=1;

	dvb_add_frontend_notifier (av7110->dvb_adapter,
				   av7110_before_after_tune, av7110);

	/**
	 *   init DiSEqC stuff
	 */
	dvb_add_frontend_ioctls (av7110->dvb_adapter,
				 av7110_diseqc_ioctl, NULL, av7110);

	av7110->audiostate.AV_sync_state=0;
	av7110->audiostate.mute_state=0;
	av7110->audiostate.play_state=AUDIO_STOPPED;
	av7110->audiostate.stream_source=AUDIO_SOURCE_DEMUX;
	av7110->audiostate.channel_select=AUDIO_STEREO;
	av7110->audiostate.bypass_mode=0;

	av7110->videostate.video_blank=0;
	av7110->videostate.play_state=VIDEO_STOPPED;
	av7110->videostate.stream_source=VIDEO_SOURCE_DEMUX;
	av7110->videostate.video_format=VIDEO_FORMAT_4_3;
	av7110->videostate.display_format=VIDEO_CENTER_CUT_OUT;
        av7110->display_ar=VIDEO_FORMAT_4_3;

        dvbdemux->priv = (void *) av7110;

	for (i=0; i<32; i++)
		av7110->handle2filter[i]=NULL;

	dvbdemux->filternum = 32;
	dvbdemux->feednum = 32;
	dvbdemux->start_feed = av7110_start_feed;
	dvbdemux->stop_feed = av7110_stop_feed;
	dvbdemux->write_to_decoder = av7110_write_to_decoder;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING | DMX_SECTION_FILTERING |
				      DMX_MEMORY_BASED_FILTERING);

	dvb_dmx_init(&av7110->demux);
	av7110->demux.dmx.get_stc = dvb_get_stc;

	av7110->dmxdev.filternum = 32;
	av7110->dmxdev.demux = &dvbdemux->dmx;
	av7110->dmxdev.capabilities = 0;
        
	dvb_dmxdev_init(&av7110->dmxdev, av7110->dvb_adapter);

        av7110->hw_frontend.source = DMX_FRONTEND_0;

        ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &av7110->hw_frontend);

        if (ret < 0)
                return ret;
        
        av7110->mem_frontend.source = DMX_MEMORY_FE;

	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, &av7110->mem_frontend);

	if (ret < 0)
                return ret;
        
        ret = dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, 
					     &av7110->hw_frontend);
        if (ret < 0)
                return ret;

	init_waitqueue_head(&av7110->video_events.wait_queue);
	spin_lock_init(&av7110->video_events.lock);
	av7110->video_events.eventw = av7110->video_events.eventr = 0;
	av7110->video_events.overflow = 0;
	memset(&av7110->video_size, 0, sizeof (video_size_t));

	dvb_register_device(av7110->dvb_adapter, &av7110->video_dev,
			    &dvbdev_video, av7110, DVB_DEVICE_VIDEO);
          
	dvb_register_device(av7110->dvb_adapter, &av7110->audio_dev,
			    &dvbdev_audio, av7110, DVB_DEVICE_AUDIO);
  
	dvb_register_device(av7110->dvb_adapter, &av7110->ca_dev,  
                                    &dvbdev_ca, av7110, DVB_DEVICE_CA);
#ifdef CONFIG_DVB_AV7110_OSD
	dvb_register_device(av7110->dvb_adapter, &av7110->osd_dev,
			    &dvbdev_osd, av7110, DVB_DEVICE_OSD);
#endif
#ifdef USE_DVB_DSP
                dvb->dsp_dev = dvb_register_dsp(dvb_audio_open, 
                                                dvb_audio_release, 
                                                dvb_audio_ioctl, 
                                                dvb_audio_write, 
                                                av7110->audio_dev);
#endif
//        }
        
        dvb_net_init(av7110->dvb_adapter, &av7110->dvb_net, &dvbdemux->dmx);

	return 0;
}


static void dvb_unregister(struct av7110 *av7110)
{
        struct dvb_demux *dvbdemux=&av7110->demux;

	DEB_EE(("av7110: %p\n",av7110));

        if (!av7110->registered)
                return;

	dvb_net_release(&av7110->dvb_net);

	dvbdemux->dmx.close(&dvbdemux->dmx);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->hw_frontend);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->mem_frontend);

        dvb_dmxdev_release(&av7110->dmxdev);
        dvb_dmx_release(&av7110->demux);

	dvb_remove_frontend_notifier (av7110->dvb_adapter,
				      av7110_before_after_tune);

	dvb_remove_frontend_ioctls (av7110->dvb_adapter,
				    av7110_diseqc_ioctl, NULL);

	dvb_unregister_device(av7110->audio_dev);
	dvb_unregister_device(av7110->video_dev);
	dvb_unregister_device(av7110->osd_dev);
	dvb_unregister_device(av7110->ca_dev);
#ifdef USE_DVB_DSP
	dvb_unregister_dsp(av7110->dsp_dev);
#endif
//        }
}

static int master_xfer (struct dvb_i2c_bus *i2c, const struct i2c_msg msgs[], int num)
{
	struct saa7146_dev *dev = i2c->data;
	return saa7146_i2c_transfer(dev, msgs, num, 6);
}

/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/

struct saa7146_extension_ioctls ioctls[] = {
	{ VIDIOC_ENUMINPUT, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_FREQUENCY,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_FREQUENCY, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_TUNER, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_TUNER, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_AUDIO,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_AUDIO,	SAA7146_EXCLUSIVE },
	{ 0, 0 }
};

static u8 saa7113_init_regs[] = {
	0x02, 0xd0,
	0x03, 0x23,
	0x04, 0x00,
	0x05, 0x00,
	0x06, 0xe9,
	0x07, 0x0d,
	0x08, 0x98,
	0x09, 0x02,
	0x0a, 0x80,
	0x0b, 0x40,
	0x0c, 0x40,
	0x0d, 0x00,
	0x0e, 0x01,
	0x0f, 0x7c,
	0x10, 0x48,
	0x11, 0x0c,
	0x12, 0x8b,
	0x13, 0x1a,
	0x14, 0x00,
	0x15, 0x00,
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1b, 0x00,
	0x1c, 0x00,
	0x1d, 0x00,
	0x1e, 0x00,

	0x41, 0x77,
	0x42, 0x77,
	0x43, 0x77,
	0x44, 0x77,
	0x45, 0x77,
	0x46, 0x77,
	0x47, 0x77,
	0x48, 0x77,
	0x49, 0x77,
	0x4a, 0x77,
	0x4b, 0x77,
	0x4c, 0x77,
	0x4d, 0x77,
	0x4e, 0x77,
	0x4f, 0x77,
	0x50, 0x77,
	0x51, 0x77,
	0x52, 0x77,
	0x53, 0x77,
	0x54, 0x77,
	0x55, 0x77,
	0x56, 0x77,
	0x57, 0xff,
	
	0xff
};


static struct saa7146_ext_vv av7110_vv_data_st;
static struct saa7146_ext_vv av7110_vv_data_c;

static int av7110_attach (struct saa7146_dev* dev, struct saa7146_pci_extension_data *pci_ext)
{
	struct av7110 *av7110 = NULL;
	int ret = 0;
	
	if (!(av7110 = kmalloc (sizeof (struct av7110), GFP_KERNEL))) {
		printk ("%s: out of memory!\n", __FUNCTION__);
		return -ENOMEM;
	}

	memset(av7110, 0, sizeof(struct av7110));

	av7110->card_name = (char*)pci_ext->ext_priv;
	(struct av7110*)dev->ext_priv = av7110;

	DEB_EE(("dev: %p, av7110: %p\n",dev,av7110));

	av7110->dev=(struct saa7146_dev *)dev;
	dvb_register_adapter(&av7110->dvb_adapter, av7110->card_name);

	/* the Siemens DVB needs this if you want to have the i2c chips
	   get recognized before the main driver is fully loaded */
	saa7146_write(dev, GPIO_CTRL, 0x500000);

	saa7146_i2c_adapter_prepare(dev, NULL, SAA7146_I2C_BUS_BIT_RATE_3200);

	av7110->i2c_bus = dvb_register_i2c_bus (master_xfer, dev,
						av7110->dvb_adapter, 0);

	if (!av7110->i2c_bus) {
		dvb_unregister_adapter (av7110->dvb_adapter);
		kfree(av7110);
		return -ENOMEM;
	}

	ttpci_eeprom_parse_mac(av7110->i2c_bus);

	saa7146_write(dev, PCI_BT_V1, 0x1c00101f);
	saa7146_write(dev, BCS_CTRL, 0x80400040);

	/* set dd1 stream a & b */
      	saa7146_write(dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(dev, DD1_INIT, 0x02000000);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	/* upload all */
	saa7146_write(dev, MC2, 0x077c077c);
        saa7146_write(dev, GPIO_CTRL, 0x000000);

	tasklet_init (&av7110->debi_tasklet, debiirq, (unsigned long) av7110);
	tasklet_init (&av7110->gpio_tasklet, gpioirq, (unsigned long) av7110);

        sema_init(&av7110->pid_mutex, 1);

        /* locks for data transfers from/to AV7110 */
        spin_lock_init (&av7110->debilock);
        sema_init(&av7110->dcomlock, 1);
        av7110->debilock=SPIN_LOCK_UNLOCKED;
        av7110->debitype=-1;

        /* default OSD window */
        av7110->osdwin=1;

        /* ARM "watchdog" */
	init_waitqueue_head(&av7110->arm_wait);
        av7110->arm_thread=0;
     
        av7110->vidmode=VIDEO_MODE_PAL;

        av7110_ipack_init(&av7110->ipack[0], IPACKS, play_audio_cb);
        av7110->ipack[0].data=(void *) av7110;
        av7110_ipack_init(&av7110->ipack[1], IPACKS, play_video_cb);
        av7110->ipack[1].data=(void *) av7110;
        

        /* allocate and init buffers */
        av7110->debi_virt = pci_alloc_consistent(dev->pci, 8192,
						 &av7110->debi_bus);
	if (!av7110->debi_virt) {
		ret = -ENOMEM;
                goto err;
	}

        av7110->iobuf = vmalloc(AVOUTLEN+AOUTLEN+BMPLEN+4*IPACKS);
	if (!av7110->iobuf) {
		ret = -ENOMEM;
                goto err;
	}

        dvb_ringbuffer_init(&av7110->avout, av7110->iobuf, AVOUTLEN);
        dvb_ringbuffer_init(&av7110->aout, av7110->iobuf+AVOUTLEN, AOUTLEN);

        /* init BMP buffer */
        av7110->bmpbuf=av7110->iobuf+AVOUTLEN+AOUTLEN;
        init_waitqueue_head(&av7110->bmpq);
        
        av7110->kbuf[0]=(u8 *)(av7110->iobuf+AVOUTLEN+AOUTLEN+BMPLEN);
        av7110->kbuf[1]=av7110->kbuf[0]+2*IPACKS;

        /* CI link layer buffers */
        ci_ll_init(&av7110->ci_rbuffer, &av7110->ci_wbuffer, 8192);

        /* handle different card types */

        /* load firmware into AV7110 cards */

	bootarm(av7110);
	firmversion(av7110);

	if (FW_VERSION(av7110->arm_app)<0x2501)
		printk ("av7110: Warning, firmware version 0x%04x is too old. "
			"System might be unstable!\n", FW_VERSION(av7110->arm_app));

	kernel_thread(arm_thread, (void *) av7110, 0);

	/* set internal volume control to maximum */
	av7110->adac_type = DVB_ADAC_TI;
	SetVolume(av7110, 0xff, 0xff);

	VidMode(av7110, vidmode);

	/* remaining inits according to card and frontend type */
	av7110->has_analog_tuner = 0;
	av7110->current_input = 0;
	if (i2c_writereg(av7110, 0x20, 0x00, 0x00)==1) {
		printk ("av7110(%d): Crystal audio DAC detected\n",
			av7110->dvb_adapter->num);
		av7110->adac_type = DVB_ADAC_CRYSTAL;
		i2c_writereg(av7110, 0x20, 0x01, 0xd2);
		i2c_writereg(av7110, 0x20, 0x02, 0x49);
		i2c_writereg(av7110, 0x20, 0x03, 0x00);
		i2c_writereg(av7110, 0x20, 0x04, 0x00);
	
	/**
	 * some special handling for the Siemens DVB-C cards...
	 */
	} else if (i2c_writereg(av7110, 0x80, 0x0, 0x80) == 1
			&& i2c_writereg(av7110, 0x80, 0x0, 0) == 1) {
		u16 version1, version2;
		printk ("av7110(%d): DVB-C analog module detected, "
			"initializing MSP3400\n",
			av7110->dvb_adapter->num);
		av7110->adac_type = DVB_ADAC_MSP;
		dvb_delay(100); // the probing above resets the msp...
		msp_readreg(av7110, MSP_RD_DSP, 0x001e, &version1);
		msp_readreg(av7110, MSP_RD_DSP, 0x001f, &version2);
		printk("av7110(%d): MSP3400 version 0x%04x 0x%04x\n",
			av7110->dvb_adapter->num, version1, version2);
		msp_writereg(av7110, MSP_WR_DSP, 0x0013, 0x0c00);
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x7f00); // loudspeaker + headphone
		msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0220); // loudspeaker source
		msp_writereg(av7110, MSP_WR_DSP, 0x0009, 0x0220); // headphone source
		msp_writereg(av7110, MSP_WR_DSP, 0x0004, 0x7f00); // loudspeaker volume
		msp_writereg(av7110, MSP_WR_DSP, 0x000a, 0x0220); // SCART 1 source
		msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x7f00); // SCART 1 volume
		msp_writereg(av7110, MSP_WR_DSP, 0x000d, 0x4800); // prescale SCART
		
		if (i2c_writereg(av7110, 0x48, 0x01, 0x00)!=1) {
			INFO(("saa7113 not accessible.\n"));
		}
		else {
			u8 *i = saa7113_init_regs;
			av7110->has_analog_tuner = 1;
			/* init the saa7113 */
			while (*i != 0xff) {
				if (i2c_writereg(av7110, 0x48, i[0], i[1]) != 1) {
					printk("av7110(%d): saa7113 initialization failed",
							av7110->dvb_adapter->num);
					break;
				}
				i += 2;
			}
			/* setup msp for analog sound: B/G Dual-FM */
			msp_writereg(av7110, MSP_WR_DEM, 0x00bb, 0x02d0); // AD_CV
			msp_writereg(av7110, MSP_WR_DEM, 0x0001,  3); // FIR1
			msp_writereg(av7110, MSP_WR_DEM, 0x0001, 18); // FIR1
			msp_writereg(av7110, MSP_WR_DEM, 0x0001, 27); // FIR1
			msp_writereg(av7110, MSP_WR_DEM, 0x0001, 48); // FIR1
			msp_writereg(av7110, MSP_WR_DEM, 0x0001, 66); // FIR1
			msp_writereg(av7110, MSP_WR_DEM, 0x0001, 72); // FIR1
			msp_writereg(av7110, MSP_WR_DEM, 0x0005,  4); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005, 64); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005,  0); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005,  3); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005, 18); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005, 27); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005, 48); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005, 66); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0005, 72); // FIR2
			msp_writereg(av7110, MSP_WR_DEM, 0x0083, 0xa000); // MODE_REG
			msp_writereg(av7110, MSP_WR_DEM, 0x0093, 0x00aa); // DCO1_LO 5.74MHz
			msp_writereg(av7110, MSP_WR_DEM, 0x009b, 0x04fc); // DCO1_HI
			msp_writereg(av7110, MSP_WR_DEM, 0x00a3, 0x038e); // DCO2_LO 5.5MHz
			msp_writereg(av7110, MSP_WR_DEM, 0x00ab, 0x04c6); // DCO2_HI
			msp_writereg(av7110, MSP_WR_DEM, 0x0056, 0); // LOAD_REG 1/2
		}	

		memcpy(standard,dvb_standard,sizeof(struct saa7146_standard)*2);
		/* set dd1 stream a & b */
      		saa7146_write(dev, DD1_STREAM_B, 0x00000000);
		saa7146_write(dev, DD1_INIT, 0x02000700);
		saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));
	}
	else if (dev->pci->subsystem_vendor == 0x110a) {
		printk("av7110(%d): DVB-C w/o analog module detected\n",
			av7110->dvb_adapter->num);
		av7110->adac_type = DVB_ADAC_NONE;
	}
	else {
		av7110->adac_type = adac;
		printk("av7110(%d): adac type set to %d\n",
			av7110->dvb_adapter->num, av7110->adac_type);
		}

	if (av7110->adac_type == DVB_ADAC_NONE || av7110->adac_type == DVB_ADAC_MSP) {
		// switch DVB SCART on
		outcom(av7110, COMTYPE_AUDIODAC, MainSwitch, 1, 0);
		outcom(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, 1);
		//saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI); // RGB on, SCART pin 16
		//saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO); // SCARTpin 8
	}
	
	SetVolume(av7110, 0xff, 0xff);

	av7110_setup_irc_config (av7110, 0);
	av7110_register(av7110);
	
	/* special case DVB-C: these cards have an analog tuner
	   plus need some special handling, so we have separate
	   saa7146_ext_vv data for these... */
	if (0 != av7110->has_analog_tuner) {
		ret = saa7146_vv_init(dev, &av7110_vv_data_c);
	} else {
		ret = saa7146_vv_init(dev, &av7110_vv_data_st);
	}
	
	if ( 0 != ret) {
		ERR(("cannot init capture device. skipping.\n"));
		ret = -ENODEV;
		goto err;
	}

	if (saa7146_register_device(&av7110->v4l_dev, dev, "av7110", VFL_TYPE_GRABBER)) {
		ERR(("cannot register capture device. skipping.\n"));
		ret = -ENODEV;
		goto video_err;
	}
	
	if (0 != av7110->has_analog_tuner) {
		if( 0 != saa7146_register_device(&av7110->vbi_dev, dev, "av7110", VFL_TYPE_VBI)) {
			ERR(("cannot register vbi v4l2 device. skipping.\n"));
		}
		/* we use this to remember that this dvb-c card cannot do vbi */
		av7110->has_analog_tuner = 2;
	}	

	printk(KERN_INFO "av7110: found av7110-%d.\n",av7110_num);
	av7110_num++;
        return 0;

video_err:
	saa7146_vv_release(dev);

err:
	if (NULL != av7110 ) {
		kfree(av7110);
	}
	if (NULL != av7110->debi_virt) {
		pci_free_consistent(dev->pci, 8192, av7110->debi_virt, av7110->debi_bus);
	}
	if (NULL != av7110->iobuf) {
		vfree(av7110->iobuf);
	}

	dvb_unregister_i2c_bus (master_xfer,av7110->i2c_bus->adapter,
				av7110->i2c_bus->id);

	dvb_unregister_adapter (av7110->dvb_adapter);

	return ret;
}

static int av7110_detach (struct saa7146_dev* saa)
{
	struct av7110 *av7110 = (struct av7110*)saa->ext_priv;
	DEB_EE(("av7110: %p\n",av7110));

	saa7146_unregister_device(&av7110->v4l_dev, saa);
	if (2 == av7110->has_analog_tuner) {
		saa7146_unregister_device(&av7110->vbi_dev, saa);
	}	

	av7110->arm_rmmod=1;
	wake_up_interruptible(&av7110->arm_wait);

	while (av7110->arm_thread)
		dvb_delay(1);

	dvb_unregister(av7110);
	
	IER_DISABLE(saa, (MASK_19 | MASK_03));
//	saa7146_write (av7110->dev, IER, 
//		     saa7146_read(av7110->dev, IER) & ~(MASK_19 | MASK_03));
	
	saa7146_write(av7110->dev, ISR,(MASK_19 | MASK_03));

	ci_ll_release(&av7110->ci_rbuffer, &av7110->ci_wbuffer);
	av7110_ipack_free(&av7110->ipack[0]);
	av7110_ipack_free(&av7110->ipack[1]);
	vfree(av7110->iobuf);
	pci_free_consistent(saa->pci, 8192, av7110->debi_virt,
			    av7110->debi_bus);

	dvb_unregister_i2c_bus (master_xfer,av7110->i2c_bus->adapter, av7110->i2c_bus->id);
	dvb_unregister_adapter (av7110->dvb_adapter);

	kfree (av7110);

	saa->ext_priv = NULL;
	av7110_num--;
	
	return 0;
}


static void av7110_irq(struct saa7146_dev* dev, u32 *isr) 
{
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;

	DEB_INT(("dev: %p, av7110: %p\n",dev,av7110));

	if (*isr & MASK_19)
		tasklet_schedule (&av7110->debi_tasklet);
	
	if (*isr & MASK_03)
		tasklet_schedule (&av7110->gpio_tasklet);
}


/* FIXME: these values are experimental values that look better than the
   values from the latest "official" driver -- at least for me... (MiHu) */
static struct saa7146_standard standard[] = {
	{
		.name	= "PAL", 	.id		= V4L2_STD_PAL_BG,
		.v_offset	= 0x15,	.v_field 	= 288,		.v_calc	= 576,
		.h_offset	= 0x4a,	.h_pixels 	= 708,		.h_calc	= 709,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC", 	.id		= V4L2_STD_NTSC,
		.v_offset	= 0x10,	.v_field 	= 244,		.v_calc	= 480,
		.h_offset	= 0x40,	.h_pixels 	= 708,		.h_calc	= 709,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static struct saa7146_standard analog_standard[] = {
	{
		.name	= "PAL", 	.id		= V4L2_STD_PAL_BG,
		.v_offset	= 0x18,	.v_field 	= 288,		.v_calc	= 576,
		.h_offset	= 0x08,	.h_pixels 	= 708,		.h_calc	= 709,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC", 	.id		= V4L2_STD_NTSC,
		.v_offset	= 0x10,	.v_field 	= 244,		.v_calc	= 480,
		.h_offset	= 0x40,	.h_pixels 	= 708,		.h_calc	= 709,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static struct saa7146_standard dvb_standard[] = {
	{
		.name	= "PAL", 	.id		= V4L2_STD_PAL_BG,
		.v_offset	= 0x14,	.v_field 	= 288,		.v_calc	= 576,
		.h_offset	= 0x4a,	.h_pixels 	= 708,		.h_calc	= 709,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC", 	.id		= V4L2_STD_NTSC,
		.v_offset	= 0x10,	.v_field 	= 244,		.v_calc	= 480,
		.h_offset	= 0x40,	.h_pixels 	= 708,		.h_calc	= 709,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static struct saa7146_extension av7110_extension;

#define MAKE_AV7110_INFO(x_var,x_name) \
static struct saa7146_pci_extension_data x_var = { \
	.ext_priv = x_name, \
	.ext = &av7110_extension }

MAKE_AV7110_INFO(fs_1_5, "Siemens cable card PCI rev1.5");
MAKE_AV7110_INFO(fs_1_3, "Siemens/Technotrend/Hauppauge PCI rev1.3");
MAKE_AV7110_INFO(tt_1_6, "Technotrend/Hauppauge PCI rev1.3 or 1.6");
MAKE_AV7110_INFO(tt_2_1, "Technotrend/Hauppauge PCI rev2.1");
MAKE_AV7110_INFO(tt_t,	 "Technotrend/Hauppauge PCI DVB-T");
MAKE_AV7110_INFO(unkwn0, "Technotrend/Hauppauge PCI rev?(unknown0)?");
MAKE_AV7110_INFO(unkwn1, "Technotrend/Hauppauge PCI rev?(unknown1)?");
MAKE_AV7110_INFO(unkwn2, "Technotrend/Hauppauge PCI rev?(unknown2)?");
MAKE_AV7110_INFO(nexus,  "Technotrend/Hauppauge Nexus PCI DVB-S");
MAKE_AV7110_INFO(dvboc11,"Octal/Technotrend DVB-C for iTV");

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(fs_1_5, 0x110a, 0xffff),
	MAKE_EXTENSION_PCI(fs_1_5, 0x110a, 0x0000),
	MAKE_EXTENSION_PCI(fs_1_3, 0x13c2, 0x0000),
	MAKE_EXTENSION_PCI(unkwn0, 0x13c2, 0x1002),
	MAKE_EXTENSION_PCI(tt_1_6, 0x13c2, 0x0001),
	MAKE_EXTENSION_PCI(tt_2_1, 0x13c2, 0x0002),
	MAKE_EXTENSION_PCI(tt_2_1, 0x13c2, 0x0003),
	MAKE_EXTENSION_PCI(tt_2_1, 0x13c2, 0x0004),
	MAKE_EXTENSION_PCI(tt_1_6, 0x13c2, 0x0006),
	MAKE_EXTENSION_PCI(tt_t,   0x13c2, 0x0008),
	MAKE_EXTENSION_PCI(tt_2_1, 0x13c2, 0x1102),
	MAKE_EXTENSION_PCI(unkwn1, 0xffc2, 0x0000),
	MAKE_EXTENSION_PCI(unkwn2, 0x00a1, 0x00a1),
	MAKE_EXTENSION_PCI(nexus,  0x00a1, 0xa1a0),
	MAKE_EXTENSION_PCI(dvboc11,0x13c2, 0x000a),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static int std_callback(struct saa7146_dev* dev, struct saa7146_standard *std)
{
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;
	if (std->id == V4L2_STD_PAL) {
                av7110->vidmode = VIDEO_MODE_PAL;
		SetMode(av7110, av7110->vidmode);
	}
	else if (std->id == V4L2_STD_NTSC) {
		av7110->vidmode = VIDEO_MODE_NTSC;
		SetMode(av7110, av7110->vidmode);
	}
	else
		return -1;

	return 0;
}


static struct saa7146_ext_vv av7110_vv_data_st = {
	.inputs		= 1,
	.audios 	= 1,
	.capabilities	= 0,
	.flags		= SAA7146_EXT_SWAP_ODD_EVEN,

	.stds		= &standard[0],
	.num_stds	= sizeof(standard)/sizeof(struct saa7146_standard),
	.std_callback	= &std_callback, 

	.ioctls		= &ioctls[0],
	.ioctl		= av7110_ioctl,
};

static struct saa7146_ext_vv av7110_vv_data_c = {
	.inputs		= 1,
	.audios 	= 1,
	.capabilities	= V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE,
	.flags		= SAA7146_USE_PORT_B_FOR_VBI,

	.stds		= &standard[0],
	.num_stds	= sizeof(standard)/sizeof(struct saa7146_standard),
	.std_callback	= &std_callback, 

	.ioctls		= &ioctls[0],
	.ioctl		= av7110_ioctl,
};


static struct saa7146_extension av7110_extension = {
	.name		= "dvb\0",

	.module		= THIS_MODULE,
	.pci_tbl	= &pci_tbl[0],
	.attach		= av7110_attach,
	.detach		= av7110_detach,

	.irq_mask	= MASK_19|MASK_03,
	.irq_func	= av7110_irq,
};	


static int __init av7110_init(void) 
{
	int retval;
	retval = saa7146_register_extension(&av7110_extension);
	if (retval)
		goto failed_saa7146_register;
	
	retval = av7110_ir_init();
	if (retval)
		goto failed_av7110_ir_init;
	return 0;
failed_av7110_ir_init:
	saa7146_unregister_extension(&av7110_extension);
failed_saa7146_register:
	return retval;
}


static void __exit av7110_exit(void)
{
	av7110_ir_exit();
	saa7146_unregister_extension(&av7110_extension);
}

module_init(av7110_init);
module_exit(av7110_exit);

MODULE_DESCRIPTION("driver for the SAA7146 based AV110 PCI DVB cards by "
		   "Siemens, Technotrend, Hauppauge");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, others");
MODULE_LICENSE("GPL");

MODULE_PARM(av7110_debug,"i");
MODULE_PARM(vidmode,"i");
MODULE_PARM_DESC(vidmode,"analog video out: 0 off, 1 CVBS+RGB (default), 2 CVBS+YC, 3 YC");
MODULE_PARM(pids_off,"i");
MODULE_PARM_DESC(pids_off,"clear video/audio/PCR PID filters when demux is closed");
MODULE_PARM(adac,"i");
MODULE_PARM_DESC(adac,"audio DAC type: 0 TI, 1 CRYSTAL, 2 MSP (use if autodetection fails)");
MODULE_PARM(hw_sections, "i");
MODULE_PARM_DESC(hw_sections, "0 use software section filter, 1 use hardware");

