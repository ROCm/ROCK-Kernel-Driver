#ifndef	__ASM_SH_KEYBOARD_H
#define	__ASM_SH_KEYBOARD_H
/*
 *	$Id: keyboard.h,v 1.1 2000/06/10 21:45:48 yaegashi Exp $
 */

#include <asm/machvec.h>

static __inline__ int kbd_setkeycode(unsigned int scancode,
				     unsigned int keycode)
{
    return -EOPNOTSUPP;
}

static __inline__ int kbd_getkeycode(unsigned int scancode)
{
    return scancode > 127 ? -EINVAL : scancode;
}

static __inline__ int kbd_translate(unsigned char scancode,
				    unsigned char *keycode, char raw_mode)
{
    *keycode = scancode;
    return 1;
}

static __inline__ char kbd_unexpected_up(unsigned char keycode)
{
    return 0200;
}

static __inline__ void kbd_leds(unsigned char leds)
{
}

extern void hp600_kbd_init_hw(void);

static __inline__ void kbd_init_hw(void)
{
	if (MACH_HP600) {
		hp600_kbd_init_hw();
	}
}

#endif
