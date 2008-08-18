/*
 * $Id: lc_eval.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of lcrash, an analysis tool for Linux memory dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, and others
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __LC_EVAL_H
#define __LC_EVAL_H

typedef struct type_s {
	int	 flag;
	union {
		struct type_s	*next;
		kltype_t	*kltp;
	} un;
} type_t;

#define t_next un.next
#define t_kltp un.kltp

/* Structure to hold info on "tokens" extracted from eval and print
 * command input strings.
 */
typedef struct token_s {
	short	 	type;
	short    	operator;	/* if token is an operator */
	char            *string;	/* string holding value or identifier */
	char            *ptr;		/* pointer to start of token */
	struct token_s  *next;		/* next token in the chain */
} token_t;

/* Structure returned by the eval() function containing the result
 * of an expression evaluation. This struct is also used to build the
 * parse tree for the expression.
 */
typedef struct node_s {
	struct node_s	*next;		/* linked list pointer */
	unsigned char    node_type;	/* type of node */
	unsigned short   flags;		/* see below */
	unsigned char    operator;	/* operator if node is type OPERATOR */
	unsigned char	 byte_size; 	/* byte_size of base_type values */
	char		*name;		/* name of variable or struct member */
	/* value and address are uint64_t in lcrash, but for ia32 ... */
	unsigned long long value;	/* numeric value or pointer */
	unsigned long    address;	/* address (could be same as pointer) */
	type_t		*type;  	/* pointer to type related info */
	char            *tok_ptr;	/* pointer to token in cmd string */
	struct node_s   *left;		/* pointer to left child */
	struct node_s   *right;		/* pointer to right child */
} node_t;

/* Token and Node types
 */
#define OPERATOR       		  1
#define NUMBER     		  2
#define INDEX     		  3
#define TYPE_DEF		  4
#define VADDR        		  5
#define MEMBER			  6
#define STRING			  7
#define TEXT			  8
#define CHARACTER		  9
#define EVAL_VAR    		 10

/* Flag values
 */
#define STRING_FLAG           0x001
#define ADDRESS_FLAG          0x002
#define INDIRECTION_FLAG      0x004
#define POINTER_FLAG          0x008
#define MEMBER_FLAG           0x010
#define BOOLIAN_FLAG          0x020
#define KLTYPE_FLAG           0x040
#define NOTYPE_FLAG           0x080
#define UNSIGNED_FLAG         0x100
#define VOID_FLAG             0x200

/* Flag value for print_eval_error() function
 */
#define CMD_NAME_FLG		  1  /* cmdname is not name of a command */
#define CMD_STRING_FLG		  2  /* cmdname is not name of a command */

/* Expression operators in order of precedence.
 */
#define CONDITIONAL		  1
#define CONDITIONAL_ELSE          2
#define LOGICAL_OR		  3
#define LOGICAL_AND		  4
#define BITWISE_OR		  5
#define BITWISE_EXCLUSIVE_OR      6
#define BITWISE_AND		  7
#define EQUAL			  8
#define NOT_EQUAL		  9
#define LESS_THAN		 10
#define GREATER_THAN		 11
#define LESS_THAN_OR_EQUAL	 12
#define GREATER_THAN_OR_EQUAL    13
#define RIGHT_SHIFT		 14
#define LEFT_SHIFT 		 15
#define ADD			 16
#define SUBTRACT		 17
#define MULTIPLY		 18
#define DIVIDE			 19
#define MODULUS			 20
#define LOGICAL_NEGATION	 21
#define ONES_COMPLEMENT		 22
#define PREFIX_INCREMENT	 23
#define PREFIX_DECREMENT	 24
#define POSTFIX_INCREMENT	 25
#define POSTFIX_DECREMENT	 26
#define CAST			 27
#define UNARY_MINUS		 28
#define UNARY_PLUS		 29
#define INDIRECTION		 30
#define ADDRESS			 31
#define SIZEOF			 32
#define RIGHT_ARROW		 33
#define DOT 			 34
#define OPEN_PAREN 		100
#define CLOSE_PAREN	 	101
#define OPEN_SQUARE_BRACKET 	102
#define CLOSE_SQUARE_BRACKET	103
#define SEMI_COLON		104
#define NOT_YET 		 -1

/* Errors codes primarily for use with eval (print) functions
 */
#define E_OPEN_PAREN           1100
#define E_CLOSE_PAREN          1101
#define E_BAD_STRUCTURE        1102
#define E_MISSING_STRUCTURE    1103
#define E_BAD_MEMBER           1104
#define E_BAD_OPERATOR         1105
#define E_BAD_OPERAND          1106
#define E_MISSING_OPERAND      1107
#define E_BAD_TYPE             1108
#define E_NOTYPE               1109
#define E_BAD_POINTER          1110
#define E_BAD_INDEX            1111
#define E_BAD_CHAR             1112
#define E_BAD_STRING           1113
#define E_END_EXPECTED         1114
#define E_BAD_EVAR             1115  /* Bad eval variable */
#define E_BAD_VALUE	       1116
#define E_NO_VALUE	       1117
#define E_DIVIDE_BY_ZERO       1118
#define E_BAD_CAST             1119
#define E_NO_ADDRESS           1120
#define E_SINGLE_QUOTE         1121

#define E_BAD_WHATIS           1197
#define E_NOT_IMPLEMENTED      1198
#define E_SYNTAX_ERROR         1199

extern uint64_t eval_error;
extern char *error_token;

/* Function prototypes
 */
node_t *eval(char **, int);
void print_eval_error(char *, char *, char *, uint64_t, int);
void free_nodes(node_t *);

/* Struct to hold information about eval variables
 */
typedef struct variable_s {
	btnode_t	 v_bt;      /* Must be first */
	int		 v_flags;
	char		*v_exp;     /* What was entered on command line      */
	char		*v_typestr; /* Actual type string after eval() call  */
	node_t		*v_node;
} variable_t;

#define v_left v_bt.bt_left
#define v_right v_bt.bt_right
#define v_name v_bt.bt_key

/* Flag values
 */
#define V_PERM          0x001  /* can't be unset - can be modified           */
#define V_DEFAULT       0x002  /* set at startup                             */
#define V_NOMOD         0x004  /* cannot be modified                         */
#define V_TYPEDEF       0x008  /* contains typed data                        */
#define V_REC_STRUCT    0x010  /* direct ref to struct/member (not pointer)  */
#define V_STRING	0x020  /* contains ASCII string (no type)            */
#define V_COMMAND	0x040  /* contains command string (no type)          */
#define V_OPTION 	0x080  /* contains option flag (e.g., $hexints)      */
#define V_PERM_NODE     0x100  /* Don't free node after setting variable     */

/* Variable table struct
 */
typedef struct vtab_s {
	variable_t	*vt_root;
	int		 vt_count;
} vtab_t;

extern vtab_t *vtab;    /* Pointer to table of eval variable info    */

/* Function Prototypes
 */
variable_t *make_variable(char *, char *, node_t *, int);
void clean_variable(variable_t *);
void free_variable(variable_t *);
void init_variables(vtab_t *);
int set_variable(vtab_t *, char *, char *, node_t *, int);
int unset_variable(vtab_t *, variable_t *);
variable_t *find_variable(vtab_t *, char *, int);
kltype_t *number_to_type(node_t *);
void free_eval_memory(void);
/* cpw: was int print_eval_results(node_t *, FILE *, int); */
int print_eval_results(node_t *, int);

#endif /* __LC_EVAL_H */
