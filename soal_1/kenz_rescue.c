#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#define FUSE_USE_VERSION 31

#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef S_IFREG
#define S_IFREG 0100000
#endif

struct kenz_state {
    char source_dir[PATH_MAX];
};

static const char *VIRTUAL_NAME = "tujuan.txt";
static const char *FRAGMENT_PREFIX = "KOORD:";
static const char *OUTPUT_PREFIX = "Tujuan Mas Amba: ";

static struct kenz_state *kenz_get_state(void)
{
    return (struct kenz_state *) fuse_get_context()->private_data;
}

static bool is_virtual_path(const char *path)
{
    return strcmp(path, "/tujuan.txt") == 0;
}

static int build_source_path(char *dest, size_t dest_size, const char *path)
{
    struct kenz_state *state = kenz_get_state();
    int written;

    if (strcmp(path, "/") == 0) {
        written = snprintf(dest, dest_size, "%s", state->source_dir);
    } else {
        written = snprintf(dest, dest_size, "%s%s", state->source_dir, path);
    }

    if (written < 0 || (size_t) written >= dest_size) {
        return -ENAMETOOLONG;
    }

    return 0;
}

static int build_numbered_source_path(char *dest, size_t dest_size, int index)
{
    int written = snprintf(dest, dest_size, "%s/%d.txt", kenz_get_state()->source_dir, index);
    if (written < 0 || (size_t) written >= dest_size) {
        return -ENAMETOOLONG;
    }

    return 0;
}

static int append_text(char **buffer, size_t *length, size_t *capacity, const char *text)
{
    size_t needed = strlen(text);

    if (*length + needed + 1 > *capacity) {
        size_t new_capacity = *capacity == 0 ? 128 : *capacity;

        while (*length + needed + 1 > new_capacity) {
            new_capacity *= 2;
        }

        char *new_buffer = realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return -ENOMEM;
        }

        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, needed);
    *length += needed;
    (*buffer)[*length] = '\0';
    return 0;
}

static int collect_fragments(char **output, size_t *output_size)
{
    char *fragments = NULL;
    size_t fragment_length = 0;
    size_t fragment_capacity = 0;
    int status = 0;

    for (int i = 1; i <= 7; i++) {
        char file_path[PATH_MAX];
        status = build_numbered_source_path(file_path, sizeof(file_path), i);
        if (status != 0) {
            goto cleanup;
        }

        FILE *file = fopen(file_path, "r");
        if (file == NULL) {
            status = -errno;
            goto cleanup;
        }

        char *line = NULL;
        size_t line_capacity = 0;
        ssize_t line_length;
        bool found = false;

        while ((line_length = getline(&line, &line_capacity, file)) != -1) {
            if (strncmp(line, FRAGMENT_PREFIX, strlen(FRAGMENT_PREFIX)) != 0) {
                continue;
            }

            char *fragment = line + strlen(FRAGMENT_PREFIX);
            while (*fragment == ' ' || *fragment == '\t') {
                fragment++;
            }

            size_t fragment_length_now = strlen(fragment);
            while (fragment_length_now > 0 &&
                   (fragment[fragment_length_now - 1] == '\n' ||
                    fragment[fragment_length_now - 1] == '\r')) {
                fragment[--fragment_length_now] = '\0';
            }

            status = append_text(&fragments, &fragment_length, &fragment_capacity, fragment);
            found = status == 0;
            break;
        }

        free(line);
        fclose(file);

        if (status != 0) {
            goto cleanup;
        }

        if (!found) {
            status = -EINVAL;
            goto cleanup;
        }
    }

    char *result = NULL;
    size_t result_length = 0;
    size_t result_capacity = 0;

    status = append_text(&result, &result_length, &result_capacity, OUTPUT_PREFIX);
    if (status != 0) {
        free(result);
        goto cleanup;
    }

    status = append_text(&result, &result_length, &result_capacity, fragments != NULL ? fragments : "");
    if (status != 0) {
        free(result);
        goto cleanup;
    }

    status = append_text(&result, &result_length, &result_capacity, "\n");
    if (status != 0) {
        free(result);
        goto cleanup;
    }

    *output = result;
    *output_size = result_length;

cleanup:
    free(fragments);
    return status;
}

