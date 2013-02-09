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

#define _D_GNU_SOURCE           /* for strcasestr() */

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
#include <search.h>             /* for hashtable of file extensions */
#include <getopt.h>

#include "my-time.h"

#define LISTEN_BACKLOG  100     /* backlog of connections */
#define LISTEN_PORT     8080    /* port to listen on */
#define HTTP_MAX_HLEN   4096    /* max header length */
#define HTTP_MAX_MLEN   32      /* max method length */
#define HTTP_MAX_ULEN   256     /* max url length */
#define HTTP_MAX_PLEN   32      /* max version length */
#define CONF_MAX_LEN    32      /* max length of config value */
#define CONTENT_YES     1       /* send the content in the response  */
#define CONTENT_NO      0       /* no not send the content in the response */

#define LOG_DEBUG(format, ...) fprintf(stderr, "[%s] " format "\n", "debug", __VA_ARGS__)
#define LOG_INFO(format, ...)  fprintf(stderr, "[%s] " format "\n", "info", __VA_ARGS__)
#define LOG_ERROR(format, ...) fprintf(stderr, "[%s] " format "\n", "error", __VA_ARGS__)
#define LOG_FATAL(format, ...) fprintf(stderr, "[%s] " format "\n", "fatal", __VA_ARGS__)

#define HTTP_BODY_400        "<!doctype html><html><head><meta charset='utf-8'><title>400</title></head><body style='background-color:#9800cf;color:#fff;'>"\
                             "<h1>400 - Bad Request</h1><hr style='border: 1px solid #fff; height: 0'></body></html>"
#define HTTP_BODY_403        "<!doctype html><html><head><meta charset='utf-8'><title>403</title></head><body style='background-color:#0098cf;color:#fff;'>"\
                             "<h1>404 - Forbidden</h1><hr style='border: 1px solid #fff; height: 0'></body></html>"
#define HTTP_BODY_404        "<!doctype html><html><head><meta charset='utf-8'><title>404</title></head><body style='background-color:#0098cf;color:#fff;'>"\
                             "<h1>404 - Page Not Found</h1><hr style='border: 1px solid #fff; height: 0'></body></html>"
#define HTTP_BODY_405        "<!doctype html><html><head><meta charset='utf-8'><title>405</title></head><body style='background-color:#0098cf;color:#fff;'>"\
                             "<h1>405 - Method Not Allowed<h1><hr style='border: 1px solid #fff; height: 0'></body></html>"
#define HTTP_BODY_503        "<!doctype html><html><head><meta charset='utf-8'><title>503</title></head><body style='background-color:#cf9800;color:#fff;'>"\
                             "<h1>503 - Service Unavailable</h1><hr style='border: 1px solid #fff; height: 0'></body></html>"


