/*
 * Amiga Networking Stub for WebKit Integration
 * Minimal blocking HTTP GET using bsdsocket.library
 * 
 * Usage: Drop this into WebCore for synchronous resource loading
 * Later: Make async by posting completions to MyRunLoop
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>   /* for snprintf */
#include <arpa/inet.h>  /* for inet_addr */
#include <sys/time.h>
#include <sys/socket.h>   /* for SHUT_RDWR */
#include <ctype.h>        /* for isdigit, tolower */
#include <strings.h>      /* for strncasecmp */
#include <errno.h>        /* for EINTR, EPIPE, etc. */

struct Library* SocketBase;

/* Global error caching for post-cleanup error reporting */
static LONG g_http_last_errno = 0;

/*
 * Minimal blocking HTTP GET (error handling trimmed for brevity)
 * Returns: bytes read on success, negative value on error
 * 
 * Error codes:
 * -1: Failed to open bsdsocket.library
 * -2: Failed to create socket
 * -3: Failed to resolve hostname
 * -4: Failed to connect
 * -5: Invalid buffer parameters
 * -6: Request formatting error
 * -7: Partial send / write error
 * -8: Receive timeout / read error
 * 
 * Features:
 * - HTTP/1.1 with chunked transfer encoding support
 * - 5-second receive timeout for friendly failure modes
 * - Numeric IP fast-path with inet_aton (handles edge cases)
 * - Graceful shutdown for better stack compatibility
 * - Professional HTTP headers (Host:port for non-80, Accept: */*, Accept-Encoding: identity, Connection: close, User-Agent)
 * - Path safety (defaults to "/" if NULL/empty)
 * - Robust recv loop with EINTR handling
 * - Partial send detection and error reporting
 * - Lenient header parsing (supports both \r\n\r\n and \n\n)
 * - HTTP status & content-length parsing helper
 * - In-place chunked body dechunker
 * - Detailed error reporting via AmigaOS native Errno()
 * - Post-cleanup error reporting via http_last_errno()
 * - Defensive SocketBase cleanup (prevents accidental reuse)
 * 
 * CONCURRENCY NOTES:
 * - http_last_errno() is global - read immediately after http_get() call
 * - SocketBase is global - only one task should use sockets at a time
 * - For multi-threading: centralize socket I/O on one "network" thread
 * 
 * FUTURE TLS IMPLEMENTATION (for concurrent loads):
 * - Move http_last_errno to per-thread storage using MyTLSCreate/MyTLSSet/MyTLSGet
 * - Example: static MyTLSKey kHttpErr = 0xFFFF; /* init once */
 * - Set: MyTLSSet(kHttpErr, (void*)(IPTR)Errno());
 * - Get: return (LONG)(IPTR)MyTLSGet(kHttpErr);
 */
