/*
 * Serial back end (Unix-specific).
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include "putty.h"
#include "tree234.h"

#define SERIAL_MAX_BACKLOG 4096

typedef struct Serial Serial;
struct Serial {
    Seat *seat;
    LogContext *logctx;
    int fd;
    bool finished;
    size_t inbufsize;
    bufchain output_data;
    Backend backend;
};

/*
 * We store our serial backends in a tree sorted by fd, so that
 * when we get an uxsel notification we know which backend instance
 * is the owner of the serial port that caused it.
 */
static int serial_compare_by_fd(void *av, void *bv)
{
    Serial *a = (Serial *)av;
    Serial *b = (Serial *)bv;

    if (a->fd < b->fd)
        return -1;
    else if (a->fd > b->fd)
        return +1;
    return 0;
}

static int serial_find_by_fd(void *av, void *bv)
{
    int a = *(int *)av;
    Serial *b = (Serial *)bv;

    if (a < b->fd)
        return -1;
    else if (a > b->fd)
        return +1;
    return 0;
}

static tree234 *serial_by_fd = NULL;

static void serial_select_result(int fd, int event);
static void serial_uxsel_setup(Serial *serial);
static void serial_try_write(Serial *serial);

static const char *serial_configure(Serial *serial, Conf *conf)
{
    struct termios options;
    int bflag, bval, speed, flow, parity;
    const char *str;

    if (serial->fd < 0)
        return "Unable to reconfigure already-closed serial connection";

    tcgetattr(serial->fd, &options);

    /*
     * Find the appropriate baud rate flag.
     */
    speed = conf_get_int(conf, CONF_serspeed);
#define SETBAUD(x) (bflag = B ## x, bval = x)
#define CHECKBAUD(x) do { if (speed >= x) SETBAUD(x); } while (0)
    SETBAUD(50);
#ifdef B75
    CHECKBAUD(75);
#endif
#ifdef B110
    CHECKBAUD(110);
#endif
#ifdef B134
    CHECKBAUD(134);
#endif
#ifdef B150
    CHECKBAUD(150);
#endif
#ifdef B200
    CHECKBAUD(200);
#endif
#ifdef B300
    CHECKBAUD(300);
#endif
#ifdef B600
    CHECKBAUD(600);
#endif
#ifdef B1200
    CHECKBAUD(1200);
#endif
#ifdef B1800
    CHECKBAUD(1800);
#endif
#ifdef B2400
    CHECKBAUD(2400);
#endif
#ifdef B4800
    CHECKBAUD(4800);
#endif
#ifdef B9600
    CHECKBAUD(9600);
#endif
#ifdef B19200
    CHECKBAUD(19200);
#endif
#ifdef B38400
    CHECKBAUD(38400);
#endif
#ifdef B57600
    CHECKBAUD(57600);
#endif
#ifdef B76800
    CHECKBAUD(76800);
#endif
#ifdef B115200
    CHECKBAUD(115200);
#endif
#ifdef B153600
    CHECKBAUD(153600);
#endif
#ifdef B230400
    CHECKBAUD(230400);
#endif
#ifdef B307200
    CHECKBAUD(307200);
#endif
#ifdef B460800
    CHECKBAUD(460800);
#endif
#ifdef B500000
    CHECKBAUD(500000);
#endif
#ifdef B576000
    CHECKBAUD(576000);
#endif
#ifdef B921600
    CHECKBAUD(921600);
#endif
#ifdef B1000000
    CHECKBAUD(1000000);
#endif
#ifdef B1152000
    CHECKBAUD(1152000);
#endif
#ifdef B1500000
    CHECKBAUD(1500000);
#endif
#ifdef B2000000
    CHECKBAUD(2000000);
#endif
#ifdef B2500000
    CHECKBAUD(2500000);
#endif
#ifdef B3000000
    CHECKBAUD(3000000);
#endif
#ifdef B3500000
    CHECKBAUD(3500000);
#endif
#ifdef B4000000
    CHECKBAUD(4000000);
#endif
#undef CHECKBAUD
#undef SETBAUD
    cfsetispeed(&options, bflag);
    cfsetospeed(&options, bflag);
    logeventf(serial->logctx, "Configuring baud rate %d", bval);

    options.c_cflag &= ~CSIZE;
    switch (conf_get_int(conf, CONF_serdatabits)) {
      case 5: options.c_cflag |= CS5; break;
      case 6: options.c_cflag |= CS6; break;
      case 7: options.c_cflag |= CS7; break;
      case 8: options.c_cflag |= CS8; break;
      default: return "Invalid number of data bits (need 5, 6, 7 or 8)";
    }
    logeventf(serial->logctx, "Configuring %d data bits",
              conf_get_int(conf, CONF_serdatabits));

    if (conf_get_int(conf, CONF_serstopbits) >= 4) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }
    logeventf(serial->logctx, "Configuring %s",
              (options.c_cflag & CSTOPB ? "2 stop bits" : "1 stop bit"));

    options.c_iflag &= ~(IXON|IXOFF);
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;
#endif
#ifdef CNEW_RTSCTS
    options.c_cflag &= ~CNEW_RTSCTS;
