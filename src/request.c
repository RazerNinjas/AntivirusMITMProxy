#include "proxy.h"

//return NULL if error parses request from browser and returns the header information
SocketBuffer* parseRequest(int socketFD)
{
    SocketBuffer* request = (SocketBuffer*)malloc(sizeof(SocketBuffer));
    memset(request,0,sizeof(SocketBuffer));
    for(;;)
    {
        int lengthRecv = recv(socketFD,&request->data[request->dataSize],MAX_RECV_SIZE,0);
        request->dataSize += lengthRecv;
        if(lengthRecv < 0)
        {
            free(request);
            return NULL;
        }
        if(request->dataSize > sizeof(request->data))
        {
            fprintf(stderr,"SIZE OF BUFFER IS %d\n",request->dataSize);
            fprintf(stderr,"BUFFER OVERFLOW:\n");
            free(request);
            return NULL;
        }
        if(strstr(request->data,"\r\n\r\n") != NULL)
        {
            request->data[request->dataSize] = 0;
            return request;
        }
            
    }
    
}

//parses the URL and extracts the port number and the hostname
// the result is a struct which is dynamically allocated and has dynamically allocated info for the hostname
Connection* parseURL(char* url)
{
    char* urlCopy = (char*)malloc(strlen(url)+1);
    strcpy(urlCopy,url);
    if(strstr(urlCopy,"http://") == urlCopy)
    {
        char* hostnameStart = urlCopy+strlen("http://");
        if(!isalnum(*hostnameStart))
        {
            fprintf(stderr,"BAD HOSTNAME\n");
            free(urlCopy);
            return NULL;
        }
        // IF NO PORT
        char* colonStart = strstr(hostnameStart,":");
        char* queryStart = strstr(hostnameStart,"?");
        if(colonStart && queryStart && queryStart < colonStart)
            colonStart = NULL;
        if(colonStart == NULL)
        {
            char* portNumberEnd = strstr(hostnameStart,"/");
            if(portNumberEnd == NULL)
            {
                fprintf(stderr,"BADLY FORMED HTTP HEADER\n");
                free(urlCopy);
                return NULL;
            }
            *portNumberEnd = 0;
            Connection* socketConnection = allocateConnection(hostnameStart,"80");
            free(urlCopy);
            return socketConnection;
        }
        else
        {
            
            char* portNumberStart = colonStart + 1;
            char* portNumberEnd = strstr(hostnameStart,"/");
            *colonStart = 0;
            if(portNumberEnd == NULL)
            {
                fprintf(stderr,"BADLY FORMED HTTP HEADER\n");
                free(urlCopy);
                return NULL;
            }
            *portNumberEnd = 0;
            Connection* socketConnection = allocateConnection(hostnameStart,portNumberStart);
            free(urlCopy);
            return socketConnection;
            
        }
         
    }
    else
    {
        puts(urlCopy);
        fprintf(stderr,"MUST USE HTTP PROTOCOL\n");
        if(urlCopy)
            free(urlCopy);
        return NULL;
    }
        
}


void handlePersistantRequest(int clientSocket, int serverSocket)
{  
    SocketBuffer* leftoverData;
    ChunkBuffer* request = readHeader(clientSocket,&leftoverData);
    if(!request)
    {
        printf("Closing Persistant Connection\n");
        close(clientSocket);
        close(serverSocket);
        return;
    }
    ChunkBuffer* data = extractPostData(clientSocket,leftoverData,readContentLength(request->buffer));
    free(leftoverData);
    int isGet;
    if(strstr(request->buffer,"GET") == request->buffer)
        isGet = 1;
    else
    {
        isGet = 0;
    }
    sendRequest(serverSocket,clientSocket,request,isGet,data);
    

}

//if operation is GET return true else (POST) return false
int parseOperation(char* operation)
{
    if(strcmp(operation, "GET"))
    {
        return 0;
    }
    return 1;
}

//be sure to free data after calling function
ChunkBuffer* extractPostData(int socketFD, SocketBuffer* data, long long length)
{
    if(length < 0)
    {
        return NULL; 
    }
    ChunkBuffer* dataBuffer = chunkBufferInit();
    memcpy(dataBuffer->buffer,data->data,data->dataSize);
    dataBuffer->currentSize = data->dataSize;
    int bytesRead;
    while(dataBuffer->currentSize < length)
    {
        if(dataBuffer->currentSize >= dataBuffer->currentMaxSize/2)
        {
            allocateBufferSpace(dataBuffer);
        }
        bytesRead = recv(socketFD,&dataBuffer->buffer[dataBuffer->currentSize],MAX_RECV_SIZE,0);
        dataBuffer->currentSize+=bytesRead;
    }
    return dataBuffer;
}


