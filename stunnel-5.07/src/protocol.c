/*
 *   stunnel       Universal SSL tunnel
 *   Copyright (C) 1998-2014 Michal Trojnara <Michal.Trojnara@mirt.net>
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the
 *   Free Software Foundation; either version 2 of the License, or (at your
 *   option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *   See the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, see <http://www.gnu.org/licenses>.
 *
 *   Linking stunnel statically or dynamically with other modules is making
 *   a combined work based on stunnel. Thus, the terms and conditions of
 *   the GNU General Public License cover the whole combination.
 *
 *   In addition, as a special exception, the copyright holder of stunnel
 *   gives you permission to combine stunnel with free software programs or
 *   libraries that are released under the GNU LGPL and with code included
 *   in the standard release of OpenSSL under the OpenSSL License (or
 *   modified versions of such code, with unchanged license). You may copy
 *   and distribute such a system following the terms of the GNU GPL for
 *   stunnel and the licenses of the other code concerned.
 *
 *   Note that people who make modified versions of stunnel are not obligated
 *   to grant this special exception for their modified versions; it is their
 *   choice whether to do so. The GNU General Public License gives permission
 *   to release a modified version without this exception; this exception
 *   also makes it possible to release a modified version which carries
 *   forward this exception.
 */

#include "common.h"
#include "prototypes.h"

#define is_prefix(a, b) (strncasecmp((a), (b), strlen(b))==0)

