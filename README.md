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

## Command

| key                                                 | description                            |
|-----------------------------------------------------|----------------------------------------|
| <kbd>q</kbd>                                        | Quit the editor                        |
| <kbd>SPACE</kbd> or <kbd>Alt</kbd>+<kbd>SPACE</kbd> | Enter Insert mode                      |
| <kbd>w</kbd>                                        | Move up one line                       |
| <kbd>s</kbd>                                        | Move down one line                     |
| <kbd>a</kbd>                                        | Move left one character                |
| <kbd>d</kbd>                                        | Move right one character               |
| <kbd>DELETE</kbd>                                   | Delete one character at the cursor     |
| <kbd>BACKSPACE</kbd>                                | Delete one character before the cursor |
| <kbd>ENTER</kbd>                                    | Insert new line                        |
