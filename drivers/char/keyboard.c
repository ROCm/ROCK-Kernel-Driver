/*
 * linux/drivers/char/keyboard.c
 *
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 *
 * Diacriticals redone & other small changes, aeb@cwi.nl, June 1993
 * Added decr/incr_console, dynamic keymaps, Unicode support,
 * dynamic function/string keys, led setting,  Sept 1994
 * `Sticky' modifier keys, 951006.
 *
 * 11-11-96: SAK should now work in the raw mode (Martin Mares)
 * 
 * Modified to provide 'generic' keyboard support by Hamish Macdonald
 * Merge with the m68k keyboard driver and split-off of the PC low-level
 * parts by Geert Uytterhoeven, May 1997
 *
 * 27-05-97: Added support for the Magic SysRq Key (Martin Mares)
 * 30-07-98: Dead keys redone, aeb@cwi.nl.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/init.h>

#include <asm/keyboard.h>
#include <asm/bitops.h>

#include <linux/console_struct.h>
#include <linux/kbd_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/vt_kern.h>
#include <linux/kbd_ll.h>
#include <linux/sysrq.h>
#include <linux/pm.h>

extern void ctrl_alt_del(void);

#define SIZE(x) (sizeof(x)/sizeof((x)[0]))

/*
 * Exported functions/variables
 */

#ifndef KBD_DEFMODE
#define KBD_DEFMODE ((1 << VC_REPEAT) | (1 << VC_META))
#endif

#ifndef KBD_DEFLEDS
/*
 * Some laptops take the 789uiojklm,. keys as number pad when NumLock
 * is on. This seems a good reason to start with NumLock off.
 */
#define KBD_DEFLEDS 0
#endif

#ifndef KBD_DEFLOCK
#define KBD_DEFLOCK 0
#endif

void (*kbd_ledfunc)(unsigned int led);
EXPORT_SYMBOL(handle_scancode);
EXPORT_SYMBOL(kbd_ledfunc);
/* kbd_pt_regs - set by keyboard_interrupt(), used by show_ptregs() */
struct pt_regs * kbd_pt_regs;
void compute_shiftstate(void);

/*
 * Handler Tables.
 */

/* Key types processed even in raw modes */

#define TYPES_ALLOWED_IN_RAW_MODE ((1 << KT_SPEC) | (1 << KT_SHIFT))
#define SPECIALS_ALLOWED_IN_RAW_MODE (1 << KVAL(K_SAK))

#define K_HANDLERS\
	k_self,		k_fn,		k_spec,		k_pad,\
	k_dead,		k_cons,		k_cur,		k_shift,\
	k_meta,		k_ascii,	k_lock,		k_lowercase,\
	k_slock,	k_dead2,	k_ignore,	k_ignore	

typedef void (k_handler_fn)(struct vc_data *vc, unsigned char value, 
			    char up_flag);
static k_handler_fn K_HANDLERS;
static k_handler_fn *k_handler[16] = { K_HANDLERS };

#define FN_HANDLERS\
	fn_null, 	fn_enter,	fn_show_ptregs,	fn_show_mem,\
	fn_show_state,	fn_send_intr, 	fn_lastcons, 	fn_caps_toggle,\
	fn_num,		fn_hold, 	fn_scroll_forw,	fn_scroll_back,\
	fn_boot_it, 	fn_caps_on, 	fn_compose,	fn_SAK,\
	fn_dec_console, fn_inc_console, fn_spawn_con, 	fn_bare_num

typedef void (fn_handler_fn)(struct vc_data *vc);
static fn_handler_fn FN_HANDLERS;
static fn_handler_fn *fn_handler[] = { FN_HANDLERS };

/*
 * Variables/functions exported for vt.c
 */

/* maximum values each key_handler can handle */
const int max_vals[] = {
	255, SIZE(func_table) - 1, SIZE(fn_handler) - 1, NR_PAD - 1,
	NR_DEAD - 1, 255, 3, NR_SHIFT - 1, 255, NR_ASCII - 1, NR_LOCK - 1,
	255, NR_LOCK - 1, 255
};

