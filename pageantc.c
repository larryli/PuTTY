/*
 * Pageant client code.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef TESTMODE
#define debug(x) (printf x)
#else
#define debug(x)
#endif

int agent_exists(void) {
    HWND hwnd;
    hwnd = FindWindow("Pageant", "Pageant");
    if (!hwnd)
        return FALSE;
    else
        return TRUE;
}

void agent_query(void *in, int inlen, void **out, int *outlen) {
#if 0
#define MAILSLOTNAME "\\\\.\\mailslot\\pageant_listener"
    SECURITY_ATTRIBUTES sa;
    HANDLE my_mailslot, agent_mailslot;
    char name[64];
    char *p;
    DWORD msglen, byteswritten, bytesread, inid;

    *out = NULL;
    *outlen = 0;

    agent_mailslot = CreateFile(MAILSLOTNAME, GENERIC_WRITE,
                                FILE_SHARE_READ, (LPSECURITY_ATTRIBUTES)NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                (HANDLE)NULL);
    debug(("opened %s: %p\n", MAILSLOTNAME, agent_mailslot));
    if (agent_mailslot == INVALID_HANDLE_VALUE)
        return;

    inid = GetCurrentThreadId();
    inid--;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    do {
        sprintf(name, "\\\\.\\mailslot\\pclient_request_%08x", ++inid);
        /*
         * Five-minute timeout.
         */
        my_mailslot = CreateMailslot(name, 0, 0, &sa);
        debug(("mailslot %s: %p\n", name, my_mailslot));
    } while (my_mailslot == INVALID_HANDLE_VALUE);
    Sleep(3000);

    msglen = strlen(name) + 1 + inlen;
    p = malloc(msglen);
    if (!p) {
        CloseHandle(my_mailslot);
        CloseHandle(agent_mailslot);
        return;
    }

    strcpy(p, name);
    memcpy(p+strlen(p)+1, in, inlen);

    debug(("ooh\n"));
    if (WriteFile(agent_mailslot, p, msglen, &byteswritten, NULL) == 0) {
        debug(("eek!\n"));
        free(p);
        CloseHandle(my_mailslot);
        CloseHandle(agent_mailslot);
        return;
    }
    debug(("aah\n"));
    free(p);
    CloseHandle(agent_mailslot);

    WaitForSingleObject(my_mailslot, 3000000);
    debug(("waited\n"));
    if (!GetMailslotInfo(my_mailslot, NULL, &msglen, NULL, NULL)) {
        CloseHandle(my_mailslot);
        return;
    }
    if (msglen == MAILSLOT_NO_MESSAGE) {
        debug(("no message\n"));
        CloseHandle(my_mailslot);
        return;
    }
    debug(("msglen=%d\n", msglen));
    p = malloc(msglen);
    if (!p) {
        CloseHandle(my_mailslot);
        return;
    }
    if (ReadFile(my_mailslot, p, msglen, &bytesread, NULL) == 0 &&
        bytesread == msglen) {
        *out = p;
        *outlen = msglen;
    }
    CloseHandle(my_mailslot);
#endif
    HWND hwnd;
    char mapname[64];
    HANDLE filemap;
    void *p, *ret;
    int id, retlen;
    COPYDATASTRUCT cds;

    *out = NULL;
    *outlen = 0;

    hwnd = FindWindow("Pageant", "Pageant");
    debug(("hwnd is %p\n", hwnd));
    if (!hwnd)
        return;
    cds.dwData = 0;                    /* FIXME */
    cds.cbData = inlen;
    cds.lpData = in;
    id = SendMessage(hwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
    debug(("return is %d\n", id));
    if (id > 0) {
        sprintf(mapname, "PageantReply%08x", id);
        filemap = OpenFileMapping(FILE_MAP_READ, FALSE, mapname);
        debug(("name is `%s', filemap is %p\n", mapname, filemap));
        debug(("error is %d\n", GetLastError()));
        if (filemap != NULL && filemap != INVALID_HANDLE_VALUE) {
            p = MapViewOfFile(filemap, FILE_MAP_READ, 0, 0, 0);
            debug(("p is %p\n", p));
            if (p) {
                retlen = *(int *)p;
                debug(("len is %d\n", retlen));
                ret = malloc(retlen);
                if (ret) {
                    memcpy(ret, ((int *)p) + 1, retlen);
                    *out = ret;
                    *outlen = retlen;
                }
                UnmapViewOfFile(p);
            }
            CloseHandle(filemap);
        }
        /* FIXME: tell agent to close its handle too */
    }
}

#ifdef TESTMODE

int main(void) {
    void *msg;
    int len;
    int i;

    agent_query("\0\0\0\1\1", 5, &msg, &len);
    debug(("%d:", len));
    for (i = 0; i < len; i++)
        debug((" %02x", ((unsigned char *)msg)[i]));
    debug(("\n"));
    return 0;
}

#endif
