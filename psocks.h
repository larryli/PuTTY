typedef struct psocks_state psocks_state;

typedef struct PsocksPlatform PsocksPlatform;
typedef struct PsocksDataSink PsocksDataSink;

/* indices into PsocksDataSink arrays */
typedef enum PsocksDirection { UP, DN } PsocksDirection;

struct PsocksDataSink {
    void (*free)(PsocksDataSink *);
    BinarySink *s[2];
};
static inline void pds_free(PsocksDataSink *pds)
{ pds->free(pds); }

PsocksDataSink *pds_stdio(FILE *fp[2]);

struct PsocksPlatform {
    PsocksDataSink *(*open_pipes)(
        const char *cmd, const char *const *direction_args,
        const char *index_arg, char **err);
    void (*found_subcommand)(CmdlineArg *arg);
    void (*start_subcommand)(void);
};

psocks_state *psocks_new(const PsocksPlatform *);
void psocks_free(psocks_state *ps);
void psocks_cmdline(psocks_state *ps, CmdlineArgList *arglist);
void psocks_start(psocks_state *ps);
