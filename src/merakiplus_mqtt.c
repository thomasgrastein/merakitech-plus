/* merakiplus_mqtt.c — Meraki+ MQTT helper (unreleased feature): PUBLISH-ONLY MQTT 3.1.1 client for Home Assistant and other consumers.
 *
 *   merakiplus_mqtt <mqtt.conf> <status-file> [<log-file>]   stdin = the UI's read-only feed (one record per line)
 *   merakiplus_mqtt --discovery <mqtt.conf> [<info-json>]     print the Home Assistant discovery document and exit
 *   merakiplus_mqtt --selftest                                (host build with -DMQTT_SELFTEST: address check, packets)
 *
 * docs/integrations.md has the design (§4.4) and what is implemented (§15). In short:
 *
 *  - READ-ONLY + OUTBOUND-ONLY. The helper is a client. It never binds, listens or accepts. It builds exactly four MQTT
 *    packet types: CONNECT, PUBLISH, PINGREQ, DISCONNECT (mq_out() refuses every other type). It has no code that builds a
 *    subscription, so nothing on the network can reach the UI or the coffee machine's controller through it. From the broker
 *    it accepts only CONNACK, PUBACK and PINGRESP; ANY other packet (including a PUBLISH) ends the connection.
 *  - ONE-WAY FEED. The UI starts this process with one end of a socketpair as stdin and shuts down its own receiving
 *    direction, so the kernel refuses anything this process writes back. The helper only reads it. When it reaches EOF (the
 *    UI ended, or MQTT was switched off) it publishes "offline", disconnects cleanly and exits.
 *  - LOCAL BROKER ONLY. The broker address must be private (10/8, 172.16/12, 192.168/16), link-local (169.254/16, fe80::/10),
 *    unique-local (fc00::/7) or loopback. Anything else is refused before a connection is attempted; there is no override.
 *  - NO SECRETS IN THE DATA. Payloads are the UI's feed lines (named fields only) and the discovery document built here
 *    from fixed text + the random id. The password is sent only inside CONNECT and is never logged or written anywhere.
 *  - BOUNDED. Static buffers only (about 150 KB), no heap of its own, no spooling to flash or /tmp; a 2 x 8 KB log in /tmp.
 *
 * Topics (base = merakiplus, id = the random id in mqtt.conf):
 *   <base>/<id>/availability   online / offline (retained; "offline" is also the last will)
 *   <base>/<id>/info           merakiplus.info/1   retained, QoS 1, sent on every connect
 *   <base>/<id>/state          merakiplus.state/1  retained, QoS 0
 *   <base>/<id>/live           merakiplus.live/1   QoS 0, only while connected (dropped otherwise)
 *   <base>/<id>/event          merakiplus.event/1  QoS 1, queued (32) while disconnected, resent until PUBACK
 *   <base>/<id>/shot           merakiplus.shot/1   retained, QoS 1 — the complete record incl. curve (self-contained)
 *   <base>/<id>/shot/summary   the same without curve (Home Assistant attributes), retained, QoS 1
 *   <discovery_prefix>/device/merakiplus_<id>/config   Home Assistant device discovery (retained, QoS 1)
 *
 * Status file (read by the UI for SYSTEM › MQTT and DIAGNOSTICS, written atomically, RAM-backed /tmp):
 *   "mqtt1 <state> <published> <dropped> <reconnects> <last publish, CLOCK_MONOTONIC s> <error>\n"
 *
 * Build (device): zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -s -o merakiplus_mqtt src/merakiplus_mqtt.c
 * Build (host):   zig cc -O2 -o merakiplus_mqtt_host src/merakiplus_mqtt.c            (tests/test_mqtt.py: mock broker)
 * Build (test):   zig cc -DMQTT_SELFTEST -O2 -o merakiplus_mqtt_selftest src/merakiplus_mqtt.c && ./merakiplus_mqtt_selftest
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MQ_VERSION "1"
#define LINE_MAX_B  16384      /* one feed line (a full shot record is at most ~8 KB) */
#define EV_MAX_B    1024       /* one event record */
#define EV_RING     32         /* queued events while disconnected / awaiting PUBACK */
#define KEEPALIVE_S 30
#define CONN_TMO_MS 5000
#define PING_TMO_MS 10000
#define BACKOFF0_MS 2000
#define BACKOFF_MAX_MS 60000
#define STATUS_MS   5000
#define LOG_CAP     8192

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ---------------- configuration (mqtt.conf, owned by Meraki+; never config.json) ---------------- */
static struct {
    int enabled, port, discovery, live;
    char host[64], user[64], pass[96], id[25], base[33], prefix[33];
} cf;
static char conf_err[48]="";

