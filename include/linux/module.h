#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H
/*
 * Dynamic loading of modules into the kernel.
 *
 * Rewritten by Richard Henderson <rth@tamu.edu> Dec 1996
 * Rewritten again by Rusty Russell, 2002
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/kmod.h>
#include <linux/elf.h>
#include <linux/stringify.h>

#include <asm/module.h>
#include <asm/uaccess.h> /* For struct exception_table_entry */

/* Not Yet Implemented */
#define MODULE_LICENSE(name)
#define MODULE_AUTHOR(name)
#define MODULE_DESCRIPTION(desc)
#define MODULE_SUPPORTED_DEVICE(name)
#define MODULE_PARM_DESC(var,desc)
#define print_modules()

/* v850 toolchain uses a `_' prefix for all user symbols */
#ifndef MODULE_SYMBOL_PREFIX
#define MODULE_SYMBOL_PREFIX ""
#endif

#define MODULE_NAME_LEN (64 - sizeof(unsigned long))
struct kernel_symbol
{
	unsigned long value;
	char name[MODULE_NAME_LEN];
};

/* These are either module local, or the kernel's dummy ones. */
extern int init_module(void);
extern void cleanup_module(void);

#ifdef MODULE

/* For replacement modutils, use an alias not a pointer. */
#define MODULE_GENERIC_TABLE(gtype,name)			\
static const unsigned long __module_##gtype##_size		\
  __attribute__ ((unused)) = sizeof(struct gtype##_id);		\
static const struct gtype##_id * __module_##gtype##_table	\
  __attribute__ ((unused)) = name;				\
extern const struct gtype##_id __mod_##gtype##_table		\
  __attribute__ ((unused, alias(__stringify(name))))

#define THIS_MODULE (&__this_module)

#else  /* !MODULE */

#define MODULE_GENERIC_TABLE(gtype,name)
#define THIS_MODULE ((struct module *)0)

#endif

#define MODULE_DEVICE_TABLE(type,name)		\
  MODULE_GENERIC_TABLE(type##_device,name)

struct kernel_symbol_group
{
	/* Links us into the global symbol list */
	struct list_head list;

	/* Module which owns it (if any) */
	struct module *owner;

	unsigned int num_syms;
	const struct kernel_symbol *syms;
};

struct exception_table
{
	struct list_head list;

	unsigned int num_entries;
	const struct exception_table_entry *entry;
};


#ifdef CONFIG_MODULES
/* Get/put a kernel symbol (calls must be symmetric) */
void *__symbol_get(const char *symbol);
void *__symbol_get_gpl(const char *symbol);
#define symbol_get(x) ((typeof(&x))(__symbol_get(MODULE_SYMBOL_PREFIX #x)))

/* For every exported symbol, place a struct in the __ksymtab section */
#define EXPORT_SYMBOL(sym)				\
	const struct kernel_symbol __ksymtab_##sym	\
	__attribute__((section("__ksymtab")))		\
	= { (unsigned long)&sym, MODULE_SYMBOL_PREFIX #sym }

#define EXPORT_SYMBOL_NOVERS(sym) EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym) EXPORT_SYMBOL(sym)

struct module_ref
{
	atomic_t count;
} ____cacheline_aligned;

enum module_state
{
	MODULE_STATE_LIVE,
	MODULE_STATE_COMING,
	MODULE_STATE_GOING,
};

struct module
{
	enum module_state state;

	/* Member of list of modules */
	struct list_head list;

	/* Unique handle for this module */
	char name[MODULE_NAME_LEN];

	/* Exported symbols */
	struct kernel_symbol_group symbols;

	/* Exception tables */
	struct exception_table extable;

	/* Startup function. */
	int (*init)(void);

	/* If this is non-NULL, vfree after init() returns */
	void *module_init;

	/* Here is the actual code + data, vfree'd on unload. */
	void *module_core;

	/* Here are the sizes of the init and core sections */
	unsigned long init_size, core_size;

	/* Arch-specific module values */
	struct mod_arch_specific arch;

	/* Am I unsafe to unload? */
	int unsafe;

#ifdef CONFIG_MODULE_UNLOAD
	/* Reference counts */
	struct module_ref ref[NR_CPUS];

	/* What modules depend on me? */
	struct list_head modules_which_use_me;

	/* Who is waiting for us to be unloaded */
	struct task_struct *waiter;

	/* Destruction function. */
	void (*exit)(void);
#endif

#ifdef CONFIG_KALLSYMS
	/* We keep the symbol and string tables for kallsyms. */
	Elf_Sym *symtab;
	unsigned long num_syms;
	char *strtab;
#endif

	/* The command line arguments (may be mangled).  People like
	   keeping pointers to this stuff */
	char *args;
};

/* FIXME: It'd be nice to isolate modules during init, too, so they
   aren't used before they (may) fail.  But presently too much code
   (IDE & SCSI) require entry into the module during init.*/
static inline int module_is_live(struct module *mod)
{
	return mod->state != MODULE_STATE_GOING;
}

#ifdef CONFIG_MODULE_UNLOAD

void __symbol_put(const char *symbol);
#define symbol_put(x) __symbol_put(MODULE_SYMBOL_PREFIX #x)
void symbol_put_addr(void *addr);

/* We only need protection against local interrupts. */
#ifndef __HAVE_ARCH_LOCAL_INC
#define local_inc(x) atomic_inc(x)
#define local_dec(x) atomic_dec(x)
#endif

static inline int try_module_get(struct module *module)
{
	int ret = 1;

	if (module) {
		unsigned int cpu = get_cpu();
		if (likely(module_is_live(module)))
			local_inc(&module->ref[cpu].count);
		else
			ret = 0;
		put_cpu();
	}
	return ret;
}

static inline void module_put(struct module *module)
{
	if (module) {
		unsigned int cpu = get_cpu();
		local_dec(&module->ref[cpu].count);
		/* Maybe they're waiting for us to drop reference? */
		if (unlikely(!module_is_live(module)))
			wake_up_process(module->waiter);
		put_cpu();
	}
}

#else /*!CONFIG_MODULE_UNLOAD*/
static inline int try_module_get(struct module *module)
{
	return !module || module_is_live(module);
}
static inline void module_put(struct module *module)
{
}
#define symbol_put(x) do { } while(0)
#define symbol_put_addr(p) do { } while(0)

#endif /* CONFIG_MODULE_UNLOAD */

/* This is a #define so the string doesn't get put in every .o file */
#define module_name(mod)			\
({						\
	struct module *__mod = (mod);		\
	__mod ? __mod->name : "kernel";		\
})

#define __unsafe(mod)							     \
do {									     \
	if (mod && !(mod)->unsafe) {					     \
		printk(KERN_WARNING					     \
		       "Module %s cannot be unloaded due to unsafe usage in" \
		       " %s:%u\n", (mod)->name, __FILE__, __LINE__);	     \
		(mod)->unsafe = 1;					     \
	}								     \
} while(0)

/* For kallsyms to ask for address resolution.  NULL means not found. */
const char *module_address_lookup(unsigned long addr,
				  unsigned long *symbolsize,
				  unsigned long *offset,
				  char **modname);

#else /* !CONFIG_MODULES... */
#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define EXPORT_SYMBOL_NOVERS(sym)

/* Get/put a kernel symbol (calls should be symmetric) */
#define symbol_get(x) (&(x))
#define symbol_put(x) do { } while(0)
#define symbol_put_addr(x) do { } while(0)

#define try_module_get(module) 1
#define module_put(module) do { } while(0)

#define module_name(mod) "kernel"

#define __unsafe(mod)

/* For kallsyms to ask for address resolution.  NULL means not found. */
static inline const char *module_address_lookup(unsigned long addr,
						unsigned long *symbolsize,
						unsigned long *offset,
						char **modname)
{
	return NULL;
}
#endif /* CONFIG_MODULES */

#ifdef MODULE
extern struct module __this_module;
#ifdef KBUILD_MODNAME
/* We make the linker do some of the work. */
struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = __stringify(KBUILD_MODNAME),
	.symbols = { .owner = &__this_module },
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
};
#endif /* KBUILD_MODNAME */
#endif /* MODULE */

/* For archs to search exception tables */
extern struct list_head extables;
extern spinlock_t modlist_lock;

#define symbol_request(x) try_then_request_module(symbol_get(x), "symbol:" #x)

/* BELOW HERE ALL THESE ARE OBSOLETE AND WILL VANISH */
static inline void __deprecated __MOD_INC_USE_COUNT(struct module *module)
{
	__unsafe(module);
	/*
	 * Yes, we ignore the retval here, that's why it's deprecated.
	 */
	try_module_get(module);
}

static inline void __deprecated __MOD_DEC_USE_COUNT(struct module *module)
{
	module_put(module);
}

#define SET_MODULE_OWNER(dev) ((dev)->owner = THIS_MODULE)

struct obsolete_modparm {
	char name[64];
	char type[64-sizeof(void *)];
	void *addr;
};
#ifdef MODULE
/* DEPRECATED: Do not use. */
#define MODULE_PARM(var,type)						    \
struct obsolete_modparm __parm_##var __attribute__((section("__obsparm"))) = \
{ __stringify(var), type };

#else
#define MODULE_PARM(var,type)
#endif

/* People do this inside their init routines, when the module isn't
   "live" yet.  They should no longer be doing that, but
   meanwhile... */
static inline void __deprecated _MOD_INC_USE_COUNT(struct module *module)
{
	__unsafe(module);

#if defined(CONFIG_MODULE_UNLOAD) && defined(MODULE)
	local_inc(&module->ref[get_cpu()].count);
	put_cpu();
#else
	try_module_get(module);
#endif
}
#define MOD_INC_USE_COUNT \
	_MOD_INC_USE_COUNT(THIS_MODULE)
#define MOD_DEC_USE_COUNT \
	__MOD_DEC_USE_COUNT(THIS_MODULE)
#define EXPORT_NO_SYMBOLS
extern int module_dummy_usage;
#define GET_USE_COUNT(module) (module_dummy_usage)
#define MOD_IN_USE 0
#define __MODULE_STRING(x) __stringify(x)
#define __mod_between(a_start, a_len, b_start, b_len)		\
(((a_start) >= (b_start) && (a_start) <= (b_start)+(b_len))	\
 || ((a_start)+(a_len) >= (b_start)				\
     && (a_start)+(a_len) <= (b_start)+(b_len)))
#define mod_bound(p, n, m)					\
(((m)->module_init						\
  && __mod_between((p),(n),(m)->module_init,(m)->init_size))	\
 || __mod_between((p),(n),(m)->module_core,(m)->core_size))

/*
 * The exception and symbol tables, and the lock
 * to protect them.
 */
extern spinlock_t modlist_lock;
extern struct list_head extables;
extern struct list_head symbols;

/* Use symbol_get and symbol_put instead.  You'll thank me. */
#define HAVE_INTER_MODULE
extern void inter_module_register(const char *, struct module *, const void *);
extern void inter_module_unregister(const char *);
extern const void *inter_module_get(const char *);
extern const void *inter_module_get_request(const char *, const char *);
extern void inter_module_put(const char *);

#endif /* _LINUX_MODULE_H */
