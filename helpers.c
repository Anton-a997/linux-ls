#include <stdlib.h>

#include "helpers.h"

/**
 * @brief Converts a file mode (mode_t) into a 10-character permission string.
 *
 * Formats the file type (e.g. 'd', 'l', 'c', 'b', 'p', 's', '-') and permission
 * bits (rwx) including setuid, setgid, and sticky bits into a null-terminated string.
 *
 * @param mode File mode containing type and permission flags.
 * @param str Buffer of at least 11 bytes to store the formatted string.
 */
void str_file_mode(mode_t mode, char *str) {
    if (S_ISDIR(mode)) {
        str[0] = 'd';
    } else if (S_ISLNK(mode)) {
        str[0] = 'l';
    } else if (S_ISCHR(mode)) {
        str[0] = 'c';
    } else if (S_ISBLK(mode)) {
        str[0] = 'b';
    } else if (S_ISFIFO(mode)) {
        str[0] = 'p';
    } else if (S_ISSOCK(mode)) {
        str[0] = 's';
    } else {
        str[0] = '-';
    }

    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID) {
        str[3] = (mode & S_IXUSR) ? 's' : 'S';
    } else {
        str[3] = (mode & S_IXUSR) ? 'x' : '-';
    }

    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID) {
        str[6] = (mode & S_IXGRP) ? 's' : 'S';
    } else {
        str[6] = (mode & S_IXGRP) ? 'x' : '-';
    }

    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX) {
        str[9] = (mode & S_IXOTH) ? 't' : 'T';
    } else {
        str[9] = (mode & S_IXOTH) ? 'x' : '-';
    }

    str[10] = '\0';
}

/**
 * @brief Formats a timespec timestamp into a human-readable date-time string.
 *
 * @param ts Pointer to the timespec structure containing the timestamp.
 * @param str Buffer where the formatted date-time string will be stored.
 * @param len Maximum capacity of the destination buffer.
 * @return 0 on success, non-zero if formatting fails.
 */
int str_timespec(struct timespec const *ts, char *str, size_t len) {
    struct tm t;

    if (localtime_r(&(ts->tv_sec), &t) == NULL) {
        return 1;
    }

    if (strftime(str, len, "%F %T", &t) == 0) {
        return 1;
    }

    return 0;
}

/**
 * @brief Calculates how many columns of a given width fit across the terminal.
 *
 * Queries the terminal window size using ioctl, falling back to the COLUMNS
 * environment variable or a default of 1 column.
 *
 * @param column_width Width in characters of a single column including spacing.
 * @return Number of columns that fit, guaranteed to be at least 1.
 */
int get_columns_count(unsigned int column_width) {
    if (column_width == 0) {
        return 1;
    }

    struct winsize ws;
    int terminal_width = 0;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        terminal_width = ws.ws_col;
    } else {
        const char *col_env = getenv("COLUMNS");
        if (col_env) {
            terminal_width = atoi(col_env);
        }
    }

    if (terminal_width <= 0) {
        return 1;
    }

    int columns_count = terminal_width / (int)column_width;
    return columns_count > 0 ? columns_count : 1;
}
