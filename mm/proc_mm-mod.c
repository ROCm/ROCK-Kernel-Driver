#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/proc_mm.h>
#include <linux/ptrace.h>
#include <linux/module.h>

#ifdef CONFIG_64BIT
#define PRINT_OFFSET(type, member) \
	printk(KERN_DEBUG "struct " #type "32->" #member " \t: %ld\n", (long) offsetof(struct type ## 32, member))
#else
#define PRINT_OFFSET(type, member) \
	printk(KERN_DEBUG "struct " #type "->" #member " \t: %ld\n", (long) offsetof(struct type, member))
#endif

static int debug_printoffsets(void)
{
	printk(KERN_DEBUG "Skas core structures layout BEGIN:\n");
	PRINT_OFFSET(mm_mmap, addr);
	PRINT_OFFSET(mm_mmap, len);
	PRINT_OFFSET(mm_mmap, prot);
	PRINT_OFFSET(mm_mmap, flags);
	PRINT_OFFSET(mm_mmap, fd);
	PRINT_OFFSET(mm_mmap, offset);

	PRINT_OFFSET(mm_munmap, addr);
	PRINT_OFFSET(mm_munmap, len);

	PRINT_OFFSET(mm_mprotect, addr);
	PRINT_OFFSET(mm_mprotect, len);
	PRINT_OFFSET(mm_mprotect, prot);

	PRINT_OFFSET(proc_mm_op, op);
	PRINT_OFFSET(proc_mm_op, u);
	PRINT_OFFSET(proc_mm_op, u.mmap);
	PRINT_OFFSET(proc_mm_op, u.munmap);
	PRINT_OFFSET(proc_mm_op, u.mprotect);
	PRINT_OFFSET(proc_mm_op, u.copy_segments);

	PRINT_OFFSET(ptrace_faultinfo, is_write);
	PRINT_OFFSET(ptrace_faultinfo, addr);

	PRINT_OFFSET(ptrace_ldt, func);
	PRINT_OFFSET(ptrace_ldt, ptr);
	PRINT_OFFSET(ptrace_ldt, bytecount);
	printk(KERN_DEBUG "Skas core structures layout END.\n");

	return 0;
}
#undef PRINT_OFFSET

module_init(debug_printoffsets);
