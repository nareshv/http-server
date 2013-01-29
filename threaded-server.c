/**
 * @file   threaded-server.c
 * @Author Naresh Kumar
 * @date   January, 2013
 * @brief  Multi-threaded Simple HTTP Server
 *
 * HTTP Compliant Multi-Threaded server which demonstrates
 * simple static content serving.
 *
 * This is the fastest webserver available.
 * Run benchmarks and see for yourself.
 *
 * (c) 2013, Naresh Kumar. Code is available under MIT License.
 */

#include <stdio.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>         /* socket() */
#include <string.h>             /* memset() */
#include <stdlib.h>             /* EXIT_FAILURE */
#include <errno.h>
#include <unistd.h>             /* close() */
#include <sys/sendfile.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <libgen.h>

#define LISTEN_BACKLOG  100     /* backlog of connections */
#define LISTEN_PORT     8080    /* port to listen on */
#define LOG_INFO        "info"  /* log level */
#define LOG_ERROR       "error" /* log level */
#define HTTP_MAX_HLEN   4096    /* max header length */
#define HTTP_MAX_MLEN   32      /* max method length */
#define HTTP_MAX_ULEN   256     /* max url length */
#define HTTP_MAX_PLEN   32      /* max version length */

/* Global configuration structure */
struct {
        char serverRoot[PATH_MAX];
        char indexFile[PATH_MAX];
        int serveIndexFileInDirectory;
        int port;
} ServerConfiguration;

/*
 * Return the size of the given file in Bytes.
 *
 * Return Codes:
 * > 0 ; if file is regular file
 * -1  ; if lstat encountered any error
 * -2  ; if requested file is directory (needed for directory indexing)
 *
 */
int getFileSize(const char *path)
{
        struct stat fileinfo;
        if (lstat(path, &fileinfo) != -1) {
                if (S_ISREG(fileinfo.st_mode)) {
                        return fileinfo.st_size;
                } else if (S_ISDIR(fileinfo.st_mode)) {
                        return -2;  // directory
                }
        }
        return -1;              // error
}

/*
 * Determine if given path is directory or not
 *
 * Return Codes
 * 1 ; if directory and exists on system
 * 0 ; for everything else
 *
 */
int isDirectory(const char *path)
{
        struct stat fileinfo;
        if (lstat(path, &fileinfo) != -1) {
                if (S_ISDIR(fileinfo.st_mode)) {
                        return 1;   // directory
                }
        }
        return 0;               // error
}

/*
 * Transfer the given file to the client
 *
 * Return Codes:
 * > 0 ; Number of bytes sent to client
 * -1  ; error from sendfile syscall
 *
 */
int transferFile(const char *file, int out_fd)
{
        char filename[PATH_MAX];
        int size = getFileSize(file);
        int in_fd;
        int offset = 0;
        ssize_t sentbytes = 0;
        char headers[HTTP_MAX_HLEN];
        if (size > 0) {
                /* file exists and is regular file */
                in_fd = open(file, O_RDONLY, 0);
                if (in_fd != -1) {
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 200 OK\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Content-Length: %d\r\n", size);
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", "NareshWebServer");
                        write(out_fd, headers, strlen(headers));
                        sentbytes = sendfile(out_fd, in_fd, NULL, size);
                        if (sentbytes == -1) {
                                perror("sendfile()");
                        }
                } else {
                        /* open failed (due to num-open files limit reached ?) */
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 503 Service Unavailable\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", "NareshWebServer");
                        write(out_fd, headers, strlen(headers));
                }
        } else {
                if (size == -2) {
                        /* in case we want to serve the index file in the directory */
                        if (ServerConfiguration.serveIndexFileInDirectory == 1) {
                                snprintf(filename, PATH_MAX, "%s/%s", file, ServerConfiguration.indexFile);
                                fprintf(stderr, "[%s] going to serve the index file %s\n", LOG_INFO, filename);
                                int size = getFileSize(filename);
                                if (size > 0) {
                                        /* check if the indexFile exists and serve it */
                                        return transferFile(filename, out_fd);
                                } else {
                                        /* send a 404 */
                                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 404 Not Found\r\n");
                                        offset = strlen(headers);
                                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", "NareshWebServer");
                                        write(out_fd, headers, strlen(headers));
                                }
                        }
                        /* directory listing denied */
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 403 Forbidden\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", "NareshWebServer");
                        write(out_fd, headers, strlen(headers));
                } else {
                        /* stat syscall failed */
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 404 Not Found\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", "NareshWebServer");
                        write(out_fd, headers, strlen(headers));
                }
        }
        return sentbytes;
}

/*
 * return the current time as string
 */
void getHTTPTime(time_t t, char *ltime)
{
        time_t curtime = time(NULL);
        struct tm loctime;
        localtime_r(&curtime, &loctime);
        asctime_r(&loctime, ltime);
        ltime[strlen(ltime) - 1] = '\0';    // consume the newline
}

/*
 * Take care of one incoming http request from the client.
 * This involves
 *      1. Quick read of headers
 *      2. Validate HTTP Method
 *      3. Lookup and Send File
 *
 */
