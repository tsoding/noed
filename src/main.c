#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// Escape Sequences
#define ES_ESCAPE "\x1b"
#define ES_BACKSPACE "\x7f"
#define ES_DELETE "\x1b\x5b\x33\x7e"

#define return_defer(value) do { result = (value); goto defer; } while(0)
#define UNUSED(x) (void)(x)
#define UNIMPLEMENTED(message) \
    do { \
        fprintf(stderr, "%s:%d: UNIMPLEMENTED: %s\n", __FILE__, __LINE__, message); \
        exit(1); \
    } while(0)

typedef struct {
    size_t begin;
    size_t end;
} Line;

typedef struct {
    Line *items;
    size_t count;
    size_t capacity;
} Lines;

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} Data; // ur mom

#define ITEMS_INIT_CAPACITY (10*1024)

#define da_append(da, item) do {                                                       \
    if ((da)->count >= (da)->capacity) {                                               \
        (da)->capacity = (da)->capacity == 0 ? ITEMS_INIT_CAPACITY : (da)->capacity*2; \
        (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items));       \
        assert((da)->items != NULL && "Buy more RAM lol");                             \
    }                                                                                  \
    (da)->items[(da)->count++] = (item);                                               \
} while (0)

#define da_reserve(da, desired_capacity) do {                                   \
   if ((da)->capacity < desired_capacity) {                                     \
       (da)->capacity = desired_capacity;                                       \
       (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
       assert((da)->items != NULL && "Buy more RAM lol");                       \
   }                                                                            \
} while(0)

typedef struct {
    // TODO: replace data with rope
    // I'm not sure if the rope is not gonna be overkill at this point.
    // Maybe we should introduce it gradually. Like first let's separate
    // the data into equal chunks, that we lookup with binary search. Then
    // see if it's sufficient.
    Data data;
    Lines lines;
    size_t cursor;
    size_t view_row;
    size_t view_col;
} Editor;

void editor_free_buffers(Editor *e)
{
    free(e->data.items);
    free(e->lines.items);
    e->data.items = NULL;
    e->lines.items = NULL;
}

// TODO: Line recomputation only based on what was changed.
//
// For example, if you changed one line, only that line and all of the consequent
// lines require recomputation. Any lines before the current line basically
// stay the same.
//
// We can even recompute them kinda lazily. We don't really need any lines after
// `e->view_row + w.ws_row - 1`. So we can only compute them as the view shifts down.
// We can clearly see that there are some uncomputed lines if
// `e->lines.items[e->lines.count - 1].end < e->data.count`
void editor_recompute_lines(Editor *e)
{
    e->lines.count = 0;

    size_t begin = 0;
    for (size_t i = 0; i < e->data.count; ++i) {
        if (e->data.items[i] == '\n') {
            da_append(&e->lines, ((Line) {
                .begin = begin,
                .end = i,
            }));
            begin = i + 1;
        }
    }

    da_append(&e->lines, ((Line) {
        .begin = begin,
        .end = e->data.count,
    }));
}

