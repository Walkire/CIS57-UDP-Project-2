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

#define PACKETSIZE 1000 //Max size for the data in packets
#define WINDOWSIZE 5 //File not found code
#define FILEERROR 0xE0
#define EXIT -1 //File transfer end code

int nextIdNumber = 0; //keeps track of next id number when creating new packet
int totalNumPackets = 0; //total number of packets for current file
int packetsLoaded = 0; //number of packets loaded into window
int fileSize = 0;

FILE* fp;
int sockfd;
struct sockaddr_in serveraddr, clientaddr;

/*Packet Structure*/
typedef struct packet {
  int idNumber;
  char data[PACKETSIZE];
  unsigned int checksum;
}packet;

/*Window*/
struct packet window[WINDOWSIZE];

/* clean()
* Resets all values to normal
*/
void clean(){
  nextIdNumber = 0;
  totalNumPackets = 0;
  packetsLoaded = 0;
  fileSize = 0;
}

/* nullPacket()
* Creates a dummy packet
*
* @return {packet} dummy packet made
*/
packet nullPacket(){
  packet pkt;
  pkt.idNumber = -1;
  return pkt;
}

/* checksum()
* This function is used to create a checksum by adding the data to itself
*
* @param {void *} buffer - Data that is added to itself to create checksum
* @param {size_t} len - size of the buffer
* @return {unsigned int} Returns a unsigned number to be used as the checksum
*/
unsigned checksum(void *buffer, size_t len){
  unsigned int checksum = 0;
  unsigned char *buf = (unsigned char *)buffer;
  size_t i;

  for(i = 0; i < len; ++i)
    checksum += (unsigned int)(*buf++);

  return ~checksum;
}

/* newPacket()
* Creates a new packet with the correct idnumber, data from file and checksum
*
* @return {packet} Packet created
*/
packet newPacket(){
  size_t len;
  packet pkt;
  ++nextIdNumber;
  pkt.idNumber = nextIdNumber;
  memset(pkt.data, 0, PACKETSIZE);
  //read data from file
  len = fread(pkt.data, 1, PACKETSIZE, fp);
  //printf("packet id:%d length:%d\n", pkt.idNumber, len); //used for testing
  pkt.checksum=checksum(pkt.data, len);
  printf("Created packet %d, checksum: %#x\n",pkt.idNumber, pkt.checksum);
  return pkt;
}

/*fillWindow()
*fills the window with 5 packets or packets up to the totalNumPackets
*/
void fillWindow(){
  int i = 0;
  for(i; i < 5;i++){
    int packetsLeft = totalNumPackets - packetsLoaded;
    //if all packets have been loaded
    if(packetsLeft == 0){
      break;
    }
    //if the window[i] has an empty packet. Useful for moveWindow()
    if(window[i].idNumber == -1){
      packet temp = newPacket();
      window[i] = temp;
      printf("Loaded packet %d into window\n", temp.idNumber);
      ++packetsLoaded;
    }
  }
}

/*initWindow()
* fills window with empty packets. Helps later with fillWindow()
*/
void initWindow(){
  int i =0;
  for(i; i < WINDOWSIZE; i++){
    window[i] = nullPacket();
  }
  printf("Created empty window\n");
  fillWindow();
}

/*moveWindow()
*"Slides" window down to minimum packet needed by client
*
*@param {int} minWindowNum - minimum packet id still needed. Related to ack recv from client
*/
void moveWindow(int minWindowNum){
  int x,y,z;
  struct packet tempArray[WINDOWSIZE];

  //Create a temp array with empty packets
  for(x=0; x < WINDOWSIZE; x++){
    tempArray[x] = nullPacket();
  }

  //"Slide" part. we move the window down to the minWindowNum
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

  //Change window to tempArray
  for(z=0; z < WINDOWSIZE; z++){
    window[z] = tempArray[z];
  }
  printf("Moved window to position %d\n", minWindowNum);
  //fill rest of window with new packets
  fillWindow();
}

/*runFileTransfer()
* Brain of the file transfer
*/
void runFileTransfer(){
  int ack, maxidNum; //ack - next packet needed from client. maxidNum - max ack number possible from client
  int running = 1;
  int resend = 0; //patience counter incase client ends without server knowing
  int i, bytes, packetNeeded;
  int length = sizeof(clientaddr);
  printf("Start file transfer to %s:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
  initWindow();

  while(running){
    //main loop to send entire window at once
    for(i=0;i < WINDOWSIZE;i++){
      if(window[i].idNumber == -1){
        break;
      }
      sendto(sockfd, &window[i], sizeof(window[i]), 0, (struct sockaddr*)&clientaddr, length);
      printf("Packet id:%d sent\n", window[i].idNumber, sizeof(window[i].data));
    }
    //wait for ack
    bytes = recvfrom(sockfd,&ack,sizeof(int),0,(struct sockaddr*)&clientaddr,&length);
    //calculate max id number we can see from client
    if(totalNumPackets == packetsLoaded){
      maxidNum = packetsLoaded;
    } else {
      maxidNum = packetsLoaded + 1;
    }
    //not the best way to do this, but this is the only check for ack corruption or timeout
    if((bytes == -1 || ack > maxidNum || ack < window[0].idNumber) && ack != -1){
      printf("No data recived retrying...\n");
      ++resend;
      //patience has run out
      if(resend == 3){
        printf("File transfer ended early\n");
        running = 0;
        break;
      }
      //try sending same thing again
      continue;
    } else {
      resend = 0;
      //end of file transfer
      if(ack == EXIT){
        printf("File transfer successful!\n");
        running = 0;
        break;
      } else {
        //move window to ack position
        printf("Got from client: %d\n", ack);
        packetNeeded = ack;
        moveWindow(packetNeeded);
      }
    }
  }
}

/*main()
* Start of program
*/
int main(){
  char buffer[PACKETSIZE]; //used to store file name from client
  char portstr[16]; //str version of port number
  int port, bytes; //port - port number of server. bytes - bytes recvfrom client

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
  timeout.tv_sec=1; //recvfrom timeout (in sec)
  timeout.tv_usec=0;
  setsockopt(sockfd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

  while(1){
    memset(buffer, 0, PACKETSIZE);
    bytes = -1;
    //wait for file name from client
    while(bytes == -1){
      bytes = recvfrom(sockfd,buffer,PACKETSIZE,0,(struct sockaddr*)&clientaddr,&length);
    }
    printf("Client %s:%d>>%s\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port),buffer);

    fp = fopen(buffer, "r");
    if(fp == 0){
      printf("File does not exist or cannot be opened\n");
      //see FILEERROR
      char err = FILEERROR;
      sendto(sockfd,&err,1,0,(struct sockaddr*)&clientaddr,length);
    } else {
       struct stat stats;
       stat(buffer, &stats);

       //we calulate the total number of packets we will need (see totalNumPackets)
       totalNumPackets = stats.st_size / 1000;
       if((stats.st_size % 1000) != 0){
         ++totalNumPackets;
       }

       fileSize = stats.st_size;
       printf("File size: %d\n", stats.st_size);
       printf("Total packets: %d\n", totalNumPackets);

       int size = stats.st_size;
       sendto(sockfd, &size, sizeof(size), 0, (struct sockaddr*)&clientaddr, length);
       //start file transfer
       runFileTransfer();
       clean();
       fclose(fp);
     }
  }
}
