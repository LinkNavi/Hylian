#include "../platform/platform.h"

static long   hy_strlen(const char *s) { long n=0; while(s[n]) n++; return n; }
static void  *hy_memcpy(void *d,const void *s,long n){ char*dd=d;const char*ss=s; while(n--)*dd++=*ss++; return d; }
static void  *hy_memset(void *d,int c,long n){ char*dd=d; while(n--)*dd++=c; return d; }
static char  *hy_strdup(const char *s){ long l=hy_strlen(s); char*o=(char*)hy_alloc(l+1); hy_memcpy(o,s,l+1); return o; }
static int    hy_isspace(char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'; }
static char   hy_toupper(char c){ return c>='a'&&c<='z'?c-32:c; }
static char   hy_tolower(char c){ return c>='A'&&c<='Z'?c+32:c; }
static char  *hy_strstr(const char *h,const char *n){
    long hl=hy_strlen(h),nl=hy_strlen(n);
    for(long i=0;i<=hl-nl;i++){
        long j=0; while(j<nl&&h[i+j]==n[j])j++;
        if(j==nl) return (char*)h+i;
    } return (void*)0;
}
static int    hy_strcmp(const char *a,const char *b){ while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
static int    hy_strncmp(const char *a,const char *b,long n){ while(n--&&*a&&*a==*b){a++;b++;} return n<0?0:(unsigned char)*a-(unsigned char)*b; }

long   hylian_length(char *s)                    { return hy_strlen(s); }
long   hylian_is_empty(char *s)                  { return !s||s[0]=='\0'; }
long   hylian_contains(char *s,char *n)          { return hy_strstr(s,n)!=(void*)0; }
long   hylian_starts_with(char *s,char *p)       { return hy_strncmp(s,p,hy_strlen(p))==0; }
long   hylian_ends_with(char *s,char *sf)        { long sl=hy_strlen(s),sfl=hy_strlen(sf); if(sfl>sl)return 0; return hy_strcmp(s+sl-sfl,sf)==0; }
long   hylian_index_of(char *s,char *n)          { char *p=hy_strstr(s,n); return p?(long)(p-s):-1; }
long   hylian_equals(char *a,char *b)            { return hy_strcmp(a,b)==0; }

char  *hylian_slice(char *s,long start,long end) {
    long len=hy_strlen(s);
    if(start<0)start=0; if(end>len)end=len; if(start>=end)return hy_strdup("");
    long sz=end-start; char *o=(char*)hy_alloc(sz+1); hy_memcpy(o,s+start,sz); o[sz]='\0'; return o;
}
char  *hylian_trim(char *s) {
    while(hy_isspace(*s))s++;
    if(!*s)return hy_strdup("");
    char *e=s+hy_strlen(s)-1; while(e>s&&hy_isspace(*e))e--;
    long l=e-s+1; char *o=(char*)hy_alloc(l+1); hy_memcpy(o,s,l); o[l]='\0'; return o;
}
char  *hylian_trim_start(char *s) { while(hy_isspace(*s))s++; return hy_strdup(s); }
char  *hylian_trim_end(char *s)   { char *o=hy_strdup(s); char *e=o+hy_strlen(o)-1; while(e>o&&hy_isspace(*e))e--; *(e+1)='\0'; return o; }
char  *hylian_to_upper(char *s)   { char *o=hy_strdup(s); for(char *p=o;*p;p++)*p=hy_toupper(*p); return o; }
char  *hylian_to_lower(char *s)   { char *o=hy_strdup(s); for(char *p=o;*p;p++)*p=hy_tolower(*p); return o; }
char  *hylian_replace(char *s,char *old,char *nw) {
    long sl=hy_strlen(s),ol=hy_strlen(old),nl=hy_strlen(nw);
    if(ol==0)return hy_strdup(s);
    long count=0; char *p=s;
    while((p=hy_strstr(p,old))){count++;p+=ol;}
    char *out=(char*)hy_alloc(sl+count*(nl-ol)+1);
    char *dst=out; p=s; char *m;
    while((m=hy_strstr(p,old))){
        long plen=m-p; hy_memcpy(dst,p,plen); dst+=plen;
        hy_memcpy(dst,nw,nl); dst+=nl; p=m+ol;
    }
    long tail=hy_strlen(p); hy_memcpy(dst,p,tail+1); return out;
}
char **hylian_split(char *s,char *delim) {
    long dl=hy_strlen(delim),count=1; char *p=s;
    while((p=hy_strstr(p,delim))){count++;p+=dl;}
    char **out=(char**)hy_alloc((count+1)*8);
    long i=0; p=s; char *m;
    while((m=hy_strstr(p,delim))){
        long l=m-p; out[i]=(char*)hy_alloc(l+1); hy_memcpy(out[i],p,l); out[i][l]='\0'; i++; p=m+dl;
    }
    out[i++]=hy_strdup(p); out[i]=(void*)0; return out;
}
char  *hylian_join(char **parts,long count,char *delim) {
    if(count==0)return hy_strdup("");
    long dl=hy_strlen(delim),total=0;
    for(long i=0;i<count;i++)total+=hy_strlen(parts[i]);
    total+=dl*(count-1);
    char *out=(char*)hy_alloc(total+1); char *dst=out;
    for(long i=0;i<count;i++){
        long l=hy_strlen(parts[i]); hy_memcpy(dst,parts[i],l); dst+=l;
        if(i<count-1){hy_memcpy(dst,delim,dl);dst+=dl;}
    } *dst='\0'; return out;
}
long   hylian_to_int(char *s,long *out)   { char *e=s; long r=0,sign=1; if(*e=='-'){sign=-1;e++;} while(*e>='0'&&*e<='9')r=r*10+(*e++-'0'); *out=sign*r; return e!=s; }
long   hylian_to_float(char *s,double *o) { (void)s;(void)o; return 0; } // stub, needs soft float
char  *hylian_from_int(long n)            { char buf[24]; long l=hylian_length(""); (void)l; char tmp[24]; int tl=0,neg=0; unsigned long u; if(n<0){neg=1;u=(unsigned long)(-(n+1))+1u;}else u=(unsigned long)n; if(u==0)tmp[tl++]='0'; else while(u>0){tmp[tl++]=(char)('0'+(int)(u%10));u/=10;} if(neg)tmp[tl++]='-'; char *o=(char*)hy_alloc(tl+1); for(int i=0;i<tl;i++)o[i]=tmp[tl-1-i]; o[tl]='\0'; return o; }
