#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void die(const char* s) {
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios)) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios)) {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = orig_termios;

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

int main() {
    enableRawMode();

    for (;;) {
        char ch = '\0';
        if (read(STDIN_FILENO, &ch, 1) == -1 && errno != EAGAIN) {
            die("read");
        }
        if (iscntrl(ch)) {
            printf("%d\r\n", ch);
        } else {
            printf("%d, ('%c')\r\n", ch, ch);
        }
        if (ch == 'q') {
            break;
        }
    }

    return 0;
}
