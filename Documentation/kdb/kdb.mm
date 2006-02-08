.TH KDB 8 "September 21, 2005"
.hy 0
.SH NAME
Built-in Kernel Debugger for Linux - v4.4
.SH "Overview"
This document describes the built-in kernel debugger available
for linux.   This debugger allows the programmer to interactively
examine kernel memory, disassemble kernel functions, set breakpoints
in the kernel code and display and modify register contents.
.P
A symbol table is included in the kernel image and in modules which
enables all non-stack symbols (including static symbols) to be used as
arguments to the kernel debugger commands.
.SH "Getting Started"
To include the kernel debugger in a linux kernel, use a
configuration mechanism (e.g. xconfig, menuconfig, et. al.)
to enable the \fBCONFIG_KDB\fP option.   Additionally, for accurate
stack tracebacks, it is recommended that the \fBCONFIG_FRAME_POINTER\fP
option be enabled (if present).   \fBCONFIG_FRAME_POINTER\fP changes the compiler
flags so that the frame pointer register will be used as a frame
pointer rather than a general purpose register.
.P
After linux has been configured to include the kernel debugger,
make a new kernel with the new configuration file (a make clean
is recommended before making the kernel), and install the kernel
as normal.
.P
You can compile a kernel with kdb support but have kdb off by default,
select \fBCONFIG_KDB_OFF\fR.  Then the user has to explicitly activate
kdb by booting with the 'kdb=on' flag or, after /proc is mounted, by
.nf
  echo "1" > /proc/sys/kernel/kdb
.fi
You can also do the reverse, compile a kernel with kdb on and
deactivate kdb with the boot flag 'kdb=off' or, after /proc is mounted,
by
.nf
  echo "0" > /proc/sys/kernel/kdb
.fi
.P
When booting the new kernel, the 'kdb=early' flag
may be added after the image name on the boot line to
force the kernel to stop in the kernel debugger early in the
kernel initialization process.  'kdb=early' implies 'kdb=on'.
If the 'kdb=early' flag isn't provided, then kdb will automatically be
invoked upon system panic or when the \fBPAUSE\fP key is used from the
keyboard, assuming that kdb is on.  Older versions of kdb used just a
boot flag of 'kdb' to activate kdb early, this is no longer supported.
.P
KDB can also be used via the serial port.  Set up the system to
have a serial console (see \fIDocumentation/serial-console.txt\fP), you
must also have a user space program such as agetty set up to read from
the serial console.
The control sequence \fB<esc>KDB\fP on the serial port will cause the
kernel debugger to be entered, assuming that kdb is on, that some
program is reading from the serial console, at least one cpu is
accepting interrupts and the serial console driver is still usable.
.P
\fBNote:\fR\ When the serial console sequence consists of multiple
characters such as <esc>KDB then all but the last character are passed
through to the application that is reading from the serial console.
After exiting from kdb, you should use backspace to delete the rest of
the control sequence.
.P
You can boot with kdb activated but without the ability to enter kdb
via any keyboard sequence.
In this mode, kdb will only be entered after a system failure.
Booting with kdb=on-nokey will activate kdb but ignore keyboard
sequences that would normally drop you into kdb.
kdb=on-nokey is mainly useful when you are using a PC keyboard and your
application needs to use the Pause key.
You can also activate this mode by
.nf
  echo "2" > /proc/sys/kernel/kdb
.fi
.P
If the console is sitting on the login prompt when you enter kdb, then
the login command may switch into upper case mode.
This is not a kdb bug, it is a "feature" of login - if the userid is
all upper case then login assumes that you using a TeleType (circa
1960) which does not have lower case characters.
Wait 60 seconds for login to timeout and it will switch back to lower
case mode.
.P
\fBNote:\fR\ Your distributor may have chosen a different kdb
activation sequence for the serial console.
Consult your distribution documentation.
.P
If you have both a keyboard+video and a serial console, you can use
either for kdb.
Define both video and serial consoles with boot parameters
.P
.nf
  console=tty0 console=ttyS0,38400
