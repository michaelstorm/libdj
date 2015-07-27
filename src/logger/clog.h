#ifndef CLOG_H
#define CLOG_H

#include <stdarg.h>
#include <sys/types.h>
#include <time.h>
#include "visibility.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOG_LOG_LEVEL_TRACE 0
#define CLOG_LOG_LEVEL_DEBUG 1
#define CLOG_LOG_LEVEL_INFO  2
#define CLOG_LOG_LEVEL_WARN  3
#define CLOG_LOG_LEVEL_ERROR 4
#define CLOG_LOG_LEVEL_FATAL 5

struct logger_ctx_t;
typedef struct logger_ctx_t logger_ctx_t;

typedef     int (*start_msg_func) (logger_ctx_t *l, const char *file, int line, const char *func, int level);
typedef     int (*printf_func)    (void *data, const char *fmt, va_list args);
typedef ssize_t (*write_func)     (void *data, const char *buf, size_t size);
typedef     int (*end_msg_func)   (logger_ctx_t *l);

struct logger_ctx_t
{
	char *name;
	int min_level;
	
	void *data;
	
	start_msg_func start_msg;
	printf_func printf;
	write_func write;
	end_msg_func end_msg;
	
	FILE *fp;
	int log_partial;
};

struct timespec;

void clog_init() DLL_PUBLIC;
void clog_free() DLL_PUBLIC;
void clog_set_event_context(const char *context) DLL_PUBLIC;
void clog_clear_event_context();
const char *clog_get_event_context();
 int clog_get_default_log_level();
void clog_set_default_log_level(int level);
 int clog_time_offset(struct timespec *diff_time);
 
		 int  logger_ctx_init(logger_ctx_t *l, const char *name, int min_level, void *data, start_msg_func start_msg, printf_func printf, write_func write, end_msg_func end_msg);
logger_ctx_t *logger_ctx_new(const char *name, int min_level, void *data, start_msg_func start_msg, printf_func printf, write_func write, end_msg_func end_msg);
        void  logger_ctx_close(logger_ctx_t *l);
        void  logger_ctx_free(logger_ctx_t *l);

 		 int  clog_add_logger(logger_ctx_t *l_new);
logger_ctx_t *clog_get_logger(const char *name);
logger_ctx_t *clog_get_logger_for_file(const char *file) DLL_PUBLIC;

void clog_log(logger_ctx_t *l, const char *file, int line, const char *func, int level, const char *fmt, ...) DLL_PUBLIC;
void clog_log_as(const char *name, const char *file, int line, const char *func, int level, const char *fmt, ...) DLL_PUBLIC;

void clog_start_log(logger_ctx_t *l, const char *file, int line, const char *func, int level);
void clog_partial_log(logger_ctx_t *l, const char *file, const char *fmt, ...);
void clog_end_log(logger_ctx_t *l, const char *file);

void clog_start_log_as(const char *name, const char *file, int line, const char *func, int level);
void clog_partial_log_as(const char *name, const char *fmt, ...);
void clog_end_log_as(const char *name);

#define clog_file_logger() clog_get_logger_for_file(__FILE__)

#define Log(level, fmt, ...) clog_log(NULL, __FILE__, __LINE__, __func__, level, fmt, ##__VA_ARGS__)
#define StartLog(level)      { clog_start_log(NULL, __FILE__, __LINE__, __func__, level)
#define PartialLog(fmt, ...)   clog_partial_log(NULL, __FILE__, fmt, ##__VA_ARGS__)
#define EndLog()             } clog_end_log(NULL, __FILE__)

#define LogTo(l_ctx, level, fmt, ...) clog_log(l_ctx, __FILE__, __LINE__, __func__, level, fmt, ##__VA_ARGS__)
#define StartLogTo(l_ctx, level)      { clog_start_log(l_ctx, __FILE__, __LINE__, __func__, level)
#define PartialLogTo(l_ctx, fmt, ...)   clog_partial_log(l_ctx, __FILE__, fmt, ##__VA_ARGS__)
#define EndLogTo(l_ctx)               } clog_end_log(l_ctx, __FILE__)

#define LogAs(name, level, fmt, ...) clog_log_as(name, __FILE__, __LINE__, __func__, level, fmt, ##__VA_ARGS__)
#define StartLogAs(name, level)      { clog_start_log_as(name, __FILE__, __LINE__, __func__, level)
#define PartialLogAs(name, fmt, ...)   clog_partial_log_as(name, fmt, ##__VA_ARGS__)
#define EndLogAs(name)               } clog_end_log_as(name)

