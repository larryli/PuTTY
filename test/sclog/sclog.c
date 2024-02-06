/*
 * sclog: the DynamoRIO instrumentation system that goes with the
 * PuTTY test binary 'testsc'.
 *
 * For general discussion and build instructions, see the comment at
 * the top of testsc.c.
 */

#include <inttypes.h>
#include <string.h>

#include "dr_api.h"
#include "drmgr.h"
#include "drsyms.h"
#include "drreg.h"
#include "drutil.h"
#include "drwrap.h"

/*
 * The file we're currently logging to, if any.
 */
static file_t outfile = INVALID_FILE;

/*
 * A counter which we can increment and decrement around any library
 * function we don't want to log the details of what happens inside.
 * Mainly this is for memory allocation functions, which will diverge
 * control depending on the progress of their search for something
 * they can allocate.
 */
size_t logging_paused = 0;

/*
 * This log message appears at the start of whatever DynamoRIO
 * considers a 'basic block', i.e. a sequence of instructions with no
 * branches. Logging these is cheaper than logging every single
 * instruction, and should still be adequate to detect any divergence
 * of control flow.
 */
static void log_pc(const char *loc)
{
    if (outfile == INVALID_FILE || logging_paused)
        return;
    dr_fprintf(outfile, "%s: start basic block\n", loc);
}

/*
 * Hardware division instructions are unlikely to run in time
 * independent of the data, so we log both their parameters.
 */
static void log_div(uint n, uint d, const char *loc)
{
    if (outfile == INVALID_FILE || logging_paused)
        return;
    dr_fprintf(outfile, "%s: divide %"PRIuMAX" / %"PRIuMAX"\n",
               loc, (uintmax_t)n, (uintmax_t)d);
}

/*
 * Register-controlled shift instructions are not reliably one cycle
 * long on all platforms, so we log the shift couhnt.
 */
static void log_var_shift(uint sh, const char *loc)
{
    if (outfile == INVALID_FILE || logging_paused)
        return;
    dr_fprintf(outfile, "%s: var shift by %"PRIuMAX"\n", loc, (uintmax_t)sh);
}

/*
 * We need to log memory accesses, so as to detect data-dependent
 * changes in the access pattern (e.g. incautious use of a lookup
 * table). But one thing we _can't_ control for perfectly is that in
 * two successive runs of the same crypto primitive, malloc may be
 * called, and may return different addresses - which of course is not
 * dependent on the data (unless the size of the allocated block
 * does).
 *
 * So we track all the memory allocations that happen during logging,
 * and any addresses accessed within those blocks are logged as
 * something along the lines of 'n bytes from the start of the mth
 * allocation'.
 *
 * Allocations that happened before a given log file was opened are
 * not tracked. The program under test will ensure that any of those
 * used by the primitive are at the same address in all runs anyway.
 */
struct allocation {
    /*
     * We store the list of allocations in a linked list, so we can
     * look them up by address, and delete them as they're freed.
     *
     * A balanced binary search tree would be faster, but this is
     * easier to get right first time!
     */
    struct allocation *prev, *next;
    uintptr_t start, size, index;
};
static struct allocation alloc_ends[1] = { alloc_ends, alloc_ends, 0, 0, 0 };
static uintptr_t next_alloc_index = 0;

static void free_allocation(struct allocation *alloc)
{
    alloc->next->prev = alloc->prev;
    alloc->prev->next = alloc->next;
    dr_global_free(alloc, sizeof(struct allocation));
}

/*
 * Wrap the log_set_file() function in testsc.c, and respond to it by
 * opening or closing log files.
 */
static void wrap_logsetfile(void *wrapctx, void **user_data)
{
    if (outfile) {
        dr_close_file(outfile);
        outfile = INVALID_FILE;
    }

    const char *outfilename = drwrap_get_arg(wrapctx, 0);
    if (outfilename) {
        outfile = dr_open_file(outfilename, DR_FILE_WRITE_OVERWRITE);
        DR_ASSERT(outfile != INVALID_FILE);
    }

    /*
     * Reset the allocation list to empty, whenever we open or close a
     * log file.
     */
    while (alloc_ends->next != alloc_ends)
        free_allocation(alloc_ends->next);
    next_alloc_index = 0;
}

