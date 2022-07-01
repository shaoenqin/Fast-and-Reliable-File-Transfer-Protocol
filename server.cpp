#include <iostream>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>

using namespace std;
//Arbitrary constants to tweak to improve performance
#define PACKET_SIZE 1400
#define SIGNAL_PACKET_SPACING_MIRCO 75
#define SIGNAL_REDUNDANCY 20
#define BEST_EFFORT_PACKET_SPACING_MIRCO 50
#define BEST_EFFORT_PHASES 1
#define BEST_EFFORT_REDUNDANCY 1
#define RESEND_PACKET_SPACING_MIRCO 75
#define RESEND_LOOP_DELAY_MICRO 1000
#define RESEND_REDUNDANCY 2
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
struct sockaddr_in serverAddr, clientAddr;  // server address for UDP connection
char *buff;
char *idToSendFlag;
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

void readFile(char *fileName) {
    //Input: ./<executable> <file to send> <host port>
    FILE *read_file;
    if((read_file = fopen(fileName, "rb")) == NULL){
        printf("Error opening specified file.\n");
        exit(1);
    }
    fseek(read_file, 0, SEEK_END);
    targetFileInfo.fileSize = (int)ftell(read_file);
    fseek(read_file, 0, SEEK_SET);
    targetFileInfo.packetNum = targetFileInfo.fileSize / PACKET_SIZE;
    if((targetFileInfo.fileSize % PACKET_SIZE) != 0) targetFileInfo.packetNum++;
    //Read packetData into buffer so it can be transmitted quickly
    buff = (char *)malloc(targetFileInfo.fileSize + 1);
    memset(buff, 0, targetFileInfo.fileSize + 1);
    fread(buff, 1, targetFileInfo.fileSize, read_file);
    fclose(read_file);
}

/**
 * create an UDP socket
 */