#endif
    flow = conf_get_int(conf, CONF_serflow);
    if (flow == SER_FLOW_XONXOFF) {
        options.c_iflag |= IXON | IXOFF;
        str = "XON/XOFF";
    } else if (flow == SER_FLOW_RTSCTS) {
#ifdef CRTSCTS
        options.c_cflag |= CRTSCTS;
#endif
#ifdef CNEW_RTSCTS
        options.c_cflag |= CNEW_RTSCTS;
#endif
        str = "RTS/CTS";
    } else
        str = "no";
    logeventf(serial->logctx, "Configuring %s flow control", str);

    /* Parity */
    parity = conf_get_int(conf, CONF_serparity);
    if (parity == SER_PAR_ODD) {
        options.c_cflag |= PARENB;
        options.c_cflag |= PARODD;
        str = "odd";
    } else if (parity == SER_PAR_EVEN) {
        options.c_cflag |= PARENB;
        options.c_cflag &= ~PARODD;
        str = "even";
    } else {
        options.c_cflag &= ~PARENB;
        str = "no";
    }
    logeventf(serial->logctx, "Configuring %s parity", str);

    options.c_cflag |= CLOCAL | CREAD;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(ISTRIP | IGNCR | INLCR | ICRNL
#ifdef IUCLC
                         | IUCLC
#endif
                         );
    options.c_oflag &= ~(OPOST
#ifdef ONLCR
                         | ONLCR
#endif
#ifdef OCRNL
                         | OCRNL
#endif
#ifdef ONOCR
                         | ONOCR
#endif
#ifdef ONLRET
                         | ONLRET
#endif
                         );
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;

    if (tcsetattr(serial->fd, TCSANOW, &options) < 0)
        return "Unable to configure serial port";

    return NULL;
}

/*
 * Called to set up the serial connection.
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static const char *serial_init(Seat *seat, Backend **backend_handle,
                               LogContext *logctx, Conf *conf,
                               const char *host, int port, char **realhost,
                               bool nodelay, bool keepalive)
{
    Serial *serial;
    const char *err;
    char *line;

    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    serial = snew(Serial);
    serial->backend.vt = &serial_backend;
    *backend_handle = &serial->backend;

    serial->seat = seat;
    serial->logctx = logctx;
    serial->finished = false;
    serial->inbufsize = 0;
    bufchain_init(&serial->output_data);

    line = conf_get_str(conf, CONF_serline);
    logeventf(serial->logctx, "Opening serial device %s", line);

    serial->fd = open(line, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
    if (serial->fd < 0)
        return "Unable to open serial port";

    cloexec(serial->fd);

    err = serial_configure(serial, conf);
    if (err)
        return err;

    *realhost = dupstr(line);

    if (!serial_by_fd)
        serial_by_fd = newtree234(serial_compare_by_fd);
    add234(serial_by_fd, serial);

    serial_uxsel_setup(serial);

    /*
     * Specials are always available.
     */
    seat_update_specials_menu(serial->seat);

    return NULL;
}

static void serial_close(Serial *serial)
{
    if (serial->fd >= 0) {
        uxsel_del(serial->fd);
        close(serial->fd);
        serial->fd = -1;
    }
}

static void serial_free(Backend *be)
{
    Serial *serial = container_of(be, Serial, backend);

    serial_close(serial);

    bufchain_clear(&serial->output_data);

    sfree(serial);
}

static void serial_reconfig(Backend *be, Conf *conf)
{
    Serial *serial = container_of(be, Serial, backend);

    /*
     * FIXME: what should we do if this returns an error?
     */
    serial_configure(serial, conf);
}

