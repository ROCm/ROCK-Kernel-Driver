/*
 * Kernel Debugger Architecture Independent Main Code
 *
 * Copyright (C) 1999-2002 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/interrupt.h>

#include <asm/system.h>

#if defined(CONFIG_MODULES)
extern struct module *module_list;
#endif

	/*
	 * Kernel debugger state flags
	 */
volatile int kdb_flags =( 0 /*KDB_DEBUG_FLAG_BP */<< KDB_DEBUG_FLAG_SHIFT); 

	/*
	 * kdb_lock protects updates to kdb_initial_cpu.  Used to
	 * single thread processors through the kernel debugger.
	 */
spinlock_t kdb_lock = SPIN_LOCK_UNLOCKED;
volatile int kdb_initial_cpu = -1;		/* cpu number that owns kdb */

volatile int kdb_nextline = 1;
static volatile int kdb_new_cpu;		/* Which cpu to switch to */

volatile int kdb_state[NR_CPUS];		/* Per cpu state */

#ifdef	CONFIG_KDB_OFF
int kdb_on = 0;				/* Default is off */
#else
int kdb_on = 1;				/* Default is on */
#endif	/* CONFIG_KDB_OFF */

const char *kdb_diemsg;

#ifdef KDB_HAVE_LONGJMP
	/*
	 * Must have a setjmp buffer per CPU.  Switching cpus will
	 * cause the jump buffer to be setup for the new cpu, and
	 * subsequent switches (and pager aborts) will use the
	 * appropriate per-processor values.
	 */
kdb_jmp_buf	kdbjmpbuf[NR_CPUS];
#endif	/* KDB_HAVE_LONGJMP */

extern int kdba_setjmp(kdb_jmp_buf*);

	/*
	 * kdb_commands describes the available commands.
	 */
static kdbtab_t *kdb_commands;
static int kdb_max_commands;

typedef struct _kdbmsg {
	int	km_diag;	/* kdb diagnostic */
	char	*km_msg;	/* Corresponding message text */
} kdbmsg_t;

#define KDBMSG(msgnum, text) \
	{ KDB_##msgnum, text }

static kdbmsg_t kdbmsgs[] = {
	KDBMSG(NOTFOUND,"Command Not Found"),
	KDBMSG(ARGCOUNT, "Improper argument count, see usage."),
	KDBMSG(BADWIDTH, "Illegal value for BYTESPERWORD use 1, 2, 4 or 8, 8 is only allowed on 64 bit systems"),
	KDBMSG(BADRADIX, "Illegal value for RADIX use 8, 10 or 16"),
	KDBMSG(NOTENV, "Cannot find environment variable"),
	KDBMSG(NOENVVALUE, "Environment variable should have value"),
	KDBMSG(NOTIMP, "Command not implemented"),
	KDBMSG(ENVFULL, "Environment full"),
	KDBMSG(ENVBUFFULL, "Environment buffer full"),
	KDBMSG(TOOMANYBPT, "Too many breakpoints defined"),
	KDBMSG(TOOMANYDBREGS, "More breakpoints than db registers defined"),
	KDBMSG(DUPBPT, "Duplicate breakpoint address"),
	KDBMSG(BPTNOTFOUND, "Breakpoint not found"),
	KDBMSG(BADMODE, "Invalid IDMODE"),
	KDBMSG(BADINT, "Illegal numeric value"),
	KDBMSG(INVADDRFMT, "Invalid symbolic address format"),
	KDBMSG(BADREG, "Invalid register name"),
	KDBMSG(BADCPUNUM, "Invalid cpu number"),
	KDBMSG(BADLENGTH, "Invalid length field"),
	KDBMSG(NOBP, "No Breakpoint exists"),
	KDBMSG(BADADDR, "Invalid address"),
};
#undef KDBMSG

static const int __nkdb_err = sizeof(kdbmsgs) / sizeof(kdbmsg_t);


/*
 * Initial environment.   This is all kept static and local to
 * this file.   We don't want to rely on the memory allocation
 * mechanisms in the kernel, so we use a very limited allocate-only
 * heap for new and altered environment variables.  The entire
 * environment is limited to a fixed number of entries (add more
 * to __env[] if required) and a fixed amount of heap (add more to
 * KDB_ENVBUFSIZE if required).
 */

static char *__env[] = {
#if defined(CONFIG_SMP)
 "PROMPT=[%d]kdb> ",
 "MOREPROMPT=[%d]more> ",
#else
 "PROMPT=kdb> ",
 "MOREPROMPT=more> ",
#endif
 "RADIX=16",
 "LINES=25",
 "COLUMNS=80",
 "MDCOUNT=8",			/* lines of md output */
 "BTARGS=5",			/* 5 possible args in bt */
 "RECURSE=1",
 KDB_PLATFORM_ENV,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
};

static const int __nenv = (sizeof(__env) / sizeof(char *));

/*
 * kdbgetenv
 *
 *	This function will return the character string value of
 *	an environment variable.
 *
 * Parameters:
 *	match	A character string representing an environment variable.
 * Outputs:
 *	None.
 * Returns:
 *	NULL	No environment variable matches 'match'
 *	char*	Pointer to string value of environment variable.
 * Locking:
 *	No locking considerations required.
 * Remarks:
 */
char *
kdbgetenv(const char *match)
{
	char **ep = __env;
	int    matchlen = strlen(match);
	int i;

	for(i=0; i<__nenv; i++) {
		char *e = *ep++;

		if (!e) continue;

		if ((strncmp(match, e, matchlen) == 0)
		 && ((e[matchlen] == '\0')
		   ||(e[matchlen] == '='))) {
			char *cp = strchr(e, '=');
			return (cp)?++cp:"";
		}
	}
	return (char *)0;
}

/*
 * kdballocenv
 *
 *	This function is used to allocate bytes for environment entries.
 *
 * Parameters:
 *	match	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long represntation of the env variable 'match'
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 * Locking:
 *	No locking considerations required.  Must be called with all
 *	processors halted.
 * Remarks:
 *	We use a static environment buffer (envbuffer) to hold the values
 *	of dynamically generated environment variables (see kdb_set).  Buffer
 *	space once allocated is never free'd, so over time, the amount of space
 *	(currently 512 bytes) will be exhausted if env variables are changed
 *	frequently.
 */
static char *
kdballocenv(size_t bytes)
{
#define	KDB_ENVBUFSIZE	512
	static char envbuffer[KDB_ENVBUFSIZE];
	static int  envbufsize;
	char *ep = (char *)0;

	if ((KDB_ENVBUFSIZE - envbufsize) >= bytes) {
		ep = &envbuffer[envbufsize];
		envbufsize += bytes;
	}
	return ep;
}

/*
 * kdbgetulenv
 *
 *	This function will return the value of an unsigned long-valued
 *	environment variable.
 *
 * Parameters:
 *	match	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long represntation of the env variable 'match'
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 * Locking:
 *	No locking considerations required.
 * Remarks:
 */

int
kdbgetulenv(const char *match, unsigned long *value)
{
	char *ep;

	ep = kdbgetenv(match);
	if (!ep) return KDB_NOTENV;
	if (strlen(ep) == 0) return KDB_NOENVVALUE;

	*value = simple_strtoul(ep, 0, 0);

	return 0;
}

/*
 * kdbgetintenv
 *
 *	This function will return the value of an integer-valued
 *	environment variable.
 *
 * Parameters:
 *	match	A character string representing an integer-valued env variable
 * Outputs:
 *	*value  the integer representation of the environment variable 'match'
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 * Locking:
 *	No locking considerations required.
 * Remarks:
 */

int
kdbgetintenv(const char *match, int *value) {
	unsigned long val;
	int           diag;

	diag = kdbgetulenv(match, &val);
	if (!diag) {
		*value = (int) val;
	}
	return diag;
}

/*
 * kdbgetularg
 *
 *	This function will convert a numeric string
 *	into an unsigned long value.
 *
 * Parameters:
 *	arg	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long represntation of arg.
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 * Locking:
 *	No locking considerations required.
 * Remarks:
 */

int
kdbgetularg(const char *arg, unsigned long *value)
{
	char *endp;
	unsigned long val;

	val = simple_strtoul(arg, &endp, 0);

	if (endp == arg) {
		/*
		 * Try base 16, for us folks too lazy to type the
		 * leading 0x...
		 */
		val = simple_strtoul(arg, &endp, 16);
		if (endp == arg)
			return KDB_BADINT;
	}

	*value = val;

	return 0;
}

/*
 * kdbgetaddrarg
 *
 *	This function is responsible for parsing an
 *	address-expression and returning the value of
 *	the expression, symbol name, and offset to the caller.
 *
 *	The argument may consist of a numeric value (decimal or
 *	hexidecimal), a symbol name, a register name (preceeded
 *	by the percent sign), an environment variable with a numeric
 *	value (preceeded by a dollar sign) or a simple arithmetic
 *	expression consisting of a symbol name, +/-, and a numeric
 *	constant value (offset).
 *
 * Parameters:
 *	argc	- count of arguments in argv
 *	argv	- argument vector
 *	*nextarg - index to next unparsed argument in argv[]
 *	regs	- Register state at time of KDB entry
 * Outputs:
 *	*value	- receives the value of the address-expression
 *	*offset - receives the offset specified, if any
 *	*name   - receives the symbol name, if any
 *	*nextarg - index to next unparsed argument in argv[]
 *
 * Returns:
 *	zero is returned on success, a kdb diagnostic code is
 *      returned on error.
 *
 * Locking:
 *	No locking requirements.
 *
 * Remarks:
 *
 */

int
kdbgetaddrarg(int argc, const char **argv, int *nextarg,
	      kdb_machreg_t *value,  long *offset,
	      char **name, struct pt_regs *regs)
{
	kdb_machreg_t addr;
	long	      off = 0;
	int	      positive;
	int	      diag;
	int	      found = 0;
	char	     *symname;
	char	      symbol = '\0';
	char	     *cp;
	kdb_symtab_t   symtab;

	/*
	 * Process arguments which follow the following syntax:
	 *
	 *  symbol | numeric-address [+/- numeric-offset]
	 *  %register
	 *  $environment-variable
	 */

	if (*nextarg > argc) {
		return KDB_ARGCOUNT;
	}

	symname = (char *)argv[*nextarg];

	/*
	 * If there is no whitespace between the symbol
	 * or address and the '+' or '-' symbols, we
	 * remember the character and replace it with a
	 * null so the symbol/value can be properly parsed
	 */
	if ((cp = strpbrk(symname, "+-")) != NULL) {
		symbol = *cp;
		*cp++ = '\0';
	}

	if (symname[0] == '$') {
		diag = kdbgetulenv(&symname[1], &addr);
		if (diag)
			return diag;
	} else if (symname[0] == '%') {
		diag = kdba_getregcontents(&symname[1], regs, &addr);
		if (diag)
			return diag;
	} else {
		found = kdbgetsymval(symname, &symtab);
		if (found) {
			addr = symtab.sym_start;
		} else {
			diag = kdbgetularg(argv[*nextarg], &addr);
			if (diag)
				return diag;
		}
	}

	if (!found)
		found = kdbnearsym(addr, &symtab);

	(*nextarg)++;

	if (name)
		*name = symname;
	if (value)
		*value = addr;
	if (offset && name && *name)
		*offset = addr - symtab.sym_start;

	if ((*nextarg > argc)
	 && (symbol == '\0'))
		return 0;

	/*
	 * check for +/- and offset
	 */

	if (symbol == '\0') {
		if ((argv[*nextarg][0] != '+')
		 && (argv[*nextarg][0] != '-')) {
			/*
			 * Not our argument.  Return.
			 */
			return 0;
		} else {
			positive = (argv[*nextarg][0] == '+');
			(*nextarg)++;
		}
	} else
		positive = (symbol == '+');

	/*
	 * Now there must be an offset!
	 */
	if ((*nextarg > argc)
	 && (symbol == '\0')) {
		return KDB_INVADDRFMT;
	}

	if (!symbol) {
		cp = (char *)argv[*nextarg];
		(*nextarg)++;
	}

	diag = kdbgetularg(cp, &off);
	if (diag)
		return diag;

	if (!positive)
		off = -off;

	if (offset)
		*offset += off;

	if (value)
		*value += off;

	return 0;
}

static void
kdb_cmderror(int diag)
{
	int i;

	if (diag >= 0) {
		kdb_printf("no error detected\n");
		return;
	}

	for(i=0; i<__nkdb_err; i++) {
		if (kdbmsgs[i].km_diag == diag) {
			kdb_printf("diag: %d: %s\n", diag, kdbmsgs[i].km_msg);
			return;
		}
	}

	kdb_printf("Unknown diag %d\n", -diag);
}

/*
 * kdb_defcmd, kdb_defcmd2
 *
 *	This function implements the 'defcmd' command which defines one
 *	command as a set of other commands, terminated by endefcmd.
 *	kdb_defcmd processes the initial 'defcmd' command, kdb_defcmd2
 *	is invoked from kdb_parse for the following commands until
 *	'endefcmd'.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

struct defcmd_set {
	int count;
	int usable;
	char *name;
	char *usage;
	char *help;
	char **command;
};
static struct defcmd_set *defcmd_set;
static int defcmd_set_count;
static int defcmd_in_progress;

/* Forward references */
int kdb_parse(const char *cmdstr, struct pt_regs *regs);
static int kdb_exec_defcmd(int argc, const char **argv, const char **envp, struct pt_regs *regs);

static int
kdb_defcmd2(const char *cmdstr, const char *argv0)
{
	struct defcmd_set *s = defcmd_set + defcmd_set_count - 1;
	char **save_command = s->command;
	if (strcmp(argv0, "endefcmd") == 0) {
		defcmd_in_progress = 0;
		if (!s->count)
			s->usable = 0;
		if (s->usable)
			kdb_register(s->name, kdb_exec_defcmd, s->usage, s->help, 0);
		return 0;
	}
	if (!s->usable)
		return KDB_NOTIMP;
	s->command = kmalloc((s->count + 1) * sizeof(*(s->command)), GFP_KERNEL);
	if (!s->command) {
		kdb_printf("Could not allocate new kdb_defcmd table for %s\n", cmdstr);
		s->usable = 0;
		return KDB_NOTIMP;
	}
	memcpy(s->command, save_command, s->count * sizeof(*(s->command)));
	s->command[s->count++] = kdb_strdup(cmdstr, GFP_KERNEL);
	kfree(save_command);
	return 0;
}

static int
kdb_defcmd(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	struct defcmd_set *save_defcmd_set = defcmd_set, *s;
	if (argc != 3)
		return KDB_ARGCOUNT;
	if (defcmd_in_progress) {
		kdb_printf("kdb: nested defcmd detected, assuming missing endefcmd\n");
		kdb_defcmd2("endefcmd", "endefcmd");
	}
	defcmd_set = kmalloc((defcmd_set_count + 1) * sizeof(*defcmd_set), GFP_KERNEL);
	if (!defcmd_set) {
		kdb_printf("Could not allocate new defcmd_set entry for %s\n", argv[1]);
		defcmd_set = save_defcmd_set;
		return KDB_NOTIMP;
	}
	memcpy(defcmd_set, save_defcmd_set, defcmd_set_count * sizeof(*defcmd_set));
	kfree(save_defcmd_set);
	s = defcmd_set + defcmd_set_count;
	memset(s, 0, sizeof(*s));
	s->usable = 1;
	s->name = kdb_strdup(argv[1], GFP_KERNEL);
	s->usage = kdb_strdup(argv[2], GFP_KERNEL);
	s->help = kdb_strdup(argv[3], GFP_KERNEL);
	if (s->usage[0] == '"') {
		strcpy(s->usage, s->usage+1);
		s->usage[strlen(s->usage)-1] = '\0';
	}
	if (s->help[0] == '"') {
		strcpy(s->help, s->help+1);
		s->help[strlen(s->help)-1] = '\0';
	}
	++defcmd_set_count;
	defcmd_in_progress = 1;
	return 0;
}

/*
 * kdb_exec_defcmd
 *
 *	Execute the set of commands associated with this defcmd name.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

static int
kdb_exec_defcmd(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int i, ret;
	struct defcmd_set *s;
	if (argc != 0)
		return KDB_ARGCOUNT;
	for (s = defcmd_set, i = 0; i < defcmd_set_count; ++i, ++s) {
		if (strcmp(s->name, argv[0]) == 0)
			break;
	}
	if (i == defcmd_set_count) {
		kdb_printf("kdb_exec_defcmd: could not find commands for %s\n", argv[0]);
		return KDB_NOTIMP;
	}
	for (i = 0; i < s->count; ++i) {
		/* Recursive use of kdb_parse, argv is now unreliable */
		argv = NULL;
		if ((ret = kdb_parse(s->command[i], regs)))
			return ret;
	}
	return 0;
}

