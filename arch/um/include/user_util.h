/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __USER_UTIL_H__
#define __USER_UTIL_H__

#include "sysdep/ptrace.h"

extern int grantpt(int __fd);
extern int unlockpt(int __fd);
extern char *ptsname(int __fd);

enum { OP_NONE, OP_EXEC, OP_FORK, OP_TRACE_ON, OP_REBOOT, OP_HALT, OP_CB };

struct cpu_task {
	int pid;
	void *task;
};

extern struct cpu_task cpu_tasks[];

extern unsigned long low_physmem;
extern unsigned long high_physmem;
extern unsigned long uml_physmem;
extern unsigned long uml_reserved;
extern unsigned long end_vm;
extern unsigned long start_vm;

extern int tracing_pid;
extern int honeypot;

extern char host_info[];

extern char saved_command_line[];
extern char command_line[];

extern int gdb_pid;

extern char *tempdir;

extern unsigned long _stext, _etext, _sdata, _edata, __bss_start, _end;
extern unsigned long _unprotected_end;
extern unsigned long brk_start;

extern int pty_output_sigio;
extern int pty_close_sigio;

extern void *open_maps(void);
extern void close_maps(void *fd);
extern unsigned long get_brk(void);
extern void stop(void);
extern int proc_start_thread(unsigned long ip, unsigned long sp);
extern void stack_protections(unsigned long address);
extern void task_protections(unsigned long address);
extern void abandon_proc_space(int (*proc)(void *), unsigned long sp);
extern int signals(int (*init_proc)(void *), void *sp);
extern int __personality(int);
extern int wait_for_stop(int pid, int sig, int cont_type, void *relay);
extern void *add_signal_handler(int sig, void (*handler)(int));
extern void signal_init(void);
extern int start_fork_tramp(void *arg, unsigned long temp_stack, 
			    int clone_flags, int (*tramp)(void *));
extern void trace_myself(void);
extern void timer(void);
extern void get_profile_timer(void);
extern void disable_profile_timer(void);
extern void set_timers(int set_signal);
extern int clone_and_wait(int (*fn)(void *), void *arg, void *sp, int flags);
extern int input_loop(void);
extern void continue_execing_proc(int pid);
extern int linux_main(int argc, char **argv);
extern void remap_data(void *segment_start, void *segment_end, int w);
extern void set_cmdline(char *cmd);
extern void input_cb(void (*proc)(void *), void *arg, int arg_len);
extern void setup_input(void);
extern int get_pty(void);
extern void save_signal_state(int *sig_ptr);
extern void *um_kmalloc(int size);
extern int raw(int fd, int complain);
extern int switcheroo(int fd, int prot, void *from, void *to, int size);
extern void idle_sleep(int secs);
extern void setup_machinename(char *machine_out);
extern void setup_hostinfo(void);
extern void add_arg(char *cmd_line, char *arg);
extern void init_new_thread(void *sig_stack, void (*usr1_handler)(int));
extern void attach_process(int pid);
extern void calc_sigframe_size(void);
extern int fork_tramp(void *sig_stack);
extern void do_exec(int old_pid, int new_pid);
extern void tracer_panic(char *msg, ...);
extern void close_fd(int);
extern int make_tempfile(const char *template, char **tempname, int do_unlink);
extern char *get_umid(int only_if_set);
extern void do_longjmp(void *p);
extern void term_handler(int sig);
extern void suspend_new_thread(int fd);
extern int detach(int pid, int sig);
extern int attach(int pid);
extern void kill_child_dead(int pid);
extern int cont(int pid);
extern void check_ptrace(void);
extern void check_sigio(void);
extern int run_kernel_thread(int (*fn)(void *), void *arg, void **jmp_ptr);
extern int user_read(int fd, char *buf, int len);
extern int user_write(int fd, char *buf, int len);
extern void write_sigio_workaround(void);
extern void arch_check_bugs(void);
extern int arch_handle_signal(int sig, struct uml_pt_regs *regs);
extern void user_time_init(void);
extern unsigned long pid_pc(int pid);
extern int arch_fixup(unsigned long address, void *sc_ptr);
extern void forward_pending_sigio(int target);

#endif

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
