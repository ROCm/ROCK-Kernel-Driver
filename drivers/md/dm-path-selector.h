/*
 * Copyright (C) 2003 Sistina Software.
 *
 * Module Author: Heinz Mauelshagen
 *
 * This file is released under the GPL.
 *
 * Path-Selector interface/registration/unregistration definitions
 *
 */

#ifndef	DM_PATH_SELECTOR_H
#define	DM_PATH_SELECTOR_H

#include <linux/device-mapper.h>

struct path;

/*
 * We provide an abstraction for the code that chooses which path
 * to send some io down.
 */
struct path_selector_type;
struct path_selector {
	struct path_selector_type *type;
	void *context;
};

/*
 * Constructs a path selector object, takes custom arguments
 */
typedef int (*ps_ctr_fn) (struct path_selector *ps);
typedef void (*ps_dtr_fn) (struct path_selector *ps);

/*
 * Add an opaque path object, along with some selector specific
 * path args (eg, path priority).
 */
typedef	int (*ps_add_path_fn) (struct path_selector *ps,
			       struct path *path,
			       int argc, char **argv, char **error);

/*
 * Chooses a path for this io, if no paths are available then
 * NULL will be returned. The selector may set the map_info
 * object if it wishes, this will be fed back into the endio fn.
 *
 * Must ensure that _any_ dynamically allocated selection context is
 * reused or reallocated because an endio call (which needs to free it)
 * might happen after a couple of select calls.
 */
typedef	struct path *(*ps_select_path_fn) (struct path_selector *ps);

/*
 * Notify the selector that a path has failed.
 */
typedef	void (*ps_fail_path_fn) (struct path_selector *ps,
				 struct path *p);

/*
 * Table content based on parameters added in ps_add_path_fn
 * or path selector status
 */
typedef	int (*ps_status_fn) (struct path_selector *ps,
			     struct path *path,
			     status_type_t type,
			     char *result, unsigned int maxlen);

/* Information about a path selector type */
struct path_selector_type {
	char *name;
	unsigned int table_args;
	unsigned int info_args;
	ps_ctr_fn ctr;
	ps_dtr_fn dtr;

	ps_add_path_fn add_path;
	ps_fail_path_fn fail_path;
	ps_select_path_fn select_path;
	ps_status_fn status;
};

/*
 * FIXME: Factor out registration code.
 */

/* Register a path selector */
int dm_register_path_selector(struct path_selector_type *type);

/* Unregister a path selector */
int dm_unregister_path_selector(struct path_selector_type *type);

/* Returns a registered path selector type */
struct path_selector_type *dm_get_path_selector(const char *name);

/* Releases a path selector  */
void dm_put_path_selector(struct path_selector_type *pst);

/* FIXME: remove these */
int dm_register_path_selectors(void);
void dm_unregister_path_selectors(void);

#endif
