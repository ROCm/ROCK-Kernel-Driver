/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

const char conf_def_filename[] = ".config";
char conf_filename[PATH_MAX+1];

const char conf_defname[] = "arch/$ARCH/defconfig";

const char *conf_confnames[] = {
	".config",
	"/lib/modules/$UNAME_RELEASE/.config",
	"/etc/kernel-config",
	"/boot/config-$UNAME_RELEASE",
	conf_defname,
	NULL,
};

static char *conf_expand_value(const char *in)
{
	struct symbol *sym;
	const char *src;
	static char res_value[SYMBOL_MAXLENGTH];
	char *dst, name[SYMBOL_MAXLENGTH];

	res_value[0] = 0;
	dst = name;
	while ((src = strchr(in, '$'))) {
		strncat(res_value, in, src - in);
		src++;
		dst = name;
		while (isalnum(*src) || *src == '_')
			*dst++ = *src++;
		*dst = 0;
		sym = sym_lookup(name, 0);
		sym_calc_value(sym);
		strcat(res_value, sym_get_string_value(sym));
		in = src;
	}
	strcat(res_value, in);

	return res_value;
}

char *conf_get_default_confname(void)
{
	return conf_expand_value(conf_defname);
}

int conf_read(const char *name)
{
	FILE *in = NULL;
	char line[128];
	char *p, *p2;
	int lineno = 0;
	struct symbol *sym;
	struct property *prop;
	struct expr *e;
	int i;

	if (name) {
		in = fopen(name, "r");
		if (in)
			strcpy(conf_filename, name);
	} else {
		const char **names = conf_confnames;
		while ((name = *names++)) {
			name = conf_expand_value(name);
			in = fopen(name, "r");
			if (in) {
				printf("#\n"
				       "# using defaults found in %s\n"
				       "#\n", name);
				break;
			}
		}
	}

	if (!in)
		return 1;

	for_all_symbols(i, sym) {
		sym->flags |= SYMBOL_NEW;
		switch (sym->type) {
		case S_INT:
		case S_HEX:
		case S_STRING:
			if (S_VAL(sym->def)) {
				free(S_VAL(sym->def));
				S_VAL(sym->def) = NULL;
			}
		default:
			;
		}
	}

	while (fgets(line, 128, in)) {
		lineno++;
		switch (line[0]) {
		case '#':
			if (memcmp(line + 2, "CONFIG_", 7))
				continue;
			p = strchr(line + 9, ' ');
			if (!p)
				continue;
			*p++ = 0;
			if (strncmp(p, "is not set", 10))
				continue;
			//printf("%s -> n\n", line + 9);
			sym = sym_lookup(line + 9, 0);
			switch (sym->type) {
			case S_BOOLEAN:
			case S_TRISTATE:
				sym->def = symbol_no.curr;
				sym->flags &= ~SYMBOL_NEW;
				break;
			default:
				;
			}
			break;
		case 'C':
			if (memcmp(line, "CONFIG_", 7))
				continue;
			p = strchr(line + 7, '=');
			if (!p)
				continue;
			*p++ = 0;
			p2 = strchr(p, '\n');
			if (p2)
				*p2 = 0;
			//printf("%s -> %s\n", line + 7, p);
			sym = sym_find(line + 7);
			if (!sym) {
				fprintf(stderr, "%s:%d: trying to assign nonexistent symbol %s\n", name, lineno, line + 7);
				break;
			}
			switch (sym->type) {
			case S_BOOLEAN:
				sym->def = symbol_yes.curr;
				sym->flags &= ~SYMBOL_NEW;
				break;
			case S_TRISTATE:
				if (p[0] == 'm')
					sym->def = symbol_mod.curr;
				else
					sym->def = symbol_yes.curr;
				sym->flags &= ~SYMBOL_NEW;
				break;
			case S_STRING:
				if (*p++ != '"')
					break;
				for (p2 = p; (p2 = strpbrk(p2, "\"\\")); p2++) {
					if (*p2 == '"') {
						*p2 = 0;
						break;
					}
					memmove(p2, p2 + 1, strlen(p2));
				}
			case S_INT:
			case S_HEX:
				if (sym_string_valid(sym, p)) {
					S_VAL(sym->def) = strdup(p);
					sym->flags &= ~SYMBOL_NEW;
				} else
					fprintf(stderr, "%s:%d:symbol value '%s' invalid for %s\n", name, lineno, p, sym->name);
				break;
			default:
				;
			}
			if (sym_is_choice_value(sym)) {
				prop = sym_get_choice_prop(sym);
				switch (S_TRI(sym->def)) {
				case mod:
					if (S_TRI(prop->def->def) == yes)
						/* warn? */;
					break;
				case yes:
					if (S_TRI(prop->def->def) != no)
						/* warn? */;
					S_VAL(prop->def->def) = sym;
					break;
				case no:
					break;
				}
				S_TRI(prop->def->def) = S_TRI(sym->def);
			}
			break;
		case '\n':
			break;
		default:
			continue;
		}
	}
	fclose(in);

	for_all_symbols(i, sym) {
		if (!sym_is_choice(sym))
			continue;
		prop = sym_get_choice_prop(sym);
		sym->flags &= ~SYMBOL_NEW;
		for (e = prop->dep; e; e = e->left.expr)
			sym->flags |= e->right.sym->flags & SYMBOL_NEW;
	}

	sym_change_count = 1;

	return 0;
}

