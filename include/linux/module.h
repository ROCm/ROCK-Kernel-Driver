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
#include <asm/local.h>

#include <asm/module.h>

/* Not Yet Implemented */
#define MODULE_SUPPORTED_DEVICE(name)
#define print_modules()

/* v850 toolchain uses a `_' prefix for all user symbols */
#ifndef MODULE_SYMBOL_PREFIX
#define MODULE_SYMBOL_PREFIX ""
#endif

#define MODULE_NAME_LEN (64 - sizeof(unsigned long))

struct kernel_symbol
{
	unsigned long value;
	const char *name;
};

struct modversion_info
{
	unsigned long crc;
	char name[MODULE_NAME_LEN];
};

/* These are either module local, or the kernel's dummy ones. */
extern int init_module(void);
extern void cleanup_module(void);

/* Archs provide a method of finding the correct exception table. */
struct exception_table_entry;

const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value);

#ifdef MODULE
#define ___module_cat(a,b) __mod_ ## a ## b
#define __module_cat(a,b) ___module_cat(a,b)
#define __MODULE_INFO(tag, name, info)					  \
static const char __module_cat(name,__LINE__)[]				  \
  __attribute__((section(".modinfo"),unused)) = __stringify(tag) "=" info

#define MODULE_GENERIC_TABLE(gtype,name)			\
extern const struct gtype##_id __mod_##gtype##_table		\
  __attribute__ ((unused, alias(__stringify(name))))

#define THIS_MODULE (&__this_module)

#else  /* !MODULE */

#define MODULE_GENERIC_TABLE(gtype,name)
#define __MODULE_INFO(tag, name, info)
#define THIS_MODULE ((struct module *)0)
#endif

/* Generic info of form tag = "info" */
#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

/* For userspace: you can also call me... */
#define MODULE_ALIAS(_alias) MODULE_INFO(alias, _alias)

/*
 * The following license idents are currently accepted as indicating free
 * software modules
 *
 *	"GPL"				[GNU Public License v2 or later]
 *	"GPL v2"			[GNU Public License v2]
 *	"GPL and additional rights"	[GNU Public License v2 rights and more]
 *	"Dual BSD/GPL"			[GNU Public License v2
 *					 or BSD license choice]
 *	"Dual MPL/GPL"			[GNU Public License v2
 *					 or Mozilla license choice]
 *
 * The following other idents are available
 *
 *	"Proprietary"			[Non free products]
 *
 * There are dual licensed components, but when running with Linux it is the
 * GPL that is relevant so this is a non issue. Similarly LGPL linked with GPL
 * is a GPL combined work.
 *
 * This exists for several reasons
 * 1.	So modinfo can show license info for users wanting to vet their setup 
 *	is free
 * 2.	So the community can ignore bug reports including proprietary modules
 * 3.	So vendors can do likewise based on their own policies
 */
#define MODULE_LICENSE(_license) MODULE_INFO(license, _license)

/* Author, ideally of form NAME <EMAIL>[, NAME <EMAIL>]*[ and NAME <EMAIL>] */
#define MODULE_AUTHOR(_author) MODULE_INFO(author, _author)
  
/* What your module does. */
#define MODULE_DESCRIPTION(_description) MODULE_INFO(description, _description)

/* One for each parameter, describing how to use it.  Some files do
   multiple of these per line, so can't just use MODULE_INFO. */
