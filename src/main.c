#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#else
#include <unistd.h>
#endif

static void safe_strcat(char *dst, size_t dst_sz, const char *src) {
    size_t len = strlen(dst);
    if (len < dst_sz - 1) {
        strncat(dst, src, dst_sz - len - 1);
    }
}

#include <dirent.h>
#include <curl/curl.h>
#include "miniz.h"
#include "lang.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#  include <direct.h>
#  define PATH_SEP      '\\'
#  define HOME_ENV      "USERPROFILE"
#  define MKDIR(p)      _mkdir(p)
#  define RMDIR(p)      _rmdir(p)
#else
#  include <unistd.h>
#  define PATH_SEP      '/'
#  define HOME_ENV      "HOME"
#  define MKDIR(p)      mkdir((p), 0755)
#  define RMDIR(p)      rmdir(p)
#endif

/* ===== 定数 ===== */
#define AU2CAT_VERSION "v0.1.4"
#define INDEX_URL    "https://raw.githubusercontent.com/Neosku/aviutl2-catalog-data/main/index.json"
#define CACHE_FILE   "au2cat_cache.json"   /* ホームディレクトリ内 */
#define CACHE_TTL    1800                  /* 30分 (秒) */
#define CONFIG_FILE  "au2cat_config.json"  /* ホームディレクトリ内 */
#define TMP_DIRNAME  "au2cat_tmp"          /* ホームディレクトリ内 */
#define MAX_PKG      512
#define MAX_STR      512
#define MAX_TAGS     16
#define MAX_DEPS     32

/* インストーラー関連 */
#define ACT_STR      256
#define MAX_ACTIONS  8
#define MAX_ARGS     6

/* ===== カラー ===== */
#ifdef _WIN32
static HANDLE g_hout;
static void   color_init(void) { g_hout = GetStdHandle(STD_OUTPUT_HANDLE); }
static void   color_set(int c) {
    WORD attr;
    switch (c) {
        case 31: attr = FOREGROUND_RED   | FOREGROUND_INTENSITY; break;
        case 32: attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
        case 33: attr = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
        case 34: attr = FOREGROUND_BLUE  | FOREGROUND_INTENSITY; break;
        case 35: attr = FOREGROUND_RED   | FOREGROUND_BLUE  | FOREGROUND_INTENSITY; break;
        case 36: attr = FOREGROUND_GREEN | FOREGROUND_BLUE  | FOREGROUND_INTENSITY; break;
        case 1 : attr = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
        default: attr = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
    }
    SetConsoleTextAttribute(g_hout, attr);
}
#  define C_RESET   color_set(0)
#  define C(c)      color_set(c)
#else
static void color_init(void) {}
#  define C_RESET   printf("\033[0m")
#  define C(c)      printf("\033[%dm", (c))
#endif

/* ===== インストーラー構造体 ===== */
typedef enum {
    ACT_DOWNLOAD, ACT_EXTRACT, ACT_EXTRACT_SFX, ACT_COPY,
    ACT_RUN, ACT_RUN_AUO_SETUP, ACT_DELETE, ACT_UNKNOWN
} ActionType;

typedef struct {
    ActionType type;
    char path[ACT_STR];
    char from[ACT_STR];
    char to[ACT_STR];
    char args[MAX_ARGS][ACT_STR];
    int  arg_count;
    int  elevate;
} InstallAction;

typedef struct {
    int  is_github;
    char direct_url[ACT_STR];
    char gh_owner[128];
    char gh_repo[128];
    char gh_pattern[256];
    InstallAction install_actions[MAX_ACTIONS];
    int  install_count;
    InstallAction uninstall_actions[MAX_ACTIONS];
    int  uninstall_count;
} Installer;

/* ===== パッケージ構造体 ===== */
typedef struct {
    char id[MAX_STR];
    char name[MAX_STR];
    char type[MAX_STR];
    char summary[MAX_STR];
    char author[MAX_STR];
    char repo_url[MAX_STR];
    char latest_version[MAX_STR];
    long popularity;
    long trend;
    char tags[MAX_TAGS][MAX_STR];
    int  tag_count;
    char deps[MAX_DEPS][MAX_STR];
    int  dep_count;
    int  deprecated;
    Installer installer;
    int  has_installer;
    char niconi_commons[MAX_STR];
    struct {
        char version[MAX_STR];
        struct { char path[256]; char xxh128[33]; } files[16];
        int file_count;
    } versions[16];
    int version_count;
} Package;

static Package g_pkg[MAX_PKG];
static int     g_count = 0;

/* ===== 最小JSONパーサー ===== */
static const char *skip_ws(const char *p) {
    while (*p && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) p++;
    return p;
}

static const char *parse_str(const char *p, char *buf, int sz) {
    int i = 0;
    if (*p != '"') return NULL;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"': case '\\': case '/':
                    if(i<sz-1) buf[i++]=*p;
                    break;
                case 'n': if (i<sz-1) buf[i++]='\n'; break;
                case 'r': if (i<sz-1) buf[i++]='\r'; break;
                case 't': if (i<sz-1) buf[i++]='\t'; break;
                case 'u': {
                    unsigned u=0; int j;
                    for(j=0;j<4&&p[1];j++){
                        p++; char c=*p;
                        if(c>='0'&&c<='9') u=u*16+(c-'0');
                        else if(c>='a'&&c<='f') u=u*16+(c-'a'+10);
                        else if(c>='A'&&c<='F') u=u*16+(c-'A'+10);
                    }
                    if(u<0x80){if(i<sz-1)buf[i++]=(char)u;}
                    else if(u<0x800){if(i<sz-2){buf[i++]=(char)(0xC0|(u>>6));buf[i++]=(char)(0x80|(u&0x3F));}}
                    else{if(i<sz-3){buf[i++]=(char)(0xE0|(u>>12));buf[i++]=(char)(0x80|((u>>6)&0x3F));buf[i++]=(char)(0x80|(u&0x3F));}}
                    break;
                }
                default: if(i<sz-1)buf[i++]=*p; break;
            }
        } else {
            if(i<sz-1) buf[i++]=*p;
        }
        p++;
    }
    buf[i]='\0';
    if(*p=='"') p++;
    return p;
}

static const char *skip_val(const char *p);
static const char *skip_val(const char *p) {
    char tmp[MAX_STR];
    p=skip_ws(p);
    if(*p=='"'){ p=parse_str(p,tmp,sizeof(tmp)); }
    else if(*p=='{'){
        p++; p=skip_ws(p);
        while(*p&&*p!='}'){
            p=parse_str(p,tmp,sizeof(tmp)); if(!p)return NULL;
            p=skip_ws(p); if(*p==':')p++;
            p=skip_val(p); if(!p)return NULL;
            p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
        }
        if(*p=='}')p++;
    } else if(*p=='['){
        p++; p=skip_ws(p);
        while(*p&&*p!=']'){
            p=skip_val(p); if(!p)return NULL;
            p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
        }
        if(*p==']')p++;
    } else {
        while(*p&&*p!=','&&*p!='}'&&*p!=']'&&*p!='\n') p++;
    }
    return p;
}

/* ---- インストーラー JSON パース ---- */
static const char *parse_action(const char *p, InstallAction *act) {
    memset(act,0,sizeof(*act));
    char key[64], val[ACT_STR];
    p=skip_ws(p);
    if(*p!='{') return skip_val(p);
    p++; p=skip_ws(p);
    while(*p && *p!='}'){
        p=skip_ws(p); if(*p!='"') break;
        p=parse_str(p,key,sizeof(key)); if(!p) return NULL;
        p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);

        if(!strcmp(key,"action")){
            p=parse_str(p,val,sizeof(val));
            if(!strcmp(val,"download")) act->type=ACT_DOWNLOAD;
            else if(!strcmp(val,"extract")) act->type=ACT_EXTRACT;
            else if(!strcmp(val,"extract_sfx")) act->type=ACT_EXTRACT_SFX;
            else if(!strcmp(val,"copy")) act->type=ACT_COPY;
            else if(!strcmp(val,"run")) act->type=ACT_RUN;
            else if(!strcmp(val,"run_auo_setup")) act->type=ACT_RUN_AUO_SETUP;
            else if(!strcmp(val,"delete")) act->type=ACT_DELETE;
            else act->type=ACT_UNKNOWN;
        }
        else if(!strcmp(key,"path")) p=parse_str(p,act->path,sizeof(act->path));
        else if(!strcmp(key,"from")) p=parse_str(p,act->from,sizeof(act->from));
        else if(!strcmp(key,"to"))   p=parse_str(p,act->to,sizeof(act->to));
        else if(!strcmp(key,"elevate")){
            if(!strncmp(p,"true",4)){ act->elevate=1; p+=4; }
            else if(!strncmp(p,"false",5)){ act->elevate=0; p+=5; }
            else p=skip_val(p);
        }
        else if(!strcmp(key,"args")){
            if(*p=='['){
                p++; p=skip_ws(p);
                while(*p && *p!=']'){
                    if(*p=='"' && act->arg_count<MAX_ARGS) p=parse_str(p,act->args[act->arg_count++],ACT_STR);
                    else p=skip_val(p);
                    if(!p) return NULL;
                    p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                }
                if(*p==']')p++;
            } else p=skip_val(p);
        }
        else p=skip_val(p);

        if(!p) return NULL;
        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
    }
    if(*p=='}')p++;
    return p;
}

static const char *parse_action_array(const char *p, InstallAction *arr, int *count, int maxn) {
    *count=0;
    p=skip_ws(p);
    if(*p!='['){ return skip_val(p); }
    p++; p=skip_ws(p);
    while(*p && *p!=']'){
        if(*p=='{'){
            if(*count<maxn) p=parse_action(p,&arr[(*count)++]);
            else p=skip_val(p);
        } else p=skip_val(p);
        if(!p) return NULL;
        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
    }
    if(*p==']')p++;
    return p;
}

