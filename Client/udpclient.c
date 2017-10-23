/* udpclient.c
* CIS457 Project 2: UDP
* Author: Nathaniel Allvin
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define PACKETSIZE 1000
#define FILEERROR 0xE0
#define WINDOWSIZE 5

#define EXIT "/exit\0"

int totalNumPackets = 0;
int nextPacketNeeded = 0;
int packetsLoaded = 0;
int fileSize = 0;
int bytes;

FILE* fp;
struct sockaddr_in serveraddr;
int sockfd;

void trimLast(char *str);

typedef struct packet {
  int idNumber;
  char data[PACKETSIZE];
  unsigned int checksum;
}packet;

struct packet packetStorage[WINDOWSIZE];

void clean(){
  printf("Cleaning\n");
  int len = sizeof(serveraddr);
  char buffer[PACKETSIZE];

  bytes = 0;
  totalNumPackets = 0;
  nextPacketNeeded = 0;
  packetsLoaded = 0;
  fileSize = 0;

  wait(5);
  while(bytes != -1){
    bytes = recvfrom(sockfd,buffer,PACKETSIZE,0,(struct sockaddr*)&serveraddr, (socklen_t *) &len);
  }
}

int checksum(void *buffer, size_t len, unsigned int checksum){
  unsigned int check = 0;
  unsigned char *buf = (unsigned char *)buffer;
  size_t i;

  for(i = 0; i < len; ++i)
    check += (unsigned int)(*buf++);

  check += checksum;

  if(check == 0xffffffff){
    return 1;
  }

  return 0;
}

packet nullPacket(){
  packet pkt;
  pkt.idNumber = -1;
  return pkt;
}

void emptyWindow(){
  int i;
  for(i=0; i < WINDOWSIZE; i++){
    packetStorage[i] = nullPacket();
  }
  printf("Emptied storage window\n");
}

void savetoFile(){
  int i, loading;
  loading = 1;
  while(loading){
    for(i = 0;i < WINDOWSIZE;i++){
      if(packetStorage[i].idNumber == nextPacketNeeded){
        if(checksum(packetStorage[i].data, sizeof(packetStorage[i].data), packetStorage[i].checksum)){
          printf("Packet id:%d is clean\n", packetStorage[i].idNumber);
          if(nextPacketNeeded == totalNumPackets){
            fwrite(packetStorage[i].data,1 , fileSize%PACKETSIZE, fp);
          } else {
            fwrite(packetStorage[i].data, 1, sizeof(packetStorage[i].data), fp);
          }
          printf("Saved packet id:%d to file\n", packetStorage[i].idNumber);
          ++packetsLoaded;
          ++nextPacketNeeded;
          break;
        } else {
          printf("Packet %d is corrupted\n", nextPacketNeeded);
        }
      }
      if(i == 4){
        loading = 0;
        break;
      }
    }
  }
}

int sendAck(){
  int endtransfer = -1;
  if(packetsLoaded == totalNumPackets){
    sendto(sockfd,&endtransfer,sizeof(endtransfer),0,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
    printf("Sent end file transfer to server\n");
    return 1;
  } else {
    sendto(sockfd,&nextPacketNeeded,sizeof(nextPacketNeeded),0,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
    printf("Sent ack for %d+\n", nextPacketNeeded);
    return 0;
  }
}

void runFileTransfer(){
  emptyWindow();
  int complete = 0;
  int running = 1;
  int windowPosition = 0;
  ++nextPacketNeeded;
  packet serverpacket;
  int length = sizeof(serveraddr);
  while(running){
    memset(serverpacket.data,0,PACKETSIZE);
    bytes = recvfrom(sockfd,&serverpacket,sizeof(serverpacket),0,(struct sockaddr*)&serveraddr,&length);
    printf("Recived packet id:%d size:%d\n", serverpacket.idNumber, bytes);
    //Timeout
    if(bytes == -1){
      printf("No packets recived, sending last ack again...\n");
      complete = sendAck();
      if(complete == 1){
        running = 0;
        printf("File transfer finished\n");
      }
    //recved something
    } else {
      packetStorage[windowPosition] = serverpacket;
      ++windowPosition;
      if(windowPosition == WINDOWSIZE || packetsLoaded + windowPosition == totalNumPackets){
        savetoFile();
        emptyWindow();
        windowPosition = 0;
        complete = sendAck();
        if(complete == 1){
          running = 0;
          printf("File transfer finnished\n");
        }
      }
    }
  }
}

int main(){
  char portstr[16];
  char address[64];
  char s_buffer[PACKETSIZE];
  unsigned int port;
  socklen_t addr_size;

  sockfd = socket(AF_INET,SOCK_DGRAM,0);
  if(sockfd < 0){
    printf("There was an error creating the socket\n");
    return 1;
  }

  printf("Server address: ");
  fgets(address, 64, stdin);
  trimLast(address);

  printf("Server port: ");
  fgets(portstr, 16, stdin);
  trimLast(portstr);
  port = atoi(portstr);

  printf("Connecting to %s:%d\n",address,port);

  serveraddr.sin_family=AF_INET;
  serveraddr.sin_port=htons(port);
  serveraddr.sin_addr.s_addr=inet_addr(address);

  addr_size = sizeof(serveraddr);
  struct timeval timeout;
  timeout.tv_sec=1;
  timeout.tv_usec=0;
  setsockopt(sockfd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

  while(1){
    memset(s_buffer, 0, PACKETSIZE);
    printf(">> ");
    fgets(s_buffer, PACKETSIZE, stdin);
    trimLast(s_buffer);

    if(!strcmp(s_buffer,EXIT)){
      return 0;
    }

    sendto(sockfd,s_buffer,sizeof(s_buffer),0,(struct sockaddr*)&serveraddr,sizeof(serveraddr));

    int size = 0;
    int len = sizeof(serveraddr);

    bytes = recvfrom(sockfd, &size, sizeof(size), 0, (struct sockaddr*)&serveraddr, (socklen_t *) &len);
    if(bytes <= 0){
      printf("Error getting data from server\n");
      return 1;
    }
    if(bytes == 1){
      if((unsigned char) size == FILEERROR){
        printf("File was not found or was unable to be opened\n");
      }
    } else {
      totalNumPackets = size / 1000;
      if((size % 1000) != 0){
        ++totalNumPackets;
      }

      fileSize = size;
      printf("File size: %d\n",size);
      printf("Total Packets: %d\n", totalNumPackets);

      fp = fopen(s_buffer, "w");
      if(fp){
        runFileTransfer();
        clean();
      } else {
        printf("Failed to create file %s\n", s_buffer);
        return 1;
      }
      fclose(fp);
    }
  }
}

void trimLast(char *str) {
    for(int i = 0; i < strlen(str); i++) {
        if(str[i] == '\n') {
            str[i] = '\0';
            break;
        }
    }
}
