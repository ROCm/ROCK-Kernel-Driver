#include <asm/types.h>
#include <asm/bitops.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/proc_fs.h>

#include "av7110.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#include "input_fake.h"
#endif


#define UP_TIMEOUT (HZ/2)

static int av7110_ir_debug = 0;

#define dprintk(x...)  do { if (av7110_ir_debug) printk (x); } while (0)


static struct input_dev input_dev;


static
u16 key_map [256] = {
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
	KEY_8, KEY_9, KEY_MHP, 0, KEY_POWER, KEY_MUTE, 0, KEY_INFO,
	KEY_VOLUMEUP, KEY_VOLUMEDOWN, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	KEY_CHANNELUP, KEY_CHANNELDOWN, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, KEY_TEXT, 0, 0, KEY_TV, 0, 0, 0, 0, 0, KEY_SETUP, 0, 0,
	0, 0, 0, KEY_SUBTITLE, 0, 0, KEY_LANGUAGE, 0,
	KEY_RADIO, 0, 0, 0, 0, KEY_EXIT, 0, 0, 
	KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_RED, KEY_GREEN, KEY_YELLOW,
	KEY_BLUE, 0, 0, 0, 0, 0, 0, 0, KEY_MENU, KEY_LIST, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN,
	0, 0, 0, 0, KEY_EPG, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_VCR
};


static
void av7110_emit_keyup (unsigned long data)
{
	if (!data || !test_bit (data, input_dev.key))
		return;

	input_event (&input_dev, EV_KEY, data, !!0);
}


static
struct timer_list keyup_timer = { function: av7110_emit_keyup };


static
void av7110_emit_key (u32 ircom)
{
	int down = ircom & (0x80000000);
	u16 keycode = key_map[ircom & 0xff];

	dprintk ("#########%08x######### key %02x %s (keycode %i)\n",
		 ircom, ircom & 0xff, down ? "pressed" : "released", keycode);

	if (!keycode) {
		printk ("%s: unknown key 0x%02x!!\n",
			__FUNCTION__, ircom & 0xff);
		return;
	}

	if (timer_pending (&keyup_timer)) {
		del_timer (&keyup_timer);
		if (keyup_timer.data != keycode)
			input_event (&input_dev, EV_KEY, keyup_timer.data, !!0);
	}

	clear_bit (keycode, input_dev.key);

	input_event (&input_dev, EV_KEY, keycode, !0);

	keyup_timer.expires = jiffies + UP_TIMEOUT;
	keyup_timer.data = keycode;

	add_timer (&keyup_timer);
}

static
void input_register_keys (void)
{
	int i;

	memset (input_dev.keybit, 0, sizeof(input_dev.keybit));

	for (i=0; i<sizeof(key_map)/sizeof(key_map[0]); i++) {
		if (key_map[i] > KEY_MAX)
			key_map[i] = 0;
		else if (key_map[i] > KEY_RESERVED)
			set_bit (key_map[i], input_dev.keybit);
	}
}


static
int av7110_ir_write_proc (struct file *file, const char *buffer,
	                  unsigned long count, void *data)
{
	u32 ir_config;

	if (count < 4 + 256 * sizeof(u16))
		return -EINVAL;
	
	memcpy (&ir_config, buffer, 4);
	memcpy (&key_map, buffer + 4, 256 * sizeof(u16));

	av7110_setup_irc_config (NULL, ir_config);

	input_register_keys ();

	return count;
}


int __init av7110_ir_init (void)
{
	static struct proc_dir_entry *e;

	init_timer (&keyup_timer);
	keyup_timer.data = 0;

        input_dev.name = "DVB on-card IR receiver";

        /**
         *  enable keys
         */
        set_bit (EV_KEY, input_dev.evbit);

	input_register_keys ();

	input_register_device(&input_dev);

	av7110_setup_irc_config (NULL, 0x0001);
	av7110_register_irc_handler (av7110_emit_key);

	e = create_proc_entry ("av7110_ir", S_IFREG | S_IRUGO | S_IWUSR, NULL);

	if (e) {
		e->write_proc = av7110_ir_write_proc;
		e->size = 4 + 256 * sizeof(u16);
	}

	return 0;
}


void __exit av7110_ir_exit (void)
{
	remove_proc_entry ("av7110_ir", NULL);
	av7110_unregister_irc_handler (av7110_emit_key);
	input_unregister_device(&input_dev);
}

//MODULE_AUTHOR("Holger Waechtler <holger@convergence.de>");
//MODULE_LICENSE("GPL");

MODULE_PARM(av7110_ir_debug,"i");
MODULE_PARM_DESC(av7110_ir_debug, "enable AV7110 IR receiver debug messages");