static const char *parse_installer(const char *p, Installer *ins) {
    memset(ins,0,sizeof(*ins));
    char key[64];
    p=skip_ws(p);
    if(*p!='{') return skip_val(p);
    p++; p=skip_ws(p);
    while(*p && *p!='}'){
        p=skip_ws(p); if(*p!='"') break;
        p=parse_str(p,key,sizeof(key)); if(!p) return NULL;
        p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);

        if(!strcmp(key,"source")){
            p=skip_ws(p);
            if(*p=='{'){
                p++; p=skip_ws(p);
                while(*p && *p!='}'){
                    char skey[64];
                    p=skip_ws(p); if(*p!='"') break;
                    p=parse_str(p,skey,sizeof(skey)); if(!p) return NULL;
                    p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                    if(!strcmp(skey,"direct")){
                        p=parse_str(p,ins->direct_url,sizeof(ins->direct_url));
                        ins->is_github=0;
                    } else if(!strcmp(skey,"googleDrive")){
                        p=skip_ws(p);
                        if(*p=='{'){
                            p++; p=skip_ws(p);
                            while(*p && *p!='}'){
                                char gkey[64];
                                p=skip_ws(p); if(*p!='"') break;
                                p=parse_str(p,gkey,sizeof(gkey)); if(!p) return NULL;
                                p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                                if(!strcmp(gkey,"id")) {
                                    char gid[128]; p=parse_str(p,gid,sizeof(gid));
                                    snprintf(ins->direct_url,sizeof(ins->direct_url),"gdrive:%s",gid);
                                }
                                else p=skip_val(p);
                                if(!p) return NULL;
                                p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                            }
                            if(*p=='}')p++;
                        }
                        ins->is_github=0;
                    } else if(!strcmp(skey,"booth")){
                        p=skip_ws(p);
                        if(*p=='{'){
                            p++; p=skip_ws(p);
                            while(*p && *p!='}'){
                                char gkey[64];
                                p=skip_ws(p); if(*p!='"') break;
                                p=parse_str(p,gkey,sizeof(gkey)); if(!p) return NULL;
                                p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                                if(!strcmp(gkey,"url")) {
                                    char b[256]; p=parse_str(p,b,sizeof(b));
                                    snprintf(ins->direct_url,sizeof(ins->direct_url),"booth:%s",b);
                                }
                                else p=skip_val(p);
                                if(!p) return NULL;
                                p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                            }
                            if(*p=='}')p++;
                        }
                        ins->is_github=0;
                    } else if(!strcmp(skey,"github")){
                        ins->is_github=1;
                        p=skip_ws(p);
                        if(*p=='{'){
                            p++; p=skip_ws(p);
                            while(*p && *p!='}'){
                                char gkey[64];
                                p=skip_ws(p); if(*p!='"') break;
                                p=parse_str(p,gkey,sizeof(gkey)); if(!p) return NULL;
                                p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                                if(!strcmp(gkey,"owner")) p=parse_str(p,ins->gh_owner,sizeof(ins->gh_owner));
                                else if(!strcmp(gkey,"repo")) p=parse_str(p,ins->gh_repo,sizeof(ins->gh_repo));
                                else if(!strcmp(gkey,"pattern")) p=parse_str(p,ins->gh_pattern,sizeof(ins->gh_pattern));
                                else p=skip_val(p);
                                if(!p) return NULL;
                                p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                            }
                            if(*p=='}')p++;
                        }
                    } else p=skip_val(p);
                    if(!p) return NULL;
                    p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                }
                if(*p=='}')p++;
            } else p=skip_val(p);
        }
        else if(!strcmp(key,"install"))   p=parse_action_array(p, ins->install_actions,   &ins->install_count,   MAX_ACTIONS);
        else if(!strcmp(key,"uninstall")) p=parse_action_array(p, ins->uninstall_actions, &ins->uninstall_count, MAX_ACTIONS);
        else p=skip_val(p);

        if(!p) return NULL;
        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
    }
    if(*p=='}')p++;
    return p;
}

static const char *parse_pkg(const char *p, Package *pk) {
    char key[MAX_STR];
    memset(pk,0,sizeof(*pk));
    p=skip_ws(p);
    while(*p&&*p!='}'){
        p=skip_ws(p); if(*p!='"') break;
        p=parse_str(p,key,sizeof(key)); if(!p)return NULL;
        p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);

        if(!strcmp(key,"id"))          { p=parse_str(p,pk->id,MAX_STR); }
        else if(!strcmp(key,"name"))   { p=parse_str(p,pk->name,MAX_STR);
            /* ⚠️ = E2 9A A0 */
            if((unsigned char)pk->name[0]==0xE2&&(unsigned char)pk->name[1]==0x9A&&(unsigned char)pk->name[2]==0xA0) pk->deprecated=1;
        }
        else if(!strcmp(key,"type"))         { p=parse_str(p,pk->type,MAX_STR); }
        else if(!strcmp(key,"summary"))      { p=parse_str(p,pk->summary,MAX_STR); }
        else if(!strcmp(key,"author"))       { p=parse_str(p,pk->author,MAX_STR); }
        else if(!strcmp(key,"repoURL"))      { p=parse_str(p,pk->repo_url,MAX_STR); }
        else if(!strcmp(key,"latest-version")){ p=parse_str(p,pk->latest_version,MAX_STR); }
        else if(!strcmp(key,"popularity"))   {
            char n[32]; int i=0;
            while(*p&&(isdigit((unsigned char)*p)||*p=='-')){if(i<31)n[i++]=*p;p++;}
            n[i]='\0'; pk->popularity=atol(n);
        }
        else if(!strcmp(key,"trend"))        {
            char n[32]; int i=0;
            while(*p&&(isdigit((unsigned char)*p)||*p=='-')){if(i<31)n[i++]=*p;p++;}
            n[i]='\0'; pk->trend=atol(n);
        }
        else if(!strcmp(key,"tags")) {
            if(*p=='['){
                p++; p=skip_ws(p);
                while(*p&&*p!=']'){
                    if(*p=='"'&&pk->tag_count<MAX_TAGS) p=parse_str(p,pk->tags[pk->tag_count++],MAX_STR);
                    else p=skip_val(p);
                    if(!p)return NULL;
                    p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                }
                if(*p==']')p++;
            } else p=skip_val(p);
        }
        else if(!strcmp(key,"dependencies")) {
            if(*p=='['){
                p++; p=skip_ws(p);
                while(*p&&*p!=']'){
                    if(*p=='"'&&pk->dep_count<MAX_DEPS) p=parse_str(p,pk->deps[pk->dep_count++],MAX_STR);
                    else p=skip_val(p);
                    if(!p)return NULL;
                    p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                }
                if(*p==']')p++;
            } else p=skip_val(p);
        }
        else if(!strcmp(key,"niconiCommonsId")) {
            if(*p=='"') p=parse_str(p,pk->niconi_commons,MAX_STR);
            else if(*p=='n') { p+=4; pk->niconi_commons[0] = '\0'; }
            else p=skip_val(p);
        }
        else if(!strcmp(key,"version")) {
            if(*p=='['){
                p++; p=skip_ws(p);
                while(*p&&*p!=']'){
                    if(*p=='{'){
                        p++; p=skip_ws(p);
                        int vc;
                        if(pk->version_count<16){
                            vc = pk->version_count++;
                        } else {
                            int k;
                            for(k=0; k<15; k++) pk->versions[k] = pk->versions[k+1];
                            vc = 15;
                        }
                        pk->versions[vc].file_count = 0;
                        while(*p&&*p!='}'){
                            char vkey[64];
                            p=skip_ws(p); if(*p!='"') break;
                            p=parse_str(p,vkey,sizeof(vkey)); if(!p) return NULL;
                            p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                            if(!strcmp(vkey,"version")) p=parse_str(p,pk->versions[vc].version,MAX_STR);
                            else if(!strcmp(vkey,"file")){
                                if(*p=='['){
                                    p++; p=skip_ws(p);
                                    while(*p&&*p!=']'){
                                        if(*p=='{'){
                                            int fc;
                                            if(pk->versions[vc].file_count<16){
                                                fc = pk->versions[vc].file_count++;
                                            } else {
                                                int k;
                                                for(k=0; k<15; k++) pk->versions[vc].files[k] = pk->versions[vc].files[k+1];
                                                fc = 15;
                                            }
                                            p++; p=skip_ws(p);
                                            pk->versions[vc].files[fc].path[0] = '\0';
                                            pk->versions[vc].files[fc].xxh128[0] = '\0';
                                            while(*p&&*p!='}'){
                                                char fkey[64];
                                                p=skip_ws(p); if(*p!='"') break;
                                                p=parse_str(p,fkey,sizeof(fkey)); if(!p) return NULL;
                                                p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                                                if(!strcmp(fkey,"path")) p=parse_str(p,pk->versions[vc].files[fc].path,256);
                                                else if(!strcmp(fkey,"XXH3_128")) p=parse_str(p,pk->versions[vc].files[fc].xxh128,33);
                                                else p=skip_val(p);
                                                p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                                            }
                                            if(*p=='}')p++;
                                        } else p=skip_val(p);
                                        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                                    }
                                    if(*p==']')p++;
                                } else p=skip_val(p);
                            } else p=skip_val(p);
                            p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                        }
                        if(*p=='}')p++;
                    } else p=skip_val(p);
                    p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
                }
                if(*p==']')p++;
            } else p=skip_val(p);
        }
        else if(!strcmp(key,"installer")) { p=parse_installer(p,&pk->installer); pk->has_installer=1; }
        else { p=skip_val(p); }

        if(!p)return NULL;
        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
    }
    if(*p=='}')p++;
    return p;
}

static int parse_catalog(const char *json) {
    const char *p=skip_ws(json);
    if(*p!='[') return 0;
    p++; p=skip_ws(p);
    while(*p&&*p!=']') {
        if(*p=='{') {
            p++;
            if(g_count<MAX_PKG){
                p=parse_pkg(p,&g_pkg[g_count]);
                if(p) g_count++; else return 0;
            } else { p=skip_val(p-1); }
        }
        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
    }
    return 1;
}

/* ===== HTTP / キャッシュ ===== */
typedef struct { char *data; size_t size; } Buf;

static size_t write_cb(void *ptr, size_t sz, size_t n, void *ud) {
    Buf *b=(Buf*)ud; size_t add=sz*n;
    char *t=realloc(b->data, b->size+add+1);
    if(!t)return 0;
    b->data=t; memcpy(b->data+b->size,ptr,add);
    b->size+=add; b->data[b->size]='\0';
    return add;
}

static char *http_get(const char *url) {
    CURL *c=curl_easy_init(); if(!c)return NULL;
    Buf b={NULL,0};
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write_cb);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"au2cat/1.0");
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    CURLcode r=curl_easy_perform(c);
    curl_easy_cleanup(c);
    if(r!=CURLE_OK){ fprintf(stderr,_( "au2cat: ダウンロードエラー: %s\n" ),curl_easy_strerror(r)); free(b.data); return NULL; }
    return b.data;
}

static size_t write_file_cb(void *ptr, size_t sz, size_t n, void *ud) {
    FILE *f=(FILE*)ud;
    return fwrite(ptr,sz,n,f);
}

/* ===== 汎用ファイルシステムユーティリティ ===== */
static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp,sizeof(tmp),"%s",path);
    size_t len=strlen(tmp);
    if(len && (tmp[len-1]=='/'||tmp[len-1]=='\\')) tmp[len-1]='\0';
    char *p;
    for(p=tmp+1; *p; p++){
        if(*p=='/'||*p=='\\'){
            char c=*p; *p='\0';
            MKDIR(tmp);
            *p=c;
        }
    }
    MKDIR(tmp);
}

static int download_to_file(const char *url, const char *outpath) {
    char dirbuf[1024]; snprintf(dirbuf,sizeof(dirbuf),"%s",outpath);
    char *slash=strrchr(dirbuf,'/');
    if(!slash) slash=strrchr(dirbuf,'\\');
    if(slash){ *slash='\0'; mkdir_p(dirbuf); }

    FILE *f=fopen(outpath,"wb");
    if(!f) return 0;
    CURL *c=curl_easy_init();
    if(!c){ fclose(f); return 0; }
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write_file_cb);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"au2cat/1.0");
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    CURLcode r=curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);
    if(r!=CURLE_OK){ fprintf(stderr,_( "au2cat: ダウンロードエラー: %s\n" ),curl_easy_strerror(r)); return 0; }
    return 1;
}

static int copy_file_single(const char *src, const char *dst) {
    FILE *in=fopen(src,"rb"); if(!in) return 0;
    char dstdir[1024]; snprintf(dstdir,sizeof(dstdir),"%s",dst);
    char *slash=strrchr(dstdir,'/');
    if(!slash) slash=strrchr(dstdir,'\\');
    if(slash){ *slash='\0'; mkdir_p(dstdir); }
    FILE *out=fopen(dst,"wb");
    if(!out){ fclose(in); return 0; }
    char buf[8192]; size_t n;
    while((n=fread(buf,1,sizeof(buf),in))>0) fwrite(buf,1,n,out);
    fclose(in); fclose(out);
    return 1;
}

static int copy_dir_recursive(const char *src, const char *dst) {
    mkdir_p(dst);
    DIR *d=opendir(src);
    if(!d) return 0;
    struct dirent *ent;
    int ok=1;
    while((ent=readdir(d))){
        if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,"..")) continue;
        char s[1024], t[1024];
        snprintf(s,sizeof(s),"%s/%s",src,ent->d_name);
        snprintf(t,sizeof(t),"%s/%s",dst,ent->d_name);
        struct stat st;
        if(stat(s,&st)==0 && S_ISDIR(st.st_mode)) { if(!copy_dir_recursive(s,t)) ok=0; }
        else { if(!copy_file_single(s,t)) ok=0; }
    }
    closedir(d);
    return ok;
}

