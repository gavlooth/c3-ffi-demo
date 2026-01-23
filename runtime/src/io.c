/*
 * io.c - Extended I/O operations for OmniLisp
 *
 * Issue 28 P2: I/O Utilities
 *
 * File Operations:
 *   - prim_read_file, prim_write_file: Read/write entire files
 *   - prim_read_lines, prim_write_lines: Read/write as line arrays
 *   - prim_read_bytes, prim_write_bytes: Binary file operations
 *
 * File Predicates:
 *   - prim_file_exists_p, prim_file_p, prim_directory_p
 *   - prim_readable_p, prim_writable_p
 *
 * Directory Operations:
 *   - prim_list_directory, prim_make_directory, prim_delete_file, etc.
 *
 * Path Operations:
 *   - prim_path_join, prim_path_dirname, prim_path_basename, prim_path_extension
 *
 * Environment:
 *   - prim_getenv, prim_setenv, prim_environ
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include "../include/omni.h"
#include "internal_types.h"

/* External environment (for prim_environ) */
extern char **environ;

/* ============== Helper Functions ============== */

/*
 * obj_to_cstr_safe - Extract C string from Obj (handles NULL)
 */
static const char* obj_to_cstr_safe(Obj* obj) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return NULL;
    if (obj->tag == TAG_STRING || obj->tag == TAG_SYM) {
        return obj->ptr ? (const char*)obj->ptr : "";
    }
    return NULL;
}

/* ============== File Operations ============== */

// TESTED
/*
 * prim_read_file - Read entire file as string
 *
 * Args: path (string)
 * Returns: file contents as string, or error on failure
 */
Obj* prim_io_read_file(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("read-file: invalid path");

    FILE* f = fopen(path, "r");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "read-file: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return mk_error("read-file: cannot determine file size");
    }

    /* Allocate buffer and read */
    char* buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return mk_error("read-file: out of memory");
    }

    size_t read_size = fread(buf, 1, size, f);
    buf[read_size] = '\0';
    fclose(f);

    Obj* result = mk_string(buf);
    free(buf);
    return result;
}

// TESTED - runtime/tests/test_io.c
/*
 * prim_write_file - Write string to file (overwrite)
 *
 * Args: path (string), content (string)
 * Returns: true on success, error on failure
 */
Obj* prim_io_write_file(Obj* path_obj, Obj* content_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    const char* content = obj_to_cstr_safe(content_obj);

    if (!path) return mk_error("write-file: invalid path");
    if (!content) content = "";

    FILE* f = fopen(path, "w");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "write-file: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        return mk_error("write-file: incomplete write");
    }

    return mk_bool(1);
}

/*
 * prim_read_lines - Read file as array of lines
 *
 * Args: path (string)
 * Returns: array of strings (one per line)
 */
Obj* prim_read_lines(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("read-lines: invalid path");

    FILE* f = fopen(path, "r");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "read-lines: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    Obj* result = mk_array(64);
    char* line = NULL;
    size_t len = 0;
    ssize_t nread;

    while ((nread = getline(&line, &len, f)) != -1) {
        /* Remove trailing newline */
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
            /* Also remove \r if present (Windows line endings) */
            if (nread > 1 && line[nread - 2] == '\r') {
                line[nread - 2] = '\0';
            }
        }
        array_push(result, mk_string(line));
    }

    free(line);
    fclose(f);
    return result;
}

/*
 * prim_write_lines - Write array of lines to file
 *
 * Args: path (string), lines (array of strings)
 * Returns: true on success, error on failure
 */
Obj* prim_write_lines(Obj* path_obj, Obj* lines_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("write-lines: invalid path");

    FILE* f = fopen(path, "w");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "write-lines: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    if (IS_BOXED(lines_obj) && lines_obj->tag == TAG_ARRAY) {
        Array* arr = (Array*)lines_obj->ptr;
        if (arr) {
            for (int i = 0; i < arr->len; i++) {
                const char* line = obj_to_cstr_safe(arr->data[i]);
                if (line) {
                    fputs(line, f);
                }
                fputc('\n', f);
            }
        }
    } else if (IS_BOXED(lines_obj) && lines_obj->tag == TAG_PAIR) {
        Obj* p = lines_obj;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            const char* line = obj_to_cstr_safe(p->a);
            if (line) {
                fputs(line, f);
            }
            fputc('\n', f);
            p = p->b;
        }
    }

    fclose(f);
    return mk_bool(1);
}

/*
 * prim_read_bytes - Read file as byte array (binary mode)
 *
 * Args: path (string)
 * Returns: array of integers (bytes)
 */
