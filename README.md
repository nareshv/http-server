
Very light weight static content serving web-server written in C. Uses pthreads to achieve high concurrency.

On a 4 core system with ab proved to serve 10K reqs/sec with minimal cpu usage.

---------
Examples:
---------

1. To compile the code

bash-$ make build

2. Run the webserver on a port

./server PORT WEBSERVERFOLDER

bash-$ ./server 8080 /var/www/html/

-------------
Benchmarking:
-------------

make benchmark will run the benchmark of 10K requests with 100 concurrency.

---------------
Benchmark Output
----------------
