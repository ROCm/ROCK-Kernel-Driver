/*
 * based on: sunkbd.c and ps2serkbd.c
 *
 * $Id: ps2serkbd.c,v 1.5 2001/09/25 10:12:07 vojtech Exp $
 */

/*
 * PS/2 keyboard via adapter at serial port driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/tqueue.h>

#define ATKBD_CMD_SETLEDS	0x10ed
#define ATKBD_CMD_GSCANSET	0x11f0
#define ATKBD_CMD_SSCANSET	0x10f0
#define ATKBD_CMD_GETID		0x02f2
#define ATKBD_CMD_ENABLE	0x00f4
#define ATKBD_CMD_RESET_DIS	0x00f5
#define ATKBD_CMD_SETALL_MB	0x00f8
#define ATKBD_CMD_EX_ENABLE	0x10ea
#define ATKBD_CMD_EX_SETLEDS	0x20eb

#define ATKBD_RET_ACK		0xfa
#define ATKBD_RET_NAK		0xfe

#define ATKBD_KEY_UNKNOWN	  0
#define ATKBD_KEY_BAT		251
#define ATKBD_KEY_EMUL0		252
#define ATKBD_KEY_EMUL1		253
#define ATKBD_KEY_RELEASE	254
#define ATKBD_KEY_NULL		255

static unsigned char ps2serkbd_set2_keycode[512] = {
    0, 67, 65, 63, 61, 59, 60, 88, 0, 68, 66, 64, 62, 15, 41, 85,
    0, 56, 42, 0, 29, 16, 2, 89, 0, 0, 44, 31, 30, 17, 3, 90,
    0, 46, 45, 32, 18, 5, 4, 91, 0, 57, 47, 33, 20, 19, 6, 0,
    0, 49, 48, 35, 34, 21, 7, 0, 0, 0, 50, 36, 22, 8, 9, 0,
    0, 51, 37, 23, 24, 11, 10, 0, 0, 52, 53, 38, 39, 25, 12, 0,
    122, 89, 40,120, 26, 13, 0, 0, 58, 54, 28, 27, 0, 43, 0, 0,
    85, 86, 90, 91, 92, 93, 14, 94, 95, 79, 0, 75, 71,121, 0,123,
    82, 83, 80, 76, 77, 72, 1, 69, 87, 78, 81, 74, 55, 73, 70, 99,
    252, 0, 0, 65, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,251, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    252,253, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    254, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,255,
    0, 0, 92, 90, 85, 0,137, 0, 0, 0, 0, 91, 89,144,115, 0,
    136,100,255, 0, 97,149,164, 0,156, 0, 0,140,115, 0, 0,125,
    0,150, 0,154,152,163,151,126,112,166, 0,140, 0,147, 0,127,
    159,167,139,160,163, 0, 0,116,158, 0,150,165, 0, 0, 0,142,
    157, 0,114,166,168, 0, 0, 0,155, 0, 98,113, 0,148, 0,138,
    0, 0, 0, 0, 0, 0,153,140, 0, 0, 96, 0, 0, 0,143, 0,
    133, 0,116, 0,143, 0,174,133, 0,107, 0,105,102, 0, 0,112,
    110,111,108,112,106,103, 0,119, 0,118,109, 0, 99,104,119
};

/*
 * Per-keyboard data.
 */

struct ps2serkbd {
    unsigned char keycode[512];
    struct input_dev dev;
    struct serio *serio;
    char name[64];
    char phys[32];
    struct tq_struct tq;
    unsigned char cmdbuf[4];
    unsigned char cmdcnt;
    unsigned char set;
    char release;
    char ack;
    char emul;
    char error;
    unsigned short id;
};

/*
 * ps2serkbd_interrupt() is called by the low level driver when a character
 * is received.
 */

static void ps2serkbd_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
    static int event_count=0;
    struct ps2serkbd* ps2serkbd = serio->private;
    int code=data;

#if 0
    printk(KERN_WARNING "ps2serkbd.c(%8d): (scancode %#x)\n", event_count, data);