//parses the request and connects to the server and calls functions that handle the rest
// the argument passed is the client socket FD but format is needed for the thread
//once we have finished dyamically allocated parts be sure to delete them
//(For the future add a best request http message to tell client of error)
void* handleRequest(void* arguments)
{
    int clientSocket = *((int*)arguments);
    free(arguments);
    struct timeval timeout;
    memset(&timeout,0,sizeof(struct timeval));  
    timeout.tv_sec = 10;
    setsockopt (clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    SocketBuffer* leftoverData;
    ChunkBuffer* request = readHeader(clientSocket,&leftoverData);
    if(!request)
    {
        close(clientSocket);
        return NULL;
    }
    ChunkBuffer* data = extractPostData(clientSocket,leftoverData,readContentLength(request->buffer));
    free(leftoverData);
    
    //BE SURE TO FREE IF ERROR OR WHEN DONE PARSING URL AND OPERATION
    char* requestCopy = (char*) malloc(request->currentSize+1);
    strcpy(requestCopy,request->buffer);
    char* savedChar;
    char* firstLine = strtok_r(requestCopy,"\r\n",&savedChar);
    char* operationCopy = strtok_r(firstLine, " ", &savedChar);
    if(!operationCopy)
    {
        fprintf(stderr,"ERROR IN PARSING REQUEST HEADER\n");
        free(requestCopy);
        freeChunk(request);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;
    }
    char* urlCopy = strtok_r(NULL, " ", &savedChar);
    if(!urlCopy)
    {
        fprintf(stderr,"ERROR IN PARSING REQUEST HEADER URL\n");
        free(requestCopy);
        freeChunk(request);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;
    }

    //FREE WHEN ERROR BEFORE NORMAL FREE OR WHEN DONE WITH PARSING HOSTNAME
    char* url = (char*)malloc(strlen(urlCopy)+1);
    //FREE WHEN ERROR BEFORE NORMAL FREE OR WHEN DONE WITH PARSING OPERATION
    char* operation = (char*)malloc(strlen(operationCopy)+1);
    strcpy(url,urlCopy);
    strcpy(operation,operationCopy);
    free(requestCopy);
    //DYNAMICALLY ALLOCATED INFO BE SURE TO CALL FREE FUNCTION IF ERROR OR WHEN DONE WITH getaddrinfo
    int isGET = parseOperation(operation);
    //IF we get the operation future communication can be easily done with https instead of http
    Connection* serverSockInfo = parseURL(url);
    
    free(url);
    free(operation);
    if(!serverSockInfo)
    {
        fprintf(stderr,"PARSE FAILED\n");
        freeChunk(request);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;
    }
    printf("HOSTNAME: %s PORT: %s\n",serverSockInfo->hostname,serverSockInfo->port);
    struct addrinfo hints;
    memset(&hints,0,sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* serverInfo;
    if(getaddrinfo(serverSockInfo->hostname,serverSockInfo->port,&hints,&serverInfo))
    {
        freeaddrinfo(serverInfo);
        fprintf(stderr,"getaddrinfo() failed\n\n");
        freeChunk(request);
        freeConnection(serverSockInfo);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;
    }

    freeConnection(serverSockInfo);
    //BE SURE TO CLOSE IF ERROR
    int outgoingSocket = socket(serverInfo->ai_family,serverInfo->ai_socktype,serverInfo->ai_protocol);
    if(outgoingSocket < 0)
    {
        fprintf(stderr,"SERVER SOCKET CREATION FAILED\n");
        freeChunk(request);
        freeaddrinfo(serverInfo);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;

    }
    timeout.tv_sec = 20;
    if (setsockopt (outgoingSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        fprintf(stderr,"SOCKOPT FAILED TIMEOUT\n");
        freeaddrinfo(serverInfo);
        freeChunk(request);
        close(outgoingSocket);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;

    }

    if (setsockopt (outgoingSocket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        fprintf(stderr,"SOCKOPT FAILED TIMEOUT\n");
        freeaddrinfo(serverInfo);
        freeChunk(request);
        close(outgoingSocket);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;

    }
    if(connect(outgoingSocket,serverInfo->ai_addr,serverInfo->ai_addrlen))
    {
        fprintf(stderr,"CONNECT() FAILED\n");
        char forbidden[] = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n";
        send(clientSocket,forbidden,strlen(forbidden),0);
        freeaddrinfo(serverInfo);
        freeChunk(request);
        close(outgoingSocket);
        close(clientSocket);
        if(data)
            freeChunk(data);
        return NULL;
    }
    freeaddrinfo(serverInfo);
    sendRequest(outgoingSocket,clientSocket,request,isGET,data);
    return NULL;

}



//sends the request data as well as creates the filename and calls functions to get the response info
void sendRequest(int outgoingSocket,int clientSocket,ChunkBuffer* request,int isGet, ChunkBuffer* data)
{
    puts(request->buffer);
    int connection = getConnection(request->buffer);
    if(send(outgoingSocket,request->buffer,request->currentSize,0) == -1)
    {
        fprintf(stderr,"ERROR IN SEND: ERRNO = %d\n",errno);
        close(outgoingSocket);
        //MAYBE ADD ERROR MSG TO CLIENT SOCKET LATER
        close(clientSocket);
        freeChunk(request);
        if(data)
            freeChunk(data);
        return;
    }
    if(data)
    {
        puts(data->buffer);
        send(outgoingSocket,data->buffer,data->currentSize,0);
        freeChunk(data);
    }
    char hashName[HASH_FILE_NAME_LENGTH];
    memset(hashName,0,HASH_FILE_NAME_LENGTH);
    fileNameHash(request->buffer,hashName);
    freeChunk(request);
    if(isGet)
    {
        handleGETResponse(outgoingSocket,clientSocket,hashName,connection);
    }
    else
    {
        handlePOSTResponse(outgoingSocket,clientSocket);
        close(clientSocket);
    }
    
}