/*
 * winstuff.h: Windows-specific inter-module stuff.
 */

/*
 * Global variables. Most modules declare these `extern', but
 * window.c will do `#define PUTTY_DO_GLOBALS' before including this
 * module, and so will get them properly defined.
 */
#ifdef PUTTY_DO_GLOBALS
#define GLOBAL
#else
#define GLOBAL extern
#endif

GLOBAL HINSTANCE hinst;

/*
 * Exports from winctrls.c.
 */

struct ctlpos {
    HWND hwnd;
    WPARAM font;
    int dlu4inpix;
    int ypos, width;
    int xoff;
    int boxystart, boxid, boxtextid;
    char *boxtext;
};

void ctlposinit(struct ctlpos *cp, HWND hwnd,
                int leftborder, int rightborder, int topborder);
void doctl(struct ctlpos *cp, RECT r,
           char *wclass, int wstyle, int exstyle,
           char *wtext, int wid);
void bartitle(struct ctlpos *cp, char *name, int id);
void beginbox(struct ctlpos *cp, char *name, int idbox, int idtext);
void endbox(struct ctlpos *cp);
void multiedit(struct ctlpos *cp, ...);
void radioline(struct ctlpos *cp,
               char *text, int id, int nacross, ...);
void radiobig(struct ctlpos *cp, char *text, int id, ...);
void checkbox(struct ctlpos *cp, char *text, int id);
void staticbtn(struct ctlpos *cp, char *stext, int sid,
               char *btext, int bid);
void staticedit(struct ctlpos *cp, char *stext,
                int sid, int eid, int percentedit);
void ersatztab(struct ctlpos *cp, char *stext, int sid,
               int lid, int s2id);
void editbutton(struct ctlpos *cp, char *stext, int sid,
                int eid, char *btext, int bid);
void sesssaver(struct ctlpos *cp, char *text,
               int staticid, int editid, int listid, ...);
void envsetter(struct ctlpos *cp, char *stext, int sid,
               char *e1stext, int e1sid, int e1id,
               char *e2stext, int e2sid, int e2id,
               int listid,
               char *b1text, int b1id, char *b2text, int b2id);
void charclass(struct ctlpos *cp, char *stext, int sid, int listid,
               char *btext, int bid, int eid, char *s2text, int s2id);
void colouredit(struct ctlpos *cp, char *stext, int sid, int listid,
                char *btext, int bid, ...);
