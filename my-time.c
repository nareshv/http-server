#include <time.h>
#include <string.h>
#include <stdio.h>

char *httpHeaderTime(time_t secs, char *outstring, size_t buflen)
{
        struct tm t;

        gmtime_r(&secs, &t);
        strftime(outstring, buflen, "%a, %d %b %Y %T %Z", &t);

        return outstring;
}

#ifndef _SHLIB_
int main(int argc, char **argv)
{
        //size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
        char outstring[30];

        printf("%s\n", httpHeaderTime(time(NULL), outstring, 30));
        return 0;
}
#endif
