/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <asm/page.h>
#include "user_util.h"
#include "kern_util.h"
#include "mem_user.h"
#include "user.h"
#include "init.h"

unsigned long stacksizelim;

char *linux_prog;

#define PGD_BOUND (4 * 1024 * 1024)
#define STACKSIZE (8 * 1024 * 1024)
#define THREAD_NAME_LEN (256)

char padding[THREAD_NAME_LEN] = { [ 0 ...  THREAD_NAME_LEN - 2] = ' ', '\0' };

static void set_stklim(void)
{
	struct rlimit lim;

	if(getrlimit(RLIMIT_STACK, &lim) < 0){
		perror("getrlimit");
		exit(1);
	}
	if((lim.rlim_cur == RLIM_INFINITY) || (lim.rlim_cur > STACKSIZE)){
		lim.rlim_cur = STACKSIZE;
		if(setrlimit(RLIMIT_STACK, &lim) < 0){
			perror("setrlimit");
			exit(1);
		}
	}
	stacksizelim = (lim.rlim_cur + PGD_BOUND - 1) & ~(PGD_BOUND - 1);
}

static __init void do_uml_initcalls(void)
{
	initcall_t *call;

	call = &__uml_initcall_start;
	while (call < &__uml_initcall_end){;
		(*call)();
		call++;
	}
}

extern int uml_exitcode;

int main(int argc, char **argv, char **envp)
{
	sigset_t mask;
	int ret, i;
	char **new_argv;

	/* Enable all signals - in some environments, we can enter with
	 * some signals blocked
	 */

	sigemptyset(&mask);
	if(sigprocmask(SIG_SETMASK, &mask, NULL) < 0){
		perror("sigprocmask");
		exit(1);
	}

	/* Allocate memory for thread command lines */
	if(argc < 2 || strlen(argv[1]) < THREAD_NAME_LEN - 1){
		new_argv = malloc((argc + 2) * sizeof(char*));
		if(!new_argv) {
			perror("Allocating extended argv");
			exit(1);
		}	
		
		new_argv[0] = argv[0];
		new_argv[1] = padding;
		
		for(i = 2; i <= argc; i++)
			new_argv[i] = argv[i - 1];
		new_argv[argc + 1] = NULL;
		
#ifdef PROFILING
		disable_profile_timer();
#endif
		execvp(new_argv[0], new_argv);
		perror("execing with extended args");
		exit(1);
	}	

	linux_prog = argv[0];

	set_stklim();
	set_task_sizes(0);

	if((new_argv = malloc((argc + 1) * sizeof(char *))) == NULL){
		perror("Mallocing argv");
		exit(1);
	}
	for(i=0;i<argc;i++){
		if((new_argv[i] = strdup(argv[i])) == NULL){
			perror("Mallocing an arg");
			exit(1);
		}
	}
	new_argv[argc] = NULL;

	do_uml_initcalls();
	ret = linux_main(argc, argv);
	
	/* Reboot */
	if(ret){
		printf("\n");
		execvp(new_argv[0], new_argv);
		perror("Failed to exec kernel");
		ret = 1;
	}
	printf("\n");
	return(uml_exitcode);
}

int allocating_monbuf = 0;

#ifdef PROFILING
extern void __real___monstartup (unsigned long, unsigned long);

void __wrap___monstartup (unsigned long lowpc, unsigned long highpc)
{
	allocating_monbuf = 1;
	__real___monstartup(lowpc, highpc);
	allocating_monbuf = 0;
	get_profile_timer();
}
#endif

extern void *__real_malloc(int);
extern unsigned long host_task_size;

static void *gmon_buf = NULL;

void *__wrap_malloc(int size)
{
	if(allocating_monbuf){
		unsigned long start, end;
		int fd;

		/* Turn this off now in case create_mem_file tries allocating
		 * memory
		 */
		allocating_monbuf = 0;
		fd = create_mem_file(size);

		/* Calculate this here because linux_main hasn't run yet
		 * and host_task_size figures in STACK_TOP, which figures
		 * in kmem_end.
		 */
		set_task_sizes(0);

		/* Same with stacksizelim */
		set_stklim();

		end = get_kmem_end();
		start = (end - size) & PAGE_MASK;
		gmon_buf = mmap((void *) start, size, PROT_READ | PROT_WRITE,
			    MAP_SHARED | MAP_FIXED, fd, 0);
		if(gmon_buf != (void *) start){
			perror("Creating gprof buffer");
			exit(1);
		}
		set_kmem_end(start);
		return(gmon_buf);
	}
	if(kmalloc_ok) return(um_kmalloc(size));
	else return(__real_malloc(size));
}

void *__wrap_calloc(int n, int size)
{
	void *ptr = __wrap_malloc(n * size);

	if(ptr == NULL) return(NULL);
	memset(ptr, 0, n * size);
	return(ptr);
}

extern void __real_free(void *);

void __wrap_free(void *ptr)
{
	/* Could maybe unmap the gmon buffer, but we're just about to
	 * exit anyway
	 */
	if(ptr == gmon_buf) return;
	if(kmalloc_ok) kfree(ptr);
	else __real_free(ptr);
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