/* Global configuration structure */
struct {
        char serverRoot[PATH_MAX];
        char indexFile[PATH_MAX];
        char serverName[CONF_MAX_LEN];
        int serveIndexFileInDirectory;
        int numFileTypes;
        short chroot;
        uid_t app_uid;
        gid_t app_gid;
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

int getFileLastModTime(const char *path)
{
        struct stat fileinfo;
        if (lstat(path, &fileinfo) != -1) {
                return fileinfo.st_mtime;
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
int transferFile(const char *file, int out_fd, int nocontent)
{
        char filename[PATH_MAX];
        int size = getFileSize(file);
        int in_fd;
        int offset = 0;
        ssize_t sentbytes = 0;
        char headers[HTTP_MAX_HLEN];
        char lastmodbuffer[30];
        time_t lastmodtime;
        if (size > 0) {
                lastmodtime = getFileLastModTime(file);
                /* file exists and is regular file */
                in_fd = open(file, O_RDONLY, 0);
                if (in_fd != -1) {
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 200 OK\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Date: %s\r\n", httpHeaderTime(time(NULL), lastmodbuffer, 30));
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Last-Modified: %s\r\n", httpHeaderTime(lastmodtime, lastmodbuffer, 30));
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Content-Length: %d\r\n", size);
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                        write(out_fd, headers, strlen(headers));
                        if (nocontent == CONTENT_YES) {
                                sentbytes = sendfile(out_fd, in_fd, NULL, size);
                                if (sentbytes == -1) {
                                        perror("sendfile()");
                                }
                        }
                        close(in_fd);
                } else {
                        /* open failed (due to num-open files limit reached ?) */
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 503 Service Unavailable\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "Connection: close\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                        write(out_fd, headers, strlen(headers));
                        /* send the body content as well */
                        write(out_fd, HTTP_BODY_503, strlen(HTTP_BODY_503));
                }
        } else {
                if (size == -2) {
                        /* in case we want to serve the index file in the directory */
                        if (ServerConfiguration.serveIndexFileInDirectory == 1) {
                                snprintf(filename, PATH_MAX, "%s/%s", file, ServerConfiguration.indexFile);
                                LOG_DEBUG("Serving the index file : %s", filename);
                                int size = getFileSize(filename);
                                if (size > 0) {
                                        /* check if the indexFile exists and serve it */
                                        return transferFile(filename, out_fd, nocontent);
                                } else {
                                        /* send a 404 */
                                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 404 Not Found\r\n");
                                        offset = strlen(headers);
                                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "Connection: close\r\n");
                                        offset = strlen(headers);
                                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                                        write(out_fd, headers, strlen(headers));
                                        /* send the body content as well */
                                        write(out_fd, HTTP_BODY_404, strlen(HTTP_BODY_404));
                                }
                        } else {
                                /* directory listing denied */
                                snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 403 Forbidden\r\n");
                                offset = strlen(headers);
                                snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "Connection: close\r\n");
                                offset = strlen(headers);
                                snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                                write(out_fd, headers, strlen(headers));
                                /* send the body content as well */
                                write(out_fd, HTTP_BODY_403, strlen(HTTP_BODY_403));
                        }
                } else {
                        /* stat syscall failed */
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "HTTP/1.1 404 Not Found\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "Connection: close\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                        write(out_fd, headers, strlen(headers));
                        /* send the body content as well */
                        write(out_fd, HTTP_BODY_404, strlen(HTTP_BODY_404));
                }
        }
        return sentbytes;
}

/*
 * return the current time as string
 */
void getLogTime(time_t t, char *ltime)
{
        time_t curtime = time(NULL);
        struct tm loctime;
        localtime_r(&curtime, &loctime);
        asctime_r(&loctime, ltime);
        ltime[strlen(ltime) - 1] = '\0';    // consume the extra newline
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
        char *host_hdr;
        char http_host[HOST_NAME_MAX];
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
                /* read the host header first for http/1.1 requests */
                if ((host_hdr = strcasestr((const char *)headers, (const char *)"host:")) == NULL) {
                        /* make sure that host header is present in the request (for virtual hosting) */
                        snprintf(headers, HTTP_MAX_HLEN, "%s", "HTTP/1.1 400 Bad Request\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "Connection: close\r\n");
                        offset = strlen(headers);
                        snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                        write(fd, headers, strlen(headers));
                        /* send the body content as well */
                        write(fd, HTTP_BODY_400, strlen(HTTP_BODY_400));
                } else {
                        /* extract host header */
                        sscanf(host_hdr + strlen("host:"), "%63s", http_host);
                        LOG_DEBUG("Extracted host header: %s", http_host);
                        /* read the type of request */
                        offset = offset + rbytes;
                        toread = HTTP_MAX_HLEN - offset;
                        rbytes = 0;
                        sscanf(headers, "%s %s %s", http_method, http_url, http_proto);
                        if (strcmp(http_method, "GET") == 0) {
                                /* send the file to browser */
                                snprintf(file_path, PATH_MAX, "%s/%s", ServerConfiguration.serverRoot, http_url);
                                rbytes = transferFile(file_path, fd, CONTENT_YES);
                                /* for logging to the stderr */
                                char ltime[27];
                                getLogTime(time(NULL), ltime);
                                fprintf(stderr, "[%s] %s %s %s %zd\n", ltime, http_proto, http_method, http_url, rbytes);
                        } else if (strcmp(http_method, "HEAD") == 0) {
                                /* send the information about file to browser */
                                snprintf(file_path, PATH_MAX, "%s/%s", ServerConfiguration.serverRoot, http_url);
                                rbytes = transferFile(file_path, fd, CONTENT_NO);
                                /* for logging to the stderr */
                                char ltime[27];
                                getLogTime(time(NULL), ltime);
                                fprintf(stderr, "[%s] %s %s %s %zd\n", ltime, http_proto, http_method, http_url, rbytes);
                        } else {
                                /* we only support GET/HEAD request */
                                snprintf(headers, HTTP_MAX_HLEN, "%s", "HTTP/1.1 405 Method Not Allowed\r\n");
                                offset = strlen(headers);
                                snprintf(headers + offset, HTTP_MAX_HLEN, "%s", "Connection: close\r\n");
                                offset = strlen(headers);
                                snprintf(headers + offset, HTTP_MAX_HLEN, "Server: %s\r\n\r\n", ServerConfiguration.serverName);
                                write(fd, headers, strlen(headers));
                                /* send the body content as well */
                                rbytes = strlen(HTTP_BODY_405);
                                write(fd, HTTP_BODY_405, rbytes);
                                /* for logging to the stderr */
                                char ltime[27];
                                getLogTime(time(NULL), ltime);
                                fprintf(stderr, "[%s] %s %s %s %zd\n", ltime, http_proto, http_method, http_url, rbytes);
                        }
                }
        }
        close(fd);
}

int dropRootPrivileges(uid_t uid, gid_t gid)
{
        if (getuid() == 0) {
                /* process is running as root, drop privileges */
                if (setgid(gid) != 0) {
                        perror("setgid()");
                        return -1;
                }
                if (setuid(uid) != 0) {
                        perror("setuid()");
                        return -1;
                }
        }
        if (setuid(0) != -1) {
                exit(EXIT_FAILURE);
                return -1;
        }
        return 0;
}

/*
 *
 * Show the help for the program
 *
 */
void showHelp(char **argv)
{
        //fprintf(stderr, "usage: %s [port] [path/to/directory] [indexFile]\n", argv[0]);
        fprintf(stderr, "usage: %s -p <port> -r <webroot> -i <indexFile>\n", argv[0]);
        exit(EXIT_FAILURE);
}

void processArguments(int argc, char **argv)
{
        char *webroot = NULL, *indexfile = NULL;
        int option = 0;
        int port = -1;

        while ((option = getopt(argc, argv, "p:r:i:u:g:")) != -1) {
                switch (option) {
                case 'p':
                        port = atoi(optarg);
                        if (port > 65536 || port < 0) {
                                LOG_ERROR("%s", "Please give correct port number (between 1 and 65536)");
                                exit(EXIT_FAILURE);
                        } else {
                                ServerConfiguration.port = port;
                        }
                        break;
                case 'r':
                        webroot = optarg;
                        if (isDirectory(webroot) != 1) {
                                LOG_ERROR("%s", "Please give a directory which needs to be served via HTTP.");
                                exit(EXIT_FAILURE);
                        } else {
                                snprintf(ServerConfiguration.serverRoot, PATH_MAX, "%s", webroot);
                        }
                        break;
                case 'i':
                        indexfile = optarg;
                        snprintf(ServerConfiguration.indexFile, PATH_MAX, "%s", indexfile);
                        break;
                case 'u':
                        ServerConfiguration.app_uid = atoi(optarg);
                        break;
                case 'g':
                        ServerConfiguration.app_gid = atoi(optarg);
                        break;
                default:
                        showHelp(argv);
                        break;
                }
        }

        /* basic data validation */
        if (port == -1 || webroot == NULL || indexfile == NULL) {
                showHelp(argv);
        }

        /* update the server configuration structure */
        snprintf(ServerConfiguration.serverName, CONF_MAX_LEN, "%s", "Route5/1.0");
        ServerConfiguration.serveIndexFileInDirectory = 1;
        ServerConfiguration.app_uid = 1000;
        ServerConfiguration.app_gid = 1000;

}

void doChroot()
{
        /* do a chroot to the root dir */
        if (chroot(ServerConfiguration.serverRoot) == -1) {
                perror("chroot()");
                exit(EXIT_FAILURE);
        }

        /* switch to the root of the filesystem */
        sprintf(ServerConfiguration.serverRoot, "%s", "/");
        if (chdir(ServerConfiguration.serverRoot) == -1) {
                perror("chdir()");
                exit(EXIT_FAILURE);
        }
}

/* This is where the program execution starts at runtime */
int main(int argc, char **argv)
{
        int sock;
        struct sockaddr_in saddr;
        struct sockaddr_storage client_addr;
        socklen_t client_addr_size;

        /* process cmdline arguments */
        processArguments(argc, argv);

        /* do chroot if needed */
        //doChroot();

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
                LOG_INFO("Started listening on %d", LISTEN_PORT);
        }