/*
 * Wrap the dry_run() function in testsc.c, to tell it we're here.
 */
static void wrap_dryrun(void *wrapctx, void *user_data)
{
    drwrap_set_retval(wrapctx, (void *)0);
}

/*
 * Look up the memory allocation record corresponding to an address.
 */
static struct allocation *find_allocation(const void *ptr)
{
    uintptr_t address = (uintptr_t)ptr;
    for (struct allocation *alloc = alloc_ends->next;
         alloc != alloc_ends; alloc = alloc->next) {
        if (alloc && address - alloc->start < alloc->size)
            return alloc;
    }
    return NULL;
}

/*
 * Log a memory access.
 */
static void log_mem(app_pc addr, uint size, uint write, const char *loc)
{
    if (outfile == INVALID_FILE || logging_paused)
        return;

    struct allocation *alloc = find_allocation((const void *)addr);
    if (!alloc) {
        dr_fprintf(outfile, "%s: %s %"PRIuMAX" @ %"PRIxMAX"\n",
                   loc, write ? "store" : "load", (uintmax_t)size,
                   (uintmax_t)addr);
    } else {
        dr_fprintf(outfile, "%s: %s %"PRIuMAX" @ allocations[%"PRIuPTR"]"
                   " + %"PRIxMAX"\n",
                   loc, write ? "store" : "load", (uintmax_t)size,
                   alloc->index, (uintmax_t)(addr - alloc->start));
    }
}

/*
 * Record the allocation of some memory. (Common code between malloc
 * and realloc.)
 */
static void allocated(void *ptr, size_t size)
{
    if (outfile == INVALID_FILE)
        return; /* no need to track allocations outside a logging interval */

    struct allocation *alloc = dr_global_alloc(sizeof(struct allocation));
    alloc->start = (uintptr_t)ptr;
    alloc->size = size;
    alloc->index = next_alloc_index++;
    alloc->prev = alloc_ends->prev;
    alloc->next = alloc_ends;
    alloc->prev->next = alloc->next->prev = alloc;
}

/*
 * Record that memory has been freed. Note that we may free something
 * that was allocated when we weren't logging, so we must cope with
 * find_allocation returning NULL.
 */
static void freed(void *ptr)
{
    struct allocation *alloc = find_allocation(ptr);
    if (alloc)
        free_allocation(alloc);
}

/*
 * The actual wrapper functions for malloc, realloc and free.
 */
static void wrap_malloc_pre(void *wrapctx, void **user_data)
{
    logging_paused++;
    *user_data = drwrap_get_arg(wrapctx, 0);
    dr_fprintf(outfile, "malloc %"PRIuMAX"\n", (uintmax_t)*user_data);
}
static void wrap_free_pre(void *wrapctx, void **user_data)
{
    logging_paused++;
    void *ptr = drwrap_get_arg(wrapctx, 0);
    freed(ptr);
}
static void wrap_realloc_pre(void *wrapctx, void **user_data)
{
    logging_paused++;
    void *ptr = drwrap_get_arg(wrapctx, 0);
    freed(ptr);
    *user_data = drwrap_get_arg(wrapctx, 1);
    dr_fprintf(outfile, "realloc %"PRIuMAX"\n", (uintmax_t)*user_data);
}
static void wrap_alloc_post(void *wrapctx, void *user_data)
{
    void *ptr = drwrap_get_retval(wrapctx);
    if (!ptr)
        return;
    size_t size = (size_t)user_data;
    allocated(ptr, size);
    logging_paused--;
}

