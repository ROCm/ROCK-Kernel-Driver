/*
 * driver for the SAA7146 based AV110 cards (like the Fujitsu-Siemens DVB)
 * av7110.c: initialization and demux stuff
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


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/byteorder/swabb.h>
#include <linux/smp_lock.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/crc32.h>

#include <asm/system.h>
#include <asm/semaphore.h>

#include <linux/dvb/frontend.h>

#include "dvb_i2c.h"
#include "dvb_frontend.h"
#include "dvb_functions.h"


	#define DEBUG_VARIABLE av7110_debug

#include "ttpci-eeprom.h"
#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_av.h"
#include "av7110_ca.h"
#include "av7110_ipack.h"


static void restart_feeds(struct av7110 *av7110);

int av7110_debug = 0;

static int vidmode=CVBS_RGB_OUT;
static int pids_off;
static int adac=DVB_ADAC_TI;
static int hw_sections = 0;
static int rgb_on = 0;

int av7110_num = 0;


static void recover_arm(struct av7110 *av7110)
{
	DEB_EE(("av7110: %p\n",av7110));

	av7110_bootarm(av7110);
        dvb_delay(100); 
        restart_feeds(av7110);
	av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetIR, 1, av7110->ir_config);
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
	unsigned long timeout;
        u16 newloops = 0;

	DEB_EE(("av7110: %p\n",av7110));
	
	dvb_kernel_thread_setup ("arm_mon");
	av7110->arm_thread = current;

	while (1) {
		timeout = wait_event_interruptible_timeout(av7110->arm_wait,0 != av7110->arm_rmmod, 5*HZ);
		if (-ERESTARTSYS == timeout || 0 != av7110->arm_rmmod) {
			/* got signal or told to quit*/
			break;
		}

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
		av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetIR, 1, ir_config);
		av7110->ir_config = ir_config;
	}
}

static void (*irc_handler)(u32);

void av7110_register_irc_handler(void (*func)(u32)) 
{
	DEB_EE(("registering %p\n", func));
        irc_handler = func;
}

void av7110_unregister_irc_handler(void (*func)(u32)) 
{
	DEB_EE(("unregistering %p\n", func));
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
			av7110_p2t_write(buffer1, buffer1_len,
                                  dvbdmxfilter->feed->pid, 
                                  &av7110->p2t_filter[dvbdmxfilter->index]);
	default:
	        return 0;
        }
}


//#define DEBUG_TIMING
static inline void print_time(char *s)
{
#ifdef DEBUG_TIMING
        struct timeval tv;
        do_gettimeofday(&tv);
        printk("%s: %d.%d\n", s, (int)tv.tv_sec, (int)tv.tv_usec);
#endif
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
		printk("DEBI irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",
		       jiffies, saa7146_read(av7110->dev, PSR),
		       saa7146_read(av7110->dev, SSR));
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
			av7110_record_cb(&av7110->p2t[handle],
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

static void gpioirq (unsigned long data)
{
	struct av7110 *av7110 = (struct av7110*) data;
        u32 rxbuf, txbuf;
        int len;
        
        //printk("GPIO0 irq\n");        

        if (av7110->debitype !=-1)
		printk("GPIO0 irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",
		       jiffies, saa7146_read(av7110->dev, PSR),
		       saa7146_read(av7110->dev, SSR));
       
        spin_lock(&av7110->debilock);

	ARM_ClearIrq(av7110);

        saa7146_write(av7110->dev, IER, 
                      saa7146_read(av7110->dev, IER) & ~MASK_19 );
        saa7146_write(av7110->dev, ISR, MASK_19 );

        av7110->debitype = irdebi(av7110, DEBINOSWAP, IRQ_STATE, 0, 2);
        av7110->debilen  = irdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
        rxbuf=irdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
        txbuf=irdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
	len = (av7110->debilen + 3) & ~3;

//        DEB_D(("GPIO0 irq %d %d\n", av7110->debitype, av7110->debilen));
        print_time("gpio");

//       DEB_D(("GPIO0 irq %02x\n", av7110->debitype&0xff));        
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
		saa7146_wait_for_debi_done(av7110->dev);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19 );
		if (len < 5)
			len = 5; /* we want a real DEBI DMA */
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
			len=av7110_pes_play(av7110->debi_virt, &av7110->aout, 2048);
                        spin_unlock(&av7110->aout.lock);
                }
                if (len<=0 && (av7110->debitype&0x200)
                        &&av7110->videostate.play_state!=VIDEO_FREEZED) {
                        spin_lock(&av7110->avout.lock);
			len=av7110_pes_play(av7110->debi_virt, &av7110->avout, 2048);
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
		saa7146_wait_for_debi_done(av7110->dev);
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
		saa7146_wait_for_debi_done(av7110->dev);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19 );
		if (len < 5)
			len = 5; /* we want a real DEBI DMA */
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
		}
		/* fall through */

        case DATA_TS_RECORD:
        case DATA_PES_RECORD:
		saa7146_wait_for_debi_done(av7110->dev);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19);
                irdebi(av7110, DEBISWAB, DPRAM_BASE+rxbuf, 0, len);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_DEBUG_MESSAGE:
		saa7146_wait_for_debi_done(av7110->dev);
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