static char* trim(char*s){ while(*s==' '||*s=='\t') s++; char*e=s+strlen(s); while(e>s&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r')) *--e=0; return s; }
/* allowed characters: an MQTT topic level must not contain + # / (base may contain /), no control characters */
static int ok_chars(const char*s,const char*extra,int minl,int maxl){
    int n=(int)strlen(s); if(n<minl||n>maxl) return 0;
    for(const char*p=s;*p;p++){ unsigned char c=(unsigned char)*p;
        if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||(c>='A'&&c<='Z')) continue;
        if(extra&&strchr(extra,c)) continue;
        return 0; }
    return 1;
}
static int conf_load(const char*path){
    memset(&cf,0,sizeof cf); cf.port=1883; cf.discovery=1; cf.live=1;
    snprintf(cf.base,sizeof cf.base,"merakiplus"); snprintf(cf.prefix,sizeof cf.prefix,"homeassistant");
    char b[2048]; int fd=open(path,O_RDONLY); if(fd<0){ snprintf(conf_err,sizeof conf_err,"no_config"); return 0; }
    int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<=0){ snprintf(conf_err,sizeof conf_err,"no_config"); return 0; } b[n]=0;
    for(char*l=strtok(b,"\n");l;l=strtok(NULL,"\n")){
        char*h=strchr(l,'#'); if(h) *h=0;
        char*e=strchr(l,'='); if(!e) continue; *e++=0; char*k=trim(l), *v=trim(e);
        if(!strcmp(k,"enabled")) cf.enabled=atoi(v)==1;
        else if(!strcmp(k,"host")) snprintf(cf.host,sizeof cf.host,"%s",v);
        else if(!strcmp(k,"port")) cf.port=atoi(v);
        else if(!strcmp(k,"user")) snprintf(cf.user,sizeof cf.user,"%s",v);
        else if(!strcmp(k,"pass")) snprintf(cf.pass,sizeof cf.pass,"%s",v);
        else if(!strcmp(k,"id")) snprintf(cf.id,sizeof cf.id,"%s",v);
        else if(!strcmp(k,"base")) snprintf(cf.base,sizeof cf.base,"%s",v);
        else if(!strcmp(k,"discovery")) cf.discovery=atoi(v)==1;
        else if(!strcmp(k,"discovery_prefix")) snprintf(cf.prefix,sizeof cf.prefix,"%s",v);
        else if(!strcmp(k,"live")) cf.live=atoi(v)==1;
    }
    memset(b,0,sizeof b);                                               /* the password does not stay in a stack buffer */
    if(!cf.enabled){ snprintf(conf_err,sizeof conf_err,"disabled"); return 0; }
    if(!cf.host[0]||!ok_chars(cf.host,".:",1,63)){ snprintf(conf_err,sizeof conf_err,"bad_host"); return 0; }
    if(cf.port<1||cf.port>65535){ snprintf(conf_err,sizeof conf_err,"bad_port"); return 0; }
    if(!ok_chars(cf.id,NULL,1,12)){ snprintf(conf_err,sizeof conf_err,"bad_id"); return 0; }
    if(!ok_chars(cf.base,"/",1,32)||cf.base[0]=='/'||cf.base[strlen(cf.base)-1]=='/'){ snprintf(conf_err,sizeof conf_err,"bad_base"); return 0; }
    if(!ok_chars(cf.prefix,NULL,1,32)){ snprintf(conf_err,sizeof conf_err,"bad_prefix"); return 0; }
    if(cf.user[0]&&!ok_chars(cf.user,".@",1,63)){ snprintf(conf_err,sizeof conf_err,"bad_user"); return 0; }
    for(const char*p=cf.pass;*p;p++) if((unsigned char)*p<32){ snprintf(conf_err,sizeof conf_err,"bad_pass"); return 0; }
    return 1;
}

/* ---------------- the local-broker rule (R3): private, link-local, unique-local or loopback only ---------------- */
static int v4_local(uint32_t a){                                         /* host byte order */
    return (a>>24)==10 || (a>>20)==((172u<<4)|1) || (a>>16)==((192u<<8)|168) || (a>>16)==((169u<<8)|254) || (a>>24)==127;
}
static int addr_local(const struct sockaddr*sa){
    if(sa->sa_family==AF_INET) return v4_local(ntohl(((const struct sockaddr_in*)sa)->sin_addr.s_addr));
    if(sa->sa_family==AF_INET6){
        const uint8_t*b=((const struct sockaddr_in6*)sa)->sin6_addr.s6_addr;
        static const uint8_t lo[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        if(!memcmp(b,lo,16)) return 1;                                   /* ::1 */
        if((b[0]&0xFE)==0xFC) return 1;                                  /* fc00::/7 unique-local */
        if(b[0]==0xFE&&(b[1]&0xC0)==0x80) return 1;                      /* fe80::/10 link-local */
        static const uint8_t mp[12]={0,0,0,0,0,0,0,0,0,0,0xFF,0xFF};     /* ::ffff:a.b.c.d -> the IPv4 rule */
        if(!memcmp(b,mp,12)) return v4_local(((uint32_t)b[12]<<24)|((uint32_t)b[13]<<16)|((uint32_t)b[14]<<8)|b[15]);
        return 0;
    }
    return 0;
}

/* ---------------- clock, log, status ---------------- */
static int64_t mono_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static char log_path[256]="", status_path[256]="";
static void lg(const char*fmt,...){
    if(!log_path[0]) return;
    struct stat st; if(stat(log_path,&st)==0&&st.st_size>LOG_CAP){ char o[270]; snprintf(o,sizeof o,"%s.1",log_path); rename(log_path,o); }
    FILE*f=fopen(log_path,"a"); if(!f) return;
    fprintf(f,"%lld ",(long long)(mono_ms()/1000)); va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap); fputc('\n',f); fclose(f);
}
enum { ST_STARTING=0, ST_CONNECTING, ST_CONNECTED, ST_WAITING, ST_NOTLOCAL, ST_CONFIG, ST_AUTH, ST_STOPPED };
static const char*const ST_KEY[]={"starting","connecting","connected","waiting","not_local","config","auth","stopped"};
static struct { int state; long n_pub, n_drop, n_reconn, n_pub_w; int64_t last_pub_ms; char err[48]; int64_t t_written; } st;
static void status_write(int64_t now){
    if(!status_path[0]) return;
    char tmp[270], b[200]; snprintf(tmp,sizeof tmp,"%s.tmp",status_path);
    int n=snprintf(b,sizeof b,"mqtt1 %s %ld %ld %ld %.1f %s\n",ST_KEY[st.state],st.n_pub,st.n_drop,st.n_reconn,
                   st.last_pub_ms>0?(double)st.last_pub_ms/1000.0:-1.0,st.err[0]?st.err:"-");
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0) return;
    int w=(int)write(fd,b,(size_t)n); close(fd);
    if(w==n) rename(tmp,status_path); else unlink(tmp);
    st.t_written=now; st.n_pub_w=st.n_pub;
}
static void set_state(int s,const char*err,int64_t now){
    if(st.state!=s||(err&&strcmp(err,st.err))){ st.state=s; snprintf(st.err,sizeof st.err,"%s",err?err:""); status_write(now); }
}

