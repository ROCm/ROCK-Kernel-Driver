#include <linux/smp.h>
#include <linux/time.h>
#include <linux/errno.h>

/* IBM Summit (EXA) Cyclone counter code*/
#define CYCLONE_CBAR_ADDR 0xFEB00CD0
#define CYCLONE_PMCC_OFFSET 0x51A0
#define CYCLONE_MPMC_OFFSET 0x51D0
#define CYCLONE_MPCS_OFFSET 0x51A8
#define CYCLONE_TIMER_FREQ 100000000

int use_cyclone;
void __init cyclone_setup(void)
{
	use_cyclone = 1;
}

static u32* volatile cyclone_timer;	/* Cyclone MPMC0 register */
static u32 last_update_cyclone;

static unsigned long offset_base;

static unsigned long get_offset_cyclone(void)
{
	u32 now;
	unsigned long offset;

	/* Read the cyclone timer */
	now = readl(cyclone_timer);
	/* .. relative to previous update*/
	offset = now - last_update_cyclone;

	/* convert cyclone ticks to nanoseconds */
	offset = (offset*NSEC_PER_SEC)/CYCLONE_TIMER_FREQ;

	/* our adjusted time in nanoseconds */
	return offset_base + offset;
}

static void update_cyclone(long delta_nsec)
{
	u32 now;
	unsigned long offset;

	/* Read the cyclone timer */
	now = readl(cyclone_timer);
	/* .. relative to previous update*/
	offset = now - last_update_cyclone;

	/* convert cyclone ticks to nanoseconds */
	offset = (offset*NSEC_PER_SEC)/CYCLONE_TIMER_FREQ;

	offset += offset_base;

	/* Be careful about signed/unsigned comparisons here: */
	if (delta_nsec < 0 || (unsigned long) delta_nsec < offset)
		offset_base = offset - delta_nsec;
	else
		offset_base = 0;

	last_update_cyclone = now;
}

static void reset_cyclone(void)
{
	offset_base = 0;
	last_update_cyclone = readl(cyclone_timer);
}

struct time_interpolator cyclone_interpolator = {
	.get_offset =	get_offset_cyclone,
	.update =	update_cyclone,
	.reset =	reset_cyclone,
	.frequency =	CYCLONE_TIMER_FREQ,
	.drift =	-100,
};

int __init init_cyclone_clock(void)
{
	u64* reg;
	u64 base;	/* saved cyclone base address */
	u64 offset;	/* offset from pageaddr to cyclone_timer register */
	int i;

	if (!use_cyclone)
		return -ENODEV;

	printk(KERN_INFO "Summit chipset: Starting Cyclone Counter.\n");

	/* find base address */
	offset = (CYCLONE_CBAR_ADDR);
	reg = (u64*)ioremap_nocache(offset, sizeof(u64));
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	base = readq(reg);
	if(!base){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR value.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	iounmap(reg);

	/* setup PMCC */
	offset = (base + CYCLONE_PMCC_OFFSET);
	reg = (u64*)ioremap_nocache(offset, sizeof(u64));
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid PMCC register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	writel(0x00000001,reg);
	iounmap(reg);

	/* setup MPCS */
	offset = (base + CYCLONE_MPCS_OFFSET);
	reg = (u64*)ioremap_nocache(offset, sizeof(u64));
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid MPCS register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	writel(0x00000001,reg);
	iounmap(reg);

	/* map in cyclone_timer */
	offset = (base + CYCLONE_MPMC_OFFSET);
	cyclone_timer = (u32*)ioremap_nocache(offset, sizeof(u32));
	if(!cyclone_timer){
		printk(KERN_ERR "Summit chipset: Could not find valid MPMC register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}

	/*quick test to make sure its ticking*/
	for(i=0; i<3; i++){
		u32 old = readl(cyclone_timer);
		int stall = 100;
		while(stall--) barrier();
		if(readl(cyclone_timer) == old){
			printk(KERN_ERR "Summit chipset: Counter not counting! DISABLED\n");
			iounmap(cyclone_timer);
			cyclone_timer = 0;
			use_cyclone = 0;
			return -ENODEV;
		}
	}
	/* initialize last tick */
	last_update_cyclone = readl(cyclone_timer);
	register_time_interpolator(&cyclone_interpolator);

	return 0;
}

__initcall(init_cyclone_clock);