/* protocol-specific function prototypes */
NOEXPORT char *proxy_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *cifs_client(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *cifs_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *pgsql_client(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *pgsql_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *smtp_client(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *smtp_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *pop3_client(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *pop3_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *imap_client(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *imap_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *nntp_client(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *connect_server(CLI *, SERVICE_OPTIONS *, const PHASE);
NOEXPORT char *connect_client(CLI *, SERVICE_OPTIONS *, const PHASE);

#if !defined(OPENSSL_NO_MD4) && OPENSSL_VERSION_NUMBER>=0x0090700fL
NOEXPORT void ntlm(CLI *, SERVICE_OPTIONS *);
NOEXPORT char *ntlm1();
NOEXPORT char *ntlm3(char *, char *, char *);
NOEXPORT void crypt_DES(DES_cblock, DES_cblock, DES_cblock);
#endif
NOEXPORT char *base64(int, char *, int);

/**************************************** framework */

char *protocol(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    if(phase==PROTOCOL_CHECK) /* default to be overridden by protocols */
        opt->option.connect_before_ssl=opt->option.client;
    if(!opt->protocol) /* no protocol specified */
        return NULL; /* skip further actions */
    if(!strcasecmp(opt->protocol, "proxy"))
        return opt->option.client ?
            "The 'proxy' protocol is not supported in client mode" :
            proxy_server(c, opt, phase);
    if(!strcasecmp(opt->protocol, "cifs"))
        return opt->option.client ?
            cifs_client(c, opt, phase) :
            cifs_server(c, opt, phase);
    if(!strcasecmp(opt->protocol, "pgsql"))
        return opt->option.client ?
            pgsql_client(c, opt, phase) :
            pgsql_server(c, opt, phase);
    if(!strcasecmp(opt->protocol, "smtp"))
        return opt->option.client ?
            smtp_client(c, opt, phase) :
            smtp_server(c, opt, phase);
    if(!strcasecmp(opt->protocol, "pop3"))
        return opt->option.client ?
            pop3_client(c, opt, phase) :
            pop3_server(c, opt, phase);
    if(!strcasecmp(opt->protocol, "imap"))
        return opt->option.client ?
            imap_client(c, opt, phase) :
            imap_server(c, opt, phase);
    if(!strcasecmp(opt->protocol, "nntp"))
        return opt->option.client ?
            nntp_client(c, opt, phase) :
            "The 'nntp' protocol is not supported in server mode";
    if(!strcasecmp(opt->protocol, "connect"))
        return opt->option.client ?
            connect_client(c, opt, phase) :
            connect_server(c, opt, phase);
    return "Protocol not supported";
}

/**************************************** proxy */

/*
 * PROXY protocol: http://haproxy.1wt.eu/download/1.5/doc/proxy-protocol.txt
 * this is a protocol client support for stunnel acting as an SSL server
 * I don't think anything else is useful, but feel free to discuss on the
 * stunnel-users mailing list if you disagree
 */

/* IP address textual representation length */
/* 1234:6789:1234:6789:1234:6789:1234:6789 -> 40 chars with '\0' */
#define IP_LEN 40
#define PORT_LEN 6

NOEXPORT char *proxy_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    SOCKADDR_UNION addr;
    socklen_t addrlen;
    char src_host[IP_LEN], dst_host[IP_LEN];
    char src_port[PORT_LEN], dst_port[PORT_LEN], *proto;
    int err;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_LATE)
        return NULL;
    addrlen=sizeof addr;
    if(getpeername(c->local_rfd.fd, &addr.sa, &addrlen)) {
        sockerror("getpeername");
        longjmp(c->err, 1);
    }
    err=getnameinfo(&addr.sa, addr_len(&addr), src_host, IP_LEN,
        src_port, PORT_LEN, NI_NUMERICHOST|NI_NUMERICSERV);
    if(err) {
        s_log(LOG_ERR, "getnameinfo: %s", s_gai_strerror(err));
        longjmp(c->err, 1);
    }

    addrlen=sizeof addr;
    if(getsockname(c->local_rfd.fd, &addr.sa, &addrlen)) {
        sockerror("getsockname");
        longjmp(c->err, 1);
    }
    err=getnameinfo(&addr.sa, addr_len(&addr), dst_host, IP_LEN,
        dst_port, PORT_LEN, NI_NUMERICHOST|NI_NUMERICSERV);
    if(err) {
        s_log(LOG_ERR, "getnameinfo: %s", s_gai_strerror(err));
        longjmp(c->err, 1);
    }

    switch(addr.sa.sa_family) {
    case AF_INET:
        proto="TCP4";
        break;
#ifdef USE_IPv6
    case AF_INET6:
        proto="TCP6";
        break;
#endif
    default: /* AF_UNIX */
        proto="UNKNOWN";
    }
    fd_printf(c, c->remote_fd.fd, "PROXY %s %s %s %s %s",
        proto, src_host, dst_host, src_port, dst_port);
    return NULL;
}

/**************************************** cifs */

NOEXPORT char *cifs_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    u8 buffer[5];
    u8 request_dummy[4] = {0x81, 0, 0, 0}; /* a zero-length request */

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    s_write(c, c->remote_fd.fd, request_dummy, 4);
    s_read(c, c->remote_fd.fd, buffer, 5);
    if(buffer[0]!=0x83) { /* NB_SSN_NEGRESP */
        s_log(LOG_ERR, "Negative response expected");
        longjmp(c->err, 1);
    }
    if(buffer[2]!=0 || buffer[3]!=1) { /* length != 1 */
        s_log(LOG_ERR, "Unexpected NetBIOS response size");
        longjmp(c->err, 1);
    }
    if(buffer[4]!=0x8e) { /* use SSL */
        s_log(LOG_ERR, "Remote server does not require SSL");
        longjmp(c->err, 1);
    }
    return NULL;
}

NOEXPORT char *cifs_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    u8 buffer[128];
    u8 response_access_denied[5] = {0x83, 0, 0, 1, 0x81};
    u8 response_use_ssl[5] = {0x83, 0, 0, 1, 0x8e};
    u16 len;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_EARLY)
        return NULL;
    s_read(c, c->local_rfd.fd, buffer, 4) ;/* NetBIOS header */
    len=buffer[3];
    len|=(u16)(buffer[2]) << 8;
    if(len>sizeof buffer-4) {
        s_log(LOG_ERR, "Received block too long");
        longjmp(c->err, 1);
    }
    s_read(c, c->local_rfd.fd, buffer+4, len);
    if(buffer[0]!=0x81) { /* NB_SSN_REQUEST */
        s_log(LOG_ERR, "Client did not send session setup");
        s_write(c, c->local_wfd.fd, response_access_denied, 5);
        longjmp(c->err, 1);
    }
    s_write(c, c->local_wfd.fd, response_use_ssl, 5);
    return NULL;
}

/**************************************** pgsql */

/* http://www.postgresql.org/docs/8.3/static/protocol-flow.html#AEN73982 */
static const u8 ssl_request[8]={0, 0, 0, 8, 0x04, 0xd2, 0x16, 0x2f};

NOEXPORT char *pgsql_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    u8 buffer[1];

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    s_write(c, c->remote_fd.fd, ssl_request, sizeof ssl_request);
    s_read(c, c->remote_fd.fd, buffer, 1);
    /* S - accepted, N - rejected, non-SSL preferred */
    if(buffer[0]!='S') {
        s_log(LOG_ERR, "PostgreSQL server rejected SSL");
        longjmp(c->err, 1);
    }
    return NULL;
}