LONG http_get(const char* host, UWORD port, const char* path, char* out, LONG outCap)
{
    if (!out || outCap <= 0) return HTTP_ERR_BADBUF; /* guard bad buffer */
    
    /* Host validation: treat empty host as resolve failure */
    if (!host || !*host) return HTTP_ERR_RESOLVE; /* treat as resolve failure */
    
    /* Path safety: default to "/" if NULL or empty */
    if (!path || !*path) path = "/";

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) return HTTP_ERR_OPENLIB;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { 
        g_http_last_errno = Errno();
        CloseLibrary(SocketBase); 
        SocketBase = NULL;
        return HTTP_ERR_SOCKET; 
    }

    struct sockaddr_in sa; 
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    
    /* Numeric host fast-path: use inet_aton for better edge case handling */
    struct in_addr ina;
    if (inet_aton(host, &ina)) {
        sa.sin_addr = ina;
    } else {
        struct hostent* he = gethostbyname(host);
        if (!he) { 
            g_http_last_errno = Errno();
            CloseSocket(s); 
            CloseLibrary(SocketBase); 
            SocketBase = NULL;
            return HTTP_ERR_RESOLVE; 
        }
        memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }

    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        g_http_last_errno = Errno();
        CloseSocket(s); 
        CloseLibrary(SocketBase); 
        SocketBase = NULL;
        return HTTP_ERR_CONNECT;
    }

    /* Set receive timeout for friendlier failure modes */
    struct timeval tv = {5, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Build host header conditionally to avoid "Host: example.com0" bug */
    char hostLine[256];
    int hostLen;
    if (port != 80)
        hostLen = snprintf(hostLine, sizeof(hostLine), "Host: %s:%u\r\n", host, (unsigned)port);
    else
        hostLen = snprintf(hostLine, sizeof(hostLine), "Host: %s\r\n", host);
    
    if (hostLen <= 0 || hostLen >= (int)sizeof(hostLine)) {
        g_http_last_errno = -1; /* formatting/truncation */
        shutdown(s, SHUT_RDWR);
        CloseSocket(s);
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return HTTP_ERR_FORMAT; /* formatting/truncation */
    }

    char req[512];
    int reqLen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"          /* was HTTP/1.0 */
        "%s"
        "Accept: */*\r\n"
        "Accept-Encoding: identity\r\n"/* avoid gzip/deflate until we add decompress */
        "Connection: close\r\n"        /* keep simple: server will close to signal EOF */
        "User-Agent: AmigaWebKit/0.1\r\n"
        "\r\n",
        path, hostLine);
    if (reqLen <= 0 || reqLen >= (int)sizeof(req)) {
        g_http_last_errno = -1; /* format error */
        CloseSocket(s); 
        CloseLibrary(SocketBase); 
        SocketBase = NULL;
        return HTTP_ERR_FORMAT; /* format error / truncated */
    }

    /* send the whole request */
    int sent = 0;
    while (sent < reqLen) {
        int n = send(s, req + sent, reqLen - sent, 0);
        if (n < 0) {
            LONG e = Errno();
            if (e == EINTR) continue;          /* interrupted, retry */
            g_http_last_errno = e;
            break;                              /* other errors end the loop */
        }
        if (n == 0) break;                     /* shouldn't happen on TCP, but bail */
        sent += n;
    }
    if (sent < reqLen) {
        g_http_last_errno = Errno(); /* likely EPIPE / ECONNRESET */
        shutdown(s, SHUT_RDWR);
        CloseSocket(s);
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return HTTP_ERR_PARTIALSEND; /* partial send / write error */
    }

    LONG total = 0;
    for (;;) {
        LONG space = outCap - total;
        if (space <= 0) break;
        LONG n = recv(s, out + total, space, 0);
        if (n < 0) {
            LONG e = Errno();
            if (e == EINTR) continue;          /* interrupted, retry */
            g_http_last_errno = e;
            total = (total > 0) ? total : HTTP_ERR_RECV; /* recv error/timeout */
            break;                              /* other errors end the loop */
        }
        if (n == 0) break;                      /* EOF */
        total += n;
    }

    /* Handle receive errors/timeouts with no data */
    if (total < 0) { /* error/timeout with no data */
        shutdown(s, SHUT_RDWR);
        CloseSocket(s);
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return total; /* -8 */
    }
    
    /* NUL-terminate if there's room (useful for debug prints) */
    if (total < outCap) out[total] = 0;

    /* Graceful close: shutdown before close to flush some stacks */
    shutdown(s, SHUT_RDWR);
    CloseSocket(s);
    CloseLibrary(SocketBase);
    SocketBase = NULL;  /* Prevent accidental reuse */
    g_http_last_errno = 0; /* success */
    return total; /* bytes read, or <0 on error */
}

/*
 * HTTP Response Parser Helper
 * Splits HTTP response into headers and body for quick resource decoding
 */
LONG http_parse_response(const char* response, LONG response_len,
                        char* headers, LONG headers_cap,
                        char* body, LONG body_cap)
{
    if (!response || !headers || !body || response_len <= 0 || headers_cap <= 0 || body_cap <= 0) {
        return -1; /* Invalid parameters */
    }
    
    /* Find end of headers (double CRLF) */
    const char* body_start = NULL;
    LONG headers_len = 0;
    
    /* Primary search: strict \r\n\r\n */
    for (LONG i = 0; i < response_len - 3; i++) {
        if (response[i] == '\r' && response[i+1] == '\n' && 
            response[i+2] == '\r' && response[i+3] == '\n') {
            body_start  = response + i + 4;
            headers_len = i;     /* Exclude the terminal CRLFCRLF */
            break;
        }
    }
    
    /* Fallback search: lenient \n\n (for servers that omit CRs) */
    if (!body_start) {
        for (LONG i = 0; i < response_len - 1; i++) {
            if (response[i] == '\n' && response[i+1] == '\n') {
                body_start  = response + i + 2;
                headers_len = i;     /* Exclude the terminal \n\n */
                break;
            }
        }
    }
    
    if (!body_start) {
        /* No body found - headers only */
        if (headers_len == 0) headers_len = response_len;
        if (headers_len >= headers_cap) return -2; /* Headers buffer too small */
        
        memcpy(headers, response, headers_len);
        headers[headers_len] = 0; /* NUL-terminate */
        return 0; /* Headers only */
    }
    
    /* Copy headers */
    if (headers_len >= headers_cap) return -2; /* Headers buffer too small */
    memcpy(headers, response, headers_len);
    headers[headers_len] = 0; /* NUL-terminate */
    
    /* Copy body */
    LONG body_len = response_len - (body_start - response);
    if (body_len > body_cap) return -3; /* Body buffer too small */
    
    memcpy(body, body_start, body_len);
    return body_len; /* Return body length */
}

/*
 * HTTP Status & Content-Length Parser Helper
 * Parses common HTTP response information for quick resource decoding
 */
static int ci_starts_with(const char* p, const char* k) {
    while (*k && *p && (tolower((unsigned char)*p) == tolower((unsigned char)*k))) { ++p; ++k; }
    return *k == '\0';
}

