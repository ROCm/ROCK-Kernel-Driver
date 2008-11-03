/*
 * Copyright (C) 2007,2008 Red Hat, Inc. All rights reserved.
 *
 * Module Author: Heinz Mauelshagen <Mauelshagen@RedHat.de>
 *
 * General device-mapper message interface argument parser.
 *
 * This file is released under the GPL.
 *
 */

#ifndef DM_MESSAGE_H
#define DM_MESSAGE_H

/* Factor out to dm.h. */
/* Reference to array end. */
#define ARRAY_END(a)    ((a) + ARRAY_SIZE(a))

/* Message return bits. */
enum dm_message_return {
	dm_msg_ret_ambiguous,		/* Action ambiguous. */
	dm_msg_ret_inval,		/* Action invalid. */
	dm_msg_ret_undef,		/* Action undefined. */

	dm_msg_ret_option,		/* Option error. */
	dm_msg_ret_arg,			/* Argument error. */
	dm_msg_ret_argcount,		/* Argument count error. */
};

/* Message argument type conversions. */
enum dm_message_argument_type {
	dm_msg_base_t,		/* Basename string. */
	dm_msg_str_t,		/* String. */
	dm_msg_int_t,		/* Signed int. */
	dm_msg_uint_t,		/* Unsigned int. */
	dm_msg_uint64_t,	/* Unsigned int 64. */
};

/* A message option. */
struct dm_message_option {
	unsigned num_options;
	char **options;
	unsigned long *actions;
};

/* Message arguments and types. */
struct dm_message_argument {
	unsigned num_args;
	unsigned long **ptr;
	enum dm_message_argument_type types[];
};

/* Client message. */
struct dm_msg {
	unsigned long action;		/* Identified action. */
	unsigned long ret;		/* Return bits. */
	unsigned num_specs;		/* # of sepcifications listed. */
	struct dm_msg_spec *specs;	/* Specification list. */
	struct dm_msg_spec *spec;	/* Specification selected. */
};

/* Secification of the message. */
struct dm_msg_spec {
	const char *cmd;	/* Name of the command (i.e. 'bandwidth'). */
	unsigned long action;
	struct dm_message_option *options;
	struct dm_message_argument *args;
	unsigned long parm;	/* Parameter to pass through to callback. */
	/* Function to process for action. */
	int (*f) (struct dm_msg *msg, void *context);
};

/* Parameter access macros. */
#define	DM_MSG_PARM(msg) ((msg)->spec->parm)

#define	DM_MSG_STR_ARGS(msg, idx) ((char *) *(msg)->spec->args->ptr[idx])
#define	DM_MSG_INT_ARGS(msg, idx) ((int) *(msg)->spec->args->ptr[idx])
#define	DM_MSG_UINT_ARGS(msg, idx) ((unsigned) DM_MSG_INT_ARG(msg, idx))
#define	DM_MSG_UINT64_ARGS(msg, idx) ((uint64_t)  *(msg)->spec->args->ptr[idx])

#define	DM_MSG_STR_ARG(msg)	DM_MSG_STR_ARGS(msg, 0)
#define	DM_MSG_INT_ARG(msg)	DM_MSG_INT_ARGS(msg, 0)
#define	DM_MSG_UINT_ARG(msg)	DM_MSG_UINT_ARGS(msg, 0)
#define	DM_MSG_UINT64_ARG(msg)	DM_MSG_UINT64_ARGS(msg, 0)


/* Parse a message and its options and optionally call a function back. */
int dm_message_parse(const char *caller, struct dm_msg *msg, void *context,
		     int argc, char **argv);

#endif
