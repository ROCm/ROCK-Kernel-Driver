#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFSIZE 12345
int
main(int argc, char **argv, char **envp)
{
	int fd, i;
	char **ep = envp;
	char *buf, *p;
	buf = malloc(42);
	if (!buf)
		exit(1);
	p = getenv("SEQNUM");
	snprintf(buf, 42, "/events/dbg.%08u.%s", getpid(), p ? p : "");
	if ((fd = open(buf, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0) {
		//perror(buf);
		exit(1);
	}
	free(buf);
	p = malloc(BUFSIZE);
	buf = p;
	for (i = 0; i < argc; ++i) {
		buf += snprintf(buf, p + BUFSIZE - buf, " %s", argv[i]);
		if (buf > p + BUFSIZE)
			goto full;
	}
	buf += snprintf(buf, p + BUFSIZE - buf, "\n");
	if (buf > p + BUFSIZE)
		goto full;
	while (*ep) {
		buf += snprintf(buf, p + BUFSIZE - buf, "%s\n", *ep++);
		if (buf > p + BUFSIZE)
			break;
	}
      full:
	buf = p;
	write(fd, buf, strlen(buf));
	close(fd);
	free(buf);
	return 0;
}
