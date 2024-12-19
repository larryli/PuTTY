/*
 * Implementation of the CmdlineArg abstraction for Unix
 */

#include "putty.h"

typedef struct CmdlineArgUnix CmdlineArgUnix;
struct CmdlineArgUnix {
    /*
     * This is a writable char *, because the arguments received by
     * main() really are writable, and moreover, you _want_ to write
     * over them in some circumstances, to manipulate how your program
     * shows up in ps(1). Our example is wiping out the argument to
     * the -pw option. This isn't robust - you need to not use that
     * option at all if you want zero risk of password exposure
     * through ps - but we do the best we can.
     *
     * Some CmdlineArg structures are invented after the program
     * starts, in which case they don't correspond to real argv words
     * at all, and this pointer is NULL.
     */
    char *argv_word;

    /*
     * A CmdlineArg invented later might need to store a string that
     * will be freed when it goes away. This pointer is non-NULL if
     * freeing needs to happen.
     */
    char *to_free;

    /*
     * This const char * is the real string value of the argument.
     */
    const char *value;

    /*
     * Our index in the CmdlineArgList, or (size_t)-1 if we don't have
     * one and are an argument invented later.
     */
    size_t index;

    /*
     * Public part of the structure.
     */
    CmdlineArg argp;
};

static CmdlineArgUnix *cmdline_arg_new_in_list(CmdlineArgList *list)
{
    CmdlineArgUnix *arg = snew(CmdlineArgUnix);
    arg->argv_word = NULL;
    arg->to_free = NULL;
    arg->value = NULL;
    arg->index = (size_t)-1;
    arg->argp.list = list;
    sgrowarray(list->args, list->argssize, list->nargs);
    list->args[list->nargs++] = &arg->argp;
    return arg;
}

static CmdlineArg *cmdline_arg_from_argv_word(CmdlineArgList *list, char *word)
{
    CmdlineArgUnix *arg = cmdline_arg_new_in_list(list);
    arg->argv_word = word;
    arg->value = arg->argv_word;
    return &arg->argp;
}

CmdlineArgList *cmdline_arg_list_from_argv(int argc, char **argv)
{
    CmdlineArgList *list = snew(CmdlineArgList);
    list->args = NULL;
    list->nargs = list->argssize = 0;
    for (int i = 1; i < argc; i++) {
        CmdlineArg *argp = cmdline_arg_from_argv_word(list, argv[i]);
        CmdlineArgUnix *arg = container_of(argp, CmdlineArgUnix, argp);
        arg->index = i - 1; /* index in list->args[], not in argv[] */
    }
    sgrowarray(list->args, list->argssize, list->nargs);
    list->args[list->nargs++] = NULL;
    return list;
}

void cmdline_arg_free(CmdlineArg *argp)
{
    if (!argp)
        return;

    CmdlineArgUnix *arg = container_of(argp, CmdlineArgUnix, argp);
    if (arg->to_free)
        burnstr(arg->to_free);
    sfree(arg);
}

void cmdline_arg_list_free(CmdlineArgList *list)
{
    for (size_t i = 0; i < list->nargs; i++)
        cmdline_arg_free(list->args[i]);
    sfree(list->args);
    sfree(list);
}

CmdlineArg *cmdline_arg_from_str(CmdlineArgList *list, const char *string)
{
    CmdlineArgUnix *arg = cmdline_arg_new_in_list(list);
    arg->to_free = dupstr(string);
    arg->value = arg->to_free;
    return &arg->argp;
}

const char *cmdline_arg_to_str(CmdlineArg *argp)
{
    if (!argp)
        return NULL;

    CmdlineArgUnix *arg = container_of(argp, CmdlineArgUnix, argp);
    return arg->value;
}

const char *cmdline_arg_to_utf8(CmdlineArg *argp)
{
    /* For the moment, return NULL. But perhaps it makes sense to
     * convert from the default locale into UTF-8? */
    return NULL;
}

Filename *cmdline_arg_to_filename(CmdlineArg *argp)
{
    if (!argp)
        return NULL;

    CmdlineArgUnix *arg = container_of(argp, CmdlineArgUnix, argp);
    return filename_from_str(arg->value);
}

void cmdline_arg_wipe(CmdlineArg *argp)
{
    if (!argp)
        return;

    CmdlineArgUnix *arg = container_of(argp, CmdlineArgUnix, argp);
    if (arg->argv_word)
        smemclr(arg->argv_word, strlen(arg->argv_word));
}

char **cmdline_arg_remainder(CmdlineArg *argp)
{
    CmdlineArgUnix *arg = container_of(argp, CmdlineArgUnix, argp);
    CmdlineArgList *list = argp->list;

    size_t index = arg->index;
    assert(index != (size_t)-1);

    size_t nargs = 0;
    while (list->args[index + nargs])
        nargs++;

    char **argv = snewn(nargs + 1, char *);
    for (size_t i = 0; i < nargs; i++) {
        CmdlineArg *ith_argp = list->args[index + i];
        CmdlineArgUnix *ith_arg = container_of(ith_argp, CmdlineArgUnix, argp);
        argv[i] = ith_arg->argv_word;
    }
    argv[nargs] = NULL;

    return argv;
}
