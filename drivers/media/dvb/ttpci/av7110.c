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
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include <linux/i2c.h>

#include <asm/system.h>
#include <asm/semaphore.h>

#include <linux/dvb/frontend.h>

#include "dvb_frontend.h"

#include "ttpci-eeprom.h"
#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_av.h"
#include "av7110_ca.h"
#include "av7110_ipack.h"

int av7110_debug;

static int vidmode=CVBS_RGB_OUT;
static int pids_off;
static int adac=DVB_ADAC_TI;
static int hw_sections;
static int rgb_on;
static int volume = 255;

module_param_named(debug, av7110_debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (bitmask, default 0)");
module_param(vidmode, int, 0444);
MODULE_PARM_DESC(vidmode,"analog video out: 0 off, 1 CVBS+RGB (default), 2 CVBS+YC, 3 YC");
module_param(pids_off, int, 0444);
MODULE_PARM_DESC(pids_off,"clear video/audio/PCR PID filters when demux is closed");
module_param(adac, int, 0444);
MODULE_PARM_DESC(adac,"audio DAC type: 0 TI, 1 CRYSTAL, 2 MSP (use if autodetection fails)");
module_param(hw_sections, int, 0444);
MODULE_PARM_DESC(hw_sections, "0 use software section filter, 1 use hardware");
module_param(rgb_on, int, 0444);
MODULE_PARM_DESC(rgb_on, "For Siemens DVB-C cards only: Enable RGB control"
		" signal on SCART pin 16 to switch SCART video mode from CVBS to RGB");
module_param(volume, int, 0444);
MODULE_PARM_DESC(volume, "initial volume: default 255 (range 0-255)");

static void restart_feeds(struct av7110 *av7110);

static int av7110_num = 0;

#define FE_FUNC_OVERRIDE(fe_func, av7110_copy, av7110_func) \
{\
	if (fe_func != NULL) { \
		av7110_copy = fe_func; \
	   	fe_func = av7110_func; \
	} \
}


static void init_av7110_av(struct av7110 *av7110)
{
	struct saa7146_dev *dev=av7110->dev;

	/* set internal volume control to maximum */
	av7110->adac_type = DVB_ADAC_TI;
	av7110_set_volume(av7110, av7110->mixer.volume_left, av7110->mixer.volume_right);

	av7710_set_video_mode(av7110, vidmode);

	/* handle different card types */
	/* remaining inits according to card and frontend type */
	av7110->analog_tuner_flags = 0;
	av7110->current_input = 0;
	if (i2c_writereg(av7110, 0x20, 0x00, 0x00) == 1) {
		printk ("dvb-ttpci: Crystal audio DAC @ card %d detected\n",
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
		printk("dvb-ttpci: DVB-C w/o analog module @ card %d detected\n",
			av7110->dvb_adapter->num);
		av7110->adac_type = DVB_ADAC_NONE;
	}
	else {
		av7110->adac_type = adac;
		printk("dvb-ttpci: adac type set to %d @ card %d\n",
			av7110->dvb_adapter->num, av7110->adac_type);
	}

	if (av7110->adac_type == DVB_ADAC_NONE || av7110->adac_type == DVB_ADAC_MSP) {
		// switch DVB SCART on
		av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, MainSwitch, 1, 0);
		av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, 1);
		if (rgb_on &&
		    (av7110->dev->pci->subsystem_vendor == 0x110a) && (av7110->dev->pci->subsystem_device == 0x0000)) {
			saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI); // RGB on, SCART pin 16
		//saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO); // SCARTpin 8
	}
	}

	av7110_set_volume(av7110, av7110->mixer.volume_left, av7110->mixer.volume_right);
	av7110_setup_irc_config(av7110, 0);
}

static void recover_arm(struct av7110 *av7110)
{
	dprintk(4, "%p\n",av7110);

	av7110_bootarm(av7110);
	msleep(100);
        restart_feeds(av7110);
	av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetIR, 1, av7110->ir_config);
}

static void arm_error(struct av7110 *av7110)
{
	dprintk(4, "%p\n",av7110);

        av7110->arm_errors++;
        av7110->arm_ready=0;
        recover_arm(av7110);
}

