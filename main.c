/*
 * au2cat — AviUtl2 Catalog CLI
 * パッケージマネージャー風 CLI (apt / pacman スタイル)
 *
 * 使い方:
 *   au2cat search   <キーワード>          パッケージを検索
 *   au2cat list     [種類]                パッケージ一覧
 *   au2cat show     <id|名前>             パッケージ詳細
 *   au2cat info     <id|名前>             show の別名
 *   au2cat stats                          統計情報
 *   au2cat update                         カタログ更新確認
 *   au2cat help                           このヘルプ
 *
 * ビルド:
 *   Linux/macOS : gcc -O2 -o au2cat main.c -lcurl -lm
 *   Windows     : gcc -O2 -o au2cat.exe main.c -lcurl -lws2_32 -lm
 *
 * 依存: libcurl のみ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <curl/curl.h>

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  define PATH_SEP      '\\'
#  define HOME_ENV      "USERPROFILE"
#  define MKDIR(p)      _mkdir(p)
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  define PATH_SEP      '/'
#  define HOME_ENV      "HOME"
#  define MKDIR(p)      mkdir((p), 0755)
#endif

/* ===== 定数 ===== */
#define INDEX_URL   "https://raw.githubusercontent.com/Neosku/aviutl2-catalog-data/main/index.json"
#define CACHE_FILE  "au2cat_cache.json"   /* ホームディレクトリ内 */
#define CACHE_TTL   1800                  /* 30分 (秒) */
#define MAX_PKG     512
#define MAX_STR     512
#define MAX_TAGS    16
#define MAX_DEPS    32

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
    CURLcode r=curl_easy_perform(c);
    curl_easy_cleanup(c);
    if(r!=CURLE_OK){ fprintf(stderr,"au2cat: ダウンロードエラー: %s\n",curl_easy_strerror(r)); free(b.data); return NULL; }
    return b.data;
}

/* キャッシュパス: ~/au2cat_cache.json */
static void cache_path(char *buf, int sz) {
    const char *home=getenv(HOME_ENV);
    if(home) snprintf(buf,sz,"%s%c%s",home,PATH_SEP,CACHE_FILE);
    else      snprintf(buf,sz,"%s",CACHE_FILE);
}

static char *load_cache(void) {
    char path[1024]; cache_path(path,sizeof(path));
    FILE *f=fopen(path,"rb"); if(!f)return NULL;

    /* TTLチェック: ファイルの mtime */
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
        /* FILETIME: 100ns intervals since 1601; convert to Unix epoch */
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
    char path[1024]; cache_path(path,sizeof(path));
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
        C(33); printf(":: "); C_RESET; printf("カタログを取得しています... (%s)\n", INDEX_URL);
        fflush(stdout);
        json=http_get(INDEX_URL);
        if(!json){ fprintf(stderr,"au2cat: 取得に失敗しました\n"); return 0; }
        save_cache(json);
    }

    int ok=parse_catalog(json);
    free(json);
    if(!ok){ fprintf(stderr,"au2cat: パースに失敗しました\n"); return 0; }

    if(from_cache){
        C(32); printf(":: "); C_RESET;
        printf("キャッシュから読み込みました (%d 件)\n", g_count);
    } else {
        C(32); printf(":: "); C_RESET;
        printf("データベースを更新しました (%d 件のパッケージ)\n", g_count);
    }
    return 1;
}

/* ===== 文字列ユーティリティ ===== */
/* ASCII 大文字小文字を無視した完全一致 (strcasecmp の代替・C99 標準のみ使用) */
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
    /* ASCII 大文字小文字を無視して部分一致 (日本語はバイト一致) */
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

/* UTF-8 バイト列を max_bytes 以内で切り詰め、残りを空白で埋めて幅を揃える */
static void print_cell(const char *s, int col_bytes) {
    /* 表示幅カウント: ASCII=1, 多バイト=2 (CJK概算) */
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
    /* 残りスペース埋め */
    while(display_width<limit){ putchar(' '); display_width++; }
}

