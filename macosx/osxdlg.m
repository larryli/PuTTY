/*
 * osxdlg.m: various PuTTY dialog boxes for OS X.
 */

#import <Cocoa/Cocoa.h>
#include "putty.h"
#include "storage.h"
#include "dialog.h"
#include "osxclass.h"

/*
 * The `ConfigWindow' class is used to start up a new PuTTY
 * session.
 */

@class ConfigTree;
@interface ConfigTree : NSObject
{
    NSString **paths;
    int *levels;
    int nitems, itemsize;
}
- (void)addPath:(char *)path;
@end

@implementation ConfigTree
- (id)init
{
    self = [super init];
    paths = NULL;
    levels = NULL;
    nitems = itemsize = 0;
    return self;
}
- (void)addPath:(char *)path
{
    if (nitems >= itemsize) {
	itemsize += 32;
	paths = sresize(paths, itemsize, NSString *);
	levels = sresize(levels, itemsize, int);
    }
    paths[nitems] = [[NSString stringWithCString:path] retain];
    levels[nitems] = ctrl_path_elements(path) - 1;
    nitems++;
}
- (void)dealloc
{
    int i;

    for (i = 0; i < nitems; i++)
	[paths[i] release];

    sfree(paths);
    sfree(levels);

    [super dealloc];
}
- (id)iterateChildren:(int)index ofItem:(id)item count:(int *)count
{
    int i, plevel;

    if (item) {
	for (i = 0; i < nitems; i++)
	    if (paths[i] == item)
		break;
	assert(i < nitems);
	plevel = levels[i];
	i++;
    } else {
	i = 0;
	plevel = -1;
    }

    if (count)
	*count = 0;

    while (index > 0) {
	if (i >= nitems || levels[i] != plevel+1)
	    return nil;
	if (count)
	    (*count)++;
	do {
	    i++;
	} while (i < nitems && levels[i] > plevel+1);
	index--;
    }

    return paths[i];
}
- (id)outlineView:(NSOutlineView *)outlineView child:(int)index ofItem:(id)item
{
    return [self iterateChildren:index ofItem:item count:NULL];
}
- (int)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item
{
    int count = 0;
    /* pass nitems+1 to ensure we run off the end */
    [self iterateChildren:nitems+1 ofItem:item count:&count];
    return count;
}
- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item
{
    return [self outlineView:outlineView numberOfChildrenOfItem:item] > 0;
}
- (id)outlineView:(NSOutlineView *)outlineView objectValueForTableColumn:(NSTableColumn *)tableColumn byItem:(id)item
{
    /*
     * Trim off all path elements except the last one.
     */
    NSArray *components = [item componentsSeparatedByString:@"/"];
    return [components objectAtIndex:[components count]-1];
}
@end