/*
 * We wrap the C library function memset, because I've noticed that at
 * least one optimised implementation of it diverges control flow
 * internally based on what appears to be the _alignment_ of the input
 * pointer - and that alignment check can vary depending on the
 * addresses of allocated blocks. So I can't guarantee no divergence
 * of control flow inside memset if malloc doesn't return the same
 * values, and instead I just have to trust that memset isn't reading
 * the contents of the block and basing control flow decisions on that.
 */
static void wrap_memset_pre(void *wrapctx, void **user_data)
{
    uint was_already_paused = logging_paused++;

    if (outfile == INVALID_FILE || was_already_paused)
        return;

    const void *addr = drwrap_get_arg(wrapctx, 0);
    size_t size = (size_t)drwrap_get_arg(wrapctx, 2);

    struct allocation *alloc = find_allocation(addr);
    if (!alloc) {
        dr_fprintf(outfile, "memset %"PRIuMAX" @ %"PRIxMAX"\n",
                   (uintmax_t)size, (uintmax_t)addr);
    } else {
        dr_fprintf(outfile, "memset %"PRIuMAX" @ allocations[%"PRIuPTR"]"
                   " + %"PRIxMAX"\n", (uintmax_t)size, alloc->index,
                   (uintmax_t)(addr - alloc->start));
    }
}

/*
 * Similarly to the above, wrap some versions of memmove.
 */
static void wrap_memmove_pre(void *wrapctx, void **user_data)
{
    uint was_already_paused = logging_paused++;

    if (outfile == INVALID_FILE || was_already_paused)
        return;

    const void *daddr = drwrap_get_arg(wrapctx, 0);
    const void *saddr = drwrap_get_arg(wrapctx, 1);
    size_t size = (size_t)drwrap_get_arg(wrapctx, 2);


    struct allocation *alloc;

    dr_fprintf(outfile, "memmove %"PRIuMAX" ", (uintmax_t)size);
    if (!(alloc = find_allocation(daddr))) {
        dr_fprintf(outfile, "to %"PRIxMAX" ", (uintmax_t)daddr);
    } else {
        dr_fprintf(outfile, "to allocations[%"PRIuPTR"] + %"PRIxMAX" ",
                   alloc->index, (uintmax_t)(daddr - alloc->start));
    }
    if (!(alloc = find_allocation(saddr))) {
        dr_fprintf(outfile, "from %"PRIxMAX"\n", (uintmax_t)saddr);
    } else {
        dr_fprintf(outfile, "from allocations[%"PRIuPTR"] + %"PRIxMAX"\n",
                   alloc->index, (uintmax_t)(saddr - alloc->start));
    }
}

/*
 * Common post-wrapper function for memset and free, whose entire
 * function is to unpause the logging.
 */
static void unpause_post(void *wrapctx, void *user_data)
{
    logging_paused--;
}

/*
 * Make a string representation of the address of an instruction,
 * including a function name and/or a file+line combination if
 * possible. These will be logged alongside every act of interest
 * where we can make one.
 */
static void instr_format_location(instr_t *instr, char **outloc)
{
    app_pc addr = (app_pc)instr_get_app_pc(instr);
    char location[2048], symbol[512], fileline[1024];
    bool got_sym = false, got_line = false;

    if (*outloc)
        return;

    symbol[0] = '\0';
    fileline[0] = '\0';

    module_data_t *data = dr_lookup_module(addr);
    if (data) {
        drsym_info_t sym;
        char file[MAXIMUM_PATH];

        sym.struct_size = sizeof(sym);
        sym.name = symbol;
        sym.name_size = sizeof(symbol);
        sym.file = file;
        sym.file_size = sizeof(file);

        drsym_error_t status = drsym_lookup_address(
            data->full_path, addr - data->start, &sym, DRSYM_DEFAULT_FLAGS);

        got_line = (status == DRSYM_SUCCESS);
        got_sym = got_line || status == DRSYM_ERROR_LINE_NOT_AVAILABLE;

        if (got_line)
            snprintf(fileline, sizeof(fileline), " = %s:%"PRIu64,
                     file, (uint64_t)sym.line);
    }

    snprintf(location, sizeof(location),
             "%"PRIx64"%s%s%s",
             (uint64_t)addr, got_sym ? " = " : "", got_sym ? symbol : "",
             fileline);
    size_t len = strlen(location) + 1;
    char *loc = dr_global_alloc(len);
    memcpy(loc, location, len);
    *outloc = loc;
}

