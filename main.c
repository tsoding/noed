#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <termios.h>

#include <unistd.h>

#define EDITOR_CAPACITY (10*1024)

typedef struct {
    size_t begin;
    size_t end;
} Line;

typedef struct {
    // TODO: replace data with rope
    char data[EDITOR_CAPACITY];
    size_t data_count;
    Line lines[EDITOR_CAPACITY + 10];
    size_t lines_count;
    size_t cursor;
} Editor;

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
    if (insert) printf("[INSERT]");
    size_t line = editor_current_line(e);
    printf("\033[%zu;%zuH", line + 1, e->cursor - e->lines[line].begin + 1);
}

static Editor editor = {0};

int editor_start_interactive(Editor *e, const char *file_path)
{
    // TODO: implement limited view and scrolling
    if (!isatty(0)) {
        fprintf(stderr, "ERROR: Please run the editor in the terminal!\n");
        return 1;
    }

    struct termios term;
    if (tcgetattr(0, &term) < 0) {
        fprintf(stderr, "ERROR: could not save the state of the terminal: %s\n", strerror(errno));
        return 1;
    }

    term.c_lflag &= ~ECHO;
    term.c_lflag &= ~ICANON;
    if (tcsetattr(0, 0, &term)) {
        fprintf(stderr, "ERROR: could not update the state of the terminal: %s\n", strerror(errno));
        return 1;
    }

    bool quit = false;
    bool insert = false;
    while (!quit && !feof(stdin)) {
        editor_rerender(e, insert);

        if (insert) {
            int x = fgetc(stdin);
            if (x == 27) {
                // TODO: proper saving
                insert = false;
                FILE *f = fopen(file_path, "wb");
                assert(f != NULL && "TODO: properly handle inability to autosave files");
                fwrite(e->data, 1, e->data_count, f);
                assert(!ferror(f) && "TODO: properly handle inability to autosave files");
                fclose(f);
            } else {
                editor_insert_char(&editor, x);
            }
        } else {
            int x = fgetc(stdin);
            switch (x) {
            case 'q': {
                quit = true;
            }
            break;

            case 'e': {
                insert = true;
            }
            break;

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

    printf("\033[2J");

    term.c_lflag |= ECHO;
    tcsetattr(0, 0, &term);

    return 0;
}

int main(int argc, char **argv)
{

    if (argc < 2) {
        fprintf(stderr, "Usage: noed <input.txt>\n");
        fprintf(stderr, "ERROR: no input file is provided\n");
        return 1;
    }

    const char *file_path = argv[1];

    FILE *f = fopen(file_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: could not open file %s: %s\n", file_path, strerror(errno));
        return 1;
    }

    editor.data_count = fread(editor.data, 1, EDITOR_CAPACITY, f);

    fclose(f);

    editor_recompute_lines(&editor);
    return editor_start_interactive(&editor, file_path);
}
