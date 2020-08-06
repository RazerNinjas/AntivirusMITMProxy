
#include "proxy.h"

struct cl_engine* engine;

int handlePostChunked(int outgoingSocket, SocketBuffer* data, int clientSocket)
{
    char* foundEnd = strstr(data->data,"\r\n");
    int bytesRead;
    while(!foundEnd)
    {
        bytesRead = recv(outgoingSocket,&data->data[data->dataSize],MAX_RECV_SIZE,0);
        if(bytesRead < 0)
        {
            fprintf(stderr,"DECHUNK RECV ERROR\n");
            return -1;
        }
        data->dataSize+=bytesRead;
        if(data->dataSize > MAX_RECV_SIZE+MAX_SEND_SIZE)
        {
            fprintf(stderr,"DECHUNK BUFFER POST OVERFLOW\n");
            return -1;
        }
        foundEnd = strstr(data->data,"\r\n");
    }
    int chunkLength = strtol(data->data,NULL,16);
    if(!chunkLength)
    {
        send(clientSocket,"0\r\n\r\n",strlen("0\r\n\r\n"),0);
        return 0;
    }
    char stringNum[100];
    sprintf(stringNum,"%X",chunkLength);
    strcat(stringNum,"\r\n");
    send(clientSocket,stringNum,strlen(stringNum),0);
    ChunkBuffer* currentChunk = chunkBufferInit();
    memcpy(currentChunk->buffer,foundEnd+2,(data->data+data->dataSize)-(foundEnd+2));
    currentChunk->currentSize = (data->data+data->dataSize)-(foundEnd+2);
    memset(data,0,sizeof(SocketBuffer));
    while(currentChunk->currentSize < chunkLength)
    {
        if(currentChunk->currentSize >= currentChunk->currentMaxSize/2)
        {
            allocateBufferSpace(currentChunk);
        }
        bytesRead = recv(outgoingSocket,&currentChunk->buffer[currentChunk->currentSize],MAX_RECV_SIZE,0);
        if(bytesRead < 0)
        {
            fprintf(stderr,"DECHUNK RECV ERROR WHILE READING CHUNK\n");

            return -1;
        }
        currentChunk->currentSize+=bytesRead;
    }
    send(clientSocket,currentChunk->buffer,chunkLength,0);
    send(clientSocket,"\r\n",strlen("\r\n"),0);
    int numExtraBytes = currentChunk->currentSize - chunkLength;
    if(numExtraBytes > 2)
    {
        memcpy(data->data,&currentChunk->buffer[chunkLength+2],numExtraBytes-2);
        data->dataSize = numExtraBytes-2;
    }
    freeChunk(currentChunk);
    return handlePostChunked(outgoingSocket,data,clientSocket);
}

void handlePOSTResponse(int outgoingSocket,int clientSocket)
{
    SocketBuffer* data;
    ChunkBuffer* header = readHeader(outgoingSocket,&data);
    
    if(!header)
    {
        fprintf(stderr,"ERROR IN PARSING HEADER OF POST RESPONSE\n");
        close(outgoingSocket);
        close(clientSocket);
        return;
    }
    send(clientSocket,header->buffer,header->currentSize,0);
    long long size = readContentLength(header->buffer);
    if(size == -1 && strstr(header->buffer,": chunked"))
    {
        if(handlePostChunked(outgoingSocket,data,clientSocket) < 0)
        {
            fprintf(stderr,"DECHUNKING OF POST FAILED\n");
        }
        free(data);
        close(outgoingSocket);
        return;
    }
    freeChunk(header);
    long long result = data->dataSize;
    for(;;)
    {
        if(size != -1 && result == size)
        {
            send(clientSocket,data->data,data->dataSize,0);
            free(data);
            return;
        }
        int bytesRecv = recv(outgoingSocket,&data->data[data->dataSize],MAX_RECV_SIZE,0);
        if(size != -1)
            result += bytesRecv;
        if(bytesRecv == -1)
        {
            //send the client what we have if server times out
            printf("SERVER TIMEOUT ON POST\n");
            free(data);
            close(outgoingSocket);
            return;

        }
        // we finished server closed
        else if(!bytesRecv)
        {
            send(clientSocket,data->data,data->dataSize,0);
            free(data);
            close(outgoingSocket);
            return;
        }
        else
        {
            data->dataSize += bytesRecv;
            if(data->dataSize >= MAX_SEND_SIZE)
            {
                send(clientSocket,data->data,data->dataSize,0);
                data->dataSize = 0;
            } 
        }
    }
    
}



int getConnection(char* header)
{
    if(strstr(header,"Connection: keep-alive"))
    {
        return 1;
    }
    else
    {
        return 0;
    }
    
}

// mainLoop of the main thread accepts new connection and has a new thread handle the response
// proxyFD the socket for the proxy to accept new connections
void mainLoop(int proxyFD)
{
    struct sockaddr_storage clientAddress;
    socklen_t client_len = sizeof(clientAddress);
    pthread_t currentThread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    struct pollfd pollFD;
    pollFD.fd = proxyFD;
    pollFD.events = POLLIN;
    for(;;)
    {
        if(poll(&pollFD,1,-1) < 0)
        {
            printf("ERROR in POLL\n");
            return;
        }
        int clientSocket = accept(proxyFD,(struct sockaddr*)&clientAddress,&client_len);
        if(clientSocket == -1)
        {
            fprintf(stderr,"ERROR IN ACCEPT: Errno = %d\n",errno);
            continue;
        }
        //create thread here
        int* socketFD = (int*)malloc(sizeof(int));
        *socketFD = clientSocket;
        pthread_create(&currentThread,&attr,handleRequest,socketFD); 
        //handleRequest(socketFD);
    }
}


//initializes the clamAV engine
int initClam()
{
    if(cl_init(CL_INIT_DEFAULT) != CL_SUCCESS)
    {
        fprintf(stderr,"CLAM INIT FAILED\n");
        return -1;
    }
    if(!(engine = cl_engine_new()))
    {
        fprintf(stderr,"FAILED ENGINE CREATION\n");
        return -1;
    }
    unsigned int numSignatures = 0;
    printf("LOADING DATABASE\n");
    if(cl_load(cl_retdbdir(),engine,&numSignatures,CL_DB_STDOPT) != CL_SUCCESS)
    {
        fprintf(stderr,"DATABASE LOAD FAILED\n");
        return -1;
    }
    printf("Compiling Engine\n");
    if(cl_engine_compile(engine) != CL_SUCCESS)
    {
        fprintf(stderr,"ENGINE FAILED COMPILATION\n");
        return -1;
    }
    return 0;
}

int main()
{
    if(initClam() < 0)
    {
        return -1;
    }
    printf("CLAM INIT DONE\n");
    srand(time(0));
    int proxyFD = setupSocket();
    signal(SIGPIPE,SIG_IGN);
    struct stat st;
    if(stat("./responseMessages",&st) == -1)
        mkdir("./responseMessages",0755);
    if(proxyFD < 0)
    {
        fprintf(stderr,"SOCKET SETUP FAILED\n");
        return -1;
    }
    if(listen(proxyFD,MAX_LISTENING_CONNECTIONS) < 0)
    {
        fprintf(stderr, "ERROR LISTENING: Errno = %d\n",errno);
        return -1;
    }
    mainLoop(proxyFD);
}