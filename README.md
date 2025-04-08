# REVERSE PROXY FILE-SHARING
TODO: fix file uploads for images, better error treatment, friendly UI, deploy

Simple reverse proxy file-sharing service written in C using sockets\
Main source of knowledge: https://beej.us/guide/bgnet/html/split/\
And some stack overflow threads + youtube videos\
Helped in listing files inside directory: https://stackoverflow.com/questions/4204666/how-to-list-files-in-a-directory-in-a-c-program

## Reverse Proxy
A reverse proxy is an intermediary server that acts as a 'bridge' between the client and the proxy server. To the client this will seem like any old regular web server, but reverse proxy increases scalability, security and performance compared to a straight access from the user to the proxy server. This implementation is quite simple, only connecting the client to one backend server and allowing them to get files.

## Implementation
This implementation only works on UNIX systems, seeing as it uses the <sys/socket.h> Berkley Sockets implementation. We use sockets to connect the three parts of the system (the client, the reverse proxy service, and the backend) together in a simple and readable way. We have two sockets, one that talks to the client and one that talks to the backend. The reverse proxy serves as a middleman that manages the communication between both sides, receiving data from one socket (client or backend) and redirecting it to the other

Simple schematic of how the system works:
![Reverse Proxy Schematic](./www/revproxy.drawio.png)
<sup>Made with draw.io</sup>

## How to run
First, compile the backend and revproxy files:

`make`\

Then, run both at the same time\
`./backend`\
`./revproxy`

## Available endpoints

### GET a file hosted in the server:
Example:\
`curl http://localhost:4950/test.txt`
Will return:\
`Hello world!`

Example 2:\
`curl http://localhost:4950/revproxy.drawio.png --output diagram.png`\
Will download the diagram shown above

### GET a list of all files available in the server:
Example:\
`curl http://localhost:4950/list`\
Will return a list of all available files

### POST send a file to the server:
`curl -F "file=@test.txt" http://localhost:4950/upload`\
Will send test.txt and save it in the server\
Obs: Doesn't support all file formats yet, only tested throughly with text files
