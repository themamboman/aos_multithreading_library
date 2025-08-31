/*
 * Amiga Networking Header for WebKit Integration
 * Declares minimal HTTP GET functionality
 */

#ifndef AMIGA_NETWORKING_H
#define AMIGA_NETWORKING_H

#include <exec/types.h>
#include <exec/libraries.h>

/*
 * Global socket library base (shared across translation units)
 * All bsdsocket calls require this to be initialized
 * 
 * CONCURRENCY WARNING: This is a single global symbol. In AmigaOS, each task
 * should have its own bsdsocket base. Sharing a single global across multiple
 * Exec tasks can race if two threads use sockets at once.
 * 
 * For first milestone (sync loads on one task): You're fine.
 * For multi-threaded: Either centralize all socket I/O on one "network" thread
 * and post completions via MyRunLoopPost, or wrap bsdsocket entry points to
 * pull the base from TLS instead of this global.
 */
extern struct Library* SocketBase;

/* HTTP error codes - for cleaner error handling */
#define HTTP_ERR_OPENLIB     (-1)  /* Failed to open bsdsocket.library */
#define HTTP_ERR_SOCKET      (-2)  /* Failed to create socket */
#define HTTP_ERR_RESOLVE     (-3)  /* Failed to resolve hostname */
#define HTTP_ERR_CONNECT     (-4)  /* Failed to connect */
#define HTTP_ERR_BADBUF      (-5)  /* Invalid buffer parameters */
#define HTTP_ERR_FORMAT      (-6)  /* Request formatting error */
#define HTTP_ERR_PARTIALSEND (-7)  /* Partial send / write error */
#define HTTP_ERR_RECV        (-8)  /* Receive timeout / read error */

/*
 * Minimal blocking HTTP GET using bsdsocket.library
 * 
 * Parameters:
 *   host: Target hostname (e.g., "example.com")
 *   port: Target port (typically 80 for HTTP)
 *   path: Request path (e.g., "/" or "/index.html")
 *   out: Output buffer for response data
 *   outCap: Capacity of output buffer
 * 
 * Returns:
 *   >0: Number of bytes read into output buffer
 *   HTTP_ERR_OPENLIB: Failed to open bsdsocket.library
 *   HTTP_ERR_SOCKET: Failed to create socket
 *   HTTP_ERR_RESOLVE: Failed to resolve hostname
 *   HTTP_ERR_CONNECT: Failed to connect
 *   HTTP_ERR_BADBUF: Invalid buffer parameters
 *   HTTP_ERR_FORMAT: Request formatting error
 *   HTTP_ERR_PARTIALSEND: Partial send / write error
 *   HTTP_ERR_RECV: Receive timeout / read error
 * 
 * Features:
 * - HTTP/1.1 protocol with chunked transfer encoding support
 * - Automatic chunked body dechunking via http_dechunk_inplace()
 * - Accept-Encoding: identity to avoid compression
 * - Connection: close for simple EOF handling
 * 
 * Note: This is synchronous/blocking. For production use,
 * consider making it async by posting completions to MyRunLoop.
 */
LONG http_get(const char* host, UWORD port, const char* path, char* out, LONG outCap);

/*
 * HTTP Response Parser Helper
 * Splits HTTP response into headers and body for quick resource decoding
 * 
 * Parameters:
 *   response: Raw HTTP response buffer
 *   response_len: Length of response data
 *   headers: Output buffer for headers (NUL-terminated)
 *   headers_cap: Capacity of headers buffer
 *   body: Output buffer for body data
 *   body_cap: Capacity of body buffer
 * 
 * Returns:
 *   >0: Body length on success
 *   0: Headers only (no body)
 *   -1: Invalid response format
 *   -2: Buffer too small for headers
 *   -3: Buffer too small for body
 */
LONG http_parse_response(const char* response, LONG response_len,
                        char* headers, LONG headers_cap,
                        char* body, LONG body_cap);

/*
 * HTTP Status & Content-Length Parser Helper
 * Parses common HTTP response information for quick resource decoding
 * 
 * Returns:
 *   HttpInfo struct with status code, content length, and chunked flag
 */
typedef struct HttpInfo {
    LONG status;            /* e.g. 200, 404; -1 if not found */
    LONG content_length;    /* -1 if absent */
    BOOL chunked;           /* TRUE if Transfer-Encoding: chunked */
} HttpInfo;

HttpInfo http_parse_status_and_length(const char* headers);

/*
 * In-place HTTP/1.1 chunked body decoder.
 * Input:  body buffer (exact chunked body from after header CRLF)
 * Output: returns >=0 decoded length, or a negative error:
 *   -10: invalid args
 *   -11: malformed chunk size line
 *   -12: truncated chunk data
 *   -13: missing CRLF after chunk data
 *   -14: malformed or truncated trailers
 */
LONG http_dechunk_inplace(char* body, LONG body_len);

/*
 * Error Reporting Helper (Optional)
 * Returns the last socket error code for detailed error reporting
 * Useful when mapping to WebKit's ResourceError
 * 
 * Returns:
 *   Socket error code (e.g., EWOULDBLOCK, ETIMEDOUT, etc.)
 *   -1 if errno not available in this SDK
 */
LONG http_last_errno(void);

#endif /* AMIGA_NETWORKING_H */
