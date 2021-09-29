#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
    Key_ArrowLeft = 1000,
    Key_ArrowRight,
    Key_ArrowUp,
    Key_ArrowDown,
};

struct EditorConfig {
    int cursorX;
    int cursorY;
    int screenRows;
    int screenCols;
    struct termios origTermios;
};

struct EditorConfig Editor;

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    printf("\r");
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &Editor.origTermios)) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &Editor.origTermios)) {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = Editor.origTermios;

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

int editorReadKey() {
    int nread;
    char ch;
    while ((nread = read(STDIN_FILENO, &ch, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) { die("read"); }
    }

    int result = ch;

    if (ch == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 2) == 2) {
            if (seq[0] == '[') {
                switch (seq[1]) {
                case 'A': result = Key_ArrowUp; break;
                case 'B': result = Key_ArrowDown; break;
                case 'C': result = Key_ArrowRight; break;
                case 'D': result = Key_ArrowLeft; break;
                }
            }
        }
    }

    return result;
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

struct AppendBuffer {
    char* buf;
    int len;
};

#define APPEND_BUFFER_INIT {NULL, 0}

void abAppend(struct AppendBuffer* ab, const char* string, int len) {
    char* new = realloc(ab->buf, ab->len + len);
    if (new) {
        memcpy(&new[ab->len], string, len);
        ab->buf = new;
        ab->len += len;
    }
}

void abFree(struct AppendBuffer* ab) {
    free(ab->buf);
}

void editorMoveCursor(int key) {
    switch (key) {
    case Key_ArrowLeft: Editor.cursorX--; break;
    case Key_ArrowRight: Editor.cursorX++; break;
    case Key_ArrowUp: Editor.cursorY--; break;
    case Key_ArrowDown: Editor.cursorY++; break;
    }
}

void editorProcessKeyPress() {
    int ch = editorReadKey();
    switch (ch) {
    case CTRL_KEY('q'): {
        write(STDOUT_FILENO, "\x1b[2J", 4); // NOTE(sen) Clear screen
        write(STDOUT_FILENO, "\x1b[H", 3); // NOTE(sen) Move cursor to top-left
        exit(0);
    } break;
    case Key_ArrowUp: case Key_ArrowDown: case Key_ArrowLeft: case Key_ArrowRight: editorMoveCursor(ch); break;
    }
}

void editorDrawRows(struct AppendBuffer* ab) {
    for (int rowIndex = 0; rowIndex < Editor.screenRows; rowIndex++) {
        if (rowIndex == Editor.screenRows / 3) {
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
            if (welcomeLen > Editor.screenCols) {
                welcomeLen = Editor.screenCols;
            }
            int padding = (Editor.screenCols - welcomeLen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) { abAppend(ab, " ", 1); };
            abAppend(ab, welcome, welcomeLen);
        } else {
            abAppend(ab, "~", 1);
        }
        abAppend(ab, "\x1b[K", 3); // NOTE(sen) Clear row after the cursor
        if (rowIndex < Editor.screenRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct AppendBuffer ab = APPEND_BUFFER_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // NOTE(sen) Hide cursor

    abAppend(&ab, "\x1b[H", 3); // NOTE(sen) Move cursor to top-left

    editorDrawRows(&ab);

    // NOTE(sen) Move cursor to the appropriate position
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", Editor.cursorY + 1, Editor.cursorX + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // NOTE(sen) Show cursor

    write(STDOUT_FILENO, ab.buf, ab.len);

    abFree(&ab);
}

void initEditor() {
    Editor.cursorX = 10;
    Editor.cursorY = 0;
    if (getWindowSize(&Editor.screenRows, &Editor.screenCols) == -1) {
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
