/* wordsplit - a word splitter
   Copyright (C) 2009-2018 Sergey Poznyakoff

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef __WORDSPLIT_H
#define __WORDSPLIT_H

#include <stddef.h>

#if 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
# define __WORDSPLIT_ATTRIBUTE_FORMAT(spec) __attribute__ ((__format__ spec))
#else
# define __WORDSPLIT_ATTRIBUTE_FORMAT(spec) /* empty */
#endif

typedef struct wordsplit wordsplit_t;

/* Structure used to direct the splitting.  Members marked with [Input]
   can be defined before calling wordsplit(), those marked with [Output]
   provide return values when the function returns.  If neither mark is
   used, the member is internal and must not be used by the caller.

   In the comments below, the identifiers in parentheses indicate bits that
   must be set (or unset, if starting with !) in ws_flags (if starting with
   WRDSF_) or ws_options (if starting with WRDSO_) to initialize or use the
   given member.
   
   If not redefined explicitly, most of them are set to some reasonable
   default value upon entry to wordsplit(). */
struct wordsplit            
{
  size_t ws_wordc;          /* [Output] Number of words in ws_wordv. */
  char **ws_wordv;          /* [Output] Array of parsed out words. */
  size_t ws_offs;           /* [Input] (WRDSF_DOOFFS) Number of initial
			       elements in ws_wordv to fill with NULLs. */
  size_t ws_wordn;          /* Number of elements ws_wordv can accomodate. */ 
  unsigned ws_flags;        /* [Input] Flags passed to wordsplit. */
  unsigned ws_options;      /* [Input] (WRDSF_OPTIONS)
			       Additional options. */
  size_t ws_maxwords;       /* [Input] (WRDSO_MAXWORDS) Return at most that
			       many words */
  size_t ws_wordi;          /* [Output] (WRDSF_INCREMENTAL) Total number of
			       words returned so far */

  const char *ws_delim;     /* [Input] (WRDSF_DELIM) Word delimiters. */
  const char *ws_comment;   /* [Input] (WRDSF_COMMENT) Comment characters. */
  const char *ws_escape[2]; /* [Input] (WRDSF_ESCAPE) Characters to be escaped
			       with backslash. */
  void (*ws_alloc_die) (wordsplit_t *wsp);
                            /* [Input] (WRDSF_ALLOC_DIE) Function called when
			       out of memory.  Must not return. */
  void (*ws_error) (const char *, ...)
                   __attribute__ ((__format__ (__printf__, 1, 2)));
                            /* [Input] (WRDSF_ERROR) Function used for error
			       reporting */
  void (*ws_debug) (const char *, ...)
                   __attribute__ ((__format__ (__printf__, 1, 2)));
                            /* [Input] (WRDSF_DEBUG) Function used for debug
			       output. */
  const char **ws_env;      /* [Input] (WRDSF_ENV, !WRDSF_NOVAR) Array of
			       environment variables. */

  char **ws_envbuf;
  size_t ws_envidx;
  size_t ws_envsiz;
  
  int (*ws_getvar) (char **ret, const char *var, size_t len, void *clos);
                            /* [Input] (WRDSF_GETVAR, !WRDSF_NOVAR) Looks up
			       the name VAR (LEN bytes long) in the table of
			       variables and if found returns in memory
			       location pointed to by RET the value of that
			       variable.  Returns WRDSE_OK (0) on success,
			       and an error code (see WRDSE_* defines below)
			       on error.  User-specific errors can be returned
			       by storing the error diagnostic string in RET
			       and returning WRDSE_USERERR.
                               Whatever is stored in RET, it must be allocated
			       using malloc(3). */
  void *ws_closure;         /* [Input] (WRDSF_CLOSURE) Passed as the CLOS
			       argument to ws_getvar and ws_command. */
  int (*ws_command) (char **ret, const char *cmd, size_t len, char **argv,
                     void *clos);
	                    /* [Input] (!WRDSF_NOCMD) Returns in the memory
			       location pointed to by RET the expansion of
			       the command CMD (LEN bytes long).  If WRDSO_ARGV
			       option is set, ARGV contains CMD split out to
			       words.  Otherwise ARGV is NULL.

			       See ws_getvar for a discussion of possible
			       return values. */
	
