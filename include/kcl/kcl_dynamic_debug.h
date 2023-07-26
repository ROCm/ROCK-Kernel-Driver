/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef AMDKCL_DYNAMIC_DEBUG_H
#define AMDKCL_DYNAMIC_DEBUG_H

#include <linux/dynamic_debug.h>

#ifndef DECLARE_DYNDBG_CLASSMAP
enum class_map_type {
	DD_CLASS_TYPE_DISJOINT_BITS,
	/**
	 * DD_CLASS_TYPE_DISJOINT_BITS: classes are independent, one per bit.
	 * expecting hex input. Built for drm.debug, basis for other types.
	 */
	DD_CLASS_TYPE_LEVEL_NUM,
	/**
	 * DD_CLASS_TYPE_LEVEL_NUM: input is numeric level, 0-N.
	 * N turns on just bits N-1 .. 0, so N=0 turns all bits off.
	 */
	DD_CLASS_TYPE_DISJOINT_NAMES,
	/**
	 * DD_CLASS_TYPE_DISJOINT_NAMES: input is a CSV of [+-]CLASS_NAMES,
	 * classes are independent, like _DISJOINT_BITS.
	 */
	DD_CLASS_TYPE_LEVEL_NAMES,
	/**
	 * DD_CLASS_TYPE_LEVEL_NAMES: input is a CSV of [+-]CLASS_NAMES,
	 * intended for names like: INFO,DEBUG,TRACE, with a module prefix
	 * avoid EMERG,ALERT,CRIT,ERR,WARNING: they're not debug
	 */
};

struct ddebug_class_map {
	struct list_head link;
	struct module *mod;
	const char *mod_name;	/* needed for builtins */
	const char **class_names;
	const int length;
	const int base;		/* index of 1st .class_id, allows split/shared space */
	enum class_map_type map_type;
};

/**
 * DECLARE_DYNDBG_CLASSMAP - declare classnames known by a module
 * @_var:   a struct ddebug_class_map, passed to module_param_cb
 * @_type:  enum class_map_type, chooses bits/verbose, numeric/symbolic
 * @_base:  offset of 1st class-name. splits .class_id space
 * @classes: class-names used to control class'd prdbgs
 */
#define DECLARE_DYNDBG_CLASSMAP(_var, _maptype, _base, ...)		\
	static const char *_var##_classnames[] = { __VA_ARGS__ };	\
	static struct ddebug_class_map __aligned(8) __used		\
		__section("__dyndbg_classes") _var = {			\
		.mod = THIS_MODULE,					\
		.mod_name = KBUILD_MODNAME,				\
		.base = _base,						\
		.map_type = _maptype,					\
		.length = NUM_TYPE_ARGS(char*, __VA_ARGS__),		\
		.class_names = _var##_classnames,			\
	}
#define NUM_TYPE_ARGS(eltype, ...)				\
        (sizeof((eltype[]){__VA_ARGS__}) / sizeof(eltype))

#endif

#if IS_ENABLED(CONFIG_DYNAMIC_DEBUG)
#ifndef _dynamic_func_call_no_desc
#define __dynamic_func_call_no_desc(id, fmt, func, ...) do {	\
        DEFINE_DYNAMIC_DEBUG_METADATA(id, fmt);                 \
        if (DYNAMIC_DEBUG_BRANCH(id))                           \
			func(__VA_ARGS__);                              	\
} while (0)

#define _dynamic_func_call_no_desc(fmt, func, ...)     \
       __dynamic_func_call_no_desc(__UNIQUE_ID(ddebug), fmt, func, ##__VA_ARGS__)
#endif /* _dynamic_func_call_no_desc */
#endif /* CONFIG_DYNAMIC_DEBUG */
#endif /* AMDKCL_DYNAMIC_DEBUG_H */