static int arm_thread(void *data)
{
	struct av7110 *av7110 = data;
        u16 newloops = 0;
	int timeout;

	dprintk(4, "%p\n",av7110);
	
        lock_kernel ();
        daemonize ("arm_mon");
        sigfillset (&current->blocked);
        unlock_kernel ();

	av7110->arm_thread = current;

	for (;;) {
		timeout = wait_event_interruptible_timeout(av7110->arm_wait,
							   av7110->arm_rmmod, 5 * HZ);
		if (-ERESTARTSYS == timeout || av7110->arm_rmmod) {
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
			printk(KERN_ERR "dvb-ttpci: ARM crashed @ card %d\n",
				av7110->dvb_adapter->num);

			arm_error(av7110);
			av7710_set_video_mode(av7110, vidmode);

			init_av7110_av(av7110);

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

	dprintk(4, "%p\n",av7110);

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
	dprintk(4, "registering %p\n", func);
        irc_handler = func;
}

void av7110_unregister_irc_handler(void (*func)(u32)) 
{
	dprintk(4, "unregistering %p\n", func);
        irc_handler = NULL;
}

static void run_handlers(unsigned long ircom)
{
        if (irc_handler != NULL)
                (*irc_handler)((u32) ircom);
}

static DECLARE_TASKLET(irtask, run_handlers, 0);

static void IR_handle(struct av7110 *av7110, u32 ircom)
{
	dprintk(4, "ircommand = %08x\n", ircom);
        irtask.data = (unsigned long) ircom;
        tasklet_schedule(&irtask);
}

/****************************************************************************
 * IRQ handling
 ****************************************************************************/

static int DvbDmxFilterCallback(u8 *buffer1, size_t buffer1_len,
                     u8 * buffer2, size_t buffer2_len,
                     struct dvb_demux_filter *dvbdmxfilter,
                     enum dmx_success success,
                     struct av7110 *av7110)
{
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
	
//	dprintk(4, "%p\n",av7110);

        print_time("debi");
	SAA7146_IER_DISABLE(av7110->dev, MASK_19);
	SAA7146_ISR_CLEAR(av7110->dev, MASK_19);

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
                                             av7110->debilen, NULL, 0, 
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
        
        if (av7110->debitype !=-1)
		printk("dvb-ttpci: GPIO0 irq oops @ %ld, psr:0x%08x, ssr:0x%08x\n",
		       jiffies, saa7146_read(av7110->dev, PSR),
		       saa7146_read(av7110->dev, SSR));
       
        spin_lock(&av7110->debilock);

	ARM_ClearIrq(av7110);

	SAA7146_IER_DISABLE(av7110->dev, MASK_19);
	SAA7146_ISR_CLEAR(av7110->dev, MASK_19);

        av7110->debitype = irdebi(av7110, DEBINOSWAP, IRQ_STATE, 0, 2);
        av7110->debilen  = irdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, 0, 2);
        rxbuf=irdebi(av7110, DEBINOSWAP, RX_BUFF, 0, 2);
        txbuf=irdebi(av7110, DEBINOSWAP, TX_BUFF, 0, 2);
	len = (av7110->debilen + 3) & ~3;

//	dprintk(8, "GPIO0 irq %d %d\n", av7110->debitype, av7110->debilen);
        print_time("gpio");

//	dprintk(8, "GPIO0 irq %02x\n", av7110->debitype&0xff);
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
		dprintk(8, "GPIO0 irq: DATA_MPEG_VIDEO_EVENT: w/h/ar = %u/%u/%u\n",
				av7110->video_size.w,
				av7110->video_size.h,
				av7110->video_size.aspect_ratio);

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
		saa7146_wait_for_debi_done(av7110->dev, 0);
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
		dprintk(8, "GPIO0 PES_PLAY len=%04x\n", len);
                iwdebi(av7110, DEBINOSWAP, TX_LEN, len, 2);
                iwdebi(av7110, DEBINOSWAP, IRQ_STATE_EXT, len, 2);
		saa7146_wait_for_debi_done(av7110->dev, 0);
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
		saa7146_wait_for_debi_done(av7110->dev, 0);
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
		saa7146_wait_for_debi_done(av7110->dev, 0);
                saa7146_write(av7110->dev, IER, 
                              saa7146_read(av7110->dev, IER) | MASK_19);
                irdebi(av7110, DEBISWAB, DPRAM_BASE+rxbuf, 0, len);
                spin_unlock(&av7110->debilock);
                return;

        case DATA_DEBUG_MESSAGE:
		saa7146_wait_for_debi_done(av7110->dev, 0);
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
		printk("dvb-ttpci: gpioirq unknown type=%d len=%d\n",
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

	dprintk(4, "%p\n", av7110);

	if (cmd == OSD_SEND_CMD)
		return av7110_osd_cmd(av7110, (osd_cmd_t *) parg);
	if (cmd == OSD_GET_CAPABILITY)
		return av7110_osd_capability(av7110, (osd_cap_t *) parg);

	return -EINVAL;
        }


static struct file_operations dvb_osd_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_generic_open,
	.release	= dvb_generic_release,
};

static struct dvb_device dvbdev_osd = {
	.priv		= NULL,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_osd_fops,
	.kernel_ioctl	= dvb_osd_ioctl,
};
#endif /* CONFIG_DVB_AV7110_OSD */


static inline int SetPIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
			  u16 subpid, u16 pcrpid)
        {
	dprintk(4, "%p\n", av7110);

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
	dprintk(4, "%p\n", av7110);
        
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

	dprintk(4, "%p\n", av7110);

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
	if (ret != 0 || handle >= 32) {
		printk("dvb-ttpci: %s error  buf %04x %04x %04x %04x  "
				"ret %x  handle %04x\n",
				__FUNCTION__, buf[0], buf[1], buf[2], buf[3],
				ret, handle);
		dvbdmxfilter->hw_handle = 0xffff;
		return -1;
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
        
	dprintk(4, "%p\n", av7110);

	handle = dvbdmxfilter->hw_handle;
	if (handle >= 32) {
		printk("%s tried to stop invalid filter %04x, filter type = %x\n",
				__FUNCTION__, handle, dvbdmxfilter->type);
		return 0;
                }

	av7110->handle2filter[handle] = NULL;

	buf[0] = (COMTYPE_PID_FILTER << 8) + DelPIDFilter;
	buf[1] = 1;
	buf[2] = handle;
	ret = av7110_fw_request(av7110, buf, 3, answ, 2);
	if (ret != 0 || answ[1] != handle) {
		printk("dvb-ttpci: %s error  cmd %04x %04x %04x  ret %x  "
				"resp %04x %04x  pid %d\n",
				__FUNCTION__, buf[0], buf[1], buf[2], ret,
				answ[0], answ[1], dvbdmxfilter->feed->pid);
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

	dprintk(4, "%p\n", av7110);

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
                
	dprintk(4, "%p\n", av7110);
                
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
	struct av7110 *av7110 = demux->priv;
                
	dprintk(4, "%p\n", av7110);
                
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
	struct av7110 *av7110 = demux->priv;

	dprintk(4, "%p\n", av7110);

	if (feed->type == DMX_TYPE_TS) {
		if (feed->ts_type & TS_DECODER) {
			if (feed->pes_type >= DMX_TS_PES_OTHER ||
			    !demux->pesfilter[feed->pes_type])
				return -EINVAL;
			demux->pids[feed->pes_type] |= 0x8000;
			demux->pesfilter[feed->pes_type] = NULL;
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

	dprintk(4, "%p\n", av7110);

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

	dprintk(4, "%p\n", av7110);

	if (num != 0)
		return -EINVAL;

	ret = av7110_fw_request(av7110, &tag, 0, fwstc, 4);
	if (ret) {
		printk(KERN_ERR "%s: av7110_fw_request error\n", __FUNCTION__);
		return -EIO;
}
	dprintk(2, "fwstc = %04hx %04hx %04hx %04hx\n",
		fwstc[0], fwstc[1], fwstc[2], fwstc[3]);

	*stc =	(((uint64_t) ((fwstc[3] & 0x8000) >> 15)) << 32) |
		(((uint64_t)  fwstc[1]) << 16) | ((uint64_t) fwstc[0]);
	*base = 1;
        
	dprintk(4, "stc = %lu\n", (unsigned long)*stc);

	return 0;
}


/******************************************************************************
 * SEC device file operations
 ******************************************************************************/


static int av7110_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct av7110* av7110 = (struct av7110*) fe->dvb->priv;

	switch (tone) {
		case SEC_TONE_ON:
			Set22K(av7110, 1);
			break;
		case SEC_TONE_OFF:
			Set22K(av7110, 0);
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

static int av7110_diseqc_send_master_cmd(struct dvb_frontend* fe,
					 struct dvb_diseqc_master_cmd* cmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_diseqc_send(av7110, cmd->msg_len, cmd->msg, -1);

	return 0;
}

static int av7110_diseqc_send_burst(struct dvb_frontend* fe,
				    fe_sec_mini_cmd_t minicmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_diseqc_send(av7110, 0, NULL, minicmd);

	return 0;
}


static int av7110_register(struct av7110 *av7110)
{
        int ret, i;
        struct dvb_demux *dvbdemux=&av7110->demux;

	dprintk(4, "%p\n", av7110);

        if (av7110->registered)
                return -1;

        av7110->registered=1;

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

	dprintk(4, "%p\n", av7110);

        if (!av7110->registered)
                return;

	dvb_net_release(&av7110->dvb_net);

	dvbdemux->dmx.close(&dvbdemux->dmx);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->hw_frontend);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &av7110->mem_frontend);

        dvb_dmxdev_release(&av7110->dmxdev);
        dvb_dmx_release(&av7110->demux);

	if (av7110->fe != NULL)
		dvb_unregister_frontend(av7110->fe);
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
	struct i2c_msg msgs;
	
	msgs.flags = 0;
	msgs.addr = id / 2;
	msgs.len = 2;
	msgs.buf = msg;
	return i2c_transfer(&av7110->i2c_adap, &msgs, 1);
}

#if 0
u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg)
{
	u8 mm1[] = {0x00};
	u8 mm2[] = {0x00};
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = id / 2;
	mm1[0] = reg;
	msgs[0].len = 1; msgs[1].len = 1;
	msgs[0].buf = mm1; msgs[1].buf = mm2;
	i2c_transfer(&av7110->i2c_adap, msgs, 2);

	return mm2[0];
}
#endif

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
		if (ret == -ENOENT) {
			printk(KERN_ERR "dvb-ttpci: could not load firmware,"
			       " file not found: dvb-ttpci-01.fw\n");
			printk(KERN_ERR "dvb-ttpci: usually this should be in"
			       " /usr/lib/hotplug/firmware\n");
			printk(KERN_ERR "dvb-ttpci: and can be downloaded here"
			       " http://www.linuxtv.org/download/dvb/firmware/\n");
		} else
			printk(KERN_ERR "dvb-ttpci: cannot request firmware"
			       " (error %i)\n", ret);
		return -EINVAL;
	}

	if (fw->size <= 200000) {
		printk("dvb-ttpci: this firmware is way too small.\n");
		release_firmware(fw);
		return -EINVAL;
	}

	/* check if the firmware is available */
	av7110->bin_fw = (unsigned char*) vmalloc(fw->size);
	if (NULL == av7110->bin_fw) {
		dprintk(1, "out of memory\n");
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(av7110->bin_fw, fw->data, fw->size);
	av7110->size_fw = fw->size;
	if ((ret = check_firmware(av7110)))
		vfree(av7110->bin_fw);

	release_firmware(fw);
	return ret;
}
#endif


static int alps_bsrv2_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = (struct av7110*) fe->dvb->priv;
	u8 pwr = 0;
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	u32 div = (params->frequency + 479500) / 125;

	if (params->frequency > 2000000) pwr = 3;
	else if (params->frequency > 1800000) pwr = 2;
	else if (params->frequency > 1600000) pwr = 1;
	else if (params->frequency > 1200000) pwr = 0;
	else if (params->frequency >= 1100000) pwr = 1;
	else pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = ((div & 0x18000) >> 10) | 0x95;
	buf[3] = (pwr << 6) | 0x30;

        // NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1x93_config alps_bsrv2_config = {
	.demod_address = 0x08,
	.xin = 90100000UL,
	.invert_pwm = 0,
	.pll_set = alps_bsrv2_pll_set,
};