#ifdef CONFIG_DVB_AV7110_OSD
static int dvb_osd_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;

	DEB_EE(("av7110: %p\n", av7110));

	if (cmd == OSD_SEND_CMD)
		return av7110_osd_cmd(av7110, (osd_cmd_t *) parg);

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


static inline int SetPIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
			  u16 subpid, u16 pcrpid)
        {
	DEB_EE(("av7110: %p\n", av7110));

	if (vpid == 0x1fff || apid == 0x1fff ||
	    ttpid == 0x1fff || subpid == 0x1fff || pcrpid == 0x1fff) {
		vpid = apid = ttpid = subpid = pcrpid = 0;
		av7110->pids[DMX_PES_VIDEO] = 0;
		av7110->pids[DMX_PES_AUDIO] = 0;
		av7110->pids[DMX_PES_TELETEXT] = 0;
		av7110->pids[DMX_PES_PCR] = 0;
	}

	return av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, MultiPID, 5,
			     pcrpid, vpid, apid, ttpid, subpid);
}

void ChangePIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
		u16 subpid, u16 pcrpid)
{
	DEB_EE(("av7110: %p\n", av7110));
        
	if (down_interruptible(&av7110->pid_mutex))
		return;

	if (!(vpid & 0x8000))
		av7110->pids[DMX_PES_VIDEO] = vpid;
	if (!(apid & 0x8000))
		av7110->pids[DMX_PES_AUDIO] = apid;
	if (!(ttpid & 0x8000))
		av7110->pids[DMX_PES_TELETEXT] = ttpid;
	if (!(pcrpid & 0x8000))
		av7110->pids[DMX_PES_PCR] = pcrpid;
	
	av7110->pids[DMX_PES_SUBTITLE] = 0;

	if (av7110->fe_synced) {
		pcrpid = av7110->pids[DMX_PES_PCR];
		SetPIDs(av7110, vpid, apid, ttpid, subpid, pcrpid);
        }

	up(&av7110->pid_mutex);
}


/******************************************************************************
 * hardware filter functions
 ******************************************************************************/

static int StartHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
	struct dvb_demux_feed *dvbdmxfeed = dvbdmxfilter->feed;
	struct av7110 *av7110 = (struct av7110 *) dvbdmxfeed->demux->priv;
	u16 buf[20];
	int ret, i;
	u16 handle;
