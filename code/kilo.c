// https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <stdint.h>

typedef int32_t i32;
typedef int64_t i64;
typedef uint64_t u32;
typedef uint64_t u64;
typedef int32_t b32;
typedef ssize_t isize;
typedef size_t usize;

#define function static
#define global static

#define CTRL_KEY(k) ((k) & 0x1f)

global char* KILO_VERSION = "0.0.1";

global struct termios OG_TERMINAL_SETTINGS;

typedef struct Row {
    i32 size;
    char* chars;
} Row;

typedef struct EditorState {
    i32 cursorX;
    i32 cursorY;
    i32 screenRows;
    i32 screenCols;
    i32 nRows;
    Row row;
} EditorState;

typedef struct AppendBuffer {
    char* buf;
    i32 len;
} AppendBuffer;

enum EditorKey {
    Key_ArrowLeft = 1000,
    Key_ArrowRight,
    Key_ArrowUp,
    Key_ArrowDown,
    Key_PageUp,
    Key_PageDown,
    Key_Home,
    Key_End,
    Key_Delete,
};

function void
die(char* message) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(message);
    printf("\r");
    exit(1);
}

function void
abAppend(AppendBuffer* ab, char* string, i32 len) {
    char* new = realloc(ab->buf, ab->len + len);
    if (new) {
        memcpy(&new[ab->len], string, len);
        ab->buf = new;
        ab->len += len;
    }
}

function void
abReset(AppendBuffer* ab) {
    memset(ab->buf, 0, ab->len);
    ab->len = 0;
}

function void
editorMoveCursor(EditorState* state, i32 key) {
    switch (key) {
    case Key_ArrowLeft: if (state->cursorX != 0) { state->cursorX--; } break;
    case Key_ArrowRight: if (state->cursorX != state->screenCols - 1) { state->cursorX++; } break;
    case Key_ArrowUp: if (state->cursorY != 0) { state->cursorY--; } break;
    case Key_ArrowDown: if (state->cursorY != state->screenRows - 1) { state->cursorY++; } break;
    }
}

function void
restoreOriginalTerminalSettings() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &OG_TERMINAL_SETTINGS)) {
        die("tcsetattr");
    }
}