static u8 alps_bsru6_inittab[] = {
	0x01, 0x15,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x7d,   /* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,   /* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,   /* DAC not used, set to high impendance mode */
	0x07, 0x00,   /* DAC LSB */
	0x08, 0x40,   /* DiSEqC off, LNB power on OP2/LOCK pin on */
	0x09, 0x00,   /* FIFO */
	0x0c, 0x51,   /* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,   /* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,   /* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,   // AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb5,   // Lock detect: -64  Carrier freq detect:on
	0x15, 0xc9,   // lock detector threshold
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,  // out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,  // 1/2 threshold
	0x2a, 0x14,  // 2/3 threshold
	0x2b, 0x0f,  // 3/4 threshold
	0x2c, 0x09,  // 5/6 threshold
	0x2d, 0x05,  // 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,  // test all FECs
	0x32, 0x19,  // viterbi and synchro search
	0x33, 0xfc,  // rs control
	0x34, 0x93,  // error control
	0x0f, 0x52,
	0xff, 0xff
};

static int alps_bsru6_set_symbol_rate(struct dvb_frontend* fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) { aclk = 0xb7; bclk = 0x47; }
	else if (srate < 3000000) { aclk = 0xb7; bclk = 0x4b; }
	else if (srate < 7000000) { aclk = 0xb7; bclk = 0x4f; }
	else if (srate < 14000000) { aclk = 0xb7; bclk = 0x53; }
	else if (srate < 30000000) { aclk = 0xb6; bclk = 0x53; }
	else if (srate < 45000000) { aclk = 0xb4; bclk = 0x51; }

	stv0299_writereg (fe, 0x13, aclk);
	stv0299_writereg (fe, 0x14, bclk);
	stv0299_writereg (fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg (fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg (fe, 0x21, (ratio      ) & 0xf0);

	return 0;
}

static int alps_bsru6_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = (struct av7110*) fe->dvb->priv;
	int ret;
	u8 data[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125; // round correctly
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	data[3] = 0xC4;

	if (params->frequency > 1530000) data[3] = 0xc0;

	ret = i2c_transfer (&av7110->i2c_adap, &msg, 1);
	if (ret != 1)
		return -EIO;
	return 0;
}

static struct stv0299_config alps_bsru6_config = {

	.demod_address = 0x68,
	.inittab = alps_bsru6_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.enhanced_tuning = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_bsru6_set_symbol_rate,
	.pll_set = alps_bsru6_pll_set,
};



static int alps_tdbe2_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x62, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (params->frequency + 35937500 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85 | ((div >> 10) & 0x60);
	data[3] = (params->frequency < 174000000 ? 0x88 : params->frequency < 470000000 ? 0x84 : 0x81);

	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1820_config alps_tdbe2_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
	.pll_set = alps_tdbe2_pll_set,
};




static int grundig_29504_451_pll_set(struct dvb_frontend* fe,
				     struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = params->frequency / 125;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = 0x00;

	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct tda8083_config grundig_29504_451_config = {
	.demod_address = 0x68,
	.pll_set = grundig_29504_451_pll_set,
};



static int philips_cd1516_pll_set(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters* params)
{
        struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u32 f = params->frequency;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (f + 36125000 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = (f < 174000000 ? 0xa1 : f < 470000000 ? 0x92 : 0x34);

	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1820_config philips_cd1516_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
	.pll_set = philips_cd1516_pll_set,
};



static int alps_tdlb7_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = fe->dvb->priv;
	u32 div, pwr;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (params->frequency + 36200000) / 166666;

	if (params->frequency <= 782000000)
		pwr = 1;
	else
		pwr = 2;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85;
	data[3] = pwr << 6;

	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static int alps_tdlb7_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
	struct av7110* av7110 = (struct av7110*) fe->dvb->priv;

	return request_firmware(fw, name, &av7110->dev->pci->dev);
}

static struct sp8870_config alps_tdlb7_config = {

	.demod_address = 0x71,
	.pll_set = alps_tdlb7_pll_set,
	.request_firmware = alps_tdlb7_request_firmware,
};



static int nexusca_stv0297_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x63, .flags = 0, .buf = data, .len = sizeof(data) };
	struct i2c_msg readmsg = { .addr = 0x63, .flags = I2C_M_RD, .buf = data, .len = 1 };
	int i;

	div = (params->frequency + 36150000 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0xce;

	if (params->frequency < 45000000)
		return -EINVAL;
	else if (params->frequency < 137000000)
		data[3] = 0x01;
	else if (params->frequency < 403000000)
		data[3] = 0x02;
	else if (params->frequency < 860000000)
		data[3] = 0x04;
	else
		return -EINVAL;

	stv0297_enable_plli2c(fe);
	if (i2c_transfer (&av7110->i2c_adap, &msg, 1) != 1) {
		printk("nexusca: pll transfer failed!\n");
		return -EIO;
	}

	// wait for PLL lock
	for(i=0; i< 20; i++) {

		stv0297_enable_plli2c(fe);
		if (i2c_transfer (&av7110->i2c_adap, &readmsg, 1) == 1)
			if (data[0] & 0x40) break;
		msleep(10);
	}

	return 0;
}

static struct stv0297_config nexusca_stv0297_config = {

	.demod_address = 0x1C,
	.pll_set = nexusca_stv0297_pll_set,
};


static void av7110_fe_lock_fix(struct av7110* av7110, fe_status_t status)
{
	int synced = (status & FE_HAS_LOCK) ? 1 : 0;

	av7110->fe_status = status;

	if (av7110->fe_synced == synced)
		return;

	av7110->fe_synced = synced;

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
		av7110_fw_cmd(av7110, COMTYPE_PID_FILTER, FlushTSQueue, 0);
		av7110_wait_msgstate(av7110, GPMQBusy);
	}

	up(&av7110->pid_mutex);
}

static int av7110_fe_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct av7110* av7110 = fe->dvb->priv;
	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_set_frontend(fe, params);
}

static int av7110_fe_init(struct dvb_frontend* fe)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_init(fe);
}

static int av7110_fe_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct av7110* av7110 = fe->dvb->priv;
	int ret;

	/* call the real implementation */
	ret = av7110->fe_read_status(fe, status);
	if (ret)
		return ret;

	if (((*status ^ av7110->fe_status) & FE_HAS_LOCK) && (*status & FE_HAS_LOCK)) {
		av7110_fe_lock_fix(av7110, *status);
	}

	return 0;
}

