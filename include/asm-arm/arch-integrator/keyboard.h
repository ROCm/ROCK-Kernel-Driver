/*
 *  linux/include/asm-arm/arch-integrator/keyboard.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Keyboard driver definitions for the Integrator architecture
 */
#include <asm/irq.h>

#define NR_SCANCODES 128

extern unsigned char kmi_kbd_sysrq_xlate[NR_SCANCODES];

extern int kmi_kbd_setkeycode(u_int scancode, u_int keycode);
extern int kmi_kbd_getkeycode(u_int scancode);
extern int kmi_kbd_translate(u_char scancode, u_char *keycode, char raw_mode);
extern char kmi_kbd_unexpected_up(u_char keycode);
extern void kmi_kbd_leds(u_char leds);
extern int kmi_kbd_init(void);

#define kbd_setkeycode(sc,kc)		kmi_kbd_setkeycode(sc,kc)
#define kbd_getkeycode(sc)		kmi_kbd_getkeycode(sc)

#define kbd_translate(sc, kcp, rm)	kmi_kbd_translate(sc,kcp,rm)
#define kbd_unexpected_up(kc)		kmi_kbd_unexpected_up(kc)
#define kbd_leds(leds)			kmi_kbd_leds(leds)
#define kbd_init_hw()			kmi_kbd_init()
#define kbd_sysrq_xlate			kmi_kbd_sysrq_xlate
#define kbd_disable_irq()		disable_irq(IRQ_KMIINT0)
#define kbd_enable_irq()		enable_irq(IRQ_KMIINT0)

#define SYSRQ_KEY			0x54