//	u16 mode=0x0320;
	u16 mode=0xb96a;

	DEB_EE(("av7110: %p\n",av7110));

	if (dvbdmxfilter->type == DMX_TYPE_SEC) {
		if (hw_sections) {
			buf[4] = (dvbdmxfilter->filter.filter_value[0] << 8) |
				dvbdmxfilter->maskandmode[0];
			for (i = 3; i < 18; i++)
				buf[i + 4 - 2] =
					(dvbdmxfilter->filter.filter_value[i] << 8) |
					dvbdmxfilter->maskandmode[i];
			mode = 4;
                }
	} else if ((dvbdmxfeed->ts_type & TS_PACKET) &&
		   !(dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)) {
		av7110_p2t_init(&av7110->p2t_filter[dvbdmxfilter->index], dvbdmxfeed);
        }

	buf[0] = (COMTYPE_PID_FILTER << 8) + AddPIDFilter;
	buf[1] = 16;
	buf[2] = dvbdmxfeed->pid;
	buf[3] = mode;

	ret = av7110_fw_request(av7110, buf, 20, &handle, 1);
	if (ret < 0) {
		printk("StartHWFilter error\n");
		return ret;
        }

	av7110->handle2filter[handle] = dvbdmxfilter;
	dvbdmxfilter->hw_handle = handle;

	return ret;
                }

static int StopHWFilter(struct dvb_demux_filter *dvbdmxfilter)
{
	struct av7110 *av7110 = (struct av7110 *) dvbdmxfilter->feed->demux->priv;
	u16 buf[3];
	u16 answ[2];
	int ret;
	u16 handle;
        
	DEB_EE(("av7110: %p\n", av7110));

	handle = dvbdmxfilter->hw_handle;
	if (handle > 32) {
		DEB_S(("dvb: StopHWFilter tried to stop invalid filter %d.\n",
		       handle));
		DEB_S(("dvb: filter type = %d\n", dvbdmxfilter->type));
		return 0;
                }

	av7110->handle2filter[handle] = NULL;

	buf[0] = (COMTYPE_PID_FILTER << 8) + DelPIDFilter;
	buf[1] = 1;
	buf[2] = handle;
	ret = av7110_fw_request(av7110, buf, 3, answ, 2);
	if (ret)
		printk("StopHWFilter error\n");

	if (answ[1] != handle) {
		DEB_S(("dvb: filter %d shutdown error :%d\n", handle, answ[1]));
		ret = -1;
        }
        return ret;
}


static void dvb_feed_start_pid(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct av7110 *av7110 = (struct av7110 *) dvbdmx->priv;
	u16 *pid = dvbdmx->pids, npids[5];
	int i;

	DEB_EE(("av7110: %p\n",av7110));

	npids[0] = npids[1] = npids[2] = npids[3] = npids[4] = 0xffff;
	i = dvbdmxfeed->pes_type;
	npids[i] = (pid[i]&0x8000) ? 0 : pid[i];
	if ((i == 2) && npids[i] && (dvbdmxfeed->ts_type & TS_PACKET)) {
		npids[i] = 0;
		ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
		StartHWFilter(dvbdmxfeed->filter);
		return;
	}
	if (dvbdmxfeed->pes_type <= 2 || dvbdmxfeed->pes_type == 4)
		ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
        
	if (dvbdmxfeed->pes_type < 2 && npids[0])
		if (av7110->fe_synced)
			av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, Scan, 0);

	if ((dvbdmxfeed->ts_type & TS_PACKET)) {
		if (dvbdmxfeed->pes_type == 0 && !(dvbdmx->pids[0] & 0x8000))
			av7110_av_start_record(av7110, RP_AUDIO, dvbdmxfeed);
		if (dvbdmxfeed->pes_type == 1 && !(dvbdmx->pids[1] & 0x8000))
			av7110_av_start_record(av7110, RP_VIDEO, dvbdmxfeed);
	}
}
                
static void dvb_feed_stop_pid(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct av7110 *av7110 = (struct av7110 *) dvbdmx->priv;
	u16 *pid = dvbdmx->pids, npids[5];
	int i;
                
	DEB_EE(("av7110: %p\n", av7110));
                
	if (dvbdmxfeed->pes_type <= 1) {
		av7110_av_stop(av7110, dvbdmxfeed->pes_type ?  RP_VIDEO : RP_AUDIO);
		if (!av7110->rec_mode)
			dvbdmx->recording = 0;
		if (!av7110->playing)
			dvbdmx->playing = 0;
	}
	npids[0] = npids[1] = npids[2] = npids[3] = npids[4] = 0xffff;
	i = dvbdmxfeed->pes_type;
	switch (i) {
	case 2: //teletext
		if (dvbdmxfeed->ts_type & TS_PACKET)
			StopHWFilter(dvbdmxfeed->filter);
		npids[2] = 0;
                break;
	case 0:
	case 1:
	case 4:
		if (!pids_off)
			return;
		npids[i] = (pid[i]&0x8000) ? 0 : pid[i];
                break;
        }
	ChangePIDs(av7110, npids[1], npids[0], npids[2], npids[3], npids[4]);
}
        