/* The command history feature is not functional at the moment.  It
 * will be replaced by something that understands editting keys,
 * including left, right, insert, delete as well as up, down.
 * Keith Owens, November 18 2000
 */
#define KDB_CMD_HISTORY_COUNT	32
#define CMD_BUFLEN		200	/* kdb_printf: max printline size == 256 */
static unsigned int cmd_head, cmd_tail;
static unsigned int cmdptr;
static char cmd_hist[KDB_CMD_HISTORY_COUNT][CMD_BUFLEN];

/*
 * kdb_parse
 *
 *	Parse the command line, search the command table for a
 *	matching command and invoke the command function.
 *	This function may be called recursively, if it is, the second call
 *	will overwrite argv and cbuf.  It is the caller's responsibility to
 *	save their argv if they recursively call kdb_parse().
 *
 * Parameters:
 *      cmdstr	The input command line to be parsed.
 *	regs	The registers at the time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Locking:
 * 	None.
 * Remarks:
 *	Limited to 20 tokens.
 *
 *	Real rudimentary tokenization. Basically only whitespace
 *	is considered a token delimeter (but special consideration
 *	is taken of the '=' sign as used by the 'set' command).
 *
 *	The algorithm used to tokenize the input string relies on
 *	there being at least one whitespace (or otherwise useless)
 *	character between tokens as the character immediately following
 *	the token is altered in-place to a null-byte to terminate the
 *	token string.
 */

#define MAXARGC	20