static void serial_select_result(int fd, int event)
{
    Serial *serial;
    char buf[4096];
    int ret;
    bool finished = false;

    serial = find234(serial_by_fd, &fd, serial_find_by_fd);

    if (!serial)
        return;                /* spurious event; keep going */

    if (event == 1) {
        ret = read(serial->fd, buf, sizeof(buf));

        if (ret == 0) {
            /*
             * Shouldn't happen on a real serial port, but I'm open
             * to the idea that there might be two-way devices we
             * can treat _like_ serial ports which can return EOF.
             */
            finished = true;
        } else if (ret < 0) {
#ifdef EAGAIN
            if (errno == EAGAIN)
                return;        /* spurious */
#endif
#ifdef EWOULDBLOCK
            if (errno == EWOULDBLOCK)
                return;        /* spurious */
#endif
            perror("read serial port");
            exit(1);
        } else if (ret > 0) {
            serial->inbufsize = seat_stdout(serial->seat, buf, ret);
            serial_uxsel_setup(serial); /* might acquire backlog and freeze */
        }
    } else if (event == 2) {
        /*
         * Attempt to send data down the pty.
         */
        serial_try_write(serial);
    }

    if (finished) {
        serial_close(serial);

        serial->finished = true;

        seat_notify_remote_exit(serial->seat);
    }
}

static void serial_uxsel_setup(Serial *serial)
{
    int rwx = 0;

    if (serial->inbufsize <= SERIAL_MAX_BACKLOG)
        rwx |= SELECT_R;
    if (bufchain_size(&serial->output_data))
        rwx |= SELECT_W;               /* might also want to write to it */
    uxsel_set(serial->fd, rwx, serial_select_result);
}

static void serial_try_write(Serial *serial)
{
    ssize_t ret;

    assert(serial->fd >= 0);

    while (bufchain_size(&serial->output_data) > 0) {
        ptrlen data = bufchain_prefix(&serial->output_data);
        ret = write(serial->fd, data.ptr, data.len);

        if (ret < 0 && (errno == EWOULDBLOCK)) {
            /*
             * We've sent all we can for the moment.
             */
            break;
        }
        if (ret < 0) {
            perror("write serial port");
            exit(1);
        }
        bufchain_consume(&serial->output_data, ret);
    }

    serial_uxsel_setup(serial);
}

/*
 * Called to send data down the serial connection.
 */
static size_t serial_send(Backend *be, const char *buf, size_t len)
{
    Serial *serial = container_of(be, Serial, backend);

    if (serial->fd < 0)
        return 0;

    bufchain_add(&serial->output_data, buf, len);
    serial_try_write(serial);

    return bufchain_size(&serial->output_data);
}

/*
 * Called to query the current sendability status.
 */
static size_t serial_sendbuffer(Backend *be)
{
    Serial *serial = container_of(be, Serial, backend);
    return bufchain_size(&serial->output_data);
}

/*
 * Called to set the size of the window
 */
static void serial_size(Backend *be, int width, int height)
{
    /* Do nothing! */
    return;
}

/*
 * Send serial special codes.
 */
static void serial_special(Backend *be, SessionSpecialCode code, int arg)
{
    Serial *serial = container_of(be, Serial, backend);

    if (serial->fd >= 0 && code == SS_BRK) {
        tcsendbreak(serial->fd, 0);
        logevent(serial->logctx, "Sending serial break at user request");
    }

    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *serial_get_specials(Backend *be)
{
    static const struct SessionSpecial specials[] = {
        {"Break", SS_BRK},
        {NULL, SS_EXITMENU}
    };
    return specials;
}

static bool serial_connected(Backend *be)
{
    return true;                       /* always connected */
}

static bool serial_sendok(Backend *be)
{
    return true;
}

static void serial_unthrottle(Backend *be, size_t backlog)
{
    Serial *serial = container_of(be, Serial, backend);
    serial->inbufsize = backlog;
    serial_uxsel_setup(serial);
}

static bool serial_ldisc(Backend *be, int option)
{
    /*
     * Local editing and local echo are off by default.
     */
    return false;
}

static void serial_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    /* This is a stub. */
}

static int serial_exitcode(Backend *be)
{
    Serial *serial = container_of(be, Serial, backend);
    if (serial->fd >= 0)
        return -1;                     /* still connected */
    else
        /* Exit codes are a meaningless concept with serial ports */
        return INT_MAX;
}

/*
 * cfg_info for Serial does nothing at all.
 */
static int serial_cfg_info(Backend *be)
{
    return 0;
}

const struct BackendVtable serial_backend = {
    serial_init,
    serial_free,
    serial_reconfig,
    serial_send,
    serial_sendbuffer,
    serial_size,
    serial_special,
    serial_get_specials,
    serial_connected,
    serial_exitcode,
    serial_sendok,
    serial_ldisc,
    serial_provide_ldisc,
    serial_unthrottle,
    serial_cfg_info,
    NULL /* test_for_upstream */,
    "serial",
    PROT_SERIAL,
    0
};
