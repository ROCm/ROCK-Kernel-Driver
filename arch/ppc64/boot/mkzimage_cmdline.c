/*
 * a little tool to modify the cmdline inside a zImage
 * Olaf Hering <olh@suse.de>  Copyright (C) 2003, 2004
 */

/*
	2003-10-02, version 1 
	2003-11-15, version 2: fix short reads if the string is at the end of the file
	2004-08-07, version 3: use mmap
 */
/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#define MY_VERSION 3

static int activate;
static int clear;
static int set;
static char *string;
static char *filename;

static const char cmdline_start[] = "cmd_line_start";
static const char cmdline_end[] = "cmd_line_end";

static void my_version(void)
{
	printf("version: %d\n", MY_VERSION);
	printf("(C) SuSE Linux AG, Nuernberg, Germany, 2003, 2004\n");
	return;
}

static void my_rtfm(const char *app)
{
	printf("modify the built-in cmdline of a CHRP boot image\n");
	printf("%s filename\n", app);
	printf("work with zImage named 'filename'\n");
	printf(" [-h] display this help\n");
	printf(" [-v] display version\n");
	printf(" [-a 0|1] disable/enable built-in cmdline\n");
	printf("          overrides whatever is passed from OpenFirmware\n");
	printf(" [-s STRING] store STRING in the boot image\n");
	printf(" [-c] clear previous content before update\n");
	printf(" no option will show the current settings in 'filename'\n");
	return;
}

int main(int argc, char **argv)
{
	struct stat sb;
	int fd, found;
	unsigned char *p, *s, *e, *tmp, *active;

	if (argc < 2) {
		my_rtfm(argv[0]);
		exit(1);
	}

	while (1) {
		int i;
		i = getopt(argc, argv, "a:hcvs:");
		if (i == -1)
			break;
		switch (i) {
		case 'a':
			if (*optarg == '0')
				activate = -1;
			else
				activate = 1;
			break;
		case 'c':
			clear = 1;
			break;
		case 'h':
			my_rtfm(argv[0]);
			exit(0);
		case 's':
			string = strdup(optarg);
			if (!string) {
				fprintf(stderr, "set: no mem\n");
				exit(1);
			}
			set = 1;
			break;
		case 'v':
			my_version();
			exit(0);
		default:
			printf("unknown option\n");
			my_rtfm(argv[0]);
			exit(1);
		}
	}
	if (argc <= optind) {
		fprintf(stderr, "filename required\n");
		exit(1);
	}
	filename = strdup(argv[optind]);
	if (!filename) {
		fprintf(stderr, "no mem\n");
		exit(1);
	}

	fd = open(filename, (activate || clear || set) ? O_RDWR : O_RDONLY);
	if (fd == -1)
		goto error;
	found = stat(filename, &sb);
	if (found < 0)
		goto error;
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "%s is not a file\n", filename);
		exit(1);
	}

	p = mmap(NULL, sb.st_size,
		 ((activate || clear || set) ?
		  PROT_WRITE : 0) | PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		goto error;
	s = p;
	e = p + sb.st_size - sizeof(cmdline_start) - sizeof(cmdline_end);
	found = 0;
	while (s < e) {
		if (memcmp(++s, cmdline_start, sizeof(cmdline_start) - 1) != 0)
			continue;
		found = 1;
		break;
	}
	if (!found)
		goto no_start;
	found = 0;

	active = s - 1;
	tmp = s = s + sizeof(cmdline_start) - 1;
	e = p + sb.st_size - sizeof(cmdline_end);
	while (tmp < e) {
		if (memcmp(++tmp, cmdline_end, sizeof(cmdline_end)) != 0)
			continue;
		found = 1;
		break;
	}
	if (!found)
		goto no_end;

	if (activate || clear || set) {
		if (activate)
			*active = activate > 0 ? '1' : '0';
		if (clear)
			memset(s, 0x0, tmp - s);
		if (set)
			snprintf(s, tmp - s, "%s", string);
	} else {
		fprintf(stdout, "cmd_line size:%d\n", tmp - s);
		fprintf(stdout, "cmd_line: %s\n", s);
		fprintf(stdout, "active: %c\n", *active);
	}

	munmap(p, sb.st_size);
	close(fd);
	return 0;

      error:
	perror(filename);
	return 1;
      no_start:
	fprintf(stderr, "%s: %s not found.\n", filename, cmdline_start);
	return 1;
      no_end:
	fprintf(stderr, "%s: %s not found.\n", filename, cmdline_end);
	return 1;
}
