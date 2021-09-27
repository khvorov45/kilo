#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // NOTE(sen) `read` timeout
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // NOTE(sen) Tenth of a second

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    for (;;) {
        char ch = '\0';
        read(STDIN_FILENO, &ch, 1);
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