/* ---------------- MQTT 3.1.1 encoding (outgoing: CONNECT, PUBLISH, PINGREQ, DISCONNECT — nothing else) ---------------- */
#define P_CONNECT    0x10
#define P_CONNACK    0x20
#define P_PUBLISH    0x30
#define P_PUBACK     0x40
#define P_PINGREQ    0xC0
#define P_PINGRESP   0xD0
#define P_DISCONNECT 0xE0
static uint8_t txb[LINE_MAX_B+512];
static int put_len(uint8_t*o,int n){ int k=0; do{ uint8_t d=(uint8_t)(n%128); n/=128; if(n) d|=0x80; o[k++]=d; }while(n&&k<4); return k; }
static int put_str(uint8_t*o,const char*s,int n){ o[0]=(uint8_t)(n>>8); o[1]=(uint8_t)n; memcpy(o+2,s,(size_t)n); return n+2; }
/* the ONLY place a packet header is made: the type must be one of the four this client may send */
static int mq_out(uint8_t*o,uint8_t first,int rem){
    uint8_t t=first&0xF0;
    if(t!=P_CONNECT&&t!=P_PUBLISH&&t!=P_PINGREQ&&t!=P_DISCONNECT) return -1;
    o[0]=first; return 1+put_len(o+1,rem);
}
static int build_connect(uint8_t*o,const char*cid,const char*will_t,const char*will_m,const char*user,const char*pass,int ka){
    uint8_t vh[10]={0,4,'M','Q','T','T',4,0,(uint8_t)(ka>>8),(uint8_t)ka};
    vh[7]=0x02|0x04|0x08|0x20;                                           /* clean session, will, will QoS 1, will retain */
    if(user&&user[0]){ vh[7]|=0x80; if(pass&&pass[0]) vh[7]|=0x40; }
    int lc=(int)strlen(cid), lt=(int)strlen(will_t), lm=(int)strlen(will_m), lu=user?(int)strlen(user):0, lp=pass?(int)strlen(pass):0;
    int rem=10+2+lc+2+lt+2+lm+((vh[7]&0x80)?2+lu:0)+((vh[7]&0x40)?2+lp:0);
    int k=mq_out(o,P_CONNECT,rem); if(k<0) return -1;
    memcpy(o+k,vh,10); k+=10; k+=put_str(o+k,cid,lc); k+=put_str(o+k,will_t,lt); k+=put_str(o+k,will_m,lm);
    if(vh[7]&0x80) k+=put_str(o+k,user,lu);
    if(vh[7]&0x40) k+=put_str(o+k,pass,lp);
    return k;
}
static int build_publish(uint8_t*o,int cap,const char*topic,const char*pl,int pn,int qos,int retain,int dup,uint16_t pid){
    int lt=(int)strlen(topic), rem=2+lt+(qos?2:0)+pn;
    if(rem+5>cap) return -1;
    int k=mq_out(o,(uint8_t)(P_PUBLISH|(dup?8:0)|(qos<<1)|(retain?1:0)),rem); if(k<0) return -1;
    k+=put_str(o+k,topic,lt);
    if(qos){ o[k++]=(uint8_t)(pid>>8); o[k++]=(uint8_t)pid; }
    memcpy(o+k,pl,(size_t)pn); return k+pn;
}

/* ---------------- the Home Assistant discovery document (§7): fixed text + the random id; no command topics ---------------- */
static char info_ui[24]="", info_fw[24]="";
/* copies a string value of "key" out of a JSON line into o, keeping only [A-Za-z0-9 ._-] (bounded, no parsing beyond that) */
static void json_pick(const char*line,const char*key,char*o,size_t on){
    char pat[48]; snprintf(pat,sizeof pat,"\"%s\":\"",key); const char*p=strstr(line,pat); size_t k=0; o[0]=0;
    if(!p) return; p+=strlen(pat);
    while(*p&&*p!='"'&&k+1<on){ char c=*p++; if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||c==' ') o[k++]=c; }
    o[k]=0;
}
typedef struct { const char*key,*platform,*name,*topic,*tpl,*dev_class,*unit,*state_class,*extra; int diag, off; } Ent;
enum { T_STATE=0, T_INFO, T_LIVE, T_EVENT, T_SUMMARY };
static const char*const TSUB[]={"state","info","live","event","shot/summary"};
#define ACT_OPTS "[\"idle\",\"grind\",\"brew\",\"water\",\"steam\",\"brew_steam\",\"sleep\",\"offline\"]"
#define EV_TYPES "[\"grind_started\",\"grind_finished\",\"brew_started\",\"shot_finished\",\"water_started\",\"water_finished\"," \
                 "\"steam_started\",\"steam_finished\",\"brew_ready\",\"steam_ready\",\"error\",\"error_cleared\"]"
