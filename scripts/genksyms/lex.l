/* Lexical analysis for genksyms.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


%{

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "genksyms.h"
#include "parse.h"

/* We've got a two-level lexer here.  We let flex do basic tokenization
   and then we categorize those basic tokens in the second stage.  */
#define YY_DECL		static int yylex1(void)

%}

IDENT			[A-Za-z_][A-Za-z0-9_]*

O_INT			0[0-7]*
D_INT			[1-9][0-9]*
X_INT			0[Xx][0-9A-Fa-f]+
I_SUF			[Uu]|[Ll]|[Uu][Ll]|[Ll][Uu]
INT			({O_INT}|{D_INT}|{X_INT}){I_SUF}?

FRAC			([0-9]*\.[0-9]+)|([0-9]+\.)
EXP			[Ee][+-]?[0-9]+
F_SUF			[FfLl]
REAL			({FRAC}{EXP}?{F_SUF}?)|([0-9]+{EXP}{F_SUF}?)

STRING			L?\"([^\\\"]*\\.)*[^\\\"]*\"
CHAR			L?\'([^\\\']*\\.)*[^\\\']*\'

MC_TOKEN		([~%^&*+=|<>/-]=)|(&&)|("||")|(->)|(<<)|(>>)

/* Version 2 checksumming does proper tokenization; version 1 wasn't
   quite so pedantic.  */
%s V2_TOKENS

/* We don't do multiple input files.  */
%option noyywrap

%%


 /* Keep track of our location in the original source files.  */
^#[ \t]+{INT}[ \t]+\"[^\"\n]+\".*\n	return FILENAME;
^#.*\n					cur_line++;
\n					cur_line++;

 /* Ignore all other whitespace.  */
[ \t\f\v\r]+				;


{STRING}				return STRING;
{CHAR}					return CHAR;
{IDENT}					return IDENT;

 /* The Pedant requires that the other C multi-character tokens be
    recognized as tokens.  We don't actually use them since we don't
    parse expressions, but we do want whitespace to be arranged
    around them properly.  */
<V2_TOKENS>{MC_TOKEN}			return OTHER;
<V2_TOKENS>{INT}			return INT;
<V2_TOKENS>{REAL}			return REAL;

 /* Version 1 checksums didn't recognize ellipsis as a token, but we
    need it for proper parsing.  We'll take care of backward compatibility
    by altering the text of the token in the second stage lexer.  */
"..."					return DOTS;

 /* All other tokens are single characters.  */
.					return yytext[0];


%%

/* Bring in the keyword recognizer.  */

#include "keywords.c"


/* Macros to append to our phrase collection list.  */

#define _APP(T,L)	do {						   \
			  cur_node = next_node;				   \
			  next_node = xmalloc(sizeof(*next_node));	   \
			  next_node->next = cur_node;			   \
			  cur_node->string = memcpy(xmalloc(L+1), T, L+1); \
			  cur_node->tag = SYM_NORMAL;			   \
			} while (0)

#define APP		_APP(yytext, yyleng)


/* The second stage lexer.  Here we incorporate knowledge of the state
   of the parser to tailor the tokens that are returned.  */