static int remove_dir_recursive(const char *path) {
    DIR *d=opendir(path);
    if(!d) return 0;
    struct dirent *ent;
    while((ent=readdir(d))){
        if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,"..")) continue;
        char sub[1024]; snprintf(sub,sizeof(sub),"%s/%s",path,ent->d_name);
        struct stat st;
        if(stat(sub,&st)==0 && S_ISDIR(st.st_mode)) remove_dir_recursive(sub);
        else remove(sub);
    }
    closedir(d);
    RMDIR(path);
    return 1;
}

static int extract_zip(const char *zip_path, const char *dest_dir) {
    mz_zip_archive za;
    memset(&za, 0, sizeof(za));
    if(!mz_zip_reader_init_file(&za, zip_path, 0)){
        fprintf(stderr,_( "au2cat: zipを開けません: %s\n" ), zip_path); return 0;
    }
    mz_uint n = mz_zip_reader_get_num_files(&za);
    mz_uint i;
    for(i=0;i<n;i++){
        mz_zip_archive_file_stat fstat;
        if(!mz_zip_reader_file_stat(&za, i, &fstat)) continue;
        char outpath[1024];
        snprintf(outpath,sizeof(outpath),"%s/%s",dest_dir,fstat.m_filename);
        if(mz_zip_reader_is_file_a_directory(&za, i)){
            mkdir_p(outpath); continue;
        }
        char dirbuf[1024]; snprintf(dirbuf,sizeof(dirbuf),"%s",outpath);
        char *slash=strrchr(dirbuf,'/');
        if(slash){ *slash='\0'; mkdir_p(dirbuf); }
        if(!mz_zip_reader_extract_to_file(&za, i, outpath, 0)){
            fprintf(stderr,_( "au2cat: 展開に失敗: %s\n" ), fstat.m_filename);
        }
    }
    mz_zip_reader_end(&za);
    return 1;
}

/* キャッシュパス: ~/au2cat_cache.json */
static void home_path(char *buf, int sz, const char *fname) {
    const char *home=getenv(HOME_ENV);
    if(home) snprintf(buf,sz,"%s%c%s",home,PATH_SEP,fname);
    else      snprintf(buf,sz,"%s",fname);
}

static char *load_cache(void) {
    char path[1024]; home_path(path,sizeof(path),CACHE_FILE);
    FILE *f=fopen(path,"rb"); if(!f)return NULL;

#ifndef _WIN32
    struct stat st;
    if(stat(path,&st)==0){
        time_t age=time(NULL)-st.st_mtime;
        if(age>CACHE_TTL){ fclose(f); return NULL; }
    }
#else
    HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(h!=INVALID_HANDLE_VALUE){
        FILETIME wt; ULARGE_INTEGER ui;
        GetFileTime(h,NULL,NULL,&wt);
        CloseHandle(h);
        ui.LowPart=wt.dwLowDateTime; ui.HighPart=wt.dwHighDateTime;
        time_t mtime=(time_t)((ui.QuadPart-116444736000000000ULL)/10000000ULL);
        if(time(NULL)-mtime>CACHE_TTL){ fclose(f); return NULL; }
    }
#endif

    fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=malloc(len+1); if(!buf){fclose(f);return NULL;}
    size_t rd = fread(buf,1,len,f); buf[rd]='\0'; fclose(f);
    return buf;
}

static void save_cache(const char *json) {
    char path[1024]; home_path(path,sizeof(path),CACHE_FILE);
    FILE *f=fopen(path,"wb"); if(!f)return;
    fwrite(json,1,strlen(json),f); fclose(f);
}

/* カタログを取得してパース (キャッシュ優先) */
static int load_catalog(int force_update) {
    char *json=NULL;
    int from_cache=0;

    if(!force_update) {
        json=load_cache();
        if(json) from_cache=1;
    }
    if(!json) {
        C(33); printf(":: "); C_RESET; printf(_( "カタログを取得しています... (%s)\n" ), INDEX_URL);
        fflush(stdout);
        json=http_get(INDEX_URL);
        if(!json){ fprintf(stderr,_( "au2cat: 取得に失敗しました\n" )); return 0; }
        save_cache(json);
    }

    int ok=parse_catalog(json);
    free(json);
    if(!ok){ fprintf(stderr,_( "au2cat: パースに失敗しました\n" )); return 0; }

    if(from_cache){
        C(32); printf(":: "); C_RESET;
        printf(_( "キャッシュから読み込みました (%d 件)\n" ), g_count);
    } else {
        C(32); printf(":: "); C_RESET;
        printf(_( "データベースを更新しました (%d 件のパッケージ)\n" ), g_count);
    }
    return 1;
}

/* ===== 文字列ユーティリティ ===== */
static int str_iequal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static int str_icontains(const char *haystack, const char *needle) {
    size_t nl=strlen(needle);
    if(!nl) return 1;
    size_t hl=strlen(haystack);
    if(hl<nl) return 0;
    size_t i,j;
    for(i=0;i<=hl-nl;i++){
        for(j=0;j<nl;j++){
            char hc=haystack[i+j], nc=needle[j];
            if(hc>='A'&&hc<='Z') hc+='a'-'A';
            if(nc>='A'&&nc<='Z') nc+='a'-'A';
            if(hc!=nc) break;
        }
        if(j==nl) return 1;
    }
    return 0;
}

static void print_cell(const char *s, int col_bytes) {
    int byte_pos=0, display_width=0, limit=col_bytes;
    while(s[byte_pos]){
        unsigned char c=(unsigned char)s[byte_pos];
        int bytes=1, dw=1;
        if((c&0xF8)==0xF0){bytes=4;dw=2;}
        else if((c&0xF0)==0xE0){bytes=3;dw=2;}
        else if((c&0xE0)==0xC0){bytes=2;dw=1;}
        if(display_width+dw>limit) break;
        int k; for(k=0;k<bytes;k++) putchar(s[byte_pos+k]);
        byte_pos+=bytes; display_width+=dw;
    }
    while(display_width<limit){ putchar(' '); display_width++; }
}

/* ===== ソート用比較 ===== */
static int cmp_pop(const void*a,const void*b){ return (int)(g_pkg[*(int*)b].popularity-g_pkg[*(int*)a].popularity); }
static int cmp_name(const void*a,const void*b){ return strcmp(g_pkg[*(int*)a].name,g_pkg[*(int*)b].name); }

/* ===== パッケージ検索 (id完全一致 -> 名前完全一致 -> 部分一致) ===== */
static Package *find_package(const char *query) {
    int i;
    for(i=0;i<g_count;i++) if(!strcmp(g_pkg[i].id,query)) return &g_pkg[i];
    for(i=0;i<g_count;i++) if(str_iequal(g_pkg[i].name,query)) return &g_pkg[i];

    Package *candidates[MAX_PKG]; int cn=0;
    for(i=0;i<g_count;i++)
        if(str_icontains(g_pkg[i].name,query)||str_icontains(g_pkg[i].id,query))
            candidates[cn++]=&g_pkg[i];
    if(cn==1) return candidates[0];
    if(cn>1){
        fprintf(stderr,_( "au2cat: \"%s\" は複数のパッケージに該当します:\n" ),query);
        for(i=0;i<cn;i++) fprintf(stderr,"  %s (%s)\n",candidates[i]->name,candidates[i]->id);
        fprintf(stderr,_( "ID を指定してください。\n" ));
        return NULL;
    }
    return NULL;
}

/* ===== 簡易正規表現 (^ $ . \d \. * +  リテラル のみ対応) ===== */
static int rx_get_atom(const char **pat, int *is_digit, int *is_any, char *lit) {
    const char *p=*pat;
    if(*p=='\\'){
        p++;
        if(*p=='d'){ *is_digit=1; *is_any=0; *lit=0; p++; }
        else { *lit=*p; *is_digit=0; *is_any=0; p++; }
    } else if(*p=='.'){ *is_any=1; *is_digit=0; *lit=0; p++; }
    else { *lit=*p; *is_digit=0; *is_any=0; p++; }
    *pat=p;
    return 1;
}

static int rx_atom_matches(int is_digit,int is_any,char lit,char c){
    if(is_digit) return isdigit((unsigned char)c)!=0;
    if(is_any) return c!='\0';
    return c==lit;
}

static int rx_match_here(const char *pat, const char *s) {
    if(*pat=='\0') return *s=='\0';
    if(*pat=='$' && *(pat+1)=='\0') return *s=='\0';

    const char *p=pat;
    int is_digit=0,is_any=0; char lit=0;
    rx_get_atom(&p,&is_digit,&is_any,&lit);

    char quant = *p;
    if(quant=='*'||quant=='+'){
        const char *rest=p+1;
        int min_count = (quant=='+')?1:0;
        int count=0;
        const char *t=s;
        while(rx_atom_matches(is_digit,is_any,lit,*t)){ t++; count++; }
        int k;
        for(k=count;k>=min_count;k--){
            if(rx_match_here(rest, s+k)) return 1;
        }
        return 0;
    } else {
        if(!rx_atom_matches(is_digit,is_any,lit,*s)) return 0;
        return rx_match_here(p, s+1);
    }
}

static int rx_match(const char *pattern, const char *str) {
    const char *p = pattern;
    if(*p=='^') p++;
    return rx_match_here(p, str);
}

/* ===== GitHub Releases ===== */
typedef struct { char name[ACT_STR]; char url[700]; } GhAsset;

static int gh_parse_assets(const char *json, GhAsset *out, int max_assets) {
    const char *p = strstr(json, "\"assets\"");
    if(!p) return 0;
    p = strchr(p, '[');
    if(!p) return 0;
    p++; p=skip_ws(p);
    int n=0;
    while(*p && *p!=']' && n<max_assets) {
        if(*p=='{'){
            p++;
            char key[64];
            GhAsset a; memset(&a,0,sizeof(a));
            p=skip_ws(p);
            while(*p && *p!='}'){
                p=skip_ws(p);
                if(*p!='"') break;
                p=parse_str(p,key,sizeof(key));
                p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
                if(!strcmp(key,"name")) p=parse_str(p,a.name,sizeof(a.name));
                else if(!strcmp(key,"browser_download_url")) p=parse_str(p,a.url,sizeof(a.url));
                else p=skip_val(p);
                if(!p) return n;
                p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
            }
            if(*p=='}') p++;
            out[n++]=a;
        }
        p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
    }
    return n;
}

static int resolve_source(Package *pkg, char *url_out, size_t url_sz, char *name_out, size_t name_sz) {
    Installer *ins=&pkg->installer;
    if(!ins->is_github){
        if(!ins->direct_url[0]){ fprintf(stderr,_( "au2cat: ダウンロード元が設定されていません\n" )); return 0; }
        if(!strncmp(ins->direct_url,"gdrive:",7)){
            snprintf(url_out,url_sz,"https://drive.google.com/uc?export=download&id=%s",ins->direct_url+7);
            snprintf(name_out,name_sz,"download.zip");
            return 1;
        } else if(!strncmp(ins->direct_url,"booth:",6)){
            fprintf(stderr,_( "au2cat: BOOTHからの自動ダウンロードはCLIではサポートされていません。\n手動でダウンロードして展開してください: %s\n" ),ins->direct_url+6);
            return 0;
        }
        snprintf(url_out,url_sz,"%s",ins->direct_url);
        const char *base=strrchr(ins->direct_url,'/');
        snprintf(name_out,name_sz,"%s", base?base+1:ins->direct_url);
        return 1;
    }
    char api[512];
    snprintf(api,sizeof(api),"https://api.github.com/repos/%s/%s/releases/latest", ins->gh_owner, ins->gh_repo);
    char *json = http_get(api);
    if(!json){ fprintf(stderr,_( "au2cat: GitHub APIへのアクセスに失敗しました\n" )); return 0; }
    GhAsset assets[64];
    int n=gh_parse_assets(json,assets,64);
    int found=0,i;
    for(i=0;i<n;i++){
        if(rx_match(ins->gh_pattern, assets[i].name)){
            snprintf(url_out,url_sz,"%s",assets[i].url);
            snprintf(name_out,name_sz,"%s",assets[i].name);
            found=1; break;
        }
    }
    free(json);
    if(!found) fprintf(stderr,_( "au2cat: パターンに一致するリリースアセットが見つかりません: %s\n" ), ins->gh_pattern);
    return found;
}