.fi
.P
Any kdb data entered on the keyboard or the serial console will be echoed
to both.
.P
If you are using a USB keyboard then kdb commands cannot be entered
until the kernel has initialised the USB subsystem and recognised the
keyboard.
Using kdb=early with a USB keyboard will not work, the USB subsystem is
initialised too late.
.P
While kdb is active, the keyboard (not serial console) indicators may strobe.
The caps lock and scroll lock lights will turn on and off, num lock is not used
because it can confuse laptop keyboards where the numeric keypad is mapped over
the normal keys.
On exit from kdb the keyboard indicators will probably be wrong, they will not match the kernel state.
Pressing caps lock twice should get the indicators back in sync with
the kernel.
.SH "Basic Commands"
There are several categories of commands available to the
kernel debugger user including commands providing memory
display and modification, register display and modification,
instruction disassemble, breakpoints and stack tracebacks.
Any command can be prefixed with '-' which will cause kdb to ignore any
errors on that command, this is useful when packaging commands using
defcmd.
A line whose first non-space character is '#' is printed and ignored.
.P
The following table shows the currently implemented standard commands,
these are always available.  Other commands can be added by extra
debugging modules, type '?' at the kdb prompt to get a list of all
available commands.
.DS
.TS
box, center;
l | l
l | l.
Command	Description
_
bc	Clear Breakpoint
bd	Disable Breakpoint
be	Enable Breakpoint
bl	Display breakpoints
bp	Set or Display breakpoint
bph	Set or Display hardware breakpoint
bpa	Set or Display breakpoint globally
bpha	Set or Display hardware breakpoint globally
bt	Stack backtrace for current process
btp	Stack backtrace for specific process
bta	Stack backtrace for all processes
btc	Cycle over all live cpus and backtrace each one
cpu	Display or switch cpus
dmesg	Display system messages
defcmd	Define a command as a set of other commands
ef	Print exception frame
env	Show environment
go	Restart execution
handlers	Control the display of IA64 MCA/INIT handlers
help	Display help message
id	Disassemble Instructions
kill	Send a signal to a process
ll	Follow Linked Lists
lsmod	List loaded modules
md	Display memory contents
mdWcN	Display memory contents with width W and count N.
mdp	Display memory based on a physical address
mdr	Display raw memory contents
mds	Display memory contents symbolically
mm	Modify memory contents, words
mmW	Modify memory contents, bytes
per_cpu	Display per_cpu variables
pid	Change the default process context
ps	Display process status
reboot	Reboot the machine
rd	Display register contents
rm	Modify register contents
rq	Display runqueue for one cpu
rqa	Display runqueue for all cpus
set	Add/change environment variable
sr	Invoke SysReq commands
ss	Single step a cpu
ssb	Single step a cpu until a branch instruction
stackdepth	Print the stack depth for selected processes
summary	Summarize the system
.TE
.DE
.P
Some commands can be abbreviated, such commands are indicated by a
non-zero \fIminlen\fP parameter to \fBkdb_register\fP; the value of
\fIminlen\fP being the minimum length to which the command can be
abbreviated (for example, the \fBgo\fP command can be abbreviated
legally to \fBg\fP).
.P
If an input string does not match a command in the command table,
it is treated as an address expression and the corresponding address
value and nearest symbol are shown.
.P
Some of the commands are described here.
Information on the more complicated commands can be found in the
appropriate manual pages.
.TP 8
cpu
With no parameters, it lists the available cpus.
\&'*' after a cpu number indicates a cpu that did not respond to the kdb
stop signal.
\&'+' after a cpu number indicates a cpu for which kdb has some data, but
that cpu is no longer responding to kdb, so you cannot switch to it.
This could be a cpu that has failed after entering kdb, or the cpu may
have saved its state for debugging then entered the prom, this is
normal for an IA64 MCA event.
\&'I' after a cpu number means that the cpu was idle before it entered
kdb, it is unlikely to contain any useful data.
\&'F' after a cpu number means that the cpu is offline.
There is currenly no way to distinguish between cpus that used to be
online but are now offline and cpus that were never online, the kernel
does not maintain the information required to separate those two cases.
.I cpu
followed by a number will switch to that cpu, you cannot switch to
a cpu marked '*', '+' or 'F'.
This command is only available if the kernel was configured for SMP.
.TP 8
dmesg [lines] [adjust]
Displays the system messages from the kernel buffer.
If kdb logging is on, it is disabled by dmesg and is left as disabled.
With no parameters or a zero value for 'lines', dmesg dumps the entire
kernel buffer.
If lines is specified and is positive, dmesg dumps the last 'lines'
from the buffer.
If lines is specified and is negative, dmesg dumps the first 'lines'
from the buffer.
If adjust is specified, adjust the starting point for the lines that
are printed.
When 'lines' is positive, move the starting point back by 'adjust'
lines, when 'lines' is negative, move the starting point forward by
\&'adjust' lines.
.I dmesg -100
will dump 100 lines, from the start of the buffer.
.I dmesg 100
will dump 100 lines, starting 100 lines from the end of the buffer,
.I dmesg 100 100
will dump 100 lines, starting 200 lines from the end of the buffer.
.I dmesg -100 100
will dump 100 lines, starting 100 lines from the start of the buffer.
.TP 8
defcmd
Defines a new command as a set of other commands, all input until
.I endefcmd
is saved and executed as a package.
.I defcmd
takes three parameters, the command name to be defined and used to
invoke the package, a quoted string containing the usage text and a
quoted string containing the help text for the command.
When using defcmd, it is a good idea to prefix commands that might fail
with '-', this ignores errors so the following commands are still
executed.
For example,
.P
.nf
        defcmd diag "" "Standard diagnostics"
          set LINES 2000
          set BTAPROMPT 0
          -id %eip-0x40
          -cpu
          -ps
          -dmesg 80
          -bt
          -bta
        endefcmd