static const Ent ENTS[]={
 {"brew_boiler","sensor","Brew boiler","S","{{ value_json.brew_boiler_c }}","temperature","°C","measurement",NULL,0,0},
 {"group","sensor","Group head","S","{{ value_json.group_c }}","temperature","°C","measurement",NULL,0,0},
 {"steam_boiler","sensor","Steam boiler","S","{{ value_json.steam_boiler_c }}","temperature","°C","measurement",NULL,0,0},
 {"milk","sensor","Milk temperature","S","{{ value_json.milk_c }}","temperature","°C","measurement",NULL,0,0},
 {"brew_weight","sensor","Cup weight","S","{{ value_json.brew_weight_g }}","weight","g","measurement",NULL,0,0},
 {"grind_weight","sensor","Grinder weight","S","{{ value_json.grind_weight_g }}","weight","g","measurement",NULL,0,0},
 {"activity","sensor","Activity","S","{{ value_json.activity }}","enum",NULL,NULL,"\"options\":" ACT_OPTS,0,0},
 {"brew_ready","binary_sensor","Brew ready","S","{{ 'ON' if value_json.brew_ready else 'OFF' }}",NULL,NULL,NULL,NULL,0,0},
 {"steam_ready","binary_sensor","Steam ready","S","{{ 'ON' if value_json.steam_ready else 'OFF' }}",NULL,NULL,NULL,NULL,0,0},
 {"water_low","binary_sensor","Water low","S","{{ 'ON' if value_json.water_low else 'OFF' }}","problem",NULL,NULL,NULL,0,0},
 {"fault","binary_sensor","Fault","S","{{ 'ON' if value_json.faults else 'OFF' }}","problem",NULL,NULL,NULL,0,0},
 {"faults","sensor","Faults","S","{{ value_json.faults | join(', ') if value_json.faults else 'none' }}",NULL,NULL,NULL,"\"icon\":\"mdi:alert-circle-outline\"",0,0},
 {"error_codes","sensor","Error codes","S","E1 {{ value_json.e1 }} · E2 {{ value_json.e2 }} · E3 {{ value_json.e3 }}",NULL,NULL,NULL,NULL,1,0},
 {"mains_v","sensor","Mains voltage","S","{{ value_json.mains_v }}","voltage","V","measurement",NULL,1,0},
 {"mains_hz","sensor","Mains frequency","S","{{ value_json.mains_hz }}","frequency","Hz","measurement",NULL,1,0},
 {"controller_fw","sensor","Controller firmware","I","{{ value_json.controller_fw }}",NULL,NULL,NULL,"\"icon\":\"mdi:chip\"",1,0},
 {"ui_version","sensor","Meraki+ version","I","{{ value_json.ui_version }}",NULL,NULL,NULL,"\"icon\":\"mdi:tag-outline\"",1,0},
 {"last_shot","sensor","Last shot","Y","{{ value_json.end_utc if value_json.clock_ok else None }}","timestamp",NULL,NULL,"\"json_attributes_topic\":\"@Y\"",0,0},
 {"shot_yield","sensor","Last shot yield","Y","{{ value_json.settled_yield_g }}","weight","g","measurement",NULL,0,0},
 {"shot_time","sensor","Last shot time","Y","{{ value_json.duration_estimate_s }}","duration","s","measurement",NULL,0,0},
 {"shot_dose","sensor","Last shot dose","Y","{{ value_json.dose_g }}","weight","g","measurement",NULL,0,0},
 {"shot_ratio","sensor","Last shot ratio","Y","{{ value_json.ratio }}",NULL,NULL,"measurement","\"icon\":\"mdi:scale-balance\"",0,0},
 {"brews","sensor","Brews","Y","{{ value_json.seq }}",NULL,NULL,"total_increasing","\"icon\":\"mdi:coffee\"",0,0},
 {"machine_event","event","Machine event","E",NULL,NULL,NULL,NULL,"\"event_types\":" EV_TYPES,0,0},
 {"live_weight","sensor","Live cup weight","L","{{ value_json.brew_weight_g }}","weight","g","measurement",NULL,0,1},
 {"live_flow","sensor","Live cup flow","L","{{ value_json.cup_flow_gs }}",NULL,"g/s","measurement",NULL,0,1},
 {"pressure_est","sensor","Pressure (estimate)","L","{{ value_json.pressure_est_bar }}",NULL,"bar","measurement",NULL,1,1},
 {"pump_raw","sensor","Pump (raw)","L","{{ value_json.pump_raw }}",NULL,NULL,"measurement",NULL,1,1},
 {"flow_raw","sensor","Flow (raw)","L","{{ value_json.flow_raw }}",NULL,NULL,"measurement",NULL,1,1},
};
static void topic_of(char*o,size_t n,const char*sub){ snprintf(o,n,"%s/%s/%s",cf.base,cf.id,sub); }
static const char* tsub_of(char c){ return c=='S'?TSUB[T_STATE]:c=='I'?TSUB[T_INFO]:c=='L'?TSUB[T_LIVE]:c=='E'?TSUB[T_EVENT]:TSUB[T_SUMMARY]; }
/* JSON-escape a template (only " and \ occur) */
static int jesc(char*o,size_t n,const char*s){ size_t k=0; for(;*s&&k+2<n;s++){ if(*s=='"'||*s=='\\') o[k++]='\\'; o[k++]=*s; } o[k]=0; return (int)k; }
static int discovery_build(char*o,size_t n){
    char av[120], t[120], tp[300]; size_t k=0;
    topic_of(av,sizeof av,"availability");
#define AP(...) do{ int w_=snprintf(o+k,n-k,__VA_ARGS__); if(w_<0||(size_t)w_>=n-k) return -1; k+=(size_t)w_; }while(0)
    AP("{\"device\":{\"identifiers\":[\"merakiplus_%s\"],\"name\":\"Meraki\",\"manufacturer\":\"MerakiTech\",\"model\":\"Meraki+ display UI\"",cf.id);
    if(info_ui[0]) AP(",\"sw_version\":\"%s\"",info_ui);
    if(info_fw[0]) AP(",\"hw_version\":\"controller %s\"",info_fw);
    AP("},\"origin\":{\"name\":\"merakiplus_mqtt\",\"sw_version\":\"" MQ_VERSION "\",\"support_url\":\"https://github.com/thomasgrastein/merakitech-plus\"}");
    AP(",\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"components\":{",av);
    for(size_t i=0;i<sizeof ENTS/sizeof ENTS[0];i++){ const Ent*e=&ENTS[i];
        topic_of(t,sizeof t,tsub_of(e->topic[0]));
        AP("%s\"%s\":{\"platform\":\"%s\",\"name\":\"%s\",\"unique_id\":\"merakiplus_%s_%s\",\"state_topic\":\"%s\"",i?",":"",e->key,e->platform,e->name,cf.id,e->key,t);
        if(e->tpl){ jesc(tp,sizeof tp,e->tpl); AP(",\"value_template\":\"%s\"",tp); }
        if(e->dev_class) AP(",\"device_class\":\"%s\"",e->dev_class);
        if(e->unit) AP(",\"unit_of_measurement\":\"%s\"",e->unit);
        if(e->state_class) AP(",\"state_class\":\"%s\"",e->state_class);
        if(e->diag) AP(",\"entity_category\":\"diagnostic\"");
        if(e->off) AP(",\"enabled_by_default\":false");
        if(e->extra){ if(!strncmp(e->extra,"\"json_attributes_topic\":\"@Y\"",28)) AP(",\"json_attributes_topic\":\"%s\"",t); else AP(",%s",e->extra); }
        AP("}");
    }
    AP("}}");
#undef AP
    return (int)k;
}

/* ---------------- connection ---------------- */
static int sock=-1, connected=0;
static int64_t t_next_try=0, t_last_tx=0, t_ping=0, backoff=BACKOFF0_MS;
static uint16_t pid_next=1;
static uint8_t rxb[256]; static int rxn=0;
/* the retained slots: latest value wins; republished on every connect */
static char s_info[2048]; static int n_info=0;
static char s_state[3072]; static int n_state=0;
static char s_shot[LINE_MAX_B]; static int n_shot=0;
static char s_sum[4096]; static int n_sum=0;
static int shot_dirty=0, sum_dirty=0, state_dirty=0, info_dirty=0;
static struct { char d[EV_MAX_B]; int n; uint16_t pid; int sent; } evq[EV_RING]; static int ev_h=0, ev_n=0;

