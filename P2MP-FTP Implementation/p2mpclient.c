/*
 * The P2MP-FTP client implements the sender in the reliable data transfer.
 * When the client starts, it reads data from a file specified in the command
 * line arguments.
 *
 * This client uses udp to transfer the data to the P2MP-FTP servers using a
 * stop-and-wait ARQ.
 *
 * Run as:
 * ./p2mpclient <server-1 hostname> [server-n hostname...] <server port> <filename> <MSS>
 *
 * Author: Aasiyah Feisal (anfeisal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DATA_PKT 0b0101010101010101
#define ACK_PKT  0b1010101010101010
#define TIMEOUT_SEC 0
#define TIMEOUT_USEC 120000
#define MAX_SERVERS 10
#define MAX_MSS 1024
#define INVALID_SEQ_NO -1

/*
 * Header structure which contains a 32-bit sequence number, a 16-bit
 * checksum of the data part being sent, and a 16-bit field which
 * determines the type of the packet sent (data packet vs. ack packet)
 */
typedef struct header_t {
  int32_t seqNum;
  int16_t checksum;
  int16_t type;
} Header;

/*
 * Packet structure which contains header information and a buffer
 * array that holds data to be sent to server
 */
typedef struct packet_t {
  Header hdr;
  char data[MAX_MSS];
} Packet;

/* Acknowledgement structure which contains header information. */
typedef struct ack_t {
  Header hdr;
} Ack;

/*
 * Server structure to keep track of server address, socket file
 * descriptor, and whether an acknowledgement has been sent for
 * every time that the server is sent data
 */
typedef struct server_t {
  struct sockaddr_in serverAddr;
  int sockfd;
  bool ackReceived;
} Server;

/* Server port to bind to supplied through a command line argument */
int serverPort;
/* Number of servers to connect to */
int numServers;
/* List representation of servers with max of 10 */
Server servers[MAX_SERVERS];
/* Maximum segment size supplied through a command line argument */
int mss;
/* Name of file supplied through a command line argument */
char *filename;
/* File pointer to file being read */
FILE* file;

/*
 * Calculates the checksum for the packet being sent
 */
int16_t calculateChecksum(char *buffer, int bufferSize) {
  int16_t sum = 0; //initialize sum to zero
  int16_t word16;

  // make 16 bit words out of every two adjacent 8 bit words and
  // calculate the sum of all 16 bit words
  for (int i = 0; i < bufferSize - 1; i = i + 2) {
    word16 = ((buffer[i] << 8) & 0xFF00) + (buffer[i+1] & 0xFF);
    sum = sum + word16;
  }

  // if bufferSize is odd, add the last byte after shifting 8 bits
  if ((bufferSize % 2) != 0) {
    word16 = ((buffer[bufferSize - 1] << 8) & 0xFF00);
    sum = sum + word16;
  }

  // Take the one's complement of sum
  sum = ~sum;

  return sum;
}

/*
 * Sends packet of designated sequence number to all servers
 * and waits for ack response from each server.
 */
void sendPacket(int segmentNum, char *buffer, int bufferSize) {
  Packet packet;
  int packetSize = bufferSize + sizeof(Header);
  Ack ack;
  socklen_t serverAddrSize;
  struct timeval tv;

  packet.hdr.seqNum = segmentNum;
  packet.hdr.type  = DATA_PKT;
  strncpy(packet.data, buffer, bufferSize);
  packet.hdr.checksum  = calculateChecksum(packet.data, bufferSize);
  packet.data[bufferSize] = '\0';

  for (int serverNum = 0; serverNum < numServers; serverNum++) {
    servers[serverNum].ackReceived = false;
    while (!servers[serverNum].ackReceived) {
      sendto(servers[serverNum].sockfd,
             &packet,
             packetSize,
             MSG_DONTWAIT,
             (struct sockaddr*)&servers[serverNum].serverAddr,
             sizeof(struct sockaddr_in));
      tv.tv_sec = TIMEOUT_SEC;
      tv.tv_usec = TIMEOUT_USEC;
      setsockopt(servers[serverNum].sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*) &tv, sizeof tv);
      int f_recv_size = recvfrom(servers[serverNum].sockfd, &ack, sizeof(Ack),
             0, (struct sockaddr*) &servers[serverNum].serverAddr, &serverAddrSize);
      if (f_recv_size > 0 && ack.hdr.seqNum == segmentNum) {
        servers[serverNum].ackReceived = true;
      } else if (f_recv_size > 0) {
        servers[serverNum].ackReceived = false;
        // if server sends INVALID_SEQ_NO, it means it connected after first few packets were already sent
        if (ack.hdr.seqNum == INVALID_SEQ_NO)
          break;
      } else {
        printf("Timeout, sequence number = %d\n", segmentNum);
      }
    }
  }
}

/*
 * Main method reads the file, calcultes the required number of segments
 * to be sent, and sends those packets to all servers until entire file
 * is sent to all servers.
 */
int main(int argc, char **argv) {

  if(argc < 5) {
    printf("Usage %s <server-i hostname> <server port> <filename> <MSS>\n", argv[0]);
    exit(0);
  }

  // parse serverPort, filename and mss
  serverPort = atoi(argv[argc - 3]);
  filename = argv[argc - 2];
  mss = atoi(argv[argc - 1]);

  // calculate num of severs from number of arguments
  numServers = argc -  4;

  // open datagram sockets to all servers & populate servers data structure
  for (int serverNum = 0; serverNum <  numServers; serverNum++) {

    servers[serverNum].sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&servers[serverNum].serverAddr, '\0', sizeof(struct sockaddr_in));
    servers[serverNum].serverAddr.sin_family = AF_INET;
    servers[serverNum].serverAddr.sin_port = htons(serverPort);
    servers[serverNum].serverAddr.sin_addr.s_addr = inet_addr(argv[1 + serverNum]);
  }

  if((file = fopen(filename, "rb")) == NULL) {
    printf("Fatal Error opening the file: %s\n", filename);
    exit(1);
  }
  fseek(file, 0, SEEK_END);           // Jump to the end of the file
  size_t fileLength = ftell(file);    // Get the current byte offset in the file
  fseek(file, 0, SEEK_SET);           // Jump back to the beginning of the file

  int numSegments = (fileLength / mss) + 1;
  int segmentNum = 0;
  char buffer[MAX_MSS];
  int bufferSize;

  while (segmentNum < numSegments) {
    if (segmentNum < (numSegments - 1)) {
      bufferSize = mss;
    } else {
      bufferSize = fileLength - (mss * segmentNum);
    }

    int nread = fread(buffer, 1, bufferSize, file);
    if (nread != bufferSize) {
      printf("Fatal Error nread %d, bufferSize %d\n", nread, bufferSize);
      exit(2);
    }

    // send segment to all Servers with Retries
    sendPacket(segmentNum, buffer, bufferSize);

    segmentNum++;
  }
  // Signal end of file by sending buffer of size 0
  sendPacket(segmentNum, buffer, 0);
}
