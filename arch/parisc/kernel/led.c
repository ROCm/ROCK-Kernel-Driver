/*
 *    Chassis LCD/LED driver for HP-PARISC workstations
 *
 *      (c) Copyright 2000 Red Hat Software
 *      (c) Copyright 2000 Helge Deller <hdeller@redhat.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <asm/gsc.h>
#include <asm/processor.h>
#include <asm/hardware.h>
#include <asm/param.h>		/* HZ */
#include <asm/led.h>


/* define to disable all LED functions */
#undef  DISABLE_LEDS


#define CPU_HVERSION ((boot_cpu_data.hversion >> 4) & 0x0FFF)


struct lcd_block {
	unsigned char command;	/* stores the command byte      */
	unsigned char on;	/* value for turning LED on     */
	unsigned char off;	/* value for turning LED off    */
};

/* Structure returned by PDC_RETURN_CHASSIS_INFO */
struct pdc_chassis_lcd_info_ret_block {
	unsigned long model:16;		/* DISPLAY_MODEL_XXXX (see below)  */
	unsigned long lcd_width:16;	/* width of the LCD in chars (DISPLAY_MODEL_LCD only) */
	char *lcd_cmd_reg_addr;		/* ptr to LCD cmd-register & data ptr for LED */
	char *lcd_data_reg_addr;	/* ptr to LCD data-register (LCD only) */
	unsigned int min_cmd_delay;	/* delay in uS after cmd-write (LCD only) */
	unsigned char reset_cmd1;	/* command #1 for writing LCD string (LCD only) */
	unsigned char reset_cmd2;	/* command #2 for writing LCD string (LCD only) */
	unsigned char act_enable;	/* 0 = no activity (LCD only) */
	struct lcd_block heartbeat;
	struct lcd_block disk_io;
	struct lcd_block lan_rcv;
	struct lcd_block lan_tx;
	char _pad;
};

/* values for pdc_chassis_lcd_info_ret_block.model: */
#define DISPLAY_MODEL_LCD  0	/* KittyHawk LED or LCD */
#define DISPLAY_MODEL_NONE 1	/* no LED or LCD */
#define DISPLAY_MODEL_LASI 2	/* LASI style 8 bit LED */
#define DISPLAY_MODEL_OLD_ASP 0x7F /* faked: ASP style 8 x 1 bit LED (only very old ASP versions) */


/* LCD_CMD and LCD_DATA for KittyHawk machines */
#ifdef __LP64__
#define KITTYHAWK_LCD_CMD 0xfffffffff0190000L
#else
#define KITTYHAWK_LCD_CMD 0xf0190000
#endif
#define KITTYHAWK_LCD_DATA (KITTYHAWK_LCD_CMD + 1)


/* lcd_info is pre-initialized to the values needed to program KittyHawk LCD's */
static struct pdc_chassis_lcd_info_ret_block
lcd_info __attribute__((aligned(8))) =
{
      model:DISPLAY_MODEL_LCD,
      lcd_width:16,
      lcd_cmd_reg_addr:(char *) KITTYHAWK_LCD_CMD,
      lcd_data_reg_addr:(char *) KITTYHAWK_LCD_DATA,
      min_cmd_delay:40,
      reset_cmd1:0x80,
      reset_cmd2:0xc0,
};


/* direct access to some of the lcd_info variables */
#define LCD_CMD_REG	lcd_info.lcd_cmd_reg_addr	 
#define LCD_DATA_REG	lcd_info.lcd_data_reg_addr	 
#define LED_DATA_REG	lcd_info.lcd_cmd_reg_addr	/* LASI & ASP only */




/*
   ** 
   ** led_ASP_driver()
   ** 
 */
#define	LED_DATA	0x01	/* data to shift (0:on 1:off) */
#define	LED_STROBE	0x02	/* strobe to clock data */
static void led_ASP_driver(unsigned char leds)
{
	int i;

	leds = ~leds;
	for (i = 0; i < 8; i++) {
		unsigned char value;
		value = (leds & 0x80) >> 7;
		gsc_writeb( value,		 LED_DATA_REG );
		gsc_writeb( value | LED_STROBE,	 LED_DATA_REG );
		leds <<= 1;
	}
}


/*
   ** 
   ** led_LASI_driver()
   ** 
 */
static void led_LASI_driver(unsigned char leds)
{
	leds = ~leds;
	gsc_writeb( leds, LED_DATA_REG );
}


/*
   ** 
   ** led_LCD_driver()
   ** 
   ** The logic of the LCD driver is, that we write at every interrupt
   ** only to one of LCD_CMD_REG _or_ LCD_DATA_REG - registers.
   ** That way we don't need to let this interrupt routine busywait
   ** the "min_cmd_delay", since idlewaiting in an interrupt-routine is
   ** allways a BAD IDEA !
   **
   ** TODO: check the value of "min_cmd_delay" against the value of HZ.
   **   
 */