/* ===== 設定 (appDir / pluginsDir) ===== */
typedef struct { char app_dir[1024]; char plugins_dir[1024]; char script_dir[1024]; } Config;

static void config_path(char *buf,int sz){ home_path(buf,sz,CONFIG_FILE); }

static int load_config(Config *cfg) {
    memset(cfg,0,sizeof(*cfg));
    char path[1024]; config_path(path,sizeof(path));
    FILE *f=fopen(path,"rb");
    if(!f) return 0;
    fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=malloc(len+1); if(!buf){fclose(f);return 0;}
    size_t rd=fread(buf,1,len,f); buf[rd]='\0'; fclose(f);

    const char *p=skip_ws(buf);
    if(*p=='{'){
        p++;
        char key[64];
        p=skip_ws(p);
        while(*p && *p!='}'){
            p=skip_ws(p); if(*p!='"') break;
            p=parse_str(p,key,sizeof(key)); if(!p) break;
            p=skip_ws(p); if(*p==':')p++; p=skip_ws(p);
            if(!strcmp(key,"appDir")) p=parse_str(p,cfg->app_dir,sizeof(cfg->app_dir));
            else if(!strcmp(key,"pluginsDir")) p=parse_str(p,cfg->plugins_dir,sizeof(cfg->plugins_dir));
            else if(!strcmp(key,"scriptDir")) p=parse_str(p,cfg->script_dir,sizeof(cfg->script_dir));
            else p=skip_val(p);
            if(!p) break;
            p=skip_ws(p); if(*p==',')p++; p=skip_ws(p);
        }
    }
    free(buf);
    return cfg->app_dir[0]!=0;
}

static void json_escape(const char *in, char *out, size_t outsz) {
    size_t oi=0;
    const char *p;
    for(p=in; *p && oi+2<outsz; p++){
        if(*p=='\\' || *p=='"') out[oi++]='\\';
        out[oi++]=*p;
    }
    out[oi]='\0';
}

static void save_config(Config *cfg) {
    char path[1024]; config_path(path,sizeof(path));
    char esc_app[2048], esc_plug[2048], esc_script[2048];
    json_escape(cfg->app_dir,esc_app,sizeof(esc_app));
    json_escape(cfg->plugins_dir,esc_plug,sizeof(esc_plug));
    json_escape(cfg->script_dir,esc_script,sizeof(esc_script));
    FILE *f=fopen(path,"wb");
    if(!f) return;
    fprintf(f,"{\n  \"appDir\": \"%s\",\n  \"pluginsDir\": \"%s\",\n  \"scriptDir\": \"%s\"\n}\n", esc_app, esc_plug, esc_script);
    fclose(f);
}

static void get_absolute_path(const char *rel, char *abs_out, size_t size) {
#ifdef _WIN32
    if (!_fullpath(abs_out, rel, size)) {
        snprintf(abs_out, size, "%s", rel);
    }
#else
    if (rel[0] == '/') {
        snprintf(abs_out, size, "%s", rel);
    } else if (rel[0] == '~' && (rel[1] == '/' || rel[1] == '\0')) {
        const char *h = getenv("HOME");
        if (h) snprintf(abs_out, size, "%s%s", h, rel + 1);
        else snprintf(abs_out, size, "%s", rel);
    } else {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(abs_out, size, "%s/%s", cwd, rel);
        } else {
            snprintf(abs_out, size, "%s", rel);
        }
    }
#endif
}

static void prompt_config(Config *cfg) {
    char line[1024]; size_t l;

    C(33); printf(":: "); C_RESET;
    printf(_( "AviUtl2 のインストール先が設定されていません。初回設定を行います。\n" ));
#ifdef _WIN32
    const char *def_app = "C:\\Program Files\\AviUtl2";
#else
    const char *def_app = NULL;
#endif

    while (1) {
        if(def_app) printf(_( "AviUtl2 のインストールフォルダのパス [既定: %s]: " ), def_app);
        else printf(_( "AviUtl2 のインストールフォルダのパスを入力してください: " ));
        fflush(stdout);
        
        cfg->app_dir[0] = '\0';
        if(fgets(line,sizeof(line),stdin)){
            l=strlen(line); while(l>0 && (line[l-1]=='\n'||line[l-1]=='\r')) line[--l]='\0';
            if(l>0) get_absolute_path(line, cfg->app_dir, sizeof(cfg->app_dir));
            else if(def_app) get_absolute_path(def_app, cfg->app_dir, sizeof(cfg->app_dir));
        } else if (def_app) {
            get_absolute_path(def_app, cfg->app_dir, sizeof(cfg->app_dir));
        }
        
        if (cfg->app_dir[0] != '\0') break;
        printf(_( "パスは空にできません。\n" ));
    }

    char default_plugins[1024];
    snprintf(default_plugins,sizeof(default_plugins),"%s/Plugin",cfg->app_dir);
    printf(_( "プラグインフォルダのパス [既定: %s]: " ), default_plugins);
    fflush(stdout);
    if(fgets(line,sizeof(line),stdin)){
        l=strlen(line); while(l>0 && (line[l-1]=='\n'||line[l-1]=='\r')) line[--l]='\0';
        if(l>0) get_absolute_path(line, cfg->plugins_dir, sizeof(cfg->plugins_dir));
        else    get_absolute_path(default_plugins, cfg->plugins_dir, sizeof(cfg->plugins_dir));
    } else {
        get_absolute_path(default_plugins, cfg->plugins_dir, sizeof(cfg->plugins_dir));
    }

    char default_script[1024];
#ifdef _WIN32
    snprintf(default_script, sizeof(default_script), "C:\\ProgramData\\AviUtl2\\Script");
#else
    snprintf(default_script, sizeof(default_script), "%s/script", cfg->app_dir);
#endif
    printf(_( "スクリプトフォルダのパス [既定: %s]: " ), default_script);
    fflush(stdout);
    if(fgets(line,sizeof(line),stdin)){
        l=strlen(line); while(l>0 && (line[l-1]=='\n'||line[l-1]=='\r')) line[--l]='\0';
        if(l>0) get_absolute_path(line, cfg->script_dir, sizeof(cfg->script_dir));
        else    get_absolute_path(default_script, cfg->script_dir, sizeof(cfg->script_dir));
    } else {
        get_absolute_path(default_script, cfg->script_dir, sizeof(cfg->script_dir));
    }

    save_config(cfg);
    C(32); printf(":: "); C_RESET; printf(_( "設定を保存しました: " ));
    char path[1024]; config_path(path,sizeof(path)); printf("%s\n",path);
}

static void ensure_config(Config *cfg) {
    if(!load_config(cfg)){
        prompt_config(cfg);
    } else if(!cfg->plugins_dir[0]){
        snprintf(cfg->plugins_dir,sizeof(cfg->plugins_dir),"%s/Plugin",cfg->app_dir);
    }
    if(!cfg->script_dir[0]){
#ifdef _WIN32
        snprintf(cfg->script_dir,sizeof(cfg->script_dir),"C:\\ProgramData\\AviUtl2\\Script");
#else
        snprintf(cfg->script_dir,sizeof(cfg->script_dir),"%s/script",cfg->app_dir);
#endif
    }
}

/* ===== Windows / Wine による exe 実行 ===== */
#ifndef _WIN32
static int wine_available(void) {
    static int checked=0, avail=0;
    if(!checked){
        checked=1;
        avail = (system("wine --version > /dev/null 2>&1") == 0);
    }
    return avail;
}
#endif

static int can_run_exe(void) {
#ifdef _WIN32
    return 1;
#else
    return wine_available();
#endif
}

static int run_exe(const char *path, char args[][ACT_STR], int argc_, int elevate) {
#ifdef _WIN32
    char argline[2048]=""; int i;
    for(i=0;i<argc_;i++){
        safe_strcat(argline, sizeof(argline), args[i]);
        safe_strcat(argline, sizeof(argline), " ");
    }
    if(elevate){
        SHELLEXECUTEINFOA sei; memset(&sei,0,sizeof(sei));
        sei.cbSize=sizeof(sei);
        sei.lpVerb="runas";
        sei.lpFile=path;
        sei.lpParameters=argline;
        sei.nShow=SW_SHOWNORMAL;
        sei.fMask=SEE_MASK_NOCLOSEPROCESS;
        if(!ShellExecuteExA(&sei)) return 0;
        if(sei.hProcess){ WaitForSingleObject(sei.hProcess, INFINITE); CloseHandle(sei.hProcess); }
        return 1;
    } else {
        char cmd[2560];
        snprintf(cmd,sizeof(cmd),"\"%s\" %s", path, argline);
        return system(cmd)==0;
    }
#else
    if(!wine_available()){
        fprintf(stderr,_( "au2cat: このアクションには Windows または Wine が必要です。スキップします: %s\n" ), path);
        return 0;
    }
    char cmd[2560]; int i;
    snprintf(cmd,sizeof(cmd),"WINEDEBUG=-all wine '%s'",path);
    for(i=0;i<argc_;i++){
        safe_strcat(cmd, sizeof(cmd), " '");
        safe_strcat(cmd, sizeof(cmd), args[i]);
        safe_strcat(cmd, sizeof(cmd), "'");
    }
    if(elevate){
        C(33); printf(":: "); C_RESET;
        printf(_( "Wine環境では管理者権限の昇格はサポートされません。通常権限で実行します。\n" ));
    }
    return system(cmd)==0;
#endif
}

static int do_extract_sfx(const char *exe_path, const char *dest_dir) {
    if(!can_run_exe()){
        fprintf(stderr,_( "au2cat: extract_sfx には Windows または Wine が必要です。スキップします: %s\n" ), exe_path);
        return 0;
    }
    char arg[ACT_STR];
    const char *final_dest = dest_dir;
#ifndef _WIN32
    char wine_dest[1024];
    if (dest_dir[0] == '/') {
        snprintf(wine_dest, sizeof(wine_dest), "Z:%s", dest_dir);
        for (char *p = wine_dest; *p; p++) {
            if (*p == '/') *p = '\\';
        }
        final_dest = wine_dest;
    }
#endif
    snprintf(arg,sizeof(arg),"-o\"%s\" -y", final_dest);
    char args[1][ACT_STR];
    snprintf(args[0],ACT_STR,"%s",arg);
    C(33); printf(":: "); C_RESET;
    printf(_( "自己解凍書庫を展開しています (7-Zip SFX 想定。Windows/Wine使用)...\n" ));
    return run_exe(exe_path,args,1,0);
}

/* ===== パス置換 ({tmp} {appDir} {pluginsDir} {download}) ===== */
typedef struct {
    char pkg_tmp_dir[1024];
    char download_path[1024];
    int  have_download;
} InstallCtx;

