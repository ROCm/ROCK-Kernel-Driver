/*
 * Copyright (C) 2007  Red Hat GmbH
 *
 * Module Author: Heinz Mauelshagen <Mauelshagen@RedHat.de>
 *
 * This file is released under the GPL.
 *
 * device-mapper message parser.
 *
 */

#include "dm.h"
#include "dm-message.h"
#include <linux/kernel.h>

#define DM_MSG_PREFIX	"dm_message"

/* Basename of a path. */
static inline char *
basename(char *s)
{
	char *p = strrchr(s, '/');

	return p ? p + 1 : s;
}

/* Get an argument depending on type. */
static void
message_arguments(struct dm_msg *msg, int argc, char **argv)
{

	if (argc) {
		int i;
		struct dm_message_argument *args = msg->spec->args;

		for (i = 0; i < args->num_args; i++) {
			int r;
			unsigned long **ptr = args->ptr;
			enum dm_message_argument_type type = args->types[i];

			switch (type) {
			case dm_msg_base_t:
				((char **) ptr)[i] = basename(argv[i]);
				break;

			case dm_msg_str_t:
				((char **) ptr)[i] = argv[i];
				break;

			case dm_msg_int_t:
				r = sscanf(argv[i], "%d", ((int **) ptr)[i]);
				goto check;

			case dm_msg_uint_t:
				r = sscanf(argv[i], "%u",
					   ((unsigned **) ptr)[i]);
				goto check;

			case dm_msg_uint64_t:
				r = sscanf(argv[i], "%llu",
					   ((unsigned long long **) ptr)[i]);

   check:
				if (r != 1) {
					set_bit(dm_msg_ret_undef, &msg->ret);
					set_bit(dm_msg_ret_arg, &msg->ret);
				}
			}
		}
	}
}

/* Parse message options. */
static void
message_options_parse(struct dm_msg *msg, int argc, char **argv)
{
	int hit = 0;
	unsigned long *action;
	size_t l1 = strlen(*argv), l_hit = 0;
	struct dm_message_option *o = msg->spec->options;
	char **option, **option_end = o->options + o->num_options;

	for (option = o->options, action = o->actions;
	     option < option_end; option++, action++) {
		size_t l2 = strlen(*option);

		if (!strnicmp(*argv, *option, min(l1, l2))) {
			hit++;
			l_hit = l2;
			set_bit(*action, &msg->action);
		}
	}

	/* Assume error. */
	msg->ret = 0;
	set_bit(dm_msg_ret_option, &msg->ret);
	if (!hit || l1 > l_hit)
		set_bit(dm_msg_ret_undef, &msg->ret);	/* Undefined option. */
	else if (hit > 1)
		set_bit(dm_msg_ret_ambiguous, &msg->ret); /* Ambiguous option.*/
	else {
		clear_bit(dm_msg_ret_option, &msg->ret); /* Option OK. */
		message_arguments(msg, --argc, ++argv);
	}
}

static inline void
print_ret(const char *caller, unsigned long ret)
{
	struct {
		unsigned long err;
		const char *err_str;
	} static err_msg[] = {
		{ dm_msg_ret_ambiguous, "message ambiguous" },
		{ dm_msg_ret_inval, "message invalid" },
		{ dm_msg_ret_undef, "message undefined" },
		{ dm_msg_ret_arg, "message argument" },
		{ dm_msg_ret_argcount, "message argument count" },
		{ dm_msg_ret_option, "option" },
	}, *e = ARRAY_END(err_msg);

	while (e-- > err_msg) {
		if (test_bit(e->err, &ret))
			DMERR("%s %s", caller, e->err_str);
	}
}

/* Parse a message action. */
int
dm_message_parse(const char *caller, struct dm_msg *msg, void *context,
		 int argc, char **argv)
{
	int hit = 0;
	size_t l1 = strlen(*argv), l_hit = 0;
	struct dm_msg_spec *s, *s_hit = NULL,
			   *s_end = msg->specs + msg->num_specs;

	if (argc < 2)
		return -EINVAL;

	for (s = msg->specs; s < s_end; s++) {
		size_t l2 = strlen(s->cmd);

		if (!strnicmp(*argv, s->cmd, min(l1, l2))) {
			hit++;
			l_hit = l2;
			s_hit = s;
		}
	}

	msg->ret = 0;
	if (!hit || l1 > l_hit)	/* No hit or message string too long. */
		set_bit(dm_msg_ret_undef, &msg->ret);
	else if (hit > 1)	/* Ambiguous message. */
		set_bit(dm_msg_ret_ambiguous, &msg->ret);
	else if (argc - 2 != s_hit->args->num_args) {
		set_bit(dm_msg_ret_undef, &msg->ret);
		set_bit(dm_msg_ret_argcount, &msg->ret);
	}

	if (msg->ret)
		goto bad;

	msg->action = 0;
	msg->spec = s_hit;
	set_bit(s_hit->action, &msg->action);
	message_options_parse(msg, --argc, ++argv);

	if (!msg->ret)
		return msg->spec->f(msg, context);

   bad:
	print_ret(caller, msg->ret);
	return -EINVAL;
}
EXPORT_SYMBOL(dm_message_parse);

MODULE_DESCRIPTION(DM_NAME " device-mapper target message parser");
MODULE_AUTHOR("Heinz Mauelshagen <hjm@redhat.com>");
MODULE_LICENSE("GPL");
