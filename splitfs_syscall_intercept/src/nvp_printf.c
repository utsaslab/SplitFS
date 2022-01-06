/*---------------------------------------------------*/
/* Modified from :                                   */
/* Public Domain version of printf                   */
/* Rud Merriam, Compsult, Inc. Houston, Tx.          */
/* For Embedded Systems Programming, 1991            */
/*                                                   */
/*---------------------------------------------------*/

#ifndef __NVP_XIL_PRINTF_C_
#define __NVP_XIL_PRINTF_C_

#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <debug.h>

FILE * _xil_printf_file;

FILE* _nvp_print_fd;


void printString(char *s);


static pthread_mutex_t __debug_mutex = PTHREAD_MUTEX_INITIALIZER;

int max_len = 0;
int pos = 0;
char * outbuf = NULL;

static void crush_outbuf(void) {
	memset(outbuf, '\0', max_len);
	pos=0;
}

static void flush_outbuf(void)
{
     static size_t (*glibc_fwrite) ( const void * ptr, size_t size, size_t count, FILE * stream ) = NULL;

	if(glibc_fwrite==NULL) {
		//void* libc_so = dlopen("/lib64/libc.so.6", RTLD_LAZY|RTLD_LOCAL);
		void *libc_so = dlopen("/lib/x86_64-linux-gnu/libc.so.6", RTLD_LAZY|RTLD_LOCAL);
        if(!libc_so) { assert(0); }
		void* glcw = dlsym(libc_so, "fwrite");
		if(!glcw) { assert(0); }
		glibc_fwrite = (size_t (*) ( const void * ptr, size_t size, size_t count, FILE * stream ))glcw;
		assert(glibc_fwrite!=NULL);
	}
	int errno_holder = errno;
	int ret;
	int ret_count = 0;
	do {
		ret = glibc_fwrite( outbuf, pos, 1, _xil_printf_file);
		ret_count++;
	} while((ret!=1) && (ret_count < 20));
	
	if(ret!=1) {
	     //xil_printf(stderr, "\n\nERROR: nvp_printf.c: glibc_fwrite returned %i, expected 1: %s\n\n", ret, strerror(errno)); fflush(stderr);
	     printString("ERROR: nvp_printf.c: glibc_fwrite returned something other than 1!\n");
	     while(ret != 1) {}
	     assert(0);
	}
	errno = errno_holder;
	fflush(_xil_printf_file);
	pos=0;
}

void outbyte (char c)
{
	if(pos >= max_len-1) {
		max_len *= 2;
		outbuf = (char*) realloc(outbuf, max_len);
		memset(outbuf+pos, '\0', max_len-pos);
	}

	outbuf[pos] = c;
	pos++;
}

void printString(char *s){
     int i = 0;
     while(s[i]) {
	  outbyte(s[i]);
	  i++;
     }
}
/*----------------------------------------------------*/
/* Use the following parameter passing structure to   */
/* make xil_printf re-entrant.                        */
/*----------------------------------------------------*/
typedef struct params_s {
    int len;
    int num1;
    int num2;
    char pad_character;
    int do_padding;
    int left_flag;
} params_t;

/*---------------------------------------------------*/
/* The purpose of this routine is to output data the */
/* same as the standard printf function without the  */
/* overhead most run-time libraries involve. Usually */
/* the printf brings in many kilobytes of code and   */
/* that is unacceptable in most embedded systems.    */
/*---------------------------------------------------*/

typedef int (*func_ptr)(int c);