static void append_str(char *out, size_t outsz, size_t *oi, const char *s){
    size_t l=strlen(s);
    if(*oi+l>=outsz) l = (*oi<outsz)? outsz-1-*oi : 0;
    if(l>0){ memcpy(out+*oi,s,l); *oi+=l; }
}

static void make_wine_path_str(const char *in, char *out, size_t sz) {
#ifndef _WIN32
    if (in[0] == '/') {
        char tmp[1024]; snprintf(tmp, sizeof(tmp), "Z:%s", in);
        for (char *p = tmp; *p; p++) if (*p == '/') *p = '\\';
        strncpy(out, tmp, sz-1); out[sz-1] = '\0';
        return;
    }
#endif
    strncpy(out, in, sz-1); out[sz-1] = '\0';
}

static void resolve_path_base(const char *tmpl, Config *cfg, char *out, size_t outsz, int to_wine) {
    char w_app[1024], w_plg[1024];
    if (to_wine) {
        make_wine_path_str(cfg->app_dir, w_app, sizeof(w_app));
        make_wine_path_str(cfg->plugins_dir, w_plg, sizeof(w_plg));
    } else {
        strncpy(w_app, cfg->app_dir, sizeof(w_app));
        strncpy(w_plg, cfg->plugins_dir, sizeof(w_plg));
    }
    size_t oi=0; out[0]='\0';
    const char *p=tmpl;
    while(*p){
        if(!strncmp(p,"{appDir}",8)){ append_str(out,outsz,&oi,w_app); p+=8; }
        else if(!strncmp(p,"{pluginsDir}",12)){ append_str(out,outsz,&oi,w_plg); p+=12; }
        else if(!strncmp(p,"{scriptsDir}",12)){
            char buf[1024]; snprintf(buf,sizeof(buf),"%s/script",w_app);
            append_str(out,outsz,&oi,buf); p+=12;
        }
        else if(!strncmp(p,"{dataDir}",9)){ append_str(out,outsz,&oi,w_app); p+=9; }
        else { char c[2]; c[0]=*p; c[1]='\0'; append_str(out,outsz,&oi,c); p++; }
    }
    if(oi<outsz) out[oi]='\0'; else out[outsz-1]='\0';
}

static void resolve_path(const char *tmpl, InstallCtx *ctx, Config *cfg, char *out, size_t outsz, int to_wine) {
    char base[1024];
    resolve_path_base(tmpl, cfg, base, sizeof(base), to_wine);
    char w_tmp[1024], w_dl[1024];
    if (to_wine) {
        make_wine_path_str(ctx->pkg_tmp_dir, w_tmp, sizeof(w_tmp));
        make_wine_path_str(ctx->download_path, w_dl, sizeof(w_dl));
    } else {
        strncpy(w_tmp, ctx->pkg_tmp_dir, sizeof(w_tmp));
        strncpy(w_dl, ctx->download_path, sizeof(w_dl));
    }
    size_t oi=0; out[0]='\0';
    const char *p=base;
    while(*p){
        if(!strncmp(p,"{tmp}",5)){ append_str(out,outsz,&oi,w_tmp); p+=5; }
        else if(!strncmp(p,"{download}",10)){ append_str(out,outsz,&oi,w_dl); p+=10; }
        else { char c[2]; c[0]=*p; c[1]='\0'; append_str(out,outsz,&oi,c); p++; }
    }
    if(oi<outsz) out[oi]='\0'; else out[outsz-1]='\0';
}

/* ===== アクション実行 ===== */
static int exec_action(InstallAction *act, InstallCtx *ctx, Config *cfg, Package *pkg) {
    char rp[1024], rf[1024], rt[1024];

    switch(act->type){
    case ACT_DOWNLOAD: {
        char url[1024], fname[ACT_STR], outpath[1024];
        if(!resolve_source(pkg, url, sizeof(url), fname, sizeof(fname))) return 0;
        snprintf(outpath,sizeof(outpath),"%s/%s",ctx->pkg_tmp_dir,fname);
        C(33); printf(":: "); C_RESET; printf(_( "ダウンロード中: %s\n" ), url);
        if(!download_to_file(url,outpath)) { fprintf(stderr,_( "au2cat: ダウンロードに失敗しました\n" )); return 0; }
        snprintf(ctx->download_path,sizeof(ctx->download_path),"%s",outpath);
        ctx->have_download=1;
        C(32); printf(":: "); C_RESET; printf(_( "ダウンロード完了: %s\n" ), outpath);
        return 1;
    }
    case ACT_EXTRACT:
        if(!ctx->have_download){ fprintf(stderr,_( "au2cat: extract: ダウンロード済みファイルがありません\n" )); return 0; }
        C(33); printf(":: "); C_RESET; printf(_( "展開中: %s\n" ), ctx->download_path);
        return extract_zip(ctx->download_path, ctx->pkg_tmp_dir);

    case ACT_EXTRACT_SFX:
        if(!ctx->have_download){ fprintf(stderr,_( "au2cat: extract_sfx: ダウンロード済みファイルがありません\n" )); return 0; }
        return do_extract_sfx(ctx->download_path, ctx->pkg_tmp_dir);

    case ACT_COPY: {
        resolve_path(act->from, ctx, cfg, rf, sizeof(rf), 0);
        resolve_path(act->to,   ctx, cfg, rt, sizeof(rt), 0);
        struct stat st;
        if(stat(rf,&st)!=0){ fprintf(stderr,_( "au2cat: コピー元が見つかりません: %s\n" ), rf); return 0; }
        C(33); printf(":: "); C_RESET; printf(_( "コピー中: %s -> %s\n" ), rf, rt);
        if(S_ISDIR(st.st_mode)) return copy_dir_recursive(rf,rt);
        else {
            char dst[1024];
            struct stat tst;
            int to_is_dir = (stat(rt,&tst)==0 && S_ISDIR(tst.st_mode));
            size_t rtlen=strlen(rt);
            int to_looks_dir = (rtlen>0 && (rt[rtlen-1]=='/'||rt[rtlen-1]=='\\'));
            if(to_is_dir || to_looks_dir){
                const char *base=strrchr(rf,'/');
                if(!base) base=strrchr(rf,'\\');
                base = base?base+1:rf;
                snprintf(dst,sizeof(dst),"%s/%s",rt,base);
            } else {
                snprintf(dst,sizeof(dst),"%s",rt);
            }
            return copy_file_single(rf,dst);
        }
    }

    case ACT_RUN:
    case ACT_RUN_AUO_SETUP: {
        int is_aviutl2_setup = 0;
        char target_dir[1024] = "";
        for(int i=0; i<act->arg_count; i++) {
            if(!strcmp(act->args[i], "-I") && i+1 < act->arg_count) {
                is_aviutl2_setup = 1;
                resolve_path(act->args[i+1], ctx, cfg, target_dir, sizeof(target_dir), 0); // 0: unix path
                break;
            }
        }
        
        if (is_aviutl2_setup) {
            char rp_unix[1024];
            resolve_path(act->path, ctx, cfg, rp_unix, sizeof(rp_unix), 0);
            
            // Remove quotes from target_dir if present
            if(target_dir[0] == '"') {
                size_t len = strlen(target_dir);
                if(target_dir[len-1] == '"') {
                    target_dir[len-1] = '\0';
                }
                memmove(target_dir, target_dir+1, len);
            }
            
            C(33); printf(":: "); C_RESET;
            printf(_( "インストーラーを直接展開しています (7-Zip使用)...\n" ));
            char cmd[2560];
            // Use 7z to extract directly
            snprintf(cmd, sizeof(cmd), "7z x \"%s\" -o\"%s\" -y > /dev/null", rp_unix, target_dir);
            int ret = system(cmd);
            if (ret == 0) return 1;
#ifdef _WIN32
            C(33); printf(":: "); C_RESET;
            printf(_( "7zでの展開に失敗しました。exeインストーラーを通常起動します...\n" ));
            // Do not return 0; allow fallback to run_exe below
#else
            fprintf(stderr, _( "au2cat: 7zコマンドでの展開に失敗しました。7zがインストールされているか確認してください。\n" ));
            return 0;
#endif
        }

        resolve_path(act->path, ctx, cfg, rp, sizeof(rp), 1);
        if(!can_run_exe()){
            fprintf(stderr,_( "au2cat: このアクションには Windows または Wine が必要です。スキップします: %s\n" ), rp);
            return 0;
        }
        char resolved_args[MAX_ARGS][ACT_STR];
        int i;
        for(i=0;i<act->arg_count;i++) resolve_path(act->args[i], ctx, cfg, resolved_args[i], ACT_STR, 1);
        C(33); printf(":: "); C_RESET; printf(_( "実行中: %s\n" ), rp);
        if(act->elevate){ C(33); printf(":: "); C_RESET; printf(_( "(管理者権限が必要です)\n" )); }
        return run_exe(rp, resolved_args, act->arg_count, act->elevate);
    }

    case ACT_DELETE: {
        resolve_path(act->path, ctx, cfg, rp, sizeof(rp), 0);
        struct stat st;
        if(stat(rp,&st)!=0) return 1; /* 既に存在しない場合は成功扱い */
        C(33); printf(":: "); C_RESET; printf(_( "削除中: %s\n" ), rp);
        if(S_ISDIR(st.st_mode)) return remove_dir_recursive(rp);
        remove(rp);
        return 1;
    }

    default:
        return 1;
    }
}

/* ===== 確認プロンプト ===== */
static int confirm_yes(const char *prompt) {
    char line[16];
    printf("%s [Y/n]: ", prompt);
    fflush(stdout);
    if(!fgets(line,sizeof(line),stdin)) return 1;
    if(line[0]=='\0'||line[0]=='\n') return 1;
    return (line[0]=='y'||line[0]=='Y');
}

static int extract_yes_flag(int *argc, char **argv) {
    int yes=0, w=0, i;
    for(i=0;i<*argc;i++){
        if(!strcmp(argv[i],"-y")||!strcmp(argv[i],"--yes")) yes=1;
        else argv[w++]=argv[i];
    }
    *argc=w;
    return yes;
}

/* ===== サブコマンド実装 ===== */


/* ===== XXH3_128 チェック ===== */
static int check_package_version(Package *p, Config *cfg, int *has_files_out) {
    if(has_files_out) *has_files_out = 0;
    if(p->version_count==0) return -1;
    
    int v;
    for(v=p->version_count-1; v>=0; v--) {
        if(p->versions[v].file_count==0) continue;
        if(has_files_out) *has_files_out = 1;
        int ok = 1;
        int f;
        for(f=0; f<p->versions[v].file_count; f++) {
            char path[1024];
            resolve_path_base(p->versions[v].files[f].path, cfg, path, sizeof(path), 0);
            FILE *file = fopen(path, "rb");
            if(!file) { ok=0; break; }
            
            XXH3_state_t* state = XXH3_createState();
            XXH3_128bits_reset(state);
            char buf[8192];
            size_t rd;
            while((rd = fread(buf, 1, sizeof(buf), file)) > 0) {
                XXH3_128bits_update(state, buf, rd);
            }
            fclose(file);
            XXH128_hash_t hash = XXH3_128bits_digest(state);
            XXH3_freeState(state);
            
            char hex[33];
            snprintf(hex, sizeof(hex), "%016llx%016llx", (unsigned long long)hash.high64, (unsigned long long)hash.low64);
            
            if(p->versions[v].files[f].xxh128[0] != '\0' && strcmp(hex, p->versions[v].files[f].xxh128) != 0) {
                ok=0; break;
            }
        }
        if(ok) return v;
    }
    return -1;
}

