/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
typedef int	FILE;
extern FILE *stdin, *stdout;
#define NULL	((void *)0)
#define EOF	(-1)
#define fopen(n, m)	NULL
#define fflush(f)	0
#define fclose(f)	0
extern char *fgets();

#define perror(s)	printf("%s: no files!\n", (s))
