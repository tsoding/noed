#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <termios.h>

#include <unistd.h>

#define return_defer(value) do { result = (value); goto defer; } while(0)
#define UNUSED(x) (void)(x)

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
        assert((da)->items != NULL && "Buy more RAM lol");                             \
    }                                                                                  \
    (da)->items[(da)->count++] = (item);                                               \
} while (0)

#define da_reserve(da, desired_capacity) do {                                   \
   if ((da)->capacity < desired_capacity) {                                     \
       (da)->capacity = desired_capacity;                                       \
       (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
   }                                                                            \
} while(0)

typedef struct {
    // TODO: replace data with rope
    Data data;
    Lines lines;
    size_t cursor;
} Editor;

// TODO: line recomputation only based on what was changed
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

void editor_insert_char(Editor *e, char x)
{
    da_append(&e->data, '\0');
    memmove(&e->data.items[e->cursor + 1], &e->data.items[e->cursor], e->data.count - 1 - e->cursor);
    e->data.items[e->cursor] = x;
    e->cursor += 1;
    editor_recompute_lines(e);
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

void editor_rerender(const Editor *e, bool insert)
{
    printf("\033[2J\033[H");
    fwrite(e->data.items, sizeof(*e->data.items), e->data.count, stdout);
    printf("\n");
    // TODO: print the mode indicator on the bottom
    if (insert) printf("[INSERT]");
    size_t line = editor_current_line(e);
    printf("\033[%zu;%zuH", line + 1, e->cursor - e->lines.items[line].begin + 1);
}

static Editor editor = {0};

bool editor_save_to_file(Editor *e, const char *file_path)
{
    bool result = true;
    FILE *f = fopen(file_path, "wb");
    if (f == NULL) {
        // TODO: we need to log something here,
        // but we can't easily do that if we are already in the special terminal mode with
        // no ECHO and no ICANON (it will just look weird).
        //
        // Maybe we could start bubbling up errors through return values like in Go? But that may
        // require dynamic memory management.
        //
        // We can just create a dedicated arena for the bubbling errors. I had this idea for quite some
        // time already, maybe we can test it in here.
        printf("ERROR: could not open file %s for writing: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
    fwrite(e->data.items, sizeof(*e->data.items), e->data.count, f);
    if (ferror(f)) {
        printf("ERROR: could not write into file %s: %s\n", file_path, strerror(errno));
        return_defer(false);
    }
defer:
    if (f) UNUSED(fclose(f));
    return result;
}

int editor_start_interactive(Editor *e, const char *file_path)
{
    int result = 0;
    bool terminal_prepared = false;

    // TODO: implement limited view and scrolling
    if (!isatty(0)) {
        fprintf(stderr, "ERROR: Please run the editor in the terminal!\n");
        return_defer(1);
    }

    struct termios term;
    if (tcgetattr(0, &term) < 0) {
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
    bool insert = false;
    while (!quit && !feof(stdin)) {
        // TODO: there is a flickering when run without tmux
        editor_rerender(e, insert);

        if (insert) {
            int x = fgetc(stdin);
            if (x == 27) {
                insert = false;
                // TODO: proper saving.
                // Probably by pressing something in the command mode.
                editor_save_to_file(e, file_path);
            } else {
                // TODO: allow inserting only printable ASCII
                editor_insert_char(&editor, x);
            }
        } else {
            int x = fgetc(stdin);
            switch (x) {
            case 'q': {
                // TODO: when the editor exists the shell prompt is shifted
                quit = true;
            }
            break;

            case 'e': {
                insert = true;
            }
            break;

            // TODO: preserve the column when moving up and down
            // Right now if the next line is shorter the current column value is clamped and lost.
            // Maybe cursor should be a pair (row, column) instead?
            // TODO: backspace delete
            case 's': {
                size_t line = editor_current_line(e);
                size_t column = e->cursor - e->lines.items[line].begin;
                if (line < e->lines.count - 1) {
                    e->cursor = e->lines.items[line + 1].begin + column;
                    if (e->cursor > e->lines.items[line + 1].end) {
                        e->cursor = e->lines.items[line + 1].end;
                    }
                }
            }
            break;

            case 'w': {
                size_t line = editor_current_line(e);
                size_t column = e->cursor - e->lines.items[line].begin;
                if (line > 0) {
                    e->cursor = e->lines.items[line - 1].begin + column;
                    if (e->cursor > e->lines.items[line - 1].end) {
                        e->cursor = e->lines.items[line - 1].end;
                    }
                }
            }
            break;

            case 'a': {
                if (editor.cursor > 0) editor.cursor -= 1;
            }
            break;

            case 'd': {
                if (editor.cursor < e->data.count) e->cursor += 1;
            }
            break;
            }
        }
    }

defer:
    if (terminal_prepared) {
        printf("\033[2J");
        term.c_lflag |= ECHO;
        tcsetattr(0, 0, &term);
    }
    return result;
}

int get_file_size(FILE *f, size_t *out)
{
    long saved = ftell(f);
    if (saved < 0) return errno;
    if (fseek(f, 0, SEEK_END) < 0) return errno;
    long size = ftell(f);
    if (size < 0) return errno;
    if (fseek(f, saved, SEEK_SET) < 0) return errno;
    *out = (size_t) size;
    return 0;
}

int main(int argc, char **argv)
{
    int result = 0;
    FILE *f = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: noed <input.txt>\n");
        fprintf(stderr, "ERROR: no input file is provided\n");
        return_defer(1);
    }

    const char *file_path = argv[1];

    f = fopen(file_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: could not open file %s: %s\n", file_path, strerror(errno));
        return_defer(1);
    }

    size_t file_size = 0;
    int err = get_file_size(f, &file_size);
    if (err != 0) {
        fprintf(stderr, "ERROR: could not determine the size of the file %s: %s\n", file_path, strerror(errno));
        return_defer(1);
    }
    da_reserve(&editor.data, file_size);

    size_t n = fread(editor.data.items, sizeof(*editor.data.items), file_size, f);
    while (n < file_size && !ferror(f)) {
        size_t m = fread(editor.data.items + n, sizeof(*editor.data.items), file_size - n, f);
        n += m;
    }
    if (ferror(f)) {
        fprintf(stderr, "ERROR: could not read file %s: %s\n", file_path, strerror(errno));
        return_defer(1);
    }
    editor.data.count = n;

    fclose(f);
    f = NULL;

    editor_recompute_lines(&editor);
    int exit_code = editor_start_interactive(&editor, file_path);
    return_defer(exit_code);
defer:
    if (f) fclose(f);
    return result;
}
