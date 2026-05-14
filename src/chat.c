/*
 * Void Chat – Encrypted Terminal Chat Client
 * OpenBSD base + libcrypto (SHA256, RAND_bytes, Base64)
 * Compile: cc -O2 -o chat chat.c -lcrypto
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#define PIPE "/tmp/chat-pipe"
#define OUT  "/tmp/chat-output"
#define LOG  "/tmp/chat.log"
#define MS   512
#define MH   500
#define KL   32
#define NL   12

char *nick;
int rows, cols, out_fd = -1;
long out_pos = 0;
char hist[MH][MS];
int hc = 0, need_full_redraw = 1;
unsigned char key[KL];
int keyset = 0;

char *cl[] = {
    "\033[31m","\033[32m","\033[33m","\033[34m",
    "\033[35m","\033[36m","\033[91m","\033[92m",
    "\033[93m","\033[94m","\033[95m","\033[96m"
};
char *rs = "\033[0m";
int ncolors = 12;
struct termios oldterm;

void sz(void *p, size_t n) { volatile unsigned char *x = p; while (n--) *x++ = 0; }
void bye(int s) {
    tcsetattr(0, TCSANOW, &oldterm);
    printf("\033[2J\033[HGoodbye, %s!\n", nick);
    if (out_fd >= 0) close(out_fd);
    sz(key, KL);
    _exit(0);
}
void winch(int s) { need_full_redraw = 1; }
int gcol(const char *u) { int h = 0; for (int i = 0; u[i]; i++) h = (h * 31 + u[i]) % ncolors; return h; }
void addh(const char *s, const char *t, int c) {
    if (hc >= MH) {
        for (int i = 1; i < MH; i++) strlcpy(hist[i-1], hist[i], MS);
        hc = MH - 1;
    }
    snprintf(hist[hc], MS, "%s%s>%s %s", cl[c], s, rs, t);
    hc++;
    need_full_redraw = 1;
}
void sw() {
    FILE *w = popen("ps aux|grep '/usr/local/bin/chat$'|grep -v grep|awk '{print $1}'|sort -u", "r");
    if (!w) return;
    char u[64], on[512] = "";
    int fi = 1, cnt = 0;
    while (fgets(u, sizeof(u), w)) {
        u[strcspn(u, "\n")] = 0;
        if (strlen(u) > 0 && strcmp(u, nick) != 0) {
            if (!fi) strlcat(on, ", ", sizeof(on));
            strlcat(on, u, sizeof(on));
            fi = 0; cnt++;
        }
    }
    pclose(w);
    char m[MS];
    if (cnt > 0) snprintf(m, MS, "Online: %s (+ you)", on);
    else snprintf(m, MS, "Online: only you (%s)", nick);
    addh("system", m, 5);
}
void dv(const char *pw) { SHA256((unsigned char*)pw, strlen(pw), key); keyset = 1; }

void xor_crypt(unsigned char *data, int dlen, unsigned char *nonce) {
    unsigned char ks[KL], buf[KL+NL+4];
    int used = 0, counter = 0;
    while (used < dlen) {
        memcpy(buf, key, KL);
        memcpy(buf+KL, nonce, NL);
        buf[KL+NL] = (counter>>24)&0xff;
        buf[KL+NL+1] = (counter>>16)&0xff;
        buf[KL+NL+2] = (counter>>8)&0xff;
        buf[KL+NL+3] = counter&0xff;
        SHA256(buf, KL+NL+4, ks);
        for (int i = 0; i < KL && used < dlen; i++, used++)
            data[used] ^= ks[i];
        counter++;
    }
}

int enc(const unsigned char *pt, int pl, unsigned char *ct, int *cl) {
    if (!keyset) return -1;
    if (pl > MS-2) return -1;
    unsigned char nonce[NL];
    RAND_bytes(nonce, NL);
    ct[0] = (pl>>8) & 0xff;
    ct[1] = pl & 0xff;
    memcpy(ct+2, pt, pl);
    xor_crypt(ct+2, pl, nonce);
    unsigned char final[MS+NL+2];
    memcpy(final, nonce, NL);
    memcpy(final+NL, ct, 2+pl);
    *cl = NL + 2 + pl;
    memcpy(ct, final, *cl);
    return 0;
}

int dec(const unsigned char *ct, int cl, unsigned char *pt, int *pl) {
    if (!keyset || cl < NL+2) return -1;
    unsigned char nonce[NL];
    memcpy(nonce, ct, NL);
    int plen = (ct[NL]<<8) | ct[NL+1];
    if (plen > MS-2 || NL+2+plen > cl) return -1;
    xor_crypt((unsigned char*)ct+NL+2, plen, nonce);
    memcpy(pt, ct+NL+2, plen);
    *pl = plen;
    return 0;
}

int b64e(const unsigned char *in, int il, char *out, int om) { return EVP_EncodeBlock((unsigned char*)out, in, il); }
int b64d(const char *in, unsigned char *out, int om) {
    return EVP_DecodeBlock(out, (unsigned char*)in, strlen(in));
}

void get_term_size() {
    struct winsize w; ioctl(0, TIOCGWINSZ, &w);
    rows = w.ws_row; cols = w.ws_col;
    if (cols < 40) cols = 80;
    if (rows < 10) rows = 24;
}
void center(const char *s) {
    int pad = (cols - strlen(s))/2;
    if (pad < 0) pad = 0;
    printf("\033[1;36m%*s%s\033[0m\n", pad, "", s);
}
void line(char c) { printf("\033[1;36m"); for (int i=0;i<cols;i++) printf("%c",c); printf("\033[0m\n"); }

void draw_full(const char *input) {
    if (!need_full_redraw) return;
    need_full_redraw = 0;
    get_term_size();
    printf("\033[2J\033[H");
    center("VOID CHAT");
    line('-');
    int visible = rows - 4;
    if (visible < 0) visible = 0;
    int start = hc - visible;
    if (start < 0) start = 0;
    for (int i = start; i < hc; i++) printf("%s\n", hist[i]);
    for (int i = hc - start; i < visible; i++) printf("\n");
    line('-');
    printf("\033[1;32m%s>\033[0m %s", nick, input);
    fflush(stdout);
}

void update_input(const char *input) {
    get_term_size();
    printf("\033[%d;1H\033[K\033[1;32m%s>\033[0m %s", rows, nick, input);
    fflush(stdout);
}

int ask(char *buf, int max) {
    struct termios t, old; tcgetattr(0, &old); t = old;
    t.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL);
    tcsetattr(0, TCSANOW, &t);
    printf("\033[1;33mRoom Key: \033[0m"); fflush(stdout);
    int p = 0;
    while (1) {
        char ch; if (read(0, &ch, 1) != 1) break;
        if (ch == '\r' || ch == '\n') { buf[p] = 0; printf("\n"); break; }
        else if ((ch == 127 || ch == 8) && p > 0) { p--; printf("\b \b"); }
        else if (ch >= 32 && p < max-1) { buf[p++] = ch; printf("*"); }
        fflush(stdout);
    }
    tcsetattr(0, TCSANOW, &old);
    return p;
}

void load_hist() {
    FILE *f = fopen(LOG, "r");
    if (!f) return;
    char buf[16384];
    while (fgets(buf, sizeof(buf), f)) {
        char *nl = strchr(buf, '\n'); if (nl) *nl = 0;
        char *colon = strchr(buf, ':');
        if (!colon) continue;
        *colon = 0;
        char *sender = buf;
        char *b64 = colon+1;
        if (strlen(sender) == 0 || strcmp(sender, "system") == 0) continue;
        unsigned char ct[MS*2], pt[MS];
        int clen = b64d(b64, ct, sizeof(ct)), pl;
        if (dec(ct, clen, pt, &pl) == 0 && pl > 0 && pl < MS) {
            pt[pl] = 0;
            addh(sender, (char*)pt, gcol(sender));
        }
    }
    fclose(f);
    out_fd = open(OUT, O_RDONLY);
    if (out_fd >= 0) out_pos = lseek(out_fd, 0, SEEK_END);
}

void check_new() {
    if (out_fd < 0) return;
    lseek(out_fd, out_pos, SEEK_SET);
    char buf[16384];
    ssize_t n = read(out_fd, buf, sizeof(buf)-1);
    if (n <= 0) return;
    buf[n] = 0;
    char *line = buf, *next;
    while (line && *line) {
        next = strchr(line, '\n');
        if (next) *next = 0;
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = 0;
            char *sender = line;
            char *b64 = colon+1;
            if (strcmp(sender, nick) != 0 && strlen(sender) > 0) {
                unsigned char ct[MS*2], pt[MS];
                int clen = b64d(b64, ct, sizeof(ct)), pl;
                if (dec(ct, clen, pt, &pl) == 0 && pl > 0 && pl < MS) {
                    pt[pl] = 0;
                    addh(sender, (char*)pt, gcol(sender));
                }
            }
        }
        if (next) line = next+1; else break;
    }
    out_pos = lseek(out_fd, 0, SEEK_CUR);
}

int main() {
    struct passwd *pw = getpwuid(getuid());
    nick = pw ? pw->pw_name : "unknown";
    OpenSSL_add_all_algorithms();
    char pwb[256]; ask(pwb, sizeof(pwb));
    dv(pwb); sz(pwb, sizeof(pwb));
    printf("\033[2J\033[H\033[1;32mKey accepted.\033[0m\n"); sleep(1);
    FILE *f = fopen(OUT, "a"); if (f) fclose(f);
    tcgetattr(0, &oldterm);
    signal(SIGINT, bye); signal(SIGTERM, bye); signal(SIGWINCH, winch);
    struct termios nt = oldterm;
    nt.c_lflag &= ~(ICANON|ECHO); nt.c_cc[VMIN]=1; nt.c_cc[VTIME]=0;
    tcsetattr(0, TCSANOW, &nt);
    load_hist();
    if (hc == 0) addh("system", "Welcome!", 5);
    char in[MS] = ""; int ps = 0, input_dirty = 0;
    while (1) {
        check_new();
        if (need_full_redraw) draw_full(in);
        else if (input_dirty) { update_input(in); input_dirty = 0; }
        fd_set fs; FD_ZERO(&fs); FD_SET(0, &fs);
        struct timeval tv = {0, 100000};
        int ret = select(1, &fs, NULL, NULL, &tv);
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret > 0) {
            char ch; int rn = read(0, &ch, 1);
            if (rn <= 0) continue;
            if (ch == '\r' || ch == '\n') {
                if (ps > 0) {
                    in[ps] = 0;
                    if (strcmp(in, "/quit") == 0) bye(0);
                    if (strcmp(in, "/who") == 0) { sw(); in[0]=0; ps=0; need_full_redraw=1; continue; }
                    unsigned char ct[MS*2]; int clen;
                    if (enc((unsigned char*)in, ps, ct, &clen) == 0) {
                        char b64[MS*3]; int bl = b64e(ct, clen, b64, sizeof(b64)); b64[bl]=0;
                        FILE *p = fopen(PIPE, "a");
                        if (p) { fprintf(p, "%s:%s\n", nick, b64); fclose(p); addh(nick, in, gcol(nick)); }
                        else addh("system", "Pipe error", 1);
                    }
                    sz(in, sizeof(in)); ps = 0; input_dirty = 1;
                }
            } else if (ch == 127 || ch == 8) {
                if (ps > 0) { in[--ps] = 0; input_dirty = 1; }
            } else if (ch >= 32 && ps < MS-1) {
                in[ps++] = ch; in[ps] = 0; input_dirty = 1;
            }
        }
    }
    bye(0);
}