        /* let kernel queue the connections which are done with TCP 3-way handshake */
        if (listen(sock, LISTEN_BACKLOG) == -1) {
                perror("bind()");
                return EXIT_FAILURE;
        } else {
                LOG_INFO("Created backlog queue of size %d", LISTEN_BACKLOG);
        }

        /* drop the root privileges */
        dropRootPrivileges(1000, 1000);

        /* in this simple example, wait forever */
        while (1) {
                memset(&client_addr, sizeof(struct sockaddr_storage), '\0');
                pthread_t client_thread;
                client_addr_size = sizeof(struct sockaddr_storage);
                int clientfd = accept(sock, (struct sockaddr *)&client_addr, &client_addr_size);
                if (clientfd == -1) {
                        perror("accept()");
                        break;
                }
                /*
                 * now we are stressing the system to an extent that thread creation should be a problem to handle C10K
                 * connections!
                 **/
                if (0 == pthread_create(&client_thread, NULL, (void *)&handleHTTPRequest, (void *)&clientfd)) {
                        pthread_join(client_thread, NULL);
                } else {
                        LOG_FATAL("%s", "Cannot handle the current connection. Closing it.");
                        close(clientfd);    // in case we do not have additional capacity. handle at level-3, rather than at layer-7
                }
        }

        /* pretend everything went great */
        return EXIT_SUCCESS;
}
