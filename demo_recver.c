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
//#include <netinet/tcp_var.h>
#include <pthread.h>
#include "demo_common.h"


ikcpcb *g_kcp = NULL;
IUINT32 g_conv = 0x12352234;
int g_fd = -1;
int g_total_recv = 0;

char *g_dest_ip = "127.0.0.1";
int g_dest_port = 9002;
/*
char *g_src_ip = "127.0.0.1";
int g_src_port = 9001;
*/

char *g_listen_ip = NULL;
int g_listen_port = -1;
struct sockaddr_in g_dest_addr;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

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

void *update_thread(void *arg){
    struct timeval tnow;
    while(1){
        pthread_mutex_lock(&g_mutex);
        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
        pthread_mutex_unlock(&g_mutex);
        usleep(10*1000);
    }
    return NULL;
}

int output_cb(const char *buf, int len, ikcpcb *kcp, void *user){
    int ret = sendto(g_fd, buf, len, 0, (struct sockaddr *)&g_dest_addr, sizeof(g_dest_addr));
    assert(ret == len);
    return 0;
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
        printf("tcp cwnd/ssthresh: %d/%d\n", tci.tcpi_snd_cwnd, tci.tcpi_snd_ssthresh);



    ret = write(sc, buf, len);
    assert(ret = len);
    return 0;
}

int main(int argc, char **argv){
    int val,ret;
    if(argc < 3){
        printf("usage: %s listen_ip listen_port\n", argv[0]);
        return -1;
    }

    g_listen_ip = argv[1];
    g_listen_port = atoi(argv[2]);

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = inet_addr(g_listen_ip);
    listen_addr.sin_port = htons(g_listen_port);
    // create kcp object
    g_kcp = ikcp_create(g_conv, NULL);

#if 0
    g_kcp->logmask = IKCP_LOG_ALL;
    g_kcp->writelog = writelog;
#endif

    // create udp socket
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    bind(g_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr));

    val = 4*1024*1024;
    ret = setsockopt(g_fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
    assert(ret == 0);

    val = 4*1024*1024;
    ret = setsockopt(g_fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
    assert(ret == 0);


    // set output
    g_kcp->stream = 1;
    ikcp_setoutput(g_kcp, output_cb);
    ikcp_wndsize(g_kcp, 1024, 1024);
    //ikcp_nodelay(g_kcp, 1, 10, 2, 1);
    //ikcp_nodelay(g_kcp, 0, 40, 0, 0);
    //ikcp_nodelay(g_kcp, 1, 10, 0, 0);
    //ikcp_nodelay(g_kcp, 1, 10, 200, 0);
    //ikcp_nodelay(g_kcp, 1, 10, 5, 0);
    //ikcp_nodelay(g_kcp, 1, 10, 3, 0);
    ikcp_nodelay(g_kcp, 0, 10, 3, 0);
    //ikcp_nodelay(g_kcp, 0, 3, 3, 0);
    ikcp_setmtu(g_kcp, CONFIG_MTU);

    pthread_t tid;
    pthread_create(&tid, NULL, update_thread, NULL);

    static int s_recv_end = -1;
    while(1){
        struct timeval tnow;
        char buf[10*1024];
        struct sockaddr_in addr;
        socklen_t sockaddr_len = sizeof(addr);
        static int s_tlast = -1;
        int recv_begin = current_time_ms();
        if(s_tlast < 0){
            s_tlast = recv_begin;
        }
        if(recv_begin > s_tlast){
            //printf("recvfrom interval: %d - %d = %d\n", recv_begin, s_tlast, recv_begin - s_tlast);
        }
        if(s_recv_end > 0 && recv_begin != s_recv_end){
            //printf("kcp took: %d\n", recv_begin - s_recv_end);
        }
        s_tlast = recv_begin;
        int ret = recvfrom(g_fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &sockaddr_len);
        if(ret <= 0){
            assert(0);
        }
        int recv_end = current_time_ms();
        if(recv_end > recv_begin){
            //printf("recvfrom took: %d - %d = %d\n", recv_end, recv_begin, recv_end - recv_begin);
        }
        s_recv_end = recv_end;
        memcpy(&g_dest_addr, &addr, sockaddr_len);

        pthread_mutex_lock(&g_mutex);
        ikcp_input(g_kcp, buf, ret);
        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);


        while(1){
            char out[10*1024];
            ret = ikcp_recv(g_kcp, out, sizeof(out));
            if(ret < 0){
                break;
            }else if(ret == 0){
                continue;
            }
            transmit_statistics(ret);
            g_total_recv += ret;
            //send_to_player(out, ret);
        }
        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
        pthread_mutex_unlock(&g_mutex);
    }
    return 0;
}

