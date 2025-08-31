/*
 * Test program for Amiga Networking Functions
 * Tests the minimal HTTP GET implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiga_networking.h"

int main(void)
{
    printf("Amiga Networking Test Program\n");
    printf("=============================\n\n");
    
    /* Test buffer */
    char buffer[4096];
    
    printf("Testing HTTP GET to httpbin.org...\n");
    
    /* Test with httpbin.org (reliable test endpoint) */
    LONG bytes = http_get("httpbin.org", 80, "/get", buffer, sizeof(buffer));
    
    if (bytes > 0) {
        printf("SUCCESS: Got %ld bytes\n", bytes);
        
        /* NUL-terminate buffer for safe string operations */
        buffer[bytes < sizeof(buffer) ? bytes : sizeof(buffer) - 1] = 0;
        
        printf("First 200 characters of response:\n");
        printf("--------------------------------\n");
        
        /* Show first 200 chars of response */
        int show_len = (bytes < 200) ? bytes : 200;
        for (int i = 0; i < show_len; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                printf("%c", buffer[i]);
            } else if (buffer[i] == '\n') {
                printf("\\n");
            } else if (buffer[i] == '\r') {
                printf("\\r");
            } else {
                printf("\\x%02x", (unsigned char)buffer[i]);
            }
        }
        printf("\n");
        
        /* Check if it looks like HTTP */
        if (strncmp(buffer, "HTTP/", 5) == 0) {
            printf("Response appears to be valid HTTP\n");
            
            /* Test HTTP response parser */
            char headers[1024];
            char body[3072];
            LONG body_len = http_parse_response(buffer, bytes, headers, sizeof(headers), body, sizeof(body));
            
            if (body_len >= 0) {
                printf("HTTP Parser: Headers (%ld bytes), Body (%ld bytes)\n", 
                       strlen(headers), body_len);
                printf("First line of headers: %.*s\n", 
                       strchr(headers, '\n') ? (int)(strchr(headers, '\n') - headers) : 50, headers);
                
                /* Test HTTP status & content-length parser */
                HttpInfo hi = http_parse_status_and_length(headers);
                printf("HTTP Info: Status=%ld, Content-Length=%ld, Chunked=%s\n",
                       hi.status, hi.content_length, hi.chunked ? "yes" : "no");
                
                /* Handle chunked encoding if present */
                if (hi.chunked) {
                    LONG dec = http_dechunk_inplace(body, body_len);
                    if (dec >= 0) {
                        body_len = dec;
                        printf("Dechunked: %ld bytes (was %ld)\n", body_len, dec);
                        /* body[0..body_len-1] is now the decoded payload */
                    } else {
                        printf("Dechunk failed: %ld\n", dec);
                    }
                } else if (hi.content_length >= 0 && body_len > hi.content_length) {
                    /* Clip to Content-Length if server kept connection open */
                    body_len = hi.content_length;
                    printf("Clipped to Content-Length: %ld bytes\n", body_len);
                }
            } else {
                printf("HTTP Parser failed: %ld\n", body_len);
            }
        } else {
            printf("Response doesn't look like HTTP\n");
        }
        
    } else {
        printf("FAILED: HTTP GET returned %ld\n", bytes);
        
        switch (bytes) {
            case HTTP_ERR_OPENLIB:
                printf("Error: Failed to open bsdsocket.library\n");
                break;
            case HTTP_ERR_SOCKET:
                printf("Error: Failed to create socket\n");
                break;
            case HTTP_ERR_RESOLVE:
                printf("Error: Failed to resolve hostname\n");
                break;
            case HTTP_ERR_CONNECT:
                printf("Error: Failed to connect\n");
                break;
            case HTTP_ERR_BADBUF:
                printf("Error: Invalid buffer parameters\n");
                break;
            case HTTP_ERR_FORMAT:
                printf("Error: Request formatting error\n");
                break;
            case HTTP_ERR_PARTIALSEND:
                printf("Error: Partial send / write error\n");
                break;
            case HTTP_ERR_RECV:
                printf("Error: Receive timeout / read error\n");
                break;
            default:
                printf("Unknown error code\n");
                break;
        }
        
        printf("\nMake sure:\n");
        printf("1. bsdsocket.library is available\n");
        printf("2. Network connection is active\n");
        printf("3. TCP/IP stack is running\n");
        
        /* Show detailed error if available */
        LONG err = http_last_errno();
        if (err > 0) {
            printf("Socket error code: %ld\n", err);
        }
    }
    
    printf("\nTest completed.\n");
    return (bytes > 0) ? 0 : 1;
}
