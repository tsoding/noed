#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_ESC_SEQ_LEN 32

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
#define ASSERT(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: ASSERTION FAILED: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
            exit(1); \
        } \
    } while (0)

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
} Data;

#define ITEMS_INIT_CAPACITY (10*1024)

#define da_append(da, item) do {                                                       \
    if ((da)->count >= (da)->capacity) {                                               \
        (da)->capacity = (da)->capacity == 0 ? ITEMS_INIT_CAPACITY : (da)->capacity*2; \
        (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items));       \
        ASSERT((da)->items != NULL, "Buy more RAM lol");                               \
    }                                                                                  \
    (da)->items[(da)->count++] = (item);                                               \
} while (0)

#define da_reserve(da, desired_capacity) do {                                   \
   if ((da)->capacity < desired_capacity) {                                     \
       (da)->capacity = desired_capacity;                                       \
       (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
       ASSERT((da)->items != NULL, "Buy more RAM lol");                         \
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

    // This has an interesting consequence of e->lines always having at least
    // one line even if e->data.count == 0. A lot of code depends on that assumption.
    // We need to be careful if we ever break it.
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
    if (e->cursor > e->data.count) e->cursor = e->data.count;
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
    ASSERT(e->cursor <= e->data.count, "cursor: %zu, size: %zu", e->cursor, e->data.count);
    ASSERT(e->lines.count >= 1, "editor_recompute_lines() guarantees there there is at least one line. Make sure you called it.");
    for (size_t i = 0; i < e->lines.count; ++i) {
        if (e->lines.items[i].begin <= e->cursor && e->cursor <= e->lines.items[i].end) {
            return i;
        }
    }
    return 0;
}

typedef struct {
    char *chars;
    size_t cursor_row, cursor_col;
    size_t rows, cols;
} Display;

void editor_rerender(Editor *e, bool insert, Display *d)
{
    const char *insert_label = "-- INSERT --";

    for (size_t i = 0; i < d->rows*d->cols; ++i) {
        d->chars[i] = ' ';
    }

    size_t rows = d->rows;
    size_t cols = d->cols;

    if (rows < 2 || cols < strlen(insert_label)) return;

    rows -= 1;

    size_t cursor_row = editor_current_line(e);
    size_t cursor_col = e->cursor - e->lines.items[cursor_row].begin;
    if (cursor_row < e->view_row) {
        e->view_row = cursor_row;
    }
    if (cursor_row >= e->view_row + rows) {
        e->view_row = cursor_row - rows + 1;
    }

    if (cursor_col < e->view_col) {
        e->view_col = cursor_col;
    }
    if (cursor_col >= e->view_col + cols) {
        e->view_col = cursor_col - cols + 1;
    }

    for (size_t i = 0; i < rows; ++i) {
        size_t row = e->view_row + i;
        if (row < e->lines.count) {
            const char *line_start = e->data.items + e->lines.items[row].begin;
            size_t line_size = e->lines.items[row].end - e->lines.items[row].begin;
            size_t view_col = e->view_col;
            if (view_col > line_size) view_col = line_size;
            line_start += view_col;
            line_size -= view_col;
            if (line_size > cols) line_size = cols;
            memcpy(d->chars + i*d->cols, line_start, line_size);
        } else {
            memcpy(d->chars + i*d->cols, "~", 1);
        }
    }

    if (insert) {
        memcpy(d->chars + rows*d->cols, insert_label, strlen(insert_label));
    }

    if (cursor_col > cols) cursor_col = cols;
    d->cursor_row = cursor_row - e->view_row;
    d->cursor_col = cursor_col;
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

void editor_move_char_right(Editor *e)
{
    if (e->cursor < e->data.count) e->cursor += 1;
}

void editor_move_char_left(Editor *e)
{
    if (e->cursor > 0) e->cursor -= 1;
}

void editor_move_line_down(Editor *e)
{
    size_t line = editor_current_line(e);
    size_t column = e->cursor - e->lines.items[line].begin;
    if (line > 0) {
        e->cursor = e->lines.items[line - 1].begin + column;
        if (e->cursor > e->lines.items[line - 1].end) {
            e->cursor = e->lines.items[line - 1].end;
        }
    }
}

void editor_move_line_up(Editor *e)
{
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
}

void editor_move_word_left(Editor *e)
{
    while (0 < e->cursor && e->cursor < e->data.count && !isalnum(e->data.items[e->cursor])) {
        e->cursor -= 1;
    }
    while (0 < e->cursor && e->cursor < e->data.count && isalnum(e->data.items[e->cursor])) {
        e->cursor -= 1;
    }
}

void editor_move_word_right(Editor *e)
{
    while (0 <= e->cursor && e->cursor < e->data.count - 1 && !isalnum(e->data.items[e->cursor])) {
        e->cursor += 1;
    }
    while (0 <= e->cursor && e->cursor < e->data.count - 1 && isalnum(e->data.items[e->cursor])) {
        e->cursor += 1;
    }
}

void editor_move_paragraph_up(Editor *e)
{
    size_t row = editor_current_line(e);
    while (row > 0 && (e->lines.items[row].end - e->lines.items[row].begin) == 0) {
        row -= 1;
    }
    while (row > 0 && (e->lines.items[row].end - e->lines.items[row].begin) > 0) {
        row -= 1;
    }
    e->cursor = e->lines.items[row].begin;
}

void editor_move_paragraph_down(Editor *e)
{
    size_t row = editor_current_line(e);
    while (row < e->lines.count - 1 && (e->lines.items[row].end - e->lines.items[row].begin) == 0) {
        row += 1;
    }
    while (row < e->lines.count - 1 && (e->lines.items[row].end - e->lines.items[row].begin) > 0) {
        row += 1;
    }
    e->cursor = e->lines.items[row].begin;
}

void editor_move_to_buffer_start(Editor *e)
{
    e->cursor = 0;
}

void editor_move_to_buffer_end(Editor *e)
{
    e->cursor = e->data.count;
}

void editor_move_to_line_start(Editor *e)
{
    size_t row = editor_current_line(e);
    e->cursor = e->lines.items[row].begin;
}

void editor_move_to_line_end(Editor *e)
{
    size_t row = editor_current_line(e);
    e->cursor = e->lines.items[row].end;
}

void display_resize(Display *d)
{
    struct winsize w;
    int err = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    ASSERT(err == 0, "All the necessary checks to make sure this works should've been done beforehand");
    d->rows = w.ws_row;
    d->cols = w.ws_col;
    d->chars = realloc(d->chars, d->rows*d->cols*sizeof(*d->chars));
    ASSERT(d->chars != NULL, "Buy more RAM lol");
}

void display_flush(FILE *target, Display *d)
{
    // TODO: efficient rerendering with patching and stuff
    // Might not be needed since the current method is already fast enough
    // to prevent flickering
    fprintf(target, "\033[H");
    fwrite(d->chars, sizeof(*d->chars), d->rows*d->cols, target);
    fprintf(target, "\033[%zu;%zuH", d->cursor_row + 1, d->cursor_col + 1);
    fflush(target);
}

void display_free_buffers(Display *d)
{
    free(d->chars);
    d->chars = 0;
}

int editor_start_interactive(Editor *e, const char *file_path)
{
    int result = 0;

    Display d = {0};
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
    display_resize(&d);
    while (!quit) {
        editor_rerender(e, insert, &d);
        display_flush(stdout, &d);

        char seq[MAX_ESC_SEQ_LEN] = {0};
        errno = 0;
        int seq_len = read(STDIN_FILENO, seq, sizeof(seq));
        if (errno == EINTR) {
            // Window got resized. Since SIGWINCH is the only signal that we
            // handle right now, there is no need to check if EINTR is caused
            // specifically by SIGWINCH. In the future it may change. But even
            // in the future I feel like just doing continue on EINTR regardless
            // of the signal is sufficient.
            display_resize(&d);
            continue;
        }
        if (errno > 0) {
            fprintf(stderr, "ERROR: something went wrong during reading of the user input: %s\n", strerror(errno));
            return_defer(1);
        }

        ASSERT(seq_len >= 0, "If there is no error, seq_len cannot be less than 0");
        if ((size_t) seq_len >= sizeof(seq)) {
            // Escape sequence is too big. Ignoring it.
            continue;
        }

        if (insert) {
            if (strcmp(seq, "\x1b ") == 0 || strcmp(seq, ES_ESCAPE) == 0) {
                insert = false;
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
            } else if (strcmp(seq, ES_ESCAPE" ") == 0 || strcmp(seq, " ") == 0) {
                insert = true;
            } else if (strcmp(seq, "s") == 0) {
                editor_move_line_up(e);
            } else if (strcmp(seq, "w") == 0) {
                editor_move_line_down(e);
            } else if (strcmp(seq, "a") == 0) {
                editor_move_char_left(e);
            } else if (strcmp(seq, "d") == 0) {
                editor_move_char_right(e);
            } else if (strcmp(seq, "k") == 0) {
                editor_move_word_left(e);
            } else if (strcmp(seq, ";") == 0) {
                editor_move_word_right(e);
            } else if (strcmp(seq, "o") == 0) {
                editor_move_paragraph_up(e);
            } else if (strcmp(seq, "l") == 0) {
                editor_move_paragraph_down(e);
            } else if (strcmp(seq, "O") == 0) {
                editor_move_to_buffer_start(e);
            } else if (strcmp(seq, "L") == 0) {
                editor_move_to_buffer_end(e);
            } else if (strcmp(seq, "K") == 0) {
                editor_move_to_line_start(e);
            } else if (strcmp(seq, ":") == 0) {
                editor_move_to_line_end(e);
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

    display_free_buffers(&d);

    return result;
}

char *shift_args(int *argc, char ***argv)
{
    ASSERT(*argc > 0, "Ran out of arguments to shift");
    char *result = **argv;
    (*argv)++;
    (*argc)--;
    return result;
}

bool decimal_string_as_uint64_with_overflow(const char *str, uint64_t *result)
{
    *result = 0;
    while (*str) {
        if (!isdigit(*str)) return false;
        *result *= 10;
        *result += *str - '0';
        str += 1;
    }
    return true;
}

void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [OPTIONS] <input.txt>\n", program);
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "    -gt <line-number>    go to the provided <line-number>\n");
}

int main(int argc, char **argv)
{
    int result = 0;
    Editor editor = {0};

    const char *program = shift_args(&argc, &argv);
    const char *file_path = NULL;
    uint64_t goto_line = 0;

    while (argc > 0) {
        const char *flag = shift_args(&argc, &argv);
        if (strcmp(flag, "-gt") == 0) {
            if (argc <= 0) {
                usage(program);
                fprintf(stderr, "ERROR: no value is provided for the flag %s\n", flag);
                return_defer(1);
            }
            const char *value = shift_args(&argc, &argv);
            if (!decimal_string_as_uint64_with_overflow(value, &goto_line)) {
                usage(program);
                fprintf(stderr, "ERROR: the value of %s is expected to be a non-negative integer\n", flag);
                return_defer(1);
            }
        } else {
            if (file_path != NULL) {
                usage(program);
                fprintf(stderr, "ERROR: editing multiple files is not supported yet\n");
                return_defer(1);
            }

            file_path = flag;
        }
    }

    if (file_path == NULL) {
        usage(program);
        fprintf(stderr, "ERROR: no input file is provided\n");
        return_defer(1);
    }

    if (!editor_open_file(&editor, file_path)) return_defer(1);
    if (goto_line >= editor.lines.count) {
        goto_line = editor.lines.count - 1;
    }
    editor.cursor = editor.lines.items[goto_line].begin;
    int exit_code = editor_start_interactive(&editor, file_path);
    return_defer(exit_code);

defer:
    editor_free_buffers(&editor);
    return result;
}

// TODO: incremental search
// TODO: goto line
// TODO: "save as.." prompt that allows you to type in the file path
// TODO: undo/redo
// TODO: word wrapping mode
// TODO: render non-displayable characters safely
//   So they do not modify the state of the terminal
// TODO: line numbers
// TODO: utf-8 support
//   - Make Data a collection of uint32_t instead of chars that stores unicode code points
//   - Encode/decode utf-8 on save/load
//   - ...
// TODO: Simple keywords highlighting
// TODO: The editor should be self-explorable:
//   - An ability to view what functions are bound to what keys from within the editor
//   - ...
