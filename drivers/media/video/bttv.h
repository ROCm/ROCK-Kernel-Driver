/*
 *  bttv - Bt848 frame grabber driver
 *
 *  card ID's and external interfaces of the bttv driver
 *  basically stuff needed by other drivers (i2c, lirc, ...)
 *  and is supported not to change much over time.
 *
 *  Copyright (C) 1996,97 Ralph Metzler (rjkm@thp.uni-koeln.de)
 *  (c) 1999,2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#ifndef _BTTV_H_
#define _BTTV_H_

#include <linux/videodev.h>

/* ---------------------------------------------------------- */
/* exported by bttv-cards.c                                   */

#define BTTV_UNKNOWN       0x00
#define BTTV_MIRO          0x01
#define BTTV_HAUPPAUGE     0x02
#define BTTV_STB           0x03
#define BTTV_INTEL         0x04
#define BTTV_DIAMOND       0x05 
#define BTTV_AVERMEDIA     0x06 
#define BTTV_MATRIX_VISION 0x07 
#define BTTV_FLYVIDEO      0x08
#define BTTV_TURBOTV       0x09
#define BTTV_HAUPPAUGE878  0x0a
#define BTTV_MIROPRO       0x0b
#define BTTV_ADSTECH_TV    0x0c
#define BTTV_AVERMEDIA98   0x0d
#define BTTV_VHX           0x0e
#define BTTV_ZOLTRIX       0x0f
#define BTTV_PIXVIEWPLAYTV 0x10
#define BTTV_WINVIEW_601   0x11
#define BTTV_AVEC_INTERCAP 0x12
#define BTTV_LIFE_FLYKIT   0x13
#define BTTV_CEI_RAFFLES   0x14
#define BTTV_CONFERENCETV  0x15
#define BTTV_PHOEBE_TVMAS  0x16
#define BTTV_MODTEC_205    0x17
#define BTTV_MAGICTVIEW061 0x18
#define BTTV_VOBIS_BOOSTAR 0x19
#define BTTV_HAUPPAUG_WCAM 0x1a
#define BTTV_MAXI          0x1b
#define BTTV_TERRATV       0x1c
#define BTTV_PXC200        0x1d
#define BTTV_FLYVIDEO_98   0x1e
#define BTTV_IPROTV        0x1f
#define BTTV_INTEL_C_S_PCI 0x20
#define BTTV_TERRATVALUE   0x21
#define BTTV_WINFAST2000   0x22
#define BTTV_CHRONOS_VS2   0x23
#define BTTV_TYPHOON_TVIEW 0x24
#define BTTV_PXELVWPLTVPRO 0x25
#define BTTV_MAGICTVIEW063 0x26
#define BTTV_PINNACLE      0x27
#define BTTV_STB2          0x28
#define BTTV_AVPHONE98     0x29
#define BTTV_PV951         0x2a
#define BTTV_ONAIR_TV      0x2b
#define BTTV_SIGMA_TVII_FM 0x2c
#define BTTV_MATRIX_VISION2 0x2d
#define BTTV_ZOLTRIX_GENIE 0x2e
#define BTTV_TERRATVRADIO  0x2f
#define BTTV_DYNALINK      0x30
#define BTTV_GVBCTV3PCI    0x31
#define BTTV_PXELVWPLTVPAK 0x32
#define BTTV_EAGLE         0x33
#define BTTV_PINNACLEPRO   0x34
#define BTTV_TVIEW_RDS_FM  0x35
#define BTTV_LIFETEC_9415  0x36
#define BTTV_BESTBUY_EASYTV 0x37
#define BTTV_FLYVIDEO_98FM 0x38
#define BTTV_GMV1          0x3d
#define BTTV_BESTBUY_EASYTV2 0x3e
#define BTTV_ATI_TVWONDER  0x3f
#define BTTV_ATI_TVWONDERVE 0x40
#define BTTV_FLYVIDEO2000   0x41
#define BTTV_TERRATVALUER   0x42
#define BTTV_GVBCTV4PCI     0x43
#define BTTV_VOODOOTV_FM    0x44
#define BTTV_AIMMS          0x45
#define BTTV_PV_BT878P_PLUS 0x46
#define BTTV_FLYVIDEO98EZ   0x47
#define BTTV_PV_BT878P_9B   0x48