const int NR_TYPES = SIZE(max_vals);

int spawnpid, spawnsig;

/*
 * Translation of escaped scancodes to keycodes.
 * This is now user-settable (for machines were it makes sense).
 */
int getkeycode(unsigned int scancode)
{
    return kbd_getkeycode(scancode);
}

int setkeycode(unsigned int scancode, unsigned int keycode)
{
    return kbd_setkeycode(scancode, keycode);
}

/*
 * Variables/function exported for console.c
 */
int shift_state = 0;

/*
 * Internal Data.
 */

static unsigned long key_down[256/BITS_PER_LONG]; /* keyboard key bitmap */
static unsigned char shift_down[NR_SHIFT];	/* shift state counters.. */	
static int dead_key_next;
static int npadch = -1;			/* -1 or number assembled on pad */
static unsigned char diacr;
static char rep;			/* flag telling character repeat */
pm_callback pm_kbd_request_override = NULL;
typedef void (pm_kbd_func) (void);
static struct pm_dev *pm_kbd;

static unsigned char ledstate = 0xff; /* undefined */
static unsigned char ledioctl;

static struct ledptr {
    unsigned int *addr;
    unsigned int mask;
    unsigned char valid:1;
} ledptrs[3];

struct kbd_struct kbd_table[MAX_NR_CONSOLES];
static struct kbd_struct *kbd = kbd_table;
#ifdef CONFIG_MAGIC_SYSRQ
static int sysrq_pressed;
#endif

/*
 * Helper Functions.
 */
void put_queue(struct vc_data *vc, int ch)
{
	struct tty_struct *tty = vc->vc_tty;	

	if (tty) {
		tty_insert_flip_char(tty, ch, 0);
		con_schedule_flip(tty);
	}
}

static void puts_queue(struct vc_data *vc, char *cp)
{
	struct tty_struct *tty = vc->vc_tty;

	if (!tty)
		return;

	while (*cp) {
		tty_insert_flip_char(tty, *cp, 0);
		cp++;
	}
	con_schedule_flip(tty);
}

static void applkey(struct vc_data *vc, int key, char mode)
{
	static char buf[] = { 0x1b, 'O', 0x00, 0x00 };

	buf[1] = (mode ? 'O' : '[');
	buf[2] = key;
	puts_queue(vc, buf);
}

/*
 * Many other routines do put_queue, but I think either
 * they produce ASCII, or they produce some user-assigned
 * string, and in both cases we might assume that it is
 * in utf-8 already. UTF-8 is defined for words of up to 31 bits,
 * but we need only 16 bits here
 */
void to_utf8(struct vc_data *vc, ushort c) 
{
	if (c < 0x80)
		/*  0*******  */
		put_queue(vc, c);
    	else if (c < 0x800) {
		/*  110***** 10******  */
		put_queue(vc, 0xc0 | (c >> 6)); 
		put_queue(vc, 0x80 | (c & 0x3f));
    	} else {
		/*  1110**** 10****** 10*******/
		put_queue(vc, 0xe0 | (c >> 12));
		put_queue(vc, 0x80 | ((c >> 6) & 0x3f));
		put_queue(vc, 0x80 | (c & 0x3f));
    	}
}

/* called after returning from RAW mode or when changing consoles -
   recompute shift_down[] and shift_state from key_down[] */
/* maybe called when keymap is undefined, so that shiftkey release is seen */
void compute_shiftstate(void)
{
	int i, j, k, sym, val;

	shift_state = 0;
	memset(shift_down, 0, sizeof(shift_down));
	
	for (i = 0; i < SIZE(key_down); i++) {
	  	
		if (!key_down[i])
			continue;	    

		k = i*BITS_PER_LONG;
	
		for (j = 0; j < BITS_PER_LONG; j++, k++) {
	      
			if (!test_bit(k, key_down))
				continue;
	
			sym = U(plain_map[k]);
			if (KTYP(sym) != KT_SHIFT && KTYP(sym) != KT_SLOCK)
				continue;

	  		val = KVAL(sym);
		  	if (val == KVAL(K_CAPSSHIFT))
		    		val = KVAL(K_SHIFT);
		  	
			shift_down[val]++;
		  	shift_state |= (1<<val);
		}
	}
}

