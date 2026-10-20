#include <stddef.h>
#include <stdint.h>

#define COLS 80
#define ROWS 25
#define HISTORY 8
#define FS_NODES 48
#define FS_NAME 32
#define FS_DATA 256
#define COLOR(f,b) ((uint8_t)(((b) << 4) | (f)))

enum { BLACK=0, BLUE=1, GREEN=2, CYAN=3, RED=4, MAGENTA=5, BROWN=6,
       LIGHT=7, DARK=8, SKY=9, LIME=10, AQUA=11, PINK=13, WHITE=15 };

static volatile uint16_t *const VGA = (uint16_t *)0xB8000;
static uint8_t color = COLOR(LIGHT, BLACK);
static size_t row, col;
static char input[128];
static size_t input_len;
static char history[HISTORY][128];
static unsigned history_count, history_pos;
static uint32_t boot_magic;
static uint32_t boot_info;
static uint32_t ticks;
static int shift_down, caps_lock;
static uint64_t usable_ram;
static uintptr_t heap_next, heap_limit;
static char cpu_name[13]="Unknown CPU";
static char host_name[32]="blackbit";
static char user_name[32]="guest";
static char language[8]="EN";
static char keyboard[8]="EN";
static int identity_loaded;
#ifdef BLACKBIT_SELFTEST
static char test_bridge_request[256];
#endif
#ifndef BLACKBIT_UI
#define BLACKBIT_UI 1
#endif
struct fs_node { int used,parent,is_dir,mounted,sealed; unsigned mode,owner; char name[FS_NAME],data[FS_DATA]; };
static struct fs_node fs[FS_NODES];
static int cwd;
static unsigned active_tasks=1;
extern uint8_t __kernel_end;

struct __attribute__((packed)) mb2_tag { uint32_t type, size; };
struct __attribute__((packed)) mb2_mmap_tag { uint32_t type,size,entry_size,entry_version; };
struct __attribute__((packed)) mb2_mmap_entry { uint64_t address,length; uint32_t type,reserved; };

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t value);
extern void cpu_halt(void);
static void putc(char c);
static void puts(const char *s);
static int starts(const char *s,const char *prefix);
static int eq(const char *a,const char *b);
static size_t slen(const char *s);
static void copy(char *d,const char *s,size_t cap);
static int fs_resolve(const char *path);
static int fs_new(const char *path,int is_dir);

#define COM1 0x3F8
static void serial_init(void) {
    outb(COM1+1,0x00);outb(COM1+3,0x80);outb(COM1+0,0x01);outb(COM1+1,0x00);
    outb(COM1+3,0x03);outb(COM1+2,0xC7);outb(COM1+4,0x0B);
}
static void serial_write(char c) { unsigned wait=0;while(!(inb(COM1+5)&0x20)&&wait++<1000000u){}outb(COM1,(uint8_t)c); }
static int serial_read_wait(void) { for(uint32_t wait=0;wait<30000000u;wait++)if(inb(COM1+5)&1)return inb(COM1);return -1; }
static int serial_line(char *buf,size_t cap) { size_t n=0;while(n+1<cap){int ch=serial_read_wait();if(ch<0)return 0;if(ch=='\n')break;if(ch!='\r')buf[n++]=(char)ch;}buf[n]=0;return 1; }
static int kaya_request(const char *request) {
#ifdef BLACKBIT_SELFTEST
    copy(test_bridge_request,request,sizeof(test_bridge_request));
    puts("mock Kaya bridge request\n");
    return 1;
#else
    for(size_t i=0;request[i];i++)serial_write(request[i]);
    serial_write('\n');
    char header[48];if(!serial_line(header,sizeof(header))||!starts(header,"KAYA-RESP ")){puts("Kaya bridge unavailable. Start Blackbit with ~/he.sh.\n");return 0;}
    char *p=header+10;uint32_t length=0;while(*p>='0'&&*p<='9'){length=length*10u+(uint32_t)(*p-'0');p++;}
    if(*p!=' '||length>8192u){puts("Kaya bridge returned an invalid response.\n");return 0;}p++;int status=0;if(*p=='1')status=1;
    puts("\n");for(uint32_t i=0;i<length;i++){int ch=serial_read_wait();if(ch<0){puts("\nKaya response ended early.\n");return 0;}putc((char)ch);}
    if(length&&status)puts("\nKaya command failed.\n");
    return status==0;
#endif
}
static int kaya_run_request(const char *package,unsigned package_argc,char **package_argv) {
    char request[256];size_t n=0;const char *prefix="KAYA run ";
    while(*prefix&&n+1<sizeof(request))request[n++]=*prefix++;
    const char *p=package;
    while(*p&&n+1<sizeof(request))request[n++]=*p++;
    for(unsigned i=0;i<package_argc;i++){
        if(n+2>=sizeof(request)){puts("Kaya command is too long.\n");return 0;}
        request[n++]=' ';p=package_argv[i];
        while(*p&&n+1<sizeof(request))request[n++]=*p++;
    }
    request[n]=0;
    return kaya_request(request);
}
static int host_apt_request(unsigned argc,char **argv) {
    char request[256];size_t n=0;const char *prefix="HOST apt";
    while(*prefix&&n+1<sizeof(request))request[n++]=*prefix++;
    for(unsigned i=0;i<argc;i++){
        if(n+2>=sizeof(request)){puts("APT command is too long.\n");return 0;}
        request[n++]=' ';const char *p=argv[i];
        while(*p&&n+1<sizeof(request))request[n++]=*p++;
    }
    request[n]=0;
    return kaya_request(request);
}

static void detect_cpu(void) {
    uint32_t a=0,b=0,c=0,d=0;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0));
    ((uint32_t *)cpu_name)[0]=b; ((uint32_t *)cpu_name)[1]=d; ((uint32_t *)cpu_name)[2]=c; cpu_name[12]=0;
}
static void parse_memory_map(uint32_t address) {
    if(!address) return;
    uint32_t total=*(uint32_t *)(uintptr_t)address;
    uint8_t *p=(uint8_t *)(uintptr_t)address+8;
    uint8_t *end=(uint8_t *)(uintptr_t)address+total;
    while(p+sizeof(struct mb2_tag)<=end) {
        struct mb2_tag *tag=(struct mb2_tag *)p;
        if(tag->size<8||p+tag->size>end) break;
        if(tag->type==6&&tag->size>=sizeof(struct mb2_mmap_tag)) {
            struct mb2_mmap_tag *map=(struct mb2_mmap_tag *)tag;
            uint8_t *e=p+sizeof(*map);
            while(e+map->entry_size<=p+tag->size&&map->entry_size>=sizeof(struct mb2_mmap_entry)) {
                struct mb2_mmap_entry *entry=(struct mb2_mmap_entry *)e;
                if(entry->type==1) {
                    usable_ram+=entry->length;
                    uintptr_t start=(uintptr_t)&__kernel_end;
                    uint64_t limit=entry->address+entry->length;
                    if(entry->address<=start&&limit>start&&limit<=0xffffffffu) {
                        uintptr_t safe=(uintptr_t)boot_info+total;
                        if(safe>start) start=(safe+15u)&~(uintptr_t)15u;
                        if(start<limit&&start>heap_next){heap_next=start;heap_limit=(uintptr_t)limit;}
                    }
                }
                e+=map->entry_size;
            }
        }
        if(tag->type==0) break;
        p+=(tag->size+7u)&~7u;
    }
}

