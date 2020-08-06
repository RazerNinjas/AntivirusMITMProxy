#include "proxy.h"

int dechunkResponse(int outgoingSocket,FILE* resultFile,SocketBuffer* data,int clientSocket)
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
            fprintf(stderr,"DECHUNK BUFFER OVERFLOW\n");
            return -1;
        }
        foundEnd = strstr(data->data,"\r\n");
    }
    int chunkLength = strtol(data->data,NULL,16);
    if(!chunkLength)
    {
        return 0;
    }
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
    fwrite(currentChunk->buffer,sizeof(char),chunkLength,resultFile);
    int numExtraBytes = currentChunk->currentSize - chunkLength;
    if(numExtraBytes > 2)
    {
        memcpy(data->data,&currentChunk->buffer[chunkLength+2],numExtraBytes-2);
        data->dataSize = numExtraBytes-2;
    }
    freeChunk(currentChunk);
    return dechunkResponse(outgoingSocket,resultFile,data,clientSocket);
}

long long readContentLength(char* header)
{
    //puts(header);
    char* start = strstr(header,"Content-Length: ");
    if(!start)
        return -1;
    start += strlen("Content-Length: ");
    return strtoll(start,NULL,10);
    
}

// Unzips the file takes the result file as the new file to be written to and the file string for the name of the zipped file
// writes unzipped version to the result file
void unzipFile(char* fileName,FILE* resultFile,char* fileString)
{
    char unzippedFileString[HASH_FILE_NAME_LENGTH*2] = "./responseMessages/" ;
    strcat(unzippedFileString,fileName);
    strcat(unzippedFileString,".txt");
    SocketBuffer* zipBuffer = (SocketBuffer*)malloc(sizeof(SocketBuffer));
    zipBuffer->dataSize = 0;
    resultFile = fopen(unzippedFileString,"wb+");
    gzFile zippedFile = gzopen(fileString,"rb");
    int numBytes;
    for(;;)
    {
        numBytes = gzread(zippedFile,&zipBuffer->data[zipBuffer->dataSize],MAX_RECV_SIZE);
        if(!numBytes)
        {
            fwrite(zipBuffer->data,sizeof(char),zipBuffer->dataSize,resultFile);
            free(zipBuffer);
            remove(fileString);
            break;
        }
        zipBuffer->dataSize+=numBytes;
        if(zipBuffer->dataSize >= MAX_SEND_SIZE)
        {
            fwrite(zipBuffer->data,sizeof(char),zipBuffer->dataSize,resultFile);
            zipBuffer->dataSize = 0;
        }

     }
    fclose(resultFile);
    gzclose(zippedFile);
}

int handleNonChunkResponse(int outgoingSocket,FILE* resultFile,SocketBuffer* data,int clientSocket,char* header)
{

    long long result = data->dataSize;
    long long size = readContentLength(header);
    for(;;)
    {
        if(size != -1 && result == size)
        {
            fwrite(data->data,sizeof(char),data->dataSize,resultFile);
            return 0;
        }
        int bytesRecv = recv(outgoingSocket,&data->data[data->dataSize],MAX_RECV_SIZE,0);
        if(size != -1)
            result += bytesRecv;
        if(bytesRecv == -1)
        {
            //send the client what we have if server times out
            printf("SERVER TIMEOUT\n");
            return -1;

        }
        // we finished server closed
        else if(!bytesRecv)
        {
            fwrite(data->data,sizeof(char),data->dataSize,resultFile);
            return 0;
        }
        else
        {
            data->dataSize += bytesRecv;
            if(data->dataSize >= MAX_SEND_SIZE)
            {

                fwrite(data->data,sizeof(char),data->dataSize,resultFile);
                data->dataSize = 0;
            } 
        }
    }
}



