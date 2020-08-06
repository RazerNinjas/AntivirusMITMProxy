#ifndef PROXY
#define PROXY
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <ctype.h>
#include <zlib.h>
#include <signal.h>
#include <clamav.h>
#include <poll.h>

#define MAX_RECV_SIZE 1024
#define MAX_SEND_SIZE 4096
#define MAX_PORT_LENGTH 6
#define PORT "8080"
//BACKLOGGED CONNECTIONS (CONNECTIONS IN QUEUE THAT HAVE NOT BEEN ACCEPTED YET)
#define MAX_LISTENING_CONNECTIONS 50
#define HASH_FILE_NAME_LENGTH 69
#define MAX_HEX_STRING_LENGTH 100
#define VIRUS 1
#define CLEAN 0
#define SCAN_ERROR -1
#define MESSAGE_LOCATION "./responseMessages/"

extern struct cl_engine* engine;


typedef struct SocketBuffer
{
    int dataSize;
    char data[MAX_RECV_SIZE+MAX_SEND_SIZE+MAX_SEND_SIZE];

} SocketBuffer;

typedef struct Connection
{
    char* hostname;
    char port[MAX_PORT_LENGTH];
} Connection;

typedef struct ChunkBuffer
{
    char* buffer;
    long long currentMaxSize;
    long long currentSize;
} ChunkBuffer;


void fileNameHash(char* request,char fileName[HASH_FILE_NAME_LENGTH]);
int setupSocket();
SocketBuffer* parseRequest(int socketFD);
Connection* allocateConnection(char* hostname, char* portNumber);
void freeConnection(Connection* connection);
Connection* parseURL(char* url);
int parseOperation(char* operation);
void handlePOSTResponse(int outgoingSocket,int clientSocket);
ChunkBuffer* chunkBufferInit();
void allocateBufferSpace(ChunkBuffer* chunk);
void freeChunk(ChunkBuffer* chunk);
int dechunkResponse(int outgoingSocket,FILE* resultFile,SocketBuffer* data,int clientSocket);
int handleNonChunkResponse(int outgoingSocket,FILE* resultFile,SocketBuffer* data,int clientSocket,char* header);
void handleGETResponse(int outgoingSocket, int clientSocket, char fileName[HASH_FILE_NAME_LENGTH],int clientConnection);
void sendRequest(int outgoingSocket,int clientSocket,ChunkBuffer* request,int isGet,ChunkBuffer* data);
void* handleRequest(void* arguments);
void mainLoop(int proxyFD);
void unzipFile(char* fileName,FILE* resultFile,char* fileString);
int initClam();
int virusScan(char* fileName, const char* virus);
void sendBadResponse(int clientSocket, const char* virusName);
void sendOKResponse(int clientSocket, char* contentType,char* fileName, int connection);
long long readContentLength(char* header);
void handlePersistantRequest(int clientSocket,int serverSocket);
int getConnection(char* data);
ChunkBuffer* readHeader(int outgoingSocket,SocketBuffer** leftoverData);
ChunkBuffer* extractPostData(int socketFD, SocketBuffer* data, long long length);
#endif