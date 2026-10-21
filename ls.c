#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "helpers.h"

/* Contains the necessary information about the files
 */
typedef struct _dir_entry_info {
    struct _dir_entry_info *next;
    char name[256];
    char lntarget[4096]; /* target path if file is symlink */
    char mode[12];
    char user[32];
    char group[32];
    char mtime[32]; /* last modification time */
    nlink_t nlink; /* link count */
    blkcnt_t blocks;
    off_t size;
} dir_entry_info;

/* Allocates new memory for struct dir_entry_info and inserts it after p_entry_info
 */
dir_entry_info *alloc_next_entry_info(dir_entry_info *p_entry_info) {
    dir_entry_info *tmp = calloc(1, sizeof(dir_entry_info));

	if (!tmp) {
		return NULL; /* calloc failed */
	}

	tmp->next = p_entry_info->next;
	p_entry_info->next = tmp;
    return tmp;
}

/* Goes through the list and frees memory
 */
void free_entry_info_list(dir_entry_info *p_head) {
    while (p_head) {
        dir_entry_info *tmp = p_head;
        p_head = p_head->next;
        free(tmp);
    }
}

/* Merges two sorted dir_entry_info lists
 */
static dir_entry_info *merge_sorted_entries(dir_entry_info *a, dir_entry_info *b) {
    dir_entry_info dummy = {0};
    dir_entry_info *tail = &dummy;

    while (a && b) {
        if (strcoll(a->name, b->name) <= 0) {
            tail->next = a;
            a = a->next;
        } else {
            tail->next = b;
            b = b->next;
        }
        tail = tail->next;
    }
    tail->next = a ? a : b;
    return dummy.next;
}

/* Sorts a dir_entry_info list alphabetically using merge sort
 */
static dir_entry_info *sort_entry_list(dir_entry_info *head) {
    if (!head || !head->next) {
        return head;
    }

    dir_entry_info *slow = head;
    dir_entry_info *fast = head->next;

    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }

    dir_entry_info *mid = slow->next;
    slow->next = NULL;

    return merge_sorted_entries(sort_entry_list(head), sort_entry_list(mid));
}

/* Fills the dir_entry_info structure with information about the specified file
 */
int fill_dir_entry_info(char const* file_path, char const* file_name, dir_entry_info *entry_info) {
    struct stat file_stat;
    struct passwd *pw;
    struct group *gr;

    snprintf(entry_info->name, sizeof(entry_info->name), "%s", file_name);
    entry_info->lntarget[0] = '\0';

    if (lstat(file_path, &file_stat) == -1) {
        return 1;
    }

    if (S_ISLNK(file_stat.st_mode)) {
        ssize_t link_len = readlink(file_path, entry_info->lntarget, sizeof(entry_info->lntarget) - 1);
        if (link_len >= 0) {
            entry_info->lntarget[link_len] = '\0';
        } else {
            entry_info->lntarget[0] = '\0';
        }
    }

    entry_info->nlink = file_stat.st_nlink;
    entry_info->blocks = file_stat.st_blocks;
    entry_info->size = file_stat.st_size;
    str_file_mode(file_stat.st_mode, entry_info->mode);
    str_timespec(&(file_stat.st_mtim), entry_info->mtime, sizeof(entry_info->mtime));
    pw = getpwuid(file_stat.st_uid);
    gr = getgrgid(file_stat.st_gid);

    if (pw) {
        snprintf(entry_info->user, sizeof(entry_info->user), "%s", pw->pw_name);
    } else {
        snprintf(entry_info->user, sizeof(entry_info->user), "%u", (unsigned int)file_stat.st_uid);
    }

    if (gr) {
        snprintf(entry_info->group, sizeof(entry_info->group), "%s", gr->gr_name);
    } else {
        snprintf(entry_info->group, sizeof(entry_info->group), "%u", (unsigned int)file_stat.st_gid);
    }

    return 0;
}

/* Returns a pointer to the beginning of the dir_entry_info list, which contains information about files,
 * and assigns the values of max_name_len and total_blocks.
 * The allocated memory must be freed with the function free_entry_info_list.
 */
dir_entry_info *get_entry_info_list(char const* path, int *max_name_len, blkcnt_t *total_blocks, int *error_occurred) {
    *max_name_len = 0;
    *total_blocks = 0;
    *error_occurred = 0;

    DIR *p_dir_obj = opendir(path);

    if (!p_dir_obj) {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n", path, strerror(errno));
        *error_occurred = 1;
        return NULL;
    }

    char file_path[4096];
    size_t path_len = strlen(path);
    int needs_slash = (path_len > 0 && path[path_len - 1] != '/');

    dir_entry_info entry_info_head = {0};
    dir_entry_info *p_entry_info_tail = &entry_info_head;
    struct dirent *p_dir_entry;

    int name_len;

    while ((p_dir_entry = readdir(p_dir_obj)) != NULL) {
        if (strcmp(p_dir_entry->d_name, ".") == 0 ||
            strcmp(p_dir_entry->d_name, "..") == 0) {
                continue;
        }
        
        if ((p_entry_info_tail = alloc_next_entry_info(p_entry_info_tail)) == NULL) {
            free_entry_info_list(entry_info_head.next);
            entry_info_head.next = NULL;
            *error_occurred = 1;
            break;
        }

        name_len = strlen(p_dir_entry->d_name);
        *max_name_len = name_len > *max_name_len ? name_len : *max_name_len;

        int written = snprintf(file_path, sizeof(file_path), "%s%s%s",
                               path, needs_slash ? "/" : "", p_dir_entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(file_path)) {
            continue;
        }
        if (fill_dir_entry_info(file_path, p_dir_entry->d_name, p_entry_info_tail) != 0) {
            fprintf(stderr, "ls: cannot access '%s': %s\n", file_path, strerror(errno));
            *error_occurred = 1;
        } else {
            *total_blocks += p_entry_info_tail->blocks / 2;
        }
    }

    closedir(p_dir_obj);
    return sort_entry_list(entry_info_head.next);
}

