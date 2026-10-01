#include "minemu/msh.h"

#include <stddef.h>

#include "minemu/kprintf.h"
#include "minemu/uart.h"

#define MSH_PROMPT "msh> "
#define MSH_LINE_MAX 20 /* bytes per line, not counting the terminating '\n' */

/*
 * A built-in command. It receives everything after the command word and its
 * separating spaces: "echo   a b" calls echo with args "a b".
 */
struct msh_command {
    const char *name;
    void (*run)(const char *args);
};

static void cmd_echo(const char *args);

/* Dispatch table: adding a command means adding one line here. */
static const struct msh_command commands[] = {
    {"echo", cmd_echo},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

/* echo TEXT: print TEXT verbatim plus a newline; bare "echo" prints "\n". */
static void cmd_echo(const char *args) {
    kprintf("%s\n", args);
}

static int str_equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int is_backspace(char c) {
    return c == 0x08 || c == 0x7f;
}

/*
 * Read one line into buf (which holds MSH_LINE_MAX + 1 bytes).
 *
 * - Only '\n' ends a line; it is not stored. Every other byte, '\r'
 *   included, is ordinary line content.
 * - 0x08 and 0x7f delete the last stored byte; on an empty line they do
 *   nothing.
 * - Nothing is echoed and no erase sequence is printed: the TUI already
 *   mirrors typed input and backspaces itself.
 *
 * Overlong input policy (report an error, do not truncate): the 21st byte
 * marks the line as too long, and everything up to the next '\n' is
 * discarded, backspaces included. Returns 0 for a usable line, -1 for an
 * overlong one. Truncating would run a command the user did not type in
 * full, so rejecting the whole line is the safer choice.
 */
static int read_line(char *buf) {
    size_t len = 0;
    int too_long = 0;

    for (;;) {
        char c = uart_getc();

        if (c == '\n') {
            buf[len] = '\0';
            return too_long ? -1 : 0;
        }
        if (too_long) {
            continue; /* discarding the rest of an overlong line */
        }
        if (is_backspace(c)) {
            if (len > 0) {
                --len;
            }
            continue;
        }
        if (len == MSH_LINE_MAX) {
            too_long = 1;
            continue;
        }
        buf[len++] = c;
    }
}

static char *skip_spaces(char *s) {
    while (*s == ' ') {
        ++s;
    }
    return s;
}

/*
 * Parse and run one line, editing it in place:
 *   leading spaces are skipped; the command is the first space-delimited
 *   word; any run of spaces after it is one separator; the rest of the line
 *   is the argument text, passed on verbatim.
 */
static void run_line(char *line) {
    char *name = skip_spaces(line);
    if (*name == '\0') {
        return; /* empty or all-space line: just prompt again */
    }

    char *args = name;
    while (*args != '\0' && *args != ' ') {
        ++args;
    }
    if (*args == ' ') {
        *args = '\0'; /* terminate the command word */
        args = skip_spaces(args + 1);
    }

    for (size_t i = 0; i < COMMAND_COUNT; ++i) {
        if (str_equal(name, commands[i].name)) {
            commands[i].run(args);
            return;
        }
    }
    kprintf("command not found: %s\n", name);
}

void msh_run(void) {
    static char line[MSH_LINE_MAX + 1];

    for (;;) {
        uart_puts(MSH_PROMPT);
        if (read_line(line) < 0) {
            kprintf("msh: line too long (max %d bytes)\n", MSH_LINE_MAX);
            continue;
        }
        run_line(line);
    }
}
