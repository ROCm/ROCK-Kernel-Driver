#include <linux/init.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>

#include "irq_kern.h"
#include "irq_user.h"
#include "x11_kern.h"
#include "x11_user.h"

#define DRIVER_NAME "uml-x11-fb"

/* ------------------------------------------------------------------ */

static int x11_enable  = 0;
static int x11_fps     = 5;
static int x11_width;
static int x11_height;

struct x11_mapping {
	struct list_head	  next;
	struct vm_area_struct     *vma;
	atomic_t                  map_refs;
	int                       faults;
	struct x11_kerndata       *kd;
};

struct x11_kerndata {
	/* common stuff */
	struct x11_window         *win;
	struct task_struct        *kthread;
	wait_queue_head_t         wq;
	int                       has_data;

	/* framebuffer driver */
	unsigned char             *fb;
	struct fb_fix_screeninfo  *fix;
        struct fb_var_screeninfo  *var;
	struct fb_info            *info;
	struct timer_list         refresh;
	int                       dirty, y1, y2;

	/* fb mapping */
	struct semaphore          mm_lock;
	int                       nr_pages;
	struct page               **pages;
	struct list_head	  mappings;

	/* input drivers */
	struct input_dev          *kbd;
	struct input_dev          *mouse;
};

void x11_update_screen(struct x11_kerndata *kd)
{
	int y1,y2,offset,length;
	unsigned char *src, *dst;
	struct list_head *item;
	struct x11_mapping *map;

	y1 = kd->y1;
	y2 = kd->y2;
	kd->dirty = kd->y1 = kd->y2 = 0;
	down(&kd->mm_lock);
	list_for_each(item, &kd->mappings) {
		map = list_entry(item, struct x11_mapping, next);
		if (!map->faults)
			continue;
		zap_page_range(map->vma, map->vma->vm_start,
			       map->vma->vm_end - map->vma->vm_start, NULL);
		map->faults = 0;
	}
	up(&kd->mm_lock);

	offset = y1 * kd->fix->line_length;
	length = (y2 - y1) * kd->fix->line_length;
	src = kd->fb + offset;
	dst = x11_get_fbmem(kd->win) + offset;
	memcpy(dst,src,length);
	x11_blit_fb(kd->win, y1, y2);
}