int
yylex(void)
{
  static enum {
    ST_NOTSTARTED, ST_NORMAL, ST_ATTRIBUTE, ST_ASM, ST_BRACKET, ST_BRACE,
    ST_EXPRESSION, ST_TABLE_1, ST_TABLE_2, ST_TABLE_3, ST_TABLE_4,
    ST_TABLE_5, ST_TABLE_6
  } lexstate = ST_NOTSTARTED;

  static int suppress_type_lookup, dont_want_brace_phrase;
  static struct string_list *next_node;

  int token, count = 0;
  struct string_list *cur_node;

  if (lexstate == ST_NOTSTARTED)
    {
      if (checksum_version > 1)
	BEGIN(V2_TOKENS);
      next_node = xmalloc(sizeof(*next_node));
      next_node->next = NULL;
      lexstate = ST_NORMAL;
    }

repeat:
  token = yylex1();

  if (token == 0)
    return 0;
  else if (token == FILENAME)
    {
      char *file, *e;

      /* Save the filename and line number for later error messages.  */

      if (cur_filename)
	free(cur_filename);

      file = strchr(yytext, '\"')+1;
      e = strchr(file, '\"');
      *e = '\0';
      cur_filename = memcpy(xmalloc(e-file+1), file, e-file+1);
      cur_line = atoi(yytext+2);

      /* Linux kernels 2.1.18 and above (roughly) get to ignore the
	 hackery below.  */
      if (checksum_version == 1)
	{
	  static const char symtab_begin[] = "linux/symtab_begin.h";
	  static const char symtab_end[] = "linux/symtab_end.h";

	  /* When building 2.0 kernels, we are only given the output directory
	     and we're supposed to deduce the output filename from the cpp
	     output.  */

	  if (outfile == NULL)
	    {
	      char buffer[PATH_MAX], *dot, *p, *q, *f;

	      dot = strrchr(file, '.');
	      f = strrchr(file, '/');

	      if (dot) *dot = '\0';
	      f = (f ? f+1 : file);

	      snprintf(buffer, sizeof(buffer), "%s/%s.ver", output_directory, f);

	      if ((outfile = fopen(buffer, "w")) == NULL)
		{
		  perror(buffer);
		  exit(1);
		}

	      fputs("/* This file is generated by genksyms  DO NOT EDIT! */\n"
		    "#if (defined(CONFIG_MODVERSIONS) || defined(MODVERSIONS))"
		    " && !defined(__GENKSYMS__)\n", outfile);

	      q = buffer;
	      *q++ = '_';
	      for (p = f; *p ; ++p, ++q)
		*q = (*p == '.' ? '_' : toupper(*p));
	      memcpy(q, "_VER_", 6);

	      fprintf(outfile, "#ifndef %s\n", buffer);
	      fprintf(outfile, "#define %s\n", buffer);

	      if (dot) *dot = '.';
	    }

	  /* For 2.0 kernels, symbol tables are constructed in big initialized
	     arrays that begin by including <linux/symtab_begin.h> and end by
	     including <linux/symtab_end.h>.  We're going to transmute this
	     little bit o' nastiness into something that, from the perspective
	     of the grammar, resembles the 2.1 scheme in the following
	     fashion:

	     When we see a file name with a suffix matching symtab_begin we
	     should be in ST_EXPRESSION, having already seen the "= {" bits.
	     We get the grammar back to top-level in two stages, by returning
	     EXPRESSION_PHRASE then ';' as we move through ST_TABLE_1 to
	     ST_TABLE_2 eating the tokens we find that were contained in
	     symtab_begin.h.  The body of the work is done in ST_TABLE_{2,5,6}
	     by transmuting X to EXPORT_SYMBOL and ',' to ';'.  We return
	     to normal mode when we see a file name with a suffix matching
	     symtab_end.h.

	     This is not particularly pretty, but it is better than either
	     truely parsing expressions or some other bit o hackery to always
	     recognize and record "X" tokens for later perusal.  */

	  if (e-file > sizeof(symtab_begin)
	      && strcmp(e-sizeof(symtab_begin)+1, symtab_begin) == 0)
	    {
	      if (lexstate != ST_TABLE_1)
		{
		  lexstate = ST_TABLE_1;
		  return EXPRESSION_PHRASE;
		}
	    }
	  else if (e-file > sizeof(symtab_end)
		   && strcmp(e-sizeof(symtab_end)+1, symtab_end) == 0)
	    lexstate = ST_TABLE_3;
	  else if (lexstate == ST_TABLE_1)
	    {
	      lexstate = ST_TABLE_2;
	      return ';';
	    }
	  else if (lexstate == ST_TABLE_3)
	    lexstate = ST_TABLE_4;
	}

      goto repeat;
    }

  switch (lexstate)
    {
    case ST_NORMAL:
      switch (token)
	{
	case IDENT:
	  APP;
	  {
	    const struct resword *r = is_reserved_word(yytext, yyleng);
	    if (r)
	      {
		switch (token = r->token)
		  {
		  case ATTRIBUTE_KEYW:
		    lexstate = ST_ATTRIBUTE;
		    count = 0;
		    goto repeat;
		  case ASM_KEYW:
		    lexstate = ST_ASM;
		    count = 0;
		    goto repeat;

		  case STRUCT_KEYW:
		  case UNION_KEYW:
		    dont_want_brace_phrase = 3;
		  case ENUM_KEYW:
		    suppress_type_lookup = 2;
		    goto fini;

		  case EXPORT_SYMBOL_KEYW:
		    if (checksum_version > 1)
		      goto fini;
		    else
		      token = IDENT;
		  }
	      }
	    if (!suppress_type_lookup)
	      {
		struct symbol *sym = find_symbol(yytext, SYM_TYPEDEF);
		if (sym && sym->type == SYM_TYPEDEF)
		  token = TYPE;
	      }
	  }
	  break;

	case '[':
	  APP;
	  lexstate = ST_BRACKET;
	  count = 1;
	  goto repeat;

	case '{':
	  APP;
	  if (dont_want_brace_phrase)
	    break;
	  lexstate = ST_BRACE;
	  count = 1;
	  goto repeat;

	case '=': case ':':
	  APP;
	  lexstate = ST_EXPRESSION;
	  break;

	case DOTS:
	  if (checksum_version == 1)
	    {
	      _APP(". . .", 5);
	      break;
	    }
	default:
	  APP;
	  break;
	}
      break;

    case ST_ATTRIBUTE:
      APP;
      switch (token)
	{
	case '(':
	  ++count;
	  goto repeat;
	case ')':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = ATTRIBUTE_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_ASM:
      APP;
      switch (token)
	{
	case '(':
	  ++count;
	  goto repeat;
	case ')':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = ASM_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_BRACKET:
      APP;
      switch (token)
	{
	case '[':
	  ++count;
	  goto repeat;
	case ']':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = BRACKET_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_BRACE:
      APP;
      switch (token)
	{
	case '{':
	  ++count;
	  goto repeat;
	case '}':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = BRACE_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_EXPRESSION:
      switch (token)
	{
	case '(': case '[': case '{':
	  ++count;
	  APP;
	  goto repeat;
	case ')': case ']': case '}':
	  --count;
	  APP;
	  goto repeat;
	case ',': case ';':
	  if (count == 0)
	    {
	      /* Put back the token we just read so's we can find it again
		 after registering the expression.  */
	      unput(token);

	      lexstate = ST_NORMAL;
	      token = EXPRESSION_PHRASE;
	      break;
	    }
	  APP;
	  goto repeat;
	default:
	  APP;
	  goto repeat;
	}
      break;

    case ST_TABLE_1:
      goto repeat;

    case ST_TABLE_2:
      if (token == IDENT && yyleng == 1 && yytext[0] == 'X')
	{
	  token = EXPORT_SYMBOL_KEYW;
	  lexstate = ST_TABLE_5;
	  APP;
	  break;
	}
      lexstate = ST_TABLE_6;
      /* FALLTHRU */

    case ST_TABLE_6:
      switch (token)
	{
	case '{': case '[': case '(':
	  ++count;
	  break;
	case '}': case ']': case ')':
	  --count;
	  break;
	case ',':
	  if (count == 0)
	    lexstate = ST_TABLE_2;
	  break;
	};
      goto repeat;

    case ST_TABLE_3:
      goto repeat;

    case ST_TABLE_4:
      if (token == ';')
	lexstate = ST_NORMAL;
      goto repeat;

    case ST_TABLE_5:
      switch (token)
	{
	case ',':
	  token = ';';
	  lexstate = ST_TABLE_2;
	  APP;
	  break;
	default:
	  APP;
	  break;
	}
      break;

    default:
      abort();
    }
fini:

  if (suppress_type_lookup > 0)
    --suppress_type_lookup;
  if (dont_want_brace_phrase > 0)
    --dont_want_brace_phrase;

  yylval = &next_node->next;

  return token;
}