/*
 * Function that tests a single operand of an instruction to see if
 * it's a memory reference, and if so, adds a call to log_mem.
 */
static void try_mem_opnd(
    void *drcontext, instrlist_t *bb, instr_t *instr, char **loc,
    opnd_t opnd, bool write)
{
    if (!opnd_is_memory_reference(opnd))
        return;

    instr_format_location(instr, loc);

    reg_id_t r0, r1;
    drreg_status_t st;
    st = drreg_reserve_register(drcontext, bb, instr, NULL, &r0);
    DR_ASSERT(st == DRREG_SUCCESS);
    st = drreg_reserve_register(drcontext, bb, instr, NULL, &r1);
    DR_ASSERT(st == DRREG_SUCCESS);

    bool ok = drutil_insert_get_mem_addr(drcontext, bb, instr, opnd, r0, r1);
    DR_ASSERT(ok);

    uint size = drutil_opnd_mem_size_in_bytes(opnd, instr);

    dr_insert_clean_call(
        drcontext, bb, instr, (void *)log_mem, false,
        4, opnd_create_reg(r0), OPND_CREATE_INT32(size),
        OPND_CREATE_INT32(write), OPND_CREATE_INTPTR(*loc));

    st = drreg_unreserve_register(drcontext, bb, instr, r1);
    DR_ASSERT(st == DRREG_SUCCESS);
    st = drreg_unreserve_register(drcontext, bb, instr, r0);
    DR_ASSERT(st == DRREG_SUCCESS);
}

/*
 * The main function called to instrument each machine instruction.
 */