NOEXPORT char *pgsql_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    u8 buffer[8], ssl_ok[1]={'S'};

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_EARLY)
        return NULL;
    memset(buffer, 0, sizeof buffer);
    s_read(c, c->local_rfd.fd, buffer, sizeof buffer);
    if(safe_memcmp(buffer, ssl_request, sizeof ssl_request)) {
        s_log(LOG_ERR, "PostgreSQL client did not request SSL, rejecting");
        /* no way to send error on startup, so just drop the client */
        longjmp(c->err, 1);
    }
    s_write(c, c->local_wfd.fd, ssl_ok, sizeof ssl_ok);
    return NULL;
}

/**************************************** smtp */

NOEXPORT char *smtp_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    line=str_dup("");
    do { /* copy multiline greeting */
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd);
        fd_putline(c, c->local_wfd.fd, line);
    } while(is_prefix(line, "220-"));

    fd_putline(c, c->remote_fd.fd, "EHLO localhost");
    do { /* skip multiline reply */
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd);
    } while(is_prefix(line, "250-"));
    if(!is_prefix(line, "250 ")) { /* error */
        s_log(LOG_ERR, "Remote server is not RFC 1425 compliant");
        str_free(line);
        longjmp(c->err, 1);
    }

    fd_putline(c, c->remote_fd.fd, "STARTTLS");
    do { /* skip multiline reply */
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd);
    } while(is_prefix(line, "220-"));
    if(!is_prefix(line, "220 ")) { /* error */
        s_log(LOG_ERR, "Remote server is not RFC 2487 compliant");
        str_free(line);
        longjmp(c->err, 1);
    }
    str_free(line);
    return NULL;
}

NOEXPORT char *smtp_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line, *domain, *greeting;

    (void)opt; /* skip warning about unused parameter */
    if(phase==PROTOCOL_CHECK)
        opt->option.connect_before_ssl=1; /* c->remote_fd needed */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;

    /* detect RFC 2487 */
    s_poll_init(c->fds);
    s_poll_add(c->fds, c->local_rfd.fd, 1, 0);
    switch(s_poll_wait(c->fds, 0, 200)) { /* wait up to 200ms */
    case 0: /* fd not ready to read */
        s_log(LOG_DEBUG, "RFC 2487 detected");
        break;
    case 1: /* fd ready to read */
        s_log(LOG_DEBUG, "RFC 2487 not detected");
        return NULL; /* return if RFC 2487 is not used */
    default: /* -1 */
        sockerror("RFC2487 (s_poll_wait)");
        longjmp(c->err, 1);
    }

    /* process server's greeting */
    line=fd_getline(c, c->remote_fd.fd);
    if(!(is_prefix(line, "220 ") || is_prefix(line, "220-"))) {
        s_log(LOG_ERR, "Unknown server welcome");
        str_free(line);
        longjmp(c->err, 1);
    }
    domain=str_dup(line+4); /* skip "220" and the separator */
    line[4]='\0';     /* only leave "220" and the separator */
    greeting=strchr(domain, ' ');
    if(greeting) {
        *greeting++='\0'; /* truncate anything after the domain name */
        fd_printf(c, c->local_wfd.fd, "%s%s stunnel for %s",
            line, domain, greeting);
    } else {
        fd_printf(c, c->local_wfd.fd, "%s%s stunnel", line, domain);
    }
    while(is_prefix(line, "220-")) { /* copy multiline greeting */
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd);
        fd_putline(c, c->local_wfd.fd, line);
    }
    str_free(line);

    /* process client's EHLO */
    line=fd_getline(c, c->local_rfd.fd);
    if(!is_prefix(line, "EHLO ")) {
        s_log(LOG_ERR, "Unknown client EHLO");
        str_free(line);
        str_free(domain);
        longjmp(c->err, 1);
    }
    str_free(line);
    fd_printf(c, c->local_wfd.fd, "250-%s", domain);
    str_free(domain);
    fd_putline(c, c->local_wfd.fd, "250 STARTTLS");

    /* process client's STARTTLS */
    line=fd_getline(c, c->local_rfd.fd);
    if(!is_prefix(line, "STARTTLS")) {
        s_log(LOG_ERR, "STARTTLS expected");
        str_free(line);
        longjmp(c->err, 1);
    }
    fd_putline(c, c->local_wfd.fd, "220 Go ahead");
    str_free(line);

    return NULL;
}