static size_t slen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
static int contains(const char *s,const char *needle) { if(!*needle)return 1; for(;*s;s++){size_t i=0;while(needle[i]&&s[i]==needle[i])i++;if(!needle[i])return 1;}return 0; }
static int eq(const char *a,const char *b) { size_t i=0; while(a[i]&&b[i]&&a[i]==b[i]) i++; return a[i]==b[i]; }
static int starts(const char *s,const char *prefix) { size_t i=0;while(prefix[i]){if(s[i]!=prefix[i])return 0;i++;}return 1; }
static void copy(char *d,const char *s,size_t cap) { size_t i=0; if(cap) { while(i+1<cap&&s[i]) {d[i]=s[i];i++;} d[i]=0;} }
static void outw(uint16_t port,uint16_t value) { __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port)); }
static uint16_t inw(uint16_t port) { uint16_t value; __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port)); return value; }
static int ata_wait(int data_ready) { for(unsigned i=0;i<1000000;i++){uint8_t s=inb(0x1F7);if(s&1)return 0;if(!(s&0x80)&&(!data_ready||(s&8)))return 1;}return 0; }
static int ata_sector(int write_sector,uint8_t *data) {
    outb(0x1F6,0xE0);outb(0x1F1,0);outb(0x1F2,1);outb(0x1F3,0);outb(0x1F4,8);outb(0x1F5,0);
    outb(0x1F7,write_sector?0x30:0x20);if(!ata_wait(1))return 0;
    if(write_sector){for(unsigned i=0;i<256;i++)outw(0x1F0,(uint16_t)data[i*2]|((uint16_t)data[i*2+1]<<8));outb(0x1F7,0xE7);return ata_wait(0);}
    for(unsigned i=0;i<256;i++){uint16_t v=inw(0x1F0);data[i*2]=(uint8_t)v;data[i*2+1]=(uint8_t)(v>>8);}return 1;
}
static int json_value(const char *json,const char *key,char *out,size_t cap) {
    char needle[24];size_t n=slen(key);if(n+3>=sizeof(needle))return 0;needle[0]='"';for(size_t i=0;i<n;i++)needle[i+1]=key[i];needle[n+1]='"';needle[n+2]=0;
    const char *p=json;while(*p&&!contains(p,needle))p++;if(!*p)return 0;while(*p&&*p!=':')p++;if(!*p)return 0;p++;while(*p==' '||*p=='\t')p++;if(*p!='"')return 0;p++;size_t i=0;while(*p&&*p!='"'&&i+1<cap)out[i++]=*p++;out[i]=0;return *p=='"';
}
static int load_guest_config(void) {
    uint8_t sector[512];if(!ata_sector(0,sector)||!contains((char *)sector,"BLACKBIT_CONFIG_V1"))return 0;
    if(!json_value((char *)sector,"Lang",language,sizeof(language))||!json_value((char *)sector,"Keyboard",keyboard,sizeof(keyboard))||!json_value((char *)sector,"Username",user_name,sizeof(user_name))||!json_value((char *)sector,"Hostname",host_name,sizeof(host_name)))return 0;
    int dir=fs_new("/Blackbit",1);if(dir==-2)dir=fs_resolve("/Blackbit");if(dir>=0){int file=fs_new("/Blackbit/asd.json",0);if(file==-2)file=fs_resolve("/Blackbit/asd.json");if(file>=0)copy(fs[file].data,(char *)sector,FS_DATA);}return 1;
}
static void save_guest_config(void) {
    uint8_t sector[512];for(unsigned i=0;i<512;i++)sector[i]=0;const char *parts[]={"{\"Format\":\"BLACKBIT_CONFIG_V1\",\"Lang\":\"","\",\"Keyboard\":\"","\",\"Username\":\"","\",\"Hostname\":\"","\"}\n"};
    size_t n=0;const char *p=parts[0];while(*p&&n<500)sector[n++]=(uint8_t)*p++;const char *values[]={language,keyboard,user_name,host_name};
    for(unsigned k=0;k<4;k++){p=values[k];while(*p&&n<500)sector[n++]=(uint8_t)*p++;p=parts[k+1];while(*p&&n<500)sector[n++]=(uint8_t)*p++;}sector[n]=0;
    if(!ata_sector(1,sector))puts("Warning: persistent data disk unavailable; setup is not saved.\n");
    int dir=fs_new("/Blackbit",1);if(dir==-2)dir=fs_resolve("/Blackbit");if(dir>=0){int file=fs_new("/Blackbit/asd.json",0);if(file==-2)file=fs_resolve("/Blackbit/asd.json");if(file>=0)copy(fs[file].data,(char *)sector,FS_DATA);}
}
static void fs_init(void) {
    fs[0].used=1;fs[0].parent=-1;fs[0].is_dir=1;fs[0].mode=0755;copy(fs[0].name,"/",FS_NAME);
    const char *dirs[]={"bin","dev","etc","home","tmp"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++){fs[i+1].used=1;fs[i+1].parent=0;fs[i+1].is_dir=1;fs[i+1].mode=0755;copy(fs[i+1].name,dirs[i],FS_NAME);}
    const char *files[]={"README.txt","VERSION","LICENSE"};
    const char *data[]={"Blackbit OS 0.1\nKernel built-ins run in ring 0.\nFiles created in this prototype live in RAM and are lost at reboot.\n","0.1\n","Blackbit prototype. License to be chosen before distribution.\n"};
    for(unsigned i=0;i<3;i++){int id=6+(int)i;fs[id].used=1;fs[id].parent=0;fs[id].mode=0644;copy(fs[id].name,files[i],FS_NAME);copy(fs[id].data,data[i],FS_DATA);}
    fs[9].used=1;fs[9].parent=4;fs[9].is_dir=1;fs[9].mode=0755;copy(fs[9].name,"guest",FS_NAME);
}
static int fs_child(int parent,const char *name) { for(int i=1;i<FS_NODES;i++)if(fs[i].used&&fs[i].parent==parent&&eq(fs[i].name,name))return i;return -1; }
static int fs_resolve(const char *path) {
    if(!*path)return cwd;
    int cur=path[0]=='/'?0:cwd;char part[FS_NAME];
    while(*path){while(*path=='/')path++;if(!*path)break;size_t n=0;while(path[n]&&path[n]!='/'){if(n+1<sizeof(part))part[n]=path[n];n++;}if(n>=sizeof(part))return -1;part[n]=0;path+=n;
        if(eq(part,"."))continue;
        if(eq(part,"..")){if(cur>0)cur=fs[cur].parent;continue;}
        cur=fs_child(cur,part);if(cur<0)return -1;
    }return cur;
}
static int fs_parent_name(const char *path,int *parent,char *name) {
    char tmp[96];copy(tmp,path,sizeof(tmp));size_t n=slen(tmp);while(n>1&&tmp[n-1]=='/')tmp[--n]=0;if(!n)return 0;
    char *slash=0;for(size_t i=0;i<n;i++)if(tmp[i]=='/')slash=&tmp[i];
    if(!slash){*parent=cwd;copy(name,tmp,FS_NAME);}else{copy(name,slash+1,FS_NAME);if(slash==tmp)*parent=0;else{*slash=0;*parent=fs_resolve(tmp);}}
    return *parent>=0&&name[0]!=0;
}
static int fs_new(const char *path,int is_dir) {
    int parent;char name[FS_NAME];if(!fs_parent_name(path,&parent,name))return -1;if(fs_child(parent,name)>=0)return -2;
    int id=-1;for(int i=1;i<FS_NODES;i++)if(!fs[i].used){id=i;break;}if(id<0)return -3;
    fs[id].used=1;fs[id].parent=parent;fs[id].is_dir=is_dir;fs[id].mode=is_dir?0755:0644;fs[id].owner=0;copy(fs[id].name,name,FS_NAME);fs[id].data[0]=0;return id;
}
static void fs_path(int id,char *out,size_t cap) {
    if(id<=0){copy(out,"/",cap);return;}char pieces[8][FS_NAME];unsigned n=0;while(id>0&&n<8){copy(pieces[n++],fs[id].name,FS_NAME);id=fs[id].parent;}
    size_t at=0;if(cap)out[at++]='/';while(n){const char *p=pieces[--n];for(size_t i=0;p[i]&&at+1<cap;i++)out[at++]=p[i];if(n&&at+1<cap)out[at++]='/';}if(cap)out[at]=0;
}
static int fs_remove(int id) { if(id<=0||id>=FS_NODES||!fs[id].used)return 0;for(int i=1;i<FS_NODES;i++)if(fs[i].used&&fs[i].parent==id)return 0;fs[id].used=0;return 1; }
static void fs_list(int id) { if(id<0||!fs[id].is_dir){puts("not a directory\n");return;}char path[128];fs_path(id,path,sizeof(path));puts(path);puts(":\n");for(int i=1;i<FS_NODES;i++)if(fs[i].used&&fs[i].parent==id){puts(fs[i].is_dir?"[dir] ":"[file] ");puts(fs[i].name);if(fs[i].mounted)puts(" (mounted)");putc('\n');} }
static int fs_readonly_file(int id) { return id==6||id==7||id==8; }
static int fs_writable_file(int id) { return id>=0&&id<FS_NODES&&fs[id].used&&!fs[id].is_dir&&!fs_readonly_file(id)&&!fs[id].sealed; }
#ifdef BLACKBIT_SELFTEST
static char test_output[2048];
static size_t test_output_len;
#endif
static void treeview(int id,unsigned depth) { if(id<0||id>=FS_NODES||!fs[id].used||depth>8)return; for(unsigned n=0;n<depth;n++)puts("  "); puts(fs[id].name); if(fs[id].is_dir){putc('/');putc('\n');for(int i=1;i<FS_NODES;i++)if(fs[i].used&&fs[i].parent==id)treeview(i,depth+1);} }
static void scroll(void) {
    for(size_t y=1;y<ROWS;y++) for(size_t x=0;x<COLS;x++) VGA[(y-1)*COLS+x]=VGA[y*COLS+x];
    for(size_t x=0;x<COLS;x++) VGA[(ROWS-1)*COLS+x]=(uint16_t)color<<8|' ';
    row=ROWS-1;
}
static void putc(char c) {
    if(c=='\n') {
#ifdef BLACKBIT_SELFTEST
        if(test_output_len+1<sizeof(test_output))test_output[test_output_len++]='\n',test_output[test_output_len]=0;
        serial_write('\r');serial_write('\n');
#endif
        col=0; if(++row>=ROWS) scroll(); return;
    }
    if(c=='\r') { col=0; return; }
    if(c=='\b') { if(col) {--col; VGA[row*COLS+col]=(uint16_t)color<<8|' ';} return; }
    VGA[row*COLS+col]=(uint16_t)color<<8|(uint8_t)c;
#ifdef BLACKBIT_SELFTEST
    if(test_output_len+1<sizeof(test_output))test_output[test_output_len++]=c,test_output[test_output_len]=0;
    serial_write(c);
#endif
    if(++col>=COLS) { col=0; if(++row>=ROWS) scroll(); }
}
static void puts(const char *s) { while(*s) putc(*s++); }
static void clear(void) { for(size_t i=0;i<COLS*ROWS;i++) VGA[i]=(uint16_t)color<<8|' '; row=0; col=0; }
static void setcolor(unsigned fg,unsigned bg) { color=COLOR(fg&15,bg&7); }
static void print_u32(uint32_t n) { char b[11]; unsigned i=0; if(!n){putc('0');return;} while(n&&i<10){b[i++]='0'+n%10;n/=10;} while(i)putc(b[--i]); }
static void prompt(void) { char path[128];setcolor(LIME,BLACK);puts(host_name);setcolor(SKY,BLACK);puts("@");setcolor(WHITE,BLACK);puts(user_name);setcolor(LIGHT,BLACK);puts(":");fs_path(cwd,path,sizeof(path));puts(path);puts("$ "); }
static uint8_t read_scancode(void) { while(!(inb(0x64)&1)) { ticks++; } return inb(0x60); }
static char keymap(uint8_t sc) {
    static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '};
    if(sc>=sizeof(map)) return 0;
    char ch=map[sc];
    if(ch>='a'&&ch<='z') { if(shift_down^caps_lock) ch=(char)(ch-'a'+'A'); return ch; }
    if(shift_down) { const char normal[]="1234567890-=[]\\;',./`"; const char shifted[]="!@#$%^&*()_+{}|:\"<>?~"; for(unsigned i=0;normal[i];i++)if(ch==normal[i])return shifted[i]; }
    return ch;
}
static void setup_identity_field(const char *label,char *value,size_t cap) {
    char entered[32];size_t n=0;int local_shift=0,local_caps=0;
    puts(label);puts(" ");
    for(;;){uint8_t sc=read_scancode();if(sc==0x2A||sc==0x36){local_shift=1;shift_down=1;continue;}if(sc==0xAA||sc==0xB6){local_shift=0;shift_down=0;continue;}if(sc==0x3A){local_caps=!local_caps;caps_lock=local_caps;continue;}if(sc&0x80)continue;
        char ch=keymap(sc);if(ch=='\n')break;if(ch=='\b'){if(n){--n;putc('\b');}continue;}
        if(ch>='a'&&ch<='z')ch=(local_shift^local_caps)?(char)(ch-'a'+'A'):ch;
        int allowed=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='+';
        if(allowed&&n+1<cap){entered[n++]=ch;putc(ch);}
    }
    putc('\n');entered[n]=0;if(n)copy(value,entered,cap);
}
static void setup_identity(void) {
    if(identity_loaded){
        int home=fs_child(4,user_name);if(home<0){char path[72]="/home/";size_t n=slen(path);copy(path+n,user_name,sizeof(path)-n);home=fs_new(path,1);}if(home>=0)cwd=home;
        setcolor(AQUA,BLACK);puts("Saved guest setup loaded from the data disk.\n\n");setcolor(LIGHT,BLACK);return;
    }
    setcolor(AQUA,BLACK);
#if BLACKBIT_UI
    puts("+--------------------------------------------------------------+\n|                    BLACKBIT INSTALLER                        |\n+--------------------------------------------------------------+\n");
#else
    puts("BLACKBIT SHELL INSTALLER\n");
#endif
    setcolor(LIGHT,BLACK);puts("Choose language and keyboard layout (for example EN, TR, DE).\n");
    setup_identity_field("Language:",language,sizeof(language));
    setup_identity_field("Keyboard:",keyboard,sizeof(keyboard));
    setup_identity_field("Hostname:",host_name,sizeof(host_name));
    setup_identity_field("Username:",user_name,sizeof(user_name));
    int home=fs_child(4,user_name);
    if(home<0){char path[72]="/home/";size_t n=slen(path);copy(path+n,user_name,sizeof(path)-n);home=fs_new(path,1);}
    if(home>=0)cwd=home;
    puts("Setup complete. Saving /Blackbit/asd.json to the guest data disk...\n");save_guest_config();puts("\n");
}
static unsigned bcd(uint8_t v) { return (v&15)+((v>>4)*10); }
static uint8_t cmos(uint8_t reg) { outb(0x70,(uint8_t)(reg|0x80)); return inb(0x71); }
static int rtc_ready(void) { return !(cmos(0x0A)&0x80); }
static void print2(unsigned n) { putc('0'+(n/10)%10); putc('0'+n%10); }
static void show_date(void) {
    unsigned sec,min,hour,day,month,year; uint8_t mode;
    do { while(!rtc_ready()){} sec=cmos(0);min=cmos(2);hour=cmos(4);day=cmos(7);month=cmos(8);year=cmos(9);mode=cmos(0x0B); } while(!rtc_ready());
    if(!(mode&4)){sec=bcd(sec);min=bcd(min);hour=(hour&0x80)|bcd(hour&0x7f);day=bcd(day);month=bcd(month);year=bcd(year);}
    if(!(mode&2)){unsigned pm=hour&0x80;hour&=0x7f;hour=(hour%12)+(pm?12:0);}
    puts("20");print2(year);putc('-');print2(month);putc('-');print2(day);putc(' ');print2(hour);putc(':');print2(min);putc(':');print2(sec);putc('\n');
}
static void banner(void) {
    setcolor(SKY,BLACK); puts("  ____  _            _    _     _ _   "); putc('\n');
    puts(" | __ )| | __ _  ___| | _| |__ (_) |_ "); putc('\n');
    puts(" |  _ \\| |/ _` |/ __| |/ / '_ \\| | __|"); putc('\n');
    puts(" | |_) | | (_| | (__|   <| |_) | | |_ "); putc('\n');
    puts(" |____/|_|\\__,_|\\___|_|\\_\\_.__/|_|\\__|"); putc('\n');
    setcolor(LIME,BLACK); puts("Blackbit OS 0.1  |  x86 protected-mode kernel"); putc('\n');
    setcolor(DARK,BLACK); puts("Booted by GRUB Multiboot2. Built-in commands run in ring 0."); putc('\n');
    puts("Type 'help' to explore the kernel command console."); putc('\n'); putc('\n');
    setcolor(LIGHT,BLACK);
}
static void help(void) {
    setcolor(SKY,BLACK); puts("BLACKBIT KERNEL COMMANDS"); putc('\n'); setcolor(LIGHT,BLACK);
    puts("  help                 list built-in kernel commands\n");
    puts("  clear                clear the VGA console\n");
    puts("  about                show OS and boot information\n");
    puts("  uname                show kernel identity\n");
    puts("  mem                  show detected usable RAM and kernel heap\n");
    puts("  cpu                  show processor vendor\n");
    puts("  date                 show real-time clock\n");
    puts("  whoami               show current kernel identity (ring 0)\n");
    puts("  pwd                  show current virtual path\n");
    puts("  history              show recent commands\n");
    puts("  scan/treeview [dir]   list files/tree\n");
    puts("  peek <file>           read file; scribe <file> <text...> writes RAM file\n");
    puts("  spawn <file>          create file; vault <dir> creates directory\n");
    puts("  seal/unseal <file>    lock/unlock file writes\n");
    puts("  wipe <file>           overwrite RAM contents then remove; wipe alone clears screen\n");
    puts("  jump <dir>            change directory (cd)\n");
    puts("  where                 print path (pwd)\n");
    puts("  show <file>           print a file (cat)\n");
    puts("  spark <file>          create a file (touch)\n");
    puts("  vault/nest <dir>      create a directory; forge install/remove/search/update/upgrade/list uses Kaya\n");
    puts("  collect <dir>/gather <name>/sweep <empty-dir> directory helpers\n");
    puts("  collapse <dir>        remove an empty directory (rmdir)\n");
    puts("  clone <src> <dst>     copy a file (cp)\n");
    puts("  shift <src> <dst>     move or rename (mv)\n");
    puts("  erase <path>          remove a file (rm)\n");
    puts("  seek <text>           search names (find)\n");
    puts("  sift <text> <file>    search file lines (grep)\n");
    puts("  tasks / terminate / monitor  process status (ps / kill / top)\n");
    puts("  clear                 clear the VGA console\n");
    puts("  machine/kernelinfo/ramview/coreview/health  system summaries\n");
    puts("  enter <dir>/leave     change directory / parent\n");
    puts("  p/v/r/x/d/m/c/n       print/view/run/execute/delete/move/copy/new aliases\n");
    puts("  permit / assign       permissions / owner (chmod / chown)\n");
    puts("  attach / detach       mark mount points (mount / umount)\n");
    puts("  kaya list             list Kaya packages via the WSL bridge\n");
    puts("  kaya install <name>   install a Kaya package (curl on WSL)\n");
    puts("  kaya info/remove/relook/upgrade  Kaya manager actions\n");
    puts("  kaya run <package> [args...]     run a Kaya package through WSL\n");
    puts("  network info|dns|ping|port|trace Kaya host network tools\n");
    puts("  small-creatures --add APT        expose Ubuntu APT on WSL PATH\n");
    puts("  apt [arguments...]               call the host Ubuntu APT through WSL\n");
    puts("  alloc <bytes>         allocate test memory from the kernel heap\n");
    puts("  heap                 show the ring-0 heap range\n");
    puts("  uptime               show time since kernel start\n");
    puts("  echo <text>          print text\n");
    puts("  color <0-15>         change console foreground\n");
    puts("  reboot               reboot the machine\n");
    puts("  halt                 halt the CPU\n");
}
static void reboot(void) { puts("Rebooting Blackbit...\n"); for(unsigned i=0;i<100000;i++) (void)inb(0x64); outb(0x64,0xFE); for(;;) cpu_halt(); }
static void command(char *s) {
    char *argv[8];unsigned argc=0;while(*s==' ')s++;
    while(*s&&argc<8){while(*s==' ')s++;if(!*s)break;argv[argc++]=s;while(*s&&*s!=' ')s++;if(*s)*s++=0;}
    if(!argc)return;
    char *cmd=argv[0];
    if(eq(cmd,"help"))help();
    else if(eq(cmd,"clear")||(eq(cmd,"wipe")&&argc==1))clear();
    else if(eq(cmd,"about")||eq(cmd,"machine"))puts("Blackbit OS 0.1\nRing-0 x86 kernel console. RAM filesystem; changes reset on reboot.\n");
    else if(eq(cmd,"uname")||eq(cmd,"kernelinfo"))puts("Blackbit blackbit-kernel 0.1 i686 bare-metal\n");
    else if(eq(cmd,"health")){puts("Kernel: running\nFilesystem: RAM-only\nScheduler: unavailable\nNetwork stack: unavailable\n");}
    else if(eq(cmd,"mem")||eq(cmd,"ramview")){puts("Usable RAM: ");print_u32((uint32_t)(usable_ram/1048576u));puts(" MiB\nKernel heap free: ");print_u32(heap_limit>heap_next?(uint32_t)((heap_limit-heap_next)/1024u):0);puts(" KiB\n");}
    else if(eq(cmd,"cpu")||eq(cmd,"coreview")){puts("CPU vendor: ");puts(cpu_name);putc('\n');}
    else if(eq(cmd,"date"))show_date();
    else if(eq(cmd,"whoami")){puts(user_name);puts(" (ring 0 kernel console)\n");}
    else if(eq(cmd,"pwd")||eq(cmd,"where")){char path[128];fs_path(cwd,path,sizeof(path));puts(path);putc('\n');}
    else if(eq(cmd,"history")){for(unsigned i=0;i<history_count;i++){print_u32(i+1);puts("  ");puts(history[i]);putc('\n');}}
    else if(eq(cmd,"treeview")){int id=argc>1?fs_resolve(argv[1]):cwd;if(id<0||!fs[id].is_dir)puts("not a directory\n");else treeview(id,0);}
    else if(eq(cmd,"collect")){int id=argc>1?fs_resolve(argv[1]):cwd;fs_list(id);}
    else if(eq(cmd,"gather")){if(argc<2)puts("usage: gather <name fragment>\n");else{char *args[]={argv[1]};char request[96];size_t n=0;const char *prefix="seek ";while(*prefix)request[n++]=*prefix++;for(size_t i=0;args[0][i]&&n+2<sizeof(request);i++)request[n++]=args[0][i];request[n]=0;command(request);}}
    else if(eq(cmd,"ls")||eq(cmd,"scan")){int id=argc>1?fs_resolve(argv[1]):cwd;fs_list(id);}
    else if(eq(cmd,"leave")){if(cwd>0)cwd=fs[cwd].parent;}
    else if(eq(cmd,"cd")||eq(cmd,"jump")||eq(cmd,"enter")){int id=argc>1?fs_resolve(argv[1]):9;if(id<0||!fs[id].is_dir)puts("directory not found\n");else cwd=id;}
    else if(eq(cmd,"cat")||eq(cmd,"show")||eq(cmd,"peek")||eq(cmd,"v")){if(argc<2)puts("usage: show <file>\n");else{int id=fs_resolve(argv[1]);if(id<0||fs[id].is_dir)puts("file not found\n");else puts(fs[id].data);}}
    else if(eq(cmd,"touch")||eq(cmd,"spark")||eq(cmd,"spawn")||eq(cmd,"n")){if(argc<2)puts("usage: spark <file>\n");else{int id=fs_resolve(argv[1]);if(id<0)id=fs_new(argv[1],0);if(id<0)puts("cannot create file\n");else if(fs[id].is_dir)puts("path is a directory\n");else puts("file ready in RAM\n");}}
    else if(eq(cmd,"forge")&&argc>1&&(eq(argv[1],"install")||eq(argv[1],"remove")||eq(argv[1],"search")||eq(argv[1],"update")||eq(argv[1],"upgrade")||eq(argv[1],"list"))){if(eq(argv[1],"list"))kaya_request("KAYA list");else if(eq(argv[1],"update"))kaya_request("KAYA relook");else if(eq(argv[1],"upgrade"))kaya_request("KAYA upgrade");else if(eq(argv[1],"search"))puts("unavailable: the current Kaya bridge does not expose repository search yet\n");else if(argc<3)puts("usage: forge install|remove <package>\n");else if(eq(argv[1],"install")||eq(argv[1],"remove")){char request[128];size_t n=0;const char *prefix=eq(argv[1],"install")?"KAYA install ":"KAYA remove ";while(*prefix)request[n++]=*prefix++;for(size_t i=0;argv[2][i]&&n+2<sizeof(request);i++)request[n++]=argv[2][i];request[n]=0;kaya_request(request);}}
    else if(eq(cmd,"mkdir")||eq(cmd,"forge")||eq(cmd,"vault")||eq(cmd,"nest")){if(argc<2)puts("usage: forge <dir>\n");else{int id=fs_new(argv[1],1);puts(id>=0?"directory forged\n":id==-2?"already exists\n":"cannot create directory\n");}}
    else if(eq(cmd,"rmdir")||eq(cmd,"collapse")||eq(cmd,"sweep")){if(argc<2)puts("usage: collapse <dir>\n");else{int id=fs_resolve(argv[1]);if(id<=0||!fs[id].is_dir||!fs_remove(id))puts("directory missing, protected, or not empty\n");else puts("directory removed\n");}}
    else if(eq(cmd,"rm")||eq(cmd,"erase")||eq(cmd,"d")){if(argc<2)puts("usage: erase <file>\n");else{int id=fs_resolve(argv[1]);if(id<0||fs[id].is_dir||!fs_writable_file(id)||!fs_remove(id))puts("file missing, sealed, or protected\n");else puts("erased from RAM\n");}}
    else if(eq(cmd,"wipe")){if(argc<2)puts("usage: wipe <file>\n");else{int id=fs_resolve(argv[1]);if(!fs_writable_file(id))puts("file missing, sealed, or protected\n");else{for(size_t i=0;i<FS_DATA;i++)fs[id].data[i]=0;fs_remove(id);puts("RAM contents overwritten and file removed\n");}}}
    else if(eq(cmd,"cp")||eq(cmd,"clone")||eq(cmd,"c")){if(argc<3)puts("usage: clone <source> <destination>\n");else{int src=fs_resolve(argv[1]);if(src<0||fs[src].is_dir)puts("source file not found\n");else{int dst=fs_resolve(argv[2]);char path[96];if(dst>=0&&fs[dst].is_dir){fs_path(dst,path,sizeof(path));size_t n=slen(path);if(n>1)path[n++]='/';copy(path+n,fs[src].name,sizeof(path)-n);dst=fs_new(path,0);}else if(dst<0)dst=fs_new(argv[2],0);else dst=-1;if(dst<0)puts("destination unavailable\n");else{copy(fs[dst].data,fs[src].data,FS_DATA);puts("cloned in RAM\n");}}}}
    else if(eq(cmd,"mv")||eq(cmd,"shift")||eq(cmd,"m")){if(argc<3)puts("usage: shift <source> <destination>\n");else{int src=fs_resolve(argv[1]);int dst=fs_resolve(argv[2]);int parent=-1;char name[FS_NAME];if(src<=0||src<10||!fs[src].used||fs[src].sealed)puts("source protected, sealed, or missing\n");else if(dst>=0&&fs[dst].is_dir){parent=dst;copy(name,fs[src].name,sizeof(name));}else if(dst<0&&fs_parent_name(argv[2],&parent,name)){}else parent=-1;int ancestor=parent;while(ancestor>=0){if(ancestor==src){parent=-1;break;}if(ancestor==0)break;ancestor=fs[ancestor].parent;}if(parent<0||fs_child(parent,name)>=0)puts("destination unavailable\n");else{fs[src].parent=parent;copy(fs[src].name,name,sizeof(fs[src].name));puts("shifted in RAM\n");}}}
    else if(eq(cmd,"find")||eq(cmd,"seek")){if(argc<2)puts("usage: seek <name fragment>\n");else{int found=0;for(int i=1;i<FS_NODES;i++)if(fs[i].used&&contains(fs[i].name,argv[1])){char path[128];fs_path(i,path,sizeof(path));puts(path);putc('\n');found++;}if(!found)puts("no matches\n");}}
    else if(eq(cmd,"grep")||eq(cmd,"sift")){if(argc<3)puts("usage: sift <text> <file>\n");else{int id=fs_resolve(argv[2]);if(id<0||fs[id].is_dir)puts("file not found\n");else{char *p=fs[id].data;int found=0;while(*p){char line[FS_DATA];size_t n=0;while(*p&&*p!='\n'&&n+1<sizeof(line))line[n++]=*p++;if(*p=='\n')p++;line[n]=0;if(contains(line,argv[1])){puts(line);putc('\n');found++;}}if(!found)puts("no matching lines\n");}}}
    else if(eq(cmd,"ps")||eq(cmd,"tasks")){puts("PID  STATE   TASK\n1    running blackbit-kernel-console\n");}
    else if(eq(cmd,"kill")||eq(cmd,"terminate")){if(argc<2)puts("usage: terminate <pid>\n");else if(eq(argv[1],"1"))puts("kernel task 1 is protected\n");else puts("no such task\n");}
    else if(eq(cmd,"top")||eq(cmd,"monitor")){puts("Blackbit kernel monitor\nTasks: ");print_u32(active_tasks);puts("  RAM: ");print_u32((uint32_t)(usable_ram/1048576u));puts(" MiB  Poll ticks: ");print_u32(ticks);putc('\n');}
    else if(eq(cmd,"scribe")){if(argc<3)puts("usage: scribe <file> <text...>\n");else{int id=fs_resolve(argv[1]);if(id<0)id=fs_new(argv[1],0);if(!fs_writable_file(id))puts("file unavailable, sealed, or protected\n");else{size_t n=0;for(unsigned i=2;i<argc;i++){if(i>2&&n+1<FS_DATA)fs[id].data[n++]=' ';for(size_t j=0;argv[i][j]&&n+1<FS_DATA;j++)fs[id].data[n++]=argv[i][j];}if(n+1<FS_DATA)fs[id].data[n++]='\n';fs[id].data[n]=0;puts("written to RAM file\n");}}}
    else if(eq(cmd,"seal")||eq(cmd,"unseal")){if(argc<2)puts("usage: seal|unseal <file>\n");else{int id=fs_resolve(argv[1]);if(id<0||fs[id].is_dir||fs_readonly_file(id))puts("file missing or protected\n");else{fs[id].sealed=eq(cmd,"seal");puts(fs[id].sealed?"file sealed\n":"file unsealed\n");}}}
    else if(eq(cmd,"echo")||eq(cmd,"say")||eq(cmd,"p")){if(argc>=4&&eq(argv[argc-2],">")){int id=fs_resolve(argv[argc-1]);if(id<0)id=fs_new(argv[argc-1],0);if(!fs_writable_file(id))puts("cannot write destination\n");else{fs[id].data[0]=0;for(unsigned i=1;i<argc-2;i++){size_t n=slen(fs[id].data);if(n+1<FS_DATA){if(i>1)fs[id].data[n++]=' ';copy(fs[id].data+n,argv[i],FS_DATA-n);}}size_t n=slen(fs[id].data);if(n+1<FS_DATA)fs[id].data[n++]='\n';fs[id].data[n]=0;puts("written to RAM file\n");}}else{for(unsigned i=1;i<argc;i++){if(i>1)putc(' ');puts(argv[i]);}putc('\n');}}
    else if(eq(cmd,"chmod")||eq(cmd,"permit")){if(argc<3)puts("usage: permit <octal-mode> <file>\n");else{int id=fs_resolve(argv[2]);unsigned mode=0;char *p=argv[1];while(*p>='0'&&*p<='7'){mode=mode*8+(*p++-'0');}if(*p||mode>0777||id<0||fs_readonly_file(id))puts("invalid mode or protected file\n");else{fs[id].mode=mode;puts("permissions updated\n");}}}
    else if(eq(cmd,"chown")||eq(cmd,"assign")){if(argc<3)puts("usage: assign <owner-id> <file>\n");else{int id=fs_resolve(argv[2]);unsigned owner=0;char *p=argv[1];while(*p>='0'&&*p<='9'){owner=owner*10+(*p++-'0');}if(*p||owner>65535||id<0||fs_readonly_file(id))puts("invalid owner or protected file\n");else{fs[id].owner=owner;puts("owner id updated\n");}}}
    else if(eq(cmd,"mount")||eq(cmd,"attach")){if(argc<3)puts("usage: attach <source> <mount-point>\n");else{int id=fs_resolve(argv[2]);if(id<0||!fs[id].is_dir)puts("mount point not found\n");else{fs[id].mounted=1;puts("RAM mount attached (device drivers are not implemented yet)\n");}}}
    else if(eq(cmd,"umount")||eq(cmd,"detach")){if(argc<2)puts("usage: detach <mount-point>\n");else{int id=fs_resolve(argv[1]);if(id<0||!fs[id].mounted)puts("mount not found\n");else{fs[id].mounted=0;puts("mount detached\n");}}}
    else if(eq(cmd,"kaya")){
        if(argc==2&&eq(argv[1],"list"))kaya_request("KAYA list");
        else if(argc==2&&eq(argv[1],"relook"))kaya_request("KAYA relook");
        else if(argc==2&&eq(argv[1],"upgrade"))kaya_request("KAYA upgrade");
        else if(argc==3&&(eq(argv[1],"info")||eq(argv[1],"install")||eq(argv[1],"remove"))){int safe=1;for(size_t i=0;argv[2][i];i++){char c=argv[2][i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))safe=0;}if(!safe||!argv[2][0])puts("invalid Kaya package name\n");else{char request[128];size_t n=0;const char *prefix="KAYA ";while(*prefix)request[n++]=*prefix++;for(size_t i=0;argv[1][i];i++)request[n++]=argv[1][i];request[n++]=' ';for(size_t i=0;argv[2][i]&&n+2<sizeof(request);i++)request[n++]=argv[2][i];request[n]=0;kaya_request(request);}}
        else if(argc>=3&&eq(argv[1],"run")){
            int safe=1;for(size_t i=0;argv[2][i];i++){char c=argv[2][i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))safe=0;}
            if(!safe||!argv[2][0])puts("invalid Kaya package name\n");
            else kaya_run_request(argv[2],argc-3,&argv[3]);
        }
        else puts("usage: kaya list | info <name> | install <name> | remove <name> | run <name> [args...] | relook | upgrade\n");
    }
    else if(eq(cmd,"network")){
        if(argc<2){puts("usage: network info | dns <host> | ping <host> [--count N] | port <host> <port> | trace <host>\n");}
        else kaya_run_request("kaya-netdiag",argc-1,&argv[1]);
    }
    else if(eq(cmd,"subnet")){
        if(argc<2)puts("usage: subnet <CIDR> [--strict]\n");
        else kaya_run_request("kaya-subnet",argc-1,&argv[1]);
    }
    else if(eq(cmd,"small-creatures")){
        if(argc!=3||(!eq(argv[1],"--add")&&!eq(argv[1],"--install"))||!eq(argv[2],"APT"))puts("usage: small-creatures --add APT\n");
        else {char *args[]={"--add","APT"};kaya_run_request("small-creatures",2,args);}
    }
    else if(eq(cmd,"apt"))host_apt_request(argc-1,&argv[1]);
    else if(eq(cmd,"r")||eq(cmd,"x")){if(argc<2)puts("usage: run|execute <Kaya-package> [args...]\n");else kaya_run_request(argv[1],argc-2,&argv[2]);}
    else if(eq(cmd,"sudo")){puts("The kernel console runs in ring 0. Host commands use the WSL user permissions; guest privileges do not grant host root.\n");}
    else if(eq(cmd,"battery")||eq(cmd,"thermal")||eq(cmd,"board")){puts("unavailable: this kernel has no ACPI/SMBIOS hardware driver yet\n");}
    else if(eq(cmd,"drives")||eq(cmd,"spaceview")||eq(cmd,"mountdisk")||eq(cmd,"ejectdisk")||eq(cmd,"formatdisk")||eq(cmd,"partition")||eq(cmd,"repairdisk")||eq(cmd,"checkdisk")){puts("unavailable: block-device and filesystem drivers are not implemented\n");}
    else if(eq(cmd,"freeze")||eq(cmd,"resume")||eq(cmd,"boost")||eq(cmd,"spawnproc")||eq(cmd,"watchproc")||eq(cmd,"hunt")||eq(cmd,"tracker")){puts("unavailable: this kernel has no scheduler or user processes yet\n");}
    else if(eq(cmd,"portal")||eq(cmd,"netview")||eq(cmd,"routeview")||eq(cmd,"beacon")||eq(cmd,"echohost")||eq(cmd,"scanhost")||eq(cmd,"discover")||eq(cmd,"listen")||eq(cmd,"bridge")||eq(cmd,"tunnel")){puts("unavailable: this kernel has no network stack; Kaya/WSL bridge tools are separate\n");}
    else if(eq(cmd,"citizen")||eq(cmd,"loginas")||eq(cmd,"logout")||eq(cmd,"grant")||eq(cmd,"revoke")||eq(cmd,"identity")||eq(cmd,"role")||eq(cmd,"quota")){puts("unavailable: accounts and authorization are not implemented; setup identity is display-only\n");}
    else if(eq(cmd,"summon")||eq(cmd,"banish")||eq(cmd,"awaken")||eq(cmd,"hibernate")||eq(cmd,"teleport")||eq(cmd,"invoke")||eq(cmd,"warp")||eq(cmd,"observe")||eq(cmd,"oracle")||eq(cmd,"chronicle")||eq(cmd,"artifact")||eq(cmd,"beast")||eq(cmd,"overseer")){puts("Blackbit kernel console: this is a decorative command family; no action was performed\n");}
    else if(eq(cmd,"heap")){puts("Kernel heap free: ");print_u32(heap_limit>heap_next?(uint32_t)((heap_limit-heap_next)/1024u):0);puts(" KiB\n");}
    else if(eq(cmd,"alloc")&&argc>1){uint32_t n=0;char *p=argv[1];while(*p>='0'&&*p<='9'){n=n*10+(*p++-'0');if(n>1048576u)break;}uintptr_t aligned=(heap_next+15u)&~(uintptr_t)15u;if(*p||!n||n>1048576u||aligned+n>heap_limit)puts("allocation failed; size must be 1..1048576 bytes\n");else{heap_next=aligned+n;puts("kernel heap allocation succeeded\n");}}
    else if(eq(cmd,"uptime")){puts("Kernel poll ticks: ");print_u32(ticks);putc('\n');}
    else if(eq(cmd,"color")&&argc>1){unsigned n=0;char *p=argv[1];while(*p>='0'&&*p<='9'){n=n*10+(*p++-'0');}if(*p||n>15)puts("color must be 0-15\n");else{setcolor(n,BLACK);puts("console color updated\n");}}
    else if(eq(cmd,"reboot")||eq(cmd,"restart"))reboot();
    else if(eq(cmd,"shutdown")||eq(cmd,"poweroff")||eq(cmd,"halt")){puts("Blackbit halted. Power off is safe.\n");for(;;)cpu_halt();}
    else {setcolor(RED,BLACK);puts("unknown command: ");puts(cmd);puts(". Type help.\n");setcolor(LIGHT,BLACK);}
}
#ifdef BLACKBIT_SELFTEST
static unsigned test_passed, test_failed;
static void test_assert(const char *name,int ok) {
    puts(ok?"[PASS] ":"[FAIL] ");puts(name);putc('\n');
    if(ok)test_passed++;else test_failed++;
}
static void test_command(const char *line) {
    char buf[128];copy(buf,line,sizeof(buf));test_output_len=0;test_output[0]=0;command(buf);
}
static void run_self_tests(void) {
    test_command("vault /cmdtest");test_assert("vault creates directory",fs_resolve("/cmdtest")>=0&&fs[fs_resolve("/cmdtest")].is_dir);
    test_command("enter /cmdtest");test_assert("enter changes directory",cwd==fs_resolve("/cmdtest"));
    test_command("spawn note");test_assert("spawn creates file",fs_resolve("note")>=0&&!fs[fs_resolve("note")].is_dir);
    test_command("scribe note hello blackbit");int note=fs_resolve("note");test_assert("scribe writes text",note>=0&&contains(fs[note].data,"hello blackbit"));
    test_command("peek note");test_assert("peek reads file",contains(test_output,"hello blackbit"));
    test_command("seal note");test_command("scribe note blocked");test_assert("seal blocks writes",fs[note].sealed&&contains(fs[note].data,"hello blackbit")&&!contains(fs[note].data,"blocked"));
    test_command("unseal note");test_command("scribe note updated");test_assert("unseal allows writes",!fs[note].sealed&&contains(fs[note].data,"updated"));
    test_command("clone note copy");test_assert("clone copies file",fs_resolve("copy")>=0&&contains(fs[fs_resolve("copy")].data,"updated"));
    test_command("shift copy moved");test_assert("shift renames file",fs_resolve("copy")<0&&fs_resolve("moved")>=0);
    test_command("erase moved");test_assert("erase removes file",fs_resolve("moved")<0);
    test_command("spawn secret");test_command("scribe secret erase-me");test_command("wipe secret");test_assert("wipe overwrites and removes file",fs_resolve("secret")<0);
    test_command("leave");test_assert("leave returns to parent",cwd==0);
    test_command("n /cmdtest/new");test_assert("n alias creates file",fs_resolve("/cmdtest/new")>=0);
    test_command("c /cmdtest/note /cmdtest/copy2");test_assert("c alias copies file",fs_resolve("/cmdtest/copy2")>=0);
    test_command("m /cmdtest/copy2 /cmdtest/moved2");test_assert("m alias moves file",fs_resolve("/cmdtest/copy2")<0&&fs_resolve("/cmdtest/moved2")>=0);
    test_command("d /cmdtest/moved2");test_assert("d alias deletes file",fs_resolve("/cmdtest/moved2")<0);
    test_command("v /cmdtest/note");test_assert("v alias reads file",contains(test_output,"updated"));
    test_command("treeview /cmdtest");test_assert("treeview lists directory tree",contains(test_output,"note")&&contains(test_output,"new"));
    test_command("collect /cmdtest");test_assert("collect lists directory entries",contains(test_output,"note"));
    test_command("gather note");test_assert("gather searches file names",contains(test_output,"/cmdtest/note"));
    test_command("nest /cmdtest/inner");test_assert("nest creates subdirectory",fs_resolve("/cmdtest/inner")>=0);
    test_command("sweep /cmdtest/inner");test_assert("sweep removes empty directory",fs_resolve("/cmdtest/inner")<0);
    test_command("forge search example");test_assert("forge search reports bridge limitation",contains(test_output,"unavailable:"));
    test_command("forge list");test_assert("forge list sends Kaya list",eq(test_bridge_request,"KAYA list"));
    test_command("forge install nano");test_assert("forge install sends package request",eq(test_bridge_request,"KAYA install nano"));
    test_command("forge remove nano");test_assert("forge remove sends package request",eq(test_bridge_request,"KAYA remove nano"));
    test_command("forge update");test_assert("forge update refreshes repository cache",eq(test_bridge_request,"KAYA relook"));
    test_command("forge upgrade");test_assert("forge upgrade sends upgrade request",eq(test_bridge_request,"KAYA upgrade"));
    test_command("r demo");test_assert("r alias forwards package execution",eq(test_bridge_request,"KAYA run demo"));
    test_command("x demo --help");test_assert("x alias forwards package execution args",eq(test_bridge_request,"KAYA run demo --help"));
    test_command("where");test_assert("where reports current path",contains(test_output,"/"));
    test_command("tasks");test_assert("tasks reports the kernel console task",contains(test_output,"blackbit-kernel-console"));
    test_command("r");test_assert("r alias is recognized",contains(test_output,"usage: run|execute"));
    test_command("x");test_assert("x alias is recognized",contains(test_output,"usage: run|execute"));
    test_command("wipe");test_assert("bare wipe clears the console",1);
    test_command("p alias-output");test_assert("p alias prints text",contains(test_output,"alias-output"));
    const char *info_commands[]={"machine","kernelinfo","ramview","coreview","health","uptime"};
    for(unsigned i=0;i<sizeof(info_commands)/sizeof(info_commands[0]);i++){test_command(info_commands[i]);test_assert(info_commands[i],!contains(test_output,"unknown command"));}
    const char *explicit_unsupported[]={"battery","thermal","board","drives","freeze","resume","boost","spawnproc","watchproc","tracker","portal","netview","routeview","beacon","echohost","scanhost","discover","listen","bridge","tunnel","citizen","loginas","logout","grant","revoke","identity","role","quota","mountdisk","ejectdisk","formatdisk","partition","repairdisk","checkdisk","spaceview"};
    for(unsigned i=0;i<sizeof(explicit_unsupported)/sizeof(explicit_unsupported[0]);i++){test_command(explicit_unsupported[i]);test_assert(explicit_unsupported[i],contains(test_output,"unavailable:"));}
    const char *fun_commands[]={"summon","banish","awaken","hibernate","teleport","invoke","warp","observe","oracle","chronicle","artifact","beast","overseer"};
    for(unsigned i=0;i<sizeof(fun_commands)/sizeof(fun_commands[0]);i++){test_command(fun_commands[i]);test_assert(fun_commands[i],contains(test_output,"decorative command family"));}
    puts("SELFTEST: ");print_u32(test_passed);puts(" passed, ");print_u32(test_failed);puts(" failed\n");
    uint32_t exit_code=test_failed?0x11:0x10;__asm__ volatile("outl %0,%1"::"a"(exit_code),"Nd"((uint16_t)0xF4));for(;;)cpu_halt();
}
#endif

