/*
 * Launcher program for OS X application bundles of PuTTY.
 */

/*
 * The 'gtk-mac-bundler' utility arranges to build an OS X application
 * bundle containing a program compiled against the Quartz GTK
 * backend. It does this by including all the necessary GTK shared
 * libraries and data files inside the bundle as well as the binary.
 *
 * But the GTK program won't start up unless all those shared
 * libraries etc are already pointed to by environment variables like
 * GTK_PATH and PANGO_LIBDIR and things like that, which won't be set
 * up when the bundle is launched.
 *
 * Hence, gtk-mac-bundler expects to install the program in the bundle
 * under a name like 'Contents/MacOS/Program-bin'; and the file called
 * 'Contents/MacOS/Program', which is the one actually executed when
 * the bundle is launched, is a wrapper script that sets up the
 * environment before running the actual GTK-using program.
 *
 * In our case, however, that's not good enough. pterm will want to
 * launch subprocesses with general-purpose shell sessions in them,
 * and those subprocesses _won't_ want the random stuff dumped in the
 * environment by the gtk-mac-bundler standard wrapper script. So I
 * have to provide my own wrapper, which has a more complicated job:
 * not only setting up the environment for the GTK app, but also
 * preserving all details of the _previous_ environment, so that when
 * pterm forks off a subprocess to run in a terminal session, it can
 * restore the environment that was in force before the wrapper
 * started messing about. This source file implements that wrapper,
 * and does it in C so as to make string processing more reliable and
 * less annoying.
 *
 * My strategy for saving the old environment is to pick a prefix
 * that's unused by anything currently in the environment; let's
 * suppose it's "P" for this discussion. Any environment variable I
 * overwrite, say "VAR", I will either set "PsVAR=old value", or
 * "PuVAR=" ("s" and "u" for "set" and "unset"). Then I pass the
 * prefix itself as a command-line argument to the main GTK
 * application binary, which then knows how to restore the original
 * environment in pterm subprocesses.
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined __APPLE__ && !defined TEST_COMPILE_ON_LINUX
/* When we're not compiling for OS X, it's easier to just turn this
 * program into a trivial hello-world by ifdef in the source than it
 * is to remove it in the makefile edifice. */
int main(int argc, char **argv)
{
    fprintf(stderr, "launcher does nothing on non-OSX platforms\n");
    return 1;
}
#else /* __APPLE__ */

#include <unistd.h>
#include <libgen.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
/* For Linux, a bodge to let as much of this code still run as
 * possible, so that you can run it under friendly debugging tools
 * like valgrind. */
int _NSGetExecutablePath(char *out, uint32_t *outlen)
{
    static const char toret[] = "/proc/self/exe";
    if (out != NULL && *outlen < sizeof(toret))
        return -1;
    *outlen = sizeof(toret);
    if (out)
        memcpy(out, toret, sizeof(toret));
    return 0;
}
#endif

/* ----------------------------------------------------------------------
 * Find an alphabetic prefix unused by any environment variable name.
 */

/*
 * This linked-list based system is a bit overkill, but I enjoy an
 * algorithmic challenge. We essentially do an incremental radix sort
 * of all the existing environment variable names: initially divide
 * them into 26 buckets by their first letter (discarding those that
 * don't have a letter at that position), then subdivide each bucket
 * in turn into 26 sub-buckets, and so on. We maintain each bucket as
 * a linked list, and link their heads together into a secondary list
 * that functions as a queue (meaning that we go breadth-first,
 * processing all the buckets of a given depth before moving on to the
 * next depth down). At any stage, if we find one of our 26
 * sub-buckets is empty, that's our unused prefix.
 *
 * The running time is O(number of strings * length of output), and I
 * doubt it's possible to do better.
 */

#define FANOUT 26
int char_index(int ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A';
    else if (ch >= 'a' && ch <= 'z')
        return ch - 'a';
    else
        return -1;
}

struct bucket {
    int prefixlen;
    struct bucket *next_bucket;
    struct node *first_node;
};

struct node {
    const char *string;
    int len, prefixlen;
    struct node *next;
};

struct node *new_node(struct node *prev_head, const char *string, int len)
{
    struct node *ret = (struct node *)malloc(sizeof(struct node));

    if (!ret) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    ret->next = prev_head;
    ret->string = string;
    ret->len = len;

    return ret;
}

