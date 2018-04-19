#include "ikcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>


ikcpcb *g_kcp = NULL;
IUINT32 g_conv = 0x12352234;
int g_fd = -1;

char *g_dest_ip = NULL;
int g_dest_port = -1;
char *g_src_ip = "127.0.0.1";
int g_src_port = 9001;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void *send_thread(void *arg){
    int ret;
    struct timeval tnow;
    int c;
    c = socket(AF_INET, SOCK_STREAM, 0);
    assert(c > 0);

    struct sockaddr_in dest_addr;
    socklen_t dest_len = sizeof(dest_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(g_src_ip);
    dest_addr.sin_port = htons(g_src_port);
 
    ret = connect(c, (const struct sockaddr *)&dest_addr, dest_len);
    assert(ret == 0);
    
    while(1){
        char buf[1024];
        ret = read(c, buf, sizeof(buf));
        assert(ret >= 0);
        if(ret == 0){
            break;
        }

        pthread_mutex_lock(&g_mutex);
        ret = ikcp_send(g_kcp, buf, ret);
        assert(ret >= 0);
        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
        pthread_mutex_unlock(&g_mutex);
    }
    return NULL;
}

void *recv_thread(void *arg){
    int ret;
    struct timeval tnow;
    while(1){
        char buf[10*1024];
        struct sockaddr_in addr;
        socklen_t sockaddr_len = sizeof(addr);
        ret = recvfrom(g_fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &sockaddr_len);
        if(ret < 0){
            assert(0);
        }

        pthread_mutex_lock(&g_mutex);
        ikcp_input(g_kcp, buf, ret);

        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);

        while(1){
            char out[10*1024];
            ret = ikcp_recv(g_kcp, out, sizeof(out));
            if(ret < 0){
                break;
            }
            printf("recv size: %d\n", ret);
        }
        pthread_mutex_unlock(&g_mutex);
    }
    return NULL;
}

void *update_thread(void *arg){
    struct timeval tnow;
    while(1){
        gettimeofday(&tnow, NULL);
        pthread_mutex_lock(&g_mutex);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
        pthread_mutex_unlock(&g_mutex);
        usleep(20*1000);
    }
    return NULL;
}

int output_cb(const char *buf, int len, ikcpcb *kcp, void *user){
    struct sockaddr_in dest_addr;
    socklen_t dest_len = sizeof(dest_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(g_dest_ip);
    dest_addr.sin_port = htons(g_dest_port);
    int ret = sendto(g_fd, buf, len, 0, (struct sockaddr *)&dest_addr, dest_len);
    assert(ret == len);
    return 0;
}

int main(int argc, char **argv){
    if(argc < 3){
        printf("usage: %s remote_ip remote_port\n", argv[0]);
        return -1;
    }

    g_dest_ip = argv[1];
    g_dest_port = atoi(argv[2]);

    // create kcp object
    g_kcp = ikcp_create(g_conv, NULL);
    // create udp socket
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    // set output
    ikcp_setoutput(g_kcp, output_cb);
    ikcp_wndsize(g_kcp, 1024, 1024);
    ikcp_nodelay(g_kcp, 1, 10, 2, 1);

    pthread_t tid1, tid2, tid3;
    pthread_create(&tid1, NULL, send_thread, NULL);
    pthread_create(&tid2, NULL, recv_thread, NULL);
    pthread_create(&tid3, NULL, update_thread, NULL);
    while(1){
        sleep(1);
    }
    return 0;
}