/**************************************** pop3 */

NOEXPORT char *pop3_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "+OK ")) {
        s_log(LOG_ERR, "Unknown server welcome");
        str_free(line);
        longjmp(c->err, 1);
    }
    fd_putline(c, c->local_wfd.fd, line);
    fd_putline(c, c->remote_fd.fd, "STLS");
    str_free(line);
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "+OK ")) {
        s_log(LOG_ERR, "Server does not support TLS");
        str_free(line);
        longjmp(c->err, 1);
    }
    str_free(line);
    return NULL;
}

NOEXPORT char *pop3_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line;

    (void)opt; /* skip warning about unused parameter */
    if(phase==PROTOCOL_CHECK)
        opt->option.connect_before_ssl=1; /* c->remote_fd needed */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    line=fd_getline(c, c->remote_fd.fd);
    fd_printf(c, c->local_wfd.fd, "%s + stunnel", line);
    str_free(line);
    line=fd_getline(c, c->local_rfd.fd);
    if(is_prefix(line, "CAPA")) { /* client wants RFC 2449 extensions */
        fd_putline(c, c->local_wfd.fd, "+OK Stunnel capability list follows");
        fd_putline(c, c->local_wfd.fd, "STLS");
        fd_putline(c, c->local_wfd.fd, ".");
        str_free(line);
        line=fd_getline(c, c->local_rfd.fd);
    }
    if(!is_prefix(line, "STLS")) {
        s_log(LOG_ERR, "Client does not want TLS");
        str_free(line);
        longjmp(c->err, 1);
    }
    str_free(line);
    fd_putline(c, c->local_wfd.fd, "+OK Stunnel starts TLS negotiation");
    return NULL;
}

/**************************************** imap */

NOEXPORT char *imap_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "* OK")) {
        s_log(LOG_ERR, "Unknown server welcome");
        str_free(line);
        longjmp(c->err, 1);
    }
    fd_putline(c, c->local_wfd.fd, line);
    fd_putline(c, c->remote_fd.fd, "stunnel STARTTLS");
    str_free(line);
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "stunnel OK")) {
        fd_putline(c, c->local_wfd.fd,
            "* BYE stunnel: Server does not support TLS");
        s_log(LOG_ERR, "Server does not support TLS");
        str_free(line);
        longjmp(c->err, 2); /* don't reset */
    }
    str_free(line);
    return NULL;
}

