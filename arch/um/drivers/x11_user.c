#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <linux/fb.h>
#include <linux/input.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>

#include "x11_kern.h"
#include "x11_user.h"

/* --------------------------------------------------------------------------- */

struct x11_window {
	/* misc x11 stuff */
	Display                   *dpy;
	Window                    root, win;
	GC                        gc;
	XVisualInfo               vi;
	Atom                      delete_window;

	/* framebuffer -- x11 */
	XImage                    *ximage;
	unsigned char             *xidata;
	XShmSegmentInfo           shminfo;
	
	/* framebuffer -- kernel */
	struct fb_fix_screeninfo  fix;
        struct fb_var_screeninfo  var;
};

/* --------------------------------------------------------------------------- */

/*
 * map X11 keycodes to linux keycodes
 *
 * WARNING: X11 keycodes are not portable, this likely breaks as soon
 * as one uses a X-Server not running on a linux machine as display.
 *
 * Using portable keysyms instead creates some strange and hard to
 * handle keymapping effects through.  That happens because both host
 * and uml machine are mapping keys then ...
 */
static int x11_keymap[] = {
	[   9 ] = KEY_ESC,
	[  10 ] = KEY_1,
	[  11 ] = KEY_2,
	[  12 ] = KEY_3,
	[  13 ] = KEY_4,
	[  14 ] = KEY_5,
	[  15 ] = KEY_6,
	[  16 ] = KEY_7,
	[  17 ] = KEY_8,
	[  18 ] = KEY_9,
	[  19 ] = KEY_0,
	[  20 ] = KEY_MINUS,
	[  21 ] = KEY_EQUAL,
	[  22 ] = KEY_BACKSPACE,

	[  23 ] = KEY_TAB,
	[  24 ] = KEY_Q,
	[  25 ] = KEY_W,
	[  26 ] = KEY_E,
	[  27 ] = KEY_R,
	[  28 ] = KEY_T,
	[  29 ] = KEY_Y,
	[  30 ] = KEY_U,
	[  31 ] = KEY_I,
	[  32 ] = KEY_O,
	[  33 ] = KEY_P,
	[  34 ] = KEY_LEFTBRACE,
	[  35 ] = KEY_RIGHTBRACE,
	[  36 ] = KEY_ENTER,

	[  37 ] = KEY_LEFTCTRL,
	[  38 ] = KEY_A,
	[  39 ] = KEY_S,
	[  40 ] = KEY_D,
	[  41 ] = KEY_F,
	[  42 ] = KEY_G,
	[  43 ] = KEY_H,
	[  44 ] = KEY_J,
	[  45 ] = KEY_K,
	[  46 ] = KEY_L,
	[  47 ] = KEY_SEMICOLON,
	[  48 ] = KEY_APOSTROPHE,
	[  49 ] = KEY_GRAVE,

	[  50 ] = KEY_LEFTSHIFT,
	[  51 ] = KEY_BACKSLASH,
	[  52 ] = KEY_Z,
	[  53 ] = KEY_X,
	[  54 ] = KEY_C,
	[  55 ] = KEY_V,
	[  56 ] = KEY_B,
	[  57 ] = KEY_N,
	[  58 ] = KEY_M,
	[  59 ] = KEY_COMMA,
	[  60 ] = KEY_DOT,
	[  61 ] = KEY_SLASH,
	[  62 ] = KEY_RIGHTSHIFT,

	[  63 ] = KEY_KPASTERISK,
	[  64 ] = KEY_LEFTALT,
	[  65 ] = KEY_SPACE,
	[  66 ] = KEY_CAPSLOCK,

	[  67 ] = KEY_F1,
	[  68 ] = KEY_F2,
	[  69 ] = KEY_F3,
	[  70 ] = KEY_F4,
	[  71 ] = KEY_F5,
	[  72 ] = KEY_F6,
	[  73 ] = KEY_F7,
	[  74 ] = KEY_F8,
	[  75 ] = KEY_F9,
	[  76 ] = KEY_F10,
	[  77 ] = KEY_NUMLOCK,
	[  78 ] = KEY_SCROLLLOCK,

	[  79 ] = KEY_KP7,
	[  80 ] = KEY_KP8,
	[  81 ] = KEY_KP9,
	[  82 ] = KEY_KPMINUS,
	[  83 ] = KEY_KP4,
	[  84 ] = KEY_KP5,
	[  85 ] = KEY_KP6,
	[  86 ] = KEY_KPPLUS,
	[  87 ] = KEY_KP1,
	[  88 ] = KEY_KP2,
	[  89 ] = KEY_KP3,
	[  90 ] = KEY_KP0,
	[  91 ] = KEY_KPDOT,

	// [  92 ] = KEY_Print,
	// [  93 ] = KEY_Mode_switch,
	// [  94 ] = KEY_less,

	[  95 ] = KEY_F11,
	[  96 ] = KEY_F12,
	[  97 ] = KEY_HOME,
	[  98 ] = KEY_UP,
	[  99 ] = KEY_PAGEUP,
	[ 100 ] = KEY_LEFT,
	[ 102 ] = KEY_RIGHT,
	[ 103 ] = KEY_END,
	[ 104 ] = KEY_DOWN,
	[ 105 ] = KEY_PAGEDOWN,
	[ 106 ] = KEY_INSERT,
	[ 107 ] = KEY_DELETE,

	// [ 108 ] = KEY_KP_Enter,
	[ 109 ] = KEY_RIGHTCTRL,
	// [ 110 ] = KEY_Pause,
	// [ 111 ] = KEY_Print,
	// [ 112 ] = KEY_KP_Divide,
	[ 113 ] = KEY_RIGHTALT,
	// [ 114 ] = KEY_Pause,
	// [ 115 ] = KEY_Super_L,
	// [ 116 ] = KEY_Super_R,
	[ 117 ] = KEY_MENU,
	// [ 124 ] = KEY_ISO_Level3_Shift,
	// [ 126 ] = KEY_KP_Equal,
};

