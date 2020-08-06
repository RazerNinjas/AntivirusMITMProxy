#!/usr/bin/env/python3

import socket
import gzip
import shutil
import threading
import hashlib
import os
import select
import random
import CertificateCreator
import OpenSSL
import time
import subprocess
import ssl



# CONSTANTS FOR THE SEND AND RECV WINDOWS
MAX_RECV_SIZE = 1024
MAX_SEND_SIZE = 4096
MAX_FILE_NAME = 10000


class Server:
    """Server class to handle the proxy functions"""
    def __init__(self):
        """create the main server socket and do the bind and listen here"""
        self.ca = CertificateCreator.CertificateAuthority('UTD Proxy','cert/UTD.pem',cert_cache=50)
        self.serverSocket = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        self.serverSocket.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        self.serverSocket.bind(('localhost',8080))
        self.serverSocket.listen(50)
        self.lock = threading.Lock()
        self.fileNum = 0

    
    def run(self):
        """
        initialize client thread on accept new request
        """
        print("Ready to receive requests!")
        while True:
            (clientSocket,clientAddress) = self.serverSocket.accept()
            daemon = threading.Thread(target=self.handleRequest,args=(clientSocket,clientAddress))
            daemon.setDaemon(True)
            
            daemon.start()

    def readHeader(self,outgoingSocket):
        """we need to parse the header for chunking and gzip so separation of the header and body is needed

        return a tuple containing (header,leftover content from recv not part of the header)"""
        header = b''
        while True:
            try:
                data = outgoingSocket.recv(MAX_RECV_SIZE)
            except:
                return None
            if data.find(b"\r\n\r\n") == -1:
                header += data
                continue
            header += data[:data.find(b"\r\n\r\n")+4]
            return (header,data[(data.find(b"\r\n\r\n")+4):])
    
    # have the dechunking send the data to get things working
    def dechunkResponse(self,clientSocket,outgoingSocket,outputFile,data):
        chunkLengthEnd = data.find(b'\r\n')
        while chunkLengthEnd == -1:
            buffer = outgoingSocket.recv(MAX_RECV_SIZE)
            data += buffer
            chunkLengthEnd = data.find(b'\r\n')
        chunkLength = int(data[:chunkLengthEnd],base=16)
        if not chunkLength:
            clientSocket.send(b'0\r\n\r\n')
            return
        clientSocket.sendall(hex(chunkLength).encode())

        clientSocket.sendall(b'\r\n')
        currentChunk = data[data.find(b'\r\n')+2:data.find(b'\r\n')+2+chunkLength+2]
        data = data[data.find(b'\r\n')+2+chunkLength+2:]
        while len(currentChunk) < chunkLength + 2:
                currentChunk += outgoingSocket.recv(MAX_RECV_SIZE)
        if not data:
            data = currentChunk[chunkLength+2:]
        currentChunk = currentChunk[:chunkLength]
        clientSocket.sendall(currentChunk)
        outputFile.write(currentChunk)
        clientSocket.sendall(b'\r\n')
        self.dechunkResponse(clientSocket,outgoingSocket,outputFile,data)

    def nonchunkResponse(self,clientSocket,outgoingSocket,outputFile,data,length):
        
        clientSocket.sendall(data)
        outputFile.write(data)
        length -= len(data)
        data = b''
        while length:
            temp = outgoingSocket.recv(MAX_RECV_SIZE)
            length -= len(temp)
            data += temp
            if len(data) >= MAX_SEND_SIZE or length == 0:
                clientSocket.sendall(data)
                outputFile.write(data)
                data = b''

    def unzipFile(self,responseHash,outputFile):
        """UNZIPS THE ZIPPED ENCODING TO PARSE WITH CLAMAV

        returns the file object to the new unzipped file"""
        
        outputFile.close()
        compressedFile = gzip.open('./responseMessages/'+responseHash+'.gzip','rb')
        outputFile = open('./responseMessages/'+responseHash+'.txt','wb+')
        try:
            shutil.copyfileobj(compressedFile,outputFile)
        except:
            print("BAD ZIP")
            compressedFile.close()
            return outputFile
        
        compressedFile.close()
        os.remove('./responseMessages/'+responseHash+'.gzip')
        return outputFile
        
    def unBrFile(self,responseHash,outputFile):
        outputFile.close()
        subprocess.run(['brotli',"-d",'./responseMessages/'+responseHash + '.txt.br'])
        subprocess.run(['rm', './responseMessages/'+responseHash + '.txt.br'])
        return open('./responseMessages/'+responseHash + '.txt')

    def handleClientRequest(self,clientSocket,outgoingSocket,requestHash):
        result = self.readHeader(outgoingSocket)
        if not result:
            return
        header,data = result[0],result[1]
        clientSocket.send(header)
        if header.find(b'304 Not Modified') != -1 or header.find(b'204 No Content') != -1:
            return
        outputFile = None
        print(header.decode())
        isZip = 1 if header.find(b'Content-Encoding: gzip') != -1 or header.find(b'content-encoding: gzip') != -1 else 0
        isBr = 1 if header.find(b'Content-Encoding: br') != -1 or header.find(b'content-encoding: br') != -1 else 0
        if isZip:
            outputFile = open('./responseMessages/'+requestHash + '.gzip','wb+')
        elif isBr:
            outputFile = open('./responseMessages/'+requestHash + '.txt.br','wb+')
        else:
            outputFile = open('./responseMessages/'+requestHash + '.txt','wb+')
        if header.find(b'Transfer-Encoding: chunked') != -1 or header.find(b'transfer-encoding: chunked') != -1:
            self.dechunkResponse(clientSocket,outgoingSocket,outputFile,data)
        else:
            if header.find(b'Content-Length: ') != -1:
                length = int(header[header.find(b'Content-Length: ') + len(b'Content-Length: '):header.find(b'\r\n',header.find(b'Content-Length: '))],base=10)
            else:
                length = int(header[header.find(b'content-length: ') + len(b'content-length: '):header.find(b'\r\n',header.find(b'content-length: '))],base=10)
            self.nonchunkResponse(clientSocket,outgoingSocket,outputFile,data,length)
        if isZip:
            outputFile = self.unzipFile(requestHash,outputFile)
        elif isBr:
            outputFile = self.unBrFile(requestHash, outputFile)
        outputFile.close()

    def sendRequest(self,operation,clientSocket,request,outgoingSocket,data):
        """Sends request and calls the appropriate function to handle response"""
        try:
            outgoingSocket.sendall(request)
        except Exception as e:
            print(str(e))
            print("BAD REQUEST FOR HTTPS")
            print(request)
        if request.find(b'Content-Length: ') != -1:
            length = int(request[request.find(b'Content-Length: ') + len(b'Content-Length: '):request.find(b'\r\n',request.find(b'Content-Length: '))],base=10)
            length -= len(data)
            while length:
                buffer = clientSocket.recv(MAX_RECV_SIZE)
                length -= len(buffer)
                data += buffer
            outgoingSocket.sendall(data)
        print(request.decode())
        self.lock.acquire()
        requestHash = str(self.fileNum % MAX_FILE_NAME + 1)
        self.fileNum += 1
        self.lock.release()

        self.handleClientRequest(clientSocket,outgoingSocket,requestHash)
        outgoingSocket.close()
        clientSocket.close()

    def connectRelay(self,clientSocket,outgoingSocket):
        clientSocket.send("HTTP/1.1 200 Connection Established\r\n\r\n".encode())
        connectionInputs = [clientSocket,outgoingSocket]
        while True:
            result = select.select(connectionInputs,[],connectionInputs,10)
            readList, errorList = result[0],result[2]
            if errorList or not readList:
                print("Timeout ending SSL Tunnel session")
                outgoingSocket.close()
                clientSocket.close()
                break
            for availableConnection in readList:
                otherEnd = clientSocket if availableConnection is outgoingSocket else outgoingSocket
                data = availableConnection.recv(MAX_RECV_SIZE)
                # TO SUPPORT OLD CONNECTIONS
                if not data:
                    clientSocket.close()
                    outgoingSocket.close()
                    return
                otherEnd.sendall(data)
                

    def intercept(self,clientSocket,serverSocket,serverName,portNumber):
        context = ssl.create_default_context()
        serverSocket = context.wrap_socket(serverSocket,server_hostname=serverName)
        try:
            serverSocket.connect((serverName,portNumber))
        except Exception as e:
            print(str(e))
            print("FAILED")
            length = len('<h1>Connection Timed out</h1>')
            message = 'HTTP/1.1 403 Forbidden\r\nContent-Length: ' + str(length) + "\r\nConnection: close\r\n\r\n"
            clientSocket.sendall(message.encode())
            clientSocket.sendall(b'<h1>Connection Found Exception</h1>')
            clientSocket.close()
            return
        
        cert,private_key = self.ca.load_cert(serverName.decode())
        clientContext = OpenSSL.SSL.Context(OpenSSL.SSL.TLSv1_2_METHOD)
        clientContext.use_certificate(cert)
        clientContext.use_privatekey(private_key)
        clientSocket.send("HTTP/1.1 200 Connection Established\r\n\r\n".encode())
        clientSocket = OpenSSL.SSL.Connection(clientContext,clientSocket)
        clientSocket.set_accept_state()
        result = self.readHeader(clientSocket)
        if not result:
            serverSocket.close()
            clientSocket.close()
            return
        request,data = result[0],result[1]
        operation = request.split(b'\n')[0].split(b' ')[0]
        self.sendRequest(operation,clientSocket,request,serverSocket,data)
        

    
    def handleRequest(self,clientSocket,clientAddress):
        """request handler parses the request message to connect to the remote server then calls the send request message"""
        request,data = self.readHeader(clientSocket)
        firstLine = request.split(b'\n')[0]
        url = firstLine.split(b' ')[1]
        operation = firstLine.split(b' ')[0]
        startLocation = url.find(b'://')
        if startLocation != -1:
            url = url[(startLocation+3):]
        portPosition = url.find(b":")
        resourceLocation = url.find(b"/")
        if resourceLocation == -1:
            resourceLocation = len(url)
        if portPosition == -1:
            if operation == b'CONNECT':
                portNumber = 443
            else:
                portNumber = 80
            serverName = url[:resourceLocation]
        else:
            portNumber = int(url[portPosition+1:resourceLocation])
            serverName = url[:portPosition]
        
        s = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        
        if operation == b'CONNECT':
            #self.connectRelay(clientSocket,s)
            self.intercept(clientSocket,s,serverName,portNumber)
        else:
            try:
                s.settimeout(15)
                s.connect((serverName,portNumber))
            except Exception as e:
                print(str(e))
                print("URL WAS " + url.decode())
                print("CONNECTION TIMED OUT!!")
                length = len('<h1>Connection Timed out</h1>')
                message = 'HTTP/1.1 403 Forbidden\r\nContent-Length: ' + str(length) + "\r\nConnection: close\r\n\r\n"
                clientSocket.sendall(message.encode())
                clientSocket.sendall(b'<h1>Connection Found Exception</h1>')
                clientSocket.close()
                return
            self.sendRequest(operation,clientSocket,request,s,data)


if __name__ == '__main__':    
    server = Server()
    server.run()