static int av7110_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *av7110 = (struct av7110 *) demux->priv;
                
	DEB_EE(("av7110: %p\n", av7110));
                
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
					       av7110_av_start_play(av7110,RP_AV);
					       demux->playing = 1;
					}
                        break;
                default:
				dvb_feed_start_pid(feed);
                        break;
                }
		} else if ((feed->ts_type & TS_PACKET) &&
			   (demux->dmx.frontend->source != DMX_MEMORY_FE)) {
			StartHWFilter(feed->filter);
		}
	}
                
	if (feed->type == DMX_TYPE_SEC) {
		int i;

		for (i = 0; i < demux->filternum; i++) {
			if (demux->filter[i].state != DMX_STATE_READY)
				continue;
			if (demux->filter[i].type != DMX_TYPE_SEC)
				continue;
			if (demux->filter[i].filter.parent != &feed->feed.sec)
				continue;
			demux->filter[i].state = DMX_STATE_GO;
			if (demux->dmx.frontend->source != DMX_MEMORY_FE)
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
			demux->pids[feed->pes_type] |= 0x8000;
			demux->pesfilter[feed->pes_type] = 0;
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
			if (demux->filter[i].state == DMX_STATE_GO &&
			    demux->filter[i].filter.parent == &feed->feed.sec) {
				demux->filter[i].state = DMX_STATE_READY;
				if (demux->dmx.frontend->source != DMX_MEMORY_FE)
					StopHWFilter(&demux->filter[i]);
		}
	}

        return 0;
}


static void restart_feeds(struct av7110 *av7110)
{
	struct dvb_demux *dvbdmx = &av7110->demux;
	struct dvb_demux_feed *feed;
	int mode;
	int i;

	DEB_EE(("av7110: %p\n",av7110));

	mode = av7110->playing;
	av7110->playing = 0;
	av7110->rec_mode = 0;

	for (i = 0; i < dvbdmx->filternum; i++) {
		feed = &dvbdmx->feed[i];
		if (feed->state == DMX_STATE_GO)
			av7110_start_feed(feed);
	}

	if (mode)
		av7110_av_start_play(av7110, mode);
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

	ret = av7110_fw_request(av7110, &tag, 0, fwstc, 4);
	if (ret) {
		printk(KERN_ERR "%s: av7110_fw_request error\n", __FUNCTION__);
		return -EIO;
}
	DEB_EE(("av7110: fwstc = %04hx %04hx %04hx %04hx\n",
		fwstc[0], fwstc[1], fwstc[2], fwstc[3]));

	*stc =	(((uint64_t) ((fwstc[3] & 0x8000) >> 15)) << 32) |
		(((uint64_t)  fwstc[1]) << 16) | ((uint64_t) fwstc[0]);
	*base = 1;
        
	DEB_EE(("av7110: stc = %lu\n", (unsigned long)*stc));

	return 0;
}


/******************************************************************************
 * SEC device file operations
 ******************************************************************************/

static int av7110_diseqc_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct av7110 *av7110 = fe->before_after_data;

	DEB_EE(("av7110: %p\n", av7110));

	switch (cmd) {
	case FE_SET_TONE:
		switch ((fe_sec_tone_mode_t) arg) {
		case SEC_TONE_ON:
			Set22K(av7110, 1);
			break;
		case SEC_TONE_OFF:
			Set22K(av7110, 0);
			break;
		default:
			return -EINVAL;
};
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
	{
		struct dvb_diseqc_master_cmd *cmd = arg;
		av7110_diseqc_send(av7110, cmd->msg_len, cmd->msg, -1);
		break;
	}

	case FE_DISEQC_SEND_BURST:
		av7110_diseqc_send(av7110, 0, NULL, (unsigned long) arg);
		break;

	default:
		return -EOPNOTSUPP;
};

	return 0;
}


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
		av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, Scan, 0);
	} else {
		SetPIDs(av7110, 0, 0, 0, 0, 0);
		av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, FlushTSQueue, 0);
	}

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

	av7110_av_register(av7110);
	av7110_ca_register(av7110);

