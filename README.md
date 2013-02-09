http-server:
------------

Very light weight static content serving web-server written in C. Uses pthreads to achieve high concurrency.

On a 4 core system with ab proved to serve 10K reqs/sec with minimal cpu usage.

Examples:
---------

1. To compile the code

bash-$ make build

2. Run the webserver on a port

./server -p <port> -r <webroot> -i <indexfile>

3. Example

bash-$ ./server -p 8080 -r /var/www/html/ -i index.html
