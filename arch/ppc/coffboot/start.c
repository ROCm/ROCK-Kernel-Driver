/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>

int (*prom)();

void *chosen_handle;
void *stdin;
void *stdout;
void *stderr;

void exit(void);
void *finddevice(const char *name);
int getprop(void *phandle, const char *name, void *buf, int buflen);
void printk(char *fmt, ...);

void
start(int a1, int a2, void *promptr)
{
    prom = (int (*)()) promptr;
    chosen_handle = finddevice("/chosen");
    if (chosen_handle == (void *) -1)
	exit();
    if (getprop(chosen_handle, "stdout", &stdout, sizeof(stdout)) != 4)
	exit();
    stderr = stdout;
    if (getprop(chosen_handle, "stdin", &stdin, sizeof(stdin)) != 4)
	exit();

    boot(a1, a2, promptr);
    for (;;)
	exit();
}

int
write(void *handle, void *ptr, int nb)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	void *ihandle;
	void *addr;
	int len;
	int actual;
    } args;

    args.service = "write";
    args.nargs = 3;
    args.nret = 1;
    args.ihandle = handle;
    args.addr = ptr;
    args.len = nb;
    args.actual = -1;
    (*prom)(&args);
    return args.actual;
}

int writestring(void *f, char *ptr, int nb)
{
	int w = 0, i;
	char *ret = "\r";

	for (i = 0; i < nb; ++i) {
		if (ptr[i] == '\n') {
			if (i > w) {
				write(f, ptr + w, i - w);
				w = i;
			}
			write(f, ret, 1);
		}
	}
	if (w < nb)
		write(f, ptr + w, nb - w);
	return nb;
}

int
read(void *handle, void *ptr, int nb)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	void *ihandle;
	void *addr;
	int len;
	int actual;
    } args;

    args.service = "read";
    args.nargs = 3;
    args.nret = 1;
    args.ihandle = handle;
    args.addr = ptr;
    args.len = nb;
    args.actual = -1;
    (*prom)(&args);
    return args.actual;
}

void
exit()
{
    struct prom_args {
	char *service;
    } args;

    for (;;) {
	args.service = "exit";
	(*prom)(&args);
    }
}

void
pause()
{
    struct prom_args {
	char *service;
    } args;

    args.service = "enter";
    (*prom)(&args);
}

void *
finddevice(const char *name)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	const char *devspec;
	void *phandle;
    } args;

    args.service = "finddevice";
    args.nargs = 1;
    args.nret = 1;
    args.devspec = name;
    args.phandle = (void *) -1;
    (*prom)(&args);
    return args.phandle;
}

void *
claim(unsigned int virt, unsigned int size, unsigned int align)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	unsigned int virt;
	unsigned int size;
	unsigned int align;
	void *ret;
    } args;

    args.service = "claim";
    args.nargs = 3;
    args.nret = 1;
    args.virt = virt;
    args.size = size;
    args.align = align;
    (*prom)(&args);
    return args.ret;
}

int
getprop(void *phandle, const char *name, void *buf, int buflen)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	void *phandle;
	const char *name;
	void *buf;
	int buflen;
	int size;
    } args;

    args.service = "getprop";
    args.nargs = 4;
    args.nret = 1;
    args.phandle = phandle;
    args.name = name;
    args.buf = buf;
    args.buflen = buflen;
    args.size = -1;
    (*prom)(&args);
    return args.size;
}

int
putc(int c, void *f)
{
    char ch = c;

    return writestring(f, &ch, 1) == 1? c: -1;
}

int
putchar(int c)
{
    return putc(c, stdout);
}

int
fputs(char *str, void *f)
{
    int n = strlen(str);

    return writestring(f, str, n) == n? 0: -1;
}

int
readchar()
{
    char ch;

    for (;;) {
	switch (read(stdin, &ch, 1)) {
	case 1:
	    return ch;
	case -1:
	    printk("read(stdin) returned -1\n");
	    return -1;
	}
    }
}

static char line[256];
static char *lineptr;
static int lineleft;

int
getchar()
{
    int c;

    if (lineleft == 0) {
	lineptr = line;
	for (;;) {
	    c = readchar();
	    if (c == -1 || c == 4)
		break;
	    if (c == '\r' || c == '\n') {
		*lineptr++ = '\n';
		putchar('\n');
		break;
	    }
	    switch (c) {
	    case 0177:
	    case '\b':
		if (lineptr > line) {
		    putchar('\b');
		    putchar(' ');
		    putchar('\b');
		    --lineptr;
		}
		break;
	    case 'U' & 0x1F:
		while (lineptr > line) {
		    putchar('\b');
		    putchar(' ');
		    putchar('\b');
		    --lineptr;
		}
		break;
	    default:
		if (lineptr >= &line[sizeof(line) - 1])
		    putchar('\a');
		else {
		    putchar(c);
		    *lineptr++ = c;
		}
	    }
	}
	lineleft = lineptr - line;
	lineptr = line;
    }
    if (lineleft == 0)
	return -1;
    --lineleft;
    return *lineptr++;
}

extern int vsprintf(char *buf, const char *fmt, va_list args);
static char sprint_buf[1024];

void
printk(char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsprintf(sprint_buf, fmt, args);
	va_end(args);
	writestring(stdout, sprint_buf, n);
}

int
printf(char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsprintf(sprint_buf, fmt, args);
	va_end(args);
	writestring(stdout, sprint_buf, n);
	return n;
}
