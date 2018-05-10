#include "ikcp.h"
#include "demo_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include "demo_common.h"


char *g_dest_ip = "127.0.0.1";
int g_dest_port = 9002;

static void print_cwnd(int cwnd){
    static int cwnd_begin_time = -1;
    static int cwnd_last_time = -1;
    static int cwnd_last_value = -1;
    if(cwnd_begin_time == -1){
        cwnd_begin_time = current_time_ms(NULL);
    }
    if(cwnd_last_value == -1){
        cwnd_last_value = cwnd;
    }

    int flag = 0;
    int tnow = current_time_ms(NULL);
    if(cwnd_last_time != tnow){
        flag = 1;
    }else if(cwnd_last_value > cwnd){
        flag = 1;
        //printf("VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV\n");
    }
    flag = 1;
    if(flag){
        printf("cwnd change: %d %d\n", tnow-cwnd_begin_time, cwnd);
        cwnd_last_value = cwnd;
        cwnd_last_time = tnow;
    }
}

static void transmit_statistics(int len){
    static unsigned long long s_total_len = 0;
    static unsigned long long s_tlast = 0;
    static unsigned long long s_segment_len = 0;
    static unsigned long long s_begin_time = 0;

    s_total_len += len;
    s_segment_len += len;

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long long tnow = now.tv_sec * 1000 + now.tv_usec / 1000;

    if(s_begin_time == 0){
        s_begin_time = tnow;
    }

    if(tnow - s_tlast > 1000){
        int total_mill_seconds = tnow-s_begin_time;
        int this_mill_seconds = tnow-s_tlast;
        if(total_mill_seconds > 0){
            printf("%d: total [%lluKB, %llukbps], now [%lluKB, %llukbps]\n",
                    total_mill_seconds/1000,
                    s_total_len/1024,
                    s_total_len*8/total_mill_seconds,
                    s_segment_len/1024,
                    s_segment_len*8/this_mill_seconds);
        }
        s_segment_len = 0;
        s_tlast = tnow;
    }
}

int send_to_player(char *buf, int len){
    static int sc = -1;
    int ret;
    if(sc < 0){
        int c;
        c = socket(AF_INET, SOCK_STREAM, 0);
        assert(c > 0);

        struct sockaddr_in dest_addr;
        socklen_t dest_len = sizeof(dest_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_addr.s_addr = inet_addr(g_dest_ip);
        dest_addr.sin_port = htons(g_dest_port);

        ret = connect(c, (const struct sockaddr *)&dest_addr, dest_len);
        assert(ret == 0);

        int i;
        ret = setsockopt( c, IPPROTO_TCP, TCP_NODELAY, (void *)&i, sizeof(i) );
        assert(ret == 0);
        sc = c;
    }

        struct tcp_connection_info tci;
        unsigned int tcilen = sizeof(tci);
        ret = getsockopt(sc, IPPROTO_TCP, TCP_CONNECTION_INFO, &tci, &tcilen);
        if(ret < 0){
            perror("getsockopt TCP_CONNECTION_INFO");
        }
        assert(ret == 0);
        //printf("tcp cwnd/ssthresh: %d/%d\n", tci.tcpi_snd_cwnd, tci.tcpi_snd_ssthresh);
        print_cwnd(tci.tcpi_snd_cwnd);



    ret = write(sc, buf, len);
    assert(ret = len);
    return 0;
}

int main(int argc, char **argv){
    while(1){
        char out[10*1024];
        int ret = 1000;
        send_to_player(out, ret);
        transmit_statistics(ret);
    }
    return 0;
}