int
kdb_parse(const char *cmdstr, struct pt_regs *regs)
{
	static char *argv[MAXARGC];
	static int  argc = 0;
	static char cbuf[CMD_BUFLEN+2];
	const char *cp;
	char *cpp, quoted = '\0';
	kdbtab_t *tp;
	int i, escaped = 0;;

	/*
	 * First tokenize the command string.
	 */
	cp = cmdstr;

	if (*cp != '\n' && *cp != '\0') {
		argc = 0;
		cpp = cbuf;
		while (*cp) {
			/* skip whitespace */
			while (isspace(*cp)) cp++;
			if ((*cp == '\0') || (*cp == '\n'))
				break;
			if (cpp >= cbuf + CMD_BUFLEN) {
				kdb_printf("kdb_parse: command buffer overflow, command ignored\n%s\n", cmdstr);
				return KDB_NOTFOUND;
			}
			if (argc >= MAXARGC - 1) {
				kdb_printf("kdb_parse: too many arguments, command ignored\n%s\n", cmdstr);
				return KDB_NOTFOUND;
			}
			argv[argc++] = cpp;
			/* Copy to next unquoted and unescaped whitespace or '=' */
			while (*cp && *cp != '\n' && (quoted || !isspace(*cp))) {
				if (cpp >= cbuf + CMD_BUFLEN)
					break;
				if (*cp == '\\') {
					escaped = 1;
					continue;
				} else if (escaped) {
					escaped = 0;
					*cpp++ = *cp++;
					continue;
				} else if (*cp == quoted) {
					quoted = '\0';
				} else if (*cp == '\'' || *cp == '"') {
					quoted = *cp;
				}
				if ((*cpp = *cp++) == '=' && !quoted)
					break;
				++cpp;
			}
			*cpp++ = '\0';	/* Squash a ws or '=' character */
		}
	}
	if (!argc)
		return 0;
	if (defcmd_in_progress)
		return kdb_defcmd2(cmdstr, argv[0]);

	for(tp=kdb_commands, i=0; i < kdb_max_commands; i++,tp++) {
		if (tp->cmd_name) {
			/*
			 * If this command is allowed to be abbreviated,
			 * check to see if this is it.
			 */

			if (tp->cmd_minlen
			 && (strlen(argv[0]) <= tp->cmd_minlen)) {
				if (strncmp(argv[0],
					    tp->cmd_name,
					    tp->cmd_minlen) == 0) {
					break;
				}
			}

			if (strcmp(argv[0], tp->cmd_name)==0) {
				break;
			}
		}
	}
	
	/*
	 * If we don't find a command by this name, see if the first
	 * few characters of this match any of the known commands.
	 * e.g., md1c20 should match md.
	 */
	if (i == kdb_max_commands) {
		for(tp=kdb_commands, i=0; i < kdb_max_commands; i++,tp++) {
			if (tp->cmd_name) {
				if (strncmp(argv[0],
					    tp->cmd_name,
					    strlen(tp->cmd_name))==0) {
					break;
				}
			}
		}
	}

	if (i < kdb_max_commands) {
		int result;
		KDB_STATE_SET(CMD);
		result = (*tp->cmd_func)(argc-1,
				       (const char**)argv,
				       (const char**)__env,
				       regs);
		KDB_STATE_CLEAR(CMD);
		switch (tp->cmd_repeat) {
		case KDB_REPEAT_NONE:
			argc = 0;
			if (argv[0])
				*(argv[0]) = '\0';
			break;
		case KDB_REPEAT_NO_ARGS:
			argc = 1;
			if (argv[1])
				*(argv[1]) = '\0';
			break;
		case KDB_REPEAT_WITH_ARGS:
			break;
		}
		return result;
	}

	/*
	 * If the input with which we were presented does not
	 * map to an existing command, attempt to parse it as an
	 * address argument and display the result.   Useful for
	 * obtaining the address of a variable, or the nearest symbol
	 * to an address contained in a register.
	 */
	{
		kdb_machreg_t value;
		char *name = NULL;
		long offset;
		int nextarg = 0;

		if (kdbgetaddrarg(0, (const char **)argv, &nextarg,
				  &value, &offset, &name, regs)) {
			return KDB_NOTFOUND;
		}

		if (argv[0]) {
			kdb_printf("%s = ", argv[0]);
			kdb_symbol_print(value, NULL, KDB_SP_DEFAULT);
			kdb_printf("\n");
		}
		return 0;
	}
}


static int
handle_ctrl_cmd(char *cmd)
{
#define CTRL_P	16
#define CTRL_N	14

	/* initial situation */
	if (cmd_head == cmd_tail) return 1;

	switch(*cmd) {
		case '\n':
		case CTRL_P:
			if (cmdptr != cmd_tail)
				cmdptr = (cmdptr-1) % KDB_CMD_HISTORY_COUNT;
			strcpy(cmd, cmd_hist[cmdptr]);
			return 0;	
		case CTRL_N:
			if (cmdptr != (cmd_head-1))
				cmdptr = (cmdptr+1) % KDB_CMD_HISTORY_COUNT;
			strcpy(cmd, cmd_hist[cmdptr]);
			return 0;
	}
	return 1;
}


/*
 * kdb_local
 *
 *	The main code for kdb.  This routine is invoked on a specific
 *	processor, it is not global.  The main kdb() routine ensures
 *	that only one processor at a time is in this routine.  This
 *	code is called with the real reason code on the first entry
 *	to a kdb session, thereafter it is called with reason SWITCH,
 *	even if the user goes back to the original cpu.
 *
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	regs		The exception frame at time of fault/breakpoint.  NULL
 *			for reason SILENT, otherwise valid.
 *	db_result	Result code from the break or debug point.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 *	KDB_CMD_GO	User typed 'go'.
 *	KDB_CMD_CPU	User switched to another cpu.
 *	KDB_CMD_SS	Single step.
 *	KDB_CMD_SSB	Single step until branch.
 * Locking:
 *	none
 * Remarks:
 *	none
 */

static int
kdb_local(kdb_reason_t reason, int error, struct pt_regs *regs, kdb_dbtrap_t db_result)
{
	char		*cmdbuf;
	int		diag;

	if (reason != KDB_REASON_DEBUG &&
	    reason != KDB_REASON_SILENT) {
		kdb_printf("\nEntering kdb (current=0x%p, pid %d) ", (void *)current, current->pid);
#if defined(CONFIG_SMP)
		kdb_printf("on processor %d ", smp_processor_id());
#endif
	}

	switch (reason) {
	case KDB_REASON_DEBUG:
	{
		/*
		 * If re-entering kdb after a single step
		 * command, don't print the message.
		 */
		switch(db_result) {
		case KDB_DB_BPT:
			kdb_printf("\nEntering kdb (0x%p) ", (void *)current);
#if defined(CONFIG_SMP)
			kdb_printf("on processor %d ", smp_processor_id());
#endif
			kdb_printf("due to Debug @ " kdb_machreg_fmt "\n", kdba_getpc(regs));
			break;
		case KDB_DB_SSB:
			/*
			 * In the midst of ssb command. Just return.
			 */
			return KDB_CMD_SSB;	/* Continue with SSB command */

			break;
		case KDB_DB_SS:
			break;
		case KDB_DB_SSBPT:
			return 1;	/* kdba_db_trap did the work */
		default:
			kdb_printf("kdb: Bad result from kdba_db_trap: %d\n",
				   db_result);
			break;
		}

	}
		break;
	case KDB_REASON_FAULT:
		break;
	case KDB_REASON_ENTER:
		kdb_printf("due to KDB_ENTER()\n");
		break;
	case KDB_REASON_KEYBOARD:
		kdb_printf("due to Keyboard Entry\n");
		break;
	case KDB_REASON_SWITCH:
		kdb_printf("due to cpu switch\n");
		break;
	case KDB_REASON_CALL:	
		if (!regs)
			kdb_printf("kdb() called with no registers, restricted function\n");
		break;
	case KDB_REASON_OOPS:
		kdb_printf("Oops: %s\n", kdb_diemsg);
		kdb_printf("due to oops @ " kdb_machreg_fmt "\n", kdba_getpc(regs));
		kdba_dumpregs(regs, NULL, NULL);
		break;
	case KDB_REASON_NMI:
		kdb_printf("due to NonMaskable Interrupt @ " kdb_machreg_fmt "\n",
			  kdba_getpc(regs));
		kdba_dumpregs(regs, NULL, NULL);
		break;
	case KDB_REASON_WATCHDOG:
		kdb_printf("due to WatchDog Interrupt @ " kdb_machreg_fmt "\n",
			  kdba_getpc(regs));
		kdba_dumpregs(regs, NULL, NULL);
		break;
	case KDB_REASON_BREAK:
		kdb_printf("due to Breakpoint @ " kdb_machreg_fmt "\n", kdba_getpc(regs));
		/*
		 * Determine if this breakpoint is one that we
		 * are interested in.
		 */
		if (db_result != KDB_DB_BPT) {
			kdb_printf("kdb: error return from kdba_bp_trap: %d\n", db_result);
			return 0;	/* Not for us, dismiss it */
		}
		break;
	case KDB_REASON_RECURSE:
		kdb_printf("due to Recursion @ " kdb_machreg_fmt "\n", kdba_getpc(regs));
		break;
	case KDB_REASON_SILENT:
		return KDB_CMD_GO;	/* Silent entry, silent exit */
		break;
	default:
		kdb_printf("kdb: unexpected reason code: %d\n", reason);
		return 0;	/* Not for us, dismiss it */
	}

	while (1) {
		/*
		 * Initialize pager context.
		 */
		kdb_nextline = 1;
		KDB_STATE_CLEAR(SUPPRESS);
#ifdef KDB_HAVE_LONGJMP
		/*
		 * Use kdba_setjmp/kdba_longjmp to break out of
		 * the pager early and to attempt to recover from kdb errors.
		 */
		KDB_STATE_CLEAR(LONGJMP);
		if (kdba_setjmp(&kdbjmpbuf[smp_processor_id()])) {
			/* Command aborted (usually in pager) */
			continue;
		}
		else
			KDB_STATE_SET(LONGJMP);
#endif	/* KDB_HAVE_LONGJMP */

do_full_getstr:
#if defined(CONFIG_SMP)
		kdb_printf(kdbgetenv("PROMPT"), smp_processor_id());
#else
		kdb_printf(kdbgetenv("PROMPT"));
#endif

		cmdbuf = cmd_hist[cmd_head];
		*cmdbuf = '\0';
		/*
		 * Fetch command from keyboard
		 */
		cmdbuf = kdb_getstr(cmdbuf, CMD_BUFLEN, defcmd_in_progress ? "[defcmd]" : "");
		if (*cmdbuf < 32 && *cmdbuf != '\n')
			if (handle_ctrl_cmd(cmdbuf))
				goto do_full_getstr;

		if (*cmdbuf != '\n') {
			cmd_head = (cmd_head+1) % KDB_CMD_HISTORY_COUNT;
			if (cmd_head == cmd_tail) cmd_tail = (cmd_tail+1) % KDB_CMD_HISTORY_COUNT;

		}

		cmdptr = cmd_head;
		diag = kdb_parse(cmdbuf, regs);
		if (diag == KDB_NOTFOUND) {
			kdb_printf("Unknown kdb command: '%s'\n", cmdbuf);
			diag = 0;
		}
		if (diag == KDB_CMD_GO
		 || diag == KDB_CMD_CPU
		 || diag == KDB_CMD_SS
		 || diag == KDB_CMD_SSB)
			break;

		if (diag)
			kdb_cmderror(diag);
	}

	return(diag);
}


/*
 * kdb_print_state
 *
 *	Print the state data for the current processor for debugging.
 *
 * Inputs:
 *	text		Identifies the debug point
 *	value		Any integer value to be printed, e.g. reason code.
 * Returns:
 *	None.
 * Locking:
 *	none
 * Remarks:
 *	none
 */

void kdb_print_state(const char *text, int value)
{
	kdb_printf("state: %s cpu %d value %d initial %d state %x\n",
		text, smp_processor_id(), value, kdb_initial_cpu, kdb_state[smp_processor_id()]);
}

/*
 * kdb_previous_event
 *
 *	Return a count of cpus that are leaving kdb, i.e. the number
 *	of processors that are still handling the previous kdb event.
 *
 * Inputs:
 *	None.
 * Returns:
 *	Count of cpus in previous event.
 * Locking:
 *	none
 * Remarks:
 *	none
 */