#ifdef CONFIG_DVB_AV7110_OSD
	dvb_register_device(av7110->dvb_adapter, &av7110->osd_dev,
			    &dvbdev_osd, av7110, DVB_DEVICE_OSD);
#endif
        
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

	dvb_unregister_device(av7110->osd_dev);
	av7110_av_unregister(av7110);
	av7110_ca_unregister(av7110);
}


/****************************************************************************
 * I2C client commands
 ****************************************************************************/

int i2c_writereg(struct av7110 *av7110, u8 id, u8 reg, u8 val)
{
	u8 msg[2] = { reg, val };
	struct dvb_i2c_bus *i2c = av7110->i2c_bus;
	struct i2c_msg msgs;
	
	msgs.flags = 0;
	msgs.addr = id / 2;
	msgs.len = 2;
	msgs.buf = msg;
	return i2c->xfer(i2c, &msgs, 1);
}

u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg)
{
	struct dvb_i2c_bus *i2c = av7110->i2c_bus;
	u8 mm1[] = {0x00};
	u8 mm2[] = {0x00};
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = id / 2;
	mm1[0] = reg;
	msgs[0].len = 1; msgs[1].len = 1;
	msgs[0].buf = mm1; msgs[1].buf = mm2;
	i2c->xfer(i2c, msgs, 2);

	return mm2[0];
}

static int master_xfer(struct dvb_i2c_bus *i2c, const struct i2c_msg msgs[], int num)
{
	struct saa7146_dev *dev = i2c->data;
	return saa7146_i2c_transfer(dev, msgs, num, 6);
}

/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/


static int check_firmware(struct av7110* av7110)
{
	u32 crc = 0, len = 0;
	unsigned char *ptr;
		
	/* check for firmware magic */
	ptr = av7110->bin_fw;
	if (ptr[0] != 'A' || ptr[1] != 'V' || 
	    ptr[2] != 'F' || ptr[3] != 'W') {
		printk("dvb-ttpci: this is not an av7110 firmware\n");
		return -EINVAL;
	}
	ptr += 4;

	/* check dpram file */
	crc = ntohl(*(u32*)ptr);
	ptr += 4;
	len = ntohl(*(u32*)ptr);
	ptr += 4;
	if (len >= 512) {
		printk("dvb-ttpci: dpram file is way to big.\n");
		return -EINVAL;
	}
	if( crc != crc32_le(0,ptr,len)) {
		printk("dvb-ttpci: crc32 of dpram file does not match.\n");
		return -EINVAL;
	}
	av7110->bin_dpram = ptr;
	av7110->size_dpram = len;
	ptr += len;
	
	/* check root file */
	crc = ntohl(*(u32*)ptr);
	ptr += 4;
	len = ntohl(*(u32*)ptr);
	ptr += 4;
	
	if (len <= 200000 || len >= 300000 ||
	    len > ((av7110->bin_fw + av7110->size_fw) - ptr)) {
		printk("dvb-ttpci: root file has strange size (%d). aborting.\n",len);
		return -EINVAL;
	}
	if( crc != crc32_le(0,ptr,len)) {
		printk("dvb-ttpci: crc32 of root file does not match.\n");
		return -EINVAL;
	}
	av7110->bin_root = ptr;
	av7110->size_root = len;
	return 0;
}