/*
 * We have a combining character DIACR here, followed by the character CH.
 * If the combination occurs in the table, return the corresponding value.
 * Otherwise, if CH is a space or equals DIACR, return DIACR.
 * Otherwise, conclude that DIACR was not combining after all,
 * queue it and return CH.
 */
unsigned char handle_diacr(struct vc_data *vc, unsigned char ch)
{
	int d = diacr;
	int i;

	diacr = 0;

	for (i = 0; i < accent_table_size; i++) {
		if (accent_table[i].diacr == d && accent_table[i].base == ch)
			return accent_table[i].result;
	}

	if (ch == ' ' || ch == d)
		return d;

	put_queue(vc, d);
	return ch;
}

/*
 * Special function handlers
 */
static void fn_enter(struct vc_data *vc)
{
	if (diacr) {
		put_queue(vc, diacr);
		diacr = 0;
	}
	put_queue(vc, 13);
	if (vc_kbd_mode(kbd, VC_CRLF))
		put_queue(vc, 10);
}

static void fn_caps_toggle(struct vc_data *vc)
{
	if (rep)
		return;
	chg_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void fn_caps_on(struct vc_data *vc)
{
	if (rep)
		return;
	set_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void fn_show_ptregs(struct vc_data *vc)
{
	if (kbd_pt_regs)
		show_regs(kbd_pt_regs);
}

static void fn_hold(struct vc_data *vc)
{
	struct tty_struct *tty = vc->vc_tty;

	if (rep || !tty)
		return;

	/*
	 * Note: SCROLLOCK will be set (cleared) by stop_tty (start_tty);
	 * these routines are also activated by ^S/^Q.
	 * (And SCROLLOCK can also be set by the ioctl KDSKBLED.)
	 */
	if (tty->stopped)
		start_tty(tty);
	else
		stop_tty(tty);
}

static void fn_num(struct vc_data *vc)
{
	if (vc_kbd_mode(kbd,VC_APPLIC))
		applkey(vc, 'P', 1);
	else
		fn_bare_num(vc);
}

/*
 * Bind this to Shift-NumLock if you work in application keypad mode
 * but want to be able to change the NumLock flag.
 * Bind this to NumLock if you prefer that the NumLock key always
 * changes the NumLock flag.
 */
static void fn_bare_num(struct vc_data *vc)
{
	if (!rep)
		chg_vc_kbd_led(kbd,VC_NUMLOCK);
}

static void fn_lastcons(struct vc_data *vc)
{
	/* switch to the last used console, ChN */
	set_console(last_console);
}

static void fn_dec_console(struct vc_data *vc)
{
	int i;
 
	for (i = fg_console-1; i != fg_console; i--) {
		if (i == -1)
			i = MAX_NR_CONSOLES-1;
		if (vc_cons_allocated(i))
			break;
	}
	set_console(i);
}

static void fn_inc_console(struct vc_data *vc)
{
	int i;

	for (i = fg_console+1; i != fg_console; i++) {
		if (i == MAX_NR_CONSOLES)
			i = 0;
		if (vc_cons_allocated(i))
			break;
	}
	set_console(i);
}

static void fn_send_intr(struct vc_data *vc)
{
	struct tty_struct *tty = vc->vc_tty;

	if (!tty)
		return;
	tty_insert_flip_char(tty, 0, TTY_BREAK);
	con_schedule_flip(tty);
}

static void fn_scroll_forw(struct vc_data *vc)
{
	scrollfront(0);
}

static void fn_scroll_back(struct vc_data *vc)
{
	scrollback(0);
}

static void fn_show_mem(struct vc_data *vc)
{
	show_mem();
}

static void fn_show_state(struct vc_data *vc)
{
	show_state();
}

static void fn_boot_it(struct vc_data *vc)
{
	ctrl_alt_del();
}

static void fn_compose(struct vc_data *vc)
{
	dead_key_next = 1;
}

static void fn_spawn_con(struct vc_data *vc)
{
        if (spawnpid)
	   if(kill_proc(spawnpid, spawnsig, 1))
	     spawnpid = 0;
}

static void fn_SAK(struct vc_data *vc)
{
	struct tty_struct *tty = vc->vc_tty;

	/*
	 * SAK should also work in all raw modes and reset
	 * them properly.
	 */
	if (tty)
		do_SAK(tty);
	reset_vc(fg_console);
#if 0
	do_unblank_screen();	/* not in interrupt routine? */
#endif
}

static void fn_null(struct vc_data *vc)
{
	compute_shiftstate();
}

/*
 * Special key handlers
 */
static void k_ignore(struct vc_data *vc, unsigned char value, char up_flag)
{
}

static void k_spec(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value >= SIZE(fn_handler))
		return;
	if ((kbd->kbdmode == VC_RAW || kbd->kbdmode == VC_MEDIUMRAW) &&
	    !(SPECIALS_ALLOWED_IN_RAW_MODE & (1 << value)))
		return;
	fn_handler[value](vc);
}

static void k_lowercase(struct vc_data *vc, unsigned char value, char up_flag)
{
	printk(KERN_ERR "keyboard.c: do_lowercase was called - impossible\n");
}

static void k_self(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;		/* no action, if this is a key release */

	if (diacr)
		value = handle_diacr(vc, value);

	if (dead_key_next) {
		dead_key_next = 0;
		diacr = value;
		return;
	}
	put_queue(vc, value);
}

#define A_GRAVE  '`'
#define A_ACUTE  '\''
#define A_CFLEX  '^'
#define A_TILDE  '~'
#define A_DIAER  '"'
#define A_CEDIL  ','
static unsigned char ret_diacr[NR_DEAD] =
	{A_GRAVE, A_ACUTE, A_CFLEX, A_TILDE, A_DIAER, A_CEDIL };

/* Obsolete - for backwards compatibility only */
static void k_dead(struct vc_data *vc, unsigned char value, char up_flag)
{
	value = ret_diacr[value];
	k_dead2(vc, value,up_flag);
}

/*
 * Handle dead key. Note that we now may have several
 * dead keys modifying the same character. Very useful
 * for Vietnamese.
 */
static void k_dead2(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	diacr = (diacr ? handle_diacr(vc, value) : value);
}

static void k_cons(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	set_console(value);
}

static void k_fn(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value < SIZE(func_table)) {
		if (func_table[value])
			puts_queue(vc, func_table[value]);
	} else
		printk(KERN_ERR "k_fn called with value=%d\n", value);
}