#endif
    event_count++;

    switch (code) {
    case ATKBD_RET_ACK:
        ps2serkbd->ack = 1;
        return;
    case ATKBD_RET_NAK:
        ps2serkbd->ack = -1;
        return;
    }

    if (ps2serkbd->cmdcnt) {
        ps2serkbd->cmdbuf[--ps2serkbd->cmdcnt] = code;
        return;
    }

    switch (ps2serkbd->keycode[code]) {
    case ATKBD_KEY_BAT:
        queue_task(&ps2serkbd->tq, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
        return;
    case ATKBD_KEY_EMUL0:
        ps2serkbd->emul = 1;
        return;
    case ATKBD_KEY_EMUL1:
        ps2serkbd->emul = 2;
        return;
    case ATKBD_KEY_RELEASE:
        ps2serkbd->release = 1;
        return;
    }

    if (ps2serkbd->emul) {
        if (--ps2serkbd->emul) return;
        code |= 0x100;
    }

    switch (ps2serkbd->keycode[code]) {
    case ATKBD_KEY_NULL:
        break;
    case ATKBD_KEY_UNKNOWN:
        printk(KERN_WARNING "ps2serkbd.c: Unknown key (set %d, scancode %#x) %s.\n",
        ps2serkbd->set, code, ps2serkbd->release ? "released" : "pressed");
        break;
    default:
        input_report_key(&ps2serkbd->dev, ps2serkbd->keycode[code], !ps2serkbd->release);
	input_sync(&ps2serkbd->dev);
    }

    ps2serkbd->release = 0;
}

/*
 * ps2serkbd_event() handles events from the input module.
 */

static int ps2serkbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
    switch (type) {

    case EV_LED:

        return 0;
    }

    return -1;
}

static int ps2serkbd_initialize(struct ps2serkbd *ps2serkbd) 
{
    return 0;
}

static void ps2serkbd_reinit(void *data) 
{
}


static void ps2serkbd_connect(struct serio *serio, struct serio_dev *dev)
{
    struct ps2serkbd *ps2serkbd;
    int i;

    if ((serio->type & SERIO_TYPE) != SERIO_RS232)
        return;

    if ((serio->type & SERIO_PROTO) && (serio->type & SERIO_PROTO) != SERIO_PS2SER)
        return;


    if (!(ps2serkbd = kmalloc(sizeof(struct ps2serkbd), GFP_KERNEL)))
        return;

    memset(ps2serkbd, 0, sizeof(struct ps2serkbd));

    ps2serkbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
    ps2serkbd->dev.ledbit[0] = BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_SCROLLL);

    ps2serkbd->serio = serio;

    ps2serkbd->dev.keycode = ps2serkbd->keycode;
    ps2serkbd->dev.event = ps2serkbd_event;
    ps2serkbd->dev.private = ps2serkbd;

    ps2serkbd->tq.routine = ps2serkbd_reinit;
    ps2serkbd->tq.data = ps2serkbd;

    serio->private = ps2serkbd;

    if (serio_open(serio, dev)) {
        kfree(ps2serkbd);
        return;
    }

    if (ps2serkbd_initialize(ps2serkbd) < 0) {
        serio_close(serio);
        kfree(ps2serkbd);
        return;
    }

    ps2serkbd->set = 4;

    if (ps2serkbd->set == 4) {
        ps2serkbd->dev.ledbit[0] |= 0;
        sprintf(ps2serkbd->name, "AT Set 2 Extended keyboard\n");
    }
    memcpy(ps2serkbd->keycode, ps2serkbd_set2_keycode, sizeof(ps2serkbd->keycode));

    sprintf(ps2serkbd->phys, "%s/input0", serio->phys);

    ps2serkbd->dev.name = ps2serkbd->name;
    ps2serkbd->dev.phys = ps2serkbd->phys;
    ps2serkbd->dev.id.bustype = BUS_RS232; 
    ps2serkbd->dev.id.vendor = SERIO_PS2SER;
    ps2serkbd->dev.id.product = ps2serkbd->set;
    ps2serkbd->dev.id.version = ps2serkbd->id;

    for (i = 0; i < 512; i++)
        if (ps2serkbd->keycode[i] && ps2serkbd->keycode[i] <= 250)
            set_bit(ps2serkbd->keycode[i], ps2serkbd->dev.keybit);

    input_register_device(&ps2serkbd->dev);
}

/*
 * ps2serkbd_disconnect() unregisters and closes behind us.
 */

static void ps2serkbd_disconnect(struct serio *serio)
{
    struct ps2serkbd *ps2serkbd = serio->private;
    input_unregister_device(&ps2serkbd->dev);
    serio_close(serio);
    kfree(ps2serkbd);
}

static struct serio_dev ps2serkbd_dev = {
interrupt:
    ps2serkbd_interrupt,
connect:
    ps2serkbd_connect,
disconnect:
    ps2serkbd_disconnect
};

/*
 * The functions for insering/removing us as a module.
 */

int __init ps2serkbd_init(void)
{
    serio_register_device(&ps2serkbd_dev);
    return 0;
}

void __exit ps2serkbd_exit(void)
{
    serio_unregister_device(&ps2serkbd_dev);
}

module_init(ps2serkbd_init);
module_exit(ps2serkbd_exit);
