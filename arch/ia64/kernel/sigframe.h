struct sigframe {
	/*
	 * Place signal handler args where user-level unwinder can find them easily.
	 * DO NOT MOVE THESE.  They are part of the IA-64 Linux ABI and there is
	 * user-level code that depends on their presence!
	 */
	unsigned long arg0;		/* signum */
	unsigned long arg1;		/* siginfo pointer */
	unsigned long arg2;		/* sigcontext pointer */

	unsigned long rbs_base;		/* base of new register backing store (or NULL) */
	void *handler;			/* pointer to the plabel of the signal handler */

	struct siginfo info;
	struct sigcontext sc;
};