#ifdef CONFIG_DVB_AV7110_FIRMWARE_FILE
#include "av7110_firm.h"
static inline int get_firmware(struct av7110* av7110)
{
	av7110->bin_fw = dvb_ttpci_fw;
	av7110->size_fw = sizeof(dvb_ttpci_fw);
	return check_firmware(av7110);
}
#else
static int get_firmware(struct av7110* av7110)
{
	int ret;
	const struct firmware *fw;

	/* request the av7110 firmware, this will block until someone uploads it */
	ret = request_firmware(&fw, "dvb-ttpci-01.fw", &av7110->dev->pci->dev);
	if (ret) {
		printk("dvb-ttpci: cannot request firmware!\n");
		return -EINVAL;
	}
	if (fw->size <= 200000) {
		printk("dvb-ttpci: this firmware is way too small.\n");
		return -EINVAL;
	}
	/* check if the firmware is available */
	av7110->bin_fw = (unsigned char*) vmalloc(fw->size);
	if (NULL == av7110->bin_fw) {
		DEB_D(("out of memory\n"));
		return -ENOMEM;
	}
	memcpy(av7110->bin_fw, fw->data, fw->size);
	av7110->size_fw = fw->size;
	if ((ret = check_firmware(av7110)))
		vfree(av7110->bin_fw);
	return ret;
}
#endif

static int av7110_attach(struct saa7146_dev* dev, struct saa7146_pci_extension_data *pci_ext)
{
	struct av7110 *av7110 = NULL;
	int ret = 0;

	DEB_EE(("dev: %p\n", dev));

	/* prepare the av7110 device struct */
	if (!(av7110 = kmalloc (sizeof (struct av7110), GFP_KERNEL))) {
		printk ("%s: out of memory!\n", __FUNCTION__);
		return -ENOMEM;
	}
	DEB_EE(("av7110: %p\n", av7110));
	memset(av7110, 0, sizeof(struct av7110));
	
	av7110->card_name = (char*)pci_ext->ext_priv;
	av7110->dev=(struct saa7146_dev *)dev;
	dev->ext_priv = av7110;

	if ((ret = get_firmware(av7110))) {
		kfree(av7110);
		return ret;
	}

	dvb_register_adapter(&av7110->dvb_adapter, av7110->card_name, THIS_MODULE);

	/* the Siemens DVB needs this if you want to have the i2c chips
	   get recognized before the main driver is fully loaded */
	saa7146_write(dev, GPIO_CTRL, 0x500000);

	saa7146_i2c_adapter_prepare(dev, NULL, 0, SAA7146_I2C_BUS_BIT_RATE_120); /* 275 kHz */

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
	saa7146_write(dev, DD1_INIT, 0x03000000);
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

	av7110_av_init(av7110);

        /* init BMP buffer */
        av7110->bmpbuf=av7110->iobuf+AVOUTLEN+AOUTLEN;
        init_waitqueue_head(&av7110->bmpq);
        
	av7110_ca_init(av7110);

        /* load firmware into AV7110 cards */
	av7110_bootarm(av7110);
	if (av7110_firmversion(av7110)) {
		ret = -EIO;
		goto err2;
	}

	if (FW_VERSION(av7110->arm_app)<0x2501)
		printk ("av7110: Warning, firmware version 0x%04x is too old. "
			"System might be unstable!\n", FW_VERSION(av7110->arm_app));

	if (kernel_thread(arm_thread, (void *) av7110, 0) < 0) {
		printk(KERN_ERR "av7110(%d): faile to start arm_mon kernel thread\n",
		       av7110->dvb_adapter->num);
		goto err2;
	}

	/* set internal volume control to maximum */
	av7110->adac_type = DVB_ADAC_TI;
	av7110_set_volume(av7110, 0xff, 0xff);

	av7710_set_video_mode(av7110, vidmode);

	/* handle different card types */
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
	} else if (0 == av7110_init_analog_module(av7110)) {
		/* done. */
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
		av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, MainSwitch, 1, 0);
		av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, 1);
		if (rgb_on)
			saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI); // RGB on, SCART pin 16
		//saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO); // SCARTpin 8
	}
	
	av7110_set_volume(av7110, 0xff, 0xff);

	av7110_setup_irc_config (av7110, 0);
	av7110_register(av7110);
	
	/* special case DVB-C: these cards have an analog tuner
	   plus need some special handling, so we have separate
	   saa7146_ext_vv data for these... */
	ret = av7110_init_v4l(av7110);
	
	if (ret)
		goto err3;

	printk(KERN_INFO "av7110: found av7110-%d.\n",av7110_num);
	av7110->device_initialized = 1;
	av7110_num++;
        return 0;

