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
#include "signal_user.h"
#include "user.h"
#include "init.h"
#include "mode.h"
#include "choose-mode.h"
#include "uml-config.h"

/* Set in set_stklim, which is called from main and __wrap_malloc.  
 * __wrap_malloc only calls it if main hasn't started.
 */
unsigned long stacksizelim;

/* Set in main */
char *linux_prog;

#define PGD_BOUND (4 * 1024 * 1024)
#define STACKSIZE (8 * 1024 * 1024)
#define THREAD_NAME_LEN (256)

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

static void last_ditch_exit(int sig)
{
	CHOOSE_MODE(kmalloc_ok = 0, (void) 0);
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	uml_cleanup();
	exit(1);
}

extern int uml_exitcode;

int main(int argc, char **argv, char **envp)
{
	char **new_argv;
	sigset_t mask;
	int ret, i;

	/* Enable all signals except SIGIO - in some environments, we can 
	 * enter with some signals blocked
	 */

	sigemptyset(&mask);
	sigaddset(&mask, SIGIO);
	if(sigprocmask(SIG_SETMASK, &mask, NULL) < 0){
		perror("sigprocmask");
		exit(1);
	}

#ifdef UML_CONFIG_MODE_TT
	/* Allocate memory for thread command lines */
	if(argc < 2 || strlen(argv[1]) < THREAD_NAME_LEN - 1){

		char padding[THREAD_NAME_LEN] = { 
			[ 0 ...  THREAD_NAME_LEN - 2] = ' ', '\0' 
		};

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
		
		execvp(new_argv[0], new_argv);
		perror("execing with extended args");
		exit(1);
	}	
#endif

	linux_prog = argv[0];

	set_stklim();

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

	set_handler(SIGINT, last_ditch_exit, SA_ONESHOT | SA_NODEFER, -1);
	set_handler(SIGTERM, last_ditch_exit, SA_ONESHOT | SA_NODEFER, -1);
	set_handler(SIGHUP, last_ditch_exit, SA_ONESHOT | SA_NODEFER, -1);

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

#define CAN_KMALLOC() \
	(kmalloc_ok && CHOOSE_MODE((getpid() != tracing_pid), 1))

extern void *__real_malloc(int);

void *__wrap_malloc(int size)
{
	if(CAN_KMALLOC())
		return(um_kmalloc(size));
	else
		return(__real_malloc(size));
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
	if(CAN_KMALLOC()) kfree(ptr);
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