static void k_pad(struct vc_data *vc, unsigned char value, char up_flag)
{
	static const char *pad_chars = "0123456789+-*/\015,.?()";
	static const char *app_map = "pqrstuvwxylSRQMnnmPQ";

	if (up_flag)
		return;		/* no action, if this is a key release */

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_mode(kbd,VC_APPLIC) && !shift_down[KG_SHIFT]) {
		applkey(vc, app_map[value], 1);
		return;
	}

	if (!vc_kbd_led(kbd,VC_NUMLOCK))
		switch (value) {
			case KVAL(K_PCOMMA):
			case KVAL(K_PDOT):
				k_fn(vc, KVAL(K_REMOVE), 0);
				return;
			case KVAL(K_P0):
				k_fn(vc, KVAL(K_INSERT), 0);
				return;
			case KVAL(K_P1):
				k_fn(vc, KVAL(K_SELECT), 0);
				return;
			case KVAL(K_P2):
				k_cur(vc, KVAL(K_DOWN), 0);
				return;
			case KVAL(K_P3):
				k_fn(vc, KVAL(K_PGDN), 0);
				return;
			case KVAL(K_P4):
				k_cur(vc, KVAL(K_LEFT), 0);
				return;
			case KVAL(K_P6):
				k_cur(vc, KVAL(K_RIGHT), 0);
				return;
			case KVAL(K_P7):
				k_fn(vc, KVAL(K_FIND), 0);
				return;
			case KVAL(K_P8):
				k_cur(vc, KVAL(K_UP), 0);
				return;
			case KVAL(K_P9):
				k_fn(vc, KVAL(K_PGUP), 0);
				return;
			case KVAL(K_P5):
				applkey(vc, 'G', vc_kbd_mode(kbd, VC_APPLIC));
				return;
		}

	put_queue(vc, pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_mode(kbd, VC_CRLF))
		put_queue(vc, 10);
}