/* i2c address list */
#define I2C_TSA5522        0xc2
#define I2C_TDA7432        0x8a
#define I2C_TDA8425        0x82
#define I2C_TDA9840        0x84
#define I2C_TDA9850        0xb6 /* also used by 9855,9873 */
#define I2C_TDA9874A       0xb0 /* also used by 9875 */
#define I2C_TDA9875        0xb0
#define I2C_HAUPEE         0xa0
#define I2C_STBEE          0xae
#define I2C_VHX            0xc0
#define I2C_MSP3400        0x80
#define I2C_TEA6300        0x80
#define I2C_DPL3518	   0x84

/* more card-specific defines */
#define PT2254_L_CHANNEL 0x10
#define PT2254_R_CHANNEL 0x08
#define PT2254_DBS_IN_2 0x400
#define PT2254_DBS_IN_10 0x20000
#define WINVIEW_PT2254_CLK  0x40
#define WINVIEW_PT2254_DATA 0x20
#define WINVIEW_PT2254_STROBE 0x80

struct bttv;

struct tvcard
{
        char *name;
        int video_inputs;
        int audio_inputs;
        int tuner;
        int svhs;
        u32 gpiomask;
        u32 muxsel[8];
        u32 audiomux[6]; /* Tuner, Radio, external, internal, mute, stereo */
        u32 gpiomask2;   /* GPIO MUX mask */

	/* i2c audio flags */
	int no_msp34xx:1;
	int no_tda9875:1;
	int needs_tvaudio:1;

	/* other settings */
	int pll;
#define PLL_NONE 0
#define PLL_28   1
#define PLL_35   2

	int tuner_type;
	int has_radio;
	void (*audio_hook)(struct bttv *btv, struct video_audio *v, int set);
};

extern struct tvcard bttv_tvcards[];
extern const int bttv_num_tvcards;

/* identification / initialization of the card */
extern void bttv_idcard(struct bttv *btv);
extern void bttv_init_card(struct bttv *btv);

/* card-specific funtions */
extern void tea5757_set_freq(struct bttv *btv, unsigned short freq);
extern void bttv_boot_msp34xx(struct bttv *btv, int pin);

/* kernel cmd line parse helper */
extern int bttv_parse(char *str, int max, int *vals);

/* extra tweaks for some chipsets */
extern void bttv_check_chipset(void);
extern int bttv_handle_chipset(struct bttv *btv);

/* ---------------------------------------------------------- */
/* exported by bttv-if.c                                      */
/* interface for gpio access by other modules                 */

/* returns card type + card ID (for bt878-based ones)
   for possible values see lines below beginning with #define BTTV_UNKNOWN
   returns negative value if error occurred 
*/
extern int bttv_get_cardinfo(unsigned int card, int *type, int *cardid);

/* obsolete, use bttv_get_cardinfo instead */
extern int bttv_get_id(unsigned int card);

/* sets GPOE register (BT848_GPIO_OUT_EN) to new value:
   data | (current_GPOE_value & ~mask)
   returns negative value if error occurred
*/
extern int bttv_gpio_enable(unsigned int card,
			    unsigned long mask, unsigned long data);

/* fills data with GPDATA register contents
   returns negative value if error occurred
*/
extern int bttv_read_gpio(unsigned int card, unsigned long *data);

/* sets GPDATA register to new value:
  (data & mask) | (current_GPDATA_value & ~mask)
  returns negative value if error occurred 
*/
extern int bttv_write_gpio(unsigned int card,
			   unsigned long mask, unsigned long data);

/* returns pointer to task queue which can be used as parameter to 
   interruptible_sleep_on
   in interrupt handler if BT848_INT_GPINT bit is set - this queue is activated
   (wake_up_interruptible) and following call to the function bttv_read_gpio 
   should return new value of GPDATA,
   returns NULL value if error occurred or queue is not available
   WARNING: because there is no buffer for GPIO data, one MUST 
   process data ASAP
*/
extern wait_queue_head_t* bttv_get_gpio_queue(unsigned int card);

/* i2c */
#define I2C_CLIENTS_MAX 16
extern void bttv_bit_setscl(void *data, int state);
extern void bttv_bit_setsda(void *data, int state);
extern void bttv_call_i2c_clients(struct bttv *btv, unsigned int cmd, void *arg);
extern int bttv_I2CRead(struct bttv *btv, unsigned char addr, char *probe_for);
extern int bttv_I2CWrite(struct bttv *btv, unsigned char addr, unsigned char b1,
			 unsigned char b2, int both);
extern void bttv_readee(struct bttv *btv, unsigned char *eedata, int addr);

#endif /* _BTTV_H_ */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
