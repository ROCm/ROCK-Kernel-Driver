/*****************************************************************************/
/* Kernel Hooks Interface.                                                   */
/* Author: Richard J Moore richardj_moore@uk.ibm.com                         */
/* 	   Vamsi Krishna S. r1vamsi@in.ibm.com                               */
/*	   Prasanna S P prasanna@in.ibm.com                                  */
/*                                                                           */
/* A sample kernel module registering exits for hooks defined in khook.o.    */
/*                                                                           */
/* Refer to Documentation/hook/HOWTO for details.                            */
/* (C) Copyright IBM Corp. 2003                                              */
/*****************************************************************************/
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/hook.h>

extern struct hook  test_hook1;
extern struct hook  test_hook2;
extern struct hook  test_hook3;
extern struct hook  test_hook4;
extern struct hook  test_hook5;
extern struct hook  ex_test_hook3;
extern struct hook  ex_test_hook4;
extern struct hook  ex_test_hook5;
int exit1(struct hook *, int *);	/* first exit for test_hook1 */
int exit2(struct hook *);		/* first exit for test_hook2 */
int exit3(struct hook *, int *);	/* second exit for test_hook1 */
int exit4(struct hook *);		/* second exit for test_hook2 */
int exit_var0(struct hook *);
int exit_var1(struct hook *, int);
int exit_var2(struct hook *, int, int);
int exit_var2_mismatch(struct hook *, int);

int ex_exit_var0(struct hook *);
int ex_exit_var1(struct hook *, int);
int ex_exit_var2(struct hook *, int, int);
int exit_var_args(struct hook * hook, ...);
extern int testhook(void);

struct hook_rec hook1;
struct hook_rec hook2;
struct hook_rec hook3;
struct hook_rec hook4;
struct hook_rec var0;
struct hook_rec var1;
struct hook_rec var2;
struct hook_rec ex_var0;
struct hook_rec ex_var1;
struct hook_rec ex_var2;

int init_module(void)
{
	int rc;

	hook1.hook_exit = &exit1;
	hook1.hook_exit_name = "exit1";
	rc = hook_exit_register(&test_hook1, &hook1);
	if (rc) {
		printk("hook_exit_register exit1 for hook1 returned %u\n", rc);
		goto err;
	}

	hook2.hook_exit = &exit2;
	hook2.hook_exit_name = "exit2";
	rc = hook_exit_register(&test_hook2, &hook2);
	if (rc) { 
		printk("hook_exit_register exit2 for hook2 returned %u\n", rc);
		goto err1;
	}

	hook3.hook_exit = &exit3;
	hook3.hook_exit_name = "exit3";
	rc = hook_exit_register(&test_hook1, &hook3);
	if (rc) {
		goto err2;
		printk("hook_exit_register exit3 for hook1 returned %u\n", rc);
	}
	hook4.hook_exit = &exit4;
	hook4.hook_exit_name = "exit4";
	rc = hook_exit_register(&test_hook2, &hook4);
	if (rc) {
		printk("hook_exit_register exit4 for hook2 returned %u\n", rc);
	 	goto err3;
	}

	var0.hook_exit = &exit_var0;
	rc = hook_exit_register(&test_hook3, &var0);
	if (rc) goto err4;

	var1.hook_exit = &exit_var1;
	rc = hook_exit_register(&test_hook4, &var1);
	if (rc) goto err5;

	var2.hook_exit = &exit_var2_mismatch;
	rc = hook_exit_register(&test_hook5, &var2);
	if (rc) goto err6;
	ex_var0.hook_exit = &ex_exit_var0;
	rc = hook_exit_register(&ex_test_hook3, &ex_var0);
	if (rc) goto err7;

	ex_var1.hook_exit = &ex_exit_var1;
	rc = hook_exit_register(&ex_test_hook4, &ex_var1);
	if (rc) goto err8;

	ex_var2.hook_exit = exit_var_args;
	rc = hook_exit_register(&ex_test_hook5, &ex_var2);
	if (rc) goto err9;
	printk("hook exits are registered, but not yet armed.\n");

	testhook();

	hook_exit_arm(&hook1);
	hook_exit_arm(&hook2);
	hook_exit_arm(&hook3);
	hook_exit_arm(&hook4);
	hook_exit_arm(&var0);
	hook_exit_arm(&var1);
	hook_exit_arm(&var2);
	hook_exit_arm(&ex_var0);
	hook_exit_arm(&ex_var1);
	hook_exit_arm(&ex_var2);

	printk("hook exit are now armed.\n");

	testhook();

	printk("disarming exit1 only\n");
	hook_exit_disarm(&hook1);

	testhook();

	printk("disarming exit2 only\n");
	hook_exit_disarm(&hook2);

	testhook();
	return 0;
	
err9:	hook_exit_deregister(&ex_var1);
err8:	hook_exit_deregister(&ex_var0);
err7:	hook_exit_deregister(&var2);
err6:	hook_exit_deregister(&var1);
err5:	hook_exit_deregister(&var0);
err4:	hook_exit_deregister(&hook4);
err3:	hook_exit_deregister(&hook3);
err2:	hook_exit_deregister(&hook2);
err1:	hook_exit_deregister(&hook1);
err: 	return rc;
}