static void k_cur(struct vc_data *vc, unsigned char value, char up_flag)
{
	static const char *cur_chars = "BDCA";

	if (up_flag)
		return;
	applkey(vc, cur_chars[value], vc_kbd_mode(kbd,VC_CKMODE));
}

static void k_shift(struct vc_data *vc, unsigned char value, char up_flag)
{
	int old_state = shift_state;

	if (rep)
		return;

	/* Mimic typewriter:
	   a CapsShift key acts like Shift but undoes CapsLock */
	if (value == KVAL(K_CAPSSHIFT)) {
		value = KVAL(K_SHIFT);
		if (!up_flag)
			clr_vc_kbd_led(kbd, VC_CAPSLOCK);
	}

	if (up_flag) {
		/* handle the case that two shift or control
		   keys are depressed simultaneously */
		if (shift_down[value])
			shift_down[value]--;
	} else
		shift_down[value]++;

	if (shift_down[value])
		shift_state |= (1 << value);
	else
		shift_state &= ~ (1 << value);

	/* kludge */
	if (up_flag && shift_state != old_state && npadch != -1) {
		if (kbd->kbdmode == VC_UNICODE)
		  to_utf8(vc, npadch & 0xffff);
		else
		  put_queue(vc, npadch & 0xff);
		npadch = -1;
	}
}

static void k_meta(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	if (vc_kbd_mode(kbd, VC_META)) {
		put_queue(vc, '\033');
		put_queue(vc, value);
	} else
		put_queue(vc, value | 0x80);
}

static void k_ascii(struct vc_data *vc, unsigned char value, char up_flag)
{
	int base;

	if (up_flag)
		return;

	if (value < 10)    /* decimal input of code, while Alt depressed */
	    base = 10;
	else {       /* hexadecimal input of code, while AltGr depressed */
	    value -= 10;
	    base = 16;
	}

	if (npadch == -1)
	  npadch = value;
	else
	  npadch = npadch * base + value;
}

static void k_lock(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag || rep)
		return;
	chg_vc_kbd_lock(kbd, value);
}

static void k_slock(struct vc_data *vc, unsigned char value, char up_flag)
{
	k_shift(vc, value,up_flag);
	if (up_flag || rep)
		return;
	chg_vc_kbd_slock(kbd, value);
	/* try to make Alt, oops, AltGr and such work */
	if (!key_maps[kbd->lockstate ^ kbd->slockstate]) {
		kbd->slockstate = 0;
		chg_vc_kbd_slock(kbd, value);
	}
}

/*
 * The leds display either (i) the status of NumLock, CapsLock, ScrollLock,
 * or (ii) whatever pattern of lights people want to show using KDSETLED,
 * or (iii) specified bits of specified words in kernel memory.
 */
unsigned char getledstate(void) {
    return ledstate;
}

