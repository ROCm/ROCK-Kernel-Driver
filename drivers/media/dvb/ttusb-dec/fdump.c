#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


int main (int argc, char **argv)
{
	unsigned char buf[8];
	unsigned int i, count, bytes = 0;
	int fd;

	if (argc != 3) {
		fprintf (stderr, "\n\tusage: %s <ucode.bin> <array_name>\n\n",
			 argv[0]);
		return -1;
	}

	fd = open (argv[1], O_RDONLY);

	printf ("\n#include <asm/types.h>\n\nu8 %s [] __initdata = {",
		argv[2]);

	while ((count = read (fd, buf, 8)) > 0) {
		printf ("\n\t");
		for (i=0;i<count;i++, bytes++)
			printf ("0x%02x, ", buf[i]);
	}

	printf ("\n};\n\n");
	close (fd);

	return 0;
}