bool editor_open_file(Editor *e, const char *file_path)
{
    bool result = true;
    int fd = -1;

    e->data.count = 0;
    e->lines.count = 0;

    struct stat statbuf;
    if (stat(file_path, &statbuf) < 0) {
        if (errno == ENOENT) {
            return_defer(true);
        } else {
            fprintf(stderr, "ERROR: could not determine if file %s exists\n", file_path);
            return_defer(false);
        }
    }

    if ((statbuf.st_mode & S_IFMT) != S_IFREG) {
        fprintf(stderr, "ERROR: %s is not a regular file\n", file_path);
        return_defer(false);
    }

    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ERROR: could not open file %s: %s\n", file_path, strerror(errno));
        return_defer(false);
    }

    size_t file_size = statbuf.st_size;
    da_reserve(&e->data, file_size);

    ssize_t n = read(fd, e->data.items, file_size);
    if (n < 0) {
        fprintf(stderr, "ERROR: could not read file %s: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
    while ((size_t) n < file_size) {
        ssize_t m = read(fd, e->data.items + n, file_size - n);
        if (m < 0) {
            fprintf(stderr, "ERROR: could not read file %s: %s\n", file_path, strerror(errno));
            return_defer(false);
        }
        n += m;
    }

    e->data.count = n;

defer:
    if (result) editor_recompute_lines(e);
    if (fd >= 0) close(fd);
    return result;
}

void editor_insert_char(Editor *e, char x)
{
    da_append(&e->data, '\0');
    memmove(&e->data.items[e->cursor + 1], &e->data.items[e->cursor], e->data.count - 1 - e->cursor);
    e->data.items[e->cursor] = x;
    e->cursor += 1;
    editor_recompute_lines(e);
}

void editor_delete_char(Editor *e)
{
    if (e->cursor < e->data.count) {
        memmove(&e->data.items[e->cursor], &e->data.items[e->cursor + 1], e->data.count - e->cursor - 1);
        e->data.count -= 1;
        editor_recompute_lines(e);
    }
}

void editor_backdelete_char(Editor *e)
{
    if (0 < e->cursor && e->cursor <= e->data.count) {
        memmove(&e->data.items[e->cursor - 1], &e->data.items[e->cursor], e->data.count - e->cursor);
        e->data.count -= 1;
        e->cursor -= 1;
        editor_recompute_lines(e);
    }
}

size_t editor_current_line(const Editor *e)
{
    assert(e->cursor <= e->data.count);
    for (size_t i = 0; i < e->lines.count; ++i) {
        if (e->lines.items[i].begin <= e->cursor && e->cursor <= e->lines.items[i].end) {
            return i;
        }
    }
    return 0;
}

void editor_rerender(Editor *e, bool insert)
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    printf("\033[2J\033[H");

    const char *insert_label = "-- INSERT --";

    if (w.ws_row < 2 || w.ws_col < strlen(insert_label)) return;

    w.ws_row -= 1;

    size_t cursor_row = editor_current_line(e);
    size_t cursor_col = e->cursor - e->lines.items[cursor_row].begin;
    if (cursor_row < e->view_row) {
        e->view_row = cursor_row;
    }
    if (cursor_row >= e->view_row + w.ws_row) {
        e->view_row = cursor_row - w.ws_row + 1;
    }

    if (cursor_col < e->view_col) {
        e->view_col = cursor_col;
    }
    if (cursor_col >= e->view_col + w.ws_col) {
        e->view_col = cursor_col - w.ws_col + 1;
    }

    for (size_t i = 0; i < w.ws_row; ++i) {
        printf("\033[%zu;%dH", i + 1, 1);
        size_t row = e->view_row + i;
        if (row < e->lines.count) {
            const char *line_start = e->data.items + e->lines.items[row].begin;
            size_t line_size = e->lines.items[row].end - e->lines.items[row].begin;
            size_t view_col = e->view_col;
            if (view_col > line_size) view_col = line_size;
            line_start += view_col;
            line_size -= view_col;
            if (line_size > w.ws_col) line_size = w.ws_col;
            fwrite(line_start, sizeof(*e->data.items), line_size, stdout);
        } else {
            fputs("~", stdout);
        }
    }

    if (insert) printf("\033[%d;%dH%s", w.ws_row + 1, 1, insert_label);

    if (cursor_col > w.ws_col) cursor_col = w.ws_col;
    printf("\033[%zu;%zuH", (cursor_row - e->view_row) + 1, cursor_col + 1);
    fflush(stdout);
}

