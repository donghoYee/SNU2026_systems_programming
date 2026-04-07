//--------------------------------------------------------------------------------------------------
// System Programming                                I/O Lab                                Spring 2026
//
/// @file
/// @brief recursively traverse directory tree and list all entries
/// @author <이동호>
/// @studid <2021-13385>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stddef.h>

/// @brief output control flags
#define F_DEPTH    0x1        ///< print directory tree
#define F_Filter   0x2        ///< pattern matching

/// @brief maximum numbers
#define MAX_DIR 64            ///< maximum number of supported directories
#define MAX_PATH_LEN 1024     ///< maximum length of a path
#define MAX_DEPTH 20          ///< maximum depth of directory tree (for -d option)
int max_depth = MAX_DEPTH;    ///< maximum depth of directory tree (for -d option)

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};

/// @brief print strings used in the output
const char *print_formats[8] = {
  "Name                                                        User:Group           Size    Blocks Type\n",
  "----------------------------------------------------------------------------------------------------\n",
  "%s  ERROR: %s\n", 
  "%-54s  No such file or directory\n",
  "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "Invalid pattern syntax",
  "Out of memory",
};

const char* pattern = NULL;  // pattern for filtering entries

// Stack for non-matching ancestor directories
char *pending_paths[MAX_DEPTH];
int pending_count = 0;

void panic(const char* msg, const char* format)
{
  if (msg) {
    if (format) fprintf(stderr, format, msg);
    else        fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}

// -------------------------------------------------------------------------------------------------
// Pattern Matching Engine
// -------------------------------------------------------------------------------------------------

/// @brief validates regex pattern to ensure there are no syntax errors
void validate_pattern(const char *pat) {
    if (!pat || *pat == '\0') panic(print_formats[5], NULL);
    int len = strlen(pat);
    int in_group = 0;
    int group_len = 0;
    
    for (int i = 0; i < len; i++) {
        if (pat[i] == '(') {
            if (in_group) panic(print_formats[5], NULL); 
            in_group = 1;
            group_len = 0;
        } else if (pat[i] == ')') {
            if (!in_group || group_len == 0) panic(print_formats[5], NULL);
            in_group = 0;
        } else if (pat[i] == '*') {
            if (i == 0 || pat[i-1] == '*' || pat[i-1] == '(') panic(print_formats[5], NULL);
        } else {
            if (in_group) group_len++;
        }
    }
    if (in_group) panic(print_formats[5], NULL);
}

bool match_literal(const char *s, const char *p, int p_len) {
    for (int i = 0; i < p_len; i++) {
        if (s[i] == '\0') return false;
        if (p[i] != '?' && s[i] != p[i]) return false;
    }
    return true;
}

bool submatch(const char *s, const char *p) {
    if (*p == '\0') return true;

    int p_len = 0;
    bool is_group = false;
    
    if (*p == '(') {
        is_group = true;
        int depth = 1;
        while (p[1 + p_len] != '\0' && depth > 0) {
            if (p[1 + p_len] == '(') depth++;
            else if (p[1 + p_len] == ')') depth--;
            p_len++;
        }
        p_len--; // Inner content length
    } else {
        p_len = 1;
    }

    const char *next_p = p + (is_group ? p_len + 2 : p_len);
    bool is_star = (*next_p == '*');
    if (is_star) next_p++;

    if (is_star) {
        if (submatch(s, next_p)) return true; // Zero repetition match
        if (is_group) {
            if (match_literal(s, p + 1, p_len)) {
                if (submatch(s + p_len, p)) return true; // Recursive loop for 1+ repetitions
            }
        } else {
            if (*s != '\0' && (*p == '?' || *s == *p)) {
                if (submatch(s + 1, p)) return true;
            }
        }
        return false;
    } else {
        if (is_group) {
            if (match_literal(s, p + 1, p_len)) {
                return submatch(s + p_len, next_p);
            }
        } else {
            if (*s != '\0' && (*p == '?' || *s == *p)) {
                return submatch(s + 1, next_p);
            }
        }
        return false;
    }
}

bool is_match(const char *str, const char *pat) {
    if (!pat) return true;
    for (int i = 0; str[i] != '\0'; i++) {
        if (submatch(str + i, pat)) return true; // Try partial match at every starting index
    }
    return submatch(str + strlen(str), pat);
}

// -------------------------------------------------------------------------------------------------

struct dirent *get_next(DIR *dir)
{
  struct dirent *next;
  int ignore;
  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}

static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }
  return strcmp(e1->d_name, e2->d_name);
}

