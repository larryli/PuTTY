/*
 * Copyright (c) 2003 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * mtcpnet.c - MacTCP interface
 */

#include <MacTypes.h>
#include <Devices.h>
#include <Endian.h>
#include <Folders.h>
#include <MacTCP.h>
#include <MixedMode.h>
#include <Resources.h>

#include <assert.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "putty.h"
#include "network.h"
#include "mac.h"

/*
 * The following structures are documented as being in
 * <AddressXlation.h>, but that isn't shipped with Universal
 * Interfaces, and it's easier to define them here than to require
 * people to download yet another SDK.
 */

static OSErr OpenResolver(char *);
static OSErr CloseResolver(void);

enum {
    OPENRESOLVER = 1,
    CLOSERESOLVER,
    STRTOADDR,
    ADDRTOSTR,
    ENUMCACHE,
    ADDRTONAME,
    HXINFO,
    MXINFO
};

#define NUM_ALT_ADDRS 4

typedef struct hostInfo {
    int rtnCode;
    char cname[255];
    unsigned long addr[NUM_ALT_ADDRS];
};

typedef CALLBACK_API(void, ResultProcPtr)(struct hostInfo *, char *);
typedef STACK_UPP_TYPE(ResultProcPtr) ResultUPP;
enum { uppResultProcInfo = kPascalStackBased
       | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(struct hostInfo*)))
       | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(char *)))
};
#define NewResultUPP(userRoutine)					\
    (ResultUPP)NewRoutineDescriptor((ProcPtr)(userRoutine),		\
				    uppResultProcInfo,			\
				    GetCurrentArchitecture())
#define DisposeResultUPP(userUPP) DisposeRoutineDescriptor(userUPP)

static OSErr StrToAddr(char *, struct hostInfo *, ResultUPP *, char *);

typedef CALLBACK_API_C(OSErr, OpenResolverProcPtr)(UInt32, char *);
typedef STACK_UPP_TYPE(OpenResolverProcPtr) OpenResolverUPP;
enum { uppOpenResolverProcInfo = kCStackBased
       | RESULT_SIZE(SIZE_CODE(sizeof(OSErr)))
       | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(UInt32)))
       | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(char*)))
};
#define InvokeOpenResolverUPP(selector, fileName, userUPP)		\
    CALL_TWO_PARAMETER_UPP((userUPP), uppOpenResolverProcInfo,		\
			   (selector), (fileName))

typedef CALLBACK_API_C(OSErr, CloseResolverProcPtr)(UInt32);
typedef STACK_UPP_TYPE(CloseResolverProcPtr) CloseResolverUPP;
enum { uppCloseResolverProcInfo = kCStackBased
       | RESULT_SIZE(SIZE_CODE(sizeof(OSErr)))
       | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(UInt32)))
};
#define InvokeCloseResolverUPP(selector, userUPP)			\
    CALL_ONE_PARAMETER_UPP((userUPP), uppCloseResolverProcInfo, (selector))

typedef CALLBACK_API_C(OSErr, StrToAddrProcPtr)(UInt32, char *,
						struct hostInfo *, ResultUPP,
						char *);
typedef STACK_UPP_TYPE(StrToAddrProcPtr) StrToAddrUPP;
enum { uppStrToAddrProcInfo = kCStackBased
       | RESULT_SIZE(SIZE_CODE(sizeof(OSErr)))
       | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(UInt32)))
       | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(char *)))
       | STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(struct hostInfo *)))
       | STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(ResultUPP)))
       | STACK_ROUTINE_PARAMETER(5, SIZE_CODE(sizeof(char *)))
};
#define InvokeStrToAddrUPP(selector, hostName, hostInfoPtr, ResultProc,	\
			   userDataPtr, userUPP)			\
    CALL_FIVE_PARAMETER_UPP((userUPP), uppStrToAddrProcInfo, (selector),\
			    (hostName), (hostInfoPtr), (ResultProc),	\
			    (userDataPtr))