Obj* prim_read_bytes(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("read-bytes: invalid path");

    FILE* f = fopen(path, "rb");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "read-bytes: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return mk_error("read-bytes: cannot determine file size");
    }

    /* Read into buffer */
    unsigned char* buf = malloc(size);
    if (!buf) {
        fclose(f);
        return mk_error("read-bytes: out of memory");
    }

    size_t read_size = fread(buf, 1, size, f);
    fclose(f);

    /* Convert to array */
    Obj* result = mk_array((int)read_size);
    for (size_t i = 0; i < read_size; i++) {
        array_push(result, mk_int(buf[i]));
    }

    free(buf);
    return result;
}

/*
 * prim_write_bytes - Write byte array to file (binary mode)
 *
 * Args: path (string), bytes (array of integers)
 * Returns: true on success, error on failure
 */
Obj* prim_write_bytes(Obj* path_obj, Obj* bytes_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("write-bytes: invalid path");

    if (!IS_BOXED(bytes_obj) || bytes_obj->tag != TAG_ARRAY) {
        return mk_error("write-bytes: expected array of bytes");
    }

    Array* arr = (Array*)bytes_obj->ptr;
    if (!arr) return mk_error("write-bytes: invalid array");

    FILE* f = fopen(path, "wb");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "write-bytes: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    for (int i = 0; i < arr->len; i++) {
        unsigned char byte = (unsigned char)obj_to_int(arr->data[i]);
        fputc(byte, f);
    }

    fclose(f);
    return mk_bool(1);
}

/* ============== File Predicates ============== */

/*
 * prim_file_exists_p - Check if path exists
 */
Obj* prim_file_exists_p(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_bool(0);
    return mk_bool(access(path, F_OK) == 0);
}

/*
 * prim_file_p - Check if path is a regular file
 */
Obj* prim_file_p(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_bool(0);

    struct stat st;
    if (stat(path, &st) != 0) return mk_bool(0);
    return mk_bool(S_ISREG(st.st_mode));
}

/*
 * prim_directory_p - Check if path is a directory
 */
Obj* prim_directory_p(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_bool(0);

    struct stat st;
    if (stat(path, &st) != 0) return mk_bool(0);
    return mk_bool(S_ISDIR(st.st_mode));
}

/*
 * prim_readable_p - Check if file is readable
 */
Obj* prim_readable_p(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_bool(0);
    return mk_bool(access(path, R_OK) == 0);
}

/*
 * prim_writable_p - Check if file is writable
 */
Obj* prim_writable_p(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_bool(0);
    return mk_bool(access(path, W_OK) == 0);
}

/* ============== Directory Operations ============== */

/*
 * prim_list_directory - List directory contents
 *
 * Args: path (string)
 * Returns: array of filenames (strings)
 */
Obj* prim_list_directory(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("list-directory: invalid path");

    DIR* dir = opendir(path);
    if (!dir) {
        char msg[256];
        snprintf(msg, sizeof(msg), "list-directory: cannot open '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    Obj* result = mk_array(64);
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        array_push(result, mk_string(entry->d_name));
    }

    closedir(dir);
    return result;
}

/*
 * prim_make_directory - Create a directory
 *
 * Args: path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_make_directory(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("make-directory: invalid path");

    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST) {
            /* Already exists, check if it's a directory */
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                return mk_bool(1);  /* Already a directory */
            }
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "make-directory: cannot create '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_bool(1);
}

/*
 * prim_make_directories - Create nested directories (like mkdir -p)
 *
 * Args: path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_make_directories(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("make-directories: invalid path");

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    /* Remove trailing slash */
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    /* Create each directory in the path */
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                char msg[256];
                snprintf(msg, sizeof(msg), "make-directories: cannot create '%s': %s", tmp, strerror(errno));
                return mk_error(msg);
            }
            *p = '/';
        }
    }

    /* Create final directory */
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        char msg[256];
        snprintf(msg, sizeof(msg), "make-directories: cannot create '%s': %s", tmp, strerror(errno));
        return mk_error(msg);
    }

    return mk_bool(1);
}

