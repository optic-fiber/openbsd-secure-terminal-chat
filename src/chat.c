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

#define CHAT_PIPE "/tmp/chat-pipe"
#define CHAT_OUT  "/tmp/chat-output"
#define CHAT_LOG  "/tmp/chat.log"
#define MAX_MSG   512
#define MAX_HISTORY 500

char *nick;
int rows, cols;
long outfile_pos = 0;

char history[MAX_HISTORY][MAX_MSG];
int history_count = 0;
int need_redraw = 1;

char *colors[] = {
    "\033[31m","\033[32m","\033[33m","\033[34m",
    "\033[35m","\033[36m","\033[91m","\033[92m",
    "\033[93m","\033[94m","\033[95m","\033[96m"
};
char *reset = "\033[0m";
int color_count = 12;

struct termios old_term;

void restore_and_exit(int sig) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    printf("\033[2J\033[H");
    printf("Goodbye, %s!\n", nick);
    _exit(0);
}

int get_color(const char *user) {
    int hash = 0;
    for (int i = 0; user[i]; i++) hash = (hash * 31 + user[i]) % color_count;
    return hash;
}

void add_history(const char *sender, const char *text, int color) {
    if (history_count >= MAX_HISTORY) {
        for (int i = 1; i < MAX_HISTORY; i++)
            strlcpy(history[i-1], history[i], MAX_MSG);
        history_count = MAX_HISTORY - 1;
    }
    snprintf(history[history_count], MAX_MSG, "%s%s>%s %s", colors[color], sender, reset, text);
    history_count++;
    need_redraw = 1;
}

void load_history() {
    FILE *log = fopen(CHAT_LOG, "r");
    if (!log) return;
    rewind(log);
    char line[MAX_MSG];
    while (fgets(line, MAX_MSG, log)) {
        char *space = strchr(line, ' ');
        if (!space) continue;
        char *msg_start = space + 1;
        char *colon = strchr(msg_start, ':');
        if (!colon) continue;
        *colon = '\0';
        char *sender = msg_start;
        char *text = colon + 1;
        while (*text == ' ') text++;
        char *nl = strchr(text, '\n'); if (nl) *nl = '\0';
        add_history(sender, text, get_color(sender));
    }
    fclose(log);
}

void get_term_size() {
    struct winsize w;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &w);
    rows = w.ws_row;
    cols = w.ws_col;
    if (cols < 40) cols = 80;
    if (rows < 10) rows = 24;
}

void draw_centered(const char *str) {
    int len = strlen(str);
    int pad = (cols - len) / 2;
    if (pad < 0) pad = 0;
    printf("\033[1;36m%*s%s\033[0m\n", pad, "", str);
}

void draw_line(char c) {
    printf("\033[1;36m");
    for (int i = 0; i < cols; i++) printf("%c", c);
    printf("\033[0m\n");
}

void draw_screen(const char *input) {
    if (!need_redraw) return;
    need_redraw = 0;
    get_term_size();
    printf("\033[2J\033[H");
    draw_centered("  ____ _           _   ");
    draw_centered(" / ___| |__   __ _| |_ ");
    draw_centered("| |   | '_ \\ / _` | __|");
    draw_centered("| |___| | | | (_| | |_ ");
    draw_centered(" \\____|_| |_|\\__,_|\\__|");
    draw_line('-');
    int input_row = rows - 2;
    int visible = input_row - 7;
    if (visible < 0) visible = 0;
    int start = history_count - visible;
    if (start < 0) start = 0;
    for (int i = start; i < history_count; i++) printf("%s\n", history[i]);
    for (int i = history_count - start; i < visible; i++) printf("\n");
    draw_line('-');
    printf("\033[1;32m%s>\033[0m %s", nick, input);
    fflush(stdout);
}

void check_new() {
    FILE *f = fopen(CHAT_OUT, "r");
    if (!f) return;
    fseek(f, outfile_pos, SEEK_SET);
    char line[MAX_MSG];
    int had_new = 0;
    while (fgets(line, MAX_MSG, f)) {
        outfile_pos = ftell(f);
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *sender = line;
        char *text = colon + 1;
        while (*text == ' ') text++;
        char *nl = strchr(text, '\n'); if (nl) *nl = '\0';
        if (strcmp(sender, nick) != 0) {
            add_history(sender, text, get_color(sender));
            had_new = 1;
        }
    }
    fclose(f);
    if (had_new) need_redraw = 1;
}

int main() {
    struct passwd *pw = getpwuid(getuid());
    nick = pw ? pw->pw_name : "unknown";
    FILE *f = fopen(CHAT_OUT, "a"); if (f) fclose(f);
    tcgetattr(STDIN_FILENO, &old_term);
    signal(SIGINT, restore_and_exit);
    signal(SIGTERM, restore_and_exit);
    struct termios new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    load_history();
    if (history_count == 0) add_history("system", "Welcome to the chat!", 5);
    char input[MAX_MSG] = "";
    int pos = 0;
    while (1) {
        check_new();
        draw_screen(input);
        fd_set fds;
        FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 100000};
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret > 0) {
            char ch;
            int n = read(STDIN_FILENO, &ch, 1);
            if (n <= 0) continue;
            if (ch == '\r' || ch == '\n') {
                if (pos > 0) {
                    input[pos] = '\0';
                    if (strcmp(input, "/quit") == 0) restore_and_exit(0);
                    FILE *p = fopen(CHAT_PIPE, "a");
                    if (p) { fprintf(p, "%s:%s\n", nick, input); fclose(p); }
                    add_history(nick, input, get_color(nick));
                    input[0] = '\0'; pos = 0;
                    need_redraw = 1;
                }
            } else if (ch == 127 || ch == 8) {
                if (pos > 0) { input[--pos] = '\0'; need_redraw = 1; }
            } else if (ch >= 32 && pos < MAX_MSG - 1) {
                input[pos++] = ch; input[pos] = '\0';
                need_redraw = 1;
            }
        }
    }
    restore_and_exit(0);
    return 0;
}