static uint16_t next_pid(void){ uint16_t p=pid_next++; if(!pid_next) pid_next=1; return p; }
static void drop_conn(const char*why,int64_t now){
    if(sock>=0){ close(sock); sock=-1; }
    if(connected) lg("disconnected: %s",why);
    connected=0; rxn=0;
    for(int i=0;i<ev_n;i++) evq[(ev_h+i)%EV_RING].sent=0;              /* unacknowledged events go out again (DUP) */
    int64_t j=backoff/4; uint32_t r=(uint32_t)(now*2654435761u);
    t_next_try=now+backoff-j+(int64_t)(r%(uint32_t)(2*j+1)); backoff=backoff*2>BACKOFF_MAX_MS?BACKOFF_MAX_MS:backoff*2;
    if(st.state==ST_CONNECTED||st.state==ST_CONNECTING) set_state(ST_WAITING,why,now);
}
static int send_all(const uint8_t*b,int n,int64_t now){
    int o=0; while(o<n){ ssize_t w=send(sock,b+o,(size_t)(n-o),MSG_NOSIGNAL); if(w<=0){ if(w<0&&errno==EINTR) continue; drop_conn("send failed",now); return 0; } o+=(int)w; }
    t_last_tx=now; return 1;
}
static int publish(const char*sub_or_topic,int full_topic,const char*pl,int pn,int qos,int retain,int dup,uint16_t pid,int64_t now){
    char t[160]; if(full_topic) snprintf(t,sizeof t,"%s",sub_or_topic); else topic_of(t,sizeof t,sub_or_topic);
    int k=build_publish(txb,(int)sizeof txb,t,pl,pn,qos,retain,dup,pid); if(k<0){ st.n_drop++; return 0; }
    if(!send_all(txb,k,now)) return 0;
    st.n_pub++; st.last_pub_ms=now; return 1;
}
static int publish_discovery(int64_t now){
    if(!cf.discovery) return 1;
    static char d[16384]; int n=discovery_build(d,sizeof d); if(n<0){ lg("discovery document too large"); return 1; }
    char t[160]; snprintf(t,sizeof t,"%s/device/merakiplus_%s/config",cf.prefix,cf.id);
    return publish(t,1,d,n,1,1,0,next_pid(),now);
}
static void flush_pending(int64_t now){
    if(!connected) return;
    if(info_dirty&&n_info){ info_dirty=0; if(!publish("info",0,s_info,n_info,1,1,0,next_pid(),now)) return; if(!publish_discovery(now)) return; }
    if(state_dirty&&n_state){ state_dirty=0; if(!publish("state",0,s_state,n_state,0,1,0,0,now)) return; }
    if(shot_dirty){ shot_dirty=0; if(n_shot&&!publish("shot",0,s_shot,n_shot,1,1,0,next_pid(),now)) return; }
    if(sum_dirty){ sum_dirty=0; if(n_sum&&!publish("shot/summary",0,s_sum,n_sum,1,1,0,next_pid(),now)) return; }
    for(int i=0;i<ev_n&&connected;i++){ int k=(ev_h+i)%EV_RING;
        if(evq[k].sent) continue;
        int dup=evq[k].pid!=0; if(!evq[k].pid) evq[k].pid=next_pid();
        if(!publish("event",0,evq[k].d,evq[k].n,1,0,dup,evq[k].pid,now)) return;
        evq[k].sent=1; }
}
static void on_puback(uint16_t pid){
    for(int i=0;i<ev_n;i++){ int k=(ev_h+i)%EV_RING; if(evq[k].sent&&evq[k].pid==pid){
        /* remove k: shift the later ones down (the ring is small) */
        for(int j=i;j<ev_n-1;j++){ int a=(ev_h+j)%EV_RING, b=(ev_h+j+1)%EV_RING; evq[a]=evq[b]; }
        ev_n--; return; } }
}
static int resolve_and_check(struct sockaddr_storage*ss,socklen_t*sl,int64_t now){
    struct addrinfo hints, *res=NULL; memset(&hints,0,sizeof hints); hints.ai_socktype=SOCK_STREAM; hints.ai_family=AF_UNSPEC;
    char port[8]; snprintf(port,sizeof port,"%d",cf.port);
    int r=getaddrinfo(cf.host,port,&hints,&res);
    if(r!=0||!res){ set_state(ST_WAITING,"dns",now); lg("cannot resolve the broker name (%s)",gai_strerror(r)); return 0; }
    int ok=0, seen=0;
    for(struct addrinfo*a=res;a;a=a->ai_next){ seen=1; if(addr_local(a->ai_addr)&&a->ai_addrlen<=sizeof *ss){ memcpy(ss,a->ai_addr,a->ai_addrlen); *sl=a->ai_addrlen; ok=1; break; } }
    freeaddrinfo(res);
    if(!ok&&seen){ set_state(ST_NOTLOCAL,"not_local",now); lg("refused: the broker address is not on the local network (private/link-local/unique-local only)"); }
    return ok;
}
static void try_connect(int64_t now){
    struct sockaddr_storage ss; socklen_t sl=0;
    if(!resolve_and_check(&ss,&sl,now)){
        if(st.state==ST_NOTLOCAL){ t_next_try=now+BACKOFF_MAX_MS; return; }   /* re-checked every minute (DNS may change); never connects */
        drop_conn("dns",now); return; }
    set_state(ST_CONNECTING,NULL,now);
    sock=socket(ss.ss_family,SOCK_STREAM,0); if(sock<0){ drop_conn("socket",now); return; }
    fcntl(sock,F_SETFD,FD_CLOEXEC);
    int fl=fcntl(sock,F_GETFL,0); fcntl(sock,F_SETFL,fl|O_NONBLOCK);
    int r=connect(sock,(struct sockaddr*)&ss,sl);
    if(r<0&&errno!=EINPROGRESS){ drop_conn("connect refused",now); return; }
    struct pollfd p={sock,POLLOUT,0}; if(r<0){ if(poll(&p,1,CONN_TMO_MS)<=0){ drop_conn("connect timeout",now); return; }
        int e=0; socklen_t el=sizeof e; getsockopt(sock,SOL_SOCKET,SO_ERROR,&e,&el); if(e){ drop_conn("connect refused",now); return; } }
    fcntl(sock,F_SETFL,fl);                                              /* blocking, with timeouts */
    struct timeval tv={5,0}; setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv); setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