static void led_LCD_driver(unsigned char leds)
{
	static int last_index;	/* 0:heartbeat, 1:disk, 2:lan_in, 3:lan_out */
	static int last_was_cmd;/* 0: CMD was written last, 1: DATA was last */
	struct lcd_block *block_ptr;
	int value;
	
 	// leds = ~leds;	/* needed ? */ 

	switch (last_index) {
	    case 0:	block_ptr = &lcd_info.heartbeat;
			value = leds & LED_HEARTBEAT;
			break;
	    case 1:	block_ptr = &lcd_info.disk_io;
			value = leds & LED_DISK_IO;
			break;					
	    case 2:	block_ptr = &lcd_info.lan_rcv;
			value = leds & LED_LAN_RCV;
			break;					
	    case 3:	block_ptr = &lcd_info.lan_tx;
			value = leds & LED_LAN_TX;
			break;
	    default:	/* should never happen: */
			BUG();
			return;
	}

	if (last_was_cmd) {
	    /* write the value to the LCD data port */
    	    gsc_writeb( value ? block_ptr->on : block_ptr->off, LCD_DATA_REG );
	} else {
	    /* write the command-byte to the LCD command register */
    	    gsc_writeb( block_ptr->command, LCD_CMD_REG );
	}    
	
	/* now update the vars for the next interrupt iteration */ 
	if (++last_was_cmd == 2) {
	    last_was_cmd = 0;
	    if (++last_index == 4)
		last_index = 0;
	}
}



static char currentleds;	/* stores current value of the LEDs */

static void (*led_func_ptr) (unsigned char); /* ptr to LCD/LED-specific function */

/*
   ** led_interrupt_func()
   ** 
   ** is called at every timer interrupt from time.c,
   ** updates the chassis LCD/LED 
 */

#define HEARTBEAT_LEN	(HZ/16)

void led_interrupt_func(void)
{
#ifndef DISABLE_LEDS
	static int count;
	static int lastleds = -1;
	static int nr;

	/* exit, if not initialized */
	if (!led_func_ptr)
	    return;
	
	/* increment the local counter */
	if (count == (HZ-1))
	    count = 0;
	else
	    count++;

	/* calculate the Heartbeat */
	if ((count % (HZ/2)) < HEARTBEAT_LEN) 
	    currentleds |= LED_HEARTBEAT;
	else
	    currentleds &= ~LED_HEARTBEAT;

	/* roll LEDs 0..2 */
	if (count == 0) {
	    if (nr++ >= 2) 
		nr = 0;
	    currentleds &= ~7;
	    currentleds |= (1 << nr);
	}

	/* now update the LEDs */
	if (currentleds != lastleds) {
	    led_func_ptr(currentleds);
	    lastleds = currentleds;
	}
#endif
}


/*
   ** register_led_driver()
   ** 
   ** All information in lcd_info needs to be set up prior
   ** calling this function. 
 */

static void __init register_led_driver(void)
{
#ifndef DISABLE_LEDS
	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:
		printk(KERN_INFO "LCD display at (%p,%p)\n",
		  LCD_CMD_REG , LCD_DATA_REG);
		led_func_ptr = led_LCD_driver;
		break;

	case DISPLAY_MODEL_LASI:
		printk(KERN_INFO "LED display at %p\n",
		       LED_DATA_REG);
		led_func_ptr = led_LASI_driver;
		break;

	case DISPLAY_MODEL_OLD_ASP:
		printk(KERN_INFO "LED (ASP-style) display at %p\n",
		       LED_DATA_REG);
		led_func_ptr = led_ASP_driver;
		break;

	default:
		printk(KERN_ERR "%s: Wrong LCD/LED model %d !\n",
		       __FUNCTION__, lcd_info.model);
		return;
	}
#endif
}

/*
 * XXX - could this move to lasi.c ??
 */

/*
   ** lasi_led_init()
   ** 
   ** lasi_led_init() is called from lasi.c with the base hpa  
   ** of the lasi controller chip. 
   ** Since Mirage and Electra machines use a different LED
   ** address register, we need to check for these machines 
   ** explicitly.
 */

#ifdef CONFIG_GSC_LASI
void __init lasi_led_init(unsigned long lasi_hpa)
{
	if (lcd_info.model != DISPLAY_MODEL_NONE ||
	    lasi_hpa == 0)
		return;

	printk("%s: CPU_HVERSION %x\n", __FUNCTION__, CPU_HVERSION);

	/* Mirage and Electra machines need special offsets */
	switch (CPU_HVERSION) {
	case 0x60A:		/* Mirage Jr (715/64) */
	case 0x60B:		/* Mirage 100 */
	case 0x60C:		/* Mirage 100+ */
	case 0x60D:		/* Electra 100 */
	case 0x60E:		/* Electra 120 */
		LED_DATA_REG = (char *) (lasi_hpa - 0x00020000);
		break;
	default:
		LED_DATA_REG = (char *) (lasi_hpa + 0x0000C000);
		break;
	}			/* switch() */

	lcd_info.model = DISPLAY_MODEL_LASI;
	register_led_driver();
}
#endif