// set default CLOG_MIN_LOG_LEVEL if not specified or blank
#define DO_EXPAND(VAL)  VAL ## 1
#define EXPAND(VAL)     DO_EXPAND(VAL)
#if !defined(CLOG_MIN_LOG_LEVEL) || (EXPAND(CLOG_MIN_LOG_LEVEL) == 1)
#define CLOG_MIN_LOG_LEVEL CLOG_LOG_LEVEL_TRACE
#endif

#if CLOG_MIN_LOG_LEVEL <= CLOG_LOG_LEVEL_TRACE
#define LogTrace(fmt, ...)   Log(CLOG_LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define LogTraceTo(l_ctx, fmt, ...)   LogTo(l_ctx, CLOG_LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define LogTraceAs(name, fmt, ...)   LogAs(name, CLOG_LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#else
#define LogTrace(fmt, ...)
#define LogTraceTo(l_ctx, fmt, ...)
#define LogTraceAs(name, fmt, ...)
#endif

#if CLOG_MIN_LOG_LEVEL <= CLOG_LOG_LEVEL_DEBUG
#define LogDebug(fmt, ...)   Log(CLOG_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LogDebugTo(l_ctx, fmt, ...)   LogTo(l_ctx, CLOG_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LogDebugAs(name, fmt, ...)   LogAs(name, CLOG_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#else
#define LogDebug(fmt, ...)
#define LogDebugTo(l_ctx, fmt, ...)
#define LogDebugAs(name, fmt, ...)
#endif

#if CLOG_MIN_LOG_LEVEL <= CLOG_LOG_LEVEL_INFO
#define LogInfo(fmt, ...)    Log(CLOG_LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define LogInfoTo(l_ctx,  fmt, ...)   LogTo(l_ctx, CLOG_LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define LogInfoAs(name,  fmt, ...)   LogAs(name, CLOG_LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#else
#define LogInfo(fmt, ...)
#define LogInfoTo(l_ctx,  fmt, ...)
#define LogInfoAs(name,  fmt, ...)
#endif

#if CLOG_MIN_LOG_LEVEL <= CLOG_LOG_LEVEL_WARN
#define LogWarn(fmt, ...)    Log(CLOG_LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define LogWarnTo(l_ctx,  fmt, ...)   LogTo(l_ctx, CLOG_LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define LogWarnAs(name,  fmt, ...)   LogAs(name, CLOG_LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#else
#define LogWarn(fmt, ...)
#define LogWarnTo(l_ctx,  fmt, ...)
#define LogWarnAs(name,  fmt, ...)
#endif

#if CLOG_MIN_LOG_LEVEL <= CLOG_LOG_LEVEL_ERROR
#define LogError(fmt, ...)   Log(CLOG_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LogErrorTo(l_ctx, fmt, ...)   LogTo(l_ctx, CLOG_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LogErrorAs(name, fmt, ...)   LogAs(name, CLOG_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#else
#define LogError(fmt, ...)
#define LogErrorTo(l_ctx, fmt, ...)
#define LogErrorAs(name, fmt, ...)
#endif

#if CLOG_MIN_LOG_LEVEL <= CLOG_LOG_LEVEL_FATAL
#define LogFatal(fmt, ...)   Log(CLOG_LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)
#define LogFatalTo(l_ctx, fmt, ...)   LogTo(l_ctx, CLOG_LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)
#define LogFatalAs(name, fmt, ...)   LogAs(name, CLOG_LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)
#else
#define LogFatal(fmt, ...)
#define LogFatalTo(l_ctx, fmt, ...)
#define LogFatalAs(name, fmt, ...)
#endif

#define Die(ret_code, fmt, ...)          { LogFatal(fmt, ##__VA_ARGS__);          LogFatal("Exiting with code " #ret_code);          exit(ret_code); }
#define DieTo(l_ctx, ret_code, fmt, ...) { LogFatalTo(l_ctx, fmt, ##__VA_ARGS__); LogFatalTo(l_ctx, "Exiting with code " #ret_code); exit(ret_code); }
#define DieAs(name, ret_code, fmt, ...)  { LogFatalAs(name, fmt,  ##__VA_ARGS__); LogFatalAs(name, "Exiting with code " #ret_code);  exit(ret_code); }

#ifdef __cplusplus
}
#endif

#endif
