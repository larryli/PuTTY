/* $Id: macstore.c,v 1.1 2002/11/19 02:13:46 ben Exp $ */

/*
 * macstore.c: Macintosh-specific impementation of the interface
 * defined in storage.h
 */

#include <MacTypes.h>
#include <Folders.h>
#include <Memory.h>
#include <Resources.h>
#include <TextUtils.h>

#include <string.h>

#include "putty.h"
#include "storage.h"

#define PUTTY_CREATOR	FOUR_CHAR_CODE('pTTY')
#define SESS_TYPE	FOUR_CHAR_CODE('Sess')


OSErr get_session_dir(Boolean makeit, short *pVRefNum, long *pDirID);
OSErr FSpGetDirID(FSSpec *f, long *idp, Boolean makeit);

/*
 * We store each session as a file in the "PuTTY" sub-directory of the
 * preferences folder.  Each (key,value) pair is stored as a resource.
 */

OSErr get_session_dir(Boolean makeit, short *pVRefNum, long *pDirID) {
    OSErr error = noErr;
    short prefVRefNum;
    FSSpec puttydir, sessdir;
    long prefDirID, puttyDirID, sessDirID;

    error = FindFolder(kOnSystemDisk, kPreferencesFolderType, makeit,
		       &prefVRefNum, &prefDirID);
    if (error != noErr) goto out;

    error = FSMakeFSSpec(prefVRefNum, prefDirID, "\pPuTTY", &puttydir);
    if (error != noErr && error != fnfErr) goto out;
    error = FSpGetDirID(&puttydir, &puttyDirID, makeit);
    if (error != noErr) goto out;

    error = FSMakeFSSpec(prefVRefNum, puttyDirID, "\pSaved Sessions",
			 &sessdir);
    if (error != noErr && error != fnfErr) goto out;
    error = FSpGetDirID(&sessdir, &sessDirID, makeit);
    if (error != noErr) goto out;

    *pVRefNum = prefVRefNum;
    *pDirID = sessDirID;

  out:
    return error;
}

OSErr FSpGetDirID(FSSpec *f, long *idp, Boolean makeit) {
    CInfoPBRec pb;
    OSErr error = noErr;

    pb.dirInfo.ioNamePtr = f->name;
    pb.dirInfo.ioVRefNum = f->vRefNum;
    pb.dirInfo.ioDrDirID = f->parID;
    pb.dirInfo.ioFDirIndex = 0;
    error = PBGetCatInfoSync(&pb);
    if (error == fnfErr && makeit)
	return FSpDirCreate(f, smSystemScript, idp);
    if (error != noErr) goto out;
    if ((pb.dirInfo.ioFlAttrib & ioDirMask) == 0) {
	error = dirNFErr;
	goto out;
    }
    *idp = pb.dirInfo.ioDrDirID;

  out:
    return error;
}

struct write_settings {
    int fd;
    FSSpec tmpfile;
    FSSpec dstfile;
};

void *open_settings_w(char *sessionname) {
    short sessVRefNum, tmpVRefNum;
    long sessDirID, tmpDirID;
    FSSpec sessfile;
    OSErr error;
    Str255 psessionname;
    struct write_settings *ws;
    
    ws = safemalloc(sizeof *ws);
    error = get_session_dir(kCreateFolder, &sessVRefNum, &sessDirID);
    if (error != noErr) goto out;

    c2pstrcpy(psessionname, sessionname);
    error = FSMakeFSSpec(sessVRefNum, sessDirID, psessionname, &ws->dstfile);
    if (error != noErr && error != fnfErr) goto out;
    if (error == fnfErr) {
	FSpCreateResFile(&ws->dstfile, PUTTY_CREATOR, SESS_TYPE,
			 smSystemScript);
	if ((error = ResError()) != noErr) goto out;
    }

    /* Create a temporary file to save to first. */
    error = FindFolder(sessVRefNum, kTemporaryFolderType, kCreateFolder,
		       &tmpVRefNum, &tmpDirID);
    if (error != noErr) goto out;
    error = FSMakeFSSpec(tmpVRefNum, tmpDirID, psessionname, &ws->tmpfile);
    if (error != noErr && error != fnfErr) goto out;
    if (error == noErr) {
	error = FSpDelete(&ws->tmpfile);
	if (error != noErr) goto out;
    }
    FSpCreateResFile(&ws->tmpfile, PUTTY_CREATOR, SESS_TYPE, smSystemScript);
    if ((error = ResError()) != noErr) goto out;

    ws->fd = FSpOpenResFile(&ws->tmpfile, fsWrPerm);
    if (ws->fd == -1) {error = ResError(); goto out;}

    return ws;

  out:
    safefree(ws);
    fatalbox("Failed to open session for write (%d)", error);
}

void write_setting_s(void *handle, char *key, char *value) {
    int fd = *(int *)handle;
    Handle h;
    int id;
    OSErr error;

    UseResFile(fd);
    if (ResError() != noErr)
        fatalbox("Failed to open saved session (%d)", ResError());

    error = PtrToHand(value, &h, strlen(value));
    if (error != noErr)
	fatalbox("Failed to allocate memory");
    /* Put the data in a resource. */
    id = Unique1ID(FOUR_CHAR_CODE('TEXT'));
    if (ResError() != noErr)
	fatalbox("Failed to get ID for resource %s (%d)", key, ResError());
    addresource(h, FOUR_CHAR_CODE('TEXT'), id, key);
    if (ResError() != noErr)
	fatalbox("Failed to add resource %s (%d)", key, ResError());
}

