#include <stdio.h>
#include <string.h>

/*
 * Extract the path and query parameters from the url
 *
 * return void
 *
 */
void extractURLDetails(const char *url, char *path, char *qs) {
    int l = strlen(url);
    int i = 0;
    int p = 0;
    int off = 0;
    char qsep = '?';
    for (i = 0; i < l; i++) {
        if (url[i] == qsep) {
            p = 1;
            path[off] = '\0';
            off = 0;
            continue;
        }
        if (p == 0) {
            path[off] = url[i];
        } else {
            qs[off] = url[i];
        }
        off++;
    }
    if (p == 0) {
        path[off] = '\0';
    } else {
        qs[off] = '\0';
    }
}

#ifndef _SHLIB_
int main() {
    //char *url = "/hello/world?q=world";
    //char *url = "/hello/world";
    char *url = "/hello/world?";
    char path[1024];
    char qs[1024];

    extractURLDetails(url, path, qs);

    printf("path = '%s', query string = '%s'\n", path, qs);
}
#endif
