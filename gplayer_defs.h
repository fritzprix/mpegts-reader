#ifndef __GPLAYER__DEFS_H
#define __GPLAYER__DEFS_H

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __DBG
#define LOG_DBG(...)  do {    \
    printf("[ts_dump %d.%d.%d] : ", MAJOR, MINOR, PATCH);  \
    printf("(@%s) :\t", __FUNCTION__);                    \
    printf(__VA_ARGS__);                                  \
} while(0)
#else
#define LOG_DBG(...)
#endif

#ifdef MAJOR 
#define LOG_ERR(err, ...) do {\
    fprintf(stderr, "[ts_dump (ERROR) %d.%d.%d] : ", MAJOR, MINOR, PATCH);  \
    fprintf(stderr, "(@%s) :\t", __FUNCTION__);                    \
    fprintf(stderr, __VA_ARGS__);                                   \
    exit(err);                                                   \
} while(0)
#else
#define LOG_ERR(err, ...)     
#endif

#ifndef TRUE
#define TRUE   (0 == 0)
#endif




#ifdef __cplusplus
}
#endif

#endif