static int x11_thread(void *data)
{
	struct x11_kerndata *kd = data;
	DECLARE_WAITQUEUE(wait,current);

	add_wait_queue(&kd->wq, &wait);
	for (;;) {
		if (kthread_should_stop())
			break;
		if (kd->dirty)
			x11_update_screen(kd);
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

/* ------------------------------------------------------------------ */
/* input driver                                                       */

void x11_kbd_input(struct x11_kerndata *kd, int key, int down)
{
	if (key >= KEY_MAX) {
		if (down)
			printk("%s: unknown key pressed [%d]\n",
			       __FUNCTION__, key-KEY_MAX);
		return;
	}
	input_report_key(kd->kbd,key,down);
	input_sync(kd->kbd);
}

void x11_mouse_input(struct x11_kerndata *kd, int key, int down,
		     int x, int y)
{
	if (key != KEY_RESERVED)
		input_report_key(kd->mouse, key, down);
	input_report_abs(kd->mouse, ABS_X, x);
	input_report_abs(kd->mouse, ABS_Y, y);
	input_sync(kd->mouse);
}

void x11_cad(struct x11_kerndata *kd)
{
	printk("%s\n",__FUNCTION__);
}

/* ------------------------------------------------------------------ */
/* framebuffer driver                                                 */

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

static void x11_fb_refresh(struct x11_kerndata *kd, int y1, int h)
{
	int y2;

	y2 = y1 + h;
	if (0 == kd->y2) {
		kd->y1 = y1;
		kd->y2 = y2;
	}
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
	x11_fb_refresh(kd, rect->dy, rect->height);
}

void x11_imageblit(struct fb_info *p, const struct fb_image *image)
{
	struct x11_kerndata *kd = p->par;

	cfb_imageblit(p, image);
	x11_fb_refresh(kd, image->dy, image->height);
}

void x11_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	struct x11_kerndata *kd = p->par;

	cfb_copyarea(p, area);
	x11_fb_refresh(kd, area->dy, area->height);
}

/* ------------------------------------------------------------------ */

static void
x11_fb_vm_open(struct vm_area_struct *vma)
{
	struct x11_mapping *map = vma->vm_private_data;

	atomic_inc(&map->map_refs);
}

static void
x11_fb_vm_close(struct vm_area_struct *vma)
{
	struct x11_mapping *map = vma->vm_private_data;
	struct x11_kerndata *kd = map->kd;

	down(&kd->mm_lock);
	if (atomic_dec_and_test(&map->map_refs)) {
		list_del(&map->next);
		kfree(map);
	}
	up(&kd->mm_lock);
}

static struct page*
x11_fb_vm_nopage(struct vm_area_struct *vma, unsigned long vaddr,
		 int *type)
{
	struct x11_mapping *map = vma->vm_private_data;
	struct x11_kerndata *kd = map->kd;
	int pgnr = (vaddr - vma->vm_start) >> PAGE_SHIFT;
	struct page *page;
	int y1,y2;

        if (pgnr >= kd->nr_pages)
		return NOPAGE_SIGBUS;

	down(&kd->mm_lock);
	page = kd->pages[pgnr];
	get_page(page);
	map->faults++;

	y1 = pgnr * PAGE_SIZE / kd->fix->line_length;
	y2 = (pgnr * PAGE_SIZE + PAGE_SIZE-1) / kd->fix->line_length;
	if (y2 > kd->var->yres)
		y2 = kd->var->yres;
	x11_fb_refresh(kd, y1, y2 - y1);
	up(&kd->mm_lock);

	if (type)
		*type = VM_FAULT_MINOR;
	return page;
}

static struct vm_operations_struct x11_fb_vm_ops =
{
	.open     = x11_fb_vm_open,
	.close    = x11_fb_vm_close,
	.nopage   = x11_fb_vm_nopage,
};

int x11_mmap(struct fb_info *p, struct file *file,
	     struct vm_area_struct * vma)
{
	struct x11_kerndata *kd = p->par;
	struct x11_mapping *map;
	int retval;
	int map_pages;

	down(&kd->mm_lock);

	retval = -ENOMEM;
	if (NULL == (map = kmalloc(sizeof(*map), GFP_KERNEL))) {
		printk("%s: oops, out of memory\n",__FUNCTION__);
		goto out;
	}
	memset(map,0,sizeof(*map));

	retval = -EINVAL;
	if (!(vma->vm_flags & VM_WRITE)) {
		printk("%s: need writable mapping\n",__FUNCTION__);
		goto out;
	}
	if (!(vma->vm_flags & VM_SHARED)) {
		printk("%s: need shared mapping\n",__FUNCTION__);
		goto out;
	}
	if (vma->vm_pgoff != 0) {
		printk("%s: need offset 0 (vm_pgoff=%ld)\n",__FUNCTION__,
		       vma->vm_pgoff);
		goto out;
	}

	map_pages = (vma->vm_end - vma->vm_start + PAGE_SIZE-1) >> PAGE_SHIFT;
	if (map_pages > kd->nr_pages) {
		printk("%s: mapping to big (%ld > %d)\n",__FUNCTION__,
		       vma->vm_end - vma->vm_start, p->fix.smem_len);
		goto out;
	}

	map->vma = vma;
	map->faults = 0;
	map->kd = kd;
	atomic_set(&map->map_refs,1);
	list_add(&map->next,&kd->mappings);
	vma->vm_ops   = &x11_fb_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_private_data = map;
	retval = 0;

out:
	up(&kd->mm_lock);
	return retval;
}

/* ------------------------------------------------------------------ */

static struct fb_ops x11_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= x11_setcolreg,
	.fb_fillrect	= x11_fillrect,
	.fb_copyarea	= x11_copyarea,
	.fb_imageblit	= x11_imageblit,
	.fb_mmap        = x11_mmap,
};

/* ---------------------------------------------------------------------------- */

static irqreturn_t x11_irq(int irq, void *data, struct pt_regs *unused)
{
	struct x11_kerndata *kd = data;

	kd->has_data++;
	wake_up(&kd->wq);
	return IRQ_HANDLED;
}

