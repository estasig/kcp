#include <sys/time.h>
#include <stdio.h>
#include "ikcp.h"

void writelog(const char *log, struct IKCPCB *kcp, void *user){
    struct timeval t;
    gettimeofday(&t, NULL);
    unsigned long long tnow = t.tv_sec*1000 + t.tv_usec/1000;
    printf("%llu: %s\n", tnow, log);
}

int current_time_ms(){
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000 + t.tv_usec/1000;
}
