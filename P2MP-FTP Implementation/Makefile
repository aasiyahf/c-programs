# This Makefile takes the words server and client as command
# arguments and uses them to compile the respective programs.
# @author Aasiyah Feisal (anfeisal)

# To compile both the client and server programs, type:
#   $ make
# and then to run the server program, type:
#   $ ./p2mpserver <port> <filename> <packet loss probability>
# and then to run the client program, type:
#   $ ./p2mpclient <server-1 hostname> [server-n hostname...] <server port> <filename> <MSS>

all: p2mpclient p2mpserver

client: p2mpclient.c
	gcc -std=c99 -o p2mpclient p2mpclient.c

server: p2mpserver.c
	gcc -std=c99 -o p2mpserver p2mpserver.c