NOEXPORT char *imap_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line, *id, *tail, *capa;

    (void)opt; /* skip warning about unused parameter */
    if(phase==PROTOCOL_CHECK)
        opt->option.connect_before_ssl=1; /* c->remote_fd needed */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    s_poll_init(c->fds);
    s_poll_add(c->fds, c->local_rfd.fd, 1, 0);
    switch(s_poll_wait(c->fds, 0, 200)) {
    case 0: /* fd not ready to read */
        s_log(LOG_DEBUG, "RFC 2595 detected");
        break;
    case 1: /* fd ready to read */
        s_log(LOG_DEBUG, "RFC 2595 not detected");
        return NULL; /* return if RFC 2595 is not used */
    default: /* -1 */
        sockerror("RFC2595 (s_poll_wait)");
        longjmp(c->err, 1);
    }

    /* process server welcome and send it to client */
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "* OK")) {
        s_log(LOG_ERR, "Unknown server welcome");
        str_free(line);
        longjmp(c->err, 1);
    }
    capa=strstr(line, "CAPABILITY");
    if(!capa)
        capa=strstr(line, "capability");
    if(capa)
        *capa='K'; /* disable CAPABILITY within greeting */
    fd_printf(c, c->local_wfd.fd, "%s (stunnel)", line);

    id=str_dup("");
    while(1) { /* process client commands */
        str_free(line);
        line=fd_getline(c, c->local_rfd.fd);
        /* split line into id and tail */
        str_free(id);
        id=str_dup(line);
        tail=strchr(id, ' ');
        if(!tail)
            break;
        *tail++='\0';

        if(is_prefix(tail, "STARTTLS")) {
            fd_printf(c, c->local_wfd.fd,
                "%s OK Begin TLS negotiation now", id);
            str_free(line);
            str_free(id);
            return NULL; /* success */
        } else if(is_prefix(tail, "CAPABILITY")) {
            fd_putline(c, c->remote_fd.fd, line); /* send it to server */
            str_free(line);
            line=fd_getline(c, c->remote_fd.fd); /* get the capabilites */
            if(*line=='*') {
                /*
                 * append STARTTLS
                 * should also add LOGINDISABLED, but can't because
                 * of Mozilla bug #324138/#312009
                 * LOGIN would fail as "unexpected command", anyway
                 */
                fd_printf(c, c->local_wfd.fd, "%s STARTTLS", line);
                str_free(line);
                line=fd_getline(c, c->remote_fd.fd); /* next line */
            }
            fd_putline(c, c->local_wfd.fd, line); /* forward to the client */
            tail=strchr(line, ' ');
            if(!tail || !is_prefix(tail+1, "OK")) { /* not OK? */
                fd_putline(c, c->local_wfd.fd,
                    "* BYE unexpected server response");
                s_log(LOG_ERR, "Unexpected server response: %s", line);
                break;
            }
        } else if(is_prefix(tail, "LOGOUT")) {
            fd_putline(c, c->local_wfd.fd, "* BYE server terminating");
            fd_printf(c, c->local_wfd.fd, "%s OK LOGOUT completed", id);
            break;
        } else {
            fd_putline(c, c->local_wfd.fd, "* BYE stunnel: unexpected command");
            fd_printf(c, c->local_wfd.fd, "%s BAD %s unexpected", id, tail);
            s_log(LOG_ERR, "Unexpected client command %s", tail);
            break;
        }
    }
    /* clean server shutdown */
    str_free(id);
    fd_putline(c, c->remote_fd.fd, "stunnel LOGOUT");
    str_free(line);
    line=fd_getline(c, c->remote_fd.fd);
    if(*line=='*') {
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd);
    }
    str_free(line);
    longjmp(c->err, 2); /* don't reset */
    return NULL; /* some C compilers require a return value */
}

/**************************************** nntp */

NOEXPORT char *nntp_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "200 ") && !is_prefix(line, "201 ")) {
        s_log(LOG_ERR, "Unknown server welcome");
        str_free(line);
        longjmp(c->err, 1);
    }
    fd_putline(c, c->local_wfd.fd, line);
    fd_putline(c, c->remote_fd.fd, "STARTTLS");
    str_free(line);
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "382 ")) {
        s_log(LOG_ERR, "Server does not support TLS");
        str_free(line);
        longjmp(c->err, 1);
    }
    str_free(line);
    return NULL;
}

/**************************************** connect */

