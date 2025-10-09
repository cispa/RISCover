#include <stdlib.h>
#include <archive_entry.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#include "util.h"
#include "log.h"
#include "extended_util.h"

void _add_directory_to_archive(struct archive *archive, const char *path) {
    struct stat st;

    DIR *dir = opendir(path);
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char inner_path[1024];
        snprintf(inner_path, sizeof(inner_path), "%s/%s", path, ent->d_name);
        stat(inner_path, &st);

        if (S_ISREG(st.st_mode)) {
            printf("adding %s\n", inner_path);


            struct archive_entry *entry = archive_entry_new();
            archive_entry_set_pathname(entry, inner_path);
            archive_entry_set_size(entry, st.st_size);
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0666);
            archive_write_header(archive, entry);

            FILE *fp = fopen(inner_path, "rb");
            if (fp == NULL) {
                log_perror("open");
                exit(EXIT_FAILURE);
            }
            char buf[4096];
            size_t bytes_read;
            while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0) {
                if (archive_write_data(archive, buf, bytes_read) < 0) {
                    exit(EXIT_FAILURE);
                }
            }
            fclose(fp);
            archive_entry_free(entry);
        }
        if (S_ISDIR(st.st_mode)) {
            if (strcmp(ent->d_name, ".") == 0 ) {
                continue;
            }
            if (strcmp(ent->d_name, "..") == 0 ) {
                continue;
            }
            printf("adding subdirectory %s\n", inner_path);
            _add_directory_to_archive(archive, inner_path);
        }
    }
    closedir(dir);
}

void compress_result_dir(const char *dirname, const char *arcname) {
    struct archive *archive;
    struct stat st;

    stat(arcname, &st);
    if (S_ISREG(st.st_mode)) {
        char *old_arcname = malloc(strlen(arcname) + 5);
        assert(old_arcname);
        strcpy(old_arcname, arcname);
        strcat(old_arcname, ".old");

        printf("renaming existing file %s to %s-old\n", arcname, old_arcname);
        if (rename(arcname, old_arcname) != 0) {
            log_perror("rename");
            exit(EXIT_FAILURE);
        }

        free(old_arcname);
    }

    archive = archive_write_new();
    archive_write_add_filter_gzip(archive);
    archive_write_set_format_pax_restricted(archive);
    archive_write_open_filename(archive, arcname);
    // Add every file in the output directory to the archive
    _add_directory_to_archive(archive, dirname);
    archive_write_close(archive);
    archive_write_free(archive);
}

void mkdir_recursive(const char* path, mode_t mode) {
    char tmp[256];
    char* p = NULL;
    size_t len;

    // Copy the path to a temporary buffer
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) {
        exit(EXIT_FAILURE);
    }
    // Remove trailing slash, if any.
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Iterate the string and create directories for each level.
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0) {
                if (errno != EEXIST) {
                    log_perror("mkdir");
                    exit(EXIT_FAILURE);
                }
            }
            *p = '/';
        }
    }
    // Create the final directory.
    if (mkdir(tmp, mode) != 0) {
        if (errno != EEXIST) {
            log_perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }
}
