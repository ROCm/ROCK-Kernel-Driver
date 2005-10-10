/*  -*- asm -*-
 *   Creation Date: <2001/12/30 20:08:53 samuel>
 *   Time-stamp: <2002/01/14 00:48:09 samuel>
 *   
 *	<asm.m4>
 *	
 *	m4 initialization (m4 is used as an assembly preprocessor)
 *   
 *   Copyright (C) 2001, 2002 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

/* This end-of-quote matches the start-of-quote in mol_config.h */
]]]]]
changequote([,])
	
dnl m4 macros to avoid in header files (we can not rename these)
dnl ==========================================================
dnl  shift, eval, expr, decr, incr, ifelse, popdef, pushdef


dnl **************************************************************
dnl * Rename to reduce namespace conflicts
dnl **************************************************************
	
dnl *** Changing the name of built-in macros using defn does not always work ***
	
undefine([changecom])
undefine([changequote])
dnl undefine([decr])
undefine([defn])
undefine([divert])
undefine([divnum])
undefine([errprint])
dnl undefine([eval])
dnl undefine([expr])
undefine([file])
undefine([format])
undefine([len])
undefine([line])
dnl undefine([ifelse])
dnl undefine([incr])
undefine([indir])
undefine([include])
undefine([index])
undefine([maketemp])
undefine([paste])
undefine([patsubst])
dnl undefine([popdef])
dnl undefine([pushdef])
undefine([regexp])
dnl undefine([shift])
undefine([sinclude])
undefine([spaste])
undefine([substr])
undefine([syscmd])
undefine([sysval])
undefine([translit])
undefine([traceoff])
undefine([traceon])
undefine([undivert])
undefine([unix])
dnl undefine([__gnu__])
dnl undefine([__unix__])

dnl Uncomment to list m4 definitions
dnl	dumpdef m4exit

/************************************************************************/
/*	M4 Macros	 						*/
/************************************************************************/

/* WARNING - M4 BUG IN MacOS X (10.1.2):
 * eval() in MacOS X (10.1.2) handles '&' as '&&' and '|' as '||'.
 */

/* FORLOOP(var, from, to, [body var...]) */
define([mFORLOOP], [pushdef([$1], [$2])_mFORLOOP([$1], [$2], [$3], [$4])popdef([$1])])
define([_mFORLOOP], [$4[]ifelse($1, [$3], ,
	 [define([$1], incr($1))_mFORLOOP([$1], [$2], [$3], [$4])])])

define([mFIRST],[$1])
define([mCONCAT_C],[ [$@] ])

/* FOREACH(var, [item1, ...], [body var ...]) */
define([mFOREACH],[pushdef([$1],mFIRST($2))_mFOREACH([$1],[shift($2)],[$3])popdef([$1])])
define([_mFOREACH],[$3] [ifelse(mFIRST($2),,,[define([$1],mFIRST($2)) _mFOREACH([$1],[shift($2)],[$3])])])


/******************** Nice macro definitions **************************/

/* MACRO(name, [param1, ...], [body _param1 ...]) */
#ifdef __linux__
define([MACRO], [
	.macro [$1] $2
	mFOREACH([i],[$2],[ pushdef(_[]i,\i) ])
	$3
	.endm
	mFOREACH([i],[$2],[ popdef(_[]i) ])
])
#else
define([MACRO], [
	.macro [$1] 
	pushdef([_n],0)
	mFOREACH([i],[$2],[ pushdef(_[]i,[$[]]_n) define([_n],incr(_n)) ]) 
	$3
	.endmacro
	mFOREACH([i],[$2],[ popdef(_[]i) ])
	popdef([_n])
])
#endif
define([MACRO_0], [MACRO([$1],[_dummy_param_],[$2])])


/* mDEFINE(name, [param1, ...], [body _param1 ...]) */
define([mDEFINE], [
	pushdef([_n],1)
	mFOREACH([i],[$2],[ pushdef(_[]i,[$[]]_n) define([_n],incr(_n)) ])
	define([$1], mCONCAT_C($3) )
	mFOREACH([i],[$2],[ popdef(_[]i) ])
	popdef([_n])
])


/* rLABEL(label): b label_b ; b label_f */
define(rLABEL,[dnl
ifdef([$1]_curnum,,[$1[]f:])dnl
	define([_tmp_curnum],ifdef($1[]_curnum, [eval($1_curnum+1)], 1)) dnl
	define([$1]_curnum,_tmp_curnum)dnl
	define([$1]f,$1_[]eval($1_curnum[]+1) )dnl
	define([$1]b,$1_[]$1_curnum[] )
$1[]_[]$1_curnum[]dnl
])