void cleanup_module(void)
{
	/* deregister all hook exits*/
	hook_exit_deregister(&ex_var2);
	hook_exit_deregister(&ex_var1);
	hook_exit_deregister(&ex_var0);
	hook_exit_deregister(&var2);
	hook_exit_deregister(&var1);
	hook_exit_deregister(&var0);
	hook_exit_deregister(&hook4);
	hook_exit_deregister(&hook3);
	hook_exit_deregister(&hook2);
	hook_exit_deregister(&hook1);

	printk("deregistered all hook exits.\n");

	testhook();

	return;
}

/* exit1 exit to test_hook1 */
int exit1(struct hook * hook, int *rc)
{
	printk("exit1: hook exit1 entered, indicate for testhook to return immediately\n");
	*rc = 0;
	printk("exit1: hook exit1 exiting\n");
	return HOOK_RETURN;
}

/* exit2 exit to test_hook2 */
int exit2(struct hook * hook)
{
	printk("exit2: hook exit2 entered\n");
	printk("exit2: hook exit2 exiting\n");
	return HOOK_CONTINUE;
}

/* exit3 exit to test_hook1 */
int exit3(struct hook * hook, int *rc)
{
	printk("exit3: hook exit3 entered, allow testhook to continue\n");
	*rc = 0;
	printk("exit3: hook exit3 exiting\n");
	return HOOK_CONTINUE;
}

/* exit4 exit to test_hook2 */
int exit4(struct hook * hook)
{
	printk("exit4: hook exit4 entered\n");
	printk("exit4: hook exit4 exiting\n");
	return HOOK_CONTINUE;
}

int exit_var0(struct hook *h)
{
	printk("exit_var0: entered\n");
	printk("exit_var0: exiting\n");
	return HOOK_CONTINUE;
}
int exit_var1(struct hook *h, int i)
{
	printk("exit_var1: entered, %x\n", i);
	printk("exit_var1: exiting\n");
	return HOOK_CONTINUE;
}
int exit_var2(struct hook *h, int i, int j)
{
	printk("exit_var2: entered %x, %x\n", i, j);
	printk("exit_var2: exiting\n");
	return HOOK_CONTINUE;
}

int exit_var2_mismatch(struct hook *h, int i)
{
	printk("exit_var2: entered using only the first arg: %x\n", i);
	printk("exit_var2: exiting\n");
	return HOOK_CONTINUE;
}

int ex_exit_var0(struct hook *h)
{
	printk("ex_exit_var0: entered %s\n", h->hook_id);
	printk("ex_exit_var0: exiting\n");
	return HOOK_CONTINUE;
}
int ex_exit_var1(struct hook *h, int i)
{
	printk("ex_exit_var1: entered, %x\n", i);
	printk("ex_exit_var1: exiting\n");
	return HOOK_CONTINUE;
}
int ex_exit_var2(struct hook *h, int i, int j)
{
	printk("ex_exit_var2: entered %x, %x\n", i, j);
	printk("ex_exit_var2: exiting\n");
	return HOOK_CONTINUE;
}

int exit_var_args(struct hook * hook, ...)
{
	va_list args;
	char buf[80];
	char *fmt;
	printk("exit_var_args: entered\n");
	va_start(args, hook);
	fmt = va_arg(args, char *);
	vsprintf(buf, fmt, args);
	printk("vsprintf output=%s\n", buf);
	va_end(args);
	printk("exit_var_args: exiting\n");
	return HOOK_CONTINUE;
}
MODULE_LICENSE("GPL");
