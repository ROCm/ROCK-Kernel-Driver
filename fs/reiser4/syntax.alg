<TITLE>Reiser4() Syntax In All Its Obscurities</TITLE>

<H1>Introduction</H1>

<p> We define a file operations API, that allows accessing files and
performing operations on them.  These same files are also accessible
by the usual Linux conventional VFS API.

<p>The existing VFS API is appropriate for accessing streams of data
that are larger than the buffers used to hold them, by using simple
hierarchical names.

<p>
There exist other needs.

<p> The new API provides a very few of them, but more significantly,
makes it easy to add more of them later.  Much of what we do in v4 to
expand the existing semantics can be done by accessing special
pseudo-files, and so we implement that.  Some of it, particularly
efficiently accessing multiple small files (or "attributes" which
should be implemented as files) with transactions in a single system
call, cannot be done via existing system calls, and requires a new
API.


<H2>Why Not Change The VFS All Filesystems Use?</H2>

<p> It is usual to ask, why don't we implement all of the new
functionality in VFS, changing all of the other filesystems, and
getting all of the authors of all of the other filesystems to agree
with the changes and participate in making them.

<p>The programming community has a different customary process, for
several reasons.  Too many cooks spoil the soup, regardless of how
good they are.  Only after the soup is made, can others know whether
our recipe is desirable or to be avoided.  Then, using the GPL license
with its restrictions, or obtaining another license from us if
proprietary, they can take what was shown to work well from our code,
and improve it from there.  Also, stability is very important to VFS,
and by making our experimental changes to separate code accessed by a
separate system call, we make change less disruptive.  Standards are
for when consensus has emerged, not for the tumult of active research.
It is not realistic to think that such a consensus will be quickly
reached, and a working implementation will propell such a consensus
faster than any other form of persuasion.


<h2>Library vs. Kernel</h2>

<p> We have no deep reason for not adopting an exo-kernel style
approach.  We developed for Linux, and Linux is not an exo-kernel.  It
is not appropriate for us to differ from the rest of Linux in this
aspect.  I am far from religious about the micro/exo/macro kernel
debates.  Micro kernels (e.g. HURD) seem to have substantially lower
performance while being easier to debug.  It won't surprise me if
somebody cures the lowered performance someday by being clever.  Many
of the developers on our team think we should put as much of our
functionality into the libraries as possible.  The danger in doing
that blindly is the danger of experiencing micro-kernel performance
problems.  Putting the functionality either all in the kernel
(monolithic kernel (Linux)), or all out of the kernel (exo-kernel)
seems likely to result in the best performance.  Selecting certain
functionality which lends itself naturally to being layered above our
interface, and putting it into libraries, is also reasonable, though
there might not be much of it frankly. Functionality that initially
seems likely to someday evolve to access functions not exposed outside
the kernel should be inside the kernel for optimal efficiency, and for
simplicity of kernel interface. If the applications don't see whether
it is in the kernel or in the library, then one can change the
location later.  For now our first pass at the problem is to put the
functionality into the kernel part of Reiser4.  Perhaps during the
debugging phase we will even pull certain pieces out of the kernel....

<h2>Creating Objects</h2>

<p> Every object must have at least one name, though that name might
be just its key.  Every object has a key.

<h3>Objects With Only Keys For Names</h3>

Objects with only keys for names can be accessed by the name
"..key/KEY" where KEY is of type key.  "..key" is not implemented as a
tradition directory: it has no directory entries, and performs lookups
by looking in the tree for them.  Objects that may have only keys for
names must have a flag so indicating in their stat data, or else fsck
will assume they are lost files and put them in lost+found.
Permission to access by key must be explicitly granted, and by default
is not granted to any user even root.

<H3>Object Plugin Determined At Object Creation Time</H3>

<p> There are various non-symmetries of object creation.  Plugins can
only be specified at object creation time (this might change in the
future for some plugins, but it is universally true in v4.0), and
changing plugins requires creating a new file and copying the old file
to the new file.

<p> The implication of these non-symmetries is that the syntax for
object creation necessarily must allow for multi-value assignment.
Each plugin must specify a set of assignments to look for, and provide
defaults for the event that they are ommitted.  There is even a
default packing locality for objects created without specified names
or packing localities.

<H3>The object creation method is determined by the name.</H3>

<H3>Packing Locality Is Determined At Object Creation Time</H3>

<p> Keys determine what an object is to be packed near. If the packing
locality for an object is specified at creation time, then we use the
specified packing locality.  If not, then if a name for the object is
specified at object creation time, then we let the parent directory
present in the name determine its packing locality, which by default
means to set it equal to the packing locality of its parent directory..
<p>
The implication of this is that specifying a name for an object at
creation time has an effect in addition to that of specifying a name
at some later time if the packing locality will be inherited from the
parent directory in the name.  Also, objects whose packing localities
are equal to the parent directory for their name can have their keys
better compressed.

<p> If an object is created without name and without specified packing
locality, then it is assigned to the default packing locality.  Since
the next field after packing locality within a key is the objectid,
and object creation time proximity correlates strongly with objectid
proximity due to the algorithms used, this has the effect of causing
objects with creation time proximity to be created near each other in
tree order and thus near each other in disk geometry.  This is an
intended effect.  Interleaved deletes may complicate this, but some
correlation will remain.  There is to be a special directory "/..keys"
which allows listing of all objects in the FS by key.

<h3>We Offer An Object, Not Block, Storage Interface</h3>

<p> Allowing applications to reserve and control specific blocks ala
Franz's exo-kernel is a feature not yet supported, though it would
have advantages for boot loaders and other applications.  Our current
mechanism of allowing tails to be turned off, and then allowing
querying of what blocks a file occupies, can be thought of as a poorly
abstracted version of this (and the linux boot loaders currently
support this poor abstraction we offer.)

<p>
ascii_command =
<p>                '/home/teletubbies/(..new/(..name<-glove_location,..object_t<-(audit|encrypted), ..perm_t<-acl );
<p>                 glove_location/..acl/(uid=357, access<-denied));
<p>                /home/teletubbies/glove_location/..audit/..new/(mailto<-"(teletubbies@pbs.org));
<p>                /home/teletubbies/glove_location<-"(we stole it quite some number of years ago,and put it in the very first loot pile (in the hole near the purple flower).);'