#define MODULE_PARM_DESC(_parm, desc) \
	__MODULE_INFO(parm, _parm, #_parm ":" desc)

#define MODULE_DEVICE_TABLE(type,name)		\
  MODULE_GENERIC_TABLE(type##_device,name)

/* Given an address, look for it in the exception tables */
const struct exception_table_entry *search_exception_tables(unsigned long add);

struct notifier_block;

#ifdef CONFIG_MODULES

/* Get/put a kernel symbol (calls must be symmetric) */
void *__symbol_get(const char *symbol);
void *__symbol_get_gpl(const char *symbol);
#define symbol_get(x) ((typeof(&x))(__symbol_get(MODULE_SYMBOL_PREFIX #x)))

#ifndef __GENKSYMS__
#ifdef CONFIG_MODVERSIONS
/* Mark the CRC weak since genksyms apparently decides not to
 * generate a checksums for some symbols */
#define __CRC_SYMBOL(sym, sec)					\
	extern void *__crc_##sym __attribute__((weak));		\
	static const unsigned long __kcrctab_##sym		\
	__attribute__((section("__kcrctab" sec), unused))	\
	= (unsigned long) &__crc_##sym;
#else
#define __CRC_SYMBOL(sym, sec)
#endif

/* For every exported symbol, place a struct in the __ksymtab section */
#define __EXPORT_SYMBOL(sym, sec)				\
	__CRC_SYMBOL(sym, sec)					\
	static const char __kstrtab_##sym[]			\
	__attribute__((section("__ksymtab_strings")))		\
	= MODULE_SYMBOL_PREFIX #sym;                    	\
	static const struct kernel_symbol __ksymtab_##sym	\
	__attribute__((section("__ksymtab" sec), unused))	\
	= { (unsigned long)&sym, __kstrtab_##sym }

#define EXPORT_SYMBOL(sym)					\
	__EXPORT_SYMBOL(sym, "")

#define EXPORT_SYMBOL_GPL(sym)					\
	__EXPORT_SYMBOL(sym, "_gpl")

#endif

/* We don't mangle the actual symbol anymore, so no need for
 * special casing EXPORT_SYMBOL_NOVERS.  FIXME: Deprecated */
#define EXPORT_SYMBOL_NOVERS(sym) EXPORT_SYMBOL(sym)

struct module_ref
{
	local_t count;
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
	const struct kernel_symbol *syms;
	unsigned int num_syms;
	const unsigned long *crcs;

	/* GPL-only exported symbols. */
	const struct kernel_symbol *gpl_syms;
	unsigned int num_gpl_syms;
	const unsigned long *gpl_crcs;

	/* Exception table */
	unsigned int num_exentries;
	const struct exception_table_entry *extable;

	/* Startup function. */
	int (*init)(void);

	/* If this is non-NULL, vfree after init() returns */
	void *module_init;

	/* Here is the actual code + data, vfree'd on unload. */
	void *module_core;

	/* Here are the sizes of the init and core sections */
	unsigned long init_size, core_size;

	/* The size of the executable code in each section.  */
	unsigned long init_text_size, core_text_size;

	/* Arch-specific module values */
	struct mod_arch_specific arch;

	/* Am I unsafe to unload? */
	int unsafe;

	/* Am I GPL-compatible */
	int license_gplok;

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
	unsigned long num_symtab;
	char *strtab;
#endif

	/* Per-cpu data. */
	void *percpu;

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

/* Is this address in a module? */
struct module *module_text_address(unsigned long addr);

/* Returns module and fills in value, defined and namebuf, or NULL if
   symnum out of range. */
struct module *module_get_kallsym(unsigned int symnum,
				  unsigned long *value,
				  char *type,
				  char namebuf[128]);
int is_exported(const char *name, const struct module *mod);

extern void __module_put_and_exit(struct module *mod, long code)
	__attribute__((noreturn));
#define module_put_and_exit(code) __module_put_and_exit(THIS_MODULE, code);

#ifdef CONFIG_MODULE_UNLOAD
unsigned int module_refcount(struct module *mod);
void __symbol_put(const char *symbol);
#define symbol_put(x) __symbol_put(MODULE_SYMBOL_PREFIX #x)
void symbol_put_addr(void *addr);

/* Sometimes we know we already have a refcount, and it's easier not
   to handle the error case (which only happens with rmmod --wait). */
static inline void __module_get(struct module *module)
{
	if (module) {
		BUG_ON(module_refcount(module) == 0);
		local_inc(&module->ref[get_cpu()].count);
		put_cpu();
	}
}

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
static inline void __module_get(struct module *module)
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

/* For extable.c to search modules' exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr);

int register_module_notifier(struct notifier_block * nb);
int unregister_module_notifier(struct notifier_block * nb);

#else /* !CONFIG_MODULES... */
#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define EXPORT_SYMBOL_NOVERS(sym)

/* Given an address, look for it in the exception tables. */
static inline const struct exception_table_entry *
search_module_extables(unsigned long addr)
{
	return NULL;
}

/* Is this address in a module? */
static inline struct module *module_text_address(unsigned long addr)
{
	return NULL;
}

/* Get/put a kernel symbol (calls should be symmetric) */
#define symbol_get(x) ({ extern typeof(x) x __attribute__((weak)); &(x); })
#define symbol_put(x) do { } while(0)
#define symbol_put_addr(x) do { } while(0)

static inline void __module_get(struct module *module)
{
}

static inline int try_module_get(struct module *module)
{
	return 1;
}

static inline void module_put(struct module *module)
{
}

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

static inline struct module *module_get_kallsym(unsigned int symnum,
						unsigned long *value,
						char *type,
						char namebuf[128])
{
	return NULL;
}

static inline int is_exported(const char *name, const struct module *mod)
{
	return 0;
}

static inline int register_module_notifier(struct notifier_block * nb)
{
	/* no events will happen anyway, so this can always succeed */
	return 0;
}

static inline int unregister_module_notifier(struct notifier_block * nb)
{
	return 0;
}

#define module_put_and_exit(code) do_exit(code)

#endif /* CONFIG_MODULES */

#ifdef MODULE
extern struct module __this_module;
#ifdef KBUILD_MODNAME
/* We make the linker do some of the work. */
struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = __stringify(KBUILD_MODNAME),
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
};
#endif /* KBUILD_MODNAME */
#endif /* MODULE */

#define symbol_request(x) try_then_request_module(symbol_get(x), "symbol:" #x)

/* BELOW HERE ALL THESE ARE OBSOLETE AND WILL VANISH */

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

static inline void __deprecated MOD_INC_USE_COUNT(struct module *module)
{
	__unsafe(module);

#if defined(CONFIG_MODULE_UNLOAD) && defined(MODULE)
	local_inc(&module->ref[get_cpu()].count);
	put_cpu();
#else
	(void)try_module_get(module);
#endif
}

static inline void __deprecated MOD_DEC_USE_COUNT(struct module *module)
{
	module_put(module);
}

#define MOD_INC_USE_COUNT	MOD_INC_USE_COUNT(THIS_MODULE)
#define MOD_DEC_USE_COUNT	MOD_DEC_USE_COUNT(THIS_MODULE)
#else
#define MODULE_PARM(var,type)
#define MOD_INC_USE_COUNT	do { } while (0)
#define MOD_DEC_USE_COUNT	do { } while (0)
#endif

#define __MODULE_STRING(x) __stringify(x)

/* Use symbol_get and symbol_put instead.  You'll thank me. */
#define HAVE_INTER_MODULE
extern void inter_module_register(const char *, struct module *, const void *);
extern void inter_module_unregister(const char *);
extern const void *inter_module_get(const char *);
extern const void *inter_module_get_request(const char *, const char *);
extern void inter_module_put(const char *);

#endif /* _LINUX_MODULE_H */