/*
 * prim_delete_file - Delete a file
 *
 * Args: path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_io_delete_file(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("delete-file: invalid path");

    if (unlink(path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "delete-file: cannot delete '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_bool(1);
}

/*
 * prim_delete_directory - Delete an empty directory
 *
 * Args: path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_delete_directory(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("delete-directory: invalid path");

    if (rmdir(path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "delete-directory: cannot delete '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_bool(1);
}

/*
 * prim_io_rename - Rename/move file or directory
 *
 * Args: old_path (string), new_path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_io_rename(Obj* old_path_obj, Obj* new_path_obj) {
    const char* old_path = obj_to_cstr_safe(old_path_obj);
    const char* new_path = obj_to_cstr_safe(new_path_obj);

    if (!old_path || !new_path) return mk_error("rename: invalid path");

    if (rename(old_path, new_path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "rename: cannot rename '%s' to '%s': %s",
                 old_path, new_path, strerror(errno));
        return mk_error(msg);
    }

    return mk_bool(1);
}

/*
 * prim_copy_file - Copy file contents
 *
 * Args: src_path (string), dst_path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_copy_file(Obj* src_obj, Obj* dst_obj) {
    const char* src_path = obj_to_cstr_safe(src_obj);
    const char* dst_path = obj_to_cstr_safe(dst_obj);

    if (!src_path || !dst_path) return mk_error("copy-file: invalid path");

    FILE* src = fopen(src_path, "rb");
    if (!src) {
        char msg[256];
        snprintf(msg, sizeof(msg), "copy-file: cannot open source '%s': %s", src_path, strerror(errno));
        return mk_error(msg);
    }

    FILE* dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        char msg[256];
        snprintf(msg, sizeof(msg), "copy-file: cannot open destination '%s': %s", dst_path, strerror(errno));
        return mk_error(msg);
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            return mk_error("copy-file: write error");
        }
    }

    fclose(src);
    fclose(dst);
    return mk_bool(1);
}

/* ============== Path Operations ============== */

/*
 * prim_path_join - Join path components with separator
 *
 * Args: parts (array of strings)
 * Returns: joined path string
 */
Obj* prim_path_join(Obj* parts_obj) {
    if (!parts_obj) return mk_string("");

    char result[PATH_MAX] = "";
    size_t pos = 0;

    /* Handle array */
    if (IS_BOXED(parts_obj) && parts_obj->tag == TAG_ARRAY) {
        Array* arr = (Array*)parts_obj->ptr;
        if (arr) {
            for (int i = 0; i < arr->len && pos < PATH_MAX - 2; i++) {
                const char* part = obj_to_cstr_safe(arr->data[i]);
                if (!part || strlen(part) == 0) continue;

                /* Add separator if not first and previous doesn't end with / */
                if (pos > 0 && result[pos - 1] != '/') {
                    result[pos++] = '/';
                }

                /* Skip leading / on subsequent parts (except if empty result) */
                if (i > 0 && part[0] == '/') {
                    part++;
                }

                size_t len = strlen(part);
                if (pos + len >= PATH_MAX - 1) len = PATH_MAX - 1 - pos;
                memcpy(result + pos, part, len);
                pos += len;
            }
        }
    }
    /* Handle two string arguments (common case) */
    else if (IS_BOXED(parts_obj) && (parts_obj->tag == TAG_STRING || parts_obj->tag == TAG_SYM)) {
        const char* part = obj_to_cstr_safe(parts_obj);
        if (part) {
            strncpy(result, part, PATH_MAX - 1);
        }
    }

    result[pos] = '\0';
    return mk_string(result);
}

/*
 * prim_path_join2 - Join two path components
 *
 * Args: path1, path2 (strings)
 * Returns: joined path
 */
Obj* prim_path_join2(Obj* path1_obj, Obj* path2_obj) {
    const char* path1 = obj_to_cstr_safe(path1_obj);
    const char* path2 = obj_to_cstr_safe(path2_obj);

    char result[PATH_MAX];

    if (!path1 || strlen(path1) == 0) {
        if (!path2) return mk_string("");
        return mk_string(path2);
    }

    if (!path2 || strlen(path2) == 0) {
        return mk_string(path1);
    }

    /* Handle trailing/leading slashes */
    size_t len1 = strlen(path1);
    bool has_trailing = (path1[len1 - 1] == '/');
    bool has_leading = (path2[0] == '/');

    if (has_trailing && has_leading) {
        snprintf(result, PATH_MAX, "%s%s", path1, path2 + 1);
    } else if (has_trailing || has_leading) {
        snprintf(result, PATH_MAX, "%s%s", path1, path2);
    } else {
        snprintf(result, PATH_MAX, "%s/%s", path1, path2);
    }

    return mk_string(result);
}

/*
 * prim_path_dirname - Get directory part of path
 *
 * Args: path (string)
 * Returns: directory portion of path
 */
Obj* prim_path_dirname(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_string(".");

    char* copy = strdup(path);
    if (!copy) return mk_string(".");

    /* Find last slash */
    char* last_slash = strrchr(copy, '/');
    if (!last_slash) {
        free(copy);
        return mk_string(".");
    }

    /* Handle root */
    if (last_slash == copy) {
        free(copy);
        return mk_string("/");
    }

    *last_slash = '\0';
    Obj* result = mk_string(copy);
    free(copy);
    return result;
}

