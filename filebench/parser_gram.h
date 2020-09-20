/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_YY_PARSER_GRAM_H_INCLUDED
# define YY_YY_PARSER_GRAM_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    FSC_LIST = 258,
    FSC_DEFINE = 259,
    FSC_QUIT = 260,
    FSC_DEBUG = 261,
    FSC_CREATE = 262,
    FSC_SLEEP = 263,
    FSC_SET = 264,
    FSC_SYSTEM = 265,
    FSC_EVENTGEN = 266,
    FSC_ECHO = 267,
    FSC_RUN = 268,
    FSC_PSRUN = 269,
    FSC_VERSION = 270,
    FSC_ENABLE = 271,
    FSC_DOMULTISYNC = 272,
    FSV_STRING = 273,
    FSV_VAL_POSINT = 274,
    FSV_VAL_NEGINT = 275,
    FSV_VAL_BOOLEAN = 276,
    FSV_VARIABLE = 277,
    FSV_WHITESTRING = 278,
    FSV_RANDUNI = 279,
    FSV_RANDTAB = 280,
    FSV_URAND = 281,
    FSV_RAND48 = 282,
    FSE_FILE = 283,
    FSE_FILES = 284,
    FSE_FILESET = 285,
    FSE_PROC = 286,
    FSE_THREAD = 287,
    FSE_FLOWOP = 288,
    FSE_CVAR = 289,
    FSE_RAND = 290,
    FSE_MODE = 291,
    FSE_MULTI = 292,
    FSK_SEPLST = 293,
    FSK_OPENLST = 294,
    FSK_CLOSELST = 295,
    FSK_OPENPAR = 296,
    FSK_CLOSEPAR = 297,
    FSK_ASSIGN = 298,
    FSK_IN = 299,
    FSK_QUOTE = 300,
    FSA_SIZE = 301,
    FSA_PREALLOC = 302,
    FSA_PARALLOC = 303,
    FSA_PATH = 304,
    FSA_REUSE = 305,
    FSA_MEMSIZE = 306,
    FSA_RATE = 307,
    FSA_READONLY = 308,
    FSA_TRUSTTREE = 309,
    FSA_IOSIZE = 310,
    FSA_FILENAME = 311,
    FSA_WSS = 312,
    FSA_NAME = 313,
    FSA_RANDOM = 314,
    FSA_INSTANCES = 315,
    FSA_DSYNC = 316,
    FSA_TARGET = 317,
    FSA_ITERS = 318,
    FSA_NICE = 319,
    FSA_VALUE = 320,
    FSA_BLOCKING = 321,
    FSA_HIGHWATER = 322,
    FSA_DIRECTIO = 323,
    FSA_DIRWIDTH = 324,
    FSA_FD = 325,
    FSA_SRCFD = 326,
    FSA_ROTATEFD = 327,
    FSA_ENTRIES = 328,
    FSA_DIRDEPTHRV = 329,
    FSA_DIRGAMMA = 330,
    FSA_USEISM = 331,
    FSA_TYPE = 332,
    FSA_LEAFDIRS = 333,
    FSA_INDEXED = 334,
    FSA_RANDTABLE = 335,
    FSA_RANDSRC = 336,
    FSA_ROUND = 337,
    FSA_RANDSEED = 338,
    FSA_RANDGAMMA = 339,
    FSA_RANDMEAN = 340,
    FSA_MIN = 341,
    FSA_MAX = 342,
    FSA_MASTER = 343,
    FSA_CLIENT = 344,
    FSS_TYPE = 345,
    FSS_SEED = 346,
    FSS_GAMMA = 347,
    FSS_MEAN = 348,
    FSS_MIN = 349,
    FSS_SRC = 350,
    FSS_ROUND = 351,
    FSA_LVAR_ASSIGN = 352,
    FSA_ALLDONE = 353,
    FSA_FIRSTDONE = 354,
    FSA_TIMEOUT = 355,
    FSA_LATHIST = 356,
    FSA_NOREADAHEAD = 357,
    FSA_IOPRIO = 358,
    FSA_WRITEONLY = 359,
    FSA_PARAMETERS = 360,
    FSA_NOUSESTATS = 361
  };
