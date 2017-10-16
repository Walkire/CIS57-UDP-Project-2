/* udpserver.c
* CIS457 Project 2: UDP
* Author: Nathaniel Allvin
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

#define PACKETSIZE 1000
#define WINDOWSIZE 5
#define FILEERROR 0xE0
#define EXIT -1

int nextIdNumber = 0;
int totalNumPackets = 0;
int packetsLoaded = 0;
int fileSize = 0;

FILE* fp;
int sockfd;
struct sockaddr_in serveraddr, clientaddr;

typedef struct packet {
  int idNumber;
  char data[PACKETSIZE];
  unsigned int checksum;
}packet;

struct packet window[WINDOWSIZE];

void clean(){
  nextIdNumber = 0;
  totalNumPackets = 0;
  packetsLoaded = 0;
  fileSize = 0;
}

packet nullPacket(){
  packet pkt;
  pkt.idNumber = -1;
  return pkt;
}

unsigned checksum(void *buffer, size_t len){
  unsigned int checksum = 0;
  unsigned char *buf = (unsigned char *)buffer;
  size_t i;

  for(i = 0; i < len; ++i)
    checksum += (unsigned int)(*buf++);

  return ~checksum;
}

packet newPacket(){
  size_t len;
  packet pkt;
  ++nextIdNumber;
  pkt.idNumber = nextIdNumber;
  memset(pkt.data, 0, PACKETSIZE);
  len = fread(pkt.data, 1, PACKETSIZE, fp);
  printf("packet id:%d length:%d\n", pkt.idNumber, len);
  pkt.checksum=checksum(pkt.data, len);
  printf("Created packet %d, checksum: %#x\n",pkt.idNumber, pkt.checksum);
  return pkt;
}

void fillWindow(){
  int i = 0;
  for(i; i < 5;i++){
    int packetsLeft = totalNumPackets - packetsLoaded;
    if(packetsLeft == 0){
      break;
    }
    if(window[i].idNumber == -1){
      packet temp = newPacket();
      window[i] = temp;
      printf("Loaded packet %d into window\n", temp.idNumber);
      ++packetsLoaded;
    }
  }
}

void initWindow(){
  int i =0;
  for(i; i < WINDOWSIZE; i++){
    window[i] = nullPacket();
  }
  printf("Created empty window\n");
  fillWindow();
}

void moveWindow(int minWindowNum){
  int x,y,z;
  struct packet tempArray[WINDOWSIZE];

  for(x=0; x < WINDOWSIZE; x++){
    tempArray[x] = nullPacket();
  }

  x=0;
  for(y=0; y < WINDOWSIZE; y++){
    if(window[y].idNumber == -1){
      break;
    }
    if(window[y].idNumber >= minWindowNum){
      tempArray[x] = window[y];
      ++x;
    }
  }

  for(z=0; z < WINDOWSIZE; z++){
    window[z] = tempArray[z];
  }
  printf("Moved window to position %d\n", minWindowNum);
  fillWindow();
}

void runFileTransfer(){
  int ack;
  int running = 1;
  int i, bytes, packetNeeded;
  int length = sizeof(clientaddr);
  printf("Start file transfer to %s:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
  initWindow();

  while(running){
    for(i=0;i < WINDOWSIZE;i++){
      if(window[i].idNumber == -1){
        break;
      }
      sendto(sockfd, &window[i], sizeof(window[i]), 0, (struct sockaddr*)&clientaddr, length);
      printf("Packet id:%d sent\n", window[i].idNumber, sizeof(window[i].data));
    }
    bytes = recvfrom(sockfd,&ack,sizeof(int),0,(struct sockaddr*)&clientaddr,&length);
    printf("Got from client: %d\n", ack);
    if(bytes == -1){
      printf("No data recived retrying...\n");
      continue;
    } else {
      if(ack == EXIT){
        printf("File transfer successful!\n");
        running = 0;
        break;
      } else {
        packetNeeded = ack;
        moveWindow(packetNeeded);
      }
    }
  }
}

int main(){
  char buffer[PACKETSIZE];
  char portstr[16];
  int port, bytes;

  sockfd = socket(AF_INET,SOCK_DGRAM,0);
  if(sockfd < 0){
    printf("Error opening socket\n");
    return 1;
  }

  printf("Server port: ");
  fgets(portstr, 16, stdin);
  port = atoi(portstr);

  serveraddr.sin_family=AF_INET;
  serveraddr.sin_port=htons(port);
  serveraddr.sin_addr.s_addr=INADDR_ANY;

  if(bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr)) < 0){
    printf("Error on bind\n");
    return 1;
  }

  int length = sizeof(clientaddr);
  struct timeval timeout;
  timeout.tv_sec=5;
  timeout.tv_usec=0;
  setsockopt(sockfd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

  while(1){
    memset(buffer, 0, PACKETSIZE);
    bytes = -1;
    while(bytes == -1){
      bytes = recvfrom(sockfd,buffer,PACKETSIZE,0,(struct sockaddr*)&clientaddr,&length);
    }
    printf("Client %s:%d>>%s\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port),buffer);

    fp = fopen(buffer, "r");
    if(fp == 0){
      printf("File does not exist or cannot be opened\n");
      char err = FILEERROR;
      sendto(sockfd,&err,1,0,(struct sockaddr*)&clientaddr,length);
    } else {
       struct stat stats;
       stat(buffer, &stats);

       totalNumPackets = stats.st_size / 1000;
       if((stats.st_size % 1000) != 0){
         ++totalNumPackets;
       }

       fileSize = stats.st_size;
       printf("File size: %d\n", stats.st_size);
       printf("Total packets: %d\n", totalNumPackets);

       int size = stats.st_size;
       sendto(sockfd, &size, sizeof(size), 0, (struct sockaddr*)&clientaddr, length);
       runFileTransfer();
       clean();
       fclose(fp);
     }
  }
}