err3:
	av7110->arm_rmmod = 1;
	wake_up_interruptible(&av7110->arm_wait);
	while (av7110->arm_thread)
		dvb_delay(1);
err2:
	av7110_ca_exit(av7110);
	av7110_av_exit(av7110);
err:
	dvb_unregister_i2c_bus (master_xfer,av7110->i2c_bus->adapter,
				av7110->i2c_bus->id);

	dvb_unregister_adapter (av7110->dvb_adapter);

	if (NULL != av7110->debi_virt)
		pci_free_consistent(dev->pci, 8192, av7110->debi_virt, av7110->debi_bus);
	if (NULL != av7110->iobuf)
		vfree(av7110->iobuf);
	if (NULL != av7110 ) {
		kfree(av7110);
	}

	return ret;
}

static int av7110_detach (struct saa7146_dev* saa)
{
	struct av7110 *av7110 = (struct av7110*)saa->ext_priv;
	DEB_EE(("av7110: %p\n",av7110));
	
	if( 0 == av7110->device_initialized ) {
		return 0;
	}

	av7110_exit_v4l(av7110);

	av7110->arm_rmmod=1;
	wake_up_interruptible(&av7110->arm_wait);

	while (av7110->arm_thread)
		dvb_delay(1);

	dvb_unregister(av7110);
	
	IER_DISABLE(saa, (MASK_19 | MASK_03));
//	saa7146_write (av7110->dev, IER, 
//		     saa7146_read(av7110->dev, IER) & ~(MASK_19 | MASK_03));
	
	saa7146_write(av7110->dev, ISR,(MASK_19 | MASK_03));

	av7110_ca_exit(av7110);
	av7110_av_exit(av7110);

	vfree(av7110->iobuf);
	pci_free_consistent(saa->pci, 8192, av7110->debi_virt,
			    av7110->debi_bus);

	dvb_unregister_i2c_bus (master_xfer,av7110->i2c_bus->adapter, av7110->i2c_bus->id);
	dvb_unregister_adapter (av7110->dvb_adapter);

	av7110_num--;
#ifndef CONFIG_DVB_AV7110_FIRMWARE_FILE 
	if (av7110->bin_fw)
		vfree(av7110->bin_fw);
#endif
	kfree (av7110);
	saa->ext_priv = NULL;

	return 0;
}


static void av7110_irq(struct saa7146_dev* dev, u32 *isr) 
{
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;

//	DEB_INT(("dev: %p, av7110: %p\n",dev,av7110));

	if (*isr & MASK_19)
		tasklet_schedule (&av7110->debi_tasklet);
	
	if (*isr & MASK_03)
		tasklet_schedule (&av7110->gpio_tasklet);
}


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


static struct saa7146_extension av7110_extension = {
	.name		= "dvb\0",
	.flags		= SAA7146_I2C_SHORT_DELAY,

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
#if defined(CONFIG_INPUT_EVDEV) || defined(CONFIG_INPUT_EVDEV_MODULE)
	if (retval)
		goto failed_saa7146_register;
	
	retval = av7110_ir_init();
	if (retval)
		goto failed_av7110_ir_init;
	return 0;
failed_av7110_ir_init:
	saa7146_unregister_extension(&av7110_extension);
failed_saa7146_register:
#endif
	return retval;
}


static void __exit av7110_exit(void)
{
#if defined(CONFIG_INPUT_EVDEV) || defined(CONFIG_INPUT_EVDEV_MODULE)
	av7110_ir_exit();
#endif
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
MODULE_PARM(rgb_on, "i");
MODULE_PARM_DESC(rgb_on, "For Siemens DVB-C cards only: Enable RGB control"
		" signal on SCART pin 16 to switch SCART video mode from CVBS to RGB");