void flush_pending() {
    for (int i = 0; i < pending_count; i++) {
        printf("%s\n", pending_paths[i]);
        free(pending_paths[i]);
    }
    pending_count = 0;
}

void process_dir_rec(const char *dn, int depth, struct summary *dstat, struct summary *tstat, unsigned int flags)
{
    DIR *dir = opendir(dn);
    if (!dir) {
        if (flags & F_Filter) flush_pending();
        char ind[100];
        memset(ind, ' ', depth * 2);
        ind[depth * 2] = '\0';
        printf(print_formats[2], ind, strerror(errno));
        return;
    }

    int capacity = 16;
    struct dirent *entries = malloc(capacity * sizeof(struct dirent));
    if (!entries) panic(print_formats[6], NULL);
    
    int count = 0;
    struct dirent *entry;
    while ((entry = get_next(dir)) != NULL) {
        if (count == capacity) {
            capacity *= 2;
            entries = realloc(entries, capacity * sizeof(struct dirent));
            if (!entries) panic(print_formats[6], NULL);
        }
        memcpy(&entries[count], entry, sizeof(struct dirent));
        count++;
    }
    closedir(dir);

    qsort(entries, count, sizeof(struct dirent), dirent_compare);

    for (int i = 0; i < count; i++) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dn, entries[i].d_name);

        struct stat st;
        if (lstat(path, &st) == -1) continue;

        bool is_dir = S_ISDIR(st.st_mode);
        bool matches = true;
        
        if (flags & F_Filter) {
            matches = is_match(entries[i].d_name, pattern);
        }

        if (!matches && !is_dir) {
            continue; // Ignore non-matching regular files completely
        }

        // Apply indentation & truncation strictly
        char ind_name[MAX_PATH_LEN];
        snprintf(ind_name, sizeof(ind_name), "%*s%s", (depth * 2) + 2, "", entries[i].d_name);
        
        char name_buf[64];
        if (strlen(ind_name) > 54) {
            snprintf(name_buf, sizeof(name_buf), "%.51s...", ind_name);
        } else {
            snprintf(name_buf, sizeof(name_buf), "%-54s", ind_name);
        }

        if (matches) {
            if (flags & F_Filter) flush_pending();

            dstat->files += S_ISREG(st.st_mode) ? 1 : 0;
            dstat->dirs += is_dir ? 1 : 0;
            dstat->links += S_ISLNK(st.st_mode) ? 1 : 0;
            dstat->fifos += S_ISFIFO(st.st_mode) ? 1 : 0;
            dstat->socks += S_ISSOCK(st.st_mode) ? 1 : 0;
            dstat->size += st.st_size;
            dstat->blocks += st.st_blocks;

            if (tstat) {
                tstat->files += S_ISREG(st.st_mode) ? 1 : 0;
                tstat->dirs += is_dir ? 1 : 0;
                tstat->links += S_ISLNK(st.st_mode) ? 1 : 0;
                tstat->fifos += S_ISFIFO(st.st_mode) ? 1 : 0;
                tstat->socks += S_ISSOCK(st.st_mode) ? 1 : 0;
                tstat->size += st.st_size;
                tstat->blocks += st.st_blocks;
            }

            char type_c = ' ';
            if (is_dir) type_c = 'd';
            else if (S_ISLNK(st.st_mode)) type_c = 'l';
            else if (S_ISFIFO(st.st_mode)) type_c = 'f';
            else if (S_ISSOCK(st.st_mode)) type_c = 's';

            struct passwd *pw = getpwuid(st.st_uid);
            struct group *gr = getgrgid(st.st_gid);
            char uname[32], gname[32];
            if (pw) strncpy(uname, pw->pw_name, 31); else snprintf(uname, 32, "%u", st.st_uid);
            if (gr) strncpy(gname, gr->gr_name, 31); else snprintf(gname, 32, "%u", st.st_gid);

            printf(print_formats[4], name_buf, uname, gname, (unsigned long long)st.st_size, (unsigned long long)st.st_blocks, type_c);
        }

        if (is_dir && depth + 1 < max_depth) {
            int my_pending_idx = -1;
            
            // Push purely traversed (but non-matching) directories onto stack
            if (!matches && (flags & F_Filter)) {
                char trunc_name[64];
                if (strlen(ind_name) > 54) snprintf(trunc_name, sizeof(trunc_name), "%.51s...", ind_name);
                else strcpy(trunc_name, ind_name);
                
                pending_paths[pending_count] = strdup(trunc_name);
                my_pending_idx = pending_count;
                pending_count++;
            }

            process_dir_rec(path, depth + 1, dstat, tstat, flags);

            // Pop it off if it wasn't flushed (Meaning no descendants evaluated to true)
            if (my_pending_idx != -1 && pending_count > my_pending_idx) {
                free(pending_paths[--pending_count]);
            }
        }
    }
    free(entries);
}