char *get_unused_env_prefix(void)
{
    struct bucket *qhead, *qtail;
    extern char **environ;
    char **e;

    qhead = (struct bucket *)malloc(sizeof(struct bucket));
    if (!qhead) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    qhead->prefixlen = 0;
    qhead->first_node = NULL;
    qhead->next_bucket = NULL;
    for (e = environ; *e; e++)
        qhead->first_node = new_node(qhead->first_node, *e, strcspn(*e, "="));

    qtail = qhead;
    while (1) {
        struct bucket *buckets[FANOUT];
        struct node *bucketnode;
        int i, index;

        for (i = 0; i < FANOUT; i++) {
            buckets[i] = (struct bucket *)malloc(sizeof(struct bucket));
            if (!buckets[i]) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            buckets[i]->prefixlen = qhead->prefixlen + 1;
            buckets[i]->first_node = NULL;
            qtail->next_bucket = buckets[i];
            qtail = buckets[i];
        }
        qtail->next_bucket = NULL;

        bucketnode = qhead->first_node;
        while (bucketnode) {
            struct node *node = bucketnode;
            bucketnode = bucketnode->next;

            if (node->len <= qhead->prefixlen)
                continue;
            index = char_index(node->string[qhead->prefixlen]);
            if (!(index >= 0 && index < FANOUT))
                continue;
            node->prefixlen++;
            node->next = buckets[index]->first_node;
            buckets[index]->first_node = node;
        }

        for (i = 0; i < FANOUT; i++) {
            if (!buckets[i]->first_node) {
                char *ret = malloc(qhead->prefixlen + 2);
                if (!ret) {
                    fprintf(stderr, "out of memory\n");
                    exit(1);
                }
                memcpy(ret, qhead->first_node->string, qhead->prefixlen);
                ret[qhead->prefixlen] = i + 'A';
                ret[qhead->prefixlen + 1] = '\0';

                /* This would be where we freed everything, if we
                 * didn't know it didn't matter because we were
                 * imminently going to exec another program */
                return ret;
            }
        }

        qhead = qhead->next_bucket;
    }
}

/* ----------------------------------------------------------------------
 * Get the pathname of this executable, so we can locate the rest of
 * the app bundle relative to it.
 */

/*
 * There are several ways to try to retrieve the pathname to the
 * running executable:
 *
 * (a) Declare main() as taking four arguments int main(int argc, char
 * **argv, char **envp, char **apple); and look at apple[0].
 *
 * (b) Use sysctl(KERN_PROCARGS) to get the process arguments for the
 * current pid. This involves two steps:
 *  - sysctl(mib, 2, &argmax, &argmax_size, NULL, 0)
 *     + mib is an array[2] of int containing
 *       { CTL_KERN, KERN_ARGMAX }
 *     + argmax is an int
 *     + argmax_size is a size_t initialised to sizeof(argmax)
 *     + returns in argmax the amount of memory you need for the next
 *       call.
 *  - sysctl(mib, 3, procargs, &procargs_size, NULL, 0)
 *     + mib is an array[3] of int containing
 *       { CTL_KERN, KERN_PROCARGS, current pid }
 *     + procargs is a buffer of size 'argmax'
 *     + procargs_size is a size_t initialised to argmax
 *     + returns in the procargs buffer a collection of
 *       zero-terminated strings of which the first is the program
 *       name.
 *
 * (c) Call _NSGetExecutablePath, once to find out the needed buffer
 * size and again to fetch the actual path.
 *
 * (d) Use Objective-C and Cocoa and call
 * [[[NSProcessInfo processInfo] arguments] objectAtIndex: 0].
 *
 * So, how do those work in various cases? Experiments show:
 *
 *  - if you run the program as 'binary' (or whatever you called it)
 *    and rely on the shell to search your PATH, all four methods
 *    return a sensible-looking absolute pathname.
 *
 *  - if you run the program as './binary', (a) and (b) return just
 *    "./binary", which has a particularly bad race condition if you
 *    try to convert it into an absolute pathname using realpath(3).
 *    (c) returns "/full/path/to/./binary", which still needs
 *    realpath(3)ing to get rid of that ".", but at least it's
 *    _trying_ to be fully qualified. (d) returns
 *    "/full/path/to/binary" - full marks!
 *     + Similar applies if you run it via a more interesting relative
 *       path such as one with a ".." in: (c) gives you an absolute
 *       path containing a ".." element, whereas (d) has sorted that
 *       out.
 *
 *  - if you run the program via a path with a symlink on, _none_ of
 *    these options successfully returns a path without the symlink.
 *
 * That last point suggests that even (d) is not a perfect solution on
 * its own, and you'll have to realpath() whatever you get back from
 * it regardless.
 *
 * And (d) is extra inconvenient because it returns an NSString, which
 * is implicitly Unicode, so it's not clear how you turn that back
 * into a char * representing a correct Unix pathname (what charset
 * should you interpret it in?). Also because you have to bring in all
 * of ObjC and Cocoa, which for a low-level Unix API client like this
 * seems like overkill.
 *
 * So my conclusion is that (c) is most practical for these purposes.
 */

char *get_program_path(void)
{
    char *our_path;
    uint32_t pathlen = 0;
    _NSGetExecutablePath(NULL, &pathlen);
    our_path = malloc(pathlen);
    if (!our_path) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    if (_NSGetExecutablePath(our_path, &pathlen)) {
        fprintf(stderr, "unable to get launcher executable path\n");
        exit(1);
    }

    /* OS X guarantees to malloc the return value if we pass NULL */
    char *our_real_path = realpath(our_path, NULL);
    if (!our_real_path) {
        fprintf(stderr, "realpath failed\n");
        exit(1);
    }

    free(our_path);
    return our_real_path;
}