static int
kdb_previous_event(void)
{
	int i, leaving = 0;
	for (i = 0; i < NR_CPUS; ++i) {
		if (KDB_STATE_CPU(LEAVING, i))
			++leaving;
	}
	return(leaving);
}

/*
 * kdb_main_loop
 *
 * The main kdb loop.  After initial setup and assignment of the controlling
 * cpu, all cpus are in this loop.  One cpu is in control and will issue the kdb
 * prompt, the others will spin until 'go' or cpu switch.
 *
 * To get a consistent view of the kernel stacks for all processes, this routine
 * is invoked from the main kdb code via an architecture specific routine.
 * kdba_main_loop is responsible for making the kernel stacks consistent for all
 * processes, there should be no difference between a blocked process and a
 * running process as far as kdb is concerned.
 *
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	reason2		kdb's current reason code.  Initially error but can change
 *			acording to kdb state.
 *	db_result	Result code from break or debug point.
 *	regs		The exception frame at time of fault/breakpoint.  If reason
 *			is KDB_REASON_SILENT then regs is NULL, otherwise it
 *			should always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Locking:
 *	none
 * Remarks:
 *	none
 */

int
kdb_main_loop(kdb_reason_t reason, kdb_reason_t reason2, int error,
	      kdb_dbtrap_t db_result, struct pt_regs *regs)
{
	int result = 1;
	/* Stay in kdb() until 'go', 'ss[b]' or an error */
	while (1) {
		int i;
		/*
		 * All processors except the one that is in control
		 * will spin here.
		 */
		KDB_DEBUG_STATE("kdb_main_loop 1", reason);
		while (KDB_STATE(HOLD_CPU))
			;
		KDB_STATE_CLEAR(SUPPRESS);
		KDB_DEBUG_STATE("kdb_main_loop 2", reason);
		if (KDB_STATE(LEAVING))
			break;	/* Another cpu said 'go' */

		/* Still using kdb, this processor is in control */
		result = kdb_local(reason2, error, regs, db_result);
		KDB_DEBUG_STATE("kdb_main_loop 3", result);

		if (result == KDB_CMD_CPU) {
			/* Cpu switch, hold the current cpu, release the target one. */
			reason2 = KDB_REASON_SWITCH;
			KDB_STATE_SET(HOLD_CPU);
			KDB_STATE_CLEAR_CPU(HOLD_CPU, kdb_new_cpu);
			continue;
		}

		if (result == KDB_CMD_SS) {
			KDB_STATE_SET(DOING_SS);
			break;
		}

		if (result == KDB_CMD_SSB) {
			KDB_STATE_SET(DOING_SS);
			KDB_STATE_SET(DOING_SSB);
			break;
		}

		if (result && result != 1 && result != KDB_CMD_GO)
			kdb_printf("\nUnexpected kdb_local return code %d\n", result);

		/*
		 * All other return codes (including KDB_CMD_GO) from
		 * kdb_local will end kdb().  Release all other cpus
		 * which will see KDB_STATE(LEAVING) is set.
		 */
		for (i = 0; i < NR_CPUS; ++i) {
			if (KDB_STATE_CPU(KDB, i))
				KDB_STATE_SET_CPU(LEAVING, i);
			KDB_STATE_CLEAR_CPU(WAIT_IPI, i);
			KDB_STATE_CLEAR_CPU(HOLD_CPU, i);
		}
		KDB_DEBUG_STATE("kdb_main_loop 4", reason);
		break;
	}
	return(result != 0);
}

/*
 * kdb
 *
 * 	This function is the entry point for the kernel debugger.  It
 *	provides a command parser and associated support functions to
 *	allow examination and control of an active kernel.
 *
 * 	This function may be invoked directly from any
 *	point in the kernel by calling with reason == KDB_REASON_CALL
 *	(XXX - note that the regs aren't set up this way - could
 *	       use a software interrupt to enter kdb to get regs...)
 *
 *	The breakpoint trap code should invoke this function with
 *	one of KDB_REASON_BREAK (int 03) or KDB_REASON_DEBUG (debug register)
 *
 *	the die_if_kernel function should invoke this function with
 *	KDB_REASON_OOPS.
 *
 *	The kernel fault handler should invoke this function with
 *	reason == KDB_REASON_FAULT and error == trap vector #.
 *
 *	In single step mode, one cpu is released to run without
 *	breakpoints.   Interrupts and NMI are reset to their original values,
 *	the cpu is allowed to do one instruction which causes a trap
 *	into kdb with KDB_REASON_DEBUG.
 *
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	regs		The exception frame at time of fault/breakpoint.  If reason
 *			is KDB_REASON_SILENT then regs is NULL, otherwise it
 *			should always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Locking:
 *	none
 * Remarks:
 *	No assumptions of system state.  This function may be invoked
 *	with arbitrary locks held.  It will stop all other processors
 *	in an SMP environment, disable all interrupts and does not use
 *	the operating systems keyboard driver.
 *
 *	This code is reentrant but only for cpu switch.  Any other
 *	reentrancy is an error, although kdb will attempt to recover.
 *
 *	At the start of a kdb session the initial processor is running
 *	kdb() and the other processors can be doing anything.  When the
 *	initial processor calls smp_kdb_stop() the other processors are
 *	driven through kdb_ipi which calls kdb() with reason SWITCH.
 *	That brings all processors into this routine, one with a "real"
 *	reason code, the other with SWITCH.
 *
 *	Because the other processors are driven via smp_kdb_stop(),
 *	they enter here from the NMI handler.  Until the other
 *	processors exit from here and exit from kdb_ipi, they will not
 *	take any more NMI requests.  The initial cpu will still take NMI.
 *
 *	Multiple race and reentrancy conditions, each with different
 *	advoidance mechanisms.
 *
 *	Two cpus hit debug points at the same time.
 *
 *	  kdb_lock and kdb_initial_cpu ensure that only one cpu gets
 *	  control of kdb.  The others spin on kdb_initial_cpu until
 *	  they are driven through NMI into kdb_ipi.  When the initial
 *	  cpu releases the others from NMI, they resume trying to get
 *	  kdb_initial_cpu to start a new event.
 *
 *	A cpu is released from kdb and starts a new event before the
 *	original event has completely ended.
 *
 *	  kdb_previous_event() prevents any cpu from entering
 *	  kdb_initial_cpu state until the previous event has completely
 *	  ended on all cpus.
 *
 *      An exception occurs inside kdb.
 *
 *	  kdb_initial_cpu detects recursive entry to kdb and attempts
 *	  to recover.  The recovery uses longjmp() which means that
 *	  recursive calls to kdb never return.  Beware of assumptions
 *	  like
 *
 *          ++depth;
 *          kdb();
 *          --depth;
 *
 *        If the kdb call is recursive then longjmp takes over and
 *        --depth is never executed.
 *
 *      NMI handling.
 *
 *	  NMI handling is tricky.  The initial cpu is invoked by some kdb event,
 *	  this event could be NMI driven but usually is not.  The other cpus are
 *	  driven into kdb() via kdb_ipi which uses NMI so at the start the other
 *	  cpus will not accept NMI.  Some operations such as SS release one cpu
 *	  but hold all the others.  Releasing a cpu means it drops back to
 *	  whatever it was doing before the kdb event, this means it drops out of
 *	  kdb_ipi and hence out of NMI status.  But the software watchdog uses
 *	  NMI and we do not want spurious watchdog calls into kdb.  kdba_read()
 *	  resets the watchdog counters in its input polling loop, when a kdb
 *	  command is running it is subject to NMI watchdog events.
 *
 *	  Another problem with NMI handling is the NMI used to drive the other
 *	  cpus into kdb cannot be distinguished from the watchdog NMI.  State
 *	  flag WAIT_IPI indicates that a cpu is waiting for NMI via kdb_ipi,
 *	  if not set then software NMI is ignored by kdb_ipi.
 *
 *      Cpu switching.
 *
 *        All cpus are in kdb (or they should be), all but one are
 *        spinning on KDB_STATE(HOLD_CPU).  Only one cpu is not in
 *        HOLD_CPU state, only that cpu can handle commands.
 *
 */

