Changes by Matija Nalis (mnalis@jagor.srce.hr) on umsdos dentry fixing
(started by Peter T. Waltenberg <peterw@karaka.chch.cri.nz>)
(Final conversion to dentries Bill Hawes <whawes@star.net>)

There is no warning any more.
Both read-only and read-write stuff is fixed, both in
msdos-compatibile mode, and in umsdos EMD mode, and it seems stable.
There are still few hardlink nuisances, but those are not fatal.

I'd call it pre-release, and ask for as many people as possible to
come and test it! See notes below for some more information, or if
you are trying to use UMSDOS as root partition.

Userland NOTE: new umsdos_progs (umssync, umssetup, udosctl & friends) that
will compile and work on 2.2.x kernels and glibc based systems may be found
at http://cvs.linux.hr/

Also look at the quick-hack "homepage" for umsdos filesystem at 
http://www.voyager.hr/~mnalis/umsdos

Information below is getting outdated slowly -- I'll fix it one day when I
get enough time - there are more important things to fix right now.

Legend: those lines marked with '+' on the beggining of line indicates it
passed all of my tests, and performed perfect in all of them.

Current status (990202) - UMSDOS 0.85:

(1) pure MSDOS (no --linux-.--- EMD file):

READ:
+ readdir			- works
+ lookup			- works
+ read file			- works

WRITE:
+ creat file			- works
+ delete file			- works
+ write file			- works
+ rename file (same dir)	- works
+ rename file (dif. dir)	- works
+ rename dir (same dir)		- works
+ rename dir (dif. dir)		- works
+ mkdir				- works
+ rmdir 			- works


(2) umsdos (with --linux-.--- EMD file):

READ:
+ readdir			- works
+ lookup 			- works
+ permissions/owners stuff	- works
+ long file names		- works
+ read file			- works
+ switching MSDOS/UMSDOS	- works
+ switching UMSDOS/MSDOS	- works
- pseudo root things		- works mostly. See notes below.
+ resolve symlink		- works
+ dereference symlink		- works
+ dangling symlink		- works
+ hard links			- works
+ special files (block/char devices, FIFOs, sockets...)	- works
+ various umsdos ioctls		- works


WRITE:
+ create symlink		- works
- create hardlink		- works
+ create file			- works
+ create special file		- works
+ write to file			- works
+ rename file (same dir)	- works
+ rename file (dif. dir)	- works
- rename hardlink (same dir)	-
- rename hardlink (dif. dir)	-
+ rename symlink (same dir)	- works
+ rename symlink (dif. dir)	- works
+ rename dir (same dir)		- works
+ rename dir (dif. dir)		- works
+ delete file			- works
+ notify_change (chown,perms)	- works
+ delete hardlink		- works
+ mkdir				- works
+ rmdir 			- works
+ umssyncing (many ioctls)	- works


- CVF-FAT stuff (compressed DOS filesystem) - there is some support from Frank
  Gockel <gockel@sent13.uni-duisburg.de> to use it even under umsdosfs, but I
  have no way of testing it -- please let me know if there are problems specific
  to umsdos (for instance, it works under msdosfs, but not under umsdosfs).


Some current notes:

Note: creating and using pseudo-hardlinks is always non-perfect, especially
in filesystems that might be externally modified like umsdos. There is
example is specs file about it. Specifically, moving directory which
contains hardlinks will break them.

Note: (about pseudoroot) If you are currently trying to use UMSDOS as root
partition (with linux installed in c:\linux) it will boot, but there may be
some problems. Volunteers ready to test pseudoroot are needed (preferably
ones with working backups or unimportant data).  For example, '/DOS' pseudo
directory is only partially re-implemented and buggy. It works most of the
time, though. Update: should work ok in 0.84, although it still does not
work correctly in combination with initrd featere. Working on this!

Note: (about creating hardlinks in pseudoroot mode) - hardlinks created in
pseudoroot mode are now again compatibile with 'normal' hardlinks, and vice
versa. Thanks to Sorin Iordachescu <sorin@rodae.ro> for providing fix.

Warning: (about hardlinks) - modifying hardlinks (esp. if they are in
different directories) are currently somewhat broken, I'm working on it.
Problem seems to be that code uses and updates EMD of directory where 'real
hardlink' is stored, not EMD of directory where our pseudo-hardlink is
located! I'm looking for ideas how to work around this in clean way, since
without it modifying hardlinks in any but most simple ways is broken!

------------------------------------------------------------------------------

Some general notes:

Good idea when running development kernels is to have SysRq support compiled
in kernel, and use Sync/Emergency-remount-RO if you bump into problems (like
not being able to umount(2) umsdosfs, and because of it root partition also,
or panics which force you to reboot etc.)

I'm unfortunately somewhat out of time to read linux-kernel@vger, but I do
check for messages having "UMSDOS" in the subject, and read them.  I might
miss some in all that volume, though.  I should reply to any direct e-mail
in few days.  If I don't, probably I never got your message.  You can try
mnalis-umsdos@voyager.hr; however mnalis@jagor.srce.hr is preferable.

