#ifndef _LINUX_SWSUSP_H
#define _LINUX_SWSUSP_H

#ifdef CONFIG_X86
#include <asm/suspend.h>
#endif
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/config.h>
#include <linux/init.h>

extern unsigned char software_suspend_enabled;

#define NORESUME	 1
#define RESUME_SPECIFIED 2

#ifdef CONFIG_SOFTWARE_SUSPEND
/* page backup entry */
typedef struct pbe {
	unsigned long address;		/* address of the copy */
	unsigned long orig_address;	/* original address of page */
	swp_entry_t swap_address;	
	swp_entry_t dummy;		/* we need scratch space at 
					 * end of page (see link, diskpage)
					 */
} suspend_pagedir_t;

#define SWAP_FILENAME_MAXLENGTH	32

struct suspend_header {
	__u32 version_code;
	unsigned long num_physpages;
	char machine[8];
	char version[20];
	int num_cpus;
	int page_size;
	suspend_pagedir_t *suspend_pagedir;
	unsigned int num_pbes;
	struct swap_location {
		char filename[SWAP_FILENAME_MAXLENGTH];
	} swap_location[MAX_SWAPFILES];
};

#define SUSPEND_PD_PAGES(x)     (((x)*sizeof(struct pbe))/PAGE_SIZE+1)
   
/* mm/vmscan.c */
extern int shrink_mem(void);

/* mm/page_alloc.c */
extern void drain_local_pages(void);

/* kernel/suspend.c */
extern void software_suspend(void);
extern void software_resume(void);

extern int register_suspend_notifier(struct notifier_block *);
extern int unregister_suspend_notifier(struct notifier_block *);
extern void refrigerator(unsigned long);

extern int freeze_processes(void);
extern void thaw_processes(void);

extern unsigned int nr_copy_pages __nosavedata;
extern suspend_pagedir_t *pagedir_nosave __nosavedata;

/* Communication between kernel/suspend.c and arch/i386/suspend.c */

extern void do_magic_resume_1(void);
extern void do_magic_resume_2(void);
extern void do_magic_suspend_1(void);
extern void do_magic_suspend_2(void);

/* Communication between acpi and arch/i386/suspend.c */

extern void do_suspend_lowlevel(int resume);
extern void do_suspend_lowlevel_s4bios(int resume);

#else
static inline void software_suspend(void)
{
}
#define software_resume()		do { } while(0)
#define register_suspend_notifier(a)	do { } while(0)
#define unregister_suspend_notifier(a)	do { } while(0)
#define refrigerator(a)			do { BUG(); } while(0)
#define freeze_processes()		do { panic("You need CONFIG_SOFTWARE_SUSPEND to do sleeps."); } while(0)
#define thaw_processes()		do { } while(0)
#endif

#endif /* _LINUX_SWSUSP_H */
