/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* spin lock profiling */

/*
 * Spin-lock profiling code.
 *
 * Basic notion in our profiling code is "profiling region" (struct
 * profregion). Profiling region is entered and left by calling
 * profregion_in() and profregion_ex() function correspondingly. It is invalid
 * to be preempted (voluntary or not) while inside profiling region. Profiling
 * regions can be entered recursively, and it is not necessary nest then
 * properly, that is
 *
 *     profregion_in(&A);
 *     profregion_in(&B);
 *     profregion_ex(&A);
 *     profregion_ex(&B))
 *
 * is valid sequence of operations. Each CPU maintains an array of currently
 * active profiling regions. This array is consulted by clock interrupt
 * handler, and counters in the profiling regions found active by handler are
 * incremented. This allows one to estimate for how long region has been
 * active on average. Spin-locking code in spin_macros.h uses this to measure
 * spin-lock contention. Specifically two profiling regions are defined for
 * each spin-lock type: one is activate while thread is trying to acquire
 * lock, and another when it holds the lock.
 *
 * Profiling regions export their statistics in the sysfs, under special
 * directory /sys/profregion.
 *
 * Each profregion is represented as a child directory
 * /sys/profregion/foo. Internally it's represented as a struct kobject (viz
 * one embedded into struct profregion, see spinprof.h).
 *
 * Each /sys/profregion/foo directory contains files representing fields in
 * profregion:
 *
 *     hits
 *     busy
 *     obj
 *     objhit
 *     code
 *     codehit
 *
 * See spinprof.h for details.
 *
 *
 */

#include "kattr.h"
#include "spinprof.h"
#include "debug.h"

#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>

#include <asm/irq.h>
#include <asm/ptrace.h> /* for instruction_pointer() */

#if REISER4_LOCKPROF

/*
 * helper macro: how many bytes left in the PAGE_SIZE buffer, starting at @buf
 * and used up to and including @p.
 */
#define LEFT(p, buf) (PAGE_SIZE - ((p) - (buf)) - 1)

void profregion_functions_start_here(void);
void profregion_functions_end_here(void);

static locksite none = {
	.hits = STATCNT_INIT,
	.func = "",
	.line = 0
};

/*
 * sysfs holder.
 */
struct profregion_attr {
	struct attribute attr;
	ssize_t (*show)(struct profregion *pregion, char *buf);
};

/*
 * macro to define profregion_attr for the given profregion
 */
#define PROFREGION_ATTR(aname)			\
static struct profregion_attr aname = {		\
	.attr = {				\
		.name = (char *)#aname,		\
		.mode = 0666			\
	},					\
	.show = aname ## _show			\
}

/*
 * ->show() method for the "hits" attribute.
 */
static ssize_t hits_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%li\n", statcnt_get(&pregion->hits));
	return (p - buf);
}

/*
 * ->show() method for the "busy" attribute.
 */
static ssize_t busy_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%li\n", statcnt_get(&pregion->busy));
	return (p - buf);
}

/*
 * ->show() method for the "obj" attribute.
 */
static ssize_t obj_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%p\n", pregion->obj);
	return (p - buf);
}

/*
 * ->show() method for the "objhit" attribute.
 */
static ssize_t objhit_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%i\n", pregion->objhit);
	return (p - buf);
}

/*
 * ->show() method for the "code" attribute.
 */
static ssize_t code_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	locksite *site;

	site = pregion->code ? : &none;
	KATTR_PRINT(p, buf, "%s:%i\n", site->func, site->line);
	return (p - buf);
}

/*
 * ->show() method for the "codehit" attribute.
 */
static ssize_t codehit_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%i\n", pregion->codehit);
	return (p - buf);
}

PROFREGION_ATTR(hits);
PROFREGION_ATTR(busy);
PROFREGION_ATTR(obj);
PROFREGION_ATTR(objhit);
PROFREGION_ATTR(code);
PROFREGION_ATTR(codehit);

/*
 * wrapper to call attribute ->show() methods (defined above). This is called
 * by sysfs.
 */
static ssize_t
profregion_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct profregion *pregion;
	struct profregion_attr *pattr;

	pregion = container_of(kobj, struct profregion, kobj);
	pattr   = container_of(attr, struct profregion_attr, attr);

	return pattr->show(pregion, buf);
}

/*
 * ->store() method for profregion sysfs object. Any write to this object,
 * just resets profregion stats.
 */
static ssize_t profregion_store(struct kobject * kobj,
				struct attribute * attr UNUSED_ARG,
				const char * buf UNUSED_ARG,
				size_t size)
{
	struct profregion *pregion;

	pregion = container_of(kobj, struct profregion, kobj);
	statcnt_reset(&pregion->hits);
	statcnt_reset(&pregion->busy);
	pregion->obj     = 0;
	pregion->objhit  = 0;
	pregion->code    = 0;
	pregion->codehit = 0;
	return size;
}

