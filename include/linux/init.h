#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/config.h>

/* These macros are used to mark some functions or 
 * initialized data (doesn't apply to uninitialized data)
 * as `initialization' functions. The kernel can take this
 * as hint that the function is used only during the initialization
 * phase and free up used memory resources after
 *
 * Usage:
 * For functions:
 * 
 * You should add __init immediately before the function name, like:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * If the function has a prototype somewhere, you can also add
 * __init between closing brace of the prototype and semicolon:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * For initialized data:
 * You should insert __initdata between the variable name and equal
 * sign followed by value, e.g.:
 *
 * static int init_variable __initdata = 0;
 * static char linux_logo[] __initdata = { 0x32, 0x36, ... };
 *
 * Don't forget to initialize data not at file scope, i.e. within a function,
 * as gcc otherwise puts the data into the bss section and not into the init
 * section.
 * 
 * Also note, that this data cannot be "const".
 */

/* These are for everybody (although not all archs will actually
   discard it in modules) */
#define __init		__attribute__ ((__section__ (".init.text")))
#define __initdata	__attribute__ ((__section__ (".init.data")))
#define __exit		__attribute__ ((__section__(".exit.text")))
#define __exitdata	__attribute__ ((__section__(".exit.data")))
#define __exit_call	__attribute__ ((unused,__section__ (".exitcall.exit")))

/* For assembly routines */
#define __INIT		.section	".init.text","ax"
#define __FINIT		.previous
#define __INITDATA	.section	".init.data","aw"

#ifndef __ASSEMBLY__
/*
 * Used for initialization calls..
 */
typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);
#endif

#ifndef MODULE

#ifndef __ASSEMBLY__

/* initcalls are now grouped by functionality into separate 
 * subsections. Ordering inside the subsections is determined
 * by link order. 
 * For backwards compatability, initcall() puts the call in 
 * the device init subsection.
 */

#define __define_initcall(level,fn) \
	static initcall_t __initcall_##fn __attribute__ ((unused,__section__ (".initcall" level ".init"))) = fn

#define core_initcall(fn)		__define_initcall("1",fn)
#define postcore_initcall(fn)		__define_initcall("2",fn)
#define arch_initcall(fn)		__define_initcall("3",fn)
#define subsys_initcall(fn)		__define_initcall("4",fn)
#define fs_initcall(fn)			__define_initcall("5",fn)
#define device_initcall(fn)		__define_initcall("6",fn)
#define late_initcall(fn)		__define_initcall("7",fn)

#define __initcall(fn) device_initcall(fn)

#define __exitcall(fn)							\
	static exitcall_t __exitcall_##fn __exit_call = fn

/*
 * Used for kernel command line parameter setup
 */
struct kernel_param {
	const char *str;
	int (*setup_func)(char *);
};

extern struct kernel_param __setup_start, __setup_end;

#define __setup(str, fn)						\
	static char __setup_str_##fn[] __initdata = str;		\
	static struct kernel_param __setup_##fn				\
		 __attribute__((unused,__section__ (".init.setup")))	\
		= { __setup_str_##fn, fn }

#endif /* __ASSEMBLY__ */

/**
 * module_init() - driver initialization entry point
 * @x: function to be run at kernel boot time or module insertion
 * 
 * module_init() will either be called during do_initcalls (if
 * builtin) or at module insertion time (if a module).  There can only
 * be one per module. */
#define module_init(x)	__initcall(x);

/**
 * module_exit() - driver exit entry point
 * @x: function to be run when driver is removed
 * 
 * module_exit() will wrap the driver clean-up code
 * with cleanup_module() when used with rmmod when
 * the driver is a module.  If the driver is statically
 * compiled into the kernel, module_exit() has no effect.
 * There can only be one per module.
 */
#define module_exit(x)	__exitcall(x);

/**
 * no_module_init - code needs no initialization.
 *
 * The equivalent of declaring an empty init function which returns 0.
 * Every module must have exactly one module_init() or no_module_init
 * invocation.  */
#define no_module_init

#else /* MODULE */

/* Don't use these in modules, but some people do... */
#define core_initcall(fn)		module_init(fn)
#define postcore_initcall(fn)		module_init(fn)
#define arch_initcall(fn)		module_init(fn)
#define subsys_initcall(fn)		module_init(fn)
#define fs_initcall(fn)			module_init(fn)
#define device_initcall(fn)		module_init(fn)
#define late_initcall(fn)		module_init(fn)

/* Each module knows its own name. */
#define __DEFINE_MODULE_NAME						\
	char __module_name[] __attribute__((section(".modulename"))) =	\
	__stringify(KBUILD_MODNAME)

/* These macros create a dummy inline: gcc 2.9x does not count alias
 as usage, hence the `unused function' warning when __init functions
 are declared static. We use the dummy __*_module_inline functions
 both to kill the warning and check the type of the init/cleanup
 function. */

/* Each module must use one module_init(), or one no_module_init */
#define module_init(initfn)					\
	__DEFINE_MODULE_NAME;					\
	static inline initcall_t __inittest(void)		\
	{ return initfn; }					\
	int __initfn(void) __attribute__((alias(#initfn)));

#define no_module_init __DEFINE_MODULE_NAME

/* This is only required if you want to be unloadable. */
#define module_exit(exitfn)					\
	static inline exitcall_t __exittest(void)		\
	{ return exitfn; }					\
	void __exitfn(void) __attribute__((alias(#exitfn)));


#define __setup(str,func) /* nothing */
#endif

/* Data marked not to be saved by software_suspend() */
#define __nosavedata __attribute__ ((__section__ (".data.nosave")))

#ifdef CONFIG_HOTPLUG
#define __devinit
#define __devinitdata
#define __devexit
#define __devexitdata
#else
#define __devinit __init
#define __devinitdata __initdata
#define __devexit __exit
#define __devexitdata __exitdata
#endif

/* Functions marked as __devexit may be discarded at kernel link time, depending
   on config options.  Newer versions of binutils detect references from
   retained sections to discarded sections and flag an error.  Pointers to
   __devexit functions must use __devexit_p(function_name), the wrapper will
   insert either the function_name or NULL, depending on the config options.
 */
#if defined(MODULE) || defined(CONFIG_HOTPLUG)
#define __devexit_p(x) x
#else
#define __devexit_p(x) NULL
#endif

#endif /* _LINUX_INIT_H */
