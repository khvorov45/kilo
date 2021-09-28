#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig {
    int screen_rows;
    int screen_cols;
    struct termios orig_termios;
};

struct editorConfig Editor;

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    printf("\r");
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &Editor.orig_termios)) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &Editor.orig_termios)) {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = Editor.orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // NOTE(sen) `read` timeout
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // NOTE(sen) Tenth of a second

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) {
        die("tcsetattr");
    }
}

char editorReadKey() {
    int nread;
    char ch;
    while ((nread = read(STDIN_FILENO, &ch, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) { die("read"); }
    }
    return ch;
}

int getCursorPosition(int* rows, int* cols) {
    int result = -1;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) == 4) {
        char buf[32];
        unsigned int index = 0;
        while (index < sizeof(buf) - 1) {
            if (read(STDIN_FILENO, buf + index, 1) != 1) {
                break;
            }
            if (buf[index] == 'R') {
                break;
            }
            index++;
        }
        buf[index] = '\0';
        if (buf[0] == '\x1b' || buf[1] == '[') {
            if (sscanf(buf + 2, "%d;%d", rows, cols) == 2) {
                result = 0;
            }
        }
    }
    return result;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;
    int result = -1;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        result = 0;
    } else if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) == 12) {
        result = getCursorPosition(rows, cols);
    }
    return result;
}

void editorProcessKeyPress() {
    char ch = editorReadKey();
    switch (ch) {
    case CTRL_KEY('q'): {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    } break;
    }
}

void editorDrawRows() {
    for (int row_index = 0; row_index < Editor.screen_rows; row_index++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}

void initEditor() {
    if (getWindowSize(&Editor.screen_rows, &Editor.screen_cols) == -1) {
        die("getWindowSize");
    }
}

int main() {
    enableRawMode();
    initEditor();

    for (;;) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}
