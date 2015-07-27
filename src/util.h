#ifndef DJ_UTIL_H
#define DJ_UTIL_H

#include <et/com_err.h>

#define CHECK_WARN(FUNC, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
}

#define CHECK_WARN_DO(FUNC, DO, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
    { \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
        DO; \
    } \
}

#define CHECK_FATAL(FUNC, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
    { \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
        exit(1); \
    } \
}

extern char *prog_name;

void exit_str(char *message, ...);
void *emalloc(size_t size);
void *ecalloc(size_t size);

#endif