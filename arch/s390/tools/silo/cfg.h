/* cfg.h  -  Configuration file parser */

/* Copyright 1992-1996 Werner Almesberger. See file COPYING for details. */


#ifndef CFG_H
#define CFG_H

typedef enum { cft_strg, cft_flag, cft_link, cft_end } CFG_TYPE;

typedef struct {
    CFG_TYPE type;
    char *name;
    void *action;
    void *data;
    void *context;
} CONFIG;

extern int cfg_open(char *name);

/* Opens the configuration file. Returns the file descriptor of the open
   file. */

extern void cfg_error(char *msg,...);

/* Signals an error while parsing the configuration file and terminates the
   program. */

extern void cfg_init(CONFIG *table);

/* Initializes the specified table. */

extern void cfg_set(CONFIG *table,char *item,char *value,void *context);

/* Sets the specified variable in table. If the variable has already been set
   since the last call to cfg_init, a warning message is issued if the context
   keys don't match or a fatal error is reported if they do. */

extern void cfg_unset(CONFIG *table,char *item);

/* Unsets the specified variable in table. It is a fatal error if the variable
   was not set. */

extern int cfg_parse(CONFIG *table);

/* Parses the configuration file for variables contained in table. A non-zero
   value is returned if a variable not found in table has been met. Zero is
   returned if EOF has been reached. */

extern int cfg_get_flag(CONFIG *table,char *item);

/* Returns one if the specified variable is set, zero if it isn't. */

extern char *cfg_get_strg(CONFIG *table,char *item);

/* Returns the value of the specified variable if it is set, NULL otherwise. */

#endif