#ifdef SO_NOSIGPIPE
    { int one=1; setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one); }
#endif
    char cid[40], will[160]; snprintf(cid,sizeof cid,"merakiplus-%s",cf.id); topic_of(will,sizeof will,"availability");
    int k=build_connect(txb,cid,will,"offline",cf.user,cf.pass,KEEPALIVE_S);
    if(k<0||!send_all(txb,k,now)){ memset(txb,0,sizeof txb); return; }
    memset(txb,0,(size_t)k);                                             /* the password does not linger in the send buffer */
    uint8_t ca[4]; int got=0; int64_t dl=now+CONN_TMO_MS;
    while(got<4){ struct pollfd q={sock,POLLIN,0}; int left=(int)(dl-mono_ms()); if(left<=0||poll(&q,1,left)<=0){ drop_conn("no CONNACK",now); return; }
        ssize_t n=recv(sock,ca+got,(size_t)(4-got),0); if(n<=0){ drop_conn("closed before CONNACK",now); return; } got+=(int)n; }
    if(ca[0]!=P_CONNACK||ca[1]!=2){ drop_conn("not a CONNACK",now); return; }
    if(ca[3]!=0){ static const char*const RC[]={"","protocol","client id","server unavailable","bad user or password","not authorized"};
        lg("broker refused the connection: %s",ca[3]<6?RC[ca[3]]:"?"); if(sock>=0){ close(sock); sock=-1; }
        connected=0; t_next_try=now+BACKOFF_MAX_MS; set_state(ST_AUTH,ca[3]==4||ca[3]==5?"auth":"refused",now); return; }
    connected=1; backoff=BACKOFF0_MS; t_ping=0; rxn=0;
    { static long n_conn=0; if(n_conn++) st.n_reconn++; }
    lg("connected to the broker (port %d)",cf.port);
    set_state(ST_CONNECTED,NULL,now);
    if(!publish("availability",0,"online",6,1,1,0,next_pid(),now)) return;
    info_dirty=1; state_dirty=1; shot_dirty=n_shot>0; sum_dirty=n_sum>0;
    if(!n_info&&!publish_discovery(now)) return;                       /* with info, discovery follows info (sw/hw versions) */
    flush_pending(now);
}
/* incoming bytes from the broker: CONNACK (only during connect), PUBACK, PINGRESP. ANYTHING else ends the connection. */
static void on_rx(int64_t now){
    ssize_t n=recv(sock,rxb+rxn,sizeof rxb-(size_t)rxn,MSG_DONTWAIT);
    if(n==0){ drop_conn("broker closed the connection",now); return; }
    if(n<0){ if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) drop_conn("recv",now); return; }
    rxn+=(int)n;
    while(rxn>=2){
        uint8_t t=rxb[0]&0xF0; int len=rxb[1];
        if(t==P_PUBLISH){ lg("the broker sent a PUBLISH — this client accepts no messages; disconnecting"); drop_conn("unexpected packet",now); return; }
        if(rxb[1]&0x80||len>2){ lg("unexpected packet from the broker (type %u, length %d) — disconnecting",t>>4,len); drop_conn("unexpected packet",now); return; }
        if(t==P_PUBACK&&len==2&&rxb[0]==P_PUBACK){ if(rxn<4) return; on_puback((uint16_t)((rxb[2]<<8)|rxb[3])); }
        else if(t==P_PINGRESP&&len==0&&rxb[0]==P_PINGRESP){ t_ping=0; }
        else { lg("unexpected packet from the broker (type %u) — disconnecting",t>>4); drop_conn("unexpected packet",now); return; }
        int used=2+len; memmove(rxb,rxb+used,(size_t)(rxn-used)); rxn-=used;
    }
}

/* ---------------- the feed (stdin): "<channel>\t<json>\n" ---------------- */
static char lb[LINE_MAX_B+1]; static int ln=0, skipping=0;
static void on_line(char*l,int n,int64_t now){
    char*tab=memchr(l,'\t',(size_t)n); if(!tab){ st.n_drop++; return; }
    *tab=0; const char*ch=l; char*pl=tab+1; int pn=n-(int)(pl-l);
    if(pn<2||pl[0]!='{'||pl[pn-1]!='}'){ st.n_drop++; return; }
    if(!strcmp(ch,"state")){ if(pn<(int)sizeof s_state){ memcpy(s_state,pl,(size_t)pn); n_state=pn; state_dirty=1; } else st.n_drop++; }
    else if(!strcmp(ch,"info")){ if(pn<(int)sizeof s_info){ memcpy(s_info,pl,(size_t)pn); s_info[pn]=0; n_info=pn; info_dirty=1;
            json_pick(s_info,"ui_version",info_ui,sizeof info_ui); json_pick(s_info,"controller_fw",info_fw,sizeof info_fw); } else st.n_drop++; }
    else if(!strcmp(ch,"live")){ if(connected&&cf.live) publish("live",0,pl,pn,0,0,0,0,now); else st.n_drop++; }
    else if(!strcmp(ch,"event")){
        if(pn>=EV_MAX_B){ st.n_drop++; return; }
        if(ev_n==EV_RING){ ev_h=(ev_h+1)%EV_RING; ev_n--; st.n_drop++; }   /* full: the oldest goes */
        int k=(ev_h+ev_n)%EV_RING; memcpy(evq[k].d,pl,(size_t)pn); evq[k].n=pn; evq[k].pid=0; evq[k].sent=0; ev_n++; }
    else if(!strcmp(ch,"shot")){ if(pn<(int)sizeof s_shot){ memcpy(s_shot,pl,(size_t)pn); n_shot=pn; shot_dirty=1; } else st.n_drop++; }
    else if(!strcmp(ch,"shotsum")){ if(pn<(int)sizeof s_sum){ memcpy(s_sum,pl,(size_t)pn); n_sum=pn; sum_dirty=1; } else st.n_drop++; }
    else st.n_drop++;                                                    /* unknown channel: ignored (newer UI) */
}
static int on_stdin(int64_t now){
    ssize_t n=read(0,lb+ln,(size_t)(LINE_MAX_B-ln));
    if(n==0) return 0;
    if(n<0) return errno==EINTR||errno==EAGAIN;
    ln+=(int)n; int s=0;
    for(int i=0;i<ln;i++) if(lb[i]=='\n'){ if(!skipping) on_line(lb+s,i-s,now); skipping=0; s=i+1; }
    if(s){ memmove(lb,lb+s,(size_t)(ln-s)); ln-=s; }
    if(ln>=LINE_MAX_B){ ln=0; skipping=1; st.n_drop++; }               /* an over-long line is dropped as a whole */
    return 1;
}

