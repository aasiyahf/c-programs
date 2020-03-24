/*
 * The P2MP-FTP server implements the receiver in the reliable data transfer.
 * When the server starts, it opens up and listens on the well-known port 7735.
 * When the server receives a data packet, it uses it to calculate checksum,
 * determines whether the packet has been received in-sequence or not, and
 * responds appropriately based on the results of the previously mentioned
 * actions.
 *
 * The server uses udp to send acknowledgements to the P2MP-FTP clients using
 * a stop-and-wait ARQ.
 *
 * Run as:
 * ./p2mpserver <port> <filename> <packet loss probability>
 *
 * Author: Aasiyah Feisal (anfeisal)
 */

#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define DATA_PKT 0b0101010101010101
#define ACK_PKT  0b1010101010101010
#define MAX_MSS 1024
#define INVALID_SEQ_NO -1

/*
 * Header structure which contains a 32-bit sequence number, a 16-bit
 * checksum of the data part being received, and a 16-bit field which
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
 * Calculates the checksum for the packet being received
 */
int16_t calculateChecksum(char *buffer, int bufferSize) {
  int16_t sum = 0;
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

/**
 * getIPv4()
 *
 * This function takes a network identifier such as "eth0" or "eth0:0" and
 * a pointer to a buffer of at least 16 bytes and then stores the IP of that
 * device gets stored in that buffer.
 *
 * it return 0 on success or -1 on failure.
 *
 * source: https://stackoverflow.com/questions/259389/finding-an-ip-address-from-an-interface-name
 *
 * Author:  Jaco Kroon <jaco@kroon.co.za>
 */
int getIPv4(char * ipv4) {
    struct ifreq ifc;
    int res;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if(sockfd < 0)
        return -1;

    // For MAC/OSx network
    strcpy(ifc.ifr_name, "en0");
    res = ioctl(sockfd, SIOCGIFADDR, &ifc);
    if(res >= 0) {
      strcpy(ipv4, inet_ntoa(((struct sockaddr_in*)&ifc.ifr_addr)->sin_addr));
      close(sockfd);
      return 0;
    }

    // For ncsu network
    strcpy(ifc.ifr_name, "ens160");
    res = ioctl(sockfd, SIOCGIFADDR, &ifc);
    if(res < 0) {
      close(sockfd);
      return -1;
    }

    strcpy(ipv4, inet_ntoa(((struct sockaddr_in*)&ifc.ifr_addr)->sin_addr));
    close(sockfd);
    return 0;
}

/*
 * Main method listens on the designated port specified by the command line
 * arguments, and receives data from the client on this port.
 *
 * When it receives a data packet, it computes the checksum and checks whether
 * it is in-sequence, and if so, it sends an ACK segment (using UDP) to the
 * client; it then writes the received data into a file whose name is provided
 * in the command line. If the packet received is out-ofsequence, an ACK for
 * the last received in-sequence packet is sent; if the checksum is incorrect,
 * the receiver does nothing.
 */
int main(int argc, char **argv) {

  if(argc != 4) {
    printf("Usage %s <port> <filename> <packet loss probability>\n", argv[0]);
    exit(0);
  }

  int port = atoi(argv[1]);
  char *filename = argv[2];

  // Generate random probability loss number
  double packetLossProb;
  sscanf(argv[3], "%lf", &packetLossProb);
  srand(time(NULL)); // clear seed

  int sockfd;
  struct sockaddr_in serverAddr, clientAddr;
  char buffer[MAX_MSS];
  int bufferSize =  0;
  socklen_t clientAddrSize;

  int expectedSeqNum = 0;
  int lastSeqNum = INVALID_SEQ_NO;
  int16_t checksum;
  Packet dataPacket;
  Ack ackPacket;
  char hostbuffer[100];

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  memset(&serverAddr, '\0', sizeof(serverAddr));

  char ipAddr[16];
  if(getIPv4(ipAddr) != 0) {
    printf("Fata Error no IP address found\n");
    exit(1);
  }
  printf("Server ip address: %s\n", ipAddr);

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = inet_addr(ipAddr);
  serverAddr.sin_port = htons(port);

  FILE* file;
  if((file = fopen(filename, "wb")) == NULL) {
    printf("Fatal Error opening the file: %s\n", filename);
    exit(1);
  }

  bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
  clientAddrSize = sizeof(clientAddr);

  while (1) {
    int f_recv_size = recvfrom(sockfd, &dataPacket, sizeof(Packet), 0, (struct sockaddr*)&clientAddr, &clientAddrSize);
    if((f_recv_size > 0) && (dataPacket.hdr.type == DATA_PKT)) {
      bufferSize =  f_recv_size - sizeof(Header);

      // verify checksum
      if ((checksum = calculateChecksum(dataPacket.data, bufferSize)) != dataPacket.hdr.checksum) {
        continue;
      }

      int randNum = rand() % 100;
      double randPacketLossProb = ((double) randNum / 100);

      if (randPacketLossProb <= packetLossProb) {
        //ignore received message
        printf("Packet loss, sequence number = %d\n", dataPacket.hdr.seqNum);
        continue;
      } else if (dataPacket.hdr.seqNum == expectedSeqNum) {
        // ELSE IF in-sequence, then send properly
        ackPacket.hdr.seqNum = expectedSeqNum;
        ackPacket.hdr.checksum = 0;
        ackPacket.hdr.type = ACK_PKT;

        sendto(sockfd, &ackPacket, sizeof(Ack), 0, (struct sockaddr*)&clientAddr, clientAddrSize);

        //Write received data to file
        fwrite(dataPacket.data, 1, bufferSize, file);
        lastSeqNum = expectedSeqNum;
        expectedSeqNum++;
      } else if (dataPacket.hdr.seqNum != expectedSeqNum) {
        // ELSE IF out-sequence, then send ack of last in-sequence packet
        ackPacket.hdr.seqNum = lastSeqNum;
        ackPacket.hdr.checksum = 0;
        ackPacket.hdr.type = ACK_PKT;

        sendto(sockfd, &ackPacket, sizeof(Ack), 0, (struct sockaddr*)&clientAddr, clientAddrSize);
      }

      // buffer size 0 signals end of file
      if (bufferSize == 0)
        break;
    } else {
      printf("Fatal Error recvfrom failed\n");
      exit(0);
    }
  }

  close(sockfd);
  fclose(file);
}
