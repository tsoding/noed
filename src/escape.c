// Simple program that prints escape sequences from stdin
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <termios.h>
#include <unistd.h>

#define return_defer(value) do { result = (value); goto defer; } while(0)

int main(void)
{
    int result = 0;
    bool terminal_prepared = false;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "ERROR: Please run the program in the terminal!\n");
        return_defer(1);
    }
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) < 0) {
        fprintf(stderr, "ERROR: could not get the state of the terminal: %s\n", strerror(errno));
        return_defer(1);
    }

    term.c_lflag &= ~ECHO;
    term.c_lflag &= ~ICANON;
    if (tcsetattr(0, 0, &term)) {
        fprintf(stderr, "ERROR: could not update the state of the terminal: %s\n", strerror(errno));
        return_defer(1);
    }

    terminal_prepared = true;

    bool quit = false;
    while (!quit) {
        // TODO: what's the biggest escape sequence?
        // Or maybe we can try to read until we get EAGAIN?
        // That way the max size of the sequence does not really matter
        char seq[32];
        memset(seq, 0, sizeof(seq));
        int ret = read(STDIN_FILENO, seq, sizeof(seq));
        if (ret < 0) {
            fprintf(stderr, "ERROR: something went wrong during reading user input: %s\n", strerror(errno));
            return_defer(1);
        }
        assert(ret < 32);
        printf("\"");
        for (int i = 0; i < ret; ++i) {
            printf("\\x%02x", seq[i]);
        }
        printf("\"\n");
    }

defer:
    if (terminal_prepared) {
        printf("\033[2J");
        term.c_lflag |= ECHO;
        term.c_lflag |= ICANON;
        tcsetattr(STDIN_FILENO, 0, &term);
    }
    return result;
}
