/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/security.h>

#define SECURITY_SCAFFOLD_VERSION	"1.0.0"

/* garloff@suse.de, 2004-05-21:
 * lsm causes a performance problem, if compiled in, due to various
 * non-inlined indirect function calls.
 * This can be avoided by putting a branch in the inlined security
 * stubs in include/linux/security.h, calling directly into the cap_
 * functions from commoncap.
 * This has some consequences:
 * - If no security module is loaded, default will be the capability
 *   security fns, not the dummy ones.
 * - If a security module is loaded, it will override the defaults;
 *   this module might be capability itself, overriding itself, 
 *   only causing a slowdown. This means that capability should NOT 
 *   be compiled into the kernel.
 * - Another module can be loaded, and capability, being a module again,
 *   might be stacked as secondary module.
 * - Unfortunately, we can't get rid of dummy, as we don't want to
 *   change the default behaviour if a security module is loaded and
 *   some stubs are not implemented in which case these default to
 *   dummy (which behaves differently to capability for some stubs). 
 * - If no security module is loaded, we set security_ops to point
 *   to capability_security_ops; it will not normally be used except for 
 *   one situation: When a security module is unloaded; the value of 
 *   security_enabled may still be evaluated to 1 when the security_ops 
 *   is already changed. The behaviour is consistent here, as we do
 *   change security_ops back to point to capability_security_ops.
 * - commoncaps needs to be compiled in unconditionally.
 */ 

/* things that live in dummy.c */
extern void security_fixup_ops (struct security_operations *ops);
/* default security ops */
extern struct security_operations capability_security_ops;

struct security_operations *security_ops;	/* Initialized to NULL */
int security_enabled;				/* ditto */
EXPORT_SYMBOL(security_enabled);

static inline int verify (struct security_operations *ops)
{
	/* verify the security_operations structure exists */
	if (!ops) {
		printk (KERN_INFO "Passed a NULL security_operations "
			"pointer, %s failed.\n", __FUNCTION__);
		return -EINVAL;
	}
	security_fixup_ops (ops);
	return 0;
}

static void __init do_security_initcalls(void)
{
	initcall_t *call;
	call = &__security_initcall_start;
	while (call < &__security_initcall_end) {
		(*call)();
		call++;
	}
}

/**
 * security_scaffolding_startup - initializes the security scaffolding framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int __init security_scaffolding_startup (void)
{
	printk (KERN_INFO "Security Scaffold v" SECURITY_SCAFFOLD_VERSION
		" initialized\n");
	
	if (verify (&capability_security_ops)) {
		printk (KERN_ERR "%s could not verify "
			"dummy_security_ops structure.\n", __FUNCTION__);
		return -EIO;
	}
	security_enabled = 0;
	security_ops = &capability_security_ops;
	
	/* Init compiled-in security modules */
	do_security_initcalls();

	return 0;
}

/**
 * register_security - registers a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function is to allow a security module to register itself with the
 * kernel security subsystem.  Some rudimentary checking is done on the @ops
 * value passed to this function.  A call to unregister_security() should be
 * done to remove this security_options structure from the kernel.
 *
 * If there is already a security module registered with the kernel,
 * an error will be returned.  Otherwise 0 is returned on success.
 */
int register_security (struct security_operations *ops)
{
	if (verify (ops)) {
		printk (KERN_INFO "%s could not verify "
			"security_operations structure.\n", __FUNCTION__);
		return -EINVAL;
	}

	if (security_ops != &capability_security_ops) {
		printk (KERN_INFO "There is already a security "
			"framework initialized, %s failed.\n", __FUNCTION__);
		return -EINVAL;
	}

	security_ops = ops;
	security_enabled = 1;

	return 0;
}

/**
 * unregister_security - unregisters a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function removes a struct security_operations variable that had
 * previously been registered with a successful call to register_security().
 *
 * If @ops does not match the valued previously passed to register_security()
 * an error is returned.  Otherwise the default security options is set to the
 * the dummy_security_ops structure, and 0 is returned.
 */
int unregister_security (struct security_operations *ops)
{
	if (ops != security_ops) {
		printk (KERN_INFO "%s: trying to unregister "
			"a security_ops structure that is not "
			"registered, failing.\n", __FUNCTION__);
		return -EINVAL;
	}

	security_enabled = 0;
	security_ops = &capability_security_ops;
	
	return 0;
}

/**
 * mod_reg_security - allows security modules to be "stacked"
 * @name: a pointer to a string with the name of the security_options to be registered
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function allows security modules to be stacked if the currently loaded
 * security module allows this to happen.  It passes the @name and @ops to the
 * register_security function of the currently loaded security module.
 *
 * The return value depends on the currently loaded security module, with 0 as
 * success.
 */
int mod_reg_security (const char *name, struct security_operations *ops)
{
	if (verify (ops)) {
		printk (KERN_INFO "%s could not verify "
			"security operations.\n", __FUNCTION__);
		return -EINVAL;
	}

	if (ops == security_ops) {
		printk (KERN_INFO "%s security operations "
			"already registered.\n", __FUNCTION__);
		return -EINVAL;
	}

	return security_ops->register_security (name, ops);
}

/**
 * mod_unreg_security - allows a security module registered with mod_reg_security() to be unloaded
 * @name: a pointer to a string with the name of the security_options to be removed
 * @ops: a pointer to the struct security_options that is to be removed
 *
 * This function allows security modules that have been successfully registered
 * with a call to mod_reg_security() to be unloaded from the system.
 * This calls the currently loaded security module's unregister_security() call
 * with the @name and @ops variables.
 *
 * The return value depends on the currently loaded security module, with 0 as
 * success.
 */
int mod_unreg_security (const char *name, struct security_operations *ops)
{
	if (ops == security_ops) {
		printk (KERN_INFO "%s invalid attempt to unregister "
			" primary security ops.\n", __FUNCTION__);
		return -EINVAL;
	}

	return security_ops->unregister_security (name, ops);
}

/**
 * capable - calls the currently loaded security module's capable() function with the specified capability
 * @cap: the requested capability level.
 *
 * This function calls the currently loaded security module's capable()
 * function with a pointer to the current task and the specified @cap value.
 *
 * This allows the security module to implement the capable function call
 * however it chooses to.
 */
int capable (int cap)
{
	if (security_ops->capable (current, cap)) {
		/* capability denied */
		return 0;
	}

	/* capability granted */
	current->flags |= PF_SUPERPRIV;
	return 1;
}

EXPORT_SYMBOL_GPL(register_security);
EXPORT_SYMBOL_GPL(unregister_security);
EXPORT_SYMBOL_GPL(mod_reg_security);
EXPORT_SYMBOL_GPL(mod_unreg_security);
EXPORT_SYMBOL(capable);
EXPORT_SYMBOL(security_ops);
