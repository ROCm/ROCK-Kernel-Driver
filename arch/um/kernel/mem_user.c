/*
 * arch/um/kernel/mem_user.c
 *
 * BRIEF MODULE DESCRIPTION
 * user side memory routines for supporting IO memory inside user mode linux
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon glonnon@ridgerun.com or info@ridgerun.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "kern_util.h"
#include "user.h"
#include "user_util.h"
#include "mem_user.h"
#include "init.h"
#include "os.h"
#include "tempfile.h"

extern struct mem_region physmem_region;

#define TEMPNAME_TEMPLATE "vm_file-XXXXXX"

int create_mem_file(unsigned long len)
{
	int fd;
	char zero;

	fd = make_tempfile(TEMPNAME_TEMPLATE, NULL, 1);
	if (fchmod(fd, 0777) < 0){
		perror("fchmod");
		exit(1);
	}
	if(os_seek_file(fd, len) < 0){
		perror("lseek");
		exit(1);
	}
	zero = 0;
	if(write(fd, &zero, 1) != 1){
		perror("write");
		exit(1);
	}
	if(fcntl(fd, F_SETFD, 1) != 0)
		perror("Setting FD_CLOEXEC failed");
	return(fd);
}

int setup_region(struct mem_region *region, void *entry)
{
	void *loc, *start;
	char *driver;
	int err, offset;

	if(region->start != -1){
		err = reserve_vm(region->start, 
				 region->start + region->len, entry);
		if(err){
			printk("setup_region : failed to reserve "
			       "0x%x - 0x%x for driver '%s'\n",
			       region->start, 
			       region->start + region->len,
			       region->driver);
			return(-1);
		}
	}
	else region->start = get_vm(region->len);
	if(region->start == 0){
		if(region->driver == NULL) driver = "physmem";
		else driver = region->driver;
		printk("setup_region : failed to find vm for "
		       "driver '%s' (length %d)\n", driver, region->len);
		return(-1);
	}
	if(region->start == uml_physmem){
		start = (void *) uml_reserved;
		offset = uml_reserved - uml_physmem;
	}
	else {
		start = (void *) region->start;
		offset = 0;
	}

	loc = mmap(start, region->len - offset, PROT_READ | PROT_WRITE, 
		   MAP_SHARED | MAP_FIXED, region->fd, offset);
	if(loc != start){
		perror("Mapping memory");
		exit(1);
	}
	return(0);
}

static int __init parse_iomem(char *str, int *add)
{
	struct stat buf;
	char *file, *driver;
	int fd;

	driver = str;
	file = strchr(str,',');
	if(file == NULL){
		printk("parse_iomem : failed to parse iomem\n");
		return(1);
	}
	*file = '\0';
	file++;
	fd = os_open_file(file, of_rdwr(OPENFLAGS()), 0);
	if(fd < 0){
		printk("parse_iomem - Couldn't open io file, errno = %d\n", 
		       errno);
		return(1);
	}
	if(fstat(fd, &buf) < 0) {
		printk("parse_iomem - cannot fstat file, errno = %d\n", errno);
		return(1);
	}
	add_iomem(driver, fd, buf.st_size);
	return(0);
}

__uml_setup("iomem=", parse_iomem,
"iomem=<name>,<file>\n"
"    Configure <file> as an IO memory region named <name>.\n\n"
);

#ifdef notdef
int logging = 0;
int logging_fd = -1;

int logging_line = 0;
char logging_buf[256];

void log(char *fmt, ...)
{
	va_list ap;
	struct timeval tv;
	struct openflags flags;

	if(logging == 0) return;
	if(logging_fd < 0){
		flags = of_create(of_trunc(of_rdrw(OPENFLAGS())));
		logging_fd = os_open_file("log", flags, 0644);
	}
	gettimeofday(&tv, NULL);
	sprintf(logging_buf, "%d\t %u.%u  ", logging_line++, tv.tv_sec, 
		tv.tv_usec);
	va_start(ap, fmt);
	vsprintf(&logging_buf[strlen(logging_buf)], fmt, ap);
	va_end(ap);
	write(logging_fd, logging_buf, strlen(logging_buf));
}
#endif

int map_memory(unsigned long virt, unsigned long phys, unsigned long len, 
	       int r, int w, int x)
{
	struct mem_region *region = phys_region(phys);

	return(os_map_memory((void *) virt, region->fd, phys_offset(phys), len,
			     r, w, x));
}

int protect_memory(unsigned long addr, unsigned long len, int r, int w, int x,
		   int must_succeed)
{
	if(os_protect_memory((void *) addr, len, r, w, x) < 0){
                if(must_succeed)
                        panic("protect failed, errno = %d", errno);
                else return(-errno);
	}
	return(0);
}

unsigned long find_iomem(char *driver, unsigned long *len_out)
{
	struct mem_region *region;
	int i, n;

	n = nregions();
	for(i = 0; i < n; i++){
		region = regions[i];
		if(region == NULL) continue;
		if((region->driver != NULL) &&
		   !strcmp(region->driver, driver)){
			*len_out = region->len;
			return(region->start);
		}
	}
	*len_out = 0;
	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