void setledstate(struct kbd_struct *kbd, unsigned int led) {
    if (!(led & ~7)) {
	ledioctl = led;
	kbd->ledmode = LED_SHOW_IOCTL;
    } else
	kbd->ledmode = LED_SHOW_FLAGS;
    set_leds();
}

void register_leds(int console, unsigned int led,
		   unsigned int *addr, unsigned int mask) {
    struct kbd_struct *kbd = kbd_table + console;
    if (led < 3) {
	ledptrs[led].addr = addr;
	ledptrs[led].mask = mask;
	ledptrs[led].valid = 1;
	kbd->ledmode = LED_SHOW_MEM;
    } else
	kbd->ledmode = LED_SHOW_FLAGS;
}

static inline unsigned char getleds(void){
    struct kbd_struct *kbd = kbd_table + fg_console;
    unsigned char leds;

    if (kbd->ledmode == LED_SHOW_IOCTL)
      return ledioctl;
    leds = kbd->ledflagstate;
    if (kbd->ledmode == LED_SHOW_MEM) {
	if (ledptrs[0].valid) {
	    if (*ledptrs[0].addr & ledptrs[0].mask)
	      leds |= 1;
	    else
	      leds &= ~1;
	}
	if (ledptrs[1].valid) {
	    if (*ledptrs[1].addr & ledptrs[1].mask)
	      leds |= 2;
	    else
	      leds &= ~2;
	}
	if (ledptrs[2].valid) {
	    if (*ledptrs[2].addr & ledptrs[2].mask)
	      leds |= 4;
	    else
	      leds &= ~4;
	}
    }
    return leds;
}

/*
 * This routine is the bottom half of the keyboard interrupt
 * routine, and runs with all interrupts enabled. It does
 * console changing, led setting and copy_to_cooked, which can
 * take a reasonably long time.
 *
 * Aside from timing (which isn't really that important for
 * keyboard interrupts as they happen often), using the software
 * interrupt routines for this thing allows us to easily mask
 * this when we don't want any of the above to happen. Not yet
 * used, but this allows for easy and efficient race-condition
 * prevention later on.
 */

static void kbd_bh(unsigned long dummy)
{
	unsigned char leds = getleds();

	if (leds != ledstate) {
		ledstate = leds;
		kbd_leds(leds);
		if (kbd_ledfunc) kbd_ledfunc(leds);
	}
}

EXPORT_SYMBOL(keyboard_tasklet);
DECLARE_TASKLET_DISABLED(keyboard_tasklet, kbd_bh, 0);

void handle_scancode(unsigned char scancode, int down)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	char up_flag = down ? 0 : 0200;
	struct tty_struct *tty;
	unsigned char keycode;
	char raw_mode;

	pm_access(pm_kbd);
	add_keyboard_randomness(scancode | up_flag);

	tty = vc->vc_tty;

	if (tty && (!tty->driver_data)) {
		/*
		 * We touch the tty structure via the ttytab array
		 * without knowing whether or not tty is open, which
		 * is inherently dangerous.  We currently rely on that
		 * fact that console_open sets tty->driver_data when
		 * it opens it, and clears it when it closes it.
		 */
		tty = NULL;
	}
	kbd = kbd_table + fg_console;
	if ((raw_mode = (kbd->kbdmode == VC_RAW))) {
		put_queue(vc, scancode | up_flag);
		/* we do not return yet, because we want to maintain
		   the key_down array, so that we have the correct
		   values when finishing RAW mode or when changing VT's */
	}

	/*
	 *  Convert scancode to keycode
	 */
	if (!kbd_translate(scancode, &keycode, raw_mode))
		goto out;

	/*
	 * At this point the variable `keycode' contains the keycode.
	 * Note: the keycode must not be 0 (++Geert: on m68k 0 is valid).
	 * We keep track of the up/down status of the key, and
	 * return the keycode if in MEDIUMRAW mode.
	 */

	if (up_flag) {
		rep = 0;
		if(!test_and_clear_bit(keycode, key_down))
		    up_flag = kbd_unexpected_up(keycode);
	} else
		rep = test_and_set_bit(keycode, key_down);