static dr_emit_flags_t instrument_instr(
    void *drcontext, void *tag, instrlist_t *bb, instr_t *instr,
    bool for_trace, bool translating, void *user_data)
{
    char *loc = NULL;

    /*
     * If this instruction is the first in its basic block, call
     * log_pc to record that we're executing this block at all.
     */
    if (drmgr_is_first_instr(drcontext, instr)) {
        instr_format_location(instr, &loc);
        dr_insert_clean_call(
            drcontext, bb, instr, (void *)log_pc, false,
            1, OPND_CREATE_INTPTR(loc));
    }

    /*
     * If the instruction reads or writes memory, log its access.
     */
    if (instr_reads_memory(instr) || instr_writes_memory(instr)) {
        for (int i = 0, limit = instr_num_srcs(instr); i < limit; i++)
            try_mem_opnd(drcontext, bb, instr, &loc,
                         instr_get_src(instr, i), instr_writes_memory(instr));
        for (int i = 0, limit = instr_num_dsts(instr); i < limit; i++)
            try_mem_opnd(drcontext, bb, instr, &loc,
                         instr_get_dst(instr, i), instr_writes_memory(instr));
    }

    /*
     * Now do opcode-specific checks.
     */
    int opcode = instr_get_opcode(instr);

    switch (opcode) {
#if defined(X86)
      case OP_div:
      case OP_idiv:
        /*
         * x86 hardware divisions. The operand order for DR's
         * representation of these seem to be: 0 = denominator, 1 =
         * numerator MSW, 2 = numerator LSW.
         */
        instr_format_location(instr, &loc);
        dr_insert_clean_call(
            drcontext, bb, instr, (void *)log_div, false,
            3, instr_get_src(instr, 2), instr_get_src(instr, 0),
            OPND_CREATE_INTPTR(loc));
        break;
#endif
#if defined(AARCH64)
      case OP_sdiv:
      case OP_udiv:
        /*
         * AArch64 hardware divisions. 0 = numerator, 1 = denominator.
         */
        instr_format_location(instr, &loc);
        dr_insert_clean_call(
            drcontext, bb, instr, (void *)log_div, false,
            3, instr_get_src(instr, 0), instr_get_src(instr, 1),
            OPND_CREATE_INTPTR(loc));
        break;
#endif
#if defined(X86)
      case OP_shl:
      case OP_shr:
      case OP_sar:
      case OP_shlx:
      case OP_shrx:
      case OP_sarx:
      case OP_rol:
      case OP_ror:
      case OP_rcl:
      case OP_rcr: {
        /*
         * Shift instructions. If they're register-controlled, log the
         * shift count.
         */
        opnd_t shiftcount = instr_get_src(instr, 0);
        if (!opnd_is_immed(shiftcount)) {
            reg_id_t r0;
            drreg_status_t st;
            st = drreg_reserve_register(drcontext, bb, instr, NULL, &r0);
            DR_ASSERT(st == DRREG_SUCCESS);
            opnd_t op_r0 = opnd_create_reg(r0);
            instr_t *movzx = INSTR_CREATE_movzx(drcontext, op_r0, shiftcount);
            instr_set_translation(movzx, instr_get_app_pc(instr));
            instrlist_preinsert(bb, instr, movzx);
            instr_format_location(instr, &loc);
            dr_insert_clean_call(
                drcontext, bb, instr, (void *)log_var_shift, false,
                2, op_r0, OPND_CREATE_INTPTR(loc));
            st = drreg_unreserve_register(drcontext, bb, instr, r0);
            DR_ASSERT(st == DRREG_SUCCESS);
        }
        break;
      }
#endif
#if defined(AARCH64)
      case OP_lslv:
      case OP_asrv:
      case OP_lsrv:
      case OP_rorv: {
        /*
         * AArch64 variable shift instructions.
         */
        opnd_t shiftcount = instr_get_src(instr, 1);
        DR_ASSERT(opnd_is_reg(shiftcount));
        reg_id_t shiftreg = opnd_get_reg(shiftcount);
        if (shiftreg >= DR_REG_W0 && shiftreg <= DR_REG_WSP)
            shiftreg = reg_32_to_64(shiftreg);
        instr_format_location(instr, &loc);
        dr_insert_clean_call(
            drcontext, bb, instr, (void *)log_var_shift, false,
            2, opnd_create_reg(shiftreg), OPND_CREATE_INTPTR(loc));
        break;
      }
#endif
    }

    return DR_EMIT_DEFAULT;
}

static void exit_event(void)
{
    if (outfile != INVALID_FILE) {
        dr_fprintf(outfile, "exit while recording enabled\n");
        dr_close_file(outfile);
        outfile = INVALID_FILE;
    }
    drsym_exit();
    drreg_exit();
    drwrap_exit();
    drutil_exit();
    drmgr_exit();
}

/*
 * We ask DR to expand any x86 string instructions like REP MOVSB, so
 * that we can log all the individual memory accesses without getting
 * confused.
 */
static dr_emit_flags_t expand_rep_movsb(
    void *drcontext, void *tag, instrlist_t *bb, bool for_trace,
    bool translating)
{
    bool ok = drutil_expand_rep_string(drcontext, bb);
    DR_ASSERT(ok);
    return DR_EMIT_DEFAULT;
}

typedef void (*prewrapper_t)(void *wrapctx, void **user_data);
typedef void (*postwrapper_t)(void *wrapctx, void *user_data);

/*
 * Helper function for bulk use of drwrap.
 */
static void try_wrap_fn(const module_data_t *module, const char *name,
                        prewrapper_t pre, postwrapper_t post, bool *done)
{
    if (*done)
        return;

    size_t offset;
    drsym_error_t status = drsym_lookup_symbol(
        module->full_path, name, &offset, DRSYM_DEFAULT_FLAGS);
    if (status == DRSYM_SUCCESS) {
        app_pc notify_fn = module->start + offset;
        bool ok = drwrap_wrap(notify_fn, pre, post);
        DR_ASSERT(ok);
        *done = true;
    }
}