  const char *ws_input;     /* Input string (the S argument to wordsplit. */  
  size_t ws_len;            /* Length of ws_input. */
  size_t ws_endp;           /* Points past the last processed byte in
			       ws_input. */
  int ws_errno;             /* [Output] Error code, if an error occurred. */
  char *ws_usererr;         /* Points to textual description of
			       the error, if ws_errno is WRDSE_USERERR.  Must
			       be allocated with malloc(3). */
  struct wordsplit_node *ws_head, *ws_tail;
                            /* Doubly-linked list of parsed out nodes. */
  int ws_lvl;               /* Invocation nesting level. */
};

/* Initial size for ws_env, if allocated automatically */
#define WORDSPLIT_ENV_INIT 16

/* Wordsplit flags. */
/* Append the words found to the array resulting from a previous
   call. */
#define WRDSF_APPEND            0x00000001
/* Insert ws_offs initial NULLs in the array ws_wordv.
   (These are not counted in the returned ws_wordc.) */
#define WRDSF_DOOFFS            0x00000002
/* Don't do command substitution. */
#define WRDSF_NOCMD             0x00000004
/* The parameter p resulted from a previous call to
   wordsplit(), and wordsplit_free() was not called. Reuse the
   allocated storage. */
#define WRDSF_REUSE             0x00000008
/* Print errors */
#define WRDSF_SHOWERR           0x00000010
/* Consider it an error if an undefined variable is expanded. */
#define WRDSF_UNDEF             0x00000020
/* Don't do variable expansion. */
#define WRDSF_NOVAR             0x00000040
/* Abort on ENOMEM error */
#define WRDSF_ENOMEMABRT        0x00000080
/* Trim off any leading and trailind whitespace */
#define WRDSF_WS                0x00000100
/* Handle single quotes */
#define WRDSF_SQUOTE            0x00000200
/* Handle double quotes */
#define WRDSF_DQUOTE            0x00000400
/* Handle single and double quotes */
#define WRDSF_QUOTE             (WRDSF_SQUOTE|WRDSF_DQUOTE)
/* Replace each input sequence of repeated delimiters with a single
   delimiter */
#define WRDSF_SQUEEZE_DELIMS    0x00000800
/* Return delimiters */
#define WRDSF_RETURN_DELIMS     0x00001000
/* Treat sed expressions as words */
#define WRDSF_SED_EXPR          0x00002000
/* ws_delim field is initialized */
#define WRDSF_DELIM             0x00004000
/* ws_comment field is initialized */
#define WRDSF_COMMENT           0x00008000
/* ws_alloc_die field is initialized */
#define WRDSF_ALLOC_DIE         0x00010000
/* ws_error field is initialized */
#define WRDSF_ERROR             0x00020000
/* ws_debug field is initialized */
#define WRDSF_DEBUG             0x00040000
/* ws_env field is initialized */
#define WRDSF_ENV               0x00080000
/* ws_getvar field is initialized */
#define WRDSF_GETVAR            0x00100000
/* enable debugging */
#define WRDSF_SHOWDBG           0x00200000
/* Don't split input into words.  Useful for side effects. */
#define WRDSF_NOSPLIT           0x00400000
/* Keep undefined variables in place, instead of expanding them to
   empty strings. */
#define WRDSF_KEEPUNDEF         0x00800000
/* Warn about undefined variables */
#define WRDSF_WARNUNDEF         0x01000000
/* Handle C escapes */
#define WRDSF_CESCAPES          0x02000000
/* ws_closure is set */
#define WRDSF_CLOSURE           0x04000000
/* ws_env is a Key/Value environment, i.e. the value of a variable is
   stored in the element that follows its name. */
#define WRDSF_ENV_KV            0x08000000
/* ws_escape is set */
#define WRDSF_ESCAPE            0x10000000
/* Incremental mode */
#define WRDSF_INCREMENTAL       0x20000000
/* Perform pathname and tilde expansion */
#define WRDSF_PATHEXPAND        0x40000000
/* ws_options is initialized */
#define WRDSF_OPTIONS           0x80000000