// TESTED
/*
 * prim_path_basename - Get filename part of path
 *
 * Args: path (string)
 * Returns: filename portion of path
 */
Obj* prim_path_basename(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_string("");

    /* Find last slash */
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) {
        return mk_string(path);
    }

    return mk_string(last_slash + 1);
}

// TESTED - test_path_operations_extension.c
/*
 * prim_path_extension - Get file extension (including dot)
 *
 * Args: path (string)
 * Returns: extension (e.g., ".txt") or empty string
 */
Obj* prim_path_extension(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_string("");

    /* Get basename first */
    const char* basename = strrchr(path, '/');
    if (basename) basename++;
    else basename = path;

    /* Find last dot in basename */
    const char* dot = strrchr(basename, '.');
    if (!dot || dot == basename) {  /* No extension or hidden file */
        return mk_string("");
    }

    return mk_string(dot);
}

/*
 * prim_path_absolute - Get absolute path
 *
 * Args: path (string)
 * Returns: absolute path
 */
Obj* prim_path_absolute(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("path-absolute: invalid path");

    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "path-absolute: cannot resolve '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_string(resolved);
}

/*
 * prim_current_directory - Get current working directory
 *
 * Returns: current directory path
 */
Obj* prim_current_directory(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return mk_error("current-directory: cannot get cwd");
    }
    return mk_string(cwd);
}

/*
 * prim_change_directory - Change current working directory
 *
 * Args: path (string)
 * Returns: true on success, error on failure
 */
Obj* prim_change_directory(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("change-directory: invalid path");

    if (chdir(path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "change-directory: cannot change to '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_bool(1);
}

/* ============== File Metadata ============== */

/*
 * prim_io_file_size - Get file size in bytes
 *
 * Args: path (string)
 * Returns: file size as integer
 */
Obj* prim_io_file_size(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("file-size: invalid path");

    struct stat st;
    if (stat(path, &st) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "file-size: cannot stat '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_int((long)st.st_size);
}

/*
 * prim_io_file_mtime - Get file modification time (Unix timestamp)
 *
 * Args: path (string)
 * Returns: modification time as integer (seconds since epoch)
 */
Obj* prim_io_file_mtime(Obj* path_obj) {
    const char* path = obj_to_cstr_safe(path_obj);
    if (!path) return mk_error("file-mtime: invalid path");

    struct stat st;
    if (stat(path, &st) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "file-mtime: cannot stat '%s': %s", path, strerror(errno));
        return mk_error(msg);
    }

    return mk_int((long)st.st_mtime);
}

/* ============== Environment ============== */

/*
 * prim_io_getenv - Get environment variable
 *
 * Args: name (string)
 * Returns: value as string, or nothing if not set
 */
Obj* prim_io_getenv(Obj* name_obj) {
    const char* name = obj_to_cstr_safe(name_obj);
    if (!name) return mk_nothing();

    const char* value = getenv(name);
    if (!value) return mk_nothing();

    return mk_string(value);
}

/*
 * prim_io_setenv - Set environment variable
 *
 * Args: name (string), value (string)
 * Returns: true on success, error on failure
 */
Obj* prim_io_setenv(Obj* name_obj, Obj* value_obj) {
    const char* name = obj_to_cstr_safe(name_obj);
    const char* value = obj_to_cstr_safe(value_obj);

    if (!name) return mk_error("setenv: invalid name");
    if (!value) value = "";

    if (setenv(name, value, 1) != 0) {
        return mk_error("setenv: failed");
    }

    return mk_bool(1);
}

/*
 * prim_io_unsetenv - Unset environment variable
 *
 * Args: name (string)
 * Returns: true on success
 */
Obj* prim_io_unsetenv(Obj* name_obj) {
    const char* name = obj_to_cstr_safe(name_obj);
    if (!name) return mk_error("unsetenv: invalid name");

    unsetenv(name);
    return mk_bool(1);
}

/*
 * prim_io_environ - Get all environment variables as dict
 *
 * Returns: dict mapping names to values
 */
Obj* prim_io_environ(void) {
    Obj* result = mk_dict();

    for (char** env = environ; *env; env++) {
        char* eq = strchr(*env, '=');
        if (eq) {
            size_t name_len = eq - *env;
            char* name = malloc(name_len + 1);
            if (name) {
                memcpy(name, *env, name_len);
                name[name_len] = '\0';

                dict_set(result, mk_string(name), mk_string(eq + 1));
                free(name);
            }
        }
    }

    return result;
}