@implementation ConfigWindow
- (id)initWithConfig:(Config)aCfg
{
    NSScrollView *scrollview;
    NSTableColumn *col;
    ConfigTree *treedata;
    int by = 0, mby = 0;
    int wmin = 0;
    int hmin = 0;
    int panelht = 0;

    get_sesslist(&sl, TRUE);

    ctrlbox = ctrl_new_box();
    setup_config_box(ctrlbox, &sl, FALSE /*midsession*/, aCfg.protocol,
		     0 /* protcfginfo */);
    unix_setup_config_box(ctrlbox, FALSE /*midsession*/);

    cfg = aCfg;			       /* structure copy */

    self = [super initWithContentRect:NSMakeRect(0,0,300,300)
	    styleMask:(NSTitledWindowMask | NSMiniaturizableWindowMask |
		       NSClosableWindowMask)
	    backing:NSBackingStoreBuffered
	    defer:YES];
    [self setTitle:@"PuTTY Configuration"];

    [self setIgnoresMouseEvents:NO];

    dv = fe_dlg_init(&cfg, self, self, @selector(configBoxFinished:));

    scrollview = [[NSScrollView alloc] initWithFrame:NSMakeRect(20,20,10,10)];
    treeview = [[NSOutlineView alloc] initWithFrame:[scrollview frame]];
    [scrollview setBorderType:NSLineBorder];
    [scrollview setDocumentView:treeview];
    [[self contentView] addSubview:scrollview];
    [scrollview setHasVerticalScroller:YES];
    [scrollview setAutohidesScrollers:YES];
    /* FIXME: the below is untested. Test it then remove this notice. */
    [treeview setAllowsColumnReordering:NO];
    [treeview setAllowsColumnResizing:NO];
    [treeview setAllowsMultipleSelection:NO];
    [treeview setAllowsEmptySelection:NO];
    [treeview setAllowsColumnSelection:YES];

    treedata = [[[ConfigTree alloc] init] retain];

    col = [[NSTableColumn alloc] initWithIdentifier:nil];
    [treeview addTableColumn:col];
    [treeview setOutlineTableColumn:col];

    [[treeview headerView] setFrame:NSMakeRect(0,0,0,0)];

    /*
     * Create the controls.
     */
    {
	int i;
	char *path = NULL;

	for (i = 0; i < ctrlbox->nctrlsets; i++) {
	    struct controlset *s = ctrlbox->ctrlsets[i];
	    int mw, mh;

	    if (!*s->pathname) {

		create_ctrls(dv, [self contentView], s, &mw, &mh);

		by += 20 + mh;

		if (wmin < mw + 40)
		    wmin = mw + 40;
	    } else {
		int j = path ? ctrl_path_compare(s->pathname, path) : 0;

		if (j != INT_MAX) {    /* add to treeview, start new panel */
		    char *c;

		    /*
		     * We expect never to find an implicit path
		     * component. For example, we expect never to
		     * see A/B/C followed by A/D/E, because that
		     * would _implicitly_ create A/D. All our path
		     * prefixes are expected to contain actual
		     * controls and be selectable in the treeview;
		     * so we would expect to see A/D _explicitly_
		     * before encountering A/D/E.
		     */
		    assert(j == ctrl_path_elements(s->pathname) - 1);

		    c = strrchr(s->pathname, '/');
		    if (!c)
			c = s->pathname;
		    else
			c++;

		    [treedata addPath:s->pathname];
		    path = s->pathname;

		    panelht = 0;
		}

		create_ctrls(dv, [self contentView], s, &mw, &mh);
		if (wmin < mw + 3*20+150)
		    wmin = mw + 3*20+150;
		panelht += mh + 20;
		if (hmin < panelht - 20)
		    hmin = panelht - 20;
	    }
	}
    }

    {
	int i;
	NSRect r;

	[treeview setDataSource:treedata];
	for (i = [treeview numberOfRows]; i-- ;)
	    [treeview expandItem:[treeview itemAtRow:i] expandChildren:YES];

	[treeview sizeToFit];
	r = [treeview frame];
	if (hmin < r.size.height)
	    hmin = r.size.height;
    }

    [self setContentSize:NSMakeSize(wmin, hmin+60+by)];
    [scrollview setFrame:NSMakeRect(20, 40+by, 150, hmin)];
    [treeview setDelegate:self];
    mby = by;

    /*
     * Now place the controls.
     */
    {
	int i;
	char *path = NULL;
	panelht = 0;

	for (i = 0; i < ctrlbox->nctrlsets; i++) {
	    struct controlset *s = ctrlbox->ctrlsets[i];

	    if (!*s->pathname) {
		by -= VSPACING + place_ctrls(dv, s, 20, by, wmin-40);
	    } else {
		if (!path || strcmp(s->pathname, path))
		    panelht = 0;

		panelht += VSPACING + place_ctrls(dv, s, 2*20+150,
						  40+mby+hmin-panelht,
						  wmin - (3*20+150));

		path = s->pathname;
	    }
	}
    }

    select_panel(dv, ctrlbox, [[treeview itemAtRow:0] cString]);

    [treeview reloadData];

    dlg_refresh(NULL, dv);

    [self center];		       /* :-) */

    return self;
}
- (void)configBoxFinished:(id)object
{
    int ret = [object intValue];       /* it'll be an NSNumber */
    if (ret) {
	[controller performSelectorOnMainThread:
	 @selector(newSessionWithConfig:)
	 withObject:[NSData dataWithBytes:&cfg length:sizeof(cfg)]
	 waitUntilDone:NO];
    }
    [self close];
}
- (void)outlineViewSelectionDidChange:(NSNotification *)notification
{
    const char *path = [[treeview itemAtRow:[treeview selectedRow]] cString];
    select_panel(dv, ctrlbox, path);
}
- (BOOL)outlineView:(NSOutlineView *)outlineView
    shouldEditTableColumn:(NSTableColumn *)tableColumn item:(id)item
{
    return NO;			       /* no editing! */
}
@end

/* ----------------------------------------------------------------------
 * Various special-purpose dialog boxes.
 */

int askappend(void *frontend, Filename filename)
{
    return 0;			       /* FIXME */
}

void askalg(void *frontend, const char *algtype, const char *algname)
{
    fatalbox("Cipher algorithm dialog box not supported yet");
    return;			       /* FIXME */
}

void verify_ssh_host_key(void *frontend, char *host, int port, char *keytype,
			 char *keystr, char *fingerprint)
{
    int ret;

    /*
     * Verify the key.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)
	return;

    /*
     * FIXME FIXME FIXME. I currently lack any sensible means of
     * asking the user for a verification non-application-modally,
     * _or_ any means of closing just this connection if the answer
     * is no (the Unix and Windows ports just exit() in this
     * situation since they're one-connection-per-process).
     * 
     * What I need to do is to make this function optionally-
     * asynchronous, much like the interface to agent_query(). It
     * can either run modally and return a result directly, _or_ it
     * can kick off a non-modal dialog, return a `please wait'
     * status, and the dialog can call the backend back when the
     * result comes in. Also, in either case, the aye/nay result
     * wants to be passed to the backend so that it can tear down
     * the connection if the answer was nay.
     * 
     * For the moment, I simply bomb out if we have an unrecognised
     * host key. This makes this port safe but not very useful: you
     * can only use it at all if you already have a host key cache
     * set up by running the Unix port.
     */
    fatalbox("Host key dialog box not supported yet");
}

void old_keyfile_warning(void)
{
    /*
     * This should never happen on OS X. We hope.
     */
}

void about_box(void *window)
{
    /* FIXME */
}
