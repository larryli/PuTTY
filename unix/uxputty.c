/*
 * Unix PuTTY main program.
 */

#include <stdio.h>
#include <assert.h>
#include <termios.h>
#include <unistd.h>

#include "putty.h"
#include "storage.h"

/*
 * FIXME: At least some of these functions should be replaced with
 * GTK GUI error-box-type things.
 * 
 * In particular, all the termios-type things must go, and
 * termios.h should disappear from the above #include list.
 */
void fatalbox(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    cleanup_exit(1);
}
void connection_fatal(void *frontend, char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    cleanup_exit(1);
}
void cmdline_error(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "plink: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();
    random_save_seed();
    exit(code);
}

void verify_ssh_host_key(void *frontend, char *host, int port, char *keytype,
			 char *keystr, char *fingerprint)
{
    int ret;

    static const char absentmsg[] =
	"The server's host key is not cached. You have no guarantee\n"
	"that the server is the computer you think it is.\n"
	"The server's key fingerprint is:\n"
	"%s\n"
	"If you trust this host, enter \"y\" to add the key to\n"
	"PuTTY's cache and carry on connecting.\n"
	"If you want to carry on connecting just once, without\n"
	"adding the key to the cache, enter \"n\".\n"
	"If you do not trust this host, press Return to abandon the\n"
	"connection.\n"
	"Store key in cache? (y/n) ";

    static const char wrongmsg[] =
	"WARNING - POTENTIAL SECURITY BREACH!\n"
	"The server's host key does not match the one PuTTY has\n"
	"cached. This means that either the server administrator\n"
	"has changed the host key, or you have actually connected\n"
	"to another computer pretending to be the server.\n"
	"The new key fingerprint is:\n"
	"%s\n"
	"If you were expecting this change and trust the new key,\n"
	"enter \"y\" to update PuTTY's cache and continue connecting.\n"
	"If you want to carry on connecting but without updating\n"
	"the cache, enter \"n\".\n"
	"If you want to abandon the connection completely, press\n"
	"Return to cancel. Pressing Return is the ONLY guaranteed\n"
	"safe choice.\n"
	"Update cached key? (y/n, Return cancels connection) ";

    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    /*
     * Verify the key.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)		       /* success - key matched OK */
	return;

    if (ret == 2) {		       /* key was different */
	fprintf(stderr, wrongmsg, fingerprint);
	fflush(stderr);
    }
    if (ret == 1) {		       /* key was absent */
	fprintf(stderr, absentmsg, fingerprint);
	fflush(stderr);
    }

    {
	struct termios oldmode, newmode;
	tcgetattr(0, &oldmode);
	newmode = oldmode;
	newmode.c_lflag |= ECHO | ISIG | ICANON;
	tcsetattr(0, TCSANOW, &newmode);
	line[0] = '\0';
	read(0, line, sizeof(line) - 1);
	tcsetattr(0, TCSANOW, &oldmode);
    }

    if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n') {
	if (line[0] == 'y' || line[0] == 'Y')
	    store_host_key(host, port, keytype, keystr);
    } else {
	fprintf(stderr, abandoned);
	cleanup_exit(0);
    }
}

/*
 * Ask whether the selected cipher is acceptable (since it was
 * below the configured 'warn' threshold).
 * cs: 0 = both ways, 1 = client->server, 2 = server->client
 */
void askcipher(void *frontend, char *ciphername, int cs)
{
    static const char msg[] =
	"The first %scipher supported by the server is\n"
	"%s, which is below the configured warning threshold.\n"
	"Continue with connection? (y/n) ";
    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    fprintf(stderr, msg,
	    (cs == 0) ? "" :
	    (cs == 1) ? "client-to-server " : "server-to-client ",
	    ciphername);
    fflush(stderr);

    {
	struct termios oldmode, newmode;
	tcgetattr(0, &oldmode);
	newmode = oldmode;
	newmode.c_lflag |= ECHO | ISIG | ICANON;
	tcsetattr(0, TCSANOW, &newmode);
	line[0] = '\0';
	read(0, line, sizeof(line) - 1);
	tcsetattr(0, TCSANOW, &oldmode);
    }

    if (line[0] == 'y' || line[0] == 'Y') {
	return;
    } else {
	fprintf(stderr, abandoned);
	cleanup_exit(0);
    }
}

void old_keyfile_warning(void)
{
    static const char message[] =
	"You are loading an SSH 2 private key which has an\n"
	"old version of the file format. This means your key\n"
	"file is not fully tamperproof. Future versions of\n"
	"PuTTY may stop supporting this private key format,\n"
	"so we recommend you convert your key to the new\n"
	"format.\n"
	"\n"
	"Once the key is loaded into PuTTYgen, you can perform\n"
	"this conversion simply by saving it again.\n";

    fputs(message, stderr);
}

/*
 * Another bunch of temporary stub functions. These ones will want
 * removing by means of implementing them properly: libcharset
 * should invent its own sensible format for codepage names and a
 * means of enumerating them, and printer_enum needs to be dealt
 * with somehow or other too.
 */

char *cp_name(int codepage)
{
    return "";
}
char *cp_enumerate(int index)
{
    return NULL;
}
int decode_codepage(char *cp_name)
{
    return -2;
}

printer_enum *printer_start_enum(int *nprinters_ptr) {
    *nprinters_ptr = 0;
    return NULL;
}
char *printer_get_name(printer_enum *pe, int i) { return NULL;
}
void printer_finish_enum(printer_enum *pe) { }

Backend *select_backend(Config *cfg)
{
    int i;
    Backend *back = NULL;
    for (i = 0; backends[i].backend != NULL; i++)
	if (backends[i].protocol == cfg->protocol) {
	    back = backends[i].backend;
	    break;
	}
    assert(back != NULL);
    return back;
}

int cfgbox(Config *cfg)
{
    extern int do_config_box(const char *title, Config *cfg);
    return do_config_box("PuTTY Configuration", cfg);
}

int main(int argc, char **argv)
{
    extern int pt_main(int argc, char **argv);
    sk_init();
    flags = FLAG_VERBOSE | FLAG_INTERACTIVE;
    default_protocol = be_default_protocol;
    /* Find the appropriate default port. */
    {
	int i;
	default_port = 0; /* illegal */
	for (i = 0; backends[i].backend != NULL; i++)
	    if (backends[i].protocol == default_protocol) {
		default_port = backends[i].backend->default_port;
		break;
	    }
    }
    return pt_main(argc, argv);
}