int
kdb(kdb_reason_t reason, int error, struct pt_regs *regs)
{
	kdb_intstate_t	int_state;	/* Interrupt state */
	kdb_reason_t	reason2 = reason;
	int		result = 1;	/* Default is kdb handled it */
	int		ss_event;
	kdb_dbtrap_t 	db_result=KDB_DB_NOBPT;
	unsigned long flags; /* local irq save/restore flags */
	if (!kdb_on)
		return 0;

	KDB_DEBUG_STATE("kdb 1", reason);
	KDB_STATE_CLEAR(SUPPRESS);

	/* Filter out userspace breakpoints first, no point in doing all
	 * the kdb smp fiddling when it is really a gdb trap.
	 * Save the single step status first, kdba_db_trap clears ss status.
	 */
	ss_event = KDB_STATE(DOING_SS) || KDB_STATE(SSBPT);
	if (reason == KDB_REASON_BREAK)
		db_result = kdba_bp_trap(regs, error);	/* Only call this once */
	if (reason == KDB_REASON_DEBUG)
		db_result = kdba_db_trap(regs, error);	/* Only call this once */

	if ((reason == KDB_REASON_BREAK || reason == KDB_REASON_DEBUG)
	 && db_result == KDB_DB_NOBPT) {
		KDB_DEBUG_STATE("kdb 2", reason);
		return 0;	/* Not one of mine */
	}

	/* Turn off single step if it was being used */
	if (ss_event) {
		kdba_clearsinglestep(regs);
		/* Single step after a breakpoint removes the need for a delayed reinstall */
		if (reason == KDB_REASON_BREAK || reason == KDB_REASON_DEBUG) {
			KDB_STATE_SET(NO_BP_DELAY);
		}
	}

	/* kdb can validly reenter but only for certain well defined conditions */
	if (reason == KDB_REASON_DEBUG
	 && !KDB_STATE(HOLD_CPU)
	 && ss_event)
		KDB_STATE_SET(REENTRY);
	else
		KDB_STATE_CLEAR(REENTRY);

	/* Wait for previous kdb event to completely exit before starting
	 * a new event.
	 */
	while (kdb_previous_event())
		;
	KDB_DEBUG_STATE("kdb 3", reason);

	/*
	 * If kdb is already active, print a message and try to recover.
	 * If recovery is not possible and recursion is allowed or
	 * forced recursion without recovery is set then try to recurse
	 * in kdb.  Not guaranteed to work but it makes an attempt at
	 * debugging the debugger.
	 */
	if (reason != KDB_REASON_SWITCH) {
		if (KDB_IS_RUNNING() && !KDB_STATE(REENTRY)) {
			int recover = 1;
			unsigned long recurse = 0;
			kdb_printf("kdb: Debugger re-entered on cpu %d, new reason = %d\n",
				smp_processor_id(), reason);
			/* Should only re-enter from released cpu */

#ifdef BRINGUP
			/* BRINGUP - temp fix for PV 816228 - 
			 * When we send an NMI to a hung cpu, it frequently gets into
			 * an endless loop printing the following messages. The following
			 * line is a temp fix until keith can better understand the problem.
			 */
			while(1);
#endif

			if (KDB_STATE(HOLD_CPU)) {
				kdb_printf("     Strange, cpu %d should not be running\n", smp_processor_id());
				recover = 0;
			}
			if (!KDB_STATE(CMD)) {
				kdb_printf("     Not executing a kdb command\n");
				recover = 0;
			}
			if (!KDB_STATE(LONGJMP)) {
				kdb_printf("     No longjmp available for recovery\n");
				recover = 0;
			}
			kdbgetulenv("RECURSE", &recurse);
			if (recurse > 1) {
				kdb_printf("     Forced recursion is set\n");
				recover = 0;
			}
			if (recover) {
				kdb_printf("     Attempting to abort command and recover\n");
#ifdef KDB_HAVE_LONGJMP
				kdba_longjmp(&kdbjmpbuf[smp_processor_id()], 0);
#endif
			}
			if (recurse) {
				if (KDB_STATE(RECURSE)) {
					kdb_printf("     Already in recursive mode\n");
				} else {
					kdb_printf("     Attempting recursive mode\n");
					KDB_STATE_SET(RECURSE);
					KDB_STATE_SET(REENTRY);
					reason2 = KDB_REASON_RECURSE;
					recover = 1;
				}
			}
			if (!recover) {
				kdb_printf("     Cannot recover, allowing event to proceed\n");
				return(0);
			}
		}
	} else if (!KDB_IS_RUNNING()) {
		kdb_printf("kdb: CPU switch without kdb running, I'm confused\n");
		return(0);
	}

	/*
	 * Disable interrupts, breakpoints etc. on this processor
	 * during kdb command processing
	 */
	KDB_STATE_SET(KDB);
	kdba_disableint(&int_state);
	if (!ss_event) {
		/* bh not re-enabled during single step */
		local_bh_disable();
	}
	if (!KDB_STATE(KDB_CONTROL)) {
		kdb_bp_remove_local();
		kdba_disable_lbr();
		KDB_STATE_SET(KDB_CONTROL);
	}
	else if (KDB_DEBUG(LBR))
		kdba_print_lbr();

	/*
	 * If not entering the debugger due to CPU switch or single step
	 * reentry, serialize access here.
	 * The processors may race getting to this point - if,
	 * for example, more than one processor hits a breakpoint
	 * at the same time.   We'll serialize access to kdb here -
	 * other processors will loop here, and the NMI from the stop
	 * IPI will take them into kdb as switch candidates.  Once
	 * the initial processor releases the debugger, the rest of
	 * the processors will race for it.
	 */
	if (reason == KDB_REASON_SWITCH 
	 || KDB_STATE(REENTRY))
		;	/* drop through */
	else {
		KDB_DEBUG_STATE("kdb 4", reason);
		spin_lock(&kdb_lock);

		while (KDB_IS_RUNNING() || kdb_previous_event()) {
			spin_unlock(&kdb_lock);

			while (KDB_IS_RUNNING() || kdb_previous_event())
				;

			spin_lock(&kdb_lock);
		}
		KDB_DEBUG_STATE("kdb 5", reason);

		kdb_initial_cpu = smp_processor_id();
		spin_unlock(&kdb_lock);
	}

	if (smp_processor_id() == kdb_initial_cpu
	 && !KDB_STATE(REENTRY)) {
		KDB_STATE_CLEAR(HOLD_CPU);
		KDB_STATE_CLEAR(WAIT_IPI);
		/*
		 * Remove the global breakpoints.  This is only done
		 * once from the initial processor on initial entry.
		 */
		kdb_bp_remove_global();

		/*
		 * If SMP, stop other processors.  The other processors
		 * will enter kdb() with KDB_REASON_SWITCH and spin
		 * below.
		 */
		KDB_DEBUG_STATE("kdb 6", reason);
		if (NR_CPUS > 1) {
			int i;
			for (i = 0; i < NR_CPUS; ++i) {
				if (!cpu_online(i))
					continue;
				if (i != kdb_initial_cpu) {
					KDB_STATE_SET_CPU(HOLD_CPU, i);
					KDB_STATE_SET_CPU(WAIT_IPI, i);
				}
			}
			KDB_DEBUG_STATE("kdb 7", reason);
			smp_kdb_stop();
			KDB_DEBUG_STATE("kdb 8", reason);
		}
	}

	/* Set up a consistent set of process stacks before talking to the user */
	KDB_DEBUG_STATE("kdb 9", result);
	result = kdba_main_loop(reason, reason2, error, db_result, regs);

	KDB_DEBUG_STATE("kdb 10", result);
	kdba_adjust_ip(reason, error, regs);
	KDB_STATE_CLEAR(LONGJMP);
	KDB_DEBUG_STATE("kdb 11", result);

	/* No breakpoints installed for SS */
	if (!KDB_STATE(DOING_SS) &&
	    !KDB_STATE(SSBPT) &&
	    !KDB_STATE(RECURSE)) {
		KDB_DEBUG_STATE("kdb 12", result);
		kdba_enable_lbr();
		kdb_bp_install_local(regs);
		local_irq_save(flags); 
		local_irq_enable();
		local_bh_enable();  /* wms ..  badness message during boot */
		local_irq_restore(flags); 
		KDB_STATE_CLEAR(NO_BP_DELAY);
		KDB_STATE_CLEAR(KDB_CONTROL);
	}

	KDB_DEBUG_STATE("kdb 13", result);
	kdba_restoreint(&int_state);

	KDB_STATE_CLEAR(KDB);		/* Main kdb state has been cleared */
	KDB_STATE_CLEAR(LEAVING);	/* Elvis has left the building ... */
	KDB_DEBUG_STATE("kdb 14", result);

	if (smp_processor_id() == kdb_initial_cpu &&
	  !KDB_STATE(DOING_SS) &&
	  !KDB_STATE(RECURSE)) {
		/*
		 * (Re)install the global breakpoints.  This is only done
		 * once from the initial processor on final exit.
		 */
		KDB_DEBUG_STATE("kdb 15", reason);
		kdb_bp_install_global(regs);
		/* Wait until all the other processors leave kdb */
		while (kdb_previous_event())
			;
		kdb_initial_cpu = -1;	/* release kdb control */
		KDB_DEBUG_STATE("kdb 16", reason);
	}

	KDB_STATE_CLEAR(RECURSE);
	KDB_DEBUG_STATE("kdb 17", reason);
	return(result != 0);
}

/*
 * kdb_mdr
 *
 *	This function implements the guts of the 'mdr' command.
 *
 *	mdr  <addr arg>,<byte count>
 *
 * Inputs:
 *	addr	Start address
 *	count	Number of bytes
 * Outputs:
 *	None.
 * Returns:
 *	Always 0.  Any errors are detected and printed by kdb_getarea.
 * Locking:
 *	none.
 * Remarks:
 */

static int
kdb_mdr(kdb_machreg_t addr, unsigned int count)
{
	unsigned char c;
	while (count--) {
		if (kdb_getarea(c, addr))
			return(0);
		kdb_printf("%02x", c);
		addr++;
	}
	kdb_printf("\n");
	return(0);
}

