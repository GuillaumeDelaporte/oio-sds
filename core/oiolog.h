/*
OpenIO SDS core library
Copyright (C) 2014 Worldine, original work as part of Redcurrant
Copyright (C) 2015 OpenIO, modified as part of OpenIO Software Defined Storage

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3.0 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.
*/

#ifndef OIO_SDS__core_oiolog_h
# define OIO_SDS__core_oiolog_h 1

#ifdef __cplusplus
extern "C" {
#endif

# include <glib.h>

# define GRID_LOGLVL_TRACE2 (64 << G_LOG_LEVEL_USER_SHIFT)
# define GRID_LOGLVL_TRACE  (32 << G_LOG_LEVEL_USER_SHIFT)
# define GRID_LOGLVL_DEBUG  (16 << G_LOG_LEVEL_USER_SHIFT)
# define GRID_LOGLVL_INFO   (8  << G_LOG_LEVEL_USER_SHIFT)
# define GRID_LOGLVL_NOTICE (4  << G_LOG_LEVEL_USER_SHIFT)
# define GRID_LOGLVL_WARN   (2  << G_LOG_LEVEL_USER_SHIFT)
# define GRID_LOGLVL_ERROR  (1  << G_LOG_LEVEL_USER_SHIFT)

/* enablers */
# ifdef HAVE_EXTRA_DEBUG
#  define GRID_TRACE2_ENABLED() (1)
#  define      TRACE2_ENABLED() (1)
#  define GRID_TRACE_ENABLED()  (1)
#  define      TRACE_ENABLED()  (1)
# else
#  define GRID_TRACE2_ENABLED() (0)
#  define      TRACE2_ENABLED() (0)
#  define GRID_TRACE_ENABLED()  (0)
#  define      TRACE_ENABLED()  (0)
# endif

# define GRID_DEBUG_ENABLED()  (oio_log_level > GRID_LOGLVL_DEBUG)
# define GRID_INFO_ENABLED()   (oio_log_level > GRID_LOGLVL_INFO)
# define GRID_NOTICE_ENABLED() (oio_log_level > GRID_LOGLVL_NOTICE)
# define GRID_WARN_ENABLED()   (oio_log_level > GRID_LOGLVL_WARN)
# define GRID_ERROR_ENABLED()  (oio_log_level > 0)

# define DEBUG_ENABLED()       GRID_DEBUG_ENABLED()
# define INFO_ENABLED()        GRID_INFO_ENABLED()
# define NOTICE_ENABLED()      GRID_NOTICE_ENABLED()
# define WARN_ENABLED()        GRID_WARN_ENABLED()
# define ERROR_ENABLED()       GRID_ERROR_ENABLED()

/* new macros */
# ifdef HAVE_EXTRA_DEBUG
#  define GRID_TRACE2(FMT,...) g_log(G_LOG_DOMAIN, GRID_LOGLVL_TRACE2, FMT, ##__VA_ARGS__)
#  define GRID_TRACE(FMT,...)  g_log(G_LOG_DOMAIN, GRID_LOGLVL_TRACE, FMT, ##__VA_ARGS__)
# else
#  define GRID_TRACE2(FMT,...)
#  define GRID_TRACE(FMT,...)
# endif
# define GRID_LOG(LEVEL,FMT,...)   g_log(G_LOG_DOMAIN, LEVEL << G_LOG_LEVEL_USER_SHIFT, FMT, ##__VA_ARGS__)
# define GRID_DEBUG(FMT,...)   g_log(G_LOG_DOMAIN, GRID_LOGLVL_DEBUG, FMT, ##__VA_ARGS__)
# define GRID_INFO(FMT,...)    g_log(G_LOG_DOMAIN, GRID_LOGLVL_INFO, FMT, ##__VA_ARGS__)
# define GRID_NOTICE(FMT,...)  g_log(G_LOG_DOMAIN, GRID_LOGLVL_NOTICE, FMT, ##__VA_ARGS__)
# define GRID_WARN(FMT,...)    g_log(G_LOG_DOMAIN, GRID_LOGLVL_WARN, FMT, ##__VA_ARGS__)
# define GRID_ERROR(FMT,...)   g_log(G_LOG_DOMAIN, GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)

/* old macros */
# ifdef HAVE_EXTRA_DEBUG
#  define TRACE2(FMT,...) g_log(G_LOG_DOMAIN, GRID_LOGLVL_TRACE2, FMT, ##__VA_ARGS__)
#  define TRACE(FMT,...)  g_log(G_LOG_DOMAIN, GRID_LOGLVL_TRACE, FMT, ##__VA_ARGS__)
# else
#  define TRACE2(FMT,...)
#  define TRACE(FMT,...)
# endif
# define DEBUG(FMT,...)   g_log(G_LOG_DOMAIN, GRID_LOGLVL_DEBUG, FMT, ##__VA_ARGS__)
# define INFO(FMT,...)    g_log(G_LOG_DOMAIN, GRID_LOGLVL_INFO, FMT, ##__VA_ARGS__)
# define NOTICE(FMT,...)  g_log(G_LOG_DOMAIN, GRID_LOGLVL_NOTICE, FMT, ##__VA_ARGS__)
# define WARN(FMT,...)    g_log(G_LOG_DOMAIN, GRID_LOGLVL_WARN, FMT, ##__VA_ARGS__)
# define ERROR(FMT,...)   g_log(G_LOG_DOMAIN, GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)
# define FATAL(FMT,...)   g_log(G_LOG_DOMAIN, GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)
# define CRIT(FMT,...)    g_log(G_LOG_DOMAIN, GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)
# define ALERT(FMT,...)   g_log(G_LOG_DOMAIN, GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)

/* domain macros */
# ifdef HAVE_EXTRA_DEBUG
#  define TRACE2_DOMAIN(D,FMT,...) g_log((D), GRID_LOGLVL_TRACE2, FMT, ##__VA_ARGS__)
#  define TRACE_DOMAIN(D,FMT,...)  g_log((D), GRID_LOGLVL_TRACE, FMT, ##__VA_ARGS__)
# else
#  define TRACE2_DOMAIN(D,FMT,...)
#  define TRACE_DOMAIN(D,FMT,...)
# endif
# define DEBUG_DOMAIN(D,FMT,...)   g_log((D), GRID_LOGLVL_DEBUG, FMT, ##__VA_ARGS__)
# define INFO_DOMAIN(D,FMT,...)    g_log((D), GRID_LOGLVL_INFO, FMT, ##__VA_ARGS__)
# define NOTICE_DOMAIN(D,FMT,...)  g_log((D), GRID_LOGLVL_NOTICE, FMT, ##__VA_ARGS__)
# define WARN_DOMAIN(D,FMT,...)    g_log((D), GRID_LOGLVL_WARN, FMT, ##__VA_ARGS__)
# define ERROR_DOMAIN(D,FMT,...)   g_log((D), GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)
# define FATAL_DOMAIN(D,FMT,...)   g_log((D), GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)
# define CRIT_DOMAIN(D,FMT,...)    g_log((D), GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)
# define ALERT_DOMAIN(D,FMT,...)   g_log((D), GRID_LOGLVL_ERROR, FMT, ##__VA_ARGS__)

#define LOG_FLAG_TRIM_DOMAIN 0x01
#define LOG_FLAG_PURIFY 0x02
#define LOG_FLAG_COLUMNIZE 0x04
#define LOG_FLAG_PRETTYTIME 0x04

/** Cruising debug level.
 * Should not be altered by the application after the program has started. */
extern int oio_log_level_default;

/** Current (transitional) debug level.
 * May be altered by the application, signals, etc. */
extern int oio_log_level;

/** Should the logging system try to reduce the prefix of each line */
extern int oio_log_flags;

void oio_log_lazy_init (void);

void oio_log_verbose(void);

void oio_log_verbose_default(void);

void oio_log_quiet(void);

void oio_log_reset_level (void);

void oio_log_init_level(int l);

void oio_log_init_level_from_env(const gchar *k);

/** Writes the layed out message to stderr (not fd=2) with complete and
 * compact layout. */
void oio_log_stderr(const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data);

/** Does nothing */
void oio_log_noop(const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data);

/** Send the mesage though /dev/syslog, with simple layout */
void oio_log_syslog(const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data);

guint16 oio_log_thread_id(GThread *thread);

guint16 oio_log_current_thread_id(void);

enum oio_log_level_e {
	OIO_LOG_ERROR,
	OIO_LOG_WARNING,
	OIO_LOG_INFO,
	OIO_LOG_DEBUG,
};

typedef void (*oio_log_handler_f) (enum oio_log_level_e lvl, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

void oio_log_set_handler (oio_log_handler_f handler);

#ifdef __cplusplus
}
#endif
#endif /*OIO_SDS__core_oiolog_h*/