#define StrToAddr(hostName, hostInfoPtr, ResultProc, userDataPtr)	\
    InvokeStrToAddrUPP(STRTOADDR, hostName, hostInfoPtr, ResultProc,	\
		       userDataPtr, (StrToAddrUPP)*mactcp.dnr_handle)

typedef CALLBACK_API_C(OSErr, AddrToStrProcPtr)(UInt32, unsigned long, char *);
typedef STACK_UPP_TYPE(AddrToStrProcPtr) AddrToStrUPP;
enum { uppAddrToStrProcInfo = kCStackBased
       | RESULT_SIZE(SIZE_CODE(sizeof(OSErr)))
       | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(unsigned long)))
       | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(char *)))
};
#define InvokeAddrToStrUPP(selector, addr, addrStr, userUPP)		\
    CALL_THREE_PARAMETER_UPP((userUPP), uppAddrToStrProcInfo, (selector),\
			     (addr), (addrStr))
#define AddrToStr(addr, addrStr)					\
    InvokeAddrToStrUPP(ADDRTOSTR, addr, addrStr,			\
		       (AddrToStrUPP)*mactcp.dnr_handle)

/* End of AddressXlation.h bits */

struct Socket_tag {
    struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */
    StreamPtr s;
    OSErr err;
    Plug plug;
    void *private_ptr;
    bufchain output_data;
    int connected;
    int writable;
    int frozen; /* this causes readability notifications to be ignored */
    int frozen_readable; /* this means we missed at least one readability
			  * notification while we were frozen */
    int localhost_only;		       /* for listening sockets */
    char oobdata[1];
    int sending_oob;
    int oobpending;		       /* is there OOB data available to read? */
    int oobinline;
    int pending_error;		       /* in case send() returns error */
    int listener;
    struct Socket_tag *next;
    struct Socket_tag **prev;
};

/*
 * We used to typedef struct Socket_tag *Socket.
 *
 * Since we have made the networking abstraction slightly more
 * abstract, Socket no longer means a tcp socket (it could mean
 * an ssl socket).  So now we must use Actual_Socket when we know
 * we are talking about a tcp socket.
 */
typedef struct Socket_tag *Actual_Socket;

struct SockAddr_tag {
    int resolved;
    struct hostInfo hostinfo;
    char hostname[512];
};

/* Global variables */
static struct {
    Handle dnr_handle;
    int initialised;
    short refnum;
    Actual_Socket socklist;
} mactcp;

static pascal void mactcp_lookupdone(struct hostInfo *hi, char *cookie);
static Plug mactcp_plug(Socket, Plug);
static void mactcp_flush(Socket);
static void mactcp_close(Socket);
static int mactcp_write(Socket, char *, int);
static int mactcp_write_oob(Socket, char *, int);
static void mactcp_set_private_ptr(Socket, void *);
static void *mactcp_get_private_ptr(Socket);
static char *mactcp_socket_error(Socket);
static void mactcp_set_frozen(Socket, int);

static void mactcp_recv(Actual_Socket s, size_t len);

/*
 * Initialise MacTCP.
 * This should be called once before any TCP connection is opened.
 */

OSErr mactcp_init(void)
{
    OSErr err;

    /*
     * IM:Devices describes a convoluted way of finding a spare unit
     * number to open a driver on before calling OpenDriver.  Happily,
     * the MacTCP INIT ensures that .IPP is already open (and hence
     * has a valid unit number already) so we don't need to go through
     * all that.  (MacTCP Programmer's Guide p6)
     */
    err = OpenDriver("\p.IPP", &mactcp.refnum);
    if (err != noErr) return err;
    err = OpenResolver(NULL);
    if (err != noErr) return err;

    mactcp.initialised = TRUE;
    return noErr;
}

void mactcp_shutdown(void)
{

    CloseResolver();
    mactcp.initialised = FALSE;
}

static ResultUPP mactcp_lookupdone_upp;

