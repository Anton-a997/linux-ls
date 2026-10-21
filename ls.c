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

typedef struct _dir_entry_info {
    struct _dir_entry_info *next;
    char name[256];
    char lntarget[4096];
    char mode[12];
    char user[32];
    char group[32];
    char mtime[32];
    nlink_t nlink;
    blkcnt_t blocks;
    off_t size;
} dir_entry_info;

/**
 * @brief Allocates a new dir_entry_info node and inserts it after a given node.
 *
 * @param p_entry_info Node after which the newly allocated node will be inserted.
 * @return Pointer to the allocated node, or NULL on allocation failure.
 */
dir_entry_info *alloc_next_entry_info(dir_entry_info *p_entry_info) {
    dir_entry_info *tmp = calloc(1, sizeof(dir_entry_info));
    if (!tmp) {
        return NULL;
    }

    tmp->next = p_entry_info->next;
    p_entry_info->next = tmp;
    return tmp;
}

/**
 * @brief Deallocates all nodes in a linked list of dir_entry_info structures.
 *
 * @param p_head Pointer to the head of the list to free.
 */
void free_entry_info_list(dir_entry_info *p_head) {
    while (p_head) {
        dir_entry_info *tmp = p_head;
        p_head = p_head->next;
        free(tmp);
    }
}

/**
 * @brief Merges two lexicographically sorted dir_entry_info linked lists.
 *
 * @param a First sorted list.
 * @param b Second sorted list.
 * @return Head pointer of the merged sorted list.
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

/**
 * @brief Sorts a dir_entry_info linked list alphabetically using merge sort.
 *
 * @param head Head pointer of the list to sort.
 * @return Head pointer of the sorted list.
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

/**
 * @brief Populates a dir_entry_info structure with metadata for a given file.
 *
 * @param file_path Path to the target file.
 * @param file_name Display name of the file.
 * @param entry_info Pointer to the entry structure to populate.
 * @return 0 on success, non-zero if lstat fails.
 */
int fill_dir_entry_info(const char *file_path, const char *file_name, dir_entry_info *entry_info) {
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

/**
 * @brief Reads directory contents and returns a sorted linked list of entry records.
 *
 * @param path Directory path to open and read.
 * @param max_name_len Out-parameter receiving the maximum filename length encountered.
 * @param total_blocks Out-parameter receiving total 1024-byte block count.
 * @param error_occurred Out-parameter set to 1 if an error occurred during directory processing.
 * @return Sorted linked list of directory entries, or NULL if empty or on error.
 */
dir_entry_info *get_entry_info_list(const char *path, int *max_name_len, blkcnt_t *total_blocks, int *error_occurred) {
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

        name_len = (int)strlen(p_dir_entry->d_name);
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

/**
 * @brief Prints a single entry in detailed long listing format.
 *
 * @param entry Pointer to the entry structure to display.
 */
static void print_long_entry(const dir_entry_info *entry) {
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

/**
 * @brief Prints a list of file entries in either multi-column or long format.
 *
 * @param entry_list_head Head of the entry list to display.
 * @param max_name_len Maximum filename length for column alignment.
 * @param long_listing_format Non-zero for long listing format, 0 for multi-column.
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

/**
 * @brief Lists all entries within a directory.
 *
 * @param path Directory path to list.
 * @param long_listing_format Non-zero for long listing format, 0 for multi-column.
 * @return 0 on success, non-zero on error.
 */
int list_dir(const char *path, int long_listing_format) {
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

/**
 * @brief Prints command-line usage information to stderr.
 */
void help(void) {
    fprintf(stderr, "Usage: ./ls [-l] [FILE...]\n\n");
    fprintf(stderr, "  -l - use a long listing format\n\n");
}

/**
 * @brief Application entry point; parses flags and lists specified files/directories.
 *
 * @param argc Argument count.
 * @param argv Argument vectors.
 * @return 0 on complete success, non-zero if errors occurred.
 */
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    tzset();

    int long_listing_format = 0;
    int optchar;

    opterr = 0;

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
    const char **dirs = malloc((size_t)(argc - optind) * sizeof(const char *));
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