.fi
.P
When used with no parameters, defcmd prints all the defined commands.
.TP 8
go
Continue normal execution.
Active breakpoints are reestablished and the processor(s) allowed to
run normally.
To continue at a specific address, use
.I rm
to change the instruction pointer then go.
.TP 8
handlers
Control the display of IA64 MCA/INIT handlers.
The IA64 MCA/INIT handlers run on separate tasks.
During an MCA/INIT event, the active tasks are typically the handlers,
rather than the original tasks, which is not very useful for debugging.
By default, KDB hides the MCA/INIT handlers so commands such as ps and
btc will display the original task.
You can change this behaviour by using
.I handlers show
to display the MCA/INIT handlers instead of the original tasks or use
.I handlers hide
(the default) to hide the MCA/INIT handlers and display the original
tasks.
.I handlers status
will list the address of the handler task and the original task for
each cpu.
\fBNote:\fR\ If the original task was running in user space or it
failed any of the MCA/INIT verification tests then there is no original
task to display.
In this case, the handler will be displayed even if
.I handlers hide
is set and
.I handlers status
will not show an original task.
.TP 8
id
Disassemble instructions starting at an address.
Environment variable IDCOUNT controls how many lines of disassembly
output the command produces.
.TP 8
kill
Internal command to send a signal (like kill(1)) to a process.
kill -signal pid.
.TP 8
lsmod
Internal command to list modules.
This does not use any kernel nor user space services so can be used at any time.
.TP 8
per_cpu <variable_name> [<length>] [<cpu>]
Display the values of a per_cpu variable, the variable_name is
specified without the \fIper_cpu__\fR prefix.
Length is the length of the variable, 1-8, if omitted or 0 it defaults
to the size of the machine's register.
To display the variable on a specific cpu, the third parameter is the
cpu number.
When the third parameter is omitted, the variable's value is printed
from all cpus, except that zero values are suppressed.
For each cpu, per_cpu prints the cpu number, the address of the
variable and its value.
.TP 8
pid <number>
Change the current process context, with no parameters it displays the
current process.
The current process is used to display registers, both kernel and user
space.
It is also used when dumping user pages.
.I pid R
resets to the original process that was running when kdb was entered.
This command is useful if you have been looking at other processes and/or
cpus and you want to get back to the original process.
It does not switch cpus, it only resets the context to the original process.
.TP 8
reboot
Reboot the system, with no attempt to do a clean close down.
.TP 8
rq <cpu>
Display the runqueues for the specified cpu.
.TP 8
rqa
Display the runqueues for all cpus.
.TP 8
stackdepth <percentage>
Print the stack usage for processes using more than the specified
percentage of their stack.
If percentage is not supplied, it defaults to 60.
This command is only implemented on i386 and ia64 architectures,
patches for other architectures will be gratefully accepted.
.TP 8
summary
Print a summary of the system, including the time (no timezone is
applied), uname information and various critical system counters.
.SH INITIAL KDB COMMANDS
kdb/kdb_cmds is a plain text file where you can define kdb commands
which are to be issued during kdb_init().  One command per line, blank
lines are ignored, lines starting with '#' are ignored.  kdb_cmds is
intended for per user customization of kdb, you can use it to set
environment variables to suit your hardware or to set standard
breakpoints for the problem you are debugging.  This file is converted
to a small C object, compiled and linked into the kernel.  You must
rebuild and reinstall the kernel after changing kdb_cmds.  This file
will never be shipped with any useful data so you can always override
it with your local copy.  Sample kdb_cmds:
.P
.nf
# Initial commands for kdb, alter to suit your needs.
# These commands are executed in kdb_init() context, no SMP, no
# processes.  Commands that require process data (including stack or
# registers) are not reliable this early.  set and bp commands should
# be safe.  Global breakpoint commands affect each cpu as it is booted.

