/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
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

extern struct security_operations dummy_security_ops;	/* lives in dummy.c */

struct security_operations *security_ops;	/* Initialized to NULL */

/* This macro checks that all pointers in a struct are non-NULL.  It 
 * can be fooled by struct padding for object tile alignment and when
 * pointers to data and pointers to functions aren't the same size.
 * Yes it's ugly, we'll replace it if it becomes a problem.
 */
#define VERIFY_STRUCT(struct_type, s, e) \
	do { \
		unsigned long * __start = (unsigned long *)(s); \
		unsigned long * __end = __start + \
				sizeof(struct_type)/sizeof(unsigned long *); \
		while (__start != __end) { \
			if (!*__start) { \
				printk(KERN_INFO "%s is missing something\n",\
					#struct_type); \
				e++; \
				break; \
			} \
			__start++; \
		} \
	} while (0)

static int inline verify (struct security_operations *ops)
{
	int err;

	/* verify the security_operations structure exists */
	if (!ops) {
		printk (KERN_INFO "Passed a NULL security_operations "
			"pointer, " __FUNCTION__ " failed.\n");
		return -EINVAL;
	}

	/* Perform a little sanity checking on our inputs */
	err = 0;

	/* This first check scans the whole security_ops struct for
	 * missing structs or functions.
	 *
	 * (There is no further check now, but will leave as is until
	 *  the lazy registration stuff is done -- JM).
	 */
	VERIFY_STRUCT(struct security_operations, ops, err);

	if (err) {
		printk (KERN_INFO "Not enough functions specified in the "
			"security_operation structure, " __FUNCTION__
			" failed.\n");
		return -EINVAL;
	}
	return 0;
}

/**
 * security_scaffolding_startup - initialzes the security scaffolding framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int security_scaffolding_startup (void)
{
	printk (KERN_INFO "Security Scaffold v" SECURITY_SCAFFOLD_VERSION
		" initialized\n");

	security_ops = &dummy_security_ops;

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
 * If the @ops structure does not contain function pointers for all hooks in
 * the structure, or there is already a security module registered with the
 * kernel, an error will be returned.  Otherwise 0 is returned on success.
 */
int register_security (struct security_operations *ops)
{

	if (verify (ops)) {
		printk (KERN_INFO __FUNCTION__ " could not verify "
			"security_operations structure.\n");
		return -EINVAL;
	}
	if (security_ops != &dummy_security_ops) {
		printk (KERN_INFO "There is already a security "
			"framework initialized, " __FUNCTION__ " failed.\n");
		return -EINVAL;
	}

	security_ops = ops;

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
		printk (KERN_INFO __FUNCTION__ ": trying to unregister "
			"a security_opts structure that is not "
			"registered, failing.\n");
		return -EINVAL;
	}

	security_ops = &dummy_security_ops;

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
		printk (KERN_INFO __FUNCTION__ " could not verify "
			"security operations.\n");
		return -EINVAL;
	}

	if (ops == security_ops) {
		printk (KERN_INFO __FUNCTION__ " security operations "
			"already registered.\n");
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
		printk (KERN_INFO __FUNCTION__ " invalid attempt to unregister "
			" primary security ops.\n");
		return -EINVAL;
	}

	return security_ops->unregister_security (name, ops);
}

/**
 * capable - calls the currently loaded security module's capable() function with the specified capability
 * @cap: the requested capability level.
 *
 * This function calls the currently loaded security module's cabable()
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

/**
 * sys_security - security syscall multiplexor.
 * @id: module id
 * @call: call identifier
 * @args: arg list for call
 *
 * Similar to sys_socketcall.  Can use id to help identify which module user
 * app is talking to.  The recommended convention for creating the
 * hexadecimal id value is:
 * 'echo "Name_of_module" | md5sum | cut -c -8'.
 * By following this convention, there's no need for a central registry.
 */
asmlinkage long sys_security (unsigned int id, unsigned int call,
			      unsigned long *args)
{
	return security_ops->sys_security (id, call, args);
}

EXPORT_SYMBOL (register_security);
EXPORT_SYMBOL (unregister_security);
EXPORT_SYMBOL (mod_reg_security);
EXPORT_SYMBOL (mod_unreg_security);
EXPORT_SYMBOL (capable);
EXPORT_SYMBOL (security_ops);
