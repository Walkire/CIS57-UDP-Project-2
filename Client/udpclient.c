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

#define PACKETSIZE 1000 //Max size for the data in packets
#define FILEERROR 0xE0 //File not found code
#define WINDOWSIZE 5

#define EXIT "/exit\0"

int totalNumPackets = 0; //total number of packets for current file
int nextPacketNeeded = 0; //next packet id number needed from server
int packetsLoaded = 0; //number of packets loaded into file
int fileSize = 0; //total size of file getting from server
int bytes; //bytes recved from server in one responce (-1 no data in buffer)

FILE* fp; //file pointer
struct sockaddr_in serveraddr;
int sockfd;

void trimLast(char *str);

/*Packet Structure*/
typedef struct packet {
  int idNumber;
  char data[PACKETSIZE];
  unsigned int checksum;
}packet;

/*Window*/
struct packet packetStorage[WINDOWSIZE];

/* clean()
* Resets all values to normal and empties the buffer
*/
void clean(){
  printf("Cleaning...\n");
  int len = sizeof(serveraddr);
  char buffer[PACKETSIZE];

  bytes = 0;
  totalNumPackets = 0;
  nextPacketNeeded = 0;
  packetsLoaded = 0;
  fileSize = 0;

  //emptying buffer, feel like there is a better way
  while(bytes != -1){
    sleep(1);
    bytes = recvfrom(sockfd,buffer,PACKETSIZE,0,(struct sockaddr*)&serveraddr, (socklen_t *) &len);
  }
}

/* checksum()
* This function is used to check if the data from a packet was corrupted or not
*
* @param {void *} buffer - Data that is added to itself to compair with checksum
* @param {size_t} len - size of the buffer
* @param {unsigned int} checksum - Checksum to be compaired with the buffer
* @return {int} Returns a 1 on match and 0 on no match
*/
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

/* emptyWindow()
* Empties the window so the window can be loaded with new packets
*/
void emptyWindow(){
  int i;
  for(i=0; i < WINDOWSIZE; i++){
    packetStorage[i] = nullPacket();
  }
  printf("Emptied storage window\n");
}

/* savetoFile()
* Takes the window and loads the packets needed to the new file in the order needed
*/
void savetoFile(){
  int i, loading;
  loading = 1;
  while(loading){
    for(i = 0;i < WINDOWSIZE;i++){
      //if the window has the next packet needed
      if(packetStorage[i].idNumber == nextPacketNeeded){
        //if the checksum returns a 1 (see checksum())
        if(checksum(packetStorage[i].data, sizeof(packetStorage[i].data), packetStorage[i].checksum)){
          printf("Packet id:%d is clean\n", packetStorage[i].idNumber);
          //if this is the last packet we need to adjust the size to not have trailing 0's
          if(nextPacketNeeded == totalNumPackets){
            fwrite(packetStorage[i].data,1 , fileSize%PACKETSIZE, fp);
          //else we just load the packet with default size (see PACKETSIZE)
          } else {
            fwrite(packetStorage[i].data, 1, sizeof(packetStorage[i].data), fp);
          }
          /*we store the packet to the file, we then loop through the window again
          in case of unordering*/
          printf("Saved packet id:%d to file\n", packetStorage[i].idNumber);
          ++packetsLoaded;
          ++nextPacketNeeded;
          break;
        } else {
          printf("Packet %d is corrupted\n", nextPacketNeeded);
        }
      }
      //if window doesnt have what we need, leave the loop
      if(i == 4){
        loading = 0;
        break;
      }
    }
  }
}

/*sendAck()
* Sends a ack to the server
*
* @return {int} Retuns a 1 if all packets have been loaded, a 0 if we still need more
*/
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

/* runFileTransfer()
* Brain for the file transfer
*/
void runFileTransfer(){
  emptyWindow();
  int complete = 0; //used for capturing sendAck() return
  int running = 1;
  int windowPosition = 0; //keeps track of current window position so to not go over 5
  ++nextPacketNeeded;
  packet serverpacket; //dummy packet to fill with server responses
  int length = sizeof(serveraddr);
  //continues till file transfer is over
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
      //Once we have a full window of packets or at least enough packets to fill the rest of the file
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

/*main()
* Start of program
*/
int main(){
  char portstr[16]; //Str version of port num
  char address[64]; //address of server
  char s_buffer[PACKETSIZE]; //contains file name
  unsigned int port; //port of server
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
  timeout.tv_sec=1; //recvfrom timeout (in sec)
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

    //send filename to server
    sendto(sockfd,s_buffer,sizeof(s_buffer),0,(struct sockaddr*)&serveraddr,sizeof(serveraddr));

    int size = 0;
    int len = sizeof(serveraddr);

    //gets size of file that we need
    bytes = recvfrom(sockfd, &size, sizeof(size), 0, (struct sockaddr*)&serveraddr, (socklen_t *) &len);
    if(bytes <= 0){
      printf("Error getting data from server\n");
      return 1;
    }
    //see FILEERROR
    if(bytes == 1){
      if((unsigned char) size == FILEERROR){
        printf("File was not found or was unable to be opened\n");
      }
    } else {
      //we calulate the total number of packets we will need (see totalNumPackets)
      totalNumPackets = size / 1000;
      if((size % 1000) != 0){
        ++totalNumPackets;
      }

      fileSize = size;
      printf("File size: %d\n",size);
      printf("Total Packets: %d\n", totalNumPackets);

      fp = fopen(s_buffer, "w");
      //start file transfer
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
