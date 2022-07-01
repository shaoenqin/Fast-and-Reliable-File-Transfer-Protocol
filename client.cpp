#include <iostream>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <pthread.h>

using namespace std;
//Arbitrary constants to tweak to improve performance
#define PACKET_SIZE 1400
#define SIGNAL_REDUNDANCY 10
#define SIGNAL_PACKET_SPACING_MIRCO 75
#define RESEND_PACKET_SPACING_MIRCO 75
#define RESEND_LOOP_DELAY_MICRO 1
#define RESEND_REDUNDANCY 5
#define END_RESEND_PHASE_REDUNDANCY 10000

#define ERROR_FLAG -1
#define FILE_INFO_CORRECT -2
#define FIRST_PHASE_END -5
#define SECOND_PHASE_END -8
#define REQUEST_SIGNAL_SIZE 8

/**
 * Defined global variables
 */
int sockfdUDP; // UDP socket
struct sockaddr_in serverAddr;  // server address for UDP connection
char *receiveBuff;
char *receiveFlag;
int *endFlag;

struct packetInfo{
    int packetID;
    char packetData[PACKET_SIZE];
};
struct packetInfo targetPacketInfo;

struct fileInfo{
    int packetNum;
    int fileSize;
};
struct fileInfo targetFileInfo;

/**
 * create an UDP socket
 */
void createUDPSocket(const char * adress, int &port)
{
    sockfdUDP = socket(AF_INET, SOCK_DGRAM, 0); // Create UCP socket
    //test if create a socket successfully
    if (sockfdUDP == ERROR_FLAG)
    {
        perror("[ERROR] server: fail to create socket for hospitalA");
        exit(1);
    }
    // from beej's tutorial
    // Initialize IP address, port number
    memset(&serverAddr, 0, sizeof(serverAddr)); //  make sure the struct is empty
    serverAddr.sin_family = AF_INET; // Use IPv4 address family
    serverAddr.sin_addr.s_addr = inet_addr(adress); // Host IP address
    serverAddr.sin_port = htons(port); // Port number for server
}