/*
 * sysfs attribute operations vector...
 */
static struct sysfs_ops profregion_attr_ops = {
	.show  = profregion_show,
	.store = profregion_store
};

/*
 * ...and attributes themselves.
 */
static struct attribute * def_attrs[] = {
	&hits.attr,
	&busy.attr,
	&obj.attr,
	&objhit.attr,
	&code.attr,
	&codehit.attr,
	NULL
};

/*
 * ktype for kobjects representing profregions.
 */
static struct kobj_type ktype_profregion = {
	.sysfs_ops	= &profregion_attr_ops,
	.default_attrs	= def_attrs,
};

/*
 * sysfs object for /sys/profregion
 */
static decl_subsys(profregion, &ktype_profregion, NULL);

/*
 * profregionstack for each CPU
 */
DEFINE_PER_CPU(struct profregionstack, inregion) = {0};

/*
 * profregion meaning "no other profregion is active"
 */
struct profregion outside = {
	.hits = STATCNT_INIT,
	.kobj = {
		.name = "outside"
	}
};

/*
 * profregion meaning "we are in reiser4 context, but no locks are held"
 */
struct profregion incontext = {
	.hits = STATCNT_INIT,
	.kobj = {
		.name = "incontext"
	}
};

/*
 * profregion meaning "we are profregion handling code". This is to estimate
 * profregion overhead.
 */
struct profregion overhead = {
	.hits = STATCNT_INIT,
	.kobj = {
		.name = "overhead"
	}
};

extern struct profregion pregion_spin_jnode_held;
extern struct profregion pregion_spin_jnode_trying;

/*
 * This is main profregion handling function. It is called from clock
 * interrupt handler on each tick (HZ times per second).
 *
 * It determines what profregions are active at the moment of call, and
 * updates their fields correspondingly.
 */
static int callback(struct notifier_block *self UNUSED_ARG,
		    unsigned long val UNUSED_ARG, void *p)
{
	struct profregionstack *stack;
	struct pt_regs *regs;
	unsigned long pc;
	int ntop;

	regs = p;
	/* instruction pointer at which interrupt happened */
	pc = instruction_pointer(regs);

	if (pc > (unsigned long)profregion_functions_start_here &&
	    pc < (unsigned long)profregion_functions_end_here) {
		/* if @pc lies in this file---count it as overhead */
		statcnt_inc(&overhead.hits);
		return 0;
	}

	stack = &get_cpu_var(inregion);
	ntop = stack->top;
	if (unlikely(ntop != 0)) {
		struct pregactivation *act;
		struct profregion *preg;
		int hits;

		act = &stack->stack[ntop - 1];
		preg = act->preg;
		statcnt_inc(&preg->hits);

		hits = 0;
		if (act->objloc != NULL) {
			BUG_ON(*act->objloc == 0x6b6b6b6b);
			BUG_ON(*act->objloc == 0x5a5a5a5a);
			hits = ++ (*act->objloc);
		}
		if (unlikely(hits > preg->objhit)) {
			if (preg->obj != act->objloc) {
				preg->objhit = hits;
				preg->obj    = act->objloc;
				if (preg->champion != NULL)
					preg->champion(preg);
			}
		}

		hits = 0;
		if (act->codeloc != NULL) {
			statcnt_inc(&act->codeloc->hits);
			hits = statcnt_get(&act->codeloc->hits);
		}
		if (unlikely(hits > preg->codehit)) {
			preg->codehit = hits;
			preg->code    = act->codeloc;
		}
		for (; ntop > 0 ; --ntop) {
			preg = stack->stack[ntop - 1].preg;
			if (preg != NULL)
				statcnt_inc(&preg->busy);
		}
	} else if (is_in_reiser4_context())
		statcnt_inc(&incontext.hits);
	else
		statcnt_inc(&outside.hits);
	put_cpu_var(inregion);
	return 0;
}

/*
 * notifier block used to register our callback for clock interrupt handler.
 */
static struct notifier_block profregionnotifier = {
	.notifier_call = callback
};

/* different architectures tend to declare register_profile_notifier() in
 * different places */
extern int register_profile_notifier(struct notifier_block * nb);

/*
 * profregion initialization: setup sysfs things.
 */
