#include "proxy.h"

ChunkBuffer* chunkBufferInit()
{
    ChunkBuffer* chunk = (ChunkBuffer*)malloc(sizeof(ChunkBuffer));
    chunk->buffer = (char*)malloc((MAX_RECV_SIZE+MAX_SEND_SIZE)*5);
    memset(chunk->buffer,0,MAX_SEND_SIZE+MAX_RECV_SIZE);
    chunk->currentMaxSize = (MAX_RECV_SIZE+MAX_SEND_SIZE)*5;
    chunk->currentSize = 0;
    return chunk;
}

void allocateBufferSpace(ChunkBuffer* chunk)
{
    char* temp = (char*)malloc(chunk->currentMaxSize*2);
    if(!temp)
    {
        fprintf(stderr,"UNABLE TO MALLOC\n");
    }
    memcpy(temp,chunk->buffer,chunk->currentSize);
    memset(&temp[chunk->currentSize],0,chunk->currentMaxSize*2-chunk->currentSize);
    free(chunk->buffer);
    chunk->buffer = temp;
    chunk->currentMaxSize *= 2;
    
}

void freeChunk(ChunkBuffer* chunk)
{
    if(!chunk)
        return;
    if(chunk->buffer)
        free(chunk->buffer);
    free(chunk);
}

// Creates a hash hex string of the file and outputs it into the outputBuffer
void fileNameHash(char* request,char fileName[HASH_FILE_NAME_LENGTH])
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    struct stat st;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256,request,strlen(request));
    //WE NEED TO ENSURE ALL HASHES ARE UNIQUE
    char fileNameText[MAX_RECV_SIZE];
    char fileNameZip[MAX_RECV_SIZE];
    int value;
    do
    {

        value = rand();
        SHA256_Update(&sha256,&value,sizeof(value));
        SHA256_Final(hash,&sha256);
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        {
            // 2 hex characters represent each byte in the hash
            sprintf(fileName + (i * 2),"%02x",hash[i]);
        }
        fileName[64] = 0;
        strcpy(fileNameText,MESSAGE_LOCATION);
        strcpy(fileNameZip, MESSAGE_LOCATION);
        strcat(fileNameText,fileName);
        strcat(fileNameZip,fileName);
        strcat(fileNameText,".txt");
        strcat(fileNameZip,".gz");

    } while (stat(fileNameText,&st) != -1 || stat(fileNameZip,&st) != -1);
}


//creates and binds socket and returns the resulting FD
//if FD is < 0 an error occured in the socket creation or bind
int setupSocket()
{
    struct addrinfo hints;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo* bindaddress;
    getaddrinfo(0,PORT,&hints,&bindaddress);
    int socketFD = socket(bindaddress->ai_family,bindaddress->ai_socktype,bindaddress->ai_protocol);
    if(socketFD < 0)
    {
        fprintf(stderr, "ERROR CREATING SOCKET: Errno = %d\n", errno);
        return -1;
    }
    if(setsockopt(socketFD,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int)) < 0)
    {
        fprintf(stderr, "ERROR WITH SOCKET OPTIONS: Errno = %d\n",errno);
        return -1;
    }
    if (bind(socketFD,bindaddress->ai_addr,bindaddress->ai_addrlen))
    {
        fprintf(stderr, "ERROR BINDING SOCKET: Errno = %d\n",errno);
        return -1;
    }
    freeaddrinfo(bindaddress);
    return socketFD;

}

//wrapper to construct the Connection dynamically
//inserts info from hostname and portnumber into the struct
Connection* allocateConnection(char* hostname, char* portNumber)
{
    Connection* outgoingSocket = (Connection*)malloc(sizeof(Connection));
    outgoingSocket->hostname = (char*)malloc(strlen(hostname)+1);
    strcpy(outgoingSocket->hostname,hostname);
    strcpy(outgoingSocket->port,portNumber);
    return outgoingSocket;
}

//free wrapper for the struct in the program
void freeConnection(Connection* connection)
{
    free(connection->hostname);
    free(connection);
}