HttpInfo http_parse_status_and_length(const char* headers)
{
    HttpInfo info = { -1, -1, FALSE };

    if (!headers) return info;

    /* ---- status line: HTTP/1.x <code> ... ---- */
    const char* p = headers;
    if (ci_starts_with(p, "HTTP/")) {
        /* skip to first space */
        while (*p && *p != ' ') ++p;
        while (*p == ' ') ++p;
        LONG code = 0; int digits = 0;
        while (isdigit((unsigned char)*p)) { code = code*10 + (*p - '0'); ++p; ++digits; }
        if (digits >= 3) info.status = code;
    }

    /* ---- scan headers ---- */
    for (const char* line = headers; *line; ) {
        const char* eol = strchr(line, '\n');
        size_t linelen = eol ? (size_t)(eol - line) : strlen(line);

        /* skip trailing CR */
        size_t eff = linelen;
        if (eff && line[eff-1] == '\r') --eff;

        if (eff >= 16 && !strncasecmp(line, "Content-Length:", 15)) {
            const char* v = line + 15;
            while (*v == ' ' || *v == '\t') ++v;
            LONG n = 0; int any = 0;
            while (v < line + eff && isdigit((unsigned char)*v)) { n = n*10 + (*v - '0'); ++v; any = 1; }
            if (any) info.content_length = n;
        } else if (eff >= 18 && !strncasecmp(line, "Transfer-Encoding:", 18)) {
            const char* v = line + 18;
            while (*v == ' ' || *v == '\t') ++v;
            if (!strncasecmp(v, "chunked", 7)) info.chunked = TRUE;
        }

        if (!eol) break;
        line = eol + 1;
        if (*line == '\r' && line[1] == '\n') break; /* already trimmed, but harmless */
    }

    return info;
}

/*
 * Helpers for hex parsing
 */
static int is_hex(int c) {
    unsigned char u = (unsigned char)c;
    return (u>='0'&&u<='9')||(u>='a'&&u<='f')||(u>='A'&&u<='F');
}

static unsigned to_hex(int c){
    unsigned char u = (unsigned char)c;
    if (u>='0'&&u<='9') return (unsigned)(u-'0');
    if (u>='a'&&u<='f') return (unsigned)(u-'a'+10);
    return (unsigned)(u-'A'+10);
}

/*
 * Accept CRLF or bare LF; return bytes consumed for the line ending (2 or 1), or 0 if none.
 */
static int consume_line_end(const char* p, const char* end) {
    if (p<end && *p=='\n') return 1;
    if (p+1<=end-1 && p[0]=='\r' && p[1]=='\n') return 2;
    return 0;
}

/*
 * In-place dechunker
 */
LONG http_dechunk_inplace(char* body, LONG body_len)
{
    if (!body || body_len < 0) return -10;

    char* dst = body;
    const char* src = body;
    const char* end = body + body_len;

    for (;;) {
        /* ---- parse chunk size line: <hex>[;ext]* CRLF ---- */
        unsigned long size = 0;
        int saw_digit = 0;

        /* read hex digits */
        while (src < end && is_hex(*src)) {
            size = (size << 4) | to_hex(*src);
            src++; saw_digit = 1;
        }
        if (!saw_digit) return -11;

        /* skip chunk extensions until end-of-line */
        while (src < end && *src != '\n') {
            /* allow anything up to EOL */
            src++;
        }
        if (src >= end) return -11; /* no EOL */
        /* consume EOL */
        int eollen = consume_line_end(src, end);
        if (!eollen) return -11;
        src += eollen;

        /* zero-size = final chunk (then optional trailers + final empty line) */
        if (size == 0) {
            /* Consume trailers until empty line */
            for (;;) {
                const char* line = src;
                /* find EOL */
                while (src < end && *src != '\n') src++;
                if (src >= end) return -14; /* unterminated trailer */
                /* empty line? allow CRLF or LF */
                if (line == src || (line+1==src && line[0]=='\r')) {
                    /* consume final EOL and finish */
                    int eol2 = consume_line_end(src, end);
                    if (!eol2) return -14;
                    src += eol2;
                    return (LONG)(dst - body); /* decoded length */
                }
                /* not empty: consume this trailer line's EOL and continue */
                int eol1 = consume_line_end(src, end);
                if (!eol1) return -14;
                src += eol1;
            }
        }

        /* ---- copy chunk data ---- */
        if ((const char*)src + (ptrdiff_t)size > end) return -12; /* truncated */
        if (dst != (char*)src) memmove(dst, src, (size_t)size);
        src += size; dst += size;

        /* ---- expect CRLF/LF after data ---- */
        int after = consume_line_end(src, end);
        if (!after) return -13;
        src += after;
    }
}

/*
 * Error Reporting Helper
 * Returns the last socket error code for detailed error reporting
 * Uses AmigaOS native Errno() function from bsdsocket.library
 */
LONG http_last_errno(void) { return g_http_last_errno; }

/*
 * Example usage:
 * 
 * char buffer[4096];
 * LONG bytes = http_get("example.com", 80, "/", buffer, sizeof(buffer));
 * if (bytes > 0) {
 *     printf("Got %ld bytes\n", bytes);
 *     // Process HTTP response in buffer (already NUL-terminated)
 * } else {
 *     printf("HTTP GET failed: %ld\n", bytes);
 *     // Check error codes: -1 to -8 for specific failures
 *     // Use http_last_errno() for detailed socket error information
 * }
 */