#endif
/* Tokens.  */
#define FSC_LIST 258
#define FSC_DEFINE 259
#define FSC_QUIT 260
#define FSC_DEBUG 261
#define FSC_CREATE 262
#define FSC_SLEEP 263
#define FSC_SET 264
#define FSC_SYSTEM 265
#define FSC_EVENTGEN 266
#define FSC_ECHO 267
#define FSC_RUN 268
#define FSC_PSRUN 269
#define FSC_VERSION 270
#define FSC_ENABLE 271
#define FSC_DOMULTISYNC 272
#define FSV_STRING 273
#define FSV_VAL_POSINT 274
#define FSV_VAL_NEGINT 275
#define FSV_VAL_BOOLEAN 276
#define FSV_VARIABLE 277
#define FSV_WHITESTRING 278
#define FSV_RANDUNI 279
#define FSV_RANDTAB 280
#define FSV_URAND 281
#define FSV_RAND48 282
#define FSE_FILE 283
#define FSE_FILES 284
#define FSE_FILESET 285
#define FSE_PROC 286
#define FSE_THREAD 287
#define FSE_FLOWOP 288
#define FSE_CVAR 289
#define FSE_RAND 290
#define FSE_MODE 291
#define FSE_MULTI 292
#define FSK_SEPLST 293
#define FSK_OPENLST 294
#define FSK_CLOSELST 295
#define FSK_OPENPAR 296
#define FSK_CLOSEPAR 297
#define FSK_ASSIGN 298
#define FSK_IN 299
#define FSK_QUOTE 300
#define FSA_SIZE 301
#define FSA_PREALLOC 302
#define FSA_PARALLOC 303
#define FSA_PATH 304
#define FSA_REUSE 305
#define FSA_MEMSIZE 306
#define FSA_RATE 307
#define FSA_READONLY 308
#define FSA_TRUSTTREE 309
#define FSA_IOSIZE 310
#define FSA_FILENAME 311
#define FSA_WSS 312
#define FSA_NAME 313
#define FSA_RANDOM 314
#define FSA_INSTANCES 315
#define FSA_DSYNC 316
#define FSA_TARGET 317
#define FSA_ITERS 318
#define FSA_NICE 319
#define FSA_VALUE 320
#define FSA_BLOCKING 321
#define FSA_HIGHWATER 322
#define FSA_DIRECTIO 323
#define FSA_DIRWIDTH 324
#define FSA_FD 325
#define FSA_SRCFD 326
#define FSA_ROTATEFD 327
#define FSA_ENTRIES 328
#define FSA_DIRDEPTHRV 329
#define FSA_DIRGAMMA 330
#define FSA_USEISM 331
#define FSA_TYPE 332
#define FSA_LEAFDIRS 333
#define FSA_INDEXED 334
#define FSA_RANDTABLE 335
#define FSA_RANDSRC 336
#define FSA_ROUND 337
#define FSA_RANDSEED 338
#define FSA_RANDGAMMA 339
#define FSA_RANDMEAN 340
#define FSA_MIN 341
#define FSA_MAX 342
#define FSA_MASTER 343
#define FSA_CLIENT 344
#define FSS_TYPE 345
#define FSS_SEED 346
#define FSS_GAMMA 347
#define FSS_MEAN 348
#define FSS_MIN 349
#define FSS_SRC 350
#define FSS_ROUND 351
#define FSA_LVAR_ASSIGN 352
#define FSA_ALLDONE 353
#define FSA_FIRSTDONE 354
#define FSA_TIMEOUT 355
#define FSA_LATHIST 356
#define FSA_NOREADAHEAD 357
#define FSA_IOPRIO 358
#define FSA_WRITEONLY 359
#define FSA_PARAMETERS 360
#define FSA_NOUSESTATS 361

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 114 "parser_gram.y" /* yacc.c:1909  */

	int64_t		 ival;
	unsigned char	 bval;
	char *		 sval;
	avd_t		 avd;
	cmd_t		*cmd;
	attr_t		*attr;
	list_t		*list;
	probtabent_t	*rndtb;

#line 277 "parser_gram.h" /* yacc.c:1909  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_PARSER_GRAM_H_INCLUDED  */