NOEXPORT char *connect_server(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *request, *proto, *header;
    NAME_LIST host_list;

    (void)opt; /* skip warning about unused parameter */
    if(phase!=PROTOCOL_EARLY)
        return NULL;
    request=fd_getline(c, c->local_rfd.fd);
    if(!is_prefix(request, "CONNECT ")) {
        fd_putline(c, c->local_wfd.fd, "HTTP/1.0 400 Bad Request Method");
        fd_putline(c, c->local_wfd.fd, "Server: stunnel/" STUNNEL_VERSION);
        fd_putline(c, c->local_wfd.fd, "");
        str_free(request);
        longjmp(c->err, 1);
    }
    proto=strchr(request+8, ' ');
    if(!proto || !is_prefix(proto, " HTTP/")) {
        fd_putline(c, c->local_wfd.fd, "HTTP/1.0 400 Bad Request Protocol");
        fd_putline(c, c->local_wfd.fd, "Server: stunnel/" STUNNEL_VERSION);
        fd_putline(c, c->local_wfd.fd, "");
        str_free(request);
        longjmp(c->err, 1);
    }
    *proto='\0';

    header=str_dup("");
    do { /* ignore any headers */
        str_free(header);
        header=fd_getline(c, c->local_rfd.fd);
    } while(*header); /* not empty */
    str_free(header);

    host_list.name=request+8;
    host_list.next=NULL;
    if(!namelist2addrlist(&c->connect_addr, &host_list, DEFAULT_LOOPBACK)) {
        fd_putline(c, c->local_wfd.fd, "HTTP/1.0 404 Not Found");
        fd_putline(c, c->local_wfd.fd, "Server: stunnel/" STUNNEL_VERSION);
        fd_putline(c, c->local_wfd.fd, "");
        str_free(request);
        longjmp(c->err, 1);
    }
    str_free(request);
    fd_putline(c, c->local_wfd.fd, "HTTP/1.0 200 OK");
    fd_putline(c, c->local_wfd.fd, "Server: stunnel/" STUNNEL_VERSION);
    fd_putline(c, c->local_wfd.fd, "");
    return NULL;
}

NOEXPORT char *connect_client(CLI *c, SERVICE_OPTIONS *opt, const PHASE phase) {
    char *line, *encoded;

    if(phase!=PROTOCOL_MIDDLE)
        return NULL;
    if(!opt->protocol_host) {
        s_log(LOG_ERR, "protocolHost not specified");
        longjmp(c->err, 1);
    }
    fd_printf(c, c->remote_fd.fd, "CONNECT %s HTTP/1.1",
        opt->protocol_host);
    fd_printf(c, c->remote_fd.fd, "Host: %s", opt->protocol_host);
    if(opt->protocol_username && opt->protocol_password) {
        if(!strcasecmp(opt->protocol_authentication, "ntlm")) {
#if !defined(OPENSSL_NO_MD4) && OPENSSL_VERSION_NUMBER>=0x0090700fL
            ntlm(c, opt);
#else
            s_log(LOG_ERR, "NTLM authentication is not available");
            longjmp(c->err, 1);
#endif
        } else { /* basic authentication */
            line=str_printf("%s:%s",
                opt->protocol_username, opt->protocol_password);
            encoded=base64(1, line, strlen(line));
            str_free(line);
            if(!encoded) {
                s_log(LOG_ERR, "Base64 encoder failed");
                longjmp(c->err, 1);
            }
            fd_printf(c, c->remote_fd.fd, "Proxy-Authorization: basic %s",
                encoded);
            str_free(encoded);
        }
    }
    fd_putline(c, c->remote_fd.fd, ""); /* empty line */
    line=fd_getline(c, c->remote_fd.fd);
    if(!is_prefix(line, "HTTP/1.0 2") && !is_prefix(line, "HTTP/1.1 2")) {
        /* not "HTTP/1.x 2xx Connection established" */
        s_log(LOG_ERR, "CONNECT request rejected");
        do { /* read all headers */
            str_free(line);
            line=fd_getline(c, c->remote_fd.fd);
        } while(*line);
        str_free(line);
        longjmp(c->err, 1);
    }
    s_log(LOG_INFO, "CONNECT request accepted");
    do {
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd); /* read all headers */
    } while(*line);
    str_free(line);
    return NULL;
}

#if !defined(OPENSSL_NO_MD4) && OPENSSL_VERSION_NUMBER>=0x0090700fL

