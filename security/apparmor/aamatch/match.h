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

 * sdmatch_alloc
 * sdmatch_free
 * sdmatch_features
 * sdmatch_serialize
 * sdmatch_match
 *
 * The intent is for the primary module to export (via virtual fs entries)
 * the features provided by the submodule (sdmatch_features) so that the
 * parser may only load policy that can be supported.
 *
 * The primary module will call sdmatch_serialize to allow the submodule
 * to consume submodule specific data from parser data stream and will call
 * sdmatch_match to determine if a pathname matches an sd_entry.
 */

typedef int (*sdmatch_serializecb)
	(struct sd_ext *, enum sd_code, void *, const char *);

/**
 * sdmatch_alloc: allocate extradata (if necessary)
 * @entry_type: type of entry being allocated
 * Return value: NULL indicates no data was allocated (ERR_PTR(x) on error)
 */
extern void* sdmatch_alloc(enum entry_t entry_type);

/**
 * sdmatch_free: release data allocated by sdmatch_alloc
 * @entry_extradata: data previously allocated by sdmatch_alloc
 */
extern void sdmatch_free(void *entry_extradata);

/**
 * sdmatch_features: return match types supported
 * Return value: space seperated string (of types supported - use type=value
 * to indicate variants of a type)
 */
extern const char* sdmatch_features(void);

/**
 * sdmatch_serialize: serialize extradata
 * @entry_extradata: data previously allocated by sdmatch_alloc
 * @e: input stream
 * @cb: callback fn (consume incoming data stream)
 * Return value: 0 success, -ve error
 */
extern int sdmatch_serialize(void *entry_extradata, struct sd_ext *e,
			     sdmatch_serializecb cb);

/**
 * sdmatch_match: determine if pathname matches entry
 * @pathname: pathname to verify
 * @entry_name: entry name
 * @entry_type: type of entry
 * @entry_extradata: data previously allocated by sdmatch_alloc
 * Return value: 1 match, 0 othersise
 */
extern unsigned int sdmatch_match(const char *pathname, const char *entry_name,
				  enum entry_t entry_type,
				  void *entry_extradata);


/**
 * sd_getentry_type - return string representation of entry_t
 * @etype: entry type
 */
static inline const char *sd_getentry_type(enum entry_t etype)
{
	const char *etype_names[] = {
		"sd_entry_literal",
		"sd_entry_tailglob",
		"sd_entry_pattern",
		"sd_entry_invalid"
	};

	if (etype >= sd_entry_invalid) {
		etype = sd_entry_invalid;
	}

	return etype_names[etype];
}

/**
 * sdmatch_match_common - helper function to check if a pathname matches
 * a literal/tailglob
 * @path: path requested to search for
 * @entry_name: name from sd_entry
 * @etype: type of entry
 */
static inline int sdmatch_match_common(const char *path,
					   const char *entry_name,
			   		   enum entry_t etype)
{
	int retval;

	/* literal, no pattern matching characters */
	if (etype == sd_entry_literal) {
		retval = (strcmp(entry_name, path) == 0);
	/* trailing ** glob pattern */
	} else if (etype == sd_entry_tailglob) {
		retval = (strncmp(entry_name, path,
				  strlen(entry_name) - 2) == 0);
	} else {
		SD_WARN("%s: Invalid entry_t %d\n", __FUNCTION__, etype);
		retval = 0;
	}

#if 0
	SD_DEBUG("%s(%d): %s %s [%s]\n",
		__FUNCTION__, retval, path, entry_name,
		sd_getentry_type(etype));
#endif

	return retval;
}

#endif /* __MATCH_H */
