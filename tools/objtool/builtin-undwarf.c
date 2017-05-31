/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * objtool undwarf:
 *
 * This command analyzes a .o file and adds an .undwarf section to it, which is
 * used by the in-kernel "undwarf" unwinder.
 *
 * This command is a superset of "objtool check".
 */

#include <string.h>
#include <subcmd/parse-options.h>
#include "builtin.h"
#include "check.h"


static const char *undwarf_usage[] = {
	"objtool undwarf generate [<options>] file.o",
	"objtool undwarf dump file.o",
	NULL,
};

extern const struct option check_options[];
extern bool nofp;

int cmd_undwarf(int argc, const char **argv)
{
	const char *objname;

	argc--; argv++;
	if (!strncmp(argv[0], "gen", 3)) {
		argc = parse_options(argc, argv, check_options, undwarf_usage, 0);
		if (argc != 1)
			usage_with_options(undwarf_usage, check_options);

		objname = argv[0];

		return check(objname, nofp, true);

	}

	if (!strcmp(argv[0], "dump")) {
		if (argc != 2)
			usage_with_options(undwarf_usage, check_options);

		objname = argv[1];

		return undwarf_dump(objname);
	}

	usage_with_options(undwarf_usage, check_options);

	return 0;
}