SockAddr sk_namelookup(char *host, char **canonicalname)
{
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));
    OSErr err;
    volatile int done = FALSE;
    char *realhost;

    /* Clear the structure. */
    memset(ret, 0, sizeof(struct SockAddr_tag));
    if (mactcp_lookupdone_upp == NULL)
	mactcp_lookupdone_upp = NewResultUPP(&mactcp_lookupdone);
    err = StrToAddr(host, &ret->hostinfo, mactcp_lookupdone_upp,
		    (char *)&done);
    /*
     * PuTTY expects DNS lookups to be synchronous (see bug
     * "async-dns"), so we pretend they are.
     */
    if (err == cacheFault)
	while (!done)
	    continue;
    ret->resolved = TRUE;
    
    if (ret->hostinfo.rtnCode == noErr)
	realhost = ret->hostinfo.cname;
    else
	realhost = "";
    *canonicalname = smalloc(1+strlen(realhost));
    strcpy(*canonicalname, realhost);
    return ret;
}

static pascal void mactcp_lookupdone(struct hostInfo *hi, char *cookie)
{
    volatile int *donep = (int *)cookie;

    *donep = TRUE;
}

SockAddr sk_nonamelookup(char *host)
{
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));

    ret->resolved = FALSE;
    ret->hostinfo.rtnCode = noErr;
    ret->hostname[0] = '\0';
    strncat(ret->hostname, host, lenof(ret->hostname) - 1);
    return ret;
}

void sk_getaddr(SockAddr addr, char *buf, int buflen)
{
    char mybuf[16];
    OSErr err;

    if (addr->resolved) {
	/* XXX only return first address */
	err = AddrToStr(addr->hostinfo.addr[0], mybuf);
	buf[0] = '\0';
	if (err != noErr)
	    strncat(buf, mybuf, buflen - 1);
    } else {
	buf[0] = '\0';
	strncat(buf, addr->hostname, buflen - 1);
    }
}

/* I think "local" here really means "loopback" */

int sk_hostname_is_local(char *name)
{

    return !strcmp(name, "localhost");
}

int sk_address_is_local(SockAddr addr)
{
    int i;

    if (addr->resolved)
	for (i = 0; i < NUM_ALT_ADDRS; i++)
	    if (addr->hostinfo.addr[i] & 0xff000000 == 0x7f000000)
		return TRUE;
    return FALSE;
}

int sk_addrtype(SockAddr addr)
{

    if (addr->resolved)
	return ADDRTYPE_IPV4;
    return ADDRTYPE_NAME;
}

void sk_addrcopy(SockAddr addr, char *buf)
{

    /* XXX only return first address */
    memcpy(buf, &addr->hostinfo.addr[0], 4);
}

void sk_addr_free(SockAddr addr)
{

    sfree(addr);
}

static Plug mactcp_plug(Socket sock, Plug p)
{
    Actual_Socket s = (Actual_Socket) sock;
    Plug ret = s->plug;

    if (p)
	s->plug = p;
    return ret;
}

static void mactcp_flush(Socket s)
{

    fatalbox("sk_tcp_flush");
}

Socket sk_register(void *sock, Plug plug)
{

    fatalbox("sk_register");
}