/*
   ** asp_led_init()
   ** 
   ** asp_led_init() is called from asp.c with the ptr 
   ** to the LED display.
 */

#ifdef CONFIG_GSC_LASI
void __init asp_led_init(unsigned long led_ptr)
{
	if (lcd_info.model != DISPLAY_MODEL_NONE ||
	    led_ptr == 0)
		return;

	lcd_info.model = DISPLAY_MODEL_OLD_ASP;
	LED_DATA_REG = (char *) led_ptr;

	register_led_driver();
}

#endif



/*
   ** register_led_regions()
   ** 
   ** Simple function, which registers the LCD/LED regions for /procfs.
   ** At bootup - where the initialisation of the LCD/LED normally happens - 
   ** not all internal structures of request_region() are properly set up,
   ** so that we delay the registration until busdevice.c is executed.
   **
 */

void __init register_led_regions(void)
{
	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:
		request_region((unsigned long)LCD_CMD_REG,  1, "lcd_cmd");
		request_region((unsigned long)LCD_DATA_REG, 1, "lcd_data");
		break;
	case DISPLAY_MODEL_LASI:
	case DISPLAY_MODEL_OLD_ASP:
		request_region((unsigned long)LED_DATA_REG, 1, "led_data");
		break;
	}
}



/*
   ** led_init()
   ** 
   ** led_init() is called very early in the bootup-process from setup.c 
   ** and asks the PDC for an usable chassis LCD or LED.
   ** If the PDC doesn't return any info, then the LED
   ** is detected by lasi.c or asp.c and registered with the
   ** above functions lasi_led_init() or asp_led_init().
   ** KittyHawk machines have often a buggy PDC, so that
   ** we explicitly check for those machines here.
 */

int __init led_init(void)
{
#ifndef DISABLE_LEDS
	long pdc_result[32];

	printk("%s: CPU_HVERSION %x\n", __FUNCTION__, CPU_HVERSION);

	/* Work around the buggy PDC of KittyHawk-machines */
	switch (CPU_HVERSION) {
	case 0x580:		/* KittyHawk DC2-100 (K100) */
	case 0x581:		/* KittyHawk DC3-120 (K210) */
	case 0x582:		/* KittyHawk DC3 100 (K400) */
	case 0x583:		/* KittyHawk DC3 120 (K410) */
	case 0x58B:		/* KittyHawk DC2 100 (K200) */
		printk("%s: KittyHawk-Machine found !!\n", __FUNCTION__);
		goto found;	/* use the preinitialized values of lcd_info */

	default:
		break;
	}

	/* initialize pdc_result, so we can check the return values of pdc_chassis_info() */
	pdc_result[0] = pdc_result[1] = 0;

	if (pdc_chassis_info(&pdc_result, &lcd_info, sizeof(lcd_info)) == PDC_OK) {
		printk("%s: chassis info: model %d, ret0=%d, ret1=%d\n",
		 __FUNCTION__, lcd_info.model, pdc_result[0], pdc_result[1]);

		/* check the results. Some machines have a buggy PDC */
		if (pdc_result[0] <= 0 || pdc_result[0] != pdc_result[1])
			goto not_found;

		switch (lcd_info.model) {
		case DISPLAY_MODEL_LCD:	/* LCD display */
			if (pdc_result[0] != sizeof(struct pdc_chassis_lcd_info_ret_block)
			    && pdc_result[0] != sizeof(struct pdc_chassis_lcd_info_ret_block) - 1)
				 goto not_found;
			printk("%s: min_cmd_delay = %d uS\n",
		             __FUNCTION__, lcd_info.min_cmd_delay);
			break;

		case DISPLAY_MODEL_NONE:	/* no LED or LCD available */
			goto not_found;

		case DISPLAY_MODEL_LASI:	/* Lasi style 8 bit LED display */
			if (pdc_result[0] != 8 && pdc_result[0] != 32)
				goto not_found;
			break;

		default:
			printk(KERN_WARNING "Unknown LCD/LED model %d\n",
			       lcd_info.model);
			goto not_found;
		}		/* switch() */

found:
		/* register the LCD/LED driver */
		register_led_driver();
		return 0;

	}			/* if() */

not_found:
	lcd_info.model = DISPLAY_MODEL_NONE;
	return 1;
#endif
}