int __init
profregion_init(void)
{
	int result;

	/* register /sys/profregion */
	result = subsystem_register(&profregion_subsys);
	if (result != 0)
		return result;

	/* register /sys/profregion/outside */
	result = profregion_register(&outside);
	if (result != 0)
		return result;

	/* register /sys/profregion/incontext */
	result = profregion_register(&incontext);
	if (result != 0)
		return result;

	/* register /sys/profregion/overhead */
	result = profregion_register(&overhead);
	if (result != 0)
		return result;

	/* register our callback function to be called on each clock tick */
	return register_profile_notifier(&profregionnotifier);
}
subsys_initcall(profregion_init);

/*
 * undo profregion_init() actions.
 */
static void __exit
profregion_exit(void)
{
	profregion_unregister(&overhead);
	profregion_unregister(&incontext);
	profregion_unregister(&outside);
	subsystem_unregister(&profregion_subsys);
}
__exitcall(profregion_exit);

/*
 * register profregion
 */
int profregion_register(struct profregion *pregion)
{
	/* tell sysfs that @pregion is part of /sys/profregion "subsystem" */
	kobj_set_kset_s(pregion, profregion_subsys);
	/* and register /sys/profregion/<pregion> */
	return kobject_register(&pregion->kobj);
}

/*
 * dual to profregion_register(): unregister profregion
 */
void profregion_unregister(struct profregion *pregion)
{
	kobject_register(&pregion->kobj);
}

void profregion_functions_start_here(void) { }

/*
 * search for @pregion in the stack of currently active profregions on this
 * cpu. Return its index if found, 0 otherwise.
 */
int profregion_find(struct profregionstack *stack, struct profregion *pregion)
{
	int i;

	for (i = stack->top - 2 ; i >= 0 ; -- i) {
		if (stack->stack[i].preg == pregion) {
			return i;
		}
	}
	BUG();
	return 0;
}

/*
 * Fill @act slot with information
 */
void profregfill(struct pregactivation *act,
		 struct profregion *pregion,
		 void *objloc, locksite *codeloc)
{
	act->objloc  = NULL;
	act->codeloc = NULL;
	/* barrier is needed here, because clock interrupt can come at any
	 * point, and we want our callback to see consistent data */
	barrier();
	act->preg    = pregion;
	act->objloc  = objloc;
	act->codeloc = codeloc;
}

/*
 * activate profregion @pregion on processor @cpu.
 */
void profregion_in(int cpu, struct profregion *pregion,
		   void *objloc, locksite *codeloc)
{
	struct profregionstack *stack;
	int ntop;

	preempt_disable();
	stack = &per_cpu(inregion, cpu);
	ntop = stack->top;
	/* check for stack overflow */
	BUG_ON(ntop == PROFREGION_MAX_DEPTH);
	/* store information about @pregion in the next free slot on the
	 * stack */
	profregfill(&stack->stack[ntop], pregion, objloc, codeloc);
	/* put optimization barrier here */
	/* barrier is needed here, because clock interrupt can come at any
	 * point, and we want our callback to see consistent data */
	barrier();
	++ stack->top;
}

/*
 * deactivate (leave) @pregion at processor @cpu.
 */
void profregion_ex(int cpu, struct profregion *pregion)
{
	struct profregionstack *stack;
	int ntop;

	stack = &per_cpu(inregion, cpu);
	ntop = stack->top;
	BUG_ON(ntop == 0);
	/*
	 * in the usual case (when locks nest properly), @pregion uses top
	 * slot of the stack. Free it.
	 */
	if(likely(stack->stack[ntop - 1].preg == pregion)) {
		do {
			-- ntop;
		} while (ntop > 0 &&
			 stack->stack[ntop - 1].preg == NULL);
		/* put optimization barrier here */
		barrier();
		stack->top = ntop;
	} else
		/*
		 * Otherwise (locks are not nested), find slot used by
		 * @prefion and free it.
		 */
		stack->stack[profregion_find(stack, pregion)].preg = NULL;
	preempt_enable();
	put_cpu();
}

/*
 * simultaneously deactivate top-level profregion in the stack, and activate
 * @pregion. This is optimization to serve common case, when profregion
 * covering "trying to take lock X" is immediately followed by profregion
 * covering "holding lock X".
 */
void profregion_replace(int cpu, struct profregion *pregion,
			void *objloc, void *codeloc)
{
	struct profregionstack *stack;
	int ntop;

	stack = &per_cpu(inregion, cpu);
	ntop = stack->top;
	BUG_ON(ntop == 0);
	profregfill(&stack->stack[ntop - 1], pregion, objloc, codeloc);
}

void profregion_functions_end_here(void) { }

/* REISER4_LOCKPROF */
#else

#if defined(CONFIG_REISER4_NOOPT) || defined(CONFIG_KGDB)

locksite __hits;
locksite __hits_h;
locksite __hits_t;
locksite __hits_held;
locksite __hits_trying;

#endif /* CONFIG_REISER4_NOOPT */

/* REISER4_LOCKPROF */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