/*
 * kdb_md
 *
 *	This function implements the 'md', 'md1', 'md2', 'md4', 'md8'
 *	'mdr' and 'mds' commands.
 *
 *	md|mds  [<addr arg> [<line count> [<radix>]]]
 *	mdWcN	[<addr arg> [<line count> [<radix>]]]
 *		where W = is the width (1, 2, 4 or 8) and N is the count.
 *		for eg., md1c20 reads 20 bytes, 1 at a time.
 *	mdr  <addr arg>,<byte count>
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_md(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	static kdb_machreg_t last_addr;
	static int last_radix, last_bytesperword, last_repeat;
	int radix = 16, mdcount = 8, bytesperword = sizeof(kdb_machreg_t), repeat;
	int nosect = 0;
	char fmtchar, fmtstr[64];
	kdb_machreg_t addr;
	unsigned long word;
	long offset = 0;
	kdb_symtab_t symtab;
	int symbolic = 0;
	int valid = 0;

	kdbgetintenv("MDCOUNT", &mdcount);
	kdbgetintenv("RADIX", &radix);
	kdbgetintenv("BYTESPERWORD", &bytesperword);

	/* Assume 'md <addr>' and start with environment values */
	repeat = mdcount * 16 / bytesperword;

	if (strcmp(argv[0], "mdr") == 0) {
		if (argc != 2)
			return KDB_ARGCOUNT;
		valid = 1;
	} else if (isdigit(argv[0][2])) {
		bytesperword = (int)(argv[0][2] - '0');
		if (bytesperword == 0) {
			bytesperword = last_bytesperword;
			if (bytesperword == 0) {
				bytesperword = 4;
			}
		}
		last_bytesperword = bytesperword;
		repeat = mdcount * 16 / bytesperword;
		if (!argv[0][3])
			valid = 1;
		else if (argv[0][3] == 'c' && argv[0][4]) {
			char *p;
			repeat = simple_strtoul(argv[0]+4, &p, 10);
			mdcount = ((repeat * bytesperword) + 15) / 16;
			valid = !*p;
		}
		last_repeat = repeat;
	} else if (strcmp(argv[0], "md") == 0)
		valid = 1;
	else if (strcmp(argv[0], "mds") == 0)
		valid = 1;
	if (!valid)
		return KDB_NOTFOUND;

	if (argc == 0) {
		if (last_addr == 0)
			return KDB_ARGCOUNT;
		addr = last_addr;
		radix = last_radix;
		bytesperword = last_bytesperword;
		repeat = last_repeat;
		mdcount = ((repeat * bytesperword) + 15) / 16;
	}

	if (argc) {
		kdb_machreg_t val;
		int diag, nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
		if (diag)
			return diag;
		if (argc > nextarg+2)
			return KDB_ARGCOUNT;

		if (argc >= nextarg) {
			diag = kdbgetularg(argv[nextarg], &val);
			if (!diag) {
				mdcount = (int) val;
				repeat = mdcount * 16 / bytesperword;
			}
		}
		if (argc >= nextarg+1) {
			diag = kdbgetularg(argv[nextarg+1], &val);
			if (!diag)
				radix = (int) val;
		}
	}

	if (strcmp(argv[0], "mdr") == 0) {
		return(kdb_mdr(addr, mdcount));
	}

	switch (radix) {
	case 10:
		fmtchar = 'd';
		break;
	case 16:
		fmtchar = 'x';
		break;
	case 8:
		fmtchar = 'o';
		break;
	default:
		return KDB_BADRADIX;
	}

	last_radix = radix;

	if (bytesperword > sizeof(kdb_machreg_t))
		return KDB_BADWIDTH;

	switch (bytesperword) {
	case 8:
		sprintf(fmtstr, "%%16.16l%c ", fmtchar);
		break;
	case 4:
		sprintf(fmtstr, "%%8.8l%c ", fmtchar);
		break;
	case 2:
		sprintf(fmtstr, "%%4.4l%c ", fmtchar);
		break;
	case 1:
		sprintf(fmtstr, "%%2.2l%c ", fmtchar);
		break;
	default:
		return KDB_BADWIDTH;
	}

	last_repeat = repeat;
	last_bytesperword = bytesperword;

	if (strcmp(argv[0], "mds") == 0) {
		symbolic = 1;
		/* Do not save these changes as last_*, they are temporary mds
		 * overrides.
		 */
		bytesperword = sizeof(kdb_machreg_t);
		repeat = mdcount;
		kdbgetintenv("NOSECT", &nosect);
	}

	/* Round address down modulo BYTESPERWORD */

	addr &= ~(bytesperword-1);

	while (repeat > 0) {
		int	num = (symbolic?1 :(16 / bytesperword));
		char	cbuf[32];
		char	*c = cbuf;
		int     i;

		memset(cbuf, '\0', sizeof(cbuf));
		kdb_printf(kdb_machreg_fmt0 " ", addr);

		for(i = 0; i < num && repeat--; i++) {
			if (kdb_getword(&word, addr, bytesperword))
				return 0;

			kdb_printf(fmtstr, word);
			if (symbolic) {
				kdbnearsym(word, &symtab);
			}
			else {
				memset(&symtab, 0, sizeof(symtab));
			}
			if (symtab.sym_name) {
				kdb_symbol_print(word, &symtab, 0);
				if (!nosect) {
					kdb_printf("\n");
					kdb_printf("                       %s %s "
						   kdb_machreg_fmt " " kdb_machreg_fmt " " kdb_machreg_fmt,
						symtab.mod_name,
						symtab.sec_name,
						symtab.sec_start,
						symtab.sym_start,
						symtab.sym_end);
				}
				addr += bytesperword;
			} else {
#define printable_char(addr) ({char __c = '\0'; unsigned long __addr = (addr); kdb_getarea(__c, __addr); isprint(__c) ? __c : '.';})
				switch (bytesperword) {
				case 8:
					*c++ = printable_char(addr++);
					*c++ = printable_char(addr++);
					*c++ = printable_char(addr++);
					*c++ = printable_char(addr++);
				case 4:
					*c++ = printable_char(addr++);
					*c++ = printable_char(addr++);
				case 2:
					*c++ = printable_char(addr++);
				case 1:
					*c++ = printable_char(addr++);
					break;
				}
#undef printable_char
			}
		}
		kdb_printf("%*s %s\n", (int)((num-i)*(2*bytesperword + 1)+1), " ", cbuf);
	}
	last_addr = addr;

	return 0;
}

/*
 * kdb_mm
 *
 *	This function implements the 'mm' command.
 *
 *	mm address-expression new-value
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	mm works on machine words, mmW works on bytes.
 */

int
kdb_mm(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	kdb_machreg_t addr;
	long 	      offset = 0;
	unsigned long contents;
	int nextarg;
	int width;

	if (argv[0][2] && !isdigit(argv[0][2]))
		return KDB_NOTFOUND;

	if (argc < 2) {
		return KDB_ARGCOUNT;
	}

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)))
		return diag;

	if (nextarg > argc)
		return KDB_ARGCOUNT;

	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &contents, NULL, NULL, regs)))
		return diag;

	if (nextarg != argc + 1)
		return KDB_ARGCOUNT;

	width = argv[0][2] ? (argv[0][2] - '0') : (sizeof(kdb_machreg_t));
	if ((diag = kdb_putword(addr, contents, width)))
		return(diag);

	kdb_printf(kdb_machreg_fmt " = " kdb_machreg_fmt "\n", addr, contents);

	return 0;
}

/*
 * kdb_go
 *
 *	This function implements the 'go' command.
 *
 *	go [address-expression]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	KDB_CMD_GO for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_go(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	kdb_machreg_t addr;
	int diag;
	int nextarg;
	long offset;

	if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg,
				     &addr, &offset, NULL, regs);
		if (diag)
			return diag;

		kdba_setpc(regs, addr);
	} else if (argc)
		return KDB_ARGCOUNT;

	return KDB_CMD_GO;
}

/*
 * kdb_rd
 *
 *	This function implements the 'rd' command.
 *
 *	rd		display all general registers.
 *	rd  c		display all control registers.
 *	rd  d		display all debug registers.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_rd(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	/*
	 */

	if (argc == 0) {
		return kdba_dumpregs(regs, NULL, NULL);
	}

	if (argc > 2) {
		return KDB_ARGCOUNT;
	}

	return kdba_dumpregs(regs, argv[1], argv[2]);
}

/*
 * kdb_rm
 *
 *	This function implements the 'rm' (register modify)  command.
 *
 *	rm register-name new-contents
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Currently doesn't allow modification of control or
 *	debug registers, nor does it allow modification
 *	of model-specific registers (MSR).
 */

int
kdb_rm(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	int ind = 0;
	kdb_machreg_t contents;

	if (argc != 2) {
		return KDB_ARGCOUNT;
	}

	/*
	 * Allow presence or absence of leading '%' symbol.
	 */

	if (argv[1][0] == '%')
		ind = 1;

	diag = kdbgetularg(argv[2], &contents);
	if (diag)
		return diag;

	diag = kdba_setregcontents(&argv[1][ind], regs, contents);
	if (diag)
		return diag;

	return 0;
}

#if defined(CONFIG_MAGIC_SYSRQ)
/*
 * kdb_sr
 *
 *	This function implements the 'sr' (SYSRQ key) command which
 *	interfaces to the soi-disant MAGIC SYSRQ functionality.
 *
 *	sr <magic-sysrq-code>
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	None.
 */
int
kdb_sr(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	if (argc != 1) {
		return KDB_ARGCOUNT;
	}

	handle_sysrq(*argv[1], regs, 0);

	return 0;
}
#endif	/* CONFIG_MAGIC_SYSRQ */

/*
 * kdb_ef
 *
 *	This function implements the 'regs' (display exception frame)
 *	command.  This command takes an address and expects to find
 *	an exception frame at that address, formats and prints it.
 *
 *	regs address-expression
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Not done yet.
 */

int
kdb_ef(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	kdb_machreg_t   addr;
	long		offset;
	int nextarg;

	if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
		if (diag)
			return diag;

		return kdba_dumpregs((struct pt_regs *)addr, NULL, NULL);
	}

	return KDB_ARGCOUNT;
}

/*
 * kdb_reboot
 *
 *	This function implements the 'reboot' command.  Reboot the system
 *	immediately.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Shouldn't return from this function.
 */

int
kdb_reboot(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	machine_restart(0);
	/* NOTREACHED */
	return 0;
}

#if defined(CONFIG_MODULES)
extern struct module *find_module(const char *);
extern void free_module(struct module *, int);

/*
 * kdb_lsmod
 *
 *	This function implements the 'lsmod' command.  Lists currently
 *	loaded kernel modules.
 *
 *	Mostly taken from userland lsmod.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *
 */

int
kdb_lsmod(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	if (argc != 0)
		return KDB_ARGCOUNT;
	return 0;

#if 0
	struct module *mod;
	struct module_ref *mr;

	kdb_printf("Module                  Size  modstruct     Used by\n");
	for (mod = module_list; mod && mod->next ;mod = mod->next) {
		kdb_printf("%-20s%8lu  0x%p  %4ld ", mod->name, mod->size, (void *)mod,
			(long)atomic_read(&mod->uc.usecount));

		if (mod->flags & MOD_DELETED)
			kdb_printf(" (deleted)");
		else if (mod->flags & MOD_INITIALIZING)
			kdb_printf(" (initializing)");
		else if (!(mod->flags & MOD_RUNNING))
			kdb_printf(" (uninitialized)");
		else {
			if (mod->flags &  MOD_AUTOCLEAN)
				kdb_printf(" (autoclean)");
			if (!(mod->flags & MOD_USED_ONCE))
				kdb_printf(" (unused)");
		}

		if (mod->refs) {
			kdb_printf(" [ ");

			mr = mod->refs;
			while (mr) {
				kdb_printf("%s ", mr->ref->name);
				mr = mr->next_ref;
			}

			kdb_printf("]");
		}

		kdb_printf("\n");
	}

	return 0;
#endif
}

/*
 * kdb_rmmod
 *
 *	This function implements the 'rmmod' command.  Removes a given
 *	kernel module.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Danger: free_module() calls mod->cleanup().  If the cleanup routine
 *	relies on interrupts then it will hang, kdb has interrupts disabled.
 */

