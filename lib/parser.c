/*
 * lib/parser.c - simple parser for mount, etc. options.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/string.h>

static int match_one(char *s, char *p, substring_t args[])
{
	char *meta;
	int argc = 0;

	if (!p)
		return 1;

	while(1) {
		int len = -1;
		meta = strchr(p, '%');
		if (!meta)
			return strcmp(p, s) == 0;

		if (strncmp(p, s, meta-p))
			return 0;

		s += meta - p;
		p = meta + 1;

		if (isdigit(*p))
			len = simple_strtoul(p, &p, 10);
		else if (*p == '%') {
			if (*s++ != '%')
				return 0;
			continue;
		}

		if (argc >= MAX_OPT_ARGS)
			return 0;

		args[argc].from = s;
		switch (*p++) {
		case 's':
			if (strlen(s) == 0)
				return 0;
			else if (len == -1 || len > strlen(s))
				len = strlen(s);
			args[argc].to = s + len;
			break;
		case 'd':
			simple_strtol(s, &args[argc].to, 0);
			goto num;
		case 'u':
			simple_strtoul(s, &args[argc].to, 0);
			goto num;
		case 'o':
			simple_strtoul(s, &args[argc].to, 8);
			goto num;
		case 'x':
			simple_strtoul(s, &args[argc].to, 16);
		num:
			if (args[argc].to == args[argc].from)
				return 0;
			break;
		default:
			return 0;
		}
		s = args[argc].to;
		argc++;
	}
}

int match_token(char *s, match_table_t table, substring_t args[])
{
	struct match_token *p;

	for (p = table; !match_one(s, p->pattern, args) ; p++)
		;

	return p->token;
}

static int match_number(substring_t *s, int *result, int base)
{
	char *endp;
	char *buf;
	int ret;

	buf = kmalloc(s->to - s->from + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, s->from, s->to - s->from);
	buf[s->to - s->from] = '\0';
	*result = simple_strtol(buf, &endp, base);
	ret = 0;
	if (endp == buf)
		ret = -EINVAL;
	kfree(buf);
	return ret;
}

int match_int(substring_t *s, int *result)
{
	return match_number(s, result, 0);
}

int match_octal(substring_t *s, int *result)
{
	return match_number(s, result, 8);
}

int match_hex(substring_t *s, int *result)
{
	return match_number(s, result, 16);
}

void match_strcpy(char *to, substring_t *s)
{
	memcpy(to, s->from, s->to - s->from);
	to[s->to - s->from] = '\0';
}

char *match_strdup(substring_t *s)
{
	char *p = kmalloc(s->to - s->from + 1, GFP_KERNEL);
	if (p)
		match_strcpy(p, s);
	return p;
}

EXPORT_SYMBOL(match_token);
EXPORT_SYMBOL(match_int);
EXPORT_SYMBOL(match_octal);
EXPORT_SYMBOL(match_hex);
EXPORT_SYMBOL(match_strcpy);
EXPORT_SYMBOL(match_strdup);
