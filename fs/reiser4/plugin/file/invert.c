/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Suppose you want to conveniently read and write a large variety of small files conveniently within a single emacs
   buffer, without having a separate buffer for each 8 byte or so file.  Inverts are the way to do that.  An invert
   provides you with the contents of a set of subfiles plus its own contents.  It is a file which inherits other files
   when you read it, and allows you to write to it and through it to the files that it inherits from.  In order for it
   to know which subfiles each part of your write should go into, there must be delimiters indicating that.  It tries to
   make that easy for you by providing those delimiters in what you read from it.

  When you read it, an invert performs an inverted assignment.  Instead of taking an assignment command and writing a
  bunch of files, it takes a bunch of files and composes an assignment command for you to read from it that if executed
  would create those files.  But which files?  Well, that must be specified in the body of the invert using a special
  syntax, and that specification is called the invert of the assignment.

  When written to, an invert performs the assignment command that is written
  to it, and modifies its own body to contain the invert of that
  assignment.

  In other words, writing to an invert file what you have read from it
  is the identity operation.

  Malformed assignments cause write errors.  Partial writes are not
  supported in v4.0, but will be.

  Example:

    If an invert contains:

    /filenameA/<>+"(some text stored in the invert)+/filenameB/<>

======================
Each element in this definition should be an invert, and all files
should be called recursively - too.  This is bad. If one of the
included files in not a regular or invert file, then we can't read
main file.

I think to make it is possible easier:

internal structure of invert file should be like symlink file. But
read and write method should be explitely indicated in i/o operation..

By default we read and write (if probably) as symlink and if we
specify ..invert at reading time that too we can specify it at write time.