void createUDPSocket(int &port)
{
    sockfdUDP = socket(AF_INET, SOCK_DGRAM, 0); // Create UCP socket
    //test if create a socket successfully
    if (sockfdUDP == ERROR_FLAG)
    {
        perror("[ERROR] server: fail to create socket for client");
        exit(1);
    }
    // from beej's tutorial
    // Initialize IP address, port number
    memset(&serverAddr, 0, sizeof(serverAddr)); //  make sure the struct is empty
    serverAddr.sin_family = AF_INET; // Use IPv4 address family
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Host IP address
    serverAddr.sin_port = htons(port); // Port number for server
    
    // Bind socket for Server with IP address and port number
    if (::bind(sockfdUDP, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == ERROR_FLAG)
    {
        perror("[ERROR] Server: fail to bind UDP socket");
        exit(1);
    }
    printf("Server is up and running using UDP on port %d. \n\n", port);
}

void receiveRequestToServer() {
    char receiveBuf[REQUEST_SIGNAL_SIZE];
    
    socklen_t clientAddrSize = sizeof(clientAddr);
    
    if (::recvfrom(sockfdUDP, receiveBuf, sizeof(receiveBuf), 0, (struct sockaddr *) &clientAddr, &clientAddrSize) == ERROR_FLAG)
    {
        perror("[ERROR] scheduler: fail to receive request information from client");
        exit(1);
    }
    
    int receivedRequest = -1;
    
    while(receivedRequest != targetFileInfo.packetNum){
        
        if(::sendto(sockfdUDP, &targetFileInfo, sizeof(targetFileInfo), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == ERROR_FLAG)
        {
            perror("failed to send file information to client");
            exit(1);
        }
        
        if (::recvfrom(sockfdUDP, &receivedRequest, sizeof(receivedRequest), 0, NULL, NULL) == ERROR_FLAG)
        {
            perror("[ERROR] scheduler: fail to receive request information from client");
            exit(1);
        }
    }
}

void firstPhase() {
    
    cout << "First phase starts" << endl << endl;
    
    int startSignal = FILE_INFO_CORRECT;
    
    for(int i = 0; i < SIGNAL_REDUNDANCY; i++){
        if(::sendto(sockfdUDP, &startSignal, sizeof(startSignal), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == ERROR_FLAG)
        {
            perror("failed to send start signal to client");
            exit(1);
        }
        usleep(SIGNAL_PACKET_SPACING_MIRCO);
    }
   
    int sentByte = 0;
    
    //first phase
    for(int phase = 0; phase < BEST_EFFORT_PHASES; phase++){
        sentByte = 0;
        
        for(int i = 1; i <= targetFileInfo.packetNum; i++){
            
            memset(&targetPacketInfo, 0, sizeof(targetPacketInfo));
            memcpy(targetPacketInfo.packetData, buff+sentByte, PACKET_SIZE);
            targetPacketInfo.packetID = i;
            
            //Send packet information to client
            
            if(::sendto(sockfdUDP, &targetPacketInfo, sizeof(targetPacketInfo), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == ERROR_FLAG)
            {
                perror("failed to send packet to client in phase 1");
                exit(1);
            }
            usleep(BEST_EFFORT_PACKET_SPACING_MIRCO);
            sentByte += PACKET_SIZE;
            
            
        }
    }
    
    //Signal end of phase 1
    memset(&targetPacketInfo, 0, sizeof(targetPacketInfo));
    targetPacketInfo.packetID = FIRST_PHASE_END;
    for(int j = 0; j < SIGNAL_REDUNDANCY; j++){
        if(::sendto(sockfdUDP, &targetPacketInfo, sizeof(targetPacketInfo), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == ERROR_FLAG)
        {
            perror("failed to send end signal of phase 1 to client");
            exit(1);
        }
        usleep(SIGNAL_PACKET_SPACING_MIRCO);
    }
    
    cout << "First phase ends" << endl << endl;
}

void* receiveLostPacketID(void *arg) {
    
    while(1){
        memset(&targetPacketInfo, 0, sizeof(targetPacketInfo));
        if (::recvfrom(sockfdUDP, &targetPacketInfo, sizeof(targetPacketInfo), 0, NULL, NULL) == ERROR_FLAG)
        {
            perror("[ERROR] scheduler: fail to receive lost packet ID from client");
            exit(1);
        }
        if(targetPacketInfo.packetID == SECOND_PHASE_END){
            memset(endFlag, 1, 4);
            break;
        }
        memcpy(idToSendFlag, targetPacketInfo.packetData, PACKET_SIZE);
    }
    return NULL;
}

void sendLostPacket() {
    int endSignal = 0;
    int idToSend;
    struct packetInfo resendPacketInfo;
    while(endSignal == 0){
        for(int i = 0; i < PACKET_SIZE; i++){
            memcpy(&idToSend, idToSendFlag + i, 4);
            if((idToSend > 0) && (idToSend <= targetFileInfo.packetNum)){
                memset(&resendPacketInfo, 0, sizeof(resendPacketInfo));
                memcpy(resendPacketInfo.packetData, buff + ((idToSend - 1) * PACKET_SIZE), PACKET_SIZE);
                resendPacketInfo.packetID = idToSend;
                for(int j = 0; j < RESEND_REDUNDANCY; j++){
                    if(::sendto(sockfdUDP, &resendPacketInfo, sizeof(resendPacketInfo), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr)) == ERROR_FLAG)
                    {
                        perror("failed to send lost packet to client");
                        exit(1);
                    }
                    usleep(RESEND_PACKET_SPACING_MIRCO);
                }
            }
        }
        memcpy(&endSignal, endFlag, 4);
    }
}

void secondPhase() {
    cout << "Second phase starts" << endl << endl;
    
    idToSendFlag = (char *)mmap(NULL, PACKET_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(idToSendFlag == MAP_FAILED) {
        perror("failed to allocate memory for idToSendFlag");
        exit(1);
    }
    memset(idToSendFlag, 0, PACKET_SIZE);

    endFlag = (int *)mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(endFlag == MAP_FAILED) {
        perror("failed to allocate memory for endFlag");
        exit(1);
    }
    memset(endFlag, 0, 4);
    
    pthread_t thread;
    
    pthread_create(&thread, NULL, receiveLostPacketID, NULL);
   
    sendLostPacket();
    
    cout << "Second phase ends" << endl << endl;
}


int main(int argc, char * argv []){
   
    readFile(argv[1]);
    
    int portNum = atoi(argv[2]);
    createUDPSocket(portNum);
    receiveRequestToServer();

    //Begin timer
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    double start = (tv1.tv_sec) + (tv1.tv_usec) / 1000000.0;

    firstPhase();
    
    secondPhase();

    gettimeofday(&tv1, NULL);
    double end = (tv1.tv_sec)+(tv1.tv_usec) / 1000000.0;
    double usedTime = end - start;
    double transferredFileSize = (targetFileInfo.fileSize / 1000000.0) * 8;
    double throughput = transferredFileSize / usedTime;
    cout << "Transfer file size is equal to " << transferredFileSize << " Mbits" << endl;
    cout << "Transfer time is equal to " << usedTime << " seconds" <<endl;
    cout << "Throughput is equal to " << throughput << " Mbits/sec" << endl;

    free(buff);
    close(sockfdUDP);
    return 0;
}
