/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02111-1301, USA */

#ifndef _my_sys_h
#define _my_sys_h
#ifdef	__cplusplus
extern "C" {
#endif

#ifdef HAVE_AIOWAIT
#include <sys/asynch.h>			/* Used by record-cache */
typedef struct my_aio_result {
  aio_result_t result;
  int	       pending;
} my_aio_result;
#endif

#ifndef _mariadb_ctype_h
#include <mariadb_ctype.h>                    /* for MARIADB_CHARSET_INFO */
#endif

#include <stdarg.h>  

#define MYSYS_PROGRAM_USES_CURSES() \
do {\
  ma_error_handler_hook = ma_message_curses;\
  mysys_uses_curses=1;\
} while(0)
#define MYSYS_PROGRAM_DONT_USE_CURSES() \
do {\
  ma_error_handler_hook = ma_message_no_curses; \
  mysys_uses_curses=0; \
} while(0)
#define MY_INIT(name) \
do {\
  ma_progname= name;\
  ma_init();\
} while(0)

#define MAXMAPS		(4)	/* Number of error message maps */
#define ERRMOD		(1000)	/* Max number of errors in a map */
#define ERRMSGSIZE	(SC_MAXWIDTH)	/* Max length of a error message */
#define NRERRBUFFS	(2)	/* Buffers for parameters */
#define MY_FILE_ERROR	((uint) ~0)

	/* General bitmaps for my_func's */
#define MY_FFNF		1	/* Fatal if file not found */
#define MY_FNABP	2	/* Fatal if not all bytes read/written */
#define MY_NABP		4	/* Error if not all bytes read/written */
#define MY_FAE		8	/* Fatal if any error */
#define MY_WME		16	/* Write message on error */
#define MY_WAIT_IF_FULL 32	/* Wait and try again if disk full error */
#define MY_RAID         64      /* Support for RAID (not the "Johnson&Johnson"-s one ;) */
#define MY_DONT_CHECK_FILESIZE 128	/* Option to init_io_cache() */
#define MY_LINK_WARNING 32	/* my_redel() gives warning if links */
#define MY_COPYTIME	64	/* my_redel() copies time */
#define MY_DELETE_OLD	256	/* my_create_with_symlink() */
#define MY_RESOLVE_LINK 128	/* my_realpath(); Only resolve links */
#define MY_HOLD_ORIGINAL_MODES 128  /* my_copy() holds to file modes */
#define MY_REDEL_MAKE_BACKUP 256
#define MY_SEEK_NOT_DONE 32	/* my_lock may have to do a seek */
#define MY_DONT_WAIT	64	/* my_lock() don't wait if can't lock */
#define MY_ZEROFILL	32	/* ma_malloc(), fill array with zero */
#define MY_ALLOW_ZERO_PTR 64	/* ma_realloc() ; zero ptr -> malloc */
#define MY_FREE_ON_ERROR 128	/* ma_realloc() ; Free old ptr on error */
#define MY_HOLD_ON_ERROR 256	/* ma_realloc() ; Return old ptr on error */
#define MY_THREADSAFE	128	/* pread/pwrite:  Don't allow interrupts */
#define MY_DONT_OVERWRITE_FILE 1024	/* my_copy; Don't overwrite file */

#define MY_CHECK_ERROR	1	/* Params to ma_end; Check open-close */
#define MY_GIVE_INFO	2	/* Give time info about process*/

#define ME_HIGHBYTE	8	/* Shift for colours */
#define ME_NOCUR	1	/* Don't use curses message */
#define ME_OLDWIN	2	/* Use old window */
#define ME_BELL		4	/* Ring bell then printing message */
#define ME_HOLDTANG	8	/* Don't delete last keys */
#define ME_WAITTOT	16	/* Wait for errtime secs of for a action */
#define ME_WAITTANG	32	/* Wait for a user action  */
#define ME_NOREFRESH	64	/* Don't refresh screen */
#define ME_NOINPUT	128	/* Don't use the input library */
#define ME_COLOUR1	((1 << ME_HIGHBYTE))	/* Possibly error-colours */
#define ME_COLOUR2	((2 << ME_HIGHBYTE))
#define ME_COLOUR3	((3 << ME_HIGHBYTE))

	/* My seek flags */
#define MY_SEEK_SET	0
#define MY_SEEK_CUR	1
#define MY_SEEK_END	2

        /* My charsets_list flags */
#define MY_NO_SETS       0
#define MY_COMPILED_SETS 1      /* show compiled-in sets */
#define MY_CONFIG_SETS   2      /* sets that have a *.conf file */
#define MY_INDEX_SETS    4      /* all sets listed in the Index file */
#define MY_LOADED_SETS    8      /* the sets that are currently loaded */

	/* Some constants */
#define MY_WAIT_FOR_USER_TO_FIX_PANIC	60	/* in seconds */
#define MY_WAIT_GIVE_USER_A_MESSAGE	10	/* Every 10 times of prev */
#define MIN_COMPRESS_LENGTH		150	/* Don't compress small bl. */
#define KEYCACHE_BLOCK_SIZE		1024

	/* root_alloc flags */
#define MY_KEEP_PREALLOC	1

	/* defines when allocating data */

#define my_checkmalloc() (0)
#undef TERMINATE
#define TERMINATE(A) {}
#define QUICK_SAFEMALLOC
#define NORMAL_SAFEMALLOC
#define ma_malloc_ci(SZ,FLAG) ma_malloc( SZ, FLAG )
#define CALLER_INFO_PROTO   /* nothing */
#define CALLER_INFO         /* nothing */
#define ORIG_CALLER_INFO    /* nothing */

#ifdef HAVE_ALLOCA
#if defined(_AIX) && !defined(__GNUC__)
#pragma alloca
#endif /* _AIX */
#if defined(__GNUC__) && !defined(HAVE_ALLOCA_H)
#ifndef alloca
#define alloca __builtin_alloca
#endif
#endif /* GNUC */
#define my_alloca(SZ) alloca((size_t) (SZ))
#define my_afree(PTR) {}
#else
#define my_alloca(SZ) ma_malloc(SZ,MYF(0))
#define my_afree(PTR) ma_free(PTR)
#endif /* HAVE_ALLOCA */

#ifndef errno
#ifdef HAVE_ERRNO_AS_DEFINE
#include <errno.h>			/* errno is a define */
#else
extern int errno;			/* declare errno */
#endif
#endif
extern const char ** NEAR my_errmsg[];
extern char NEAR errbuff[NRERRBUFFS][ERRMSGSIZE];
/* tbr
extern int (*ma_error_handler_hook)(uint my_err, const char *str,myf MyFlags);
extern int (*fatal_ma_error_handler_hook)(uint my_err, const char *str,
				       myf MyFlags);
*/

/* charsets */
/* tbr
extern uint get_charset_number(const char *cs_name);
extern const char *get_charset_name(uint cs_number);
extern my_bool set_default_charset(uint cs, myf flags);
extern my_bool set_default_charset_by_name(const char *cs_name, myf flags);
extern void free_charsets(void);
extern char *list_charsets(myf want_flags); 
extern char *get_charsets_dir(char *buf);
*/
extern MARIADB_CHARSET_INFO *get_charset(uint cs_number, myf flags);
extern MARIADB_CHARSET_INFO *get_charset_by_name(const char *cs_name);
extern MARIADB_CHARSET_INFO *get_charset_by_nr(uint cs_number);

/* string functions */
char *ma_strmake(register char *dst, register const char *src, size_t length);


#endif
typedef struct st_typelib {	/* Different types saved here */
  uint count;			/* How many types */
  const char *name;			/* Name of typelib */
  const char **type_names;
} TYPELIB;

enum cache_type {READ_CACHE,WRITE_CACHE,READ_FIFO,READ_NET,WRITE_NET};
enum flush_type { FLUSH_KEEP, FLUSH_RELEASE, FLUSH_IGNORE_CHANGED,
		  FLUSH_FORCE_WRITE};

typedef struct st_record_cache	/* Used when caching records */
{
  File file;
  int	rc_seek,error,inited;
  uint	rc_length,read_length,reclength;
  my_off_t rc_record_pos,end_of_file;
  unsigned char	*rc_buff,*rc_buff2,*rc_pos,*rc_end,*rc_request_pos;
#ifdef HAVE_AIOWAIT
  int	use_async_io;
  my_aio_result aio_result;
#endif
  enum cache_type type;
} RECORD_CACHE;


typedef struct st_dynamic_array {
  char *buffer;
  uint elements,max_element;
  uint alloc_increment;
  uint size_of_element;
} DYNAMIC_ARRAY;

typedef struct st_dynamic_string {
  char *str;
  size_t length,max_length,alloc_increment;
} DYNAMIC_STRING;


typedef struct st_io_cache		/* Used when caching files */
{
  my_off_t pos_in_file,end_of_file;
  unsigned char	*rc_pos,*rc_end,*buffer,*rc_request_pos;
  int (*read_function)(struct st_io_cache *,unsigned char *,uint);
  char *file_name;			/* if used with 'open_cached_file' */
  char *dir,*prefix;
  File file;
  int	seek_not_done,error;
  uint	buffer_length,read_length;
  myf	myflags;			/* Flags used to my_read/my_write */
  enum cache_type type;
#ifdef HAVE_AIOWAIT
  uint inited;
  my_off_t aio_read_pos;
  my_aio_result aio_result;
#endif
} IO_CACHE;

typedef int (*qsort2_cmp)(const void *, const void *, const void *);

	/* defines for mf_iocache */

	/* Test if buffer is inited */
#define my_b_clear(info) do{(info)->buffer= 0;} while (0)
#define my_b_inited(info) ((info)->buffer)
#define my_b_EOF INT_MIN

#define my_b_read(info,Buffer,Count) \
  ((info)->rc_pos + (Count) <= (info)->rc_end ?\
   (memcpy((Buffer),(info)->rc_pos,(size_t) (Count)), \
    ((info)->rc_pos+=(Count)),0) :\
   (*(info)->read_function)((info),(Buffer),(Count)))

#define my_b_get(info) \
  ((info)->rc_pos != (info)->rc_end ?\
   ((info)->rc_pos++, (int) (uchar) (info)->rc_pos[-1]) :\
   _my_b_get(info))

#define my_b_write(info,Buffer,Count) \
  ((info)->rc_pos + (Count) <= (info)->rc_end ?\
   (memcpy((info)->rc_pos,(Buffer),(size_t) (Count)), \
    ((info)->rc_pos+=(Count)),0) :\
   _my_b_write((info),(Buffer),(Count)))

	/* my_b_write_byte doesn't have any err-check */
#define my_b_write_byte(info,chr) \
  (((info)->rc_pos < (info)->rc_end) ?\
   ((*(info)->rc_pos++)=(chr)) :\
   (_my_b_write((info),0,0) , ((*(info)->rc_pos++)=(chr))))

#define my_b_fill_cache(info) \
  (((info)->rc_end=(info)->rc_pos),(*(info)->read_function)((info),0,0))

#define my_b_tell(info) ((info)->pos_in_file + \
			 ((info)->rc_pos - (info)->rc_request_pos))

#define my_b_bytes_in_cache(info) ((uint) ((info)->rc_end - (info)->rc_pos))

typedef struct st_changeable_var {
  const char *name;			/* Name of variable */
  long *varptr;				/* Pointer to variable */
  long def_value,			/* Default value */
       min_value,			/* Min allowed value */
       max_value,			/* Max allowed value */
       sub_size,			/* Subtract this from given value */
       block_size;			/* Value should be a mult. of this */
} CHANGEABLE_VAR;
