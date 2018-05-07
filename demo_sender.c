#include "ikcp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include "demo_common.h"

ikcpcb *g_kcp = NULL;
IUINT32 g_conv = 0x12352234;
int g_fd = -1;

char *g_dest_ip = NULL;
int g_dest_port = -1;
char *g_src_ip = "127.0.0.1";
int g_src_port = 9001;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;


typedef struct msg{
    struct msg *next;
    struct msg *prev;
    char *p;
    int size;
}msg_t;

msg_t g_msg_queue;
pthread_mutex_t g_msg_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_msg_cond = PTHREAD_COND_INITIALIZER;

void transmit_statistics(int len){
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

void *send_thread(void *arg){
    int ret;
    struct timeval tnow;
    int c;
    int s;
    s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s > 0);

    struct sockaddr_in listen_addr;
    socklen_t addr_len = sizeof(listen_addr);
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = inet_addr(g_src_ip);
    listen_addr.sin_port = htons(g_src_port);
    ret = bind(s, (const struct sockaddr *)&listen_addr, addr_len);
    assert(ret == 0);

    ret = listen(s, 5);
    assert(ret == 0);
    while(1){
        struct sockaddr_in remote_addr;
        addr_len = sizeof(remote_addr);
        c = accept(s, (struct sockaddr *)&remote_addr, &addr_len);
        assert(c > 0);
    
        while(1){
            char buf[CONFIG_MTU-70];
            ret = read(c, buf, sizeof(buf));
            assert(ret >= 0);
            if(ret == 0){
                break;
            }else if(ret < 0){
                assert(0);
            }

            int send_len = ret;
            pthread_mutex_lock(&g_mutex);
            if(ikcp_waitsnd(g_kcp) >= CONFIG_MAX_BUFFER_PACKET){
                pthread_mutex_unlock(&g_mutex);
                continue;
            }
            ret = ikcp_send(g_kcp, buf, ret);
            assert(ret >= 0);
            transmit_statistics(send_len);
            gettimeofday(&tnow, NULL);
            ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
            //ikcp_flush(g_kcp);
#if 0
            if(ikcp_waitsnd(g_kcp) >= CONFIG_MAX_BUFFER_PACKET){
                printf("kcp buffer full...");fflush(stdout);
                int wait_begin = time(NULL);
                while(1){
                    pthread_mutex_unlock(&g_mutex);
                    usleep(10*1000);
                    pthread_mutex_lock(&g_mutex);

                    gettimeofday(&tnow, NULL);
                    ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
                    if(ikcp_waitsnd(g_kcp) < (CONFIG_MAX_BUFFER_PACKET/2)){
                        break;
                    }
                }
                int wait_end = time(NULL);
                printf("%ds\n", wait_end-wait_begin);
            }
#endif
            pthread_mutex_unlock(&g_mutex);
        }
        close(c);
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
#if 0
        int i;
        for(i=0; i<ret/24; i++){
            ikcp_input(g_kcp, buf+i*24, 24);
        }
#endif
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
        gettimeofday(&tnow, NULL);
        ikcp_update(g_kcp, tnow.tv_sec * 1000 + tnow.tv_usec / 1000);
        pthread_mutex_unlock(&g_mutex);
    }
    return NULL;
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

static void dump_queue(){
    msg_t *p;
    for(p=g_msg_queue.next; p!=&g_msg_queue; p=p->next){
        int sn;
        memcpy(&sn, p->p+12, 4);
        printf("sn %d\n", sn);
    }
}

int output_cb(const char *buf, int len, ikcpcb *kcp, void *user){

    pthread_mutex_lock(&g_msg_mutex);
    msg_t *msg = malloc(sizeof(msg_t));
    msg->p = (char *)malloc(len);
    memcpy(msg->p, buf, len);

    int sn;
    memcpy(&sn, msg->p+12, 4);
    //printf("sn -> %d\n", sn);
            
    msg->size = len;
    msg->prev = g_msg_queue.prev;
    msg->next = &g_msg_queue;
    g_msg_queue.prev->next = msg;
    g_msg_queue.prev = msg;

#if 0
    if((sn % 10) == 0){
        dump_queue();
    }
#endif
    pthread_mutex_unlock(&g_msg_mutex);
    pthread_cond_signal(&g_msg_cond);
    //getchar();
    return 0;
}
#if 1
void *output_thread(void *arg){

    struct sockaddr_in dest_addr;
    socklen_t dest_len = sizeof(dest_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(g_dest_ip);
    dest_addr.sin_port = htons(g_dest_port);
    
    while(1){
        //sleep(100);
        int send_len = 0;
        pthread_mutex_lock(&g_msg_mutex);
        if(g_msg_queue.next == &g_msg_queue){
            pthread_cond_wait(&g_msg_cond, &g_msg_mutex);
            pthread_mutex_unlock(&g_msg_mutex);
            continue;
        }

        while(g_msg_queue.next != &g_msg_queue){
            msg_t *msg = g_msg_queue.next;
            int sn;
            memcpy(&sn, msg->p+12, 4);
           // printf("sn -> %d\n", sn);
            char *buf = msg->p;
            int len = msg->size;
            int ret = sendto(g_fd, buf, len, 0, (struct sockaddr *)&dest_addr, dest_len);
            //printf("udp ret %d\n", ret);
            if (ret < 0){
                perror("udp send error");
            }
            assert(ret == len);
            send_len += msg->size;

            msg->next->prev = msg->prev;
            msg->prev->next = msg->next;
            free(msg->p);
            free(msg);


            if(send_len > 4*1024){
                break;
            }
        }
//printf("udp send %p:%d\n", buf, len);
    /*
    char bufff[65536];
    int i;
    for(i=1; i<65536; i += 10000){
        int ret = sendto(g_fd, bufff, i, 0, (struct sockaddr *)&dest_addr, dest_len);
        if(ret < 0){
            printf("send len %d, %s\n", i, strerror(errno));
        }

        assert(ret > 0);
    }
    */

        //g_msg_queue.next = msg;
        //msg->prev = &g_msg_queue;
        pthread_mutex_unlock(&g_msg_mutex);
        usleep(1*1000);
    }

    return 0;
}
#endif
int main(int argc, char **argv){
    int val,ret;
    if(argc < 3){
        printf("usage: %s remote_ip remote_port\n", argv[0]);
        return -1;
    }

    g_dest_ip = argv[1];
    g_dest_port = atoi(argv[2]);
    g_msg_queue.next = g_msg_queue.prev = &g_msg_queue;

    // create kcp object
    g_kcp = ikcp_create(g_conv, NULL);
#if 1
    g_kcp->logmask = IKCP_LOG_MISC;
    //g_kcp->logmask = IKCP_LOG_INPUT;
    //g_kcp->logmask = IKCP_LOG_ALL;
    g_kcp->writelog = writelog;
#endif
    // create udp socket
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);

    val = 4*1024*1024;
    ret = setsockopt(g_fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
    if(ret < 0){
        perror("set sndbuf\n");
    }
    assert(ret == 0);

    val = 4*1024*1024;
    ret = setsockopt(g_fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
    if(ret < 0){
        perror("set rcvbuf\n");
    }
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

    pthread_t tid1, tid2, tid3, tid4;
    pthread_create(&tid1, NULL, send_thread, NULL);
    pthread_create(&tid2, NULL, recv_thread, NULL);
    pthread_create(&tid3, NULL, update_thread, NULL);
    pthread_create(&tid4, NULL, output_thread, NULL);
    while(1){
        sleep(1);
    }
    return 0;
}