static int kenz_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void) fi;
    memset(stbuf, 0, sizeof(*stbuf));

    if (is_virtual_path(path)) {
        char *contents = NULL;
        size_t size = 0;
        int status = collect_fragments(&contents, &size);
        if (status != 0) {
            free(contents);
            return status;
        }

        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = (off_t) size;
        free(contents);
        return 0;
    }

    char source_path[PATH_MAX];
    int status = build_source_path(source_path, sizeof(source_path), path);
    if (status != 0) {
        return status;
    }

    if (lstat(source_path, stbuf) == -1) {
        return -errno;
    }

    return 0;
}

static int kenz_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0) {
        char source_path[PATH_MAX];
        int status = build_source_path(source_path, sizeof(source_path), path);
        if (status != 0) {
            return status;
        }

        DIR *dir = opendir(source_path);
        if (dir == NULL) {
            return -errno;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = entry->d_ino;
            st.st_mode = entry->d_type << 12;

            if (filler(buf, entry->d_name, &st, 0, 0) != 0) {
                closedir(dir);
                return 0;
            }
        }

        closedir(dir);
        return 0;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    DIR *dir = opendir(kenz_get_state()->source_dir);
    if (dir == NULL) {
        return -errno;
    }

    bool virtual_present = false;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (strcmp(entry->d_name, VIRTUAL_NAME) == 0) {
            virtual_present = true;
        }

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = entry->d_ino;
        st.st_mode = entry->d_type << 12;

        if (filler(buf, entry->d_name, &st, 0, 0) != 0) {
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);

    if (!virtual_present) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_mode = S_IFREG | 0444;
        if (filler(buf, VIRTUAL_NAME, &st, 0, 0) != 0) {
            return 0;
        }
    }

    return 0;
}

static int kenz_open(const char *path, struct fuse_file_info *fi)
{
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EROFS;
    }

    if (is_virtual_path(path)) {
        return 0;
    }

    char source_path[PATH_MAX];
    int status = build_source_path(source_path, sizeof(source_path), path);
    if (status != 0) {
        return status;
    }

    int fd = open(source_path, O_RDONLY);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = (uint64_t) fd;
    return 0;
}

static int kenz_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    if (is_virtual_path(path)) {
        char *contents = NULL;
        size_t content_size = 0;
        int status = collect_fragments(&contents, &content_size);
        if (status != 0) {
            free(contents);
            return status;
        }

        if ((size_t) offset >= content_size) {
            free(contents);
            return 0;
        }

        if (offset + (off_t) size > (off_t) content_size) {
            size = content_size - (size_t) offset;
        }

        memcpy(buf, contents + offset, size);
        free(contents);
        return (int) size;
    }

    ssize_t res = pread((int) fi->fh, buf, size, offset);
    if (res == -1) {
        return -errno;
    }

    return (int) res;
}

static int kenz_release(const char *path, struct fuse_file_info *fi)
{
    if (!is_virtual_path(path)) {
        close((int) fi->fh);
    }

    fi->fh = 0;
    return 0;
}

static const struct fuse_operations kenz_ops = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .open = kenz_open,
    .read = kenz_read,
    .release = kenz_release,
};

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_directory> <mount_directory> [FUSE options]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct stat st;
    if (stat(argv[1], &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Source directory is invalid: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    struct kenz_state *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    if (realpath(argv[1], state->source_dir) == NULL) {
        perror("realpath");
        free(state);
        return EXIT_FAILURE;
    }

    int fuse_argc = argc - 1;
    char **fuse_argv = calloc((size_t) fuse_argc + 1, sizeof(char *));
    if (fuse_argv == NULL) {
        perror("calloc");
        free(state);
        return EXIT_FAILURE;
    }

    fuse_argv[0] = argv[0];
    for (int i = 2; i < argc; i++) {
        fuse_argv[i - 1] = argv[i];
    }
    fuse_argv[fuse_argc] = NULL;

    int status = fuse_main(fuse_argc, fuse_argv, &kenz_ops, state);

    free(fuse_argv);
    free(state);
    return status;
}
