/****************************************************************************/
/* Kernel Hooks Interface.                                    		    */
/* Author: Richard J Moore richardj_moore@uk.ibm.com                        */
/* 	   Vamsi Krishna S. r1vamsi@in.ibm.com 				    */
/*	   Prasanna S P prasanna@in.ibm.com                                 */
/*                                                                          */
/* A sample kernel module with hooks in it (in the testhook function).      */
/*                                                                          */
/*  (C) Copyright IBM Corp. 2003                                            */
/* Refer to Documentation/hook/HOWTO for details.                           */
/****************************************************************************/
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/hook.h>

#define TEST_HOOK1 test_hook1
#define TEST_HOOK2 test_hook2
#define TEST_HOOK3 test_hook3
#define TEST_HOOK4 test_hook4
#define TEST_HOOK5 test_hook5
#define EX_TEST_HOOK3 ex_test_hook3
#define EX_TEST_HOOK4 ex_test_hook4
#define EX_TEST_HOOK5 ex_test_hook5

DECLARE_HOOK(TEST_HOOK1);
DECLARE_HOOK(TEST_HOOK2);
DECLARE_HOOK(TEST_HOOK3);
DECLARE_HOOK(TEST_HOOK4);
DECLARE_HOOK(TEST_HOOK5);
DECLARE_EXCLUSIVE_HOOK(EX_TEST_HOOK3);
DECLARE_EXCLUSIVE_HOOK(EX_TEST_HOOK4);
DECLARE_EXCLUSIVE_HOOK(EX_TEST_HOOK5);

int init_module(void)
{
	printk("now load kexit.o to test the hook(s)\n");
	return 0;
}

void cleanup_module(void)
{

	return;
}

/*
 * this is an example of how the hooks would be coded in the kernel or a
 * kernel module.
 */
int testhook(void)
{
	printk("testhook entered\n");
	
	HOOK_RET(TEST_HOOK1);
	HOOK(TEST_HOOK2);

	HOOK(TEST_HOOK3);
	HOOK(TEST_HOOK4, 4);
	HOOK(TEST_HOOK5, 5, 0);
	EXCLUSIVE_HOOK(EX_TEST_HOOK3);
	EXCLUSIVE_HOOK(EX_TEST_HOOK4, 4);
	EXCLUSIVE_HOOK(EX_TEST_HOOK5, "%s, %d", "test_hook3", 1);
	printk("testhook exited\n");
	return 0;
}

EXPORT_SYMBOL_NOVERS(testhook);
MODULE_LICENSE("GPL");