Socket sk_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, Plug plug)
{
    static struct socket_function_table fn_table = {
	mactcp_plug,
	mactcp_close,
	mactcp_write,
	mactcp_write_oob,
	mactcp_flush,
	mactcp_set_private_ptr,
	mactcp_get_private_ptr,
	mactcp_set_frozen,
	mactcp_socket_error
    };
    TCPiopb pb;
    UDPiopb upb;
    Actual_Socket ret;
    ip_addr dstaddr;
    size_t buflen;

    /*
     * Create Socket structure.
     */
    ret = smalloc(sizeof(struct Socket_tag));
    ret->s = 0;
    ret->fn = &fn_table;
    ret->err = noErr;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->connected = 0;		       /* to start with */
    ret->writable = 0;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;	       /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->oobinline = oobinline;
    ret->oobpending = FALSE;
    ret->listener = 0;

    dstaddr = addr->hostinfo.addr[0]; /* XXX should try all of them */
    /*
     * Create a TCP stream.
     * 
     * MacTCP requires us to provide it with some buffer memory.  Page
     * 31 of the Programmer's Guide says it should be a minimum of
     * 4*MTU+1024.  Page 36 says a minimum of 4096 bytes.  Assume
     * they're both correct.
     */
    assert(addr->resolved);
    upb.ioCRefNum = mactcp.refnum;
    upb.csCode = UDPMaxMTUSize;
    upb.csParam.mtu.remoteHost = dstaddr;
    upb.csParam.mtu.userDataPtr = NULL;
    ret->err = PBControlSync((ParmBlkPtr)&upb);
    if (ret->err != noErr) return (Socket)ret;

    buflen = upb.csParam.mtu.mtuSize * 4 + 1024;
    if (buflen < 4096) buflen = 4096;
    pb.ioCRefNum = mactcp.refnum;
    pb.csCode = TCPCreate;
    pb.csParam.create.rcvBuff = smalloc(buflen);
    pb.csParam.create.rcvBuffLen = buflen;
    pb.csParam.create.notifyProc = NULL;
    pb.csParam.create.userDataPtr = (Ptr)ret;
    ret->err = PBControlSync((ParmBlkPtr)&pb);
    if (ret->err != noErr) return (Socket)ret;
    ret->s = pb.tcpStream;

    /*
     * Open the connection.
     */
    pb.ioCRefNum = mactcp.refnum;
    pb.csCode = TCPActiveOpen;
    pb.tcpStream = ret->s;
    pb.csParam.open.validityFlags = 0;
    pb.csParam.open.remoteHost = dstaddr;
    pb.csParam.open.remotePort = port;
    pb.csParam.open.localPort = privport ? 1023 : 0;
    pb.csParam.open.dontFrag = FALSE;
    pb.csParam.open.timeToLive = 0;
    pb.csParam.open.security = 0;
    pb.csParam.open.optionCnt = 0;
    pb.csParam.open.userDataPtr = (Ptr)ret;
    while (1) {
	ret->err = PBControlSync((ParmBlkPtr)&pb);
	if (!privport || ret->err != duplicateSocket)
	    break;
	pb.csParam.open.localPort--;
	if (pb.csParam.open.localPort == 0)
	    break;
    }

    if (ret->err != noErr) return (Socket)ret;

    ret->connected = TRUE;
    ret->writable = TRUE;

    /* Add this to the list of all sockets */
    ret->next = mactcp.socklist;
    ret->prev = &mactcp.socklist;
    ret->next->prev = &ret->next;
    mactcp.socklist = ret;

    return (Socket)ret;
}

Socket sk_newlistener(char *srcaddr, int port, Plug plug, int local_host_only)
{

    fatalbox("sk_newlistener");
}

static void mactcp_close(Socket sock)
{
    Actual_Socket s = (Actual_Socket)sock;
    TCPiopb pb;

    /*
     * TCPClose is equivalent to shutdown(fd, SHUT_WR), and hence
     * leaves the Rx side open, while TCPAbort seems rather vicious,
     * throwing away Tx data that haven't been ACKed yet.  We do both
     * in succession.
     */
    pb.ioCRefNum = mactcp.refnum;
    pb.csCode = TCPClose;
    pb.tcpStream = s->s;
    pb.csParam.close.validityFlags = 0;
    pb.csParam.close.userDataPtr = (Ptr)s;
    s->err = PBControlSync((ParmBlkPtr)&pb);
    /* Not much we can do about an error anyway. */

    pb.ioCRefNum = mactcp.refnum;
    pb.csCode = TCPAbort;
    pb.tcpStream = s->s;
    pb.csParam.abort.userDataPtr = (Ptr)s;
    s->err = PBControlSync((ParmBlkPtr)&pb);
    /* Even less we can do about an error here. */

    pb.ioCRefNum = mactcp.refnum;
    pb.csCode = TCPRelease;
    pb.tcpStream = s->s;
    pb.csParam.create.userDataPtr = (Ptr)s;
    s->err = PBControlSync((ParmBlkPtr)&pb);
    if (s->err == noErr)
	sfree(pb.csParam.create.rcvBuff);

    /* Unhitch from list of sockets */
    *s->prev = s->next;
    if (s->next != NULL)
	s->next->prev = s->prev;

    sfree(s);
}