void sendRequestToServer() {
    char sendBuf[REQUEST_SIGNAL_SIZE] = "requst";
    
    for(int i = 0; i < SIGNAL_REDUNDANCY; i++){
        if(::sendto(sockfdUDP, sendBuf, sizeof(sendBuf), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == ERROR_FLAG)
        {
            perror("failed to send request to server");
            exit(1);
        }
    }
    cout<<"send request"<< endl;
    struct fileInfo targetFileInfoTemp;
    
    while(targetFileInfoTemp.packetNum != FILE_INFO_CORRECT) {
        
        if (::recvfrom(sockfdUDP, &targetFileInfoTemp, sizeof(targetFileInfoTemp), 0, NULL, NULL) == ERROR_FLAG)
        {
            perror("[ERROR] scheduler: fail to receive file information from server");
            exit(1);
        }
        if(targetFileInfoTemp.packetNum >= 0) {
            targetFileInfo.packetNum = targetFileInfoTemp.packetNum;
            targetFileInfo.fileSize = targetFileInfoTemp.fileSize;
            if(::sendto(sockfdUDP, &targetFileInfo.packetNum, sizeof(targetFileInfo.packetNum), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == ERROR_FLAG)
            {
                perror("failed to send file size to server");
                exit(1);
            }
        }
    }
}

void firstPhase(){
    
    cout << "First phase starts" << endl << endl << endl;
    
    receiveBuff = (char *)mmap(NULL, targetFileInfo.packetNum*PACKET_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(receiveBuff == MAP_FAILED) {
        perror("failed to allocate memory for receiveBuff");
        exit(1);
    }
    memset(receiveBuff, 0, targetFileInfo.packetNum*PACKET_SIZE);
    
    receiveFlag = (char *)mmap(NULL, targetFileInfo.packetNum+1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(receiveFlag == MAP_FAILED) {
        perror("failed to allocate memory for receiveFlag");
        exit(1);
    }
    memset(receiveFlag, 0, targetFileInfo.packetNum+1);

    while(targetPacketInfo.packetID != FIRST_PHASE_END){
        
        memset(&targetPacketInfo, 0, sizeof(targetPacketInfo));
        
        if (::recvfrom(sockfdUDP, &targetPacketInfo, sizeof(targetPacketInfo), 0, NULL, NULL) == ERROR_FLAG)
        {
            perror("[ERROR] scheduler: fail to receive packet information from server");
            exit(1);
        }
        cout<< "Received packet ID: " << targetPacketInfo.packetID << " Total packet number: " << targetFileInfo.packetNum << endl;
        
        if((targetPacketInfo.packetID > 0) && (targetPacketInfo.packetID <= targetFileInfo.packetNum)){
            
            memcpy(receiveBuff + ((targetPacketInfo.packetID - 1) * PACKET_SIZE), &targetPacketInfo.packetData, sizeof(targetPacketInfo.packetData));
            memset(receiveFlag + targetPacketInfo.packetID, 1, 1);
        }
    }
    
}


void sendLostPacketID() {
    
    int lostPacketNum;
    char packetFlag;
    int packetIndex;
    struct packetInfo sendLostPacketInfo;
    
    while(1){
        
        lostPacketNum = 0;
        packetIndex = 0;
        memset(&sendLostPacketInfo, 0, sizeof(sendLostPacketInfo));
        
        for(int i = 1; i <= targetFileInfo.packetNum; i++){
            memcpy(&packetFlag, receiveFlag+i, 1);
            if(packetFlag == 0){
                memcpy(sendLostPacketInfo.packetData+(packetIndex*4), &i, 4);
                packetIndex++;
                lostPacketNum++;
                if((lostPacketNum % (PACKET_SIZE / 4)) == 0){
                    sendLostPacketInfo.packetID = 0;
                    for(int j = 0; j < RESEND_REDUNDANCY; j++){
                        if(::sendto(sockfdUDP, &sendLostPacketInfo, sizeof(sendLostPacketInfo), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == ERROR_FLAG)
                        {
                            perror("failed to send lost packet ID to server");
                            exit(1);
                        }
                        usleep(RESEND_PACKET_SPACING_MIRCO);
                    }
                    memset(&sendLostPacketInfo, 0, sizeof(sendLostPacketInfo));
                    packetIndex = 0;
                }
            }
        }
        
        cout << "Number of lost packets: " << lostPacketNum << endl;
        
        if(lostPacketNum == 0){
            memset(endFlag, 1, 4);
            break;
        }
        
        //Fill the remaining packet date size with 0
        if((PACKET_SIZE - (lostPacketNum * 4)) > 0){
            memset(sendLostPacketInfo.packetData + (lostPacketNum*4), 0, (PACKET_SIZE - (lostPacketNum * 4)));
        }
        
        //Send the last packet of lost packetID to server
        for(int j = 0; j < RESEND_REDUNDANCY; j++){
            if(::sendto(sockfdUDP, &sendLostPacketInfo, sizeof(sendLostPacketInfo), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == ERROR_FLAG)
            {
                perror("failed to send last lost packet ID to server");
                exit(1);
            }
            usleep(RESEND_PACKET_SPACING_MIRCO);
        }
        
        //Delay
        usleep(RESEND_LOOP_DELAY_MICRO);
    }
}

void* receiveLostPacket(void *arg) {
    int endSignal = 0;
    
    while(endSignal == 0){
        
        memset(&targetPacketInfo, 0, sizeof(targetPacketInfo));
        
        if (::recvfrom(sockfdUDP, &targetPacketInfo, sizeof(targetPacketInfo), 0, NULL, NULL) == ERROR_FLAG)
        {
            perror("[ERROR] scheduler: fail to receive packet information from server");
            exit(1);
        }
        
        if(targetPacketInfo.packetID > 0){
            memcpy(receiveBuff + ((targetPacketInfo.packetID - 1) * PACKET_SIZE), &targetPacketInfo.packetData, sizeof(targetPacketInfo.packetData));
            memset(receiveFlag + targetPacketInfo.packetID, 1, 1);
        }
        memcpy(&endSignal, endFlag, 4);

    }

    return NULL;
}

void secondPhase() {
    
    cout << "Second phase starts" << endl << endl << endl;
    
    endFlag = (int *)mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(endFlag, 0, sizeof(int));
    
    pthread_t thread;
    
    pthread_create(&thread, NULL, receiveLostPacket, NULL);
    
    sendLostPacketID();
    
}

void endPhase() {
    //Signal end of this phase by sending packets with packetID -8 to server
    targetPacketInfo.packetID = SECOND_PHASE_END;
    for(int j = 0; j < END_RESEND_PHASE_REDUNDANCY; j++){
        if(::sendto(sockfdUDP, &targetPacketInfo, sizeof(targetPacketInfo), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == ERROR_FLAG)
        {
            perror("failed to send last lost packet ID to server");
            exit(1);
        }
        usleep(SIGNAL_PACKET_SPACING_MIRCO);
    }
}

void writeFile(char * fileName) {
    FILE *fp;
    if((fp = fopen(fileName, "wb")) == NULL){
        perror("couldn't open file for writing.\n");
        exit(1);
    }
    
    //Write contents of buffer to output file
    cout << "storing the file..." << endl;
    fwrite(receiveBuff, 1, targetFileInfo.fileSize, fp);

    fclose(fp);
}

int main(int argc, char * argv []){
    //Input: ./<executable> <filename> <IP> <port number>
    
    int portNum = atoi(argv[3]);
    createUDPSocket(argv[2], portNum);

    //Send requset to server
    sendRequestToServer();

    firstPhase();
    
    secondPhase();
    
    endPhase();
    
    writeFile(argv[1]);
    
    close(sockfdUDP);
    
    cout << "Finished" << endl;
}
