# Fast and Reliable File Transfer Protocol

This file transfer protocol uses multi-thread to reliably transfer files from server to client over UDP, optimizing for performance under high-congestion and high-loss conditions.

## Instructions

Notice: start the server first.

server.cpp:

- Description:
- Compile: `g++ -o <targer_file_name> server.cpp`
- Run: `./<target_file_name> <file_to_transfer> <host_port>`
- Example:

  `g++ -o server server.cpp`

  `./server sent.txt 8888`

client.cpp:

- Description:
- Compile: `g++ -o <target_file_name> client.cpp`
- Run: `./<target_file_name> <path_to_store_file> <hostname> <host_port>`
- Example:

  `g++ -o client client.cpp`

  `./client ./received.txt 127.0.0.1 8888`