ChunkBuffer* readHeader(int outgoingSocket,SocketBuffer** leftoverData)
{
    int bytesRead;
    ChunkBuffer* getResponse = chunkBufferInit();
    for(;;)
    {
        bytesRead = recv(outgoingSocket,&getResponse->buffer[getResponse->currentSize],MAX_RECV_SIZE,0);
        
        if(bytesRead == -1 || !bytesRead)
        {
            //fprintf(stderr,"ERROR READING HEADER\n");
            freeChunk(getResponse);
            return NULL;
        }
        getResponse->currentSize+=bytesRead;
        char* headerEnd = strstr(getResponse->buffer,"\r\n\r\n");
        
        if(headerEnd != NULL)
        {
            
            
            SocketBuffer* data = (SocketBuffer*) malloc(sizeof(SocketBuffer));
            memset(data,0,sizeof(SocketBuffer));
            int initialPosition = (headerEnd+4)-getResponse->buffer;
            for(char* i = headerEnd+4; i < getResponse->buffer+getResponse->currentSize;i++)
            {
                data->data[data->dataSize++] = *i;
            }
            
            *leftoverData = data;
            //printf("IM HERE\n");
            ChunkBuffer* header = chunkBufferInit();
            for(char* start = getResponse->buffer; start != headerEnd+4;start++)
            {
                header->buffer[header->currentSize++] = *start;
                if(header->currentSize >= header->currentMaxSize/2)
                {
                    allocateBufferSpace(getResponse);
                }
            }
            freeChunk(getResponse);
            return header;
        }
        if(getResponse->currentSize >= getResponse->currentMaxSize/2)
        {
            fprintf(stderr,"HEADER TOO LONG!!! NEED MEMORY\n");
            allocateBufferSpace(getResponse);
            //puts(getResponse->data);
        }
    }
}



//Gets the Content Type field from the header and stores a copy of it
char* getContentType(char* header)
{
    char* contentTypeCopy = strstr(header,"Content-Type: ");
    if(!contentTypeCopy)
        return NULL;
    char* end = strstr(contentTypeCopy,"\r\n");
    if(!end)
        return NULL;
    *end = 0;
    // +5 since \r\n\r\n and NULL 
    char* contentType = (char*)malloc(strlen(contentTypeCopy)+5);
    strcpy(contentType,contentTypeCopy);
    strcat(contentType,"\r\n");
    return contentType;
}

//Scans file and if virus stores result in virus name
int virusScan(char* fileName, const char* virus)
{
    struct cl_scan_options options;
    memset(&options,0,sizeof(struct cl_scan_options));
    int result;
    result = cl_scanfile(fileName,&virus,NULL,engine,&options);
    if(result == CL_VIRUS)
    {
        printf("VIRUS FILE in %s\n",fileName);
        return 1;
    }
    else if(result == CL_CLEAN)
    {
        printf("CLEAN FILE %s\n",fileName);
        return 0;
    }
    else
    {
        printf("ERROR in virus scan\n");
        return -1;
    }
}

//sends OK response header and content from the unzipped file (the original request may have been chunked and/or zipped)
void sendOKResponse(int clientSocket, char* contentType,char* fileName, int connection)
{
    send(clientSocket,"HTTP/1.1 200 OK\r\n",strlen("HTTP/1.1 200 OK\r\n"),0);
    if(contentType)
    {
        send(clientSocket,contentType,strlen(contentType),0);
    }
    if(connection)
    {
        send(clientSocket,"Connection: keep-alive\r\nTransfer-Encoding: chunked\r\n\r\n",strlen("Connection: keep-alive\r\nTransfer-Encoding: chunked\r\n\r\n"),0);
    }
    else
    {
        send(clientSocket,"Connection: close\r\nTransfer-Encoding: chunked\r\n\r\n",strlen("Connection: close\r\nTransfer-Encoding: chunked\r\n\r\n"),0);
    }
    
    FILE* output = fopen(fileName,"rb");
    SocketBuffer* outputBuffer = (SocketBuffer*)malloc(sizeof(SocketBuffer));
    if(!outputBuffer)
    {
        fprintf(stderr,"FAILED TO MALLOC\n");
        return;
    }
    outputBuffer->dataSize = 0;
    int numBytes;
    char hexBuffer[100];
    for(;;)
    {
        numBytes = fread(&outputBuffer->data[outputBuffer->dataSize],sizeof(char),MAX_RECV_SIZE,output);
        if(!numBytes)
        {
            if(outputBuffer->dataSize)
            {
                sprintf(hexBuffer,"%X",outputBuffer->dataSize);
                send(clientSocket,hexBuffer,strlen(hexBuffer),0);
                send(clientSocket,"\r\n",strlen("\r\n"),0);
                send(clientSocket,outputBuffer->data,outputBuffer->dataSize,0);
                send(clientSocket,"\r\n",strlen("\r\n"),0);
            }
                
            send(clientSocket,"0\r\n\r\n",strlen("0\r\n\r\n"),0);
            free(outputBuffer);
            fclose(output);
            return;
        }
        outputBuffer->dataSize += numBytes;
        if(outputBuffer->dataSize >= MAX_SEND_SIZE)
        {

            sprintf(hexBuffer,"%X",outputBuffer->dataSize);
            send(clientSocket,hexBuffer,strlen(hexBuffer),0);
            send(clientSocket,"\r\n",strlen("\r\n"),0);      
            if(send(clientSocket,outputBuffer->data,outputBuffer->dataSize,0) < 0)
            {
                free(outputBuffer);
                fclose(output);
                return;
            }
            send(clientSocket,"\r\n",strlen("\r\n"),0);
            outputBuffer->dataSize = 0;
        }
    }
    
}

