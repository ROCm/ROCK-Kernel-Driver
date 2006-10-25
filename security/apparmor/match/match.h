/*
 *	Copyright (C) 2002-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor submodule (match) prototypes
 */

#ifndef __MATCH_H
#define __MATCH_H

#include "../module_interface.h"
#include "../apparmor.h"

/* The following functions implement an interface used by the primary
 * AppArmor module to perform name matching (n.b. "AppArmor" was previously
 * called "SubDomain").

 * aamatch_alloc
 * aamatch_free
 * aamatch_features
 * aamatch_serialize
 * aamatch_match
 *
 * The intent is for the primary module to export (via virtual fs entries)
 * the features provided by the submodule (aamatch_features) so that the
 * parser may only load policy that can be supported.
 *
 * The primary module will call aamatch_serialize to allow the submodule
 * to consume submodule specific data from parser data stream and will call
 * aamatch_match to determine if a pathname matches an aa_entry.
 */

typedef int (*aamatch_serializecb)
	(struct aa_ext *, enum aa_code, void *, const char *);

/**
 * aamatch_alloc: allocate extradata (if necessary)
 * @type: type of entry being allocated
 * Return value: NULL indicates no data was allocated (ERR_PTR(x) on error)
 */
extern void* aamatch_alloc(enum entry_match_type type);

/**
 * aamatch_free: release data allocated by aamatch_alloc
 * @entry_extradata: data previously allocated by aamatch_alloc
 */
extern void aamatch_free(void *entry_extradata);

/**
 * aamatch_features: return match types supported
 * Return value: space seperated string (of types supported - use type=value
 * to indicate variants of a type)
 */
extern const char* aamatch_features(void);

/**
 * aamatch_serialize: serialize extradata
 * @entry_extradata: data previously allocated by aamatch_alloc
 * @e: input stream
 * @cb: callback fn (consume incoming data stream)
 * Return value: 0 success, -ve error
 */
extern int aamatch_serialize(void *entry_extradata, struct aa_ext *e,
			     aamatch_serializecb cb);

/**
 * aamatch_match: determine if pathname matches entry
 * @pathname: pathname to verify
 * @entry_name: entry name
 * @type: type of entry
 * @entry_extradata: data previously allocated by aamatch_alloc
 * Return value: 1 match, 0 othersise
 */
extern unsigned int aamatch_match(const char *pathname, const char *entry_name,
				  enum entry_match_type type,
				  void *entry_extradata);


/**
 * sd_getmatch_type - return string representation of entry_match_type
 * @type: entry match type
 */
static inline const char *sd_getmatch_type(enum entry_match_type type)
{
	const char *names[] = {
		"aa_entry_literal",
		"aa_entry_tailglob",
		"aa_entry_pattern",
		"aa_entry_invalid"
	};

	if (type >= aa_entry_invalid) {
		type = aa_entry_invalid;
	}

	return names[type];
}

/**
 * aamatch_match_common - helper function to check if a pathname matches
 * a literal/tailglob
 * @path: path requested to search for
 * @entry_name: name from aa_entry
 * @type: type of entry
 */
static inline int aamatch_match_common(const char *path,
					   const char *entry_name,
			   		   enum entry_match_type type)
{
	int retval;

	/* literal, no pattern matching characters */
	if (type == aa_entry_literal) {
		retval = (strcmp(entry_name, path) == 0);
	/* trailing ** glob pattern */
	} else if (type == aa_entry_tailglob) {
		retval = (strncmp(entry_name, path,
				  strlen(entry_name) - 2) == 0);
	} else {
		AA_WARN("%s: Invalid entry_match_type %d\n",
			__FUNCTION__, type);
		retval = 0;
	}

	return retval;
}

#endif /* __MATCH_H */
