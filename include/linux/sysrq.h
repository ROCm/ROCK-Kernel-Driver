/* -*- linux-c -*-
 *
 *	$Id: sysrq.h,v 1.3 1997/07/17 11:54:33 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	(c) 2000 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 *	overhauled to use key registration
 *	based upon discusions in irc://irc.openprojects.net/#kernelnewbies
 */

#include <linux/config.h>

struct pt_regs;
struct tty_struct;

struct sysrq_key_op {
	void (*handler)(int, struct pt_regs *, struct tty_struct *);
	char *help_msg;
	char *action_msg;
};

#ifdef CONFIG_MAGIC_SYSRQ

/* Generic SysRq interface -- you may call it from any device driver, supplying
 * ASCII code of the key, pointer to registers and kbd/tty structs (if they
 * are available -- else NULL's).
 */

void handle_sysrq(int, struct pt_regs *, struct tty_struct *);

/* 
 * Nonlocking version of handle sysrq, used by sysrq handlers that need to
 * call sysrq handlers
 */

void __handle_sysrq_nolock(int, struct pt_regs *, struct tty_struct *);

/*
 * Sysrq registration manipulation functions
 */

void __sysrq_lock_table (void);
void __sysrq_unlock_table (void);
struct sysrq_key_op *__sysrq_get_key_op (int key);
void __sysrq_put_key_op (int key, struct sysrq_key_op *op_p);

extern __inline__ int
__sysrq_swap_key_ops_nolock(int key, struct sysrq_key_op *insert_op_p,
				struct sysrq_key_op *remove_op_p)
{
	int retval;
	if (__sysrq_get_key_op(key) == remove_op_p) {
		__sysrq_put_key_op(key, insert_op_p);
		retval = 0;
	} else {
                retval = -1;
	}
	return retval;
}

extern __inline__ int
__sysrq_swap_key_ops(int key, struct sysrq_key_op *insert_op_p,
				struct sysrq_key_op *remove_op_p) {
	int retval;
	__sysrq_lock_table();
	retval = __sysrq_swap_key_ops_nolock(key, insert_op_p, remove_op_p);
	__sysrq_unlock_table();
	return retval;
}
	
static inline int register_sysrq_key(int key, struct sysrq_key_op *op_p)
{
	return __sysrq_swap_key_ops(key, op_p, NULL);
}

static inline int unregister_sysrq_key(int key, struct sysrq_key_op *op_p)
{
	return __sysrq_swap_key_ops(key, NULL, op_p);
}

#else

static inline int __reterr(void)
{
	return -EINVAL;
}

#define register_sysrq_key(ig,nore) __reterr()
#define unregister_sysrq_key(ig,nore) __reterr()

#endif