static int av7110_fe_diseqc_reset_overload(struct dvb_frontend* fe)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_diseqc_reset_overload(fe);
}

static int av7110_fe_diseqc_send_master_cmd(struct dvb_frontend* fe,
					    struct dvb_diseqc_master_cmd* cmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_diseqc_send_master_cmd(fe, cmd);
}

static int av7110_fe_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_diseqc_send_burst(fe, minicmd);
}

static int av7110_fe_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_set_tone(fe, tone);
}

static int av7110_fe_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_set_voltage(fe, voltage);
}

static int av7110_fe_dishnetwork_send_legacy_command(struct dvb_frontend* fe, unsigned int cmd)
{
	struct av7110* av7110 = fe->dvb->priv;

	av7110_fe_lock_fix(av7110, 0);
	return av7110->fe_dishnetwork_send_legacy_command(fe, cmd);
}

static u8 read_pwm(struct av7110* av7110)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { { .addr = 0x50,.flags = 0,.buf = &b,.len = 1 },
				 { .addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1} };

        if ((i2c_transfer(&av7110->i2c_adap, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}

static void frontend_init(struct av7110 *av7110)
{
	if (av7110->dev->pci->subsystem_vendor == 0x110a) {
		switch(av7110->dev->pci->subsystem_device) {
		case 0x0000: // Fujitsu/Siemens DVB-Cable (ves1820/Philips CD1516(??))
			av7110->fe = ves1820_attach(&philips_cd1516_config,
						    &av7110->i2c_adap, read_pwm(av7110));
			break;
		}

	} else if (av7110->dev->pci->subsystem_vendor == 0x13c2) {
		switch(av7110->dev->pci->subsystem_device) {
		case 0x0000: // Hauppauge/TT WinTV DVB-S rev1.X
		case 0x0003: // Hauppauge/TT WinTV Nexus-S Rev 2.X
		case 0x1002: // Hauppauge/TT WinTV DVB-S rev1.3SE

			// try the ALPS BSRV2 first of all
			av7110->fe = ves1x93_attach(&alps_bsrv2_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops->diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops->diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops->set_tone = av7110_set_tone;
				break;
			}

			// try the ALPS BSRU6 now
			av7110->fe = stv0299_attach(&alps_bsru6_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops->diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops->diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops->set_tone = av7110_set_tone;
				break;
			}

			// Try the grundig 29504-451
                        av7110->fe = tda8083_attach(&grundig_29504_451_config, &av7110->i2c_adap);
			if (av7110->fe) {
				av7110->fe->ops->diseqc_send_master_cmd = av7110_diseqc_send_master_cmd;
				av7110->fe->ops->diseqc_send_burst = av7110_diseqc_send_burst;
				av7110->fe->ops->set_tone = av7110_set_tone;
				break;
			}

			/* Try DVB-C cards */
			switch(av7110->dev->pci->subsystem_device) {
			case 0x0000:
				/* Siemens DVB-C (full-length card) VES1820/Philips CD1516 */
				av7110->fe = ves1820_attach(&philips_cd1516_config, &av7110->i2c_adap,
							read_pwm(av7110));
				break;
			case 0x0003:
				/* Haupauge DVB-C 2.1 VES1820/ALPS TDBE2 */
				av7110->fe = ves1820_attach(&alps_tdbe2_config, &av7110->i2c_adap,
							read_pwm(av7110));
				break;
			}
			break;

		case 0x0001: // Hauppauge/TT Nexus-T premium rev1.X

			// ALPS TDLB7
                        av7110->fe = sp8870_attach(&alps_tdlb7_config, &av7110->i2c_adap);
			break;

		case 0x0002: // Hauppauge/TT DVB-C premium rev2.X

                        av7110->fe = ves1820_attach(&alps_tdbe2_config, &av7110->i2c_adap, read_pwm(av7110));
			break;

		case 0x000A: // Hauppauge/TT Nexus-CA rev1.X

			av7110->fe = stv0297_attach(&nexusca_stv0297_config, &av7110->i2c_adap, 0x7b);
			if (av7110->fe) {
				/* set TDA9819 into DVB mode */
				saa7146_setgpio(av7110->dev, 1, SAA7146_GPIO_OUTHI); // TDA9198 pin9(STD)
				saa7146_setgpio(av7110->dev, 3, SAA7146_GPIO_OUTLO); // TDA9198 pin30(VIF)

				/* tuner on this needs a slower i2c bus speed */
				av7110->dev->i2c_bitrate = SAA7146_I2C_BUS_BIT_RATE_240;
				break;
			}
		}
	}

	if (av7110->fe == NULL) {
		printk("dvb-ttpci: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       av7110->dev->pci->vendor,
		       av7110->dev->pci->device,
		       av7110->dev->pci->subsystem_vendor,
		       av7110->dev->pci->subsystem_device);
	} else {
		FE_FUNC_OVERRIDE(av7110->fe->ops->init, av7110->fe_init, av7110_fe_init);
		FE_FUNC_OVERRIDE(av7110->fe->ops->read_status, av7110->fe_read_status, av7110_fe_read_status);
		FE_FUNC_OVERRIDE(av7110->fe->ops->diseqc_reset_overload, av7110->fe_diseqc_reset_overload, av7110_fe_diseqc_reset_overload);
		FE_FUNC_OVERRIDE(av7110->fe->ops->diseqc_send_master_cmd, av7110->fe_diseqc_send_master_cmd, av7110_fe_diseqc_send_master_cmd);
		FE_FUNC_OVERRIDE(av7110->fe->ops->diseqc_send_burst, av7110->fe_diseqc_send_burst, av7110_fe_diseqc_send_burst);
		FE_FUNC_OVERRIDE(av7110->fe->ops->set_tone, av7110->fe_set_tone, av7110_fe_set_tone);
		FE_FUNC_OVERRIDE(av7110->fe->ops->set_voltage, av7110->fe_set_voltage, av7110_fe_set_voltage;)
		FE_FUNC_OVERRIDE(av7110->fe->ops->dishnetwork_send_legacy_command, av7110->fe_dishnetwork_send_legacy_command, av7110_fe_dishnetwork_send_legacy_command);
		FE_FUNC_OVERRIDE(av7110->fe->ops->set_frontend, av7110->fe_set_frontend, av7110_fe_set_frontend);

		if (dvb_register_frontend(av7110->dvb_adapter, av7110->fe)) {
			printk("av7110: Frontend registration failed!\n");
			if (av7110->fe->ops->release)
				av7110->fe->ops->release(av7110->fe);
			av7110->fe = NULL;
		}
	}
}

static int av7110_attach(struct saa7146_dev* dev, struct saa7146_pci_extension_data *pci_ext)
{
	struct av7110 *av7110 = NULL;
	int ret = 0;

	dprintk(4, "dev: %p\n", dev);

	/* prepare the av7110 device struct */
	if (!(av7110 = kmalloc (sizeof (struct av7110), GFP_KERNEL))) {
		dprintk(1, "out of memory\n");
		return -ENOMEM;
	}

	memset(av7110, 0, sizeof(struct av7110));
	
	av7110->card_name = (char*)pci_ext->ext_priv;
	av7110->dev = dev;
	dev->ext_priv = av7110;

	if ((ret = get_firmware(av7110))) {
		kfree(av7110);
		return ret;
	}

	dvb_register_adapter(&av7110->dvb_adapter, av7110->card_name, THIS_MODULE);

	/* the Siemens DVB needs this if you want to have the i2c chips
	   get recognized before the main driver is fully loaded */
	saa7146_write(dev, GPIO_CTRL, 0x500000);

#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	av7110->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL;
#else
	av7110->i2c_adap.class = I2C_CLASS_TV_DIGITAL;
#endif
	strlcpy(av7110->i2c_adap.name, pci_ext->ext_priv, sizeof(av7110->i2c_adap.name));

	saa7146_i2c_adapter_prepare(dev, &av7110->i2c_adap, SAA7146_I2C_BUS_BIT_RATE_120); /* 275 kHz */

	if (i2c_add_adapter(&av7110->i2c_adap) < 0) {
		dvb_unregister_adapter (av7110->dvb_adapter);
		kfree(av7110);
		return -ENOMEM;
	}

	ttpci_eeprom_parse_mac(&av7110->i2c_adap, av7110->dvb_adapter->proposed_mac);

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
        av7110->debitype=-1;

        /* default OSD window */
        av7110->osdwin=1;
	sema_init(&av7110->osd_sema, 1);

        /* ARM "watchdog" */
	init_waitqueue_head(&av7110->arm_wait);
        av7110->arm_thread=NULL;
     
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
		printk ("dvb-ttpci: Warning, firmware version 0x%04x is too old. "
			"System might be unstable!\n", FW_VERSION(av7110->arm_app));

	if (kernel_thread(arm_thread, (void *) av7110, 0) < 0) {
		printk("dvb-ttpci: failed to start arm_mon kernel thread @ card %d\n",
		       av7110->dvb_adapter->num);
		goto err2;
	}

	/* set initial volume in mixer struct */
	av7110->mixer.volume_left  = volume;
	av7110->mixer.volume_right = volume;
	
	init_av7110_av(av7110);

	av7110_register(av7110);
	
	/* special case DVB-C: these cards have an analog tuner
	   plus need some special handling, so we have separate
	   saa7146_ext_vv data for these... */
	ret = av7110_init_v4l(av7110);
	
	if (ret)
		goto err3;

	av7110->dvb_adapter->priv = av7110;
	frontend_init(av7110);

	printk(KERN_INFO "dvb-ttpci: found av7110-%d.\n", av7110_num);
	av7110->device_initialized = 1;
	av7110_num++;
        return 0;

err3:
	av7110->arm_rmmod = 1;
	wake_up_interruptible(&av7110->arm_wait);
	while (av7110->arm_thread)
		msleep(1);
err2:
	av7110_ca_exit(av7110);
	av7110_av_exit(av7110);
err:
	i2c_del_adapter(&av7110->i2c_adap);

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
	dprintk(4, "%p\n", av7110);
	
	if( 0 == av7110->device_initialized ) {
		return 0;
	}

	av7110_exit_v4l(av7110);

	av7110->arm_rmmod=1;
	wake_up_interruptible(&av7110->arm_wait);

	while (av7110->arm_thread)
		msleep(1);

	dvb_unregister(av7110);
	
	SAA7146_IER_DISABLE(saa, MASK_19 | MASK_03);
	SAA7146_ISR_CLEAR(saa, MASK_19 | MASK_03);

	av7110_ca_exit(av7110);
	av7110_av_exit(av7110);

	vfree(av7110->iobuf);
	pci_free_consistent(saa->pci, 8192, av7110->debi_virt,
			    av7110->debi_bus);

	i2c_del_adapter(&av7110->i2c_adap);

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
	struct av7110 *av7110 = dev->ext_priv;

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

MAKE_AV7110_INFO(tts_1_X,    "Technotrend/Hauppauge WinTV DVB-S rev1.X");
MAKE_AV7110_INFO(ttt_1_X,    "Technotrend/Hauppauge WinTV DVB-T rev1.X");
MAKE_AV7110_INFO(ttc_1_X,    "Technotrend/Hauppauge WinTV Nexus-CA rev1.X");
MAKE_AV7110_INFO(ttc_2_X,    "Technotrend/Hauppauge WinTV DVB-C rev2.X");
MAKE_AV7110_INFO(tts_2_X,    "Technotrend/Hauppauge WinTV Nexus-S rev2.X");
MAKE_AV7110_INFO(tts_1_3se,  "Technotrend/Hauppauge WinTV Nexus-S rev1.3");
MAKE_AV7110_INFO(fsc,        "Fujitsu Siemens DVB-C");

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(tts_1_X,   0x13c2, 0x0000),
	MAKE_EXTENSION_PCI(ttt_1_X,   0x13c2, 0x0001),
	MAKE_EXTENSION_PCI(ttc_2_X,   0x13c2, 0x0002),
	MAKE_EXTENSION_PCI(tts_2_X,   0x13c2, 0x0003),
	MAKE_EXTENSION_PCI(tts_1_3se, 0x13c2, 0x1002),
	MAKE_EXTENSION_PCI(fsc,       0x110a, 0x0000),
	MAKE_EXTENSION_PCI(ttc_1_X,   0x13c2, 0x000a),

/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0004), UNDEFINED CARD */ // Galaxis DVB PC-Sat-Carte
/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0005), UNDEFINED CARD */ // Technisat SkyStar1
/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0006), UNDEFINED CARD */ // TT/Hauppauge WinTV Nexus-S v????
/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0008), UNDEFINED CARD */ // TT/Hauppauge WinTV DVB-T v????
/*	MAKE_EXTENSION_PCI(???, 0x13c2, 0x0009), UNDEFINED CARD */ // TT/Hauppauge WinTV Nexus-CA v????

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