int
kdb_rmmod(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
    return 0;
#if 0
	struct module *mod;


	if (argc != 1)
		return KDB_ARGCOUNT;

	kdb_printf("Attempting to remove module: [%s]\n", argv[1]);
	if ((mod = find_module(argv[1])) == NULL) {
		kdb_printf("Unable to find a module by that name\n");
		return 0;
	}

	if (mod->refs != NULL || __MOD_IN_USE(mod)) {
		kdb_printf("Module is in use, unable to unload\n");
		return 0;
	}

	free_module(mod, 0);
	kdb_printf("Module successfully unloaded\n");

	return 0;
#endif
}
#endif	/* CONFIG_MODULES */

/*
 * kdb_env
 *
 *	This function implements the 'env' command.  Display the current
 *	environment variables.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_env(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int i;

	for(i=0; i<__nenv; i++) {
		if (__env[i]) {
			kdb_printf("%s\n", __env[i]);
		}
	}

	if (KDB_DEBUG(MASK))
		kdb_printf("KDBFLAGS=0x%x\n", kdb_flags);

	return 0;
}

/*
 * kdb_set
 *
 *	This function implements the 'set' command.  Alter an existing
 *	environment variable or create a new one.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_set(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int i;
	char *ep;
	size_t varlen, vallen;

	/*
	 * we can be invoked two ways:
	 *   set var=value    argv[1]="var", argv[2]="value"
	 *   set var = value  argv[1]="var", argv[2]="=", argv[3]="value"
	 * - if the latter, shift 'em down.
	 */
	if (argc == 3) {
		argv[2] = argv[3];
		argc--;
	}

	if (argc != 2)
		return KDB_ARGCOUNT;

	/*
	 * Check for internal variables
	 */
	if (strcmp(argv[1], "KDBDEBUG") == 0) {
		unsigned int debugflags;
		char *cp;

		debugflags = simple_strtoul(argv[2], &cp, 0);
		if (cp == argv[2] || debugflags & ~KDB_DEBUG_FLAG_MASK) {
			kdb_printf("kdb: illegal debug flags '%s'\n",
				    argv[2]);
			return 0;
		}
		kdb_flags = (kdb_flags & ~(KDB_DEBUG_FLAG_MASK << KDB_DEBUG_FLAG_SHIFT))
			  | (debugflags << KDB_DEBUG_FLAG_SHIFT);

		return 0;
	}

	/*
	 * Tokenizer squashed the '=' sign.  argv[1] is variable
	 * name, argv[2] = value.
	 */
	varlen = strlen(argv[1]);
	vallen = strlen(argv[2]);
	ep = kdballocenv(varlen + vallen + 2);
	if (ep == (char *)0)
		return KDB_ENVBUFFULL;

	sprintf(ep, "%s=%s", argv[1], argv[2]);

	ep[varlen+vallen+1]='\0';

	for(i=0; i<__nenv; i++) {
		if (__env[i]
		 && ((strncmp(__env[i], argv[1], varlen)==0)
		   && ((__env[i][varlen] == '\0')
		    || (__env[i][varlen] == '=')))) {
			__env[i] = ep;
			return 0;
		}
	}

	/*
	 * Wasn't existing variable.  Fit into slot.
	 */
	for(i=0; i<__nenv-1; i++) {
		if (__env[i] == (char *)0) {
			__env[i] = ep;
			return 0;
		}
	}

	return KDB_ENVFULL;
}

/*
 * kdb_dmesg
 *
 *	This function implements the 'dmesg' command to display the contents
 *	of the syslog buffer.
 *
 *	dmesg [lines]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	None.
 */

int
kdb_dmesg(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	char *syslog_data[4], *start, *end, c;
	int diag, logging, logsize, lines = 0;

	if (argc > 1)
		return KDB_ARGCOUNT;
	if (argc) {
		char *cp;
		lines = simple_strtoul(argv[1], &cp, 0);
		if (*cp || lines < 0)
			lines = 0;
	}

	/* disable LOGGING if set */
	diag = kdbgetintenv("LOGGING", &logging);
	if (!diag && logging) {
		const char *setargs[] = { "set", "LOGGING", "0" };
		kdb_set(2, setargs, envp, regs);
	}

	/* syslog_data[0,1] physical start, end+1.  syslog_data[2,3] logical start, end+1. */
	kdb_syslog_data(syslog_data);
	if (syslog_data[2] == syslog_data[3])
		return 0;
	logsize = syslog_data[1] - syslog_data[0];
	start = syslog_data[0] + (syslog_data[2] - syslog_data[0]) % logsize;
	end = syslog_data[0] + (syslog_data[3] - syslog_data[0]) % logsize;
#define WRAP(p) if (p < syslog_data[0]) p = syslog_data[1]-1; else if (p >= syslog_data[1]) p = syslog_data[0]
	if (lines) {
		char *p = end;
		++lines;
		do {
			--p;
			WRAP(p);
			if (*p == '\n') {
				if (--lines == 0) {
					++p;
					WRAP(p);
					break;
				}
			}
		} while (p != start);
		start = p;
	}
	/* Do a line at a time (max 200 chars) to reduce protocol overhead */
	c = '\0';
	while(1) {
		char *p;
		int chars = 0;
		if (!*start) {
			while (!*start) {
				++start;
				WRAP(start);
				if (start == end)
					break;
			}
			if (start == end)
				break;
		}
		p = start;
		while (*start && chars < 200) {
			c = *start;
			++chars;
			++start;
			WRAP(start);
			if (start == end || c == '\n')
				break;
		}
		if (chars)
			kdb_printf("%.*s", chars, p);
		if (start == end)
			break;
	}
	if (c != '\n')
		kdb_printf("\n");

	return 0;
}

/*
 * kdb_cpu
 *
 *	This function implements the 'cpu' command.
 *
 *	cpu	[<cpunum>]
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	KDB_CMD_CPU for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	All cpu's should be spinning in kdb().  However just in case
 *	a cpu did not take the smp_kdb_stop NMI, check that a cpu
 *	entered kdb() before passing control to it.
 */

int
kdb_cpu(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	unsigned long cpunum;
	int diag, first = 1;

	if (argc == 0) {
		int i;

		kdb_printf("Currently on cpu %d\n", smp_processor_id());
		kdb_printf("Available cpus: ");
		for (i=0; i<NR_CPUS; i++) {
			if (cpu_online(i)) {
				if (!first)
					kdb_printf(", ");
				first = 0;
				kdb_printf("%d", i);
				if (!KDB_STATE_CPU(KDB, i))
					kdb_printf("*");
			}
		}
		kdb_printf("\n");
		return 0;
	}

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetularg(argv[1], &cpunum);
	if (diag)
		return diag;

	/*
	 * Validate cpunum
	 */
	if ((cpunum > NR_CPUS)
	 || !cpu_online(cpunum)
	 || !KDB_STATE_CPU(KDB, cpunum))
		return KDB_BADCPUNUM;

	kdb_new_cpu = cpunum;

	/*
	 * Switch to other cpu
	 */
	return KDB_CMD_CPU;
}

/*
 * kdb_ps
 *
 *	This function implements the 'ps' command which shows
 *	a list of the active processes.
 *
 *	ps [DRSTZU]			All processes, optionally filtered by state
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

void
kdb_ps1(struct task_struct *p)
{
	kdb_printf("0x%p %08d %08d  %1.1d  %3.3d  %s  0x%p%c%s\n",
		   (void *)p, p->pid, p->parent->pid,
		   p->state == TASK_RUNNING, p->thread_info->cpu,
		   (p->state == 0)?"run ":(p->state>0)?"stop":"unrn",
		   (void *)(&p->thread),
		   (p == current) ? '*': ' ',
		   p->comm);
}

int
kdb_ps(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	struct task_struct	*p;
	unsigned long	mask;

	kdb_printf("%-*s Pid      Parent   [*] cpu  State %-*s Command\n",
		(int)(2*sizeof(void *))+2, "Task Addr",
		(int)(2*sizeof(void *))+2, "Thread");
	mask = kdb_task_state_string(argc, argv, envp);
	for_each_process(p) {
		if (!kdb_task_state(p, mask))
			continue;
		kdb_ps1(p);
	}

	return 0;
}

/*
 * kdb_ll
 *
 *	This function implements the 'll' command which follows a linked
 *	list and executes an arbitrary command for each element.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_ll(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	kdb_machreg_t addr;
	long 	      offset = 0;
	kdb_machreg_t va;
	unsigned long linkoffset;
	int nextarg;

	if (argc != 3) {
		return KDB_ARGCOUNT;
	}

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	diag = kdbgetularg(argv[2], &linkoffset);
	if (diag)
		return diag;

	/*
	 * Using the starting address as
	 * the first element in the list, and assuming that
	 * the list ends with a null pointer.
	 */

	va = addr;

	while (va) {
		char buf[80];

		sprintf(buf, "%s " kdb_machreg_fmt "\n", argv[3], va);
		diag = kdb_parse(buf, regs);
		if (diag)
			return diag;

		addr = va + linkoffset;
		if (kdb_getword(&va, addr, sizeof(va)))
			return(0);
	}

	return 0;
}

/*
 * kdb_sections_callback
 *
 *	Invoked from kallsyms_sections for each section.
 *
 * Inputs:
 *	prevmod	Previous module name
 *	modname	Module name
 *	secname	Section name
 *	secstart Start of section
 *	secend	End of section
 *	secflags Section flags
 * Outputs:
 *	None.
 * Returns:
 *	Always zero
 * Locking:
 *	none.
 * Remarks:
 */

static int
kdb_sections_callback(void *token, const char *modname, const char *secname,
		      ElfW(Addr) secstart, ElfW(Addr) secend, ElfW(Word) secflags)
{
	const char **prevmod = (const char **)token;
	if (*prevmod != modname) {
		*prevmod = modname;
		kdb_printf("\n%s", modname);
	}
	kdb_printf(" %s " kdb_elfw_addr_fmt0 " " kdb_elfw_addr_fmt0 " 0x%x",
		secname, secstart, secend, secflags);
	return(0);
}

/*
 * kdb_sections
 *
 *	This function implements the 'sections' command which prints the
 *	kernel and module sections.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_sections(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
#if 0
	char *prev_mod = NULL;
	if (argc != 0) {
		return KDB_ARGCOUNT;
	}
	kallsyms_sections(&prev_mod, kdb_sections_callback);
#endif
	kdb_printf("\n");	/* End last module */
	return(0);
}

