/*
 *
 *
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#define SIZE 1024
#define BLOCK_ALIGN(x)  (((x)+SIZE-1)&(~(SIZE-1)))

static void
die(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);
	exit(1);
}

static void
usage(void)
{
	printf("Usage: mkbinimg <bootstrap> <kernel> <ramdisk> -o <binary>\n");
	exit(1);
}

static int
copy_blocks(int ifd, int ofd, unsigned long *offset, unsigned long *size)
{
	off_t cur;
	int amt;
	unsigned long len = 0;
	char buffer[SIZE];

	cur = lseek(ofd, 0, SEEK_CUR);

	if (cur % SIZE) {
		cur = BLOCK_ALIGN(cur);
		cur = lseek(ofd, cur, SEEK_SET);
	}

	*offset = (unsigned long) cur;
	while((amt = read(ifd, buffer, SIZE)) > 0) {
		write(ofd, buffer, amt);
		len += amt;
	}
	*size = len;
	return 0;
}


int
main(int argc, char *argv[])
{
	char *kernel, *loader, *rdimage = NULL;
	unsigned long ld_off, kern_off, rd_off;
	unsigned long ld_size, kern_size, rd_size;
	int fd, ofd, len;
	char buffer[500];

	if (argc < 5 && !strcmp(argv[argc-2], "-o"))
		usage();

	if (argc > 5)
		rdimage = argv[3];

	kernel = argv[2];
	loader = argv[1];

	ofd = open(argv[argc-1], (O_RDWR|O_CREAT), 0755);
	if (ofd < 0) {
		die("can't open %s: %s", argv[5], strerror(errno));
	}

	ld_off = kern_off = rd_off = 0;
	ld_size = kern_size = rd_size = 0;
	memset(buffer, 0, 500);
	len = 0;

	fd = open(loader, O_RDONLY);
	if (fd < 0) 
		die("can't open loader: %s", strerror(errno));

	copy_blocks(fd, ofd, &ld_off, &ld_size);
	len = sprintf(buffer, "bootloader: %x %x\n", ld_off, ld_size);
	close(fd);

	fd = open(kernel, O_RDONLY);
	if (fd < 0)
		die("can't open kernel: %s", strerror(errno));

	copy_blocks(fd, ofd, &kern_off, &kern_size);
	len += sprintf(buffer+len, "zimage: %x %x\n", kern_off, kern_size);
	close(fd);
	
	if (rdimage) {
		fd = open(rdimage, O_RDONLY);
		if (fd < 0)
			die("can't get ramdisk: %s", strerror(errno));
		
		copy_blocks(fd, ofd, &rd_off, &rd_size);
		close(fd);
	}

	len += sprintf(buffer+len, "initrd: %x %x", rd_off, rd_size);

	close(ofd);

	printf("%s\n", buffer);

	return 0;
}