/* ---- check ---- */
static void cmd_check(void) {
    Config cfg; ensure_config(&cfg);
    C(33); printf(":: "); C_RESET;
    printf(_( "インストール済みのパッケージを検出しています...\n\n" ));
    
    int found_count = 0;
    int i;
    for(i=0; i<g_count; i++) {
        int v = check_package_version(&g_pkg[i], &cfg, NULL);
        if(v >= 0) {
            C(32); printf("[OK] "); C_RESET;
            printf("%-40s ", g_pkg[i].name);
            C(90); printf("(%s) ", g_pkg[i].id); C_RESET;
            C(35); printf("%s", g_pkg[i].versions[v].version); C_RESET;
            if(strcmp(g_pkg[i].versions[v].version, g_pkg[i].latest_version) != 0) {
                printf(_( " (最新: " )); C(33); printf("%s", g_pkg[i].latest_version); C_RESET; printf(")");
            }
            printf("\n");
            found_count++;
        }
    }
    if(found_count == 0) {
        printf(_( "  インストールされているパッケージが見つかりませんでした。\n" ));
    } else {
        printf(_( "\n計 %d 件のパッケージが検出されました。\n" ), found_count);
    }
}

/* ---- upgrade ---- */
static void upgrade_cli_if_available(int assume_yes) {
    char path[1024]; home_path(path, sizeof(path), "au2cat_cli_update.txt");
    FILE *f = fopen(path, "r");
    if(!f) return;
    
    char tag[64];
    if(fscanf(f, "%63s", tag) == 1) {
        fclose(f);
        C(36); printf(":: "); C_RESET;
        printf(_("au2cat 本体の新しいバージョン (%s) が利用可能です。\n"), tag);
        
        if(!assume_yes) {
            printf(_("本体を更新しますか？ [Y/n] "));
            char ans[16];
            if(fgets(ans, sizeof(ans), stdin) && (ans[0]=='n' || ans[0]=='N')) {
                return;
            }
        }
        
        C(33); printf(":: "); C_RESET;
        printf(_("本体をダウンロード中...\n"));
#ifdef _WIN32
        const char *url = "https://github.com/Madotsukanai/aviutl2-catalog-cli/releases/latest/download/au2cat.exe";
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        char old_path[MAX_PATH];
        snprintf(old_path, sizeof(old_path), "%s.old", exe_path);
        remove(old_path);
        MoveFileExA(exe_path, old_path, MOVEFILE_REPLACE_EXISTING);
        if(download_to_file(url, exe_path)) {
            C(32); printf(":: "); C_RESET;
            printf(_("本体の更新が完了しました！ (%s)\n"), tag);
            remove(path);
            exit(0);
        } else {
            MoveFileExA(old_path, exe_path, MOVEFILE_REPLACE_EXISTING);
            fprintf(stderr, _("au2cat: 本体の更新に失敗しました。\n"));
        }
#else
        const char *url = "https://github.com/Madotsukanai/aviutl2-catalog-cli/releases/latest/download/au2cat";
        char exe_path[1024];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
        if(len != -1) {
            exe_path[len] = '\0';
        } else {
            strcpy(exe_path, "au2cat");
        }
        char tmp_path[1024];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", exe_path);
        if(download_to_file(url, tmp_path)) {
            chmod(tmp_path, 0755);
            rename(tmp_path, exe_path);
            C(32); printf(":: "); C_RESET;
            printf(_("本体の更新が完了しました！ (%s)\n"), tag);
            remove(path);
            exit(0);
        } else {
            fprintf(stderr, _("au2cat: 本体の更新に失敗しました。\n"));
        }
#endif
    } else {
        fclose(f);
    }
}

static void cmd_upgrade(int assume_yes) {
    upgrade_cli_if_available(assume_yes);
    
    Config cfg; ensure_config(&cfg);
    C(33); printf(":: "); C_RESET;
    printf(_( "更新可能なパッケージを確認しています...\n\n" ));
    
    Package *to_upgrade[MAX_PKG];
    int upgrade_count = 0;
    
    int i;
    for(i=0; i<g_count; i++) {
        int v = check_package_version(&g_pkg[i], &cfg, NULL);
        if(v >= 0) {
            if(strcmp(g_pkg[i].versions[v].version, g_pkg[i].latest_version) != 0) {
                if(g_pkg[i].has_installer && g_pkg[i].installer.install_count > 0) {
                    to_upgrade[upgrade_count++] = &g_pkg[i];
                }
            }
        }
    }
    
    if(upgrade_count == 0) {
        printf(_( "  更新可能なパッケージはありません。\n" ));
        return;
    }
    
    C(1); printf(_( "以下のパッケージが更新されます:\n" )); C_RESET;
    for(i=0; i<upgrade_count; i++) {
        printf("  %s (-> %s)\n", to_upgrade[i]->name, to_upgrade[i]->latest_version);
    }
    printf("\n");
    
    if(!assume_yes && !confirm_yes(_( "更新を続行しますか?" ))) {
        printf(_( "中止しました。\n" ));
        return;
    }
    
    for(i=0; i<upgrade_count; i++) {
        Package *p = to_upgrade[i];
        C(33); printf(":: "); C_RESET; printf(_( "更新中: %s\n" ), p->name);
        
        InstallCtx ctx; memset(&ctx,0,sizeof(ctx));
        char home[1024]; const char *h=getenv(HOME_ENV);
        snprintf(home,sizeof(home),"%s",h?h:".");
        snprintf(ctx.pkg_tmp_dir,sizeof(ctx.pkg_tmp_dir),"%s%c%s%c%s",home,PATH_SEP,TMP_DIRNAME,PATH_SEP,p->id);
        mkdir_p(ctx.pkg_tmp_dir);
        
        int ok=1;
        int j;
        for(j=0; j<p->installer.install_count && ok; j++) {
            ok = exec_action(&p->installer.install_actions[j], &ctx, &cfg, p);
        }
        if(ok) { C(32); printf(":: "); C_RESET; printf(_( "更新完了: %s\n" ), p->name); }
        else   { C(31); printf(":: "); C_RESET; printf(_( "更新に失敗しました: %s\n" ), p->name); }
    }
}

/* ---- commons ---- */
static void cmd_commons(void) {
    Config cfg; ensure_config(&cfg);
    C(33); printf(":: "); C_RESET;
    printf(_( "インストール済みパッケージのニコニ・コモンズIDを抽出しています...\n\n" ));
    
    char all_ids[MAX_STR * MAX_PKG] = "";
    int first = 1;
    int i;
    for(i=0; i<g_count; i++) {
        int v = check_package_version(&g_pkg[i], &cfg, NULL);
        if(v >= 0 && g_pkg[i].niconi_commons[0] != '\0') {
            if(!first) safe_strcat(all_ids, sizeof(all_ids), ",");
            safe_strcat(all_ids, sizeof(all_ids), g_pkg[i].niconi_commons);
            first = 0;
            printf("  %s (%s)\n", g_pkg[i].niconi_commons, g_pkg[i].name);
        }
    }
    if(first) {
        printf(_( "  対応するパッケージは見つかりませんでした。\n" ));
    } else {
        printf(_( "\nコモンズID一覧 (カンマ区切り):\n" ));
        C(32); printf("%s\n", all_ids); C_RESET;
    }
}

/* ---- search ---- */
static void cmd_search(int argc, char **argv) {
    char query[MAX_STR]="";
    int i;
    for(i=0;i<argc;i++){
        if(i) strncat(query," ",MAX_STR-strlen(query)-1);
        strncat(query,argv[i],MAX_STR-strlen(query)-1);
    }

    if(!query[0]){
        fprintf(stderr,_( "使い方: au2cat search <キーワード>\n" ));
        return;
    }

    int idx[MAX_PKG]; int cnt=0;
    for(i=0;i<g_count;i++){
        Package *p=&g_pkg[i];
        int match = str_icontains(p->name,   query) ||
                    str_icontains(p->author,  query) ||
                    str_icontains(p->summary, query) ||
                    str_icontains(p->type,    query);
        if(!match){
            int t; for(t=0;t<p->tag_count;t++) if(str_icontains(p->tags[t],query)){match=1;break;}
        }
        if(match) idx[cnt++]=i;
    }
    qsort(idx,cnt,sizeof(int),cmp_pop);

    C(33); printf(":: "); C_RESET;
    printf(_( "検索結果: \"" )); C(1); printf("%s",query); C_RESET; printf(_( "\"  %d 件\n\n" ),cnt);

    if(cnt==0){
        printf(_( "  該当するパッケージが見つかりませんでした。\n" ));
        return;
    }

    C(1);
    printf("%-40s  %-40s  %-14s  %-14s  %s\n",_( "名前" ),"ID",_( "作者" ),_( "種類" ),_( "バージョン" ));
    C_RESET;
    printf("%-40s  %-40s  %-14s  %-14s  %s\n",
        "----------------------------------------",
        "----------------------------------------",
        "--------------","--------------","----------");

    for(i=0;i<cnt;i++){
        Package *p=&g_pkg[idx[i]];
        if(p->deprecated){ C(31); printf(_( "(非推奨) " )); C_RESET; print_cell(p->name,31); }
        else              { C(32); print_cell(p->name,40); C_RESET; }
        printf("  ");
        C(90); print_cell(p->id,40); C_RESET;
        printf("  ");
        C(33); print_cell(p->author,14); C_RESET;
        printf("  ");
        C(36); print_cell(_(p->type),14); C_RESET;
        printf("  ");
        C(35); printf("%s",p->latest_version); C_RESET;
        printf("\n");
    }
    printf("\n");
    C(33); printf(":: "); C_RESET;
    printf(_( "詳細は  au2cat show <名前 または ID>  で確認できます。\n" ));
}

/* ---- list ---- */
static void cmd_list(int argc, char **argv) {
    char type_filter[MAX_STR]="";
    if(argc>0) strncpy(type_filter,argv[0],MAX_STR-1);

    int idx[MAX_PKG]; int cnt=0; int i;
    for(i=0;i<g_count;i++){
        if(type_filter[0] && !str_icontains(g_pkg[i].type,type_filter)) continue;
        idx[cnt++]=i;
    }
    qsort(idx,cnt,sizeof(int),cmp_name);

    C(33); printf(":: "); C_RESET;
    if(type_filter[0]) printf(_( "パッケージ一覧 [種類: %s]  %d 件\n\n" ),type_filter,cnt);
    else               printf(_( "パッケージ一覧 (全種類)  %d 件\n\n" ),cnt);

    C(1);
    printf("%-40s  %-40s  %-14s  %-14s  %s\n",_( "名前" ),"ID",_( "作者" ),_( "種類" ),_( "バージョン" ));
    C_RESET;
    printf("%-40s  %-40s  %-14s  %-14s  %s\n",
        "----------------------------------------",
        "----------------------------------------",
        "--------------","--------------","----------");

    for(i=0;i<cnt;i++){
        Package *p=&g_pkg[idx[i]];
        if(p->deprecated){ C(31); print_cell(p->name,40); C_RESET; }
        else              { C(32); print_cell(p->name,40); C_RESET; }
        printf("  ");
        C(90); print_cell(p->id,40); C_RESET;
        printf("  ");
        C(33); print_cell(p->author,14); C_RESET;
        printf("  ");
        C(36); print_cell(_(p->type),14); C_RESET;
        printf("  ");
        C(35); printf("%s",p->latest_version); C_RESET;
        printf("\n");
    }
}