//Virus confirmed send a bad response to client
void sendBadResponse(int clientSocket, const char* virusName)
{
    char response[MAX_SEND_SIZE+MAX_RECV_SIZE];
    strcpy(response,"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: close\r\n");
    strcat(response,"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>MALWWARE DETECTED</title></head><body><h1>MALWARE DETECTED ON THE RESPONSE</h1><h2>MALWARE NAME: ");
    send(clientSocket,response,strlen(response),0);
    send(clientSocket,virusName,strlen(virusName),0);
    send(clientSocket,"</h2></body></html>",strlen("</h2></body></html>"),0);
}

//main worker function calls functions to process the response
void handleGETResponse(int outgoingSocket, int clientSocket, char fileName[HASH_FILE_NAME_LENGTH],int clientConnection)
{
    SocketBuffer* data;
    ChunkBuffer* header = readHeader(outgoingSocket,&data);
    
    if(!header)
    {
        fprintf(stderr,"ERROR IN PARSING HEADER OF RESPONSE\n");
        close(outgoingSocket);
        close(clientSocket);
        return;
    }
    int connection = getConnection(header->buffer);
    char* firstLineEnd = strstr(header->buffer,"\r\n");
    char* notModified = strstr(header->buffer,"304 Not Modified\r\n");
    if(notModified && notModified < firstLineEnd)
    {
        printf("DATA CACHED ON CLIENT\n");
        free(data);
        send(clientSocket,header->buffer,header->currentSize,0);
        freeChunk(header);
        if(clientConnection && connection)
        {
            
            handlePersistantRequest(clientSocket,outgoingSocket);
            return;
        }
        else
        {
            close(clientSocket);
            close(outgoingSocket);
            return;
        }
    }
    int isZip = 0;
    char fileString[HASH_FILE_NAME_LENGTH*2] = "./responseMessages/" ;
    strcat(fileString,fileName);
    if(strstr(header->buffer,"Content-Encoding: gzip") != NULL || strstr(header->buffer,"Transfer-Encoding: gzip") != NULL)
    {
        strcat(fileString,".gz");
        isZip = 1;
    }
    else
    {
        strcat(fileString,".txt");
    }
    FILE* resultFile = fopen(fileString,"wb+");
    if(strstr(header->buffer,"Transfer-Encoding: chunked") != NULL)
    {
        if(dechunkResponse(outgoingSocket,resultFile,data,clientSocket))
        {
            fprintf(stderr,"DECHUNK ERROR\n");
            fclose(resultFile);
            remove(fileString);
            close(outgoingSocket);
            close(clientSocket);
            puts(header->buffer);
            freeChunk(header);
            free(data);
            return;
        }
    }
    else
    {
        if(handleNonChunkResponse(outgoingSocket,resultFile,data,clientSocket,header->buffer))
        {
            fprintf(stderr,"ERROR IN NON CHUNKED RESPONSE\n");
            close(outgoingSocket);
            close(clientSocket);
            fclose(resultFile);
            free(data);
            freeChunk(header);
            remove(fileString);
            return;
        }
    }
    //NEED CONTENT STRING
    char* contentType = getContentType(header->buffer);
    freeChunk(header);
    free(data);
    if(!connection || !clientConnection)
        close(outgoingSocket);
    fclose(resultFile);
    
    if(isZip)
    {
        unzipFile(fileName,resultFile,fileString);
    }
    char finalFileName[HASH_FILE_NAME_LENGTH*2] = "./responseMessages/";
    strcat(finalFileName,fileName);
    strcat(finalFileName,".txt");
    const char* virusName;
    int resultScan = virusScan(finalFileName,virusName);
    if(resultScan == VIRUS)
    {
        sendBadResponse(clientSocket,virusName);
        if(contentType)
            free(contentType);
        remove(finalFileName);
        return;
    }
    else if(resultScan == CLEAN)
    {
        sendOKResponse(clientSocket,contentType,finalFileName, connection);
        if(contentType)
        free(contentType);
        remove(finalFileName);
        if(connection && clientConnection)
        {
            handlePersistantRequest(clientSocket,outgoingSocket);
        }
        else
        {
            close(clientSocket);
        }
        
    }

}