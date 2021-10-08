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

#define assert(condition) if (!(condition)) __builtin_trap()

#define CTRL_KEY(k) ((k) & 0x1f)

global char* KILO_VERSION = "0.0.1";

global struct termios OG_TERMINAL_SETTINGS;

typedef struct Row {
    i32 charsSize;
    char* fileChars;
    i32 renderSize;
    char* renderChars;
} Row;

typedef struct EditorState {
    i32 cursorFileX;
    i32 cursorRenderX;
    i32 cursorY;
    i32 screenRows;
    i32 screenCols;
    i32 rowOffset;
    i32 colOffset;
    i32 nRows;
    Row* rows;
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

function i32
clamp(i32 value, i32 min, i32 max) {
    i32 result = value;
    if (value < min) {
        result = min;
    } else if (value > max) {
        result = max;
    }
    return result;
}

function void
makeCursorXValidAfterRowChange(EditorState* state, char tabChar, i32 replacementsPerTab) {
    i32 closestValidRenderOffset = 0;
    i32 closestValidFileOffset = 0;
    if (state->cursorY < state->nRows) {
        i32 renderIndex = 0;
        Row* row = state->rows + state->cursorY;
        for (i32 charIndex = 0; charIndex <= row->charsSize; charIndex++) {
            if (abs(state->cursorRenderX - renderIndex) <= abs(state->cursorRenderX - closestValidRenderOffset)) {
                closestValidRenderOffset = renderIndex;
                closestValidFileOffset = charIndex;
            } else {
                break;
            }
            if (charIndex == row->charsSize) {
                break;
            }
            if (row->fileChars[charIndex] == tabChar) {
                renderIndex += replacementsPerTab;
            } else {
                renderIndex++;
            }
        }
    }
    state->cursorRenderX = closestValidRenderOffset;
    state->cursorFileX = closestValidFileOffset;
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
        // NOTE(sen) Make room for the status bar and user message
        state.screenRows -= 1;
    }

    // NOTE(sen) Read file
    char tabChar = '\t';
    char tabReplacement = ' ';
    i32 replacementsPerTab = 8;
    char* filename = '\0';
    if (argc > 1) {
        filename = argv[1];
        FILE* file = fopen(filename, "r");
        if (!file) { die("fopen"); }
        char* line = 0;
        usize linecap = 0;
        i32 linelen;
        while ((linelen = getline(&line, &linecap, file)) != -1) {
            // NOTE(sen) Trim the final newline
            while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
                --linelen;
            }
            // NOTE(sen) Add a row
            i32 rowIndex = state.nRows++;
            state.rows = realloc(state.rows, state.nRows * sizeof(Row));
            Row* row = state.rows + rowIndex;
            // NOTE(sen) Copy the actual characters
            row->charsSize = linelen;
            row->fileChars = malloc(row->charsSize + 1);
            memcpy(row->fileChars, line, row->charsSize);
            row->fileChars[row->charsSize] = '\0';
            // NOTE(sen) Construct the render characters
            i32 nTabs = 0;
            for (i32 charIndex = 0; charIndex < row->charsSize; charIndex++) {
                if (row->fileChars[charIndex] == tabChar) {
                    nTabs++;
                }
            }
            row->renderSize = row->charsSize - nTabs + nTabs * replacementsPerTab;
            row->renderChars = malloc(row->renderSize + 1);
            i32 renderIndex = 0;
            for (i32 charIndex = 0; charIndex < row->charsSize; charIndex++) {
                char rowChar = row->fileChars[charIndex];
                if (rowChar == tabChar) {
                    for (i32 spaceIndex = 0; spaceIndex < replacementsPerTab; ++spaceIndex) {
                        row->renderChars[renderIndex++] = tabReplacement;
                    }
                } else {
                    row->renderChars[renderIndex++] = row->fileChars[charIndex];
                }
            }
            assert(renderIndex == row->renderSize);
        }
        free(line);
        fclose(file);
    }

    struct AppendBuffer appendBuffer = {};

    for (;;) {
        // NOTE(sen) Adjust offsets (scroll)
        {
            // NOTE(sen) Vertical
            assert(state.cursorY >= 0);
            assert(state.rowOffset >= 0);
            if (state.cursorY < state.rowOffset) {
                state.rowOffset = state.cursorY;
            } else if (state.cursorY >= state.rowOffset + state.screenRows) {
                state.rowOffset = state.cursorY - state.screenRows + 1;
            }
            // NOTE(sen) Horizontal
            assert(state.cursorRenderX >= 0);
            assert(state.colOffset >= 0);
            if (state.cursorRenderX < state.colOffset) {
                state.colOffset = state.cursorRenderX;
            } else if (state.cursorRenderX >= state.colOffset + state.screenCols) {
                state.colOffset = state.cursorRenderX - state.screenCols + 1;
            }
        }

        // NOTE(sen) Render
        {
            abAppend(&appendBuffer, "\x1b[?25l", 6); // NOTE(sen) Hide cursor
            abAppend(&appendBuffer, "\x1b[H", 3); // NOTE(sen) Move cursor to top-left

            // NOTE(sen) Draw rows
            for (int rowIndex = 0; rowIndex < state.screenRows; rowIndex++) {
                i32 fileRowIndex = rowIndex + state.rowOffset;
                if (fileRowIndex < state.nRows) {
                    // NOTE(sen) Print file rows
                    Row* row = state.rows + fileRowIndex;
                    if (row->renderSize > state.colOffset) {
                        i32 len = row->renderSize - state.colOffset;
                        if (len > state.screenCols) {
                            len = state.screenCols;
                        }
                        abAppend(&appendBuffer, row->renderChars + state.colOffset, len);
                    }
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
                }

                // NOTE(sen) Clear row after the cursor
                abAppend(&appendBuffer, "\x1b[K", 3);

                // NOTE(sen) Make sure there is no newline on the last line
                //if (rowIndex < state.screenRows - 1) {
                abAppend(&appendBuffer, "\r\n", 2);
                //}
            }

            // NOTE(sen) Draw status bar
            char status[80];
            i32 statusLen = snprintf(status, sizeof(status), "%.20s - %d lines", filename, state.nRows);
            if (statusLen > state.screenCols) {
                statusLen = state.screenCols;
            }
            i32 statusPad = state.screenCols - statusLen;
            // statusPad = 20;
            while (statusPad > 0) {
                abAppend(&appendBuffer, " ", 1);
                statusPad--;
            }
            abAppend(&appendBuffer, "\x1b[1m", 4); // NOTE(sen) Bold
            abAppend(&appendBuffer, status, statusLen);
            abAppend(&appendBuffer, "\x1b[m", 3); // NOTE(sen) Reset formatting

            abAppend(&appendBuffer, "\r\n", 2);

            // NOTE(sen) Draw user message
            char message[80];
            i32 messageLen = snprintf(message, sizeof(message), "HELP: Ctrl-Q = quit");
            if (messageLen > state.screenCols) {
                messageLen = state.screenCols;
            }
            i32 messagePadTotal = state.screenCols - messageLen;
            i32 messagePadSide = messagePadTotal / 2;
            i32 messagePad = messagePadSide;
            while (messagePad > 0) {
                abAppend(&appendBuffer, " ", 1);
                messagePad--;
            }
            abAppend(&appendBuffer, message, messageLen);
            messagePad = messagePadSide;
            while (messagePad > 0) {
                abAppend(&appendBuffer, " ", 1);
                messagePad--;
            }

            // NOTE(sen) Move cursor to the appropriate position
            char buf[32];
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH", state.cursorY - state.rowOffset + 1, state.cursorRenderX - state.colOffset + 1);
            abAppend(&appendBuffer, buf, strlen(buf));

            abAppend(&appendBuffer, "\x1b[?25h", 6); // NOTE(sen) Show cursor

            write(STDOUT_FILENO, appendBuffer.buf, appendBuffer.len);

            abReset(&appendBuffer);
        } // NOTE(sen) Render

        // NOTE(sen) Get input
        i32 key = 0;
        {
            // NOTE(sen) Read the first character input
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
        }

        // NOTE(sen) Respond to input
        switch (key) {

            // NOTE(sen) Quit
        case CTRL_KEY('q'): {
            write(STDOUT_FILENO, "\x1b[2J", 4); // NOTE(sen) Clear screen
            write(STDOUT_FILENO, "\x1b[H", 3); // NOTE(sen) Move cursor to top-left
            exit(0);
        } break;

            // NOTE(sen) Cursor move
        case Key_ArrowDown: {
            if (state.cursorY < state.nRows) {
                state.cursorY++;
                if (state.cursorY == state.nRows) {
                    state.cursorRenderX = 0;
                    state.cursorFileX = 0;
                } else {
                    makeCursorXValidAfterRowChange(&state, tabChar, replacementsPerTab);
                }
            }
        }; break;
        case Key_ArrowUp: {
            if (state.cursorY > 0) {
                state.cursorY--;
                makeCursorXValidAfterRowChange(&state, tabChar, replacementsPerTab);
            }
        }; break;
        case Key_ArrowRight: {
            if (state.cursorY < state.nRows) {
                if (state.cursorFileX == state.rows[state.cursorY].charsSize) {
                    state.cursorFileX = 0;
                    state.cursorRenderX = 0;
                    state.cursorY++;
                } else {
                    if (state.rows[state.cursorY].fileChars[state.cursorFileX] == tabChar) {
                        state.cursorRenderX += replacementsPerTab;
                    } else {
                        state.cursorRenderX++;
                    }
                    state.cursorFileX++;
                }
            }
        }; break;
        case Key_ArrowLeft: {
            if (state.cursorFileX == 0) {
                if (state.cursorY > 0) {
                    state.cursorY--;
                    state.cursorFileX = state.rows[state.cursorY].charsSize;
                    state.cursorRenderX = state.rows[state.cursorY].renderSize;
                }
            } else {
                if (state.rows[state.cursorY].fileChars[state.cursorFileX - 1] == tabChar) {
                    state.cursorRenderX -= replacementsPerTab;
                } else {
                    state.cursorRenderX--;
                }
                state.cursorFileX--;
            }
        }; break;
        case Key_PageDown: {
            i32 newY = state.cursorY + state.screenRows;
            state.cursorY = clamp(newY, 0, state.nRows);
            makeCursorXValidAfterRowChange(&state, tabChar, replacementsPerTab);
        }; break;
        case Key_PageUp: {
            i32 newY = state.cursorY - state.screenRows;
            state.cursorY = clamp(newY, 0, state.nRows);
            makeCursorXValidAfterRowChange(&state, tabChar, replacementsPerTab);
        }; break;
        case Key_Home: {
            state.cursorFileX = 0;
            state.cursorRenderX = 0;
        } break;
        case Key_End: {
            if (state.cursorY < state.nRows) {
                Row* row = state.rows + state.cursorY;
                state.cursorFileX = row->charsSize;
                state.cursorRenderX = row->renderSize;
            }
        } break;
        }

    } // NOTE(sen) Mainloop

    return 0;
}