/*
 * NTLM code is based on the following documentation:
 * http://davenport.sourceforge.net/ntlm.html
 * http://www.innovation.ch/personal/ronald/ntlm.html
 */

#define s_min(a, b) ((a)>(b)?(b):(a))

NOEXPORT void ntlm(CLI *c, SERVICE_OPTIONS *opt) {
    char *line, buf[BUFSIZ], *ntlm1_txt, *ntlm2_txt, *ntlm3_txt, *tmpstr;
    long content_length=0; /* no HTTP content */

    /* send Proxy-Authorization (phase 1) */
    fd_printf(c, c->remote_fd.fd, "Proxy-Connection: keep-alive");
    ntlm1_txt=ntlm1();
    if(!ntlm1_txt) {
        s_log(LOG_ERR, "Proxy-Authenticate: Failed to build NTLM request");
        longjmp(c->err, 1);
    }
    fd_printf(c, c->remote_fd.fd, "Proxy-Authorization: NTLM %s", ntlm1_txt);
    str_free(ntlm1_txt);
    fd_putline(c, c->remote_fd.fd, ""); /* empty line */
    line=fd_getline(c, c->remote_fd.fd);

    /* receive Proxy-Authenticate (phase 2) */
    if(!is_prefix(line, "HTTP/1.0 407") && !is_prefix(line, "HTTP/1.1 407")) {
        s_log(LOG_ERR, "Proxy-Authenticate: NTLM authorization request rejected");
        do { /* read all headers */
            str_free(line);
            line=fd_getline(c, c->remote_fd.fd);
        } while(*line);
        str_free(line);
        longjmp(c->err, 1);
    }
    ntlm2_txt=NULL;
    do { /* read all headers */
        str_free(line);
        line=fd_getline(c, c->remote_fd.fd);
        if(is_prefix(line, "Proxy-Authenticate: NTLM "))
            ntlm2_txt=str_dup(line+25);
        else if(is_prefix(line, "Content-Length: ")) {
            content_length=strtol(line+16, &tmpstr, 10);
            if(tmpstr==line+16 || *tmpstr || content_length<0) {
                s_log(LOG_ERR, "Proxy-Authenticate: Invalid Content-Length");
                str_free(line);
                longjmp(c->err, 1);
            }
        }
    } while(*line);
    if(!ntlm2_txt) { /* no Proxy-Authenticate: NTLM header */
        s_log(LOG_ERR, "Proxy-Authenticate: NTLM header not found");
        str_free(line);
        longjmp(c->err, 1);
    }

    /* read and ignore HTTP content (if any) */
    while(content_length>0) {
        s_read(c, c->remote_fd.fd, buf, s_min(content_length, BUFSIZ));
        content_length-=s_min(content_length, BUFSIZ);
    }

    /* send Proxy-Authorization (phase 3) */
    fd_printf(c, c->remote_fd.fd, "CONNECT %s HTTP/1.1", opt->protocol_host);
    fd_printf(c, c->remote_fd.fd, "Host: %s", opt->protocol_host);
    ntlm3_txt=ntlm3(opt->protocol_username, opt->protocol_password, ntlm2_txt);
    str_free(ntlm2_txt);
    if(!ntlm3_txt) {
        s_log(LOG_ERR, "Proxy-Authenticate: Failed to build NTLM response");
        longjmp(c->err, 1);
    }
    fd_printf(c, c->remote_fd.fd, "Proxy-Authorization: NTLM %s", ntlm3_txt);
    str_free(ntlm3_txt);
}

NOEXPORT char *ntlm1() {
    char phase1[16];

    memset(phase1, 0, sizeof phase1);
    strcpy(phase1, "NTLMSSP");
    phase1[8]=1; /* type: 1 */
    phase1[12]=2; /* flag: negotiate OEM */
    phase1[13]=2; /* flag: negotiate NTLM */
    return base64(1, phase1, sizeof phase1); /* encode */
}