/*
 * When each module (e.g. shared library) is loaded, try to wrap all
 * the functions we care about. For each one, we keep a static bool
 * that will stop us trying again once we've found it the first time.
 */
static void load_module(
    void *drcontext, const module_data_t *module, bool loaded)
{
    bool libc = !strncmp(dr_module_preferred_name(module), "libc", 4);

#define TRY_WRAP(fn, pre, post) do                              \
    {                                                           \
        static bool done_this_one = false;                      \
        try_wrap_fn(module, fn, pre, post, &done_this_one);     \
    } while (0)

    if (loaded) {
        TRY_WRAP("log_to_file_real", wrap_logsetfile, NULL);
        TRY_WRAP("dry_run_real", NULL, wrap_dryrun);
        if (libc) {
            TRY_WRAP("malloc", wrap_malloc_pre, wrap_alloc_post);
            TRY_WRAP("realloc", wrap_realloc_pre, wrap_alloc_post);
            TRY_WRAP("free", wrap_free_pre, unpause_post);
            TRY_WRAP("memset", wrap_memset_pre, unpause_post);
            TRY_WRAP("memmove", wrap_memmove_pre, unpause_post);

            /*
             * More strangely named versions of standard C library
             * functions, which I've observed in practice to be where the
             * calls end up. I think these are probably selected by
             * STT_IFUNC in libc.so, so that the normally named version of
             * the function is never reached at all.
             *
             * This list is not expected to be complete. If you re-run
             * this test on a different platform and find control flow
             * diverging inside some libc function that looks as if it's
             * another name for malloc or memset or whatever, then you may
             * need to add more aliases here to stop the test failing.
             */
            TRY_WRAP("__GI___libc_malloc", wrap_malloc_pre, wrap_alloc_post);
            TRY_WRAP("__libc_malloc", wrap_malloc_pre, wrap_alloc_post);
            TRY_WRAP("__GI___libc_realloc", wrap_realloc_pre, wrap_alloc_post);
            TRY_WRAP("__GI___libc_free", wrap_free_pre, unpause_post);
            TRY_WRAP("__memset_sse2_unaligned", wrap_memset_pre, unpause_post);
            TRY_WRAP("__memset_sse2", wrap_memset_pre, unpause_post);
            TRY_WRAP("__memmove_avx_unaligned_erms", wrap_memmove_pre,
                     unpause_post);
            TRY_WRAP("cfree", wrap_free_pre, unpause_post);
        }
    }
}

/*
 * Main entry point that sets up all the facilities we need.
 */
DR_EXPORT void dr_client_main(client_id_t id, int argc, const char **argv)
{
    dr_set_client_name(
        "Time-sensitive activity logger for PuTTY crypto testing",
        "https://www.chiark.greenend.org.uk/~sgtatham/putty/");

    outfile = INVALID_FILE;

    bool ok = drmgr_init();
    DR_ASSERT(ok);

    /*
     * Run our main instrumentation pass with lower priority than
     * drwrap, so that we don't start logging the inside of a function
     * whose drwrap pre-wrapper would have wanted to disable logging.
     */
    drmgr_priority_t pri = {sizeof(pri), "sclog", NULL, NULL,
                            DRMGR_PRIORITY_INSERT_DRWRAP+1};
    ok = drmgr_register_bb_instrumentation_event(
        NULL, instrument_instr, &pri);
    DR_ASSERT(ok);

    ok = drutil_init();
    DR_ASSERT(ok);

    ok = drwrap_init();
    DR_ASSERT(ok);

    drsym_error_t symstatus = drsym_init(0);
    DR_ASSERT(symstatus == DRSYM_SUCCESS);

    dr_register_exit_event(exit_event);

    drreg_options_t ops = { sizeof(ops), 3, false };
    drreg_status_t regstatus = drreg_init(&ops);
    DR_ASSERT(regstatus == DRREG_SUCCESS);

    drmgr_register_module_load_event(load_module);

    ok = drmgr_register_bb_app2app_event(expand_rep_movsb, NULL);
    DR_ASSERT(ok);
}
