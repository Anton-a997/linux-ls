#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
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
    size_t size;
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

/* Fills the dir_entry_info structure with information about the specified file
 */
int fill_dir_entry_info(char const* file_path, char const* file_name, unsigned char file_type, dir_entry_info *entry_info) {
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
    str_file_mode(file_stat.st_mode, file_type, entry_info->mode);
    str_timespec(&(file_stat.st_mtim), entry_info->mtime, sizeof(entry_info->mtime));
    pw = getpwuid(file_stat.st_uid);
    gr = getgrgid(file_stat.st_gid);

    if (pw) {
        snprintf(entry_info->user, sizeof(entry_info->user), "%s", pw->pw_name);
    }

    if (gr) {
        snprintf(entry_info->group, sizeof(entry_info->group), "%s", gr->gr_name);
    }

    return 0;
}

/* Returns a pointer to the beginning of the dir_entry_info list, which contains information about files,
 * and assigns the values of max_name_len and total_blocks.
 * The allocated memory must be freed with the function free_entry_info_list.
 */
dir_entry_info *get_entry_info_list(char const* path, int *max_name_len, size_t *total_blocks) {
    *max_name_len = 0;
    *total_blocks = 0;

    DIR *p_dir_obj = opendir(path);

    if (!p_dir_obj) {
        return NULL;
    }

    char file_path[4096];
    size_t path_len = strlen(path);
    int needs_slash = (path_len > 0 && path[path_len - 1] != '/');

    dir_entry_info entry_info_head = {NULL}; /* empty list */
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
            break;
        }

        name_len = strlen(p_dir_entry->d_name);
        *max_name_len = name_len > *max_name_len ? name_len : *max_name_len;

        int written = snprintf(file_path, sizeof(file_path), "%s%s%s",
                               path, needs_slash ? "/" : "", p_dir_entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(file_path)) {
            continue;
        }
        fill_dir_entry_info(file_path, p_dir_entry->d_name, p_dir_entry->d_type, p_entry_info_tail);

        *total_blocks += p_entry_info_tail->blocks / 2;
    }

    closedir(p_dir_obj);
    return entry_info_head.next;
}

/* Lists the entries in the specified directory.
 * long_listing_format - the -l argument was passed
 */
int list_dir(char const* path, int long_listing_format) {
    size_t total_blocks;
    int max_name_len;
    int col_count;

    dir_entry_info *entry_list_head = get_entry_info_list(path, &max_name_len, &total_blocks);

    /* Output the "Total" header even for an empty directory */
    if (long_listing_format && !errno) {
        printf("Total: %lu\n", total_blocks);
    }

    if (entry_list_head == NULL) {
        if (errno) {
            perror(NULL);
        }

        /* errno == 0 and entry_list_head == NULL if a directory is empty */
        return errno ? 1 : 0;
    }

    /* For pretty output */
    if (!long_listing_format) {
        col_count = get_columns_count(max_name_len + 3);
    }

    dir_entry_info *entry_list_cur = entry_list_head;
    int i = 0;

    while (entry_list_cur) {
        /* The -l argument was passed */
        if (long_listing_format) {
            printf("%10s %10lu %10s %10s %10lu%20s %s",
                entry_list_cur->mode,
                entry_list_cur->nlink,
                entry_list_cur->user,
                entry_list_cur->group,
                entry_list_cur->size,
                entry_list_cur->mtime,
                entry_list_cur->name);

                if (entry_list_cur->lntarget[0]) { /* if is symlink */
                    printf(" -> %s", entry_list_cur->lntarget);
                }

                printf("\n");
        }
        /* Output in columns */
        else {
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

    free_entry_info_list(entry_list_head);
    return 0;
}

/* Prints help
 */
void help() {
    printf("Usage: ./ls [-l] [FILE]\n\n");
    printf("  -l - use a long listing format\n\n");
}

/* Parses arguments
 */
int main(int argc, char* argv[]) {
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

    int ret = 0;

    /* The rest of the arguments are directories */
    if (optind < argc) {
        while (optind < argc) {
            printf("%s:\n", argv[optind]);
            ret |= list_dir(argv[optind++], long_listing_format);
            printf("\n");
        }
    }
    else {
        ret = list_dir(".", long_listing_format);
    }
    
    return ret;
}
