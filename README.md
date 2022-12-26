# Noed

Not [ed(1)](https://linux.die.net/man/1/ed).

**THIS SOFTWARE IS UNFINISHED!!!**

## Quick Start

```console
$ ./build.sh
$ ./build/noed ./src/main.c
```

# Controls

We have two modes: Command and Insert. Just like in vi.

## Command Mode

| Key                                      | Description                            |
|------------------------------------------|----------------------------------------|
| <kbd>q</kbd>                             | Quit the editor                        |
| <kbd>SPACE</kbd> or <kbd>Alt+SPACE</kbd> | Switch to Insert Mode                  |
| <kbd>w</kbd>                             | Move up one line                       |
| <kbd>s</kbd>                             | Move down one line                     |
| <kbd>a</kbd>                             | Move left one character                |
| <kbd>d</kbd>                             | Move right one character               |
| <kbd>o</kbd>                             | Move up one paragraph                  |
| <kbd>l</kbd>                             | Move down one paragraph                |
| <kbd>k</kbd>                             | Move left one word                     |
| <kbd>;</kbd>                             | Move right one word                    |
| <kbd>DELETE</kbd>                        | Delete one character at the cursor     |
| <kbd>BACKSPACE</kbd>                     | Delete one character before the cursor |
| <kbd>ENTER</kbd>                         | Insert new line                        |

## Insert Mode

| Key                                        | Description                                         |
|--------------------------------------------|-----------------------------------------------------|
| <kbd>Alt+SPACE</kbd> or <kbd>ESCAPE</kbd>  | Save the current file and switch to Command Mode    |
| <kbd>DELETE</kbd>                          | Delete one character at the cursor                  |
| <kbd>BACKSPACE</kbd>                       | Delete one character before the cursor              |
| <kbd>ENTER</kbd>                           | Insert new line                                     |
| <kbd>Any displayable ASCII character</kbd> | Insert the character (unicode is not supported yet) |
