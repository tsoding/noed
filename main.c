#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <termios.h>

#include <unistd.h>

#define return_defer(value) do { result = (value); goto defer; } while(0)
#define UNUSED(x) (void)(x)

#define EDITOR_CAPACITY (10*1024)

typedef struct {
    size_t begin;
    size_t end;
} Line;

typedef struct {
    // TODO: replace data with rope
    // TODO: use dynamic memory for data and lines
    char data[EDITOR_CAPACITY];
    size_t data_count;
    Line lines[EDITOR_CAPACITY + 10];
    size_t lines_count;
    size_t cursor;
} Editor;

// TODO: line recomputation only based on what was changed
void editor_recompute_lines(Editor *e)
{
    e->lines_count = 0;

    size_t begin = 0;
    for (size_t i = 0; i < e->data_count; ++i) {
        if (e->data[i] == '\n') {
            e->lines[e->lines_count].begin = begin;
            e->lines[e->lines_count].end = i;
            e->lines_count += 1;
            begin = i + 1;
        }
    }

    e->lines[e->lines_count].begin = begin;
    e->lines[e->lines_count].end = e->data_count;
    e->lines_count += 1;
}

void editor_insert_char(Editor *e, char x)
{
    if (e->data_count < EDITOR_CAPACITY) {
        memmove(&e->data[e->cursor + 1], &e->data[e->cursor], e->data_count - e->cursor);
        e->data[e->cursor] = x;
        e->cursor += 1;
        e->data_count += 1;
        editor_recompute_lines(e);
    }
}

size_t editor_current_line(const Editor *e)
{
    assert(e->cursor <= e->data_count);
    for (size_t i = 0; i < e->lines_count; ++i) {
        if (e->lines[i].begin <= e->cursor && e->cursor <= e->lines[i].end) {
            return i;
        }
    }
    return 0;
}

void editor_rerender(const Editor *e, bool insert)
{
    printf("\033[2J\033[H");
    fwrite(e->data, 1, e->data_count, stdout);
    printf("\n");
    // TODO: print the mode indicator on the bottom
    if (insert) printf("[INSERT]");
    size_t line = editor_current_line(e);
    printf("\033[%zu;%zuH", line + 1, e->cursor - e->lines[line].begin + 1);
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
    fwrite(e->data, 1, e->data_count, f);
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
                size_t column = e->cursor - e->lines[line].begin;
                if (line < e->lines_count - 1) {
                    e->cursor = e->lines[line + 1].begin + column;
                    if (e->cursor > e->lines[line + 1].end) {
                        e->cursor = e->lines[line + 1].end;
                    }
                }
            }
            break;

            case 'w': {
                size_t line = editor_current_line(e);
                size_t column = e->cursor - e->lines[line].begin;
                if (line > 0) {
                    e->cursor = e->lines[line - 1].begin + column;
                    if (e->cursor > e->lines[line - 1].end) {
                        e->cursor = e->lines[line - 1].end;
                    }
                }
            }
            break;

            case 'a': {
                if (editor.cursor > 0) editor.cursor -= 1;
            }
            break;

            case 'd': {
                if (editor.cursor < e->data_count) e->cursor += 1;
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

    editor.data_count = fread(editor.data, 1, EDITOR_CAPACITY, f);
    if (ferror(f)) {
        fprintf(stderr, "ERROR: could not read file %s: %s\n", file_path, strerror(errno));
        return_defer(1);
    }

    fclose(f);
    f = NULL;

    editor_recompute_lines(&editor);
    int exit_code = editor_start_interactive(&editor, file_path);
    return_defer(exit_code);
defer:
    if (f) fclose(f);
    return result;
}