/*---------------------------------------------------*/
/*                                                   */
/* This routine puts pad characters into the output  */
/* buffer.                                           */
/*                                                   */
static void padding( const int l_flag, params_t *par)
{
    int i;

    if (par->do_padding && l_flag && (par->len < par->num1))
        for (i=par->len; i<par->num1; i++)
            outbyte( par->pad_character);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine moves a string to the output buffer  */
/* as directed by the padding and positioning flags. */
/*                                                   */
static void outs(charptr lp, params_t *par)
{
    /* pad on left if needed                         */
    if(lp == NULL) { lp = "(null)"; }
    par->len = strlen( lp);
    padding( !(par->left_flag), par);

    /* Move string to the buffer                     */
    while (*lp && (par->num2)--)
        outbyte( *lp++);

    /* Pad on right if needed                        */
    /* CR 439175 - elided next stmt. Seemed bogus.   */
    /* par->len = strlen( lp);                       */
    padding( par->left_flag, par);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine moves a number to the output buffer  */
/* as directed by the padding and positioning flags. */
/*                                                   */

static void outnum( const long long n, const long long base, params_t *par)
{
    charptr cp;
    int negative;
    char outbuf[32];
    const char digits[] = "0123456789ABCDEF";
    unsigned long long num;

    /* Check if number is negative                   */
    if (base == 10 && n < 0L) {
        negative = 1;
        num = -(n);
    }
    else{
        num = (n);
        negative = 0;
    }
   
    /* Build number (backwards) in outbuf            */
    cp = outbuf;
    do {
        *cp++ = digits[(int)(num % base)];
    } while ((num /= base) > 0);
    if (negative)
        *cp++ = '-';
    *cp-- = 0;

    /* Move the converted number to the buffer and   */
    /* add in the padding where needed.              */
    par->len = strlen(outbuf);
    padding( !(par->left_flag), par);
    while (cp >= outbuf)
        outbyte( *cp--);
    padding( par->left_flag, par);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine gets a number from the format        */
/* string.                                           */
/*                                                   */
static int getnum( charptr* linep)
{
    int n;
    charptr cp;

    n = 0;
    cp = *linep;
    while (isdigit(*cp))
        n = n*10 + ((*cp++) - '0');
    *linep = cp;
    return(n);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine operates just like a printf/sprintf  */
/* routine. It outputs a set of data under the       */
/* control of a formatting string. Not all of the    */
/* standard C format control are supported. The ones */
/* provided are primarily those needed for embedded  */
/* systems work. Primarily the floaing point         */
/* routines are omitted. Other formats could be      */
/* added easily by following the examples shown for  */
/* the supported formats.                            */
/*                                                   */

/* void esp_printf( const func_ptr f_ptr,
   const charptr ctrl1, ...) */
void xil_printf( FILE * file, const charptr ctrl1, ...)
{
	pthread_mutex_lock(&__debug_mutex);

	_xil_printf_file = file;


    int long_flag;
    int dot_flag;

    params_t par;

    char ch;
    va_list argp;
    charptr ctrl = ctrl1;

    va_start( argp, ctrl1);


	if(outbuf == NULL) {
		max_len = 512;
		outbuf = (char*) calloc(max_len, sizeof(char));
	}

	crush_outbuf();

    for ( ; *ctrl; ctrl++) {

        /* move format string chars to buffer until a  */
        /* format control is found.                    */
        if (*ctrl != '%') {
            outbyte(*ctrl);
            continue;
        }

        /* initialize all the flags for this format.   */
        dot_flag   = long_flag = par.left_flag = par.do_padding = 0;
        par.pad_character = ' ';
        par.num2=32767;

 try_next:
        ch = *(++ctrl);

        if (isdigit(ch)) {
            if (dot_flag)
                par.num2 = getnum(&ctrl);
            else {
                if (ch == '0')
                    par.pad_character = '0';

                par.num1 = getnum(&ctrl);
                par.do_padding = 1;
            }
            ctrl--;
            goto try_next;
        }

        switch (tolower(ch)) {
            case '%':
                outbyte( '%');
                continue;

            case '-':
                par.left_flag = 1;
                break;

            case '.':
                dot_flag = 1;
                break;

            case 'l':
                long_flag = 1;
                break;

            case 'i':
	    case 'd':
	    case 'u':
                if (long_flag || ch == 'D') {
                    outnum( va_arg(argp, long), 10L, &par);
                    continue;
                }
                else {
                    outnum( va_arg(argp, int), 10L, &par);
                    continue;
                }
	    case 'p':
	    	outbyte('0');
		outbyte('x');
            case 'x':
                outnum((long long)va_arg(argp, long long), 16L, &par);
                continue;

            case 's':
                outs( va_arg( argp, charptr), &par);
                continue;

            case 'c':
                outbyte( va_arg( argp, int));
                continue;

            case '\\':
                switch (*ctrl) {
                    case 'a':
                        outbyte( 0x07);
                        break;
                    case 'h':
                        outbyte( 0x08);
                        break;
                    case 'r':
                        outbyte( 0x0D);
                        break;
                    case 'n':
                        outbyte( 0x0D);
                        outbyte( 0x0A);
                        break;
                    default:
                        outbyte( *ctrl);
                        break;
                }
                ctrl++;
                break;

	    case '\0':
	    continue;

            default:
		 printString("Illegal format character: '");
		 char t[2];
		 t[1] = 0;
		 t[0] = ch;
		 printString(t);
		 printString("'.  Good bye.\n");
		 assert(0); // this code sucks
		 continue;

        }
        goto try_next;
    }
    va_end( argp);

	flush_outbuf();

	pthread_mutex_unlock(&__debug_mutex);

}

/*---------------------------------------------------*/


#endif

#define MK_STR(x) #x

#ifndef _NVP_PRINT_ERROR_NAME_FDEF_
#define _NVP_PRINT_ERROR_NAME_FDEF_
void _nvp_print_error_name(int errnoin) 
{
BOOST_PP_LIST_FOR_EACH(ERROR_IF_PRINT, errnoin, ERROR_NAMES_LIST)
}
#endif

