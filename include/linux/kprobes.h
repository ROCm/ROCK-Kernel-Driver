#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H
#include <linux/config.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <asm/kprobes.h>

struct kprobe;
struct pt_regs;

typedef int (*kprobe_pre_handler_t)(struct kprobe *, struct pt_regs *);
typedef void (*kprobe_post_handler_t)(struct kprobe *, struct pt_regs *,
				      unsigned long flags);
typedef int (*kprobe_fault_handler_t)(struct kprobe *, struct pt_regs *,
				      int trapnr);
struct kprobe {
	struct list_head list;

	/* location of the probe point */
	kprobe_opcode_t *addr;

	/* user space probe info */
	struct uprobe *user;
 
	 /* Called before addr is executed. */
	kprobe_pre_handler_t pre_handler;

	/* Called after addr is executed, unless... */
	kprobe_post_handler_t post_handler;

	 /* ... called if executing addr causes a fault (eg. page fault).
	  * Return 1 if it handled fault, otherwise kernel will see it. */
	kprobe_fault_handler_t fault_handler;

	/* Saved opcode (which has been replaced with breakpoint) */
	kprobe_opcode_t opcode;

	/* copy of the original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

struct uprobe {
	struct inode *inode;
	unsigned long offset;
	kprobe_opcode_t *addr;

	/* for kprobes internal use */
	struct vm_area_struct *vma;
	struct page *page;
};

#ifdef CONFIG_KPROBES
/* Locks kprobe: irq must be disabled */
void lock_kprobes(void);
void unlock_kprobes(void);

/* kprobe running now on this CPU? */
static inline int kprobe_running(void)
{
	extern unsigned int kprobe_cpu;
	return kprobe_cpu == smp_processor_id();
}

extern void arch_prepare_kprobe(struct kprobe *p);

/* Get the kprobe at this addr (if any).  Must have called lock_kprobes */
extern struct kprobe *get_kprobe(void *addr);
extern void put_kprobe(struct kprobe *p);
extern void set_opcode(struct kprobe *p, kprobe_opcode_t opcode);

extern int register_kprobe(struct kprobe *p);
extern void unregister_kprobe(struct kprobe *p);
extern int register_kprobe_user(struct kprobe *p);
extern void unregister_kprobe_user(struct kprobe *p);
extern int insert_kprobe_user(struct kprobe *p);
extern int remove_kprobe_user(struct kprobe *p);
#else
static inline int kprobe_running(void) { return 0; }
static inline int register_kprobe(struct kprobe *p) { return -ENOSYS; }
static inline void unregister_kprobe(struct kprobe *p) { }
static inline int register_kprobe_user(struct kprobe *p) { return -ENOSYS; }
static inline void unregister_kprobe_user(struct kprobe *p) { }
static inline int insert_kprobe_user(struct kprobe *p) { return -ENOSYS; }
static inline int remove_kprobe_user(struct kprobe *p) { return -ENOSYS; }
#endif
#endif /* _LINUX_KPROBES_H */
