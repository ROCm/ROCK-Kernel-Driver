/*
 *  linux/include/asm-m68k/q40_keyboard.h
 *
 *  Created 
 */

/*
 *  This file contains the Q40 specific keyboard definitions
 */


#ifdef __KERNEL__


#include <asm/machdep.h>



extern int q40kbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int q40kbd_getkeycode(unsigned int scancode);
extern int q40kbd_pretranslate(unsigned char scancode, char raw_mode);
extern int q40kbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char q40kbd_unexpected_up(unsigned char keycode);
extern void q40kbd_leds(unsigned char leds);
extern void q40kbd_init_hw(void);
extern unsigned char q40kbd_sysrq_xlate[128];


#if 0
#define kbd_setkeycode		q40kbd_setkeycode
#define kbd_getkeycode		q40kbd_getkeycode
#define kbd_pretranslate	q40kbd_pretranslate
#define kbd_translate		q40kbd_translate
#define kbd_unexpected_up	q40kbd_unexpected_up
#define kbd_leds		q40kbd_leds
#define kbd_init_hw		q40kbd_init_hw
#define kbd_sysrq_xlate		q40kbd_sysrq_xlate


#define SYSRQ_KEY 0x54
#endif
#endif /* __KERNEL__ */