#define WRDSF_DEFFLAGS	       \
  (WRDSF_NOVAR | WRDSF_NOCMD | \
   WRDSF_QUOTE | WRDSF_SQUEEZE_DELIMS | WRDSF_CESCAPES)

/* Remove the word that produces empty string after path expansion */
#define WRDSO_NULLGLOB        0x00000001
/* Print error message if path expansion produces empty string */
#define WRDSO_FAILGLOB        0x00000002
/* Allow a leading period to be matched by metacharacters. */
#define WRDSO_DOTGLOB         0x00000004
/* ws_command needs argv parameter */
#define WRDSO_ARGV            0x00000008
/* Keep backslash in unrecognized escape sequences in words */
#define WRDSO_BSKEEP_WORD     0x00000010
/* Handle octal escapes in words */
#define WRDSO_OESC_WORD       0x00000020
/* Handle hex escapes in words */
#define WRDSO_XESC_WORD       0x00000040

/* ws_maxwords field is initialized */
#define WRDSO_MAXWORDS        0x00000080

/* Keep backslash in unrecognized escape sequences in quoted strings */
#define WRDSO_BSKEEP_QUOTE    0x00000100
/* Handle octal escapes in quoted strings */
#define WRDSO_OESC_QUOTE      0x00000200
/* Handle hex escapes in quoted strings */
#define WRDSO_XESC_QUOTE      0x00000400

#define WRDSO_BSKEEP          WRDSO_BSKEEP_WORD     
#define WRDSO_OESC            WRDSO_OESC_WORD       
#define WRDSO_XESC            WRDSO_XESC_WORD       

/* Indices into ws_escape */
#define WRDSX_WORD  0
#define WRDSX_QUOTE 1

/* Set escape option F in WS for words (Q==0) or quoted strings (Q==1) */
#define WRDSO_ESC_SET(ws,q,f) ((ws)->ws_options |= ((f) << 4*(q)))
/* Test WS for escape option F for words (Q==0) or quoted strings (Q==1) */
#define WRDSO_ESC_TEST(ws,q,f) ((ws)->ws_options & ((f) << 4*(q)))

#define WRDSE_OK         0
#define WRDSE_EOF        WRDSE_OK
#define WRDSE_QUOTE      1
#define WRDSE_NOSPACE    2
#define WRDSE_USAGE      3
#define WRDSE_CBRACE     4
#define WRDSE_UNDEF      5
#define WRDSE_NOINPUT    6
#define WRDSE_PAREN      7
#define WRDSE_GLOBERR    8
#define WRDSE_USERERR    9

int wordsplit (const char *s, wordsplit_t *ws, unsigned flags);
int wordsplit_len (const char *s, size_t len, wordsplit_t *ws, unsigned flags);
void wordsplit_free (wordsplit_t *ws);
void wordsplit_free_words (wordsplit_t *ws);
void wordsplit_free_envbuf (wordsplit_t *ws);
int wordsplit_get_words (wordsplit_t *ws, size_t *wordc, char ***wordv);

static inline void wordsplit_getwords (wordsplit_t *ws, size_t *wordc, char ***wordv)
  __attribute__ ((deprecated));

static inline void
wordsplit_getwords (wordsplit_t *ws, size_t *wordc, char ***wordv)
{
  wordsplit_get_words (ws, wordc, wordv);
}

int wordsplit_append (wordsplit_t *wsp, int argc, char **argv);

int wordsplit_c_unquote_char (int c);
int wordsplit_c_quote_char (int c);
size_t wordsplit_c_quoted_length (const char *str, int quote_hex, int *quote);
void wordsplit_c_quote_copy (char *dst, const char *src, int quote_hex);

void wordsplit_perror (wordsplit_t *ws);
const char *wordsplit_strerror (wordsplit_t *ws);

void wordsplit_clearerr (wordsplit_t *ws);

#endif