/* ===== ソート用比較 ===== */
static int cmp_pop(const void*a,const void*b){ return (int)(g_pkg[*(int*)b].popularity-g_pkg[*(int*)a].popularity); }
static int cmp_name(const void*a,const void*b){ return strcmp(g_pkg[*(int*)a].name,g_pkg[*(int*)b].name); }

/* ===== サブコマンド実装 ===== */

/* ---- search ---- */
static void cmd_search(int argc, char **argv) {
    /* 全引数をスペース結合してクエリに */
    char query[MAX_STR]="";
    int i;
    for(i=0;i<argc;i++){
        if(i) strncat(query," ",MAX_STR-strlen(query)-1);
        strncat(query,argv[i],MAX_STR-strlen(query)-1);
    }

    if(!query[0]){
        fprintf(stderr,"使い方: au2cat search <キーワード>\n");
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
    printf("検索結果: \""); C(1); printf("%s",query); C_RESET; printf("\"  %d 件\n\n",cnt);

    if(cnt==0){
        printf("  該当するパッケージが見つかりませんでした。\n");
        return;
    }

    /* ヘッダ */
    C(1);
    printf("%-40s  %-14s  %-14s  %s\n","名前","作者","種類","バージョン");
    C_RESET;
    printf("%-40s  %-14s  %-14s  %s\n",
        "----------------------------------------",
        "--------------","--------------","----------");

    for(i=0;i<cnt;i++){
        Package *p=&g_pkg[idx[i]];
        if(p->deprecated){ C(31); printf("(非推奨) "); C_RESET; print_cell(p->name,31); }
        else              { C(32); print_cell(p->name,40); C_RESET; }
        printf("  ");
        C(33); print_cell(p->author,14); C_RESET;
        printf("  ");
        C(36); print_cell(p->type,14); C_RESET;
        printf("  ");
        C(35); printf("%s",p->latest_version); C_RESET;
        printf("\n");
    }
    printf("\n");
    C(33); printf(":: "); C_RESET;
    printf("詳細は  au2cat show <名前 または ID>  で確認できます。\n");
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
    if(type_filter[0]) printf("パッケージ一覧 [種類: %s]  %d 件\n\n",type_filter,cnt);
    else               printf("パッケージ一覧 (全種類)  %d 件\n\n",cnt);

    C(1);
    printf("%-40s  %-14s  %-14s  %s\n","名前","作者","種類","バージョン");
    C_RESET;
    printf("%-40s  %-14s  %-14s  %s\n",
        "----------------------------------------",
        "--------------","--------------","----------");

    for(i=0;i<cnt;i++){
        Package *p=&g_pkg[idx[i]];
        if(p->deprecated){ C(31); print_cell(p->name,40); C_RESET; }
        else              { C(32); print_cell(p->name,40); C_RESET; }
        printf("  ");
        C(33); print_cell(p->author,14); C_RESET;
        printf("  ");
        C(36); print_cell(p->type,14); C_RESET;
        printf("  ");
        C(35); printf("%s",p->latest_version); C_RESET;
        printf("\n");
    }
}

/* ---- show / info ---- */
static void cmd_show(int argc, char **argv) {
    if(argc==0){ fprintf(stderr,"使い方: au2cat show <ID または 名前>\n"); return; }

    /* 複数引数をスペース結合 */
    char query[MAX_STR]="";
    int i;
    for(i=0;i<argc;i++){
        if(i) strncat(query," ",MAX_STR-strlen(query)-1);
        strncat(query,argv[i],MAX_STR-strlen(query)-1);
    }

    /* ID完全一致 → 名前完全一致 → 名前部分一致 */
    Package *found=NULL;
    for(i=0;i<g_count;i++) if(!strcmp(g_pkg[i].id,query)){found=&g_pkg[i];break;}
    if(!found) for(i=0;i<g_count;i++) if(str_iequal(g_pkg[i].name,query)){found=&g_pkg[i];break;}
    if(!found){
        Package *candidates[MAX_PKG]; int cn=0;
        for(i=0;i<g_count;i++)
            if(str_icontains(g_pkg[i].name,query)||str_icontains(g_pkg[i].id,query))
                candidates[cn++]=&g_pkg[i];
        if(cn==1){ found=candidates[0]; }
        else if(cn>1){
            fprintf(stderr,"au2cat: \"%s\" は複数のパッケージに該当します:\n",query);
            for(i=0;i<cn;i++) fprintf(stderr,"  %s (%s)\n",candidates[i]->name,candidates[i]->id);
            fprintf(stderr,"ID を指定してください。\n");
            return;
        }
    }

    if(!found){ fprintf(stderr,"au2cat: パッケージ \"%s\" が見つかりません。\n",query); return; }

    Package *p=found;
    printf("\n");

    /* パッケージ名 */
    if(p->deprecated){ C(31); C(1); printf("%s",p->name); C_RESET; printf("  "); C(31); printf("[非推奨]"); C_RESET; }
    else              { C(32); C(1); printf("%s",p->name); C_RESET; }
    printf("\n");

    /* 概要 */
    printf("%s\n",p->summary);
    printf("\n");

    /* メタ情報 */
    C(1); printf("%-16s","ID"); C_RESET; printf(": %s\n",p->id);
    C(1); printf("%-16s","バージョン"); C_RESET; printf(": "); C(35); printf("%s",p->latest_version); C_RESET; printf("\n");
    C(1); printf("%-16s","種類"); C_RESET; printf(": %s\n",p->type);
    C(1); printf("%-16s","作者"); C_RESET; printf(": %s\n",p->author);
    C(1); printf("%-16s","人気度"); C_RESET; printf(": %ld  トレンド: %ld\n",p->popularity,p->trend);

    if(p->repo_url[0]){
        C(1); printf("%-16s","URL"); C_RESET; printf(": "); C(34); printf("%s",p->repo_url); C_RESET; printf("\n");
    }

    if(p->tag_count>0){
        int t;
        C(1); printf("%-16s","タグ"); C_RESET; printf(":");
        for(t=0;t<p->tag_count;t++){ printf(" "); C(35); printf("%s",p->tags[t]); C_RESET; }
        printf("\n");
    }

    if(p->dep_count>0){
        int d;
        C(1); printf("%-16s","依存"); C_RESET; printf(":");
        for(d=0;d<p->dep_count;d++){ printf(" %s",p->deps[d]); if(d<p->dep_count-1)printf(","); }
        printf("\n");
    }
    printf("\n");
}

/* ---- stats ---- */
static void cmd_stats(void) {
    /* 種類集計 */
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

    /* 種類を件数降順にソート (バブル) */
    for(i=0;i<tn-1;i++) for(j=i+1;j<tn;j++)
        if(tcnt[j]>tcnt[i]){
            int tmp=tcnt[i];tcnt[i]=tcnt[j];tcnt[j]=tmp;
            const char *ts=types[i];types[i]=types[j];types[j]=ts;
        }

    C(33); printf(":: "); C_RESET;
    printf("AviUtl2 Catalog データベース統計\n\n");

    C(1); printf("総パッケージ数  : "); C_RESET; C(32); printf("%d\n",g_count); C_RESET;
    C(1); printf("非推奨          : "); C_RESET; C(31); printf("%d\n",deprecated); C_RESET;
    C(1); printf("総人気度        : "); C_RESET; printf("%ld\n",tot_pop);
    printf("\n");

    /* 種類別バー */
    C(1); printf("種類別:\n"); C_RESET;
    int bar_max=tcnt[0]>0?tcnt[0]:1;
    for(i=0;i<tn;i++){
        int bar=(tcnt[i]*30+bar_max/2)/bar_max;
        C(33); print_cell(types[i],14); C_RESET;
        printf(" ");
        C(36);
        int b; for(b=0;b<bar;b++) printf("█");
        C_RESET;
        printf(" %d\n",tcnt[i]);
    }
    printf("\n");

    /* 人気 Top10 */
    int all_idx[MAX_PKG];
    for(i=0;i<g_count;i++) all_idx[i]=i;
    qsort(all_idx,g_count,sizeof(int),cmp_pop);
    int topn=(g_count<10?g_count:10);
    int top[10];
    for(i=0;i<topn;i++) top[i]=all_idx[i];

    C(1); printf("人気トップ %d:\n",topn); C_RESET;
    for(i=0;i<topn;i++){
        Package *p=&g_pkg[top[i]];
        C(33); printf(" %2d. ",i+1); C_RESET;
        C(32); print_cell(p->name,36); C_RESET;
        C(35); printf("  %-12s",p->latest_version); C_RESET;
        printf("  人気度: %ld\n",p->popularity);
    }
    printf("\n");
}

/* ---- update ---- */
static void cmd_update(void) {
    /* キャッシュ削除して再取得 */
    char path[1024]; cache_path(path,sizeof(path));
    remove(path);
    g_count=0;

    C(33); printf(":: "); C_RESET;
    printf("データベースを更新しています...\n");
    char *json=http_get(INDEX_URL);
    if(!json){ fprintf(stderr,"au2cat: 更新に失敗しました\n"); return; }
    save_cache(json);
    int ok=parse_catalog(json);
    free(json);
    if(!ok){ fprintf(stderr,"au2cat: パースエラー\n"); return; }
    C(32); printf(":: "); C_RESET;
    printf("更新完了  —  %d 件のパッケージ\n",g_count);
}

/* ---- help ---- */
static void cmd_help(const char *prog) {
    C(1); C(36); printf("au2cat"); C_RESET;
    printf(" — AviUtl2 Catalog CLI\n\n");
    C(1); printf("使い方:\n"); C_RESET;
    printf("  %s <コマンド> [オプション]\n\n",prog);
    C(1); printf("コマンド:\n"); C_RESET;
    printf("  "); C(32); printf("search"); C_RESET;  printf("  <キーワード>    パッケージを検索 (名前 / 作者 / 概要 / タグ)\n");
    printf("  "); C(32); printf("list"); C_RESET;    printf("    [種類]         パッケージ一覧 (種類で絞込可)\n");
    printf("  "); C(32); printf("show"); C_RESET;    printf("    <ID|名前>      パッケージの詳細情報\n");
    printf("  "); C(32); printf("info"); C_RESET;    printf("    <ID|名前>      show の別名\n");
    printf("  "); C(32); printf("stats"); C_RESET;   printf("                  データベース統計\n");
    printf("  "); C(32); printf("update"); C_RESET;  printf("                  カタログを強制更新\n");
    printf("  "); C(32); printf("help"); C_RESET;    printf("                  このヘルプ\n");
    printf("\n");
    C(1); printf("例:\n"); C_RESET;
    printf("  %s search x264\n",prog);
    printf("  %s search rigaya\n",prog);
    printf("  %s list スクリプト\n",prog);
    printf("  %s show x264guiEx\n",prog);
    printf("  %s show Mr-Ojii.L-SMASH-Works\n",prog);
    printf("  %s update\n",prog);
    printf("\n");
    C(1); printf("データ元:\n"); C_RESET;
    printf("  %s\n",INDEX_URL);
    printf("\n");
    C(1); printf("キャッシュ:\n"); C_RESET;
    char path[1024]; cache_path(path,sizeof(path));
    printf("  %s  (TTL: %d 分)\n",path,CACHE_TTL/60);
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

/* サブコマンド / オプション取得 */
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

} else if (!strcmp(cmd, "stats") ||
           !strcmp(cmd, "--stats")) {

    cmd_stats();

} else {

    fprintf(stderr,
            "au2cat: 不明なコマンドまたはオプション '%s'\n",
            cmd);
    fprintf(stderr,
            "  %s help でコマンド一覧を確認できます。\n",
            prog);

    curl_global_cleanup();
    return 1;
}

curl_global_cleanup();
return 0;
}