static void x11_kbd(struct x11_window *win, struct x11_kerndata *kd, XEvent *e)
{
	int key = KEY_RESERVED;
	
	if (e->xkey.keycode < sizeof(x11_keymap)/sizeof(x11_keymap[0]))
		key = x11_keymap[e->xkey.keycode];
	if (KEY_RESERVED != key) {
		x11_kbd_input(kd, key, e->type == KeyPress);
	} else {
		x11_kbd_input(kd, KEY_MAX + e->xkey.keycode, e->type == KeyPress);
	}
}

/* --------------------------------------------------------------------------- */

static int mitshm_err;

static int
catch_no_mitshm(Display * dpy, XErrorEvent * event)
{
	mitshm_err++;
	return 0;
}

static void init_color(int32_t mask, struct fb_bitfield *bf)
{
    int i;

    memset(bf, 0, sizeof(*bf));
    for (i = 0; i < 32; i++) {
	    if (mask & ((int32_t)1 << i))
		    bf->length++;
	    else if (!bf->length)
		    bf->offset++;
    }
}
    
struct x11_window *x11_open(int width, int height)
{
	char *title = "user mode linux framebuffer";
	struct x11_window *win;
	XSizeHints hints;
	XTextProperty prop;
	XVisualInfo *info, template;
	void *old_handler;
	int n,bytes_pp;
	
	win = malloc(sizeof(*win));
	if (NULL == win)
		goto fail;
	
	win->dpy = XOpenDisplay(NULL);
	if (NULL == win->dpy)
		goto fail_free;

	/* get visual info */
	template.screen = XDefaultScreen(win->dpy);
	template.depth  = DefaultDepth(win->dpy, DefaultScreen(win->dpy));
	info = XGetVisualInfo(win->dpy, VisualScreenMask | VisualDepthMask,
			      &template, &n);
	if (0 == n)
		goto fail_free;
	win->vi = info[0];
	bytes_pp = (win->vi.depth+7)/8;
	XFree(info);
	if (win->vi.class != TrueColor && win->vi.class != DirectColor)
		goto fail_free;

	/* create pixmap */
	mitshm_err  = 0;
	old_handler = XSetErrorHandler(catch_no_mitshm);
	win->ximage = XShmCreateImage(win->dpy,win->vi.visual,win->vi.depth,
				      ZPixmap, NULL, &win->shminfo,
				      width, height);
	if (NULL == win->ximage)
		goto shm_error;
	win->shminfo.shmid = shmget(IPC_PRIVATE,
				    win->ximage->bytes_per_line * win->ximage->height,
				    IPC_CREAT | 0777);
	if (-1 == win->shminfo.shmid)
		goto shm_error;
	win->shminfo.shmaddr = (char *) shmat(win->shminfo.shmid, 0, 0);
	if ((void *)-1 == win->shminfo.shmaddr)
		goto shm_error;

	win->ximage->data = win->shminfo.shmaddr;
	win->shminfo.readOnly = False;
        XShmAttach(win->dpy, &win->shminfo);
	XSync(win->dpy, False);
	if (mitshm_err)
		goto shm_error;
	shmctl(win->shminfo.shmid, IPC_RMID, 0);
	XSetErrorHandler(old_handler);
	goto have_ximage;

shm_error:
	/* can't use shared memory -- cleanup and try without */
	if (win->ximage) {
		XDestroyImage(win->ximage);
		win->ximage = NULL;
	}
	if ((void *)-1 != win->shminfo.shmaddr  &&  NULL != win->shminfo.shmaddr)
		shmdt(win->shminfo.shmaddr);
	XSetErrorHandler(old_handler);

	memset(&win->shminfo,0,sizeof(win->shminfo));
	if (NULL == (win->xidata = malloc(width * height * bytes_pp)))
		goto fail_free;

	win->ximage = XCreateImage(win->dpy, win->vi.visual, win->vi.depth,
				   ZPixmap, 0,  win->xidata,
				   width, height, 8, 0);

have_ximage:
	/* fill structs */
	win->var.xres           = width;
	win->var.xres_virtual   = width;
	win->var.yres           = height;
	win->var.yres_virtual   = height;
	win->var.bits_per_pixel = bytes_pp * 8;
	win->var.pixclock       = 10000000 / win->var.xres * 1000 / win->var.yres;
	win->var.left_margin    = (win->var.xres / 8) & 0xf8;
	win->var.hsync_len      = (win->var.xres / 8) & 0xf8;

	init_color(win->vi.red_mask,   &win->var.red);
	init_color(win->vi.green_mask, &win->var.green);
	init_color(win->vi.blue_mask,  &win->var.blue);
	
	win->var.activate       = FB_ACTIVATE_NOW;
	win->var.height		= -1;
	win->var.width		= -1;
	win->var.right_margin	= 32;
	win->var.upper_margin	= 16;
	win->var.lower_margin	= 4;
	win->var.vsync_len	= 4;
	win->var.vmode		= FB_VMODE_NONINTERLACED;

#if 0
	win->fix.smem_start     = ;
	win->fix.smem_len       = ;
#endif
	win->fix.line_length    = win->ximage->bytes_per_line;
	win->fix.visual         = FB_VISUAL_TRUECOLOR;
	
	strcpy(win->fix.id,"x11");
	win->fix.type		= FB_TYPE_PACKED_PIXELS;
	win->fix.accel		= FB_ACCEL_NONE;
	
	/* create + init window */
	hints.flags      = PMinSize | PMaxSize;
	hints.min_width  = width;
	hints.min_height = height;
	hints.max_width  = width;
	hints.max_height = height;
	XStringListToTextProperty(&title,1,&prop);
	
	win->root  = RootWindow(win->dpy, DefaultScreen(win->dpy));
	win->win = XCreateSimpleWindow(win->dpy, win->root,
				       0, 0, width, height,
				       CopyFromParent, CopyFromParent,
				       BlackPixel(win->dpy, DefaultScreen(win->dpy)));
	win->gc = XCreateGC(win->dpy, win->win, 0, NULL);
	XSelectInput(win->dpy, win->win,
		     KeyPressMask    | KeyReleaseMask    | /* virtual keyboard */
		     ButtonPressMask | ButtonReleaseMask | /* mouse (touchscreen?) */
		     PointerMotionMask | ExposureMask | StructureNotifyMask |
		     PropertyChangeMask);
	XMapWindow(win->dpy,win->win);
	XSetWMNormalHints(win->dpy,win->win,&hints);
	XSetWMName(win->dpy,win->win,&prop);
	win->delete_window = XInternAtom(win->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(win->dpy, win->win, &win->delete_window, 1);

	XFlush(win->dpy);
	return win;
	
fail_free:
	free(win);
fail:
	return NULL;
}

int x11_get_fd(struct x11_window *win)
{
	return ConnectionNumber(win->dpy);
}

struct fb_fix_screeninfo* x11_get_fix(struct x11_window *win)
{
	return &win->fix;
}

struct fb_var_screeninfo* x11_get_var(struct x11_window *win)
{
	return &win->var;
}

void* x11_get_fbmem(struct x11_window *win)
{
	return win->ximage->data;
}

int x11_blit_fb(struct x11_window *win, int x1, int y1, int x2, int y2)
{
	if (win->shminfo.shmid)
		XShmPutImage(win->dpy, win->win, win->gc, win->ximage,
			     x1,y1,x1,y1, x2-x1,y2-y1, True);
	else
		XPutImage(win->dpy, win->win, win->gc, win->ximage,
			  x1,y1,x1,y1, x2-x1,y2-y1);
	XFlush(win->dpy);
	return 0;
}

int x11_has_data(struct x11_window *win, struct x11_kerndata *kd)
{
	XEvent e;
	int count = 0;
	
	while (True == XCheckMaskEvent(win->dpy, ~0, &e)) {
		count++;
		switch (e.type) {
		case KeyPress:
		case KeyRelease:
			x11_kbd(win, kd, &e);
			break;
		case ButtonPress:
		case ButtonRelease:
			x11_mouse_input(kd, e.xbutton.state, e.xbutton.x, e.xbutton.y);
			break;
		case MotionNotify:
			x11_mouse_input(kd, e.xmotion.state, e.xmotion.x, e.xmotion.y);
			break;
		case Expose:
			if (0 == e.xexpose.count)
				x11_blit_fb(win, 0,0, win->var.xres, win->var.yres);
			break;
		case ClientMessage:
//			if (e.xclient.data.l[0] == win->delete_window)
				x11_cad(kd);
			break;
		}
	}
	return count;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
