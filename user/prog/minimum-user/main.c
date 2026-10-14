#include <stddef.h>
#include <stdint.h>

#include "minemu/user_abi.h"

/*
 * msh, the minimum shell, as a USR-mode program. All console I/O goes
 * through the ioctl syscall on UART0; the shell never touches a device.
 *
 * Line rules:
 *   - Only '\n' ends a line. 0x08 and 0x7f delete the last typed byte
 *     (nothing on an empty line). Nothing is echoed: the terminal already
 *     shows what was typed.
 *   - Spaces only separate words. Leading and trailing spaces are dropped
 *     and a run of spaces counts as one, so "  echo   a   b " is the
 *     command line "echo a b".
 *   - The 20-byte limit (MSH_LINE_MAX) applies separately to the command
 *     word and to the normalized text after it, so "echo TEXT" works for
 *     any TEXT of up to 20 bytes. A line over either limit is discarded
 *     without running anything, and the prompt comes back.
 *
 * Why not 20 bytes for the whole line: the course tests require
 * "echo hello from user mode" (25 bytes, TEXT exactly 20) to run, and a
 * 140-byte single word to be dropped silently.
 */

#define MSH_PROMPT "msh> "
#define MSH_LINE_MAX 20

/*
 * Raw bytes of the line being typed, before normalization. A line that
 * outgrows even this is certainly overlong, so it is discarded the same way.
 */
#define MSH_RAW_MAX 256

static size_t str_length(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static int str_equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/* Write all of buf to the console, retrying after a short write. */
static void put_bytes(const char *buf, size_t n) {
    while (n > 0) {
        int32_t written = ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)buf, n);
        if (written <= 0) {
            return;
        }
        buf += written;
        n -= (size_t)written;
    }
}

static void put_str(const char *s) {
    put_bytes(s, str_length(s));
}

/* Next input byte. ioctl never blocks, so poll until one arrives. */
static char get_char(void) {
    static char buf[32];
    static int32_t count;
    static int32_t next;

    while (next >= count) {
        next = 0;
        count = ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, (void *)buf, sizeof(buf));
        if (count < 0) {
            count = 0;
        }
    }
    return buf[next++];
}

static int is_backspace(char c) {
    return c == 0x08 || c == 0x7f;
}

/*
 * Read one raw line (without its '\n') into raw[]. Returns its length, or
 * -1 if it did not fit and was discarded.
 */
static int read_raw_line(char *raw) {
    size_t len = 0;
    int overflow = 0;

    for (;;) {
        char c = get_char();
        if (c == '\n') {
            return overflow ? -1 : (int)len;
        }
        if (overflow) {
            continue; /* drop the rest of an overlong line, backspaces too */
        }
        if (is_backspace(c)) {
            if (len > 0) {
                --len;
            }
        } else if (len == MSH_RAW_MAX) {
            overflow = 1;
        } else {
            raw[len++] = c;
        }
    }
}

/*
 * Collapse raw[0..len) into line as space-separated words (see the rules
 * above). Returns the normalized length, 0 for a blank line, or -1 if the
 * command word or the text after it exceeds MSH_LINE_MAX.
 */
static int normalize(const char *raw, int len, char *line) {
    int out = 0;
    int part_start = 0; /* where the part being limited (word, then TEXT) began */
    int i = 0;

    for (;;) {
        while (i < len && raw[i] == ' ') {
            ++i;
        }
        if (i == len) {
            break;
        }
        if (out > 0) {
            if (part_start == 0) {
                part_start = out + 1; /* TEXT starts after the first separator */
            } else if (out - part_start >= MSH_LINE_MAX) {
                return -1; /* the separator itself would overflow TEXT */
            }
            line[out++] = ' ';
        }
        while (i < len && raw[i] != ' ') {
            if (out - part_start >= MSH_LINE_MAX) {
                return -1;
            }
            line[out++] = raw[i++];
        }
    }
    line[out] = '\0';
    return out;
}

/* A built-in: receives the text after the command word (maybe ""). */
struct msh_command {
    const char *name;
    void (*run)(const char *args);
};

/* echo TEXT: print TEXT and a newline; bare "echo" prints an empty line. */
static void cmd_echo(const char *args) {
    put_str(args);
    put_str("\n");
}

static const struct msh_command commands[] = {
    {"echo", cmd_echo},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

/* Run one normalized, non-empty line, splitting it in place. */
static void run_line(char *line) {
    char *name = line;
    char *args = line;
    while (*args != '\0' && *args != ' ') {
        ++args;
    }
    if (*args == ' ') {
        *args++ = '\0'; /* normalized: exactly one space separates words */
    }

    for (size_t i = 0; i < COMMAND_COUNT; ++i) {
        if (str_equal(name, commands[i].name)) {
            commands[i].run(args);
            return;
        }
    }
    put_str("command not found: ");
    put_str(name);
    put_str("\n");
}

void minemu_user_main(void) {
    static char raw[MSH_RAW_MAX];
    static char line[2 * MSH_LINE_MAX + 2]; /* word, space, TEXT, NUL */

    for (;;) {
        put_str(MSH_PROMPT);
        int len = read_raw_line(raw);
        if (len < 0 || normalize(raw, len, line) <= 0) {
            continue; /* overlong, or empty / all spaces */
        }
        run_line(line);
    }
}