set LINES=50
set MDCOUNT=25
set RECURSE=1
bp sys_init_module
.fi
.SH INTERRUPTS AND KDB
When a kdb event occurs, one cpu (the initial cpu) enters kdb state.
It uses a cross system interrupt to interrupt the
other cpus and bring them all into kdb state.  All cpus run with
interrupts disabled while they are inside kdb, this prevents most
external events from disturbing the kernel while kdb is running.
.B Note:
Disabled interrupts means that any I/O that relies on interrupts cannot
proceed while kdb is in control, devices can time out.  The clock tick
is also disabled, machines will lose track of time while they are
inside kdb.
.P
Even with interrupts disabled, some non-maskable interrupt events will
still occur, these can disturb the kernel while you are debugging it.
The initial cpu will still accept NMI events, assuming that kdb was not
entered for an NMI event.  Any cpu where you use the SS or SSB commands
will accept NMI events, even after the instruction has finished and the
cpu is back in kdb.  This is an unavoidable side effect of the fact that
doing SS[B] requires the cpu to drop all the way out of kdb, including
exiting from the event that brought the cpu into kdb.  Under normal
circumstances the only NMI event is for the NMI oopser and that is kdb
aware so it does not disturb the kernel while kdb is running.
.P
Sometimes doing SS or SSB on ix86 will allow one interrupt to proceed,
even though the cpu is disabled for interrupts.  I have not been able
to track this one down but I suspect that the interrupt was pending
when kdb was entered and it runs when kdb exits through IRET even
though the popped flags are marked as cli().  If any ix86 hardware
expert can shed some light on this problem, please notify the kdb
maintainer.
.SH RECOVERING FROM KDB ERRORS
If a kdb command breaks and kdb has enough of a recovery environment
then kdb will abort the command and drop back into mainline kdb code.
This means that user written kdb commands can follow bad pointers
without killing kdb.  Ideally all code should verify that data areas
are valid (using kdb_getarea) before accessing it but lots of calls to
kdb_getarea can be clumsy.
.P
The sparc64 port does not currently provide this error recovery.
If someone would volunteer to write the necessary longjmp/setjmp
code, their efforts would be greatly appreciated. In the
meantime, it is possible for kdb to trigger a panic by accessing
a bad address.
.SH DEBUGGING THE DEBUGGER
kdb has limited support for debugging problems within kdb.  If you
suspect that kdb is failing, you can set environment variable KDBDEBUG
to a bit pattern which will activate kdb_printf statements within kdb.
See include/linux/kdb.h, KDB_DEBUG_FLAG_xxx defines.  For example
.nf
  set KDBDEBUG=0x60
.fi
activates the event callbacks into kdb plus state tracing in sections
of kdb.
.nf
  set KDBDEBUG=0x18
.fi
gives lots of tracing as kdb tries to decode the process stack.
.P
You can also perform one level of recursion in kdb.  If environment
variable RECURSE is not set or is 0 then kdb will either recover from
an error (if the recovery environment is satisfactory) or kdb will
allow the error to percolate, usually resulting in a dead system.  When
RECURSE is 1 then kdb will recover from an error or, if there is no
satisfactory recovery environment, it will drop into kdb state to let
you diagnose the problem.  When RECURSE is 2 then all errors drop into
kdb state, kdb does not attempt recovery first.  Errors while in
recursive state all drop through, kdb does not even attempt to recover
from recursive errors.
.SH KEYBOARD EDITING
kdb supports a command history, which can be accessed via keyboard
sequences.
It supports the special keys on PC keyboards, control characters and
vt100 sequences on a serial console or a PC keyboard.
.P
.DS
.TS
box, center;
l | l | l l | l
l | l | l l | l.
PC Special keys	Control	VT100 key	Codes	Action
_
Backspace	ctrl-H	Backspace	0x7f	Delete character to the left of the cursor
Delete	ctrl-D	Delete	\\e[3~	Delete character to the right of the cursor
Home	ctrl-A	Home	\\e[1~	Go to start of line
End	ctrl-E	End	\\e[4~	Go to end of line
Up arrow	ctrl-P	Up arrow	\\e[A	Up one command in history
Down arrow	ctrl-N	Down arrow	\\e[B	Down one command in history
Left arrow	ctrl-B	Left arrow	\\e[D	Left one character in current command
Right arrow	ctrl-F	Right arrow	\\e[C	Right one character in current command
.TE
.DE
.P
There is no toggle for insert/replace mode, kdb editing is always in
insert mode.
Use delete and backspace to delete characters.
.P
kdb also supports tab completion for kernel symbols
Type the start of a kernel symbol and press tab (ctrl-I) to complete
the name
If there is more than one possible match, kdb will append any common
characters and wait for more input, pressing tab a second time will
display the possible matches
The number of matches is limited by environment variable DTABCOUNT,
with a default of 30 if that variable is not set.
.SH AUTHORS
Scott Lurndal, Richard Bass, Scott Foehner, Srinivasa Thirumalachar,
Masahiro Adegawa, Marc Esipovich, Ted Kline, Steve Lord, Andi Kleen,
Sonic Zhang.
.br
Keith Owens <kaos@sgi.com> - kdb maintainer.
.SH SEE ALSO
.P
linux/Documentation/kdb/kdb_{bp,bt,env,ll,md,ps,rd,sr,ss}.man
