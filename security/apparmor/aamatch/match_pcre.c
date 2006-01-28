/*
 *	Copyright (C) 2002-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	http://forge.novell.com/modules/xfmod/project/?apparmor
 *
 *	AppArmor aamatch submodule (w/ pattern expansion).
 *
 *	This module makes use of a slightly modified version of the PCRE
 *	library developed by Philip Hazel <ph10@cam.ac.uk>.  See the files
 *	pcre_* in this directory.
 */

#include <linux/module.h>
#include "match.h"
#include "pcre_exec.h"
#include "pcre_tables.h"

static const char *features="literal tailglob pattern=pcre";

struct sdmatch_entry
{
	char *pattern;
	pcre *compiled;
};

void* sdmatch_alloc(enum entry_t entry_type)
{
void *ptr=NULL;

	if (entry_type == sd_entry_pattern) {
		ptr = kmalloc(sizeof(struct sdmatch_entry), GFP_KERNEL);
		if (ptr)
			memset(ptr, 0, sizeof(struct sdmatch_entry));
		else
			ptr=ERR_PTR(-ENOMEM);
	} else if (entry_type != sd_entry_literal &&
		   entry_type != sd_entry_tailglob) {
		ptr = ERR_PTR(-EINVAL);
	}

	return ptr;
}

void sdmatch_free(void *ptr)
{
	if (ptr) {
		struct sdmatch_entry *ed = (struct sdmatch_entry *) ptr;
		kfree(ed->pattern);
		kfree(ed->compiled);	/* allocated by SD_READ_X */
	}
	kfree(ptr);
}

const char *sdmatch_features(void)
{
	return features;
}

int sdmatch_serialize(void *entry_extradata, struct sd_ext *e,
		      sdmatch_serializecb cb)
{
#define SD_READ_X(E, C, D, N) \
	do { \
		if (!cb((E), (C), (D), (N))) { \
			error = -EINVAL; \
			goto done; \
		}\
	} while (0)

	int error = 0;
	u32 size, magic, opts;
	u8 t_char;
	struct sdmatch_entry *ed = (struct sdmatch_entry *) entry_extradata;

	if (ed == NULL)
		goto done;

	SD_READ_X(e, SD_DYN_STRING, &ed->pattern, NULL);

	/* size determines the real size of the pcre struct,
	   it is size_t - sizeof(pcre) on user side.
	   uschar must be the same in user and kernel space */
	/* check that we are processing the correct structure */
	SD_READ_X(e, SD_STRUCT, NULL, "pcre");
	SD_READ_X(e, SD_U32, &size, "pattern.size");
	SD_READ_X(e, SD_U32, &magic, "pattern.magic");

	/* the allocation of pcre is delayed because it depends on the size
	 * of the pattern */
	ed->compiled = (pcre *) kmalloc(size + sizeof(pcre), GFP_KERNEL);
	if (!ed->compiled) {
		error = -ENOMEM;
		goto done;
	}

	memset(ed->compiled, 0, size + sizeof(pcre));
	ed->compiled->magic_number = magic;
	ed->compiled->size = size + sizeof(pcre);

	SD_READ_X(e, SD_U32, &opts, "pattern.options");
	ed->compiled->options = opts;
	SD_READ_X(e, SD_U16, &ed->compiled->top_bracket, "pattern.top_bracket");
	SD_READ_X(e, SD_U16, &ed->compiled->top_backref, "pattern.top_backref");
	SD_READ_X(e, SD_U8, &t_char, "pattern.first_char");
	ed->compiled->first_char = t_char;
	SD_READ_X(e, SD_U8, &t_char, "pattern.req_char");
	ed->compiled->req_char = t_char;
	SD_READ_X(e, SD_U8, &t_char, "pattern.code[0]");
	ed->compiled->code[0] = t_char;

	SD_READ_X(e, SD_STATIC_BLOB, &ed->compiled->code[1], NULL);

	SD_READ_X(e, SD_STRUCTEND, NULL, NULL);

	/* stitch in pcre patterns, it was NULLed out by parser
	 * pcre_default_tables defined in pcre_tables.h */
	ed->compiled->tables = pcre_default_tables;

done:
	if (error != 0 && ed) {
		kfree(ed->pattern); /* allocated by SD_READ_X */
		kfree(ed->compiled);
		ed->pattern = NULL;
		ed->compiled = NULL;
	}

	return error;
}

unsigned int sdmatch_match(const char *pathname, const char *entry_name,
			   enum entry_t entry_type, void *entry_extradata)
{
	int ret;

	if (entry_type == sd_entry_pattern) {
		int pcreret;
		struct sdmatch_entry *ed =
			(struct sdmatch_entry *) entry_extradata;

        	pcreret = pcre_exec(ed->compiled, NULL,
				    pathname, strlen(pathname),
			    	    0, 0, NULL, 0);

        	ret = (pcreret >= 0);

		// XXX - this needs access to subdomain_debug,  hmmm
        	//SD_DEBUG("%s(%d): %s %s %d\n", __FUNCTION__,
		//	 ret, pathname, ed->pattern, pcreret);
	} else {
		ret = sdmatch_match_common(pathname, entry_name, entry_type);
	}

        return ret;
}

EXPORT_SYMBOL_GPL(sdmatch_alloc);
EXPORT_SYMBOL_GPL(sdmatch_free);
EXPORT_SYMBOL_GPL(sdmatch_features);
EXPORT_SYMBOL_GPL(sdmatch_serialize);
EXPORT_SYMBOL_GPL(sdmatch_match);

MODULE_DESCRIPTION("AppArmor aa_match module [pcre]");
MODULE_AUTHOR("Tony Jones <tonyj@suse.de>");
MODULE_LICENSE("GPL");