example:
/my_invert_file/..invert<- ( (/filenameA<-"(The contents of filenameA))+"(some text stored in the invert)+(/filenameB<-"(The contents of filenameB) ) )
will create  /my_invert_file as invert, and will creat /filenameA and /filenameB with specified body.

read of /my_invert_file/..invert will be
/filenameA<-"(The contents of filenameA)+"(some text stored in the invert)+/filenameB<-"(The contents of filenameB)

but read of /my_invert_file/ will be
The contents of filenameAsome text stored in the invertThe contents of filenameB

we also can creat this file as
/my_invert_file/<-/filenameA+"(some text stored in the invert)+/filenameB
will create  /my_invert_file , and use existing files /filenameA and /filenameB.

and when we will read it will be as previously invert file.

This is correct?

 vv
DEMIDOV-FIXME-HANS:

Maybe you are right, but then you must disable writes to /my_invert_file/ and only allow writes to /my_invert_file/..invert

Do you agree?  Discuss it on reiserfs-list....

-Hans
=======================

  Then a read will return:

    /filenameA<-"(The contents of filenameA)+"(some text stored in the invert)+/filenameB<-"(The contents of filenameB)

    and a write of the line above to the invert will set the contents of
    the invert and filenameA and filenameB to their original values.

  Note that the contents of an invert have no influence on the effect
  of a write unless the write is a partial write (and a write of a
  shorter file without using truncate first is a partial write).

  truncate() has no effect on filenameA and filenameB, it merely
  resets the value of the invert.

  Writes to subfiles via the invert are implemented by preceding them
  with truncates.

  Parse failures cause write failures.

  Questions to ponder: should the invert be acted on prior to file
  close when writing to an open filedescriptor?

 Example:

 If an invert contains:

   "(This text and a pair of quotes are all that is here.)

Then a read will return:

   "(This text and a pair of quotes are all that is here.)

*/

/* OPEN method places a struct file in memory associated with invert body
  and returns something like file descriptor to the user for the future access
  to the invert file.
  During opening we parse the body of invert and get a list of the 'entryes'
  (that describes all its subfiles) and place pointer on the first struct in
  reiserfs-specific part of invert inode (arbitrary decision).

  Each subfile is described by the struct inv_entry that has a pointer @sd on
  in-core based stat-data and  a pointer on struct file @f (if we find that the
  subfile uses more then one unformated node (arbitrary decision), we load
  struct file in memory, otherwise we load base stat-data (and maybe 1-2 bytes
  of some other information we need)

  Since READ and WRITE methods for inverts were formulated in assignment
  language, they don't contain arguments 'size' and 'offset' that make sense
  only in ordinary read/write methods.

  READ method is a combination of two methods:
  1) ordinary read method (with offset=0, lenght = @f->...->i_size) for entries
  with @f != 0, this method uses pointer on struct file as an argument
  2) read method for inode-less files with @sd != 0, this method uses
  in-core based stat-data instead struct file as an argument.
  in the first case we don't use pagecache, just copy data that we got after
  cbk() into userspace.

  WRITE method for invert files is more complex.
  Besides declared WRITE-interface in assignment languageb above we need
  to have an opportunity to edit unwrapped body of invert file with some
  text editor, it means we need GENERIC WRITE METHOD for invert file:

  my_invert_file/..invert <- "string"

  this method parses "string" and looks for correct subfile signatures, also
  the parsing process splits this "string" on the set of flows in  accordance
  with the set of subfiles specified by this signarure.
  The found list of signatures #S is compared with the opened one #I of invert
  file. If it doesn't have this one (#I==0, it will be so for instance if we
  have just create this invert file) the write method assignes found signature
  (#I=#S;) to the invert file. Then if #I==#S, generic write method splits
  itself to the some write methods for ordinary or light-weight, or call itself
  recursively for invert files with corresponding flows.
  I am not sure, but the list of signatures looks like what mr.Demidov means
  by 'delimiters'.

  The cases when #S<#I (#I<#S) (in the sense of set-theory) are also available
  and cause delete (create new) subfiles (arbitrary decision - it may looks
  too complex, but this interface will be the completest). The order of entries
  of list #S (#I) and inherited order on #I (#S) must coincide.
  The other parsing results give malformed signature that aborts READ method
  and releases all resources.


  Format of subfile (entry) signature:

  "START_MAGIC"<>(TYPE="...",LOOKUP_ARG="...")SUBFILE_BODY"END_MAGIC"

  Legend:

    START_MAGIC - keyword indicates the start of subfile signature;

    <> indicates the start of 'subfile metadata', that is the pair
  (TYPE="...",LOOKUP_ARG="...") in parenthesis separated by comma.

    TYPE - the string "type" indicates the start of one of the three words:
  - ORDINARY_FILE,
  - LIGHT_WEIGHT_FILE,
  - INVERT_FILE;

    LOOKUP_ARG - lookup argument depends on previous type:
  */

 /************************************************************/
 /*       TYPE        *          LOOKUP ARGUMENT             */
 /************************************************************/
 /* LIGH_WEIGHT_FILE  *           stat-data key              */
 /************************************************************/
 /*   ORDINARY_FILE   *             filename                 */
 /************************************************************/
 /*   INVERT_FILE     *             filename                 */
 /************************************************************/

 /* where:
  *stat-data key - the string contains stat data key of this subfile, it will be
  passed to fast-access lookup method for light-weight files;
  *filename - pathname of this subfile, iyt well be passed to VFS lookup methods
  for ordinary and invert files;

  SUBFILE_BODY - data of this subfile (it will go to the flow)
  END_MAGIC - the keyword indicates the end of subfile signature.

  The other simbols inside the signature interpreted as 'unformatted content',
  which is available with VFS's read_link() (arbitraruy decision).

  NOTE: Parse method for a body of invert file uses mentioned signatures _without_
  subfile bodies.

  Now the only unclear thing is WRITE in regular light-weight subfile A that we
  can describe only in  assignment language:

  A <- "some_string"

  I guess we don't want to change stat-data and body items of file A
  if this file exist, and size(A) != size("some_string") because this operation is
  expencive, so we only do the partial write if size(A) > size("some_string")
  and do truncate of the "some_string", and then do A <- "truncated string", if
  size(A) < size("some_string"). This decision is also arbitrary..
  */

/* here is infrastructure for formated flows */

#define SUBFILE_HEADER_MAGIC 0x19196605
#define FLOW_HEADER_MAGIC 0x01194304

#include "../plugin.h"
#include "../../debug.h"
#include "../../forward.h"
#include "../object.h"
#include "../item/item.h"
#include "../item/static_stat.h"
#include "../../dformat.h"
#include "../znode.h"
#include "../inode.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct file  */
#include <linux/list.h>		/* for struct list_head */

typedef enum {
	LIGHT_WEIGHT_FILE,
	ORDINARY_FILE,
	INVERT_FILE
} inv_entry_type;

typedef struct flow_header {
	d32 fl_magic;
	d16 fl_nr;		/* number of subfiles in the flow */
};

typedef struct subfile_header {
	d32 sh_magic;		/* subfile magic */
	d16 sh_type;		/* type of subfile: light-weight, ordinary, invert */
	d16 sh_arg_len;		/* lenght of lookup argument (filename, key) */
	d32 sh_body_len;	/* lenght of subfile body */
};

/* functions to get/set fields of flow header */

static void
fl_set_magic(flow_header * fh, __u32 value)
{
	cputod32(value, &fh->fh_magic);
}

static __u32
fl_get_magic(flow_header * fh)
{
	return d32tocpu(&fh->fh_magic);
}
static void
fl_set_number(flow_header * fh, __u16 value)
{
	cputod16(value, &fh->fh_nr);
}
static unsigned
fl_get_number(flow_header * fh)
{
	return d16tocpu(&fh->fh_nr);
}

/* functions to get/set fields of subfile header */

static void
sh_set_magic(subfile_header * sh, __u32 value)
{
	cputod32(value, &sh->sh_magic);
}

static __u32
sh_get_magic(subfile_header * sh)
{
	return d32tocpu(&sh->sh_magic);
}
static void
sh_set_type(subfile_header * sh, __u16 value)
{
	cputod16(value, &sh->sh_magic);
}
static unsigned
sh_get_type(subfile_header * sh)
{
	return d16tocpu(&sh->sh_magic);
}
static void
sh_set_arg_len(subfile_header * sh, __u16 value)
{
	cputod16(value, &sh->sh_arg_len);
}
static unsigned
sh_get_arg_len(subfile_header * sh)
{
	return d16tocpu(&sh->sh_arg_len);
}
static void
sh_set_body_len(subfile_header * sh, __u32 value)
{
	cputod32(value, &sh->sh_body_len);
}

static __u32
sh_get_body_len(subfile_header * sh)
{
	return d32tocpu(&sh->sh_body_len);
}

/* in-core minimal stat-data, light-weight analog of inode */

struct incore_sd_base {
	umode_t isd_mode;
	nlink_t isd_nlink;
	loff_t isd_size;
	char *isd_data;		/* 'subflow' to write */
};

/* open invert create a list of invert entries,
   every entry is represented by structure inv_entry */

struct inv_entry {
	struct list_head *ie_list;
	struct file *ie_file;	/* this is NULL if the file doesn't
				   have unformated nodes */
	struct incore_sd_base *ie_sd;	/* inode-less analog of struct file */
};

/* allocate and init invert entry */

static struct inv_entry *
allocate_inv_entry(void)
{
	struct inv_entry *inv_entry;

	inv_entry = reiser4_kmalloc(sizeof (struct inv_entry), GFP_KERNEL);
	if (!inv_entry)
		return ERR_PTR(RETERR(-ENOMEM));
	inv_entry->ie_file = NULL;
	inv_entry->ie_sd = NULL;
	INIT_LIST_HEAD(&inv_entry->ie_list);
	return inv_entry;
}

static int
put_inv_entry(struct inv_entry *ientry)
{
	int result = 0;

	assert("edward-96", ientry != NULL);
	assert("edward-97", ientry->ie_list != NULL);

	list_del(ientry->ie_list);
	if (ientry->ie_sd != NULL) {
		kfree(ientry->ie_sd);
		kfree(ientry);
	}
	if (ientry->ie_file != NULL)
		result = filp_close(ientry->file, NULL);
	return result;
}

static int
allocate_incore_sd_base(struct inv_entry *inv_entry)
{
	struct incore_sd_base *isd_base assert("edward-98", inv_entry != NULL);
	assert("edward-99", inv_entry->ie_inode = NULL);
	assert("edward-100", inv_entry->ie_sd = NULL);

	isd_base = reiser4_kmalloc(sizeof (struct incore_sd_base), GFP_KERNEL);
	if (!isd_base)
		return RETERR(-ENOMEM);
	inv_entry->ie_sd = isd_base;
	return 0;
}

/* this can be installed as ->init_inv_entry () method of
   item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c).
   Copies data from on-disk stat-data format into light-weight analog of inode .
   Doesn't hanlde stat-data extensions. */

static void
sd_base_load(struct inv_entry *inv_entry, char *sd)
{
	reiser4_stat_data_base *sd_base;

	assert("edward-101", inv_entry != NULL);
	assert("edward-101", inv_entry->ie_sd != NULL);
	assert("edward-102", sd != NULL);

	sd_base = (reiser4_stat_data_base *) sd;
	inv_entry->incore_sd_base->isd_mode = d16tocpu(&sd_base->mode);
	inv_entry->incore_sd_base->isd_nlink = d32tocpu(&sd_base->nlink);
	inv_entry->incore_sd_base->isd_size = d64tocpu(&sd_base->size);
	inv_entry->incore_sd_base->isd_data = NULL;
}

/* initialise incore stat-data */

static void
init_incore_sd_base(struct inv_entry *inv_entry, coord_t * coord)
{
	reiser4_plugin *plugin = item_plugin_by_coord(coord);
	void *body = item_body_by_coord(coord);

	assert("edward-103", inv_entry != NULL);
	assert("edward-104", plugin != NULL);
	assert("edward-105", body != NULL);

	sd_base_load(inv_entry, body);
}

/* takes a key or filename and allocates new invert_entry,
   init and adds it into the list,
   we use lookup_sd_by_key() for light-weight files and VFS lookup by filename */

int
get_inv_entry(struct inode *invert_inode,	/* inode of invert's body */
	      inv_entry_type type,	/* LIGHT-WEIGHT or ORDINARY */
	      const reiser4_key * key,	/* key of invert entry stat-data */
	      char *filename,	/* filename of the file to be opened */
	      int flags, int mode)
{
	int result;
	struct inv_entry *ientry;

	assert("edward-107", invert_inode != NULL);

	ientry = allocate_inv_entry();
	if (IS_ERR(ientry))
		return (PTR_ERR(ientry));

	if (type == LIGHT_WEIGHT_FILE) {
		coord_t coord;
		lock_handle lh;

		assert("edward-108", key != NULL);

		init_coord(&coord);
		init_lh(&lh);
		result = lookup_sd_by_key(tree_by_inode(invert_inode), ZNODE_READ_LOCK, &coord, &lh, key);
		if (result == 0)
			init_incore_sd_base(ientry, coord);

		done_lh(&lh);
		done_coord(&coord);
		return (result);
	} else {
		struct file *file = filp_open(filename, flags, mode);
		/* FIXME_EDWARD here we need to check if we
		   did't follow to any mount point */

		assert("edward-108", filename != NULL);

		if (IS_ERR(file))
			return (PTR_ERR(file));
		ientry->ie_file = file;
		return 0;
	}
}

/* takes inode of invert, reads the body of this invert, parses it,
   opens all invert entries and return pointer on the first inv_entry */

struct inv_entry *
open_invert(struct file *invert_file)
{

}

ssize_t subfile_read(struct *invert_entry, flow * f)
{

}

ssize_t subfile_write(struct *invert_entry, flow * f)
{

}

ssize_t invert_read(struct *file, flow * f)
{

}

ssize_t invert_write(struct *file, flow * f)
{

}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