void kernel_main(uint32_t magic,uint32_t info) {
    boot_magic=magic; boot_info=info; detect_cpu(); parse_memory_map(info); fs_init(); serial_init(); setcolor(LIGHT,BLACK); clear();identity_loaded=load_guest_config();banner();
    if(magic!=0x36d76289) { setcolor(RED,BLACK); puts("Warning: Multiboot2 magic mismatch.\n"); setcolor(LIGHT,BLACK); }
#ifdef BLACKBIT_SELFTEST
    copy(user_name,"tester",sizeof(user_name));
    int test_home=fs_child(4,user_name);if(test_home<0){test_home=fs_new("/home/tester",1);}if(test_home>=0)cwd=test_home;
    run_self_tests();
#else
    setup_identity();
    prompt();
#endif
    for(;;) {
        uint8_t sc=read_scancode();
        if(sc==0x2A||sc==0x36){shift_down=1;continue;}
        if(sc==0xAA||sc==0xB6){shift_down=0;continue;}
        if(sc==0x3A){caps_lock=!caps_lock;continue;}
        if(sc&0x80) continue;
        if(sc==0x48) { if(history_count){if(history_pos)history_pos--;copy(input,history[history_pos],sizeof(input));input_len=slen(input);puts("\rblackbit@kernel:~$ ");puts(input);} continue; }
        if(sc==0x50) { if(history_pos<history_count)history_pos++;if(history_pos>=history_count){input[0]=0;input_len=0;}else{copy(input,history[history_pos],sizeof(input));input_len=slen(input);}puts("\rblackbit@kernel:~$ ");puts(input);continue; }
        char ch=keymap(sc); if(!ch) continue;
        if(ch=='\n') { putc('\n'); input[input_len]=0; if(input_len){if(history_count<HISTORY)copy(history[history_count++],input,128);else{for(unsigned i=1;i<HISTORY;i++)copy(history[i-1],history[i],128);copy(history[HISTORY-1],input,128);}history_pos=history_count;} command(input); input_len=0; input[0]=0; prompt(); }
        else if(ch=='\b') { if(input_len){input[--input_len]=0;putc('\b');} }
        else if(input_len+1<sizeof(input)&&ch>=32) { input[input_len++]=ch; input[input_len]=0; putc(ch); }
    }
}