static int __init x11_probe(struct device *device)
{
	struct x11_kerndata *kd;
	int i;

	kd = kzalloc(sizeof(*kd),GFP_KERNEL);
	if (NULL == kd)
		return -ENOMEM;

	kd->kbd   = input_allocate_device();
	kd->mouse = input_allocate_device();
	if (NULL == kd->kbd || NULL == kd->mouse)
		goto fail_free;

	kd->win = x11_open(x11_width, x11_height);
	if (NULL == kd->win) {
		printk(DRIVER_NAME ": can't open X11 window\n");
		goto fail_free;
	}
	kd->fix = x11_get_fix(kd->win);
	kd->var = x11_get_var(kd->win);
	INIT_LIST_HEAD(&kd->mappings);

	/* alloc memory */
	kd->fb  = vmalloc(kd->fix->smem_len);
	if (NULL == kd->fb) {
		printk("%s: vmalloc(%d) failed\n",
		       __FUNCTION__,kd->fix->smem_len);
		goto fail_close;
	}
	memset(kd->fb,0,kd->fix->smem_len);
	kd->nr_pages  = (kd->fix->smem_len + PAGE_SIZE-1) >> PAGE_SHIFT;
	kd->pages = kmalloc(sizeof(struct page*)*kd->nr_pages, GFP_KERNEL);
	if (NULL == kd->pages)
		goto fail_vfree;
	for (i = 0; i < kd->nr_pages; i++)
		kd->pages[i] = vmalloc_to_page(kd->fb + i*PAGE_SIZE);

	/* framebuffer setup */
	kd->info = framebuffer_alloc(sizeof(u32) * 256, device);
	kd->info->pseudo_palette = kd->info->par;
	kd->info->par = kd;
        kd->info->screen_base = kd->fb;

	kd->info->fbops = &x11_fb_ops;
	kd->info->var = *kd->var;
	kd->info->fix = *kd->fix;
	kd->info->flags = FBINFO_FLAG_DEFAULT;

	fb_alloc_cmap(&kd->info->cmap, 256, 0);
	register_framebuffer(kd->info);
	printk(KERN_INFO "fb%d: %s frame buffer device, %dx%d, %d fps, %d bpp (%d:%d:%d)\n",
	       kd->info->node, kd->info->fix.id,
	       kd->var->xres, kd->var->yres, x11_fps, kd->var->bits_per_pixel,
	       kd->var->red.length, kd->var->green.length, kd->var->blue.length);

	/* keyboard setup */
	set_bit(EV_KEY, kd->kbd->evbit);
	for (i = 0; i < KEY_MAX; i++)
		set_bit(i, kd->kbd->keybit);
	kd->kbd->id.bustype = BUS_HOST;
	kd->kbd->name = DRIVER_NAME " virtual keyboard";
	kd->kbd->phys = DRIVER_NAME "/input0";
	kd->kbd->cdev.dev = device;
	input_register_device(kd->kbd);

	/* mouse setup */
        init_input_dev(kd->mouse);
	set_bit(EV_ABS,     kd->mouse->evbit);
	set_bit(EV_KEY,     kd->mouse->evbit);
	set_bit(BTN_TOUCH,  kd->mouse->keybit);
	set_bit(BTN_LEFT,   kd->mouse->keybit);
	set_bit(BTN_MIDDLE, kd->mouse->keybit);
	set_bit(BTN_RIGHT,  kd->mouse->keybit);
	set_bit(ABS_X,      kd->mouse->absbit);
	set_bit(ABS_Y,      kd->mouse->absbit);
	kd->mouse->absmin[ABS_X] = 0;
	kd->mouse->absmax[ABS_X] = kd->var->xres;
	kd->mouse->absmin[ABS_Y] = 0;
	kd->mouse->absmax[ABS_Y] = kd->var->yres;
	kd->mouse->id.bustype = BUS_HOST;
	kd->mouse->name = DRIVER_NAME " virtual mouse";
	kd->mouse->phys = DRIVER_NAME "/input1";
	kd->mouse->cdev.dev = device;
	input_register_device(kd->mouse);

	/* misc common kernel stuff */
	init_MUTEX(&kd->mm_lock);
	init_waitqueue_head(&kd->wq);
	init_timer(&kd->refresh);
	kd->refresh.function = x11_fb_timer;
	kd->refresh.data     = (unsigned long)kd;

	kd->kthread = kthread_run(x11_thread, kd,
				  DRIVER_NAME " thread");
	um_request_irq(X11_IRQ, x11_get_fd(kd->win), IRQ_READ, x11_irq,
		       SA_INTERRUPT | SA_SHIRQ, DRIVER_NAME, kd);

	return 0;

fail_vfree:
	vfree(kd->fb);
fail_close:
	x11_close(kd->win);
fail_free:
	if (kd->kbd)
		input_free_device(kd->kbd);
	if (kd->mouse)
		input_free_device(kd->mouse);
	kfree(kd);
	return -ENODEV;
}

static struct device_driver x11_driver = {
	.name  = DRIVER_NAME,
	.bus   = &platform_bus_type,
	.probe = x11_probe,
};
static struct platform_device x11_device = {
	.name  = DRIVER_NAME,
};

static int __init x11_init(void)
{
	int ret;

	ret = driver_register(&x11_driver);
	if (ret)
		return ret;
	if (!x11_enable)
		return 0;
	ret = platform_device_register(&x11_device);
	if (ret)
		driver_unregister(&x11_driver);
	return ret;
}

static void __exit x11_fini(void)
{
	if (x11_enable)
		platform_device_unregister(&x11_device);
	driver_unregister(&x11_driver);
}

module_init(x11_init);
module_exit(x11_fini);

extern int console_use_vt; /* FIXME */

static int x11_setup(char *str)
{
	if (3 == sscanf(str,"%dx%d@%d",&x11_width,&x11_height,&x11_fps) ||
	    2 == sscanf(str,"%dx%d",&x11_width,&x11_height)) {
#if defined(CONFIG_DUMMY_CONSOLE)
		printk("%s: enable linux vt subsystem\n",__FUNCTION__);
		x11_enable = 1;
		console_use_vt = 1;
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
