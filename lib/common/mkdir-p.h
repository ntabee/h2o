#pragma once

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "h2o.h"

#if __GNUC__ >= 3
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

/* cf. http://stackoverflow.com/a/2336245 */
static inline int mkdir_p(const char *dir) {
    size_t len = strlen(dir);
#ifdef __cplusplus
    char* tmp = static_cast<char*>(alloca(len+1));
#else
    char* tmp = (char*)alloca(len+1);
#endif
    char *p = NULL;
    int v;
    memcpy(tmp, dir, len+1);
    if(unlikely(tmp[len - 1] == '/')) {
        tmp[len - 1] = 0;
    }
    for(p = tmp + 1; *p; p++) {
        if(unlikely(*p == '/')) {
            *p = 0;
            v = mkdir(tmp, S_IRWXU);
            if (unlikely(v != 0 && (errno!=EEXIST))) {
                return v;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, S_IRWXU);
}
static inline int mkdir_p_parent(char* path) {
    size_t len = strlen(path);
    char* p = path+len-1;
    if (*p == '/') {
        return mkdir_p(path);
    }
    while (unlikely(*p != '/' && p != path)) {
        --p;
    }
    if (likely(p > path)) {
        int v;
        *p = '\0';
        v = mkdir_p(path);
        *p = '/';
        return v;
    }
    return -1;
}