static int mactcp_write(Socket sock, char *buf, int len)
{
    Actual_Socket s = (Actual_Socket) sock;
    wdsEntry wds[2];
    TCPiopb pb;

    wds[0].length = len;
    wds[0].ptr = buf;
    wds[1].length = 0;

    pb.ioCRefNum = mactcp.refnum;
    pb.csCode = TCPSend;
    pb.tcpStream = s->s;
    pb.csParam.send.validityFlags = 0;
    pb.csParam.send.pushFlag = TRUE; /* XXX we want it to return. */
    pb.csParam.send.urgentFlag = 0;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    pb.csParam.send.userDataPtr = (Ptr)s;
    s->err = PBControlSync((ParmBlkPtr)&pb);
    return 0;
}

static int mactcp_write_oob(Socket sock, char *buf, int len)
{

    fatalbox("mactcp_write_oob");
}

/*
 * Called from our event loop if there's work to do.
 */
void mactcp_poll(void)
{
    Actual_Socket s;
    TCPiopb pb;

    for (s = mactcp.socklist; s != NULL; s = s->next) {
	/* XXX above can't handle sockets being deleted. */
	pb.ioCRefNum = mactcp.refnum;
	pb.csCode = TCPStatus;
	pb.tcpStream = s->s;
	pb.csParam.status.userDataPtr = (Ptr)s;
	s->err = PBControlSync((ParmBlkPtr)&pb);
	if (s->err != noErr)
	    continue;
	if (pb.csParam.status.amtUnreadData > 0)
	    mactcp_recv(s, pb.csParam.status.amtUnreadData);
	/* Should check connectionState in case remote has closed */
    }
}

static void mactcp_recv(Actual_Socket s, size_t len)
{
    rdsEntry rds[2];
    TCPiopb pb;

    if (s->frozen) return;

    while (len > 0) {
	pb.ioCRefNum = mactcp.refnum;
	pb.csCode = TCPNoCopyRcv;
	pb.tcpStream = s->s;
	pb.csParam.receive.commandTimeoutValue = 0;
	pb.csParam.receive.rdsPtr = (Ptr)rds;
	pb.csParam.receive.rdsLength = lenof(rds) - 1;
	pb.csParam.receive.userDataPtr = (Ptr)s;
	s->err = PBControlSync((ParmBlkPtr)&pb);
	if (s->err != noErr)
	    return;
	plug_receive(s->plug, 0, rds[0].ptr, rds[0].length);
	len -= rds[0].length;
	pb.csCode = TCPRcvBfrReturn;
	s->err = PBControlSync((ParmBlkPtr)&pb);
	if (s->err != noErr)
	    return;
    }	
}

/*
 * Each socket abstraction contains a `void *' private field in
 * which the client can keep state.
 */
static void mactcp_set_private_ptr(Socket sock, void *ptr)
{
    Actual_Socket s = (Actual_Socket) sock;
    s->private_ptr = ptr;
}

static void *mactcp_get_private_ptr(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->private_ptr;
}

/*
 * Special error values are returned from sk_namelookup and sk_new
 * if there's a problem. These functions extract an error message,
 * or return NULL if there's no problem.
 */