/* ----------------------------------------------------------------------
 * Wrapper on dirname(3) which mallocs its return value to whatever
 * size is needed.
 */

char *dirname_wrapper(const char *path)
{
    char *path_copy = malloc(strlen(path) + 1);
    if (!path_copy) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(path_copy, path);
    char *ret_orig = dirname(path_copy);
    char *ret = malloc(strlen(ret_orig) + 1);
    if (!ret) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(ret, ret_orig);
    free(path_copy);
    return ret;
}

/* ----------------------------------------------------------------------
 * mallocing string concatenation function.
 */

char *alloc_cat(const char *str1, const char *str2)
{
    int len1 = strlen(str1), len2 = strlen(str2);
    char *ret = malloc(len1 + len2 + 1);
    if (!ret) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(ret, str1);
    strcpy(ret + len1, str2);
    return ret;
}

/* ----------------------------------------------------------------------
 * Overwrite an environment variable, preserving the old one for the
 * real app to restore.
 */
void setenv_wrap(const char *name, const char *value)
{
#ifdef DEBUG_OSXLAUNCH
    printf("setenv(\"%s\",\"%s\")\n", name, value);
#endif
    setenv(name, value, 1);
}

void unsetenv_wrap(const char *name)
{
#ifdef DEBUG_OSXLAUNCH
    printf("unsetenv(\"%s\")\n", name);
#endif
    unsetenv(name);
}

char *prefix, *prefixset, *prefixunset;
void overwrite_env(const char *name, const char *value)
{
    const char *oldvalue = getenv(name);
    if (oldvalue) {
        setenv_wrap(alloc_cat(prefixset, name), oldvalue);
    } else {
        setenv_wrap(alloc_cat(prefixunset, name), "");
    }
    if (value)
        setenv_wrap(name, value);
    else
        unsetenv_wrap(name);
}

/* ----------------------------------------------------------------------
 * Main program.
 */

int main(int argc, char **argv)
{
    prefix = get_unused_env_prefix();
    prefixset = alloc_cat(prefix, "s");
    prefixunset = alloc_cat(prefix, "u");

#ifdef DEBUG_OSXLAUNCH
    printf("Environment prefixes: main=\"%s\", set=\"%s\", unset=\"%s\"\n",
           prefix, prefixset, prefixunset);
#endif

    char *prog_path = get_program_path(); // <bundle>/Contents/MacOS/<filename>
    char *macos = dirname_wrapper(prog_path); // <bundle>/Contents/MacOS
    char *contents = dirname_wrapper(macos);  // <bundle>/Contents
//    char *bundle = dirname_wrapper(contents); // <bundle>
    char *resources = alloc_cat(contents, "/Resources");
//    char *bin = alloc_cat(resources, "/bin");
    char *etc = alloc_cat(resources, "/etc");
    char *lib = alloc_cat(resources, "/lib");
    char *share = alloc_cat(resources, "/share");
    char *xdg = alloc_cat(etc, "/xdg");
//    char *gtkrc = alloc_cat(etc, "/gtk-2.0/gtkrc");
    char *locale = alloc_cat(share, "/locale");
    char *realbin = alloc_cat(prog_path, "-bin");

//    overwrite_env("DYLD_LIBRARY_PATH", lib);
    overwrite_env("XDG_CONFIG_DIRS", xdg);
    overwrite_env("XDG_DATA_DIRS", share);
    overwrite_env("GTK_DATA_PREFIX", resources);
    overwrite_env("GTK_EXE_PREFIX", resources);
    overwrite_env("GTK_PATH", resources);
    overwrite_env("PANGO_LIBDIR", lib);
    overwrite_env("PANGO_SYSCONFDIR", etc);
    overwrite_env("I18NDIR", locale);
    overwrite_env("LANG", NULL);
    overwrite_env("LC_MESSAGES", NULL);
    overwrite_env("LC_MONETARY", NULL);
    overwrite_env("LC_COLLATE", NULL);

    char **new_argv = malloc((argc + 16) * sizeof(const char *));
    if (!new_argv) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    int j = 0;
    new_argv[j++] = realbin;
#ifdef DEBUG_OSXLAUNCH
    printf("argv[%d] = \"%s\"\n", j-1, new_argv[j-1]);
#endif
    {
        int i = 1;
        if (i < argc && !strncmp(argv[i], "-psn_", 5))
            i++;

        for (; i < argc; i++) {
            new_argv[j++] = argv[i];
#ifdef DEBUG_OSXLAUNCH
            printf("argv[%d] = \"%s\"\n", j-1, new_argv[j-1]);
#endif
        }
    }
    new_argv[j++] = prefix;
#ifdef DEBUG_OSXLAUNCH
    printf("argv[%d] = \"%s\"\n", j-1, new_argv[j-1]);
#endif
    new_argv[j++] = NULL;

#ifdef DEBUG_OSXLAUNCH
    printf("executing \"%s\"\n", realbin);
#endif
    execv(realbin, new_argv);
    perror("execv");
    free(new_argv);
    free(contents);
    free(macos);
    return 127;
}

#endif /* __APPLE__ */
