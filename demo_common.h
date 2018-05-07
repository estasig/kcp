
#define CONFIG_MTU 470
//#define CONFIG_MTU 9000
#define PACKET_FOR_EACH_MB    (1024*1024/8/CONFIG_MTU)
#define CONFIG_BUFFER_SECONDS    2
#define CONFIG_STREAM_BITRATE_MB    2
#define CONFIG_MAX_BUFFER_PACKET    (PACKET_FOR_EACH_MB * CONFIG_BUFFER_SECONDS * CONFIG_STREAM_BITRATE_MB)


void writelog(const char *log, struct IKCPCB *kcp, void *user);
int current_time_ms();