void write_setting_i(void *handle, char *key, int value) {
    int fd = *(int *)handle;
    Handle h;
    int id;
    OSErr error;

    UseResFile(fd);
    if (ResError() != noErr)
        fatalbox("Failed to open saved session (%d)", ResError());

    /* XXX assume all systems have the same "int" format */
    error = PtrToHand(&value, &h, sizeof(int));
    if (error != noErr)
	fatalbox("Failed to allocate memory (%d)", error);

    /* Put the data in a resource. */
    id = Unique1ID(FOUR_CHAR_CODE('Int '));
    if (ResError() != noErr)
	fatalbox("Failed to get ID for resource %s (%d)", key, ResError());
    addresource(h, FOUR_CHAR_CODE('Int '), id, key);
    if (ResError() != noErr)
	fatalbox("Failed to add resource %s (%d)", key, ResError());
}

void close_settings_w(void *handle) {
    struct write_settings *ws = handle;
    OSErr error;

    CloseResFile(ws->fd);
    if ((error = ResError()) != noErr)
	goto out;
    error = FSpExchangeFiles(&ws->tmpfile, &ws->dstfile);
    if (error != noErr) goto out;
    error = FSpDelete(&ws->tmpfile);
    if (error != noErr) goto out;
    return;

  out:
    fatalbox("Close of saved session failed (%d)", error);
    safefree(handle);
}

void *open_settings_r(char *sessionname) {
    short sessVRefNum;
    long sessDirID;
    FSSpec sessfile;
    OSErr error;
    Str255 psessionname;
    int fd;
    int *handle;

    error = get_session_dir(kDontCreateFolder, &sessVRefNum, &sessDirID);

    c2pstrcpy(psessionname, sessionname);
    error = FSMakeFSSpec(sessVRefNum, sessDirID, psessionname, &sessfile);
    if (error != noErr) goto out;
    fd = FSpOpenResFile(&sessfile, fsRdPerm);
    if (fd == 0) {error = ResError(); goto out;}

    handle = safemalloc(sizeof *handle);
    *handle = fd;
    return handle;

  out:
    return NULL;
}


char *read_setting_s(void *handle, char *key, char *buffer, int buflen) {
    int fd;
    Handle h;
    OSErr error;

    if (handle == NULL) goto out;
    fd = *(int *)handle;
    UseResFile(fd);
    if (ResError() != noErr) goto out;
    h = get1namedresource(FOUR_CHAR_CODE('TEXT'), key);
    if (h == NULL) goto out;

    if (GetHandleSize(h) > buflen) goto out;
    p2cstrcpy(buffer, (StringPtr)*h);
    ReleaseResource(h);
    if (ResError() != noErr) goto out;
    return buffer;

  out:
    return NULL;
}

int read_setting_i(void *handle, char *key, int defvalue) {
    int fd;
    Handle h;
    OSErr error;
    int value;

    if (handle == NULL) goto out;
    fd = *(int *)handle;
    UseResFile(fd);
    if (ResError() != noErr) goto out;
    h = get1namedresource(FOUR_CHAR_CODE('Int '), key);
    if (h == NULL) goto out;
    value = *(int *)*h;
    ReleaseResource(h);
    if (ResError() != noErr) goto out;
    return value;

  out:
    return defvalue;
}

void close_settings_r(void *handle) {
    int fd;

    if (handle == NULL) return;
    fd = *(int *)handle;
    CloseResFile(fd);
    if (ResError() != noErr)
	fatalbox("Close of saved session failed (%d)", ResError());
    safefree(handle);
}

void del_settings(char *sessionname) {
    OSErr error;
    FSSpec sessfile;
    short sessVRefNum;
    long sessDirID;
    Str255 psessionname;

    error = get_session_dir(kDontCreateFolder, &sessVRefNum, &sessDirID);

    c2pstrcpy(psessionname, sessionname);
    error = FSMakeFSSpec(sessVRefNum, sessDirID, psessionname, &sessfile);
    if (error != noErr) goto out;

    error = FSpDelete(&sessfile);
    return;
  out:
    fatalbox("Delete session failed (%d)", error);
}

struct enum_settings_state {
    short vRefNum;
    long dirID;
    int index;
};

void *enum_settings_start(void) {
    OSErr error;
    struct enum_settings_state *state;

    state = safemalloc(sizeof(*state));
    error = get_session_dir(kDontCreateFolder, &state->vRefNum, &state->dirID);
    if (error != noErr) {
	safefree(state);
	return NULL;
    }
    state->index = 1;
    return state;
}

char *enum_settings_next(void *handle, char *buffer, int buflen) {
    struct enum_settings_state *e = handle;
    CInfoPBRec pb;
    OSErr error = noErr;
    Str255 name;

    if (e == NULL) return NULL;
    do {
	pb.hFileInfo.ioNamePtr = name;
	pb.hFileInfo.ioVRefNum = e->vRefNum;
	pb.hFileInfo.ioDirID = e->dirID;
	pb.hFileInfo.ioFDirIndex = e->index++;
	error = PBGetCatInfoSync(&pb);
	if (error != noErr) return NULL;
    } while (!((pb.hFileInfo.ioFlAttrib & ioDirMask) == 0 &&
	       pb.hFileInfo.ioFlFndrInfo.fdCreator == PUTTY_CREATOR &&
	       pb.hFileInfo.ioFlFndrInfo.fdType == SESS_TYPE &&
	       name[0] < buflen));

    p2cstrcpy(buffer, name);
    return buffer;
}

void enum_settings_finish(void *handle) {

    safefree(handle);
}


/*
 * Emacs magic:
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
