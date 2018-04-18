#include "ikcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>


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

void *update_thread(void *arg){
    struct timeval tnow;
    while(1){
        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
        usleep(20*1000);
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

    ret = write(sc, buf, len);
    assert(ret = len);
    return 0;
}

int main(int argc, char **argv){
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
    // create udp socket
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    bind(g_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr));
    // set output
    ikcp_setoutput(g_kcp, output_cb);
    ikcp_wndsize(g_kcp, 1024, 1024);
    ikcp_nodelay(g_kcp, 1, 10, 2, 1);

    pthread_t tid;
    pthread_create(&tid, NULL, update_thread, NULL);

    while(1){
        struct timeval tnow;
        char buf[10*1024];
        struct sockaddr_in addr;
        socklen_t sockaddr_len = sizeof(addr);
        int ret = recvfrom(g_fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &sockaddr_len);
        if(ret < 0){
            assert(0);
        }
        memcpy(&g_dest_addr, &addr, sockaddr_len);

        ikcp_input(g_kcp, buf, ret);
        gettimeofday(&tnow, NULL);
        //ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);


        while(1){
            char out[1*1024];
            ret = ikcp_recv(g_kcp, out, sizeof(out));
            if(ret < 0){
                break;
            }else if(ret == 0){
                continue;
            }
            g_total_recv += ret;
            send_to_player(out, ret);
            printf("total recv size: %d\n", g_total_recv);
        }
    }
    return 0;
}