char *sk_addr_error(SockAddr addr)
{
    static char buf[64];

    switch (addr->hostinfo.rtnCode) {
      case noErr:
	return NULL;
      case nameSyntaxErr:
	return "Name syntax error";
      case noNameServer:
	return "No name server found";
      case authNameErr:
	return "Domain name does not exist";
      case noAnsErr:
	return "No answer from domain name server";
      case dnrErr:
	return "Domain name server returned an error";
      case outOfMemory:
	return "Out of memory";
      default:
	sprintf(buf, "Unknown DNR error %d", addr->hostinfo.rtnCode);
	return buf;
    }
}

static char *mactcp_socket_error(Socket sock)
{
    static char buf[64];
    Actual_Socket s = (Actual_Socket) sock;

    switch (s->err) {
      case noErr:
	return NULL;
      case insufficientResources:
	return "Insufficient resources to open TCP stream";
      case duplicateSocket:
	return "Duplicate socket";
      case openFailed:
	return "Connection failed while opening";
      default:
	sprintf(buf, "Unknown MacTCP error %d", s->err);
	return buf;
    }
}

static void mactcp_set_frozen(Socket sock, int is_frozen)
{
    Actual_Socket s = (Actual_Socket) sock;

    if (s->frozen == is_frozen)
	return;
    s->frozen = is_frozen;
}

/*
 * Bits below here would usually be in dnr.c, shipped with the MacTCP
 * SDK, but its convenient not to require that, and since we assume
 * System 7 we can actually simplify things a lot.
 */

static OSErr OpenResolver(char *hosts_file)
{
    short vrefnum;
    long dirid;
    HParamBlockRec pb;
    Str255 filename;
    OSErr err;
    int fd;
    Handle dnr_handle;

    if (mactcp.dnr_handle != NULL)
	return noErr;

    err = FindFolder(kOnSystemDisk, kControlPanelFolderType, FALSE, &vrefnum,
		     &dirid);
    if (err != noErr) return err;

    /*
     * Might be better to use PBCatSearch here, but it's not always
     * available.
     */
    pb.fileParam.ioCompletion = NULL;
    pb.fileParam.ioNamePtr = filename;
    pb.fileParam.ioVRefNum = vrefnum;
    pb.fileParam.ioFDirIndex = 1;
    pb.fileParam.ioDirID = dirid;
    fd = -1;

    while (PBHGetFInfoSync(&pb) == noErr) {
	if (pb.fileParam.ioFlFndrInfo.fdType == 'cdev' &&
	    pb.fileParam.ioFlFndrInfo.fdCreator == 'ztcp') {
	    fd = HOpenResFile(vrefnum, dirid, filename, fsRdPerm);
	    if (fd == -1) continue;
	    dnr_handle = Get1IndResource('dnrp', 1);
	    if (dnr_handle != NULL)
		break;
	    CloseResFile(fd);
	    fd = -1;
	}
	pb.fileParam.ioDirID = dirid;
	pb.fileParam.ioFDirIndex++;
    }
    if (fd == -1)
	return fnfErr;
    
    DetachResource(dnr_handle);
    CloseResFile(fd);

    MoveHHi(dnr_handle);
    HLock(dnr_handle);

    err = InvokeOpenResolverUPP(OPENRESOLVER, hosts_file,
				(OpenResolverUPP)*dnr_handle);
    if (err != noErr) {
	HUnlock(dnr_handle);
	DisposeHandle(dnr_handle);
	return err;
    }

    mactcp.dnr_handle = dnr_handle;
    return noErr;
}

OSErr CloseResolver(void)
{
    Handle dnr_handle = mactcp.dnr_handle;
    OSErr err;

    if (mactcp.dnr_handle == NULL)
	return notOpenErr;

    err = InvokeCloseResolverUPP(CLOSERESOLVER,
				 (CloseResolverUPP)*mactcp.dnr_handle);
    if (err != noErr)
	return err;

    mactcp.dnr_handle = NULL;
    HUnlock(dnr_handle);
    DisposeHandle(dnr_handle);
    return noErr;
}

/* MacTCP doesn't have a services database. */
int net_service_lookup(char *service)
{

    return 0;
}


/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