void handleHTTPRequest(void *clientfd)
{
        int fd = *(int *)clientfd;
        char headers[HTTP_MAX_HLEN];
        char http_method[HTTP_MAX_MLEN];
        char http_url[HTTP_MAX_ULEN];
        char http_proto[HTTP_MAX_PLEN];
        char file_path[PATH_MAX];
        ssize_t rbytes = 0;
        ssize_t toread = HTTP_MAX_HLEN;
        ssize_t offset = 0;

        /*
         * fast read the headers.
         * Fast read does work for header size > 4095 (ie huge cookies)
         */
        rbytes = recv(fd, headers + offset, toread, 0);

        if (rbytes == -1) {
                perror("recv()");
        } else {
                offset = offset + rbytes;
                toread = HTTP_MAX_HLEN - offset;
                sscanf(headers, "%s %s %s", http_method, http_url, http_proto);
                if (strcmp(http_method, "GET") == 0) {
                        /* send the file to browser */
                        snprintf(file_path, PATH_MAX, "%s/%s", ServerConfiguration.serverRoot, http_url);
                        rbytes = transferFile(file_path, fd);
                        /* for logging to the stderr */
                        char ltime[27];
                        getHTTPTime(time(NULL), ltime);
                        fprintf(stderr, "[%s] %s %s %s %ld\n", ltime, http_proto, http_method, http_url, rbytes);
                } else {
                        /* we only support GET request */
                        snprintf(headers, HTTP_MAX_HLEN, "%s", "HTTP/1.1 405 Method Not Allowed\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", "NareshWebServer");
                        write(fd, headers, strlen(headers));
                }
        }
        close(fd);
}

/*
 *
 * Show the help for the program
 *
 */
void showHelp(char **argv)
{
        fprintf(stderr, "%s [port] [path/to/directory] [indexFile]\n", argv[0]);
        exit(EXIT_FAILURE);
}

/* This is where the program execution starts at runtime */
int main(int argc, char **argv)
{
        int sock;
        struct sockaddr_in saddr;
        struct sockaddr_storage client_addr;
        socklen_t client_addr_size;
        int port;

        /* basic data validation */
        if (argc < 4) {
                showHelp(argv);
        }

        port = atoi(argv[1]);

        if (port > 65536 || port < 0) {
                fprintf(stderr, "[%s] Please give correct port number (between 1 and 65536).\n", LOG_ERROR);
                return EXIT_FAILURE;
        } else {
                ServerConfiguration.port = port;
        }

        if (isDirectory(argv[2]) != 1) {
                fprintf(stderr, "[%s] Please give a directory which needs to be served via HTTP.\n", LOG_ERROR);
                return EXIT_FAILURE;
        } else {
                snprintf(ServerConfiguration.serverRoot, PATH_MAX, "%s", argv[2]);
        }

        snprintf(ServerConfiguration.indexFile, PATH_MAX, "%s", argv[3]);
        ServerConfiguration.serveIndexFileInDirectory = 1;

        /* server socket */
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
                perror("socket()");
                return EXIT_FAILURE;
        }

        /* make sure we can re-use (while development) its needed */
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&sock, sizeof(int)) == -1) {
                perror("setsockopt()");
                return EXIT_FAILURE;
        }
#ifdef TCP_DEFER_ACCEPT
        /* defer accept until data is available */
        if (setsockopt(sock, IPPROTO_TCP, TCP_DEFER_ACCEPT, (char *)&sock, sizeof(int)) == -1) {
                perror("setsockopt()");
                return EXIT_FAILURE;
        }
#endif

        /* make sure structure is emptied correctly. */
        memset(&saddr, sizeof(struct sockaddr_in), '\0');

        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(LISTEN_PORT);
        saddr.sin_addr.s_addr = htonl(INADDR_ANY);

        /* tell the kernel that we are interested in given ip/port for connections on given socket */
        if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
                perror("bind()");
                return EXIT_FAILURE;
        } else {
                fprintf(stderr, "[%s] Started listening on %d\n", LOG_INFO, LISTEN_PORT);
        }

        /* let kernel queue the connections which are done with TCP 3-way handshake */
        if (listen(sock, LISTEN_BACKLOG) == -1) {
                perror("bind()");
                return EXIT_FAILURE;
        } else {
                fprintf(stderr, "[%s] Created backlog queue of size %d\n", LOG_INFO, LISTEN_BACKLOG);
        }

        /* in this simple example, wait forever */
        while (1) {
                memset(&client_addr, sizeof(struct sockaddr_storage), '\0');
                pthread_t client_thread;
                int clientfd = accept(sock, (struct sockaddr *)&client_addr, &client_addr_size);
                if (clientfd == -1) {
                        perror("accept()");
                        break;
                }
                /*
                 * now we are stressing the system to an extent that thread creation should be a problem to handle C10K
                 * connections!
                 **/
                pthread_create(&client_thread, NULL, (void *)&handleHTTPRequest, (void *)&clientfd);
                pthread_join(client_thread, NULL);
        }

        /* pretend everything went great */
        return EXIT_SUCCESS;
}