/*
 * kdb_help
 *
 *	This function implements the 'help' and '?' commands.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 */

int
kdb_help(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	kdbtab_t *kt;

	kdb_printf("%-15.15s %-20.20s %s\n", "Command", "Usage", "Description");
	kdb_printf("----------------------------------------------------------\n");
	for(kt=kdb_commands; kt->cmd_name; kt++) {
		kdb_printf("%-15.15s %-20.20s %s\n", kt->cmd_name,
			kt->cmd_usage, kt->cmd_help);
	}
	return 0;
}

/*
 * kdb_register_repeat
 *
 *	This function is used to register a kernel debugger command.
 *
 * Inputs:
 *	cmd	Command name
 *	func	Function to execute the command
 *	usage	A simple usage string showing arguments
 *	help	A simple help string describing command
 *	repeat	Does the command auto repeat on enter?
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, one if a duplicate command.
 * Locking:
 *	none.
 * Remarks:
 *
 */

#define kdb_command_extend 50	/* arbitrary */
int
kdb_register_repeat(char *cmd,
		    kdb_func_t func,
		    char *usage,
		    char *help,
		    short minlen,
		    kdb_repeat_t repeat)
{
	int i;
	kdbtab_t *kp;

	/*
	 *  Brute force method to determine duplicates
	 */
	for (i=0, kp=kdb_commands; i<kdb_max_commands; i++, kp++) {
		if (kp->cmd_name && (strcmp(kp->cmd_name, cmd)==0)) {
			kdb_printf("Duplicate kdb command registered: '%s'\n",
				   cmd);
			return 1;
		}
	}

	/*
	 * Insert command into first available location in table
	 */
	for (i=0, kp=kdb_commands; i<kdb_max_commands; i++, kp++) {
		if (kp->cmd_name == NULL) {
			break;
		}
	}

	if (i >= kdb_max_commands) {
		kdbtab_t *new = kmalloc((kdb_max_commands + kdb_command_extend) * sizeof(*new), GFP_KERNEL);
		if (!new) {
			kdb_printf("Could not allocate new kdb_command table\n");
			return 1;
		}
		if (kdb_commands) {
			memcpy(new, kdb_commands, kdb_max_commands * sizeof(*new));
			kfree(kdb_commands);
		}
		memset(new + kdb_max_commands, 0, kdb_command_extend * sizeof(*new));
		kdb_commands = new;
		kp = kdb_commands + kdb_max_commands;
		kdb_max_commands += kdb_command_extend;
	}

	kp->cmd_name   = cmd;
	kp->cmd_func   = func;
	kp->cmd_usage  = usage;
	kp->cmd_help   = help;
	kp->cmd_flags  = 0;
	kp->cmd_minlen = minlen;
	kp->cmd_repeat = repeat;

	return 0;
}

/*
 * kdb_register
 *
 *	Compatibility register function for commands that do not need to
 *	specify a repeat state.  Equivalent to kdb_register_repeat with
 *	KDB_REPEAT_NONE.
 *
 * Inputs:
 *	cmd	Command name
 *	func	Function to execute the command
 *	usage	A simple usage string showing arguments
 *	help	A simple help string describing command
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, one if a duplicate command.
 * Locking:
 *	none.
 * Remarks:
 *
 */

int
kdb_register(char *cmd,
	     kdb_func_t func,
	     char *usage,
	     char *help,
	     short minlen)
{
	return kdb_register_repeat(cmd, func, usage, help, minlen, KDB_REPEAT_NONE);
}

/*
 * kdb_unregister
 *
 *	This function is used to unregister a kernel debugger command.
 *	It is generally called when a module which implements kdb
 *	commands is unloaded.
 *
 * Inputs:
 *	cmd	Command name
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, one command not registered.
 * Locking:
 *	none.
 * Remarks:
 *
 */

int
kdb_unregister(char *cmd)
{
	int i;
	kdbtab_t *kp;

	/*
	 *  find the command.
	 */
	for (i=0, kp=kdb_commands; i<kdb_max_commands; i++, kp++) {
		if (kp->cmd_name && (strcmp(kp->cmd_name, cmd)==0)) {
			kp->cmd_name = NULL;
			return 0;
		}
	}

	/*
	 * Couldn't find it.
	 */
	return 1;
}

/*
 * kdb_inittab
 *
 *	This function is called by the kdb_init function to initialize
 *	the kdb command table.   It must be called prior to any other
 *	call to kdb_register_repeat.
 *
 * Inputs:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *
 */

static void __init
kdb_inittab(void)
{
	int i;
	kdbtab_t *kp;

	for(i=0, kp=kdb_commands; i < kdb_max_commands; i++,kp++) {
		kp->cmd_name = NULL;
	}

	kdb_register_repeat("md", kdb_md, "<vaddr>",   "Display Memory Contents, also mdWcN, e.g. md8c1", 1, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mdr", kdb_md, "<vaddr> <bytes>", 	"Display Raw Memory", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mds", kdb_md, "<vaddr>", 	"Display Memory Symbolically", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mm", kdb_mm, "<vaddr> <contents>",   "Modify Memory Contents", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("id", kdb_id, "<vaddr>",   "Display Instructions", 1, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("go", kdb_go, "[<vaddr>]", "Continue Execution", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("rd", kdb_rd, "",		"Display Registers", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("rm", kdb_rm, "<reg> <contents>", "Modify Registers", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("ef", kdb_ef, "<vaddr>",   "Display exception frame", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("bt", kdb_bt, "[<vaddr>]", "Stack traceback", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("btp", kdb_bt, "<pid>", 	"Display stack for process <pid>", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("bta", kdb_bt, "", 	"Display stack all processes", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("ll", kdb_ll, "<first-element> <linkoffset> <cmd>", "Execute cmd for each element in linked list", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("env", kdb_env, "", 	"Show environment variables", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("set", kdb_set, "", 	"Set environment variables", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("help", kdb_help, "", 	"Display Help Message", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("?", kdb_help, "",         "Display Help Message", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("cpu", kdb_cpu, "<cpunum>","Switch to new cpu", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("ps", kdb_ps, "", 		"Display active task list", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("reboot", kdb_reboot, "",  "Reboot the machine immediately", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("sections", kdb_sections, "",  "List kernel and module sections", 0, KDB_REPEAT_NONE);
#if defined(CONFIG_MODULES)
	kdb_register_repeat("lsmod", kdb_lsmod, "",	"List loaded kernel modules", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("rmmod", kdb_rmmod, "<modname>", "Remove a kernel module", 0, KDB_REPEAT_NONE);
#endif
#if defined(CONFIG_MAGIC_SYSRQ)
	kdb_register_repeat("sr", kdb_sr, "<key>",	"Magic SysRq key", 0, KDB_REPEAT_NONE);
#endif
	kdb_register_repeat("dmesg", kdb_dmesg, "[lines]",	"Display syslog buffer", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("defcmd", kdb_defcmd, "name \"usage\" \"help\"", "Define a set of commands, down to endefcmd", 0, KDB_REPEAT_NONE);
}

/*
 * kdb_cmd_init
 *
 *	This function is called by the kdb_init function to execute any
 *	commands defined in kdb_cmds.
 *
 * Inputs:
 *	Commands in *kdb_cmds[];
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *
 */

static void __init
kdb_cmd_init(void)
{
	int i, diag;
	for (i = 0; kdb_cmds[i]; ++i) {
		kdb_printf("kdb_cmd[%d]%s: %s",
				i, defcmd_in_progress ? "[defcmd]" : "", kdb_cmds[i]);
		diag = kdb_parse(kdb_cmds[i], NULL);
		if (diag)
			kdb_printf("command failed, kdb diag %d\n", diag);
	}
	if (defcmd_in_progress) {
		kdb_printf("Incomplete 'defcmd' set, forcing endefcmd\n");
		kdb_parse("endefcmd", NULL);
	}
}

/*
 * kdb_panic
 *
 *	Invoked via the panic_notifier_list.
 *
 * Inputs:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	Zero.
 * Locking:
 *	None.
 * Remarks:
 *	When this function is called from panic(), the other cpus have already
 *	been stopped.
 *
 */

static int
kdb_panic(struct notifier_block *self, unsigned long command, void *ptr)
{
	KDB_ENTER();
	return(0);
}

static struct notifier_block kdb_block = { kdb_panic, NULL, 0 };

/*
 * kdb_init
 *
 * 	Initialize the kernel debugger environment.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	None.
 */

void __init
kdb_init(void)
{
	/*
	 * This must be called before any calls to kdb_printf.
	 */
	kdb_io_init();

	kdb_inittab();		/* Initialize Command Table */
	kdb_initbptab();	/* Initialize Breakpoint Table */
	kdb_id_init();		/* Initialize Disassembler */
	kdba_init();		/* Architecture Dependent Initialization */

	/*
	 * Use printk() to get message in log_buf[];
	 */
	printk("kdb version %d.%d%s by Keith Owens, Scott Lurndal. "\
	       "Copyright SGI, All Rights Reserved\n",
		KDB_MAJOR_VERSION, KDB_MINOR_VERSION, KDB_TEST_VERSION);

	kdb_cmd_init();		/* Preset commands from kdb_cmds */
	kdb(KDB_REASON_SILENT, 0, 0);	/* Activate any preset breakpoints on boot cpu */
	notifier_chain_register(&panic_notifier_list, &kdb_block);
}

EXPORT_SYMBOL(kdb_register);
EXPORT_SYMBOL(kdb_register_repeat);
EXPORT_SYMBOL(kdb_unregister);
EXPORT_SYMBOL(kdb_getarea_size);
EXPORT_SYMBOL(kdb_putarea_size);
EXPORT_SYMBOL(kdb_getword);
EXPORT_SYMBOL(kdb_putword);
EXPORT_SYMBOL(kdbgetularg);
EXPORT_SYMBOL(kdbgetenv);
EXPORT_SYMBOL(kdbgetintenv);
EXPORT_SYMBOL(kdbgetaddrarg);
EXPORT_SYMBOL(kdb);
EXPORT_SYMBOL(kdb_on);
EXPORT_SYMBOL(kdbgetsymval);
EXPORT_SYMBOL(kdbnearsym);
EXPORT_SYMBOL(kdb_printf);
EXPORT_SYMBOL(kdb_symbol_print);
