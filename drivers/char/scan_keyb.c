/*
 *	$Id: scan_keyb.c,v 1.2 2000/07/04 06:24:42 yaegashi Exp $ 
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Generic scan keyboard driver
 */

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/kbd_kern.h>

struct scan_keyboard {
	struct scan_keyboard *next;
	void (*scan)(unsigned char *buffer);
	const unsigned char *table;
	unsigned char *s0, *s1;
	int length;
};
		     
static struct scan_keyboard *keyboards=NULL;
static struct tq_struct task_scan_kbd;	

static void check_kbd(const unsigned char *table,
		      unsigned char *new, unsigned char *old, int length)
{
	int need_tasklet_schedule=0;
	unsigned char xor, bit;
	
	while(length-->0) {
		if((xor=*new^*old)==0) {
			table+=8;
		}
		else {
			for(bit=0x80; bit!=0; bit>>=1) {
				if(xor&bit) {
					handle_scancode(*table, !(*new&bit));
					need_tasklet_schedule=1;
				}
				table++;
			}
		}
		new++; old++;
	}

	if(need_tasklet_schedule)
		tasklet_schedule(&keyboard_tasklet);
}


static void scan_kbd(void *dummy)
{
	struct scan_keyboard *kbd;

	for(kbd=keyboards; kbd!=NULL; kbd=kbd->next) {
		if(jiffies&1) {
			kbd->scan(kbd->s0);
			check_kbd(kbd->table, kbd->s0, kbd->s1, kbd->length);
		}
		else {
			kbd->scan(kbd->s1);
			check_kbd(kbd->table, kbd->s1, kbd->s0, kbd->length);
		}
		
	}
	queue_task(&task_scan_kbd, &tq_timer);
}


int register_scan_keyboard(void (*scan)(unsigned char *buffer),
			   const unsigned char *table,
			   int length)
{
	struct scan_keyboard *kbd;

	kbd = kmalloc(sizeof(struct scan_keyboard), GFP_KERNEL);
	if (kbd == NULL)
		goto error_out;

	kbd->scan=scan;
	kbd->table=table;
	kbd->length=length;

	kbd->s0 = kmalloc(length, GFP_KERNEL);
	if (kbd->s0 == NULL)
		goto error_free_kbd;

	kbd->s1 = kmalloc(length, GFP_KERNEL);
	if (kbd->s1 == NULL)
		goto error_free_s0;

	kbd->scan(kbd->s0);
	kbd->scan(kbd->s1);
	
	kbd->next=keyboards;
	keyboards=kbd;

	return 0;

 error_free_s0:
	kfree(kbd->s0);

 error_free_kbd:
	kfree(kbd);

 error_out:
	return -ENOMEM;
}
			      
			      
void __init scan_kbd_init(void)
{

	INIT_LIST_HEAD(task_scan_kbd.list);
	task_scan_kbd.sync=0;
	task_scan_kbd.routine=scan_kbd;
	task_scan_kbd.data=NULL;
	queue_task(&task_scan_kbd, &tq_timer);
	printk(KERN_INFO "Generic scan keyboard driver initialized\n");
}
