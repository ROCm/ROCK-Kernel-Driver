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

#include "irq_kern.h"
#include "irq_user.h"
#include "x11_kern.h"
#include "x11_user.h"

#define DRIVER_NAME "uml-x11-fb"

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

	/* fb mapping */
	struct semaphore          mm_lock;
	struct vm_area_struct     *vma;
	atomic_t                  map_refs;
	int                       faults;
	int                       nr_pages;
	struct page               **pages;
	int                       *mapped;

	/* input drivers */
	struct input_dev          kbd;
	struct input_dev          mouse;
};

void x11_mmap_update(struct x11_kerndata *kd)
{
	int i, off, len;
	char *src;

	zap_page_range(kd->vma, kd->vma->vm_start,
		       kd->vma->vm_end - kd->vma->vm_start, NULL);
	kd->faults = 0;
	for (i = 0; i < kd->nr_pages; i++) {
		if (NULL == kd->pages[i])
			continue;
		if (0 == kd->mapped[i])
			continue;
		kd->mapped[i] = 0;
		off = i << PAGE_SHIFT;
		len = PAGE_SIZE;
		if (len > kd->fix->smem_len - off)
			len = kd->fix->smem_len - off;
		src = kmap(kd->pages[i]);
		memcpy(kd->info->screen_base + off, src, len);
		kunmap(kd->pages[i]);
	}
}

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
			down(&kd->mm_lock);
			if (kd->faults > 0)
				x11_mmap_update(kd);
			up(&kd->mm_lock);
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

void x11_mouse_input(struct x11_kerndata *kd, int key, int down,
		     int x, int y)
{
	if (key != KEY_RESERVED)
		input_report_key(&kd->mouse, key, down);
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

/* ---------------------------------------------------------------------------- */

static void
x11_fb_vm_open(struct vm_area_struct *vma)
{
	struct x11_kerndata *kd = vma->vm_private_data;

	atomic_inc(&kd->map_refs);
}

static void
x11_fb_vm_close(struct vm_area_struct *vma)
{
	struct x11_kerndata *kd = vma->vm_private_data;
	int i;

	if (!atomic_dec_and_test(&kd->map_refs))
		return;
	down(&kd->mm_lock);
	for (i = 0; i < kd->nr_pages; i++) {
		if (NULL == kd->pages[i])
			continue;
		put_page(kd->pages[i]);
	}
	kfree(kd->pages);
	kfree(kd->mapped);
	kd->pages    = NULL;
	kd->mapped   = NULL;
	kd->vma      = NULL;
	kd->nr_pages = 0;
	kd->faults   = 0;
	up(&kd->mm_lock);
}

static struct page*
x11_fb_vm_nopage(struct vm_area_struct *vma, unsigned long vaddr,
		 int *type)
{
	struct x11_kerndata *kd = vma->vm_private_data;
	int pgnr = (vaddr - vma->vm_start) >> PAGE_SHIFT;
	int y1,y2;

        if (pgnr >= kd->nr_pages)
		return NOPAGE_SIGBUS;

	down(&kd->mm_lock);
	if (NULL == kd->pages[pgnr]) {
		struct page *page;
		page = alloc_page_vma(GFP_HIGHUSER, vma, vaddr);
		if (!page)
			return NOPAGE_OOM;
		clear_user_highpage(page, vaddr);
		kd->pages[pgnr] = page;
	}
	get_page(kd->pages[pgnr]);
	kd->mapped[pgnr] = 1;
	kd->faults++;
	up(&kd->mm_lock);

	y1 = pgnr * PAGE_SIZE / kd->fix->line_length;
	y2 = (pgnr * PAGE_SIZE + PAGE_SIZE-1) / kd->fix->line_length;
	if (y2 > kd->var->yres)
		y2 = kd->var->yres;
	x11_fb_refresh(kd, 0, y1, kd->var->xres, y2 - y1);
	
	if (type)
		*type = VM_FAULT_MINOR;
	return kd->pages[pgnr];
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
	int retval;
	int fb_pages;
	int map_pages;

	down(&kd->mm_lock);

	retval = -EBUSY;
	if (kd->vma) {
		printk("%s: busy, mapping exists\n",__FUNCTION__);
		goto out;
	}

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

	fb_pages  = (p->fix.smem_len             + PAGE_SIZE-1) >> PAGE_SHIFT;
	map_pages = (vma->vm_end - vma->vm_start + PAGE_SIZE-1) >> PAGE_SHIFT;
	if (map_pages > fb_pages) {
		printk("%s: mapping to big (%ld > %d)\n",__FUNCTION__,
		       vma->vm_end - vma->vm_start, p->fix.smem_len);
		goto out;
	}

	retval = -ENOMEM;
	kd->pages = kmalloc(sizeof(struct page*)*map_pages, GFP_KERNEL);
	if (NULL == kd->pages)
		goto out;
	kd->mapped = kmalloc(sizeof(int)*map_pages, GFP_KERNEL);
	if (NULL == kd->mapped) {
		kfree(kd->pages);
		goto out;
	}
	memset(kd->pages,  0, sizeof(struct page*) * map_pages);
	memset(kd->mapped, 0, sizeof(int)          * map_pages);
	kd->vma = vma;
	kd->nr_pages = map_pages;
	atomic_set(&kd->map_refs,1);
	kd->faults = 0;

	vma->vm_ops   = &x11_fb_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_flags &= ~VM_IO; /* using shared anonymous pages */
	vma->vm_private_data = kd;
	retval = 0;

out:
	up(&kd->mm_lock);
	return retval;	
}

/* ---------------------------------------------------------------------------- */

static struct fb_ops x11_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= x11_setcolreg,
	.fb_fillrect	= x11_fillrect,
	.fb_copyarea	= x11_copyarea,
	.fb_imageblit	= x11_imageblit,
	.fb_cursor	= soft_cursor,
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
	
	kd = kmalloc(sizeof(*kd),GFP_KERNEL);
	if (NULL == kd)
		return -ENOMEM;
	memset(kd,0,sizeof(*kd));

	kd->win = x11_open(x11_width, x11_height);
	if (NULL == kd->win) {
		printk(DRIVER_NAME ": can't open X11 window\n");
		goto fail_free;
	}
	kd->fix = x11_get_fix(kd->win);
	kd->var = x11_get_var(kd->win);
	
	/* framebuffer setup */
	kd->info = framebuffer_alloc(sizeof(u32) * 256, device);
	kd->info->pseudo_palette = kd->info->par;
	kd->info->par = kd;
        kd->info->screen_base = x11_get_fbmem(kd->win);

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
        init_input_dev(&kd->kbd);
	set_bit(EV_KEY, kd->kbd.evbit);
	for (i = 0; i < KEY_MAX; i++)
		set_bit(i, kd->kbd.keybit);
	kd->kbd.id.bustype = BUS_HOST;
	kd->kbd.name = DRIVER_NAME " virtual keyboard";
	kd->kbd.phys = DRIVER_NAME "/input0";
	kd->kbd.dev  = device;
	input_register_device(&kd->kbd);

	/* mouse setup */
        init_input_dev(&kd->mouse);
	set_bit(EV_ABS,     kd->mouse.evbit);
	set_bit(EV_KEY,     kd->mouse.evbit);
	set_bit(BTN_TOUCH,  kd->mouse.keybit);
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
	kd->mouse.name = DRIVER_NAME " virtual mouse";
	kd->mouse.phys = DRIVER_NAME "/input1";
	kd->mouse.dev  = device;
	input_register_device(&kd->mouse);

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

fail_free:
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