/* Prints a single entry in long listing format
 */
static void print_long_entry(dir_entry_info const *entry) {
    printf("%10s %10lu %10s %10s %10lld %s %s",
        entry->mode,
        (unsigned long)entry->nlink,
        entry->user,
        entry->group,
        (long long)entry->size,
        entry->mtime,
        entry->name);

    if (entry->lntarget[0]) {
        printf(" -> %s", entry->lntarget);
    }
    printf("\n");
}

/* Prints a list of entries in either multi-column or long format
 */
static void print_entry_list(dir_entry_info *entry_list_head, int max_name_len, int long_listing_format) {
    int col_count = 1;
    if (!long_listing_format) {
        col_count = get_columns_count((unsigned int)(max_name_len + 3));
    }

    dir_entry_info *entry_list_cur = entry_list_head;
    int i = 0;

    while (entry_list_cur) {
        if (long_listing_format) {
            print_long_entry(entry_list_cur);
        } else {
            printf("%-*s", max_name_len + 3, entry_list_cur->name);

            if (++i % col_count == 0) {
                printf("\n");
            }
        }

        entry_list_cur = entry_list_cur->next;
    }

    if (!long_listing_format && i % col_count) {
        printf("\n");
    }
}

/* Lists the entries in the specified directory.
 * long_listing_format - the -l argument was passed
 */
int list_dir(char const* path, int long_listing_format) {
    blkcnt_t total_blocks;
    int max_name_len;
    int error_occurred = 0;

    dir_entry_info *entry_list_head = get_entry_info_list(path, &max_name_len, &total_blocks, &error_occurred);

    if (error_occurred && entry_list_head == NULL) {
        return 1;
    }

    if (long_listing_format) {
        printf("Total: %lu\n", (unsigned long)total_blocks);
    }

    if (entry_list_head == NULL) {
        return error_occurred ? 1 : 0;
    }

    print_entry_list(entry_list_head, max_name_len, long_listing_format);
    free_entry_info_list(entry_list_head);
    return error_occurred ? 1 : 0;
}

/* Prints help
 */
void help(void) {
    fprintf(stderr, "Usage: ./ls [-l] [FILE...]\n\n");
    fprintf(stderr, "  -l - use a long listing format\n\n");
}

/* Parses arguments
 */
int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");
    tzset();

    int long_listing_format = 0;
    int optchar;

    opterr = 0;

    /* Parse arguments */
    while ((optchar = getopt(argc, argv, "l")) != -1) {
        switch (optchar) {
        case 'l':
            long_listing_format = 1;
            break;
        
        default:
            help();
            exit(1);
            break;
        }
    }

    if (optind >= argc) {
        return list_dir(".", long_listing_format);
    }

    int ret = 0;
    dir_entry_info file_head = {0};
    dir_entry_info *file_tail = &file_head;
    int max_file_name_len = 0;

    int dir_count = 0;
    char const **dirs = malloc((size_t)(argc - optind) * sizeof(char const *));
    if (!dirs) {
        perror("malloc");
        return 1;
    }

    for (int i = optind; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) == -1) {
            fprintf(stderr, "ls: cannot access '%s': %s\n", argv[i], strerror(errno));
            ret = 1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            dirs[dir_count++] = argv[i];
        } else {
            dir_entry_info *node = alloc_next_entry_info(file_tail);
            if (node) {
                if (fill_dir_entry_info(argv[i], argv[i], node) == 0) {
                    int len = (int)strlen(argv[i]);
                    if (len > max_file_name_len) {
                        max_file_name_len = len;
                    }
                    file_tail = node;
                }
            }
        }
    }

    int has_files = (file_head.next != NULL);
    if (has_files) {
        dir_entry_info *sorted_files = sort_entry_list(file_head.next);
        print_entry_list(sorted_files, max_file_name_len, long_listing_format);
        free_entry_info_list(sorted_files);
    }

    for (int i = 0; i < dir_count; i++) {
        if (has_files || i > 0) {
            printf("\n");
        }
        if (dir_count > 1 || has_files) {
            printf("%s:\n", dirs[i]);
        }
        ret |= list_dir(dirs[i], long_listing_format);
    }

    free(dirs);
    return ret;
}