void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags) {
    // Left empty: Required process_dir parameters modified for recursive depth state implementation.
}

void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;
    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);
    printf("\n\n");
  }

  assert(argv0 != NULL);
  fprintf(stderr, "Usage %s [-d depth] [-f pattern] [-h] [path...]\n"
                  "Recursively traverse directory tree and list all entries. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -d depth   | set maximum depth of directory traversal (1-%d)\n"
                  " -f pattern | filter entries using pattern (supports \'?\', \'*\' and \'()\')\n"
                  " -h         | print this help\n"
                  " path...    | list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DEPTH, MAX_DIR);

  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary tstat = { 0 };
  unsigned int flags = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (!strcmp(argv[i], "-d")) {
        flags |= F_DEPTH;
        if (++i < argc && argv[i][0] != '-') {
          max_depth = atoi(argv[i]);
          if (max_depth < 1 || max_depth > MAX_DEPTH) {
            syntax(argv[0], "Invalid depth value '%s'. Must be between 1 and %d.", argv[i], MAX_DEPTH);
          }
        } 
        else syntax(argv[0], "Missing depth value argument.");
      }
      else if (!strcmp(argv[i], "-f")) {
        if (++i < argc && argv[i][0] != '-') {
          flags |= F_Filter;
          pattern = argv[i];
          validate_pattern(pattern);
        }
        else syntax(argv[0], "Missing filtering pattern argument.");
      }
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    }
    else {
      if (ndir < MAX_DIR) directories[ndir++] = argv[i];
      else fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
    }
  }

  if (ndir == 0) directories[ndir++] = CURDIR;

  for (int i = 0; i < ndir; i++) {
      struct summary dstat = {0};
      pending_count = 0;

      printf("%s", print_formats[0]);
      printf("%s", print_formats[1]);

      struct stat st;
      if (lstat(directories[i], &st) == -1) {
          printf("%s\n", directories[i]);
          printf(print_formats[2], "", "No such file or directory");
      } else {
          printf("%s\n", directories[i]);
          if (max_depth > 0) {
              process_dir_rec(directories[i], 0, &dstat, &tstat, flags);
          }
      }

      printf("%s", print_formats[1]);

      char sum_buf[128];
      snprintf(sum_buf, sizeof(sum_buf), "%u file%s, %u director%s, %u link%s, %u pipe%s, and %u socket%s",
          dstat.files, dstat.files == 1 ? "" : "s",
          dstat.dirs, dstat.dirs == 1 ? "y" : "ies",
          dstat.links, dstat.links == 1 ? "" : "s",
          dstat.fifos, dstat.fifos == 1 ? "" : "s",
          dstat.socks, dstat.socks == 1 ? "" : "s");

      char sum_trunc[69];
      if (strlen(sum_buf) > 68) snprintf(sum_trunc, sizeof(sum_trunc), "%.65s...", sum_buf);
      else snprintf(sum_trunc, sizeof(sum_trunc), "%-68s", sum_buf);

      printf("%-68s   %14llu %9llu\n\n", sum_trunc, dstat.size, dstat.blocks);
  }

  if (ndir > 1) {
    printf("Analyzed %d directories:\n"
      "  total # of files:        %16d\n"
      "  total # of directories:  %16d\n"
      "  total # of links:        %16d\n"
      "  total # of pipes:        %16d\n"
      "  total # of sockets:      %16d\n"
      "  total # of entries:      %16d\n"
      "  total file size:         %16llu\n"
      "  total # of blocks:       %16llu\n",
      ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks,
      tstat.files + tstat.dirs + tstat.links + tstat.fifos + tstat.socks, 
      tstat.size, tstat.blocks);
  }

  return EXIT_SUCCESS;
}