NOEXPORT char *ntlm3(char *username, char *password, char *phase2) {
    MD4_CTX md4;
    char *decoded; /* decoded reply from proxy */
    char phase3[146];
    unsigned char md4_hash[21];
    unsigned int userlen=strlen(username);
    unsigned int phase3len=s_min(88+userlen, sizeof phase3);

    /* setup phase3 structure */
    memset(phase3, 0, sizeof phase3);
    strcpy(phase3, "NTLMSSP");
    phase3[8]=3;            /* type: 3 */
    phase3[16]=phase3len;   /* LM-resp off */
    phase3[20]=24;          /* NT-resp len */
    phase3[22]=24;          /* NT-Resp len */
    phase3[24]=64;          /* NT-resp off */
    phase3[32]=phase3len;   /* domain offset */
    phase3[36]=userlen;     /* user length */
    phase3[38]=userlen;     /* user length */
    phase3[40]=88;          /* user offset */
    phase3[48]=phase3len;   /* host offset */
    phase3[56]=phase3len;   /* message len */
    phase3[60]=2;           /* flag: negotiate OEM */
    phase3[61]=2;           /* flag: negotiate NTLM */

    /* calculate MD4 of UTF-16 encoded password */
    MD4_Init(&md4);
    while(*password) {
        MD4_Update(&md4, password++, 1);
        MD4_Update(&md4, "", 1); /* UTF-16 */
    }
    MD4_Final(md4_hash, &md4);
    memset(md4_hash+16, 0, 5); /* pad to 21 bytes */

    /* decode challenge and calculate response */
    decoded=base64(0, phase2, strlen(phase2)); /* decode */
    if(!decoded)
        return NULL;
    crypt_DES((unsigned char *)phase3+64,
        (unsigned char *)decoded+24, md4_hash);
    crypt_DES((unsigned char *)phase3+72,
        (unsigned char *)decoded+24, md4_hash+7);
    crypt_DES((unsigned char *)phase3+80,
        (unsigned char *)decoded+24, md4_hash+14);
    str_free(decoded);

    strncpy(phase3+88, username, sizeof phase3-88);

    return base64(1, phase3, phase3len); /* encode */
}

NOEXPORT void crypt_DES(DES_cblock dst, const_DES_cblock src, DES_cblock hash) {
    DES_cblock key;
    DES_key_schedule sched;

    /* convert key from 56 to 64 bits */
    key[0]=hash[0];
    key[1]=((hash[0]&1)<<7)|(hash[1]>>1);
    key[2]=((hash[1]&3)<<6)|(hash[2]>>2);
    key[3]=((hash[2]&7)<<5)|(hash[3]>>3);
    key[4]=((hash[3]&15)<<4)|(hash[4]>>4);
    key[5]=((hash[4]&31)<<3)|(hash[5]>>5);
    key[6]=((hash[5]&63)<<2)|(hash[6]>>6);
    key[7]=((hash[6]&127)<<1);
    DES_set_odd_parity(&key);

    /* encrypt */
    DES_set_key_unchecked(&key, &sched);
    DES_ecb_encrypt((const_DES_cblock *)src,
        (DES_cblock *)dst, &sched, DES_ENCRYPT);
}

#endif

NOEXPORT char *base64(int encode, char *in, int len) {
    BIO *bio, *b64;
    char *out;
    int n;

    b64=BIO_new(BIO_f_base64());
    if(!b64)
        return NULL;
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio=BIO_new(BIO_s_mem());
    if(!bio) {
        str_free(b64);
        return NULL;
    }
    if(encode)
        bio=BIO_push(b64, bio);
    BIO_write(bio, in, len);
    (void)BIO_flush(bio); /* ignore the error if any */
    if(encode) {
        bio=BIO_pop(bio);
        BIO_free(b64);
    } else {
        bio=BIO_push(b64, bio);
    }
    n=BIO_pending(bio);
    /* 32 bytes as a safety precaution for passing decoded data to crypt_DES */
    /* n+1 to get null-terminated string on encode */
    out=str_alloc(n<32?32:n+1);
    n=BIO_read(bio, out, n);
    if(n<0) {
        BIO_free_all(bio);
        str_free(out);
        return NULL;
    }
    BIO_free_all(bio);
    return out;
}

/* end of protocol.c */