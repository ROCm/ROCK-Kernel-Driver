#include <linux/init.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/console.h>

#include "irq_kern.h"
#include "irq_user.h"
#include "x11_kern.h"
#include "x11_user.h"

/* ---------------------------------------------------------------------------- */

static int x11_enable  = 0;
static int x11_fps     = 5;
static int x11_width;
static int x11_height;

struct x11_kerndata {
	/* common stuff */
	struct x11_window         *win;
	struct task_struct        *kthread;
	wait_queue_head_t         wq;
	int                       has_data;

	/* framebuffer driver */
	struct fb_fix_screeninfo  *fix;
        struct fb_var_screeninfo  *var;
	struct fb_info            *info;
	struct timer_list         refresh;
	int                       dirty, x1, x2, y1, y2;

	/* input drivers */
	struct input_dev          kbd;
	struct input_dev          mouse;
};

static int x11_thread(void *data)
{
	struct x11_kerndata *kd = data;
	DECLARE_WAITQUEUE(wait,current);

	add_wait_queue(&kd->wq, &wait);
	for (;;) {
		if (kthread_should_stop())
			break;
		if (kd->dirty) {
			int x1 = kd->x1;
			int x2 = kd->x2;
			int y1 = kd->y1;
			int y2 = kd->y2;
			kd->dirty = kd->x1 = kd->x2 = kd->y1 = kd->y2 = 0;
			x11_blit_fb(kd->win, x1, y1, x2, y2);
		}
		if (kd->has_data) {
			kd->has_data = 0;
			x11_has_data(kd->win,kd);
			reactivate_fd(x11_get_fd(kd->win), X11_IRQ);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	remove_wait_queue(&kd->wq, &wait);
	return 0;
}

/* ---------------------------------------------------------------------------- */
/* input driver                                                                 */

void x11_kbd_input(struct x11_kerndata *kd, int key, int down)
{
	if (key >= KEY_MAX) {
		if (down)
			printk("%s: unknown key pressed [%d]\n",
			       __FUNCTION__, key-KEY_MAX);
		return;
	}
	input_report_key(&kd->kbd,key,down);
        input_sync(&kd->kbd);
}

void x11_mouse_input(struct x11_kerndata *kd, int state, int x, int y)
{
	input_report_key(&kd->mouse, BTN_LEFT,   (state >>  8) & 1); /* Button1Mask */
	input_report_key(&kd->mouse, BTN_MIDDLE, (state >>  9) & 1); /* Button2Mask */
	input_report_key(&kd->mouse, BTN_RIGHT,  (state >> 10) & 1); /* Button3Mask */
	input_report_abs(&kd->mouse, ABS_X, x);
	input_report_abs(&kd->mouse, ABS_Y, y);
	input_sync(&kd->mouse);
}

void x11_cad(struct x11_kerndata *kd)
{
	printk("%s\n",__FUNCTION__);
}

/* ---------------------------------------------------------------------------- */
/* framebuffer driver                                                           */

static int x11_setcolreg(unsigned regno, unsigned red, unsigned green,
			 unsigned blue, unsigned transp,
			 struct fb_info *info)
{
	if (regno >= info->cmap.len)
		return 1;

	switch (info->var.bits_per_pixel) {
	case 16:
		if (info->var.red.offset == 10) {
			/* 1:5:5:5 */
			((u32*) (info->pseudo_palette))[regno] =	
				((red   & 0xf800) >>  1) |
				((green & 0xf800) >>  6) |
				((blue  & 0xf800) >> 11);
		} else {
			/* 0:5:6:5 */
			((u32*) (info->pseudo_palette))[regno] =	
				((red   & 0xf800)      ) |
				((green & 0xfc00) >>  5) |
				((blue  & 0xf800) >> 11);
		}
		break;
	case 24:
		red   >>= 8;
		green >>= 8;
		blue  >>= 8;
		((u32 *)(info->pseudo_palette))[regno] =
			(red   << info->var.red.offset)   |
			(green << info->var.green.offset) |
			(blue  << info->var.blue.offset);
		break;
	case 32:
		red   >>= 8;
		green >>= 8;
		blue  >>= 8;
		((u32 *)(info->pseudo_palette))[regno] =
			(red   << info->var.red.offset)   |
			(green << info->var.green.offset) |
			(blue  << info->var.blue.offset);
		break;
	}
	return 0;
}

static void x11_fb_timer(unsigned long data)
{
	struct x11_kerndata *kd = (struct x11_kerndata*)data;
	kd->dirty++;
	wake_up(&kd->wq);
}

static void x11_fb_refresh(struct x11_kerndata *kd,
			   int x1, int y1, int w, int h)
{
	int x2, y2;

	x2 = x1 + w;
	y2 = y1 + h;
	if (0 == kd->x2 || 0 == kd->y2) {
		kd->x1 = x1;
		kd->x2 = x2;
		kd->y1 = y1;
		kd->y2 = y2;
	}
	if (kd->x1 > x1)
		kd->x1 = x1;
	if (kd->x2 < x2)
		kd->x2 = x2;
	if (kd->y1 > y1)
		kd->y1 = y1;
	if (kd->y2 < y2)
		kd->y2 = y2;

	if (timer_pending(&kd->refresh))
		return;
	mod_timer(&kd->refresh, jiffies + HZ/x11_fps);
}

void x11_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	struct x11_kerndata *kd = p->par;

	cfb_fillrect(p, rect);
	x11_fb_refresh(kd, rect->dx, rect->dy, rect->width, rect->height);
}

void x11_imageblit(struct fb_info *p, const struct fb_image *image)
{
	struct x11_kerndata *kd = p->par;

	cfb_imageblit(p, image);
	x11_fb_refresh(kd, image->dx, image->dy, image->width, image->height);
}

void x11_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	struct x11_kerndata *kd = p->par;

	cfb_copyarea(p, area);
	x11_fb_refresh(kd, area->dx, area->dy, area->width, area->height);
}

static struct fb_ops x11_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= x11_setcolreg,
	.fb_fillrect	= x11_fillrect,
	.fb_copyarea	= x11_copyarea,
	.fb_imageblit	= x11_imageblit,
	.fb_cursor	= soft_cursor,
};

/* ---------------------------------------------------------------------------- */

static irqreturn_t x11_irq(int irq, void *data, struct pt_regs *unused)
{
	struct x11_kerndata *kd = data;

	kd->has_data++;
	wake_up(&kd->wq);
	return IRQ_HANDLED;
}

static int x11_probe(void)
{
	struct x11_kerndata *kd;
	int i;
	
	if (!x11_enable)
		return -ENODEV;

	kd = kmalloc(sizeof(*kd),GFP_KERNEL);
	if (NULL == kd)
		return -ENOMEM;
	memset(kd,0,sizeof(*kd));

	kd->win = x11_open(x11_width, x11_height);
	if (NULL == kd->win)
		goto fail_free;
	kd->fix = x11_get_fix(kd->win);
	kd->var = x11_get_var(kd->win);
	
	/* framebuffer setup */
	kd->info = framebuffer_alloc(sizeof(u32) * 256, NULL);
	kd->info->pseudo_palette = kd->info->par;
	kd->info->par = kd;
        kd->info->screen_base = x11_get_fbmem(kd->win);

	kd->info->fbops = &x11_fb_ops;
	kd->info->var = *kd->var;
	kd->info->fix = *kd->fix;
	kd->info->flags = FBINFO_FLAG_DEFAULT;

	fb_alloc_cmap(&kd->info->cmap, 256, 0);
	register_framebuffer(kd->info);
	printk(KERN_INFO "fb%d: %s frame buffer device, %dx%d, %d fps, %d:%d:%d\n",
	       kd->info->node, kd->info->fix.id,
	       kd->var->xres, kd->var->yres, x11_fps,
	       kd->var->red.length, kd->var->green.length, kd->var->blue.length);

	/* keyboard setup */
        init_input_dev(&kd->kbd);
	set_bit(EV_KEY, kd->kbd.evbit);
	for (i = 0; i < KEY_MAX; i++)
		set_bit(i, kd->kbd.keybit);
	kd->kbd.id.bustype = BUS_HOST;
	kd->kbd.name = "virtual keyboard";
	kd->kbd.phys = "x11/input0";
	input_register_device(&kd->kbd);

	/* mouse setup */
        init_input_dev(&kd->mouse);
	set_bit(EV_ABS,     kd->mouse.evbit);
	set_bit(EV_KEY,     kd->mouse.evbit);
	set_bit(BTN_LEFT,   kd->mouse.keybit);
	set_bit(BTN_MIDDLE, kd->mouse.keybit);
	set_bit(BTN_RIGHT,  kd->mouse.keybit);
	set_bit(ABS_X,      kd->mouse.absbit);
	set_bit(ABS_Y,      kd->mouse.absbit);
	kd->mouse.absmin[ABS_X] = 0;
	kd->mouse.absmax[ABS_X] = kd->var->xres;
	kd->mouse.absmin[ABS_Y] = 0;
	kd->mouse.absmax[ABS_Y] = kd->var->yres;
	kd->mouse.id.bustype = BUS_HOST;
	kd->mouse.name = "virtual mouse";
	kd->mouse.phys = "x11/input1";
	input_register_device(&kd->mouse);

	/* misc common kernel stuff */
	init_waitqueue_head(&kd->wq);
	init_timer(&kd->refresh);
	kd->refresh.function = x11_fb_timer;
	kd->refresh.data     = (unsigned long)kd;

	kd->kthread = kthread_run(x11_thread, kd, "x11 thread");
	um_request_irq(X11_IRQ, x11_get_fd(kd->win), IRQ_READ, x11_irq,
		       SA_INTERRUPT | SA_SHIRQ, "x11", kd);

	return 0;

fail_free:
	kfree(kd);
	return -ENODEV;
}

static int __init x11_init(void)
{
	return x11_probe();
}

static void __exit x11_fini(void)
{
	/* FIXME */
}

module_init(x11_init);
module_exit(x11_fini);

static int x11_setup(char *str)
{
	if (3 == sscanf(str,"%dx%d@%d",&x11_width,&x11_height,&x11_fps) ||
	    2 == sscanf(str,"%dx%d",&x11_width,&x11_height)) {
		x11_enable = 1;
#if defined(CONFIG_DUMMY_CONSOLE)
		/* this enables the virtual consoles */
		conswitchp = &dummy_con;
#endif  
		return 0;
	}
	return -1;
}
__setup("x11=", x11_setup);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