/* ---- show / info ---- */
static void cmd_show(int argc, char **argv) {
    if(argc==0){ fprintf(stderr,_( "使い方: au2cat show <ID または 名前>\n" )); return; }

    char query[MAX_STR]="";
    int i;
    for(i=0;i<argc;i++){
        if(i) strncat(query," ",MAX_STR-strlen(query)-1);
        strncat(query,argv[i],MAX_STR-strlen(query)-1);
    }

    Package *p = find_package(query);
    if(!p){ fprintf(stderr,_( "au2cat: パッケージ \"%s\" が見つかりません。\n" ),query); return; }

    printf("\n");

    if(p->deprecated){ C(31); C(1); printf("%s",p->name); C_RESET; printf("  "); C(31); printf(_( "[非推奨]" )); C_RESET; }
    else              { C(32); C(1); printf("%s",p->name); C_RESET; }
    printf("\n");

    printf("%s\n",p->summary);
    printf("\n");

    C(1); printf("%-16s","ID"); C_RESET; printf(": %s\n",p->id);
    C(1); printf("%-16s",_( "バージョン" )); C_RESET; printf(": "); C(35); printf("%s",p->latest_version); C_RESET; printf("\n");
    C(1); printf("%-16s",_( "種類" )); C_RESET; printf(": %s\n",_(p->type));
    C(1); printf("%-16s",_( "作者" )); C_RESET; printf(": %s\n",p->author);
    C(1); printf("%-16s",_( "人気度" )); C_RESET; printf(_( ": %ld  トレンド: %ld\n" ),p->popularity,p->trend);
    Config cfg; ensure_config(&cfg);
    int has_files = 0;
    int v = check_package_version(p, &cfg, &has_files);
    C(1); printf("%-16s",_( "インストール状態" )); C_RESET; printf(": ");
    if(v >= 0) {
        C(32); printf(_( "インストール済み (%s)\n" ), p->versions[v].version); C_RESET;
    } else {
        if(has_files) printf(_( "未インストール (ファイル未検出)\n" ));
        else printf(_( "検出情報なし (ハッシュ未登録)\n" ));
    }

    if(p->repo_url[0]){
        C(1); printf("%-16s","URL"); C_RESET; printf(": "); C(34); printf("%s",p->repo_url); C_RESET; printf("\n");
    }

    if(p->tag_count>0){
        int t;
        C(1); printf("%-16s",_( "タグ" )); C_RESET; printf(":");
        for(t=0;t<p->tag_count;t++){ printf(" "); C(35); printf("%s",p->tags[t]); C_RESET; }
        printf("\n");
    }

    if(p->dep_count>0){
        int d;
        C(1); printf("%-16s",_( "依存" )); C_RESET; printf(":");
        for(d=0;d<p->dep_count;d++){ printf(" %s",p->deps[d]); if(d<p->dep_count-1)printf(","); }
        printf("\n");
    }

    C(1); printf("%-16s",_( "インストール" )); C_RESET; printf(": ");
    if(p->has_installer && p->installer.install_count>0){
        C(32); printf(_( "au2cat install \"%s\" で利用可能" ), p->id); C_RESET; printf("\n");
    } else {
        printf(_( "自動インストール情報なし (上記URLから手動で入手してください)\n" ));
    }
    printf("\n");
}

static int do_install_package(Package *p, Config *cfg, int assume_yes, int depth) {
    if (depth > 10) return 0;
    
    int has_files = 0;
    int v = check_package_version(p, cfg, &has_files);
    if (v == 0) {
        C(32); printf(":: "); C_RESET;
        printf(_( "%s は既に最新版がインストールされています。\n" ), p->name);
        return 1;
    }
    
    if(!p->has_installer || p->installer.install_count==0){
        fprintf(stderr,_( "au2cat: \"%s\" には自動インストール情報がありません。手動でインストールしてください: %s\n" ),p->name,p->repo_url);
        return 1; // Continue even if a dependency lacks an installer
    }
    
    for(int i=0; i<p->dep_count; i++){
        Package *dep = find_package(p->deps[i]);
        if(dep){
            if(!do_install_package(dep, cfg, assume_yes, depth+1)){
                fprintf(stderr,_( "au2cat: 依存パッケージ %s のインストールに失敗しました。\n" ), dep->name);
                return 0;
            }
        } else {
            fprintf(stderr,_( "au2cat: 依存パッケージ %s が見つかりません。\n" ), p->deps[i]);
        }
    }

    C(33); printf(":: "); C_RESET;
    printf(_( "以下のパッケージをインストールします: " )); C(1); printf("%s",p->name); C_RESET;
    printf(" ("); C(35); printf("%s",p->latest_version); C_RESET; printf(")\n");
    C(33); printf("   => "); C_RESET;
    printf(_( "インストール先: %s\n" ), cfg->app_dir);
    if(p->deprecated){ C(31); printf(_( ":: 警告: このパッケージは非推奨です\n" )); C_RESET; }

    if(!assume_yes && !confirm_yes(_( "続行しますか?" ))) { printf(_( "中止しました。\n" )); return 0; }

    InstallCtx ctx; memset(&ctx,0,sizeof(ctx));
    char home[1024]; const char *h=getenv(HOME_ENV);
    snprintf(home,sizeof(home),"%s",h?h:".");
    snprintf(ctx.pkg_tmp_dir,sizeof(ctx.pkg_tmp_dir),"%s%c%s%c%s",home,PATH_SEP,TMP_DIRNAME,PATH_SEP,p->id);
    mkdir_p(ctx.pkg_tmp_dir);

    int ok=1;
    for(int i=0;i<p->installer.install_count && ok;i++){
        ok = exec_action(&p->installer.install_actions[i], &ctx, cfg, p);
    }

    if(ok){ C(32); printf(":: "); C_RESET; printf(_( "インストール完了: %s (%s)\n" ),p->name,p->latest_version); }
    else  { C(31); printf(":: "); C_RESET; printf(_( "インストールに失敗しました: %s\n" ),p->name); }
    return ok;
}

/* ---- install ---- */
static void cmd_install(int argc, char **argv, int assume_yes) {
    if(argc==0){ fprintf(stderr,_( "使い方: au2cat install <ID または 名前> [-y]\n" )); return; }

    char query[MAX_STR]="";
    int i;
    for(i=0;i<argc;i++){
        if(i) safe_strcat(query, MAX_STR, " ");
        safe_strcat(query, MAX_STR, argv[i]);
    }

    Package *p = find_package(query);
    if(!p){ fprintf(stderr,_( "au2cat: パッケージ \"%s\" が見つかりません。\n" ),query); return; }

    Config cfg; ensure_config(&cfg);
    do_install_package(p, &cfg, assume_yes, 0);
}

/* ---- uninstall ---- */
static void cmd_uninstall(int argc, char **argv, int assume_yes) {
    if(argc==0){ fprintf(stderr,_( "使い方: au2cat uninstall <ID または 名前> [-y]\n" )); return; }

    char query[MAX_STR]="";
    int i;
    for(i=0;i<argc;i++){
        if(i) safe_strcat(query, MAX_STR, " ");
        safe_strcat(query, MAX_STR, argv[i]);
    }

    Package *p = find_package(query);
    if(!p){ fprintf(stderr,_( "au2cat: パッケージ \"%s\" が見つかりません。\n" ),query); return; }
    
    Config cfg; ensure_config(&cfg);
    
    int has_files = 0;
    int v = check_package_version(p, &cfg, &has_files);
    if (v < 0) {
        if (!has_files) {
            fprintf(stderr,_( "au2cat: パッケージ \"%s\" はインストールされていません。\n" ), p->name);
            return;
        }
        // If files exist but hash doesn't match, we still proceed to uninstall to clean it up.
        v = 0; // Fallback to latest version file list
    }

    if(!p->has_installer || p->installer.uninstall_count==0){
        C(33); printf(":: "); C_RESET;
        printf(_( "以下のパッケージをアンインストールします: " )); C(1); printf("%s",p->name); C_RESET; printf("\n");
        if(!assume_yes && !confirm_yes(_( "続行しますか?" ))) { printf(_( "中止しました。\n" )); return; }
        
        for(int f=0; f<p->versions[v].file_count; f++) {
            char path[1024];
            resolve_path_base(p->versions[v].files[f].path, &cfg, path, sizeof(path), 0);
            C(33); printf(":: "); C_RESET; printf(_( "削除中: %s\n" ), path);
            remove(path);
        }
        C(32); printf(":: "); C_RESET; printf(_( "アンインストール完了: %s\n" ),p->name);
        return;
    }

    C(33); printf(":: "); C_RESET;
    printf(_( "以下のパッケージをアンインストールします: " )); C(1); printf("%s",p->name); C_RESET; printf("\n");

    if(!assume_yes && !confirm_yes(_( "続行しますか?" ))) { printf(_( "中止しました。\n" )); return; }

    InstallCtx ctx; memset(&ctx,0,sizeof(ctx));
    int ok=1;
    for(i=0;i<p->installer.uninstall_count && ok;i++){
        ok = exec_action(&p->installer.uninstall_actions[i], &ctx, &cfg, p);
    }

    if(ok){ C(32); printf(":: "); C_RESET; printf(_( "アンインストール完了: %s\n" ),p->name); }
    else  { C(31); printf(":: "); C_RESET; printf(_( "アンインストールに失敗しました: %s\n" ),p->name); }
}

/* ---- config ---- */
static void cmd_config(int argc, char **argv) {
    Config cfg; load_config(&cfg);

    if(argc==0){
        C(1); printf(_( "現在の設定:\n" )); C_RESET;
        printf("  appDir      : %s\n", cfg.app_dir[0]?cfg.app_dir:_( "(未設定)" ));
        printf("  pluginsDir  : %s\n", cfg.plugins_dir[0]?cfg.plugins_dir:_( "(未設定)" ));
        printf("  scriptDir   : %s\n", cfg.script_dir[0]?cfg.script_dir:_( "(未設定)" ));
        char path[1024]; config_path(path,sizeof(path));
        printf(_( "  設定ファイル: %s\n" ), path);
        printf(_( "\n変更するには:\n  au2cat config --app-dir <パス> [--plugins-dir <パス>] [--script-dir <パス>]\n" ));
        return;
    }

    int i;
    for(i=0;i<argc;i++){
        if(!strcmp(argv[i],"--app-dir") && i+1<argc){ get_absolute_path(argv[++i], cfg.app_dir, sizeof(cfg.app_dir)); }
        else if(!strcmp(argv[i],"--plugins-dir") && i+1<argc){ get_absolute_path(argv[++i], cfg.plugins_dir, sizeof(cfg.plugins_dir)); }
        else if(!strcmp(argv[i],"--script-dir") && i+1<argc){ get_absolute_path(argv[++i], cfg.script_dir, sizeof(cfg.script_dir)); }
        else { fprintf(stderr,_( "au2cat: 不明なオプション '%s'\n" ),argv[i]); }
    }
    if(!cfg.plugins_dir[0] && cfg.app_dir[0]) snprintf(cfg.plugins_dir,sizeof(cfg.plugins_dir),"%s/Plugin",cfg.app_dir);
    if(!cfg.script_dir[0] && cfg.app_dir[0]) {
#ifdef _WIN32
        snprintf(cfg.script_dir,sizeof(cfg.script_dir),"C:\\ProgramData\\AviUtl2\\Script");
#else
        snprintf(cfg.script_dir,sizeof(cfg.script_dir),"%s/script",cfg.app_dir);
#endif
    }

    save_config(&cfg);
    C(32); printf(":: "); C_RESET; printf(_( "設定を保存しました。\n" ));
}