static void shutdown_clean(int64_t now){
    if(connected){
        flush_pending(now);
        if(connected) publish("availability",0,"offline",7,1,1,0,next_pid(),now);
        if(connected){ uint8_t d[2]; int k=mq_out(d,P_DISCONNECT,0); if(k>0) send_all(d,k,now); }
    }
    if(sock>=0) close(sock);
    sock=-1; connected=0; set_state(ST_STOPPED,NULL,now); lg("stopped (feed closed)");
}

#ifndef MQTT_SELFTEST
int main(int argc,char**argv){
    if(argc>=3&&!strcmp(argv[1],"--discovery")){
        if(!conf_load(argv[2])){ fprintf(stderr,"merakiplus_mqtt: %s: %s\n",argv[2],conf_err); return 2; }
        if(argc>=4){ json_pick(argv[3],"ui_version",info_ui,sizeof info_ui); json_pick(argv[3],"controller_fw",info_fw,sizeof info_fw); }
        static char d[16384]; int n=discovery_build(d,sizeof d); if(n<0) return 3;
        printf("%s/device/merakiplus_%s/config\n%s\n",cf.prefix,cf.id,d); return 0;
    }
    if(argc<3){ fprintf(stderr,"usage: merakiplus_mqtt <mqtt.conf> <status-file> [<log-file>]   (stdin = the Meraki+ feed)\n"); return 2; }
    signal(SIGPIPE,SIG_IGN);
    snprintf(status_path,sizeof status_path,"%s",argv[2]);
    if(argc>=4) snprintf(log_path,sizeof log_path,"%s",argv[3]);
    int64_t now=mono_ms();
    if(!conf_load(argv[1])){ set_state(ST_CONFIG,conf_err,now); lg("not started: %s",conf_err); status_write(now);
        while(read(0,lb,sizeof lb)>0){} return 3; }                       /* drain the feed until the UI closes it */
    lg("started (broker port %d, discovery %d, live %d)",cf.port,cf.discovery,cf.live);
    status_write(now);
    int in_open=1;
    while(in_open){
        now=mono_ms();
        if(!connected&&sock<0&&now>=t_next_try) try_connect(now);
        now=mono_ms();
        if(connected){
            flush_pending(now);
            if(connected&&now-t_last_tx>=(KEEPALIVE_S*1000*3)/4&&!t_ping){ uint8_t p[2]; int k=mq_out(p,P_PINGREQ,0); if(k>0&&send_all(p,k,now)) t_ping=now; }
            if(connected&&t_ping&&now-t_ping>PING_TMO_MS) drop_conn("no PINGRESP",now);
        }
        if(now-st.t_written>=STATUS_MS||(st.n_pub!=st.n_pub_w&&now-st.t_written>=1000)) status_write(now);   /* RAM-backed /tmp: cheap */
        struct pollfd fds[2]={{0,POLLIN,0},{sock,POLLIN,0}}; int nf=connected?2:1;
        int tmo=1000; if(!connected&&t_next_try>now&&t_next_try-now<tmo) tmo=(int)(t_next_try-now);
        int r=poll(fds,(nfds_t)nf,tmo); now=mono_ms();
        if(r<0&&errno!=EINTR) break;
        if(r>0&&(fds[0].revents&(POLLIN|POLLHUP|POLLERR))){ if(!on_stdin(now)) in_open=0; }
        if(r>0&&nf==2&&connected&&(fds[1].revents&(POLLIN|POLLHUP|POLLERR))) on_rx(now);
    }
    shutdown_clean(mono_ms());
    memset(&cf,0,sizeof cf);
    return 0;
}
#else
/* ---------------- selftest (host): the address rule, the packets, the discovery document ---------------- */
static int nt=0, nf=0;
static void ck(const char*what,int ok){ nt++; if(!ok) nf++; printf("  [%s] %s\n",ok?"PASS":"FAIL",what); }
static int is_local_str(const char*s){
    struct sockaddr_in a4; struct sockaddr_in6 a6; memset(&a4,0,sizeof a4); memset(&a6,0,sizeof a6);
    if(inet_pton(AF_INET,s,&a4.sin_addr)==1){ a4.sin_family=AF_INET; return addr_local((struct sockaddr*)&a4); }
    if(inet_pton(AF_INET6,s,&a6.sin6_addr)==1){ a6.sin6_family=AF_INET6; return addr_local((struct sockaddr*)&a6); }
    return -1;
}
int main(void){
    printf("== merakiplus_mqtt selftest ==\n");
    (void)try_connect; (void)on_rx; (void)on_stdin; (void)shutdown_clean;   /* the network loop is tested against a mock broker (tests/test_mqtt.py) */
    static const char*const LOC[]={"10.0.0.0","10.255.255.255","172.16.0.0","172.31.255.255","192.168.1.50","169.254.3.4","127.0.0.1",
                                   "fd12:3456::1","fc00::1","fe80::1","::1","::ffff:192.168.1.100","::ffff:10.0.0.0"};
    static const char*const REM[]={"8.8.8.8","1.1.1.1","172.15.255.255","172.32.0.1","192.169.0.1","100.64.0.1","0.0.0.0","255.255.255.255",
                                   "2001:4860:4860::8888","2a00:1450::1","::","::ffff:8.8.8.8","::ffff:100.64.0.1","64:ff9b::808:808","fec0::1"};
    int ok=1; for(size_t i=0;i<sizeof LOC/sizeof LOC[0];i++) if(is_local_str(LOC[i])!=1){ ok=0; printf("     not local: %s\n",LOC[i]); }
    ck("private / link-local / unique-local / loopback / v4-mapped private are accepted",ok);
    ok=1; for(size_t i=0;i<sizeof REM/sizeof REM[0];i++) if(is_local_str(REM[i])!=0){ ok=0; printf("     accepted: %s\n",REM[i]); }
    ck("public v4/v6, CGNAT 100.64/10, 0.0.0.0, broadcast, ::, v4-mapped public, NAT64, deprecated site-local are refused",ok);
    uint8_t b[256]; int k=build_connect(b,"merakiplus-ab12","merakiplus/ab12/availability","offline","u","p",30);
    static const uint8_t want[]={0x10,0x48,0,4,'M','Q','T','T',4,0xEE,0,30,0,15,'m','e','r','a','k','i','p','l','u','s','-','a','b','1','2'};
    ck("CONNECT: MQTT 3.1.1, clean session, last will 'offline' (QoS 1, retained), user + password, keep-alive 30 s",
       k==0x48+2&&!memcmp(b,want,sizeof want)&&!memcmp(b+29,"\0\x1cmerakiplus/ab12/availability\0\x07offline\0\x01u\0\x01p",2+28+2+7+3+3));
    k=build_connect(b,"merakiplus-ab12","t","offline","","",30); ck("CONNECT without a user: no user/password flags",k>0&&b[9]==0x2E);
    k=build_publish(b,sizeof b,"a/b","{}",2,1,1,0,0x1234);
    ck("PUBLISH QoS 1 retained = 33 09 00 03 'a/b' 12 34 '{}'",k==11&&!memcmp(b,"\x33\x09\0\x03" "a/b\x12\x34{}",11));
    k=build_publish(b,sizeof b,"a","x",1,0,0,0,0); ck("PUBLISH QoS 0 = 30 04 00 01 'a' 'x'",k==6&&!memcmp(b,"\x30\x04\0\x01" "ax",6));
    ck("a 200-byte remaining length is encoded in two bytes (C8 01)",put_len(b,200)==2&&b[0]==0xC8&&b[1]==1);
    int ref=1; for(int t=0;t<16;t++){ uint8_t ty=(uint8_t)(t<<4); int allow=ty==P_CONNECT||ty==P_PUBLISH||ty==P_PINGREQ||ty==P_DISCONNECT; if((mq_out(b,(uint8_t)(ty|(ty==0x80?2:0)),0)>=0)!=allow) ref=0; }
    ck("mq_out() builds only CONNECT / PUBLISH / PINGREQ / DISCONNECT — SUBSCRIBE (0x82), UNSUBSCRIBE, PUBREC/REL/COMP, PUBACK are refused",ref);
    snprintf(cf.id,sizeof cf.id,"3fa9c2d1"); snprintf(cf.base,sizeof cf.base,"merakiplus"); snprintf(cf.prefix,sizeof cf.prefix,"homeassistant");
    snprintf(info_ui,sizeof info_ui,"6.24"); snprintf(info_fw,sizeof info_fw,"V5.1.4");
    static char d[16384]; int n=discovery_build(d,sizeof d);
    ck("discovery: built, < 12 KB, names the device by the random id only",n>0&&n<12288&&strstr(d,"\"identifiers\":[\"merakiplus_3fa9c2d1\"]")!=NULL);
    ck("discovery: no command topic, no controllable platform (switch/button/number/select/text)",
       !strstr(d,"command_topic")&&!strstr(d,"cmd_t")&&!strstr(d,"\"switch\"")&&!strstr(d,"\"button\"")&&!strstr(d,"\"number\"")&&!strstr(d,"\"select\"")&&!strstr(d,"\"text\""));
    ck("discovery: availability topic + an event entity with shot_finished",strstr(d,"\"availability_topic\":\"merakiplus/3fa9c2d1/availability\"")&&strstr(d,"\"shot_finished\""));
    char o[24]; json_pick("{\"ui_version\":\"6.24<b>{x}\",\"controller_fw\":\"V5.1.4\"}","ui_version",o,sizeof o);
    ck("json_pick keeps only [A-Za-z0-9 ._-] (no quote or markup gets into the discovery document)",!strcmp(o,"6.24bx"));
    char path[]="/tmp/merakiplus_mqtt_selftest_conf_XXXXXX"; int fd=mkstemp(path);
    const char*c1="# test\nenabled = 1\nhost = 192.168.1.50\nport = 1884\nuser = merakiplus\npass = s3cret pw\nid = 3fa9c2d1\n";
    if(fd>=0){ if(write(fd,c1,strlen(c1))<0){} close(fd); }
    ck("mqtt.conf: key = value with spaces and comments",conf_load(path)&&cf.port==1884&&!strcmp(cf.host,"192.168.1.50")&&!strcmp(cf.pass,"s3cret pw")&&!strcmp(cf.base,"merakiplus")&&cf.discovery==1&&cf.live==1);
    const char*bad[]={"enabled = 0\nhost = 10.0.0.0\nid = ab\n","enabled = 1\nid = ab\n","enabled = 1\nhost = 10.0.0.0\n","enabled = 1\nhost = 10.0.0.0\nid = a/b\n",
                      "enabled = 1\nhost = 10.0.0.0\nid = ab\nbase = x/#\n","enabled = 1\nhost = 10.0.0.0\nid = ab\nport = 70000\n","enabled = 1\nhost = a b\nid = ab\n"};
    static const char*const why[]={"disabled","bad_host","bad_id","bad_id","bad_base","bad_port","bad_host"};
    ok=1; for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){ fd=open(path,O_WRONLY|O_TRUNC); if(fd>=0){ if(write(fd,bad[i],strlen(bad[i]))<0){} close(fd); }
        if(conf_load(path)||strcmp(conf_err,why[i])){ ok=0; printf("     case %d: %s\n",(int)i,conf_err); } }
    ck("mqtt.conf refused: disabled, no host, no id, '/' in id, '#' in base, port > 65535, space in host",ok);
    unlink(path);
    printf("== merakiplus_mqtt selftest: %s (%d checks, %d failed) ==\n",nf?"FAILED":"ALL OK",nt,nf);
    return nf?1:0;
}
#endif
