#ifndef HELPERS_H
#define HELPERS_H

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Converts a file mode (mode_t) into a 10-character permission string.
 *
 * Formats the file type (e.g. 'd', 'l', 'c', 'b', 'p', 's', '-') and permission
 * bits (rwx) including setuid, setgid, and sticky bits into a null-terminated string.
 *
 * @param mode File mode containing type and permission flags.
 * @param str Buffer of at least 11 bytes to store the formatted string.
 */
void str_file_mode(mode_t mode, char *str);

/**
 * @brief Formats a timespec timestamp into a human-readable date-time string.
 *
 * @param ts Pointer to the timespec structure containing the timestamp.
 * @param str Buffer where the formatted date-time string will be stored.
 * @param len Maximum capacity of the destination buffer.
 * @return 0 on success, non-zero if formatting fails.
 */
int str_timespec(struct timespec const *ts, char *str, size_t len);

/**
 * @brief Calculates how many columns of a given width fit across the terminal.
 *
 * Queries the terminal window size using ioctl, falling back to the COLUMNS
 * environment variable or a default of 1 column.
 *
 * @param column_width Width in characters of a single column including spacing.
 * @return Number of columns that fit, guaranteed to be at least 1.
 */
int get_columns_count(unsigned int column_width);

#endif /* HELPERS_H */
