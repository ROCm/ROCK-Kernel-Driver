Using the ACPI debugger with kdb
--------------------------------

ACPI CA includes a full-featured debugger, which allows the examination of
a running system's ACPI tables, as well as running and stepping through
control methods.

Configuration
-------------
1) Edit the main acpi Makefile. On the ACPI_CFLAGS line, remove the '#', thus
   enabling the debugger.

2) Download the latest kdb patch from:

   ftp://oss.sgi.com/www/projects/kdb/download/ix86/ 

   Follow the instructions at http://oss.sgi.com/projects/kdb/ on how to
   install the patch and configure KDB.

3) This would probably be a good time to recompile the kernel, and make sure
   kdb works (Hitting the Pause key should drop you into it. Type "go" to exit
   it.

4) The kdb <--> ACPI debugger interface is a module. Type "make modules", and
   it will be built and placed in drivers/acpi/kdb.

5) Change to that directory and type "insmod kdbm_acpi.o". This loads the
   module we just built.

6) Break back into kdb. If you type help, you should now see "acpi" listed as
   a command, at the bottom.

7) Type "acpi". You are now in the ACPI debugger. While hosted by kdb, it is
   wholly separate, and has many ACPI-specific commands. Type "?" or "help"
   to get a listing of the command categories, and then "help <category>" for
   a list of commands and their descriptions
