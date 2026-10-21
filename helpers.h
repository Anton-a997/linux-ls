#ifndef HELPERS_H
#define HELPERS_H

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Converts the mode_t value to a human-readable string like 'drwxrwxrwx'
 */
void str_file_mode(mode_t mode, char *str);

/* Converts the timespec value to a human-readable string
 */
int str_timespec(struct timespec const *ts, char *str, size_t len);

/* Returns the number of columns of a given width that can fit in the terminal
 */
int get_columns_count(unsigned column_width);

#endif // HELPERS_H