/* ---- stats ---- */
static void cmd_stats(void) {
    const char *types[64]; int tcnt[64]; int tn=0;
    long tot_pop=0; int deprecated=0;
    int i,j;

    for(i=0;i<g_count;i++){
        Package *p=&g_pkg[i];
        tot_pop+=p->popularity;
        if(p->deprecated) deprecated++;
        int found=0;
        for(j=0;j<tn;j++) if(!strcmp(types[j],p->type)){tcnt[j]++;found=1;break;}
        if(!found&&tn<64){types[tn]=p->type;tcnt[tn++]=1;}
    }

    for(i=0;i<tn-1;i++) for(j=i+1;j<tn;j++)
        if(tcnt[j]>tcnt[i]){
            int tmp=tcnt[i];tcnt[i]=tcnt[j];tcnt[j]=tmp;
            const char *ts=types[i];types[i]=types[j];types[j]=ts;
        }

    C(33); printf(":: "); C_RESET;
    printf(_( "AviUtl2 Catalog データベース統計\n\n" ));

    C(1); printf(_( "総パッケージ数  : " )); C_RESET; C(32); printf("%d\n",g_count); C_RESET;
    C(1); printf(_( "非推奨          : " )); C_RESET; C(31); printf("%d\n",deprecated); C_RESET;
    C(1); printf(_( "総人気度        : " )); C_RESET; printf("%ld\n",tot_pop);
    printf("\n");

    C(1); printf(_( "種類別:\n" )); C_RESET;
    int bar_max=tcnt[0]>0?tcnt[0]:1;
    for(i=0;i<tn;i++){
        int bar=(tcnt[i]*30+bar_max/2)/bar_max;
        C(33); print_cell(_(types[i]),14); C_RESET;
        printf(" ");
        C(36);
        int b; for(b=0;b<bar;b++) printf("█");
        C_RESET;
        printf(" %d\n",tcnt[i]);
    }
    printf("\n");

    int all_idx[MAX_PKG];
    for(i=0;i<g_count;i++) all_idx[i]=i;
    qsort(all_idx,g_count,sizeof(int),cmp_pop);
    int topn=(g_count<10?g_count:10);
    int top[10];
    for(i=0;i<topn;i++) top[i]=all_idx[i];

    C(1); printf(_( "人気トップ %d:\n" ),topn); C_RESET;
    for(i=0;i<topn;i++){
        Package *p=&g_pkg[top[i]];
        C(33); printf(" %2d. ",i+1); C_RESET;
        C(32); print_cell(p->name,36); C_RESET;
        C(35); printf("  %-12s",p->latest_version); C_RESET;
        printf(_( "  人気度: %ld\n" ),p->popularity);
    }
    printf("\n");
}

static void check_cli_update(void) {
    char *json = http_get("https://api.github.com/repos/Madotsukanai/aviutl2-catalog-cli/releases/latest");
    if(json) {
        char *p = strstr(json, "\"tag_name\"");
        if(p) {
            p = strchr(p, ':');
            if(p) {
                p++;
                p = (char*)skip_ws(p);
                char tag[64];
                if(parse_str(p, tag, sizeof(tag))) {
                    const char *v_tag = tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag;
                    const char *v_cur = AU2CAT_VERSION[0] == 'v' || AU2CAT_VERSION[0] == 'V' ? AU2CAT_VERSION + 1 : AU2CAT_VERSION;
                    if(strcmp(v_tag, v_cur) != 0) {
                        C(36); printf("\n:: "); C_RESET;
                        printf(_("au2cat 本体の新しいバージョンが利用可能です: %s (現在: %s)\n"), tag, AU2CAT_VERSION);
                        printf(_("   'au2cat upgrade' で本体を更新できます。\n\n"));
                        
                        char path[1024]; home_path(path, sizeof(path), "au2cat_cli_update.txt");
                        FILE *f = fopen(path, "w");
                        if(f) { fprintf(f, "%s\n", tag); fclose(f); }
                    } else {
                        char path[1024]; home_path(path, sizeof(path), "au2cat_cli_update.txt");
                        remove(path);
                    }
                }
            }
        }
        free(json);
    }
}

/* ---- update ---- */
static void cmd_update(void) {
    char path[1024]; home_path(path,sizeof(path),CACHE_FILE);
    remove(path);
    g_count=0;

    C(33); printf(":: "); C_RESET;
    printf(_( "データベースを更新しています...\n" ));
    char *json=http_get(INDEX_URL);
    if(!json){ fprintf(stderr,_( "au2cat: 更新に失敗しました\n" )); return; }
    save_cache(json);
    int ok=parse_catalog(json);
    free(json);
    if(!ok){ fprintf(stderr,_( "au2cat: パースエラー\n" )); return; }
    C(32); printf(":: "); C_RESET;
    printf(_( "更新完了  —  %d 件のパッケージ\n" ),g_count);
    
    check_cli_update();
}

/* ---- help ---- */
static void cmd_help(const char *prog) {
    C(1); C(36); printf("au2cat"); C_RESET;
    printf(" — AviUtl2 Catalog CLI\n\n");
    C(1); printf(_( "使い方:\n" )); C_RESET;
    printf(_( "  %s <コマンド> [オプション]\n\n" ),prog);
    C(1); printf(_( "コマンド:\n" )); C_RESET;
    printf("  "); C(32); printf("search"); C_RESET;    printf(_( "    <キーワード>    パッケージを検索 (名前 / 作者 / 概要 / タグ)\n" ));
    printf("  "); C(32); printf("list"); C_RESET;      printf(_( "      [種類]         パッケージ一覧 (種類で絞込可)\n" ));
    printf("  "); C(32); printf("show"); C_RESET;      printf(_( "      <ID|名前>      パッケージの詳細情報\n" ));
    printf("  "); C(32); printf("info"); C_RESET;      printf(_( "      <ID|名前>      show の別名\n" ));
    printf("  "); C(32); printf("install"); C_RESET;   printf(_( "   <ID|名前> [-y] パッケージをインストール\n" ));
    printf("  "); C(32); printf("uninstall"); C_RESET; printf(_( " <ID|名前> [-y] パッケージをアンインストール (remove/rm でも可)\n" ));
    printf("  "); C(32); printf("config"); C_RESET;    printf(_( "    [--app-dir <パス>] [--plugins-dir <パス>]\n" ));
    printf(_( "                            インストール先設定の表示/変更\n" ));
    printf("  "); C(32); printf("check"); C_RESET;     printf(_( "                    インストール済みパッケージを検出\n" ));
    printf("  "); C(32); printf("upgrade"); C_RESET;   printf(_( "                  更新可能なパッケージを一括更新\n" ));
    printf("  "); C(32); printf("commons"); C_RESET;   printf(_( "                  ニコニ・コモンズIDをまとめてコピー用に出力\n" ));
    printf("  "); C(32); printf("stats"); C_RESET;     printf(_( "                    データベース統計\n" ));
    printf("  "); C(32); printf("update"); C_RESET;    printf(_( "                    カタログを強制更新\n" ));
    printf("  "); C(32); printf("version"); C_RESET;   printf(_( "                   バージョン情報を表示\n" ));
    printf("  "); C(32); printf("help"); C_RESET;      printf(_( "                    このヘルプ\n" ));
    printf("\n");
    C(1); printf(_( "例:\n" )); C_RESET;
    printf("  %s search x264\n",prog);
    printf("  %s search rigaya\n",prog);
    printf(_( "  %s list スクリプト\n" ),prog);
    printf("  %s show x264guiEx\n",prog);
    printf("  %s install Mr-Ojii.L-SMASH-Works\n",prog);
    printf("  %s install x264guiEx -y\n",prog);
    printf("  %s uninstall x264guiEx\n",prog);
    printf("  %s config --app-dir \"C:\\\\AviUtl2\"\n",prog);
    printf("\n");
    C(1); printf(_( "インストール機能について:\n" )); C_RESET;
    printf(_( "  - zip展開・ファイルコピーは全OSで動作します。\n" ));
    printf(_( "  - exe実行 (run / run_auo_setup / extract_sfx) は Windows、または\n" ));
    printf(_( "    非Windows環境で wine が見つかった場合のみ動作します。\n" ));
    printf(_( "  - appDir / pluginsDir は初回 install/uninstall 時に質問されます。\n" ));
    printf(_( "    au2cat config で確認・変更できます。\n" ));
    printf("\n");
    C(1); printf(_( "データ元:\n" )); C_RESET;
    printf("  %s\n",INDEX_URL);
    printf("\n");
    C(1); printf(_( "キャッシュ:\n" )); C_RESET;
    char path[1024]; home_path(path,sizeof(path),CACHE_FILE);
    printf(_( "  %s  (TTL: %d 分)\n" ),path,CACHE_TTL/60);
    printf("\n");
}

/* ===== エントリーポイント ===== */
int main(int argc, char **argv) {
#ifdef _WIN32
SetConsoleOutputCP(65001);
SetConsoleCP(65001);
#endif
color_init();

const char *prog = argc > 0 ? argv[0] : "au2cat";

if (argc < 2) {
    cmd_help(prog);
    return 0;
}

const char *cmd = argv[1];

/* help は事前ロード不要 */
if (!strcmp(cmd, "help") ||
    !strcmp(cmd, "-h") ||
    !strcmp(cmd, "--help")) {

    cmd_help(prog);
    return 0;
}

if (!strcmp(cmd, "version") ||
    !strcmp(cmd, "-v") ||
    !strcmp(cmd, "--version")) {
    
    printf("au2cat %s\n", AU2CAT_VERSION);
    return 0;
}

/* config もカタログ不要 */
if (!strcmp(cmd, "config") || !strcmp(cmd, "--config")) {
    cmd_config(argc-2, argv+2);
    return 0;
}

/* update は強制再フェッチ */
if (!strcmp(cmd, "update") ||
    !strcmp(cmd, "-u") ||
    !strcmp(cmd, "--update")) {

    curl_global_init(CURL_GLOBAL_DEFAULT);
    cmd_update();
    curl_global_cleanup();
    return 0;
}

/* その他コマンドはカタログロード後に実行 */
curl_global_init(CURL_GLOBAL_DEFAULT);

if (!load_catalog(0)) {
    curl_global_cleanup();
    return 1;
}

printf("\n");

if (!strcmp(cmd, "search") ||
    !strcmp(cmd, "s") ||
    !strcmp(cmd, "-s") ||
    !strcmp(cmd, "--search")) {

    cmd_search(argc - 2, argv + 2);

} else if (!strcmp(cmd, "list") ||
           !strcmp(cmd, "ls") ||
           !strcmp(cmd, "-l") ||
           !strcmp(cmd, "--list")) {

    cmd_list(argc - 2, argv + 2);

} else if (!strcmp(cmd, "show") ||
           !strcmp(cmd, "info") ||
           !strcmp(cmd, "-i") ||
           !strcmp(cmd, "--info")) {

    cmd_show(argc - 2, argv + 2);

} else if (!strcmp(cmd, "check")) {
    cmd_check();
} else if (!strcmp(cmd, "upgrade")) {
    int ac = argc - 2; char **av = argv + 2;
    int yes = extract_yes_flag(&ac, av);
    cmd_upgrade(yes);
} else if (!strcmp(cmd, "commons")) {
    cmd_commons();
} else if (!strcmp(cmd, "install") ||
           !strcmp(cmd, "--install")) {

    int ac = argc - 2; char **av = argv + 2;
    int yes = extract_yes_flag(&ac, av);
    cmd_install(ac, av, yes);

} else if (!strcmp(cmd, "uninstall") ||
           !strcmp(cmd, "remove") ||
           !strcmp(cmd, "rm") ||
           !strcmp(cmd, "--uninstall")) {

    int ac = argc - 2; char **av = argv + 2;
    int yes = extract_yes_flag(&ac, av);
    cmd_uninstall(ac, av, yes);

} else if (!strcmp(cmd, "stats") ||
           !strcmp(cmd, "--stats")) {

    cmd_stats();

} else {

    fprintf(stderr,
            _( "au2cat: 不明なコマンドまたはオプション '%s'\n" ),
            cmd);
    fprintf(stderr,
            _( "  %s help でコマンド一覧を確認できます。\n" ),
            prog);

    curl_global_cleanup();
    return 1;
}

curl_global_cleanup();
return 0;
}
