#ifndef LED_H
#define LED_H


#define	LED7		0x80		/* top (or furthest right) LED */
#define	LED6		0x40
#define	LED5		0x20
#define	LED4		0x10
#define	LED3		0x08
#define	LED2		0x04
#define	LED1		0x02
#define	LED0		0x01		/* bottom (or furthest left) LED */

#define	LED_LAN_TX	LED0		/* for LAN transmit activity */
#define	LED_LAN_RCV	LED1		/* for LAN receive activity */
#define	LED_DISK_IO	LED2		/* for disk activity */
#define	LED_HEARTBEAT	LED3		/* heartbeat */


/* irq function */
extern void led_interrupt_func(void);

/* LASI & ASP specific LED initialization funcs */
extern void __init lasi_led_init( unsigned long lasi_hpa );
extern void __init asp_led_init( unsigned long led_ptr );

/* registers the LED regions for procfs */
extern void __init register_led_regions(void);

/* main LED initialization function (uses the PDC) */ 
extern int __init led_init(void);

#endif /* LED_H */