bool editor_save_to_file(Editor *e, const char *file_path)
{
    bool result = true;
    int fd = open(file_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        // TODO: we need to log something here,
        // but we can't easily do that if we are already in the special terminal mode with
        // no ECHO and no ICANON (it will just look weird).
        //
        // Maybe we could start bubbling up errors through return values like in Go? But that may
        // require dynamic memory management.
        //
        // We can just create a dedicated arena for the bubbling errors. I had this idea for quite some
        // time already, maybe we can test it in here.
        fprintf(stderr, "ERROR: could not open file %s for writing: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
    ssize_t n = write(fd, e->data.items, e->data.count);
    if (n < 0) {
        fprintf(stderr, "ERROR: could not write into file %s: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
    while ((size_t) n < e->data.count) {
        ssize_t m = write(fd, e->data.items + n, e->data.count - n);
        if (m < 0) {
            fprintf(stderr, "ERROR: could not write into file %s: %s\n", file_path, strerror(errno));
            return_defer(false);
        }
        n += m;
    }
defer:
    if (fd >= 0) UNUSED(close(fd));
    return result;
}

void window_resize_signal(int signal)
{
    UNUSED(signal);
}

bool is_display(char x)
{
    return ' ' <= x && x <= '~';
}

int editor_start_interactive(Editor *e, const char *file_path)
{
    int result = 0;
    bool terminal_prepared = false;
    bool signals_prepared = false;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "ERROR: Please run the editor in the terminal!\n");
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

    struct sigaction act, old = {0};
    act.sa_handler = window_resize_signal;
    if (sigaction(SIGWINCH, &act, &old) < 0) {
        fprintf(stderr, "ERROR: could not set up window resize signal: %s\n", strerror(errno));
        return_defer(1);
    }

    signals_prepared = true;

    bool quit = false;
    bool insert = false;
    while (!quit) {
        editor_rerender(e, insert);

        // TODO: what's the biggest escape sequence?
        // Or maybe we can try to read until we get EAGAIN?
        // That way the max size of the sequence does not really matter
        char seq[32] = {0};
        errno = 0;
        int seq_len = read(STDIN_FILENO, seq, sizeof(seq));
        if (errno == EINTR) {
            // Window got resized. Since SIGWINCH is the only signal that we handle right now, there is no need to
            // check if EINTR is caused specifically by SIGWINCH. In the future it may change. But even in the future
            // I feel like just doing continue on EINTR regardless of the signal is sufficient.
            continue;
        }
        if (errno > 0) {
            fprintf(stderr, "ERROR: something went wrong during reading of the user input: %s\n", strerror(errno));
            return_defer(1);
        }

        assert(seq_len >= 0);
        assert((size_t) seq_len < sizeof(seq));

        if (insert) {
            if (strcmp(seq, ES_ESCAPE) == 0) {
                insert = false;
                // TODO: proper saving.
                // Probably by pressing something in the command mode.
                editor_save_to_file(e, file_path);
            } else if (strcmp(seq, ES_BACKSPACE) == 0) {
                editor_backdelete_char(e);
            } else if (strcmp(seq, ES_DELETE) == 0) {
                editor_delete_char(e);
            } else if (strcmp(seq, "\n") == 0) {
                editor_insert_char(e, '\n');
            } else if (seq_len == 1 && is_display(seq[0])) {
                editor_insert_char(e, seq[0]);
            }
        } else {
            if (strcmp(seq, "q") == 0) {
                quit = true;
            } else if (strcmp(seq, "e") == 0) {
                insert = true;
            } else if (strcmp(seq, "s") == 0) {
                // TODO: preserve the column when moving up and down
                // Right now if the next line is shorter the current column value is clamped and lost.
                // Maybe cursor should be a pair (row, column) instead?
                size_t line = editor_current_line(e);
                size_t column = e->cursor - e->lines.items[line].begin;
                if (line < e->lines.count - 1) {
                    e->cursor = e->lines.items[line + 1].begin + column;
                    if (e->cursor > e->lines.items[line + 1].end) {
                        e->cursor = e->lines.items[line + 1].end;
                    }
                }
            } else if (strcmp(seq, "w") == 0) {
                size_t line = editor_current_line(e);
                size_t column = e->cursor - e->lines.items[line].begin;
                if (line > 0) {
                    e->cursor = e->lines.items[line - 1].begin + column;
                    if (e->cursor > e->lines.items[line - 1].end) {
                        e->cursor = e->lines.items[line - 1].end;
                    }
                }
            } else if (strcmp(seq, "a") == 0) {
                if (e->cursor > 0) e->cursor -= 1;
            } else if (strcmp(seq, "d") == 0) {
                if (e->cursor < e->data.count) e->cursor += 1;
            } else if (strcmp(seq, ES_DELETE) == 0) {
                editor_delete_char(e);
            } else if (strcmp(seq, ES_BACKSPACE) == 0) {
                editor_backdelete_char(e);
            } else if (strcmp(seq, "\n") == 0) {
                editor_insert_char(e, '\n');
            }
        }
    }

defer:
    if (signals_prepared) {
        UNUSED(sigaction(SIGWINCH, &old, NULL));
    }

    if (terminal_prepared) {
        printf("\033[2J\033[H");
        term.c_lflag |= ECHO;
        term.c_lflag |= ICANON;
        UNUSED(tcsetattr(STDIN_FILENO, 0, &term));
    }
    return result;
}

int main(int argc, char **argv)
{
    int result = 0;
    Editor editor = {0};

    if (argc < 2) {
        fprintf(stderr, "Usage: noed <input.txt>\n");
        fprintf(stderr, "ERROR: no input file is provided\n");
        return_defer(1);
    }

    const char *file_path = argv[1];
    editor_open_file(&editor, file_path);
    int exit_code = editor_start_interactive(&editor, file_path);
    return_defer(exit_code);

defer:
    editor_free_buffers(&editor);
    return result;
}

// TODO: there is flickering when running without tmux
// TODO: undo/redo
// TODO: word wrapping mode
// TODO: render non-displayable characters safely
// So they do not modify the state of the terminal
// TODO: line numbers
// TODO: utf-8 support
// - Make Data a collection of uint32_t instead of chars that stores unicode code points
// - Encode/decode utf-8 on save/load
// - ...
// TODO: Simple keywords highlighting