int conf_write(const char *name)
{
	FILE *out, *out_h;
	struct symbol *sym;
	struct menu *menu;
	char oldname[128];
	int type, l;
	const char *str;

	out = fopen(".tmpconfig", "w");
	if (!out)
		return 1;
	out_h = fopen(".tmpconfig.h", "w");
	if (!out_h)
		return 1;
	fprintf(out, "#\n"
		     "# Automatically generated make config: don't edit\n"
		     "#\n");
	fprintf(out_h, "/*\n"
		       " * Automatically generated C config: don't edit\n"
		       " */\n"
		       "#define AUTOCONF_INCLUDED\n");

	if (!sym_change_count)
		sym_clear_all_valid();

	menu = rootmenu.list;
	while (menu) {
		sym = menu->sym;
		if (!sym) {
			if (!menu_is_visible(menu))
				goto next;
			str = menu_get_prompt(menu);
			fprintf(out, "\n"
				     "#\n"
				     "# %s\n"
				     "#\n", str);
			fprintf(out_h, "\n"
				       "/*\n"
				       " * %s\n"
				       " */\n", str);
		} else if (!(sym->flags & SYMBOL_CHOICE)) {
			sym_calc_value(sym);
			if (!(sym->flags & SYMBOL_WRITE))
				goto next;
			sym->flags &= ~SYMBOL_WRITE;
			type = sym->type;
			if (type == S_TRISTATE) {
				sym_calc_value(modules_sym);
				if (S_TRI(modules_sym->curr) == no)
					type = S_BOOLEAN;
			}
			switch (type) {
			case S_BOOLEAN:
			case S_TRISTATE:
				switch (sym_get_tristate_value(sym)) {
				case no:
					fprintf(out, "# CONFIG_%s is not set\n", sym->name);
					fprintf(out_h, "#undef CONFIG_%s\n", sym->name);
					break;
				case mod:
					fprintf(out, "CONFIG_%s=m\n", sym->name);
					fprintf(out_h, "#define CONFIG_%s_MODULE 1\n", sym->name);
					break;
				case yes:
					fprintf(out, "CONFIG_%s=y\n", sym->name);
					fprintf(out_h, "#define CONFIG_%s 1\n", sym->name);
					break;
				}
				break;
			case S_STRING:
				// fix me
				str = sym_get_string_value(sym);
				fprintf(out, "CONFIG_%s=\"", sym->name);
				fprintf(out_h, "#define CONFIG_%s \"", sym->name);
				do {
					l = strcspn(str, "\"\\");
					if (l) {
						fwrite(str, l, 1, out);
						fwrite(str, l, 1, out_h);
					}
					str += l;
					while (*str == '\\' || *str == '"') {
						fprintf(out, "\\%c", *str);
						fprintf(out_h, "\\%c", *str);
						str++;
					}
				} while (*str);
				fputs("\"\n", out);
				fputs("\"\n", out_h);
				break;
			case S_HEX:
				str = sym_get_string_value(sym);
				if (str[0] != '0' || (str[1] != 'x' && str[1] != 'X')) {
					fprintf(out, "CONFIG_%s=%s\n", sym->name, str);
					fprintf(out_h, "#define CONFIG_%s 0x%s\n", sym->name, str);
					break;
				}
			case S_INT:
				str = sym_get_string_value(sym);
				fprintf(out, "CONFIG_%s=%s\n", sym->name, str);
				fprintf(out_h, "#define CONFIG_%s %s\n", sym->name, str);
				break;
			}
		}

	next:
		if (menu->list) {
			menu = menu->list;
			continue;
		}
		if (menu->next)
			menu = menu->next;
		else while ((menu = menu->parent)) {
			if (menu->next) {
				menu = menu->next;
				break;
			}
		}
	}
	fclose(out);
	fclose(out_h);

	if (!name) {
		rename(".tmpconfig.h", "include/linux/autoconf.h");
		name = conf_def_filename;
		file_write_dep(NULL);
	} else
		unlink(".tmpconfig.h");

	sprintf(oldname, "%s.old", name);
	rename(name, oldname);
	if (rename(".tmpconfig", name))
		return 1;
	strcpy(conf_filename, name);

	sym_change_count = 0;

	return 0;
}