#ifdef CONFIG_MAGIC_SYSRQ		/* Handle the SysRq Hack */
	if (keycode == SYSRQ_KEY) {
		sysrq_pressed = !up_flag;
		goto out;
	} else if (sysrq_pressed) {
		if (!up_flag) {
			handle_sysrq(kbd_sysrq_xlate[keycode], kbd_pt_regs, tty);
			goto out;
		}
	}
#endif

	if (kbd->kbdmode == VC_MEDIUMRAW) {
		/* soon keycodes will require more than one byte */
		put_queue(vc, keycode + up_flag);
		raw_mode = 1;	/* Most key classes will be ignored */
	}

	/*
	 * Small change in philosophy: earlier we defined repetition by
	 *	 rep = keycode == prev_keycode;
	 *	 prev_keycode = keycode;
	 * but now by the fact that the depressed key was down already.
	 * Does this ever make a difference? Yes.
	 */

	/*
	 *  Repeat a key only if the input buffers are empty or the
	 *  characters get echoed locally. This makes key repeat usable
	 *  with slow applications and under heavy loads.
	 */
	if (!rep ||
	    (vc_kbd_mode(kbd,VC_REPEAT) && tty &&
	     (L_ECHO(tty) || (tty->driver.chars_in_buffer(tty) == 0)))) {
		u_short keysym;
		u_char type;

		/* the XOR below used to be an OR */
		int shift_final = (shift_state | kbd->slockstate) ^
		    kbd->lockstate;
		ushort *key_map = key_maps[shift_final];

		if (key_map != NULL) {
			keysym = key_map[keycode];
			type = KTYP(keysym);

			if (type >= 0xf0) {
			    type -= 0xf0;
			    if (raw_mode && ! (TYPES_ALLOWED_IN_RAW_MODE & (1 << type)))
				goto out;
			    if (type == KT_LETTER) {
				type = KT_LATIN;
				if (vc_kbd_led(kbd, VC_CAPSLOCK)) {
				    key_map = key_maps[shift_final ^ (1<<KG_SHIFT)];
				    if (key_map)
				      keysym = key_map[keycode];
				}
			    }
			    (*k_handler[type])(vc, keysym & 0xff, up_flag);
			    if (type != KT_SLOCK)
			      kbd->slockstate = 0;
			} else {
			    /* maybe only if (kbd->kbdmode == VC_UNICODE) ? */
			    if (!up_flag && !raw_mode)
			      to_utf8(vc, keysym);
			}
		} else {
			/* maybe beep? */
			/* we have at least to update shift_state */
#if 1			/* how? two almost equivalent choices follow */
			compute_shiftstate();
			kbd->slockstate = 0; /* play it safe */
#else
			keysym =  U(plain_map[keycode]);
			type = KTYP(keysym);
			if (type == KT_SHIFT)
			  (*key_handler[type])(keysym & 0xff, up_flag);
#endif
		}
	}
out:
	do_poke_blanked_console = 1;
	schedule_console_callback();
}

int __init kbd_init(void)
{
	int i;
	struct kbd_struct kbd0;

	kbd0.ledflagstate = kbd0.default_ledflagstate = KBD_DEFLEDS;
	kbd0.ledmode = LED_SHOW_FLAGS;
	kbd0.lockstate = KBD_DEFLOCK;
	kbd0.slockstate = 0;
	kbd0.modeflags = KBD_DEFMODE;
	kbd0.kbdmode = VC_XLATE;
 
	for (i = 0 ; i < MAX_NR_CONSOLES ; i++)
		kbd_table[i] = kbd0;

	kbd_init_hw();

	tasklet_enable(&keyboard_tasklet);
	tasklet_schedule(&keyboard_tasklet);
	
	pm_kbd = pm_register(PM_SYS_DEV, PM_SYS_KBC, pm_kbd_request_override);

	return 0;
}