i32
main(i32 argc, char* argv[]) {
    // NOTE(sen) Save the original settings
    if (tcgetattr(STDIN_FILENO, &OG_TERMINAL_SETTINGS)) {
        die("tcgetattr");
    }
    atexit(restoreOriginalTerminalSettings);

    // NOTE(sen) Change terminal settings
    {
        struct termios newTerminalSettings = OG_TERMINAL_SETTINGS;

        // NOTE(sen) Raw mode
        newTerminalSettings.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        newTerminalSettings.c_oflag &= ~(OPOST);
        newTerminalSettings.c_cflag |= CS8;
        newTerminalSettings.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

        // NOTE(sen) `read` timeout
        newTerminalSettings.c_cc[VMIN] = 0;
        newTerminalSettings.c_cc[VTIME] = 1; // NOTE(sen) Tenth of a second

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newTerminalSettings)) {
            die("tcsetattr");
        }
    }

    EditorState state = {};

    // NOTE(sen) Figure out window size
    {
        struct winsize ws;
        b32 success = 0;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0) {
            state.screenRows = ws.ws_row;
            state.screenCols = ws.ws_col;
            success = 1;
        } else if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) == 12) {
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
                    if (sscanf(buf + 2, "%d;%d", &state.screenRows, &state.screenCols) == 2) {
                        success = 1;
                    }
                }
            }
        }
        if (!success) {
            die("failed to get window size");
        }
    }

    // NOTE(sen) Read a line from a file
    if (argc > 1) {
        FILE* file = fopen(argv[1], "r");
        if (!file) { die("fopen"); }
        char* line = 0;
        usize linecap = 0;
        isize linelen = getline(&line, &linecap, file);
        if (linelen != -1) {
            // NOTE(sen) Trim the final newline
            while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
                --linelen;
            }
            state.row.size = linelen;
            state.row.chars = malloc(linelen + 1);
            memcpy(state.row.chars, line, linelen);
            state.row.chars[linelen] = '\0';
            state.nRows = 1;
        }
        free(line);
        fclose(file);
    }

    struct AppendBuffer appendBuffer = {};

    for (;;) {
        // NOTE(sen) Refresh screen
        {
            abAppend(&appendBuffer, "\x1b[?25l", 6); // NOTE(sen) Hide cursor
            abAppend(&appendBuffer, "\x1b[H", 3); // NOTE(sen) Move cursor to top-left

            // NOTE(sen) Draw rows
            for (int rowIndex = 0; rowIndex < state.screenRows; rowIndex++) {

                if (rowIndex < state.nRows) {
                    // NOTE(sen) Print the row that we have
                    i32 len = state.row.size;
                    if (len > state.screenRows) {
                        len = state.screenRows;
                    }
                    abAppend(&appendBuffer, state.row.chars, len);
                } else if (rowIndex == state.screenRows / 3 && state.nRows == 0) {
                    // NOTE(sen) Welcome message
                    char welcome[80];
                    int welcomeLen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
                    if (welcomeLen > state.screenCols) {
                        welcomeLen = state.screenCols;
                    }
                    int padding = (state.screenCols - welcomeLen) / 2;
                    if (padding) {
                        abAppend(&appendBuffer, "~", 1);
                        padding--;
                    }
                    while (padding--) { abAppend(&appendBuffer, " ", 1); };
                    abAppend(&appendBuffer, welcome, welcomeLen);
                } else {
                    // NOTE(sen) Empty row
                    abAppend(&appendBuffer, "~", 1);
                }

                // NOTE(sen) Clear row after the cursor
                abAppend(&appendBuffer, "\x1b[K", 3);

                // NOTE(sen) Make sure there is no newline on the last line
                if (rowIndex < state.screenRows - 1) {
                    abAppend(&appendBuffer, "\r\n", 2);
                }
            }

            // NOTE(sen) Move cursor to the appropriate position
            char buf[32];
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH", state.cursorY + 1, state.cursorX + 1);
            abAppend(&appendBuffer, buf, strlen(buf));

            abAppend(&appendBuffer, "\x1b[?25h", 6); // NOTE(sen) Show cursor

            write(STDOUT_FILENO, appendBuffer.buf, appendBuffer.len);

            abReset(&appendBuffer);
        }

        // NOTE(sen) Process key press
        {
            // NOTE(sen) Read the first character input
            i32 key = 0;
            {
                i32 nread;
                char ch;
                while ((nread = read(STDIN_FILENO, &ch, 1)) != 1) {
                    if (nread == -1 && errno != EAGAIN) { die("read"); }
                }
                key = ch;
            }

            // NOTE(sen) Process escape sequences
            if (key == '\x1b') {
                char seq[3];
                if (read(STDIN_FILENO, seq, 3) >= 2) {
                    if (seq[0] == '[') {
                        switch (seq[1]) {
                        case 'A': key = Key_ArrowUp; break;
                        case 'B': key = Key_ArrowDown; break;
                        case 'C': key = Key_ArrowRight; break;
                        case 'D': key = Key_ArrowLeft; break;
                        case 'H': key = Key_Home; break;
                        case 'F': key = Key_End; break;
                        case 'O': {
                            switch (seq[2]) {
                            case 'H': key = Key_Home; break;
                            case 'F': key = Key_End; break;
                            }
                        } break;
                        case '5': if (seq[2] == '~') { key = Key_PageUp; } break;
                        case '6': if (seq[2] == '~') { key = Key_PageDown; } break;
                        case '1': case '7': if (seq[2] == '~') { key = Key_Home; } break;
                        case '4': case '8': if (seq[2] == '~') { key = Key_End; } break;
                        case '3': if (seq[2] == '~') { key = Key_Delete; } break;
                        }
                    }
                }
            }

            // NOTE(sen) Respond to the key
            switch (key) {
            case CTRL_KEY('q'): {
                write(STDOUT_FILENO, "\x1b[2J", 4); // NOTE(sen) Clear screen
                write(STDOUT_FILENO, "\x1b[H", 3); // NOTE(sen) Move cursor to top-left
                exit(0);
            } break;
            case Key_ArrowUp: case Key_ArrowDown: case Key_ArrowLeft: case Key_ArrowRight: editorMoveCursor(&state, key); break;
            case Key_PageUp: case Key_PageDown: {
                i32 times = state.screenRows;
                while (times--) { editorMoveCursor(&state, key == Key_PageUp ? Key_ArrowUp : Key_ArrowDown); }
            } break;
            case Key_Home: state.cursorX = 0; break;
            case Key_End: state.cursorX = state.screenCols - 1; break;
            }
        }
    }

    return 0;
}
