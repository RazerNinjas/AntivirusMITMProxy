# Proxy Research Project UTD


## How to Setup the Proxy
### Libraries to install using Ubuntu
1. ```sudo apt-get install clamav```
1. ```sudo apt-get install libclamav-dev```
1. ```sudo apt-get install libz-dev```
1. ```sudo apt-get install libssl-dev```

Now run ```sudo freshclam``` to update the clamav engine

## Compiling and Running the C program
1. Within the proxy directory run ```make``` to execute the make file
1. Run the executable called proxy which executes the proxy the proxy will be running on port 8080

## Running the Python Program
1. Simply run MITM.py to start the proxy it will be running on port 8080

## Compiling the HTTPS MITM Python Proxy
**Note this setup works only for Ubuntu/Debian systems for adding the CA please check your system if different**
1.  Run ```sudo apt install python3-pip```
2.  Run ```pip3 install certauth```
3.  Run ```sudo apt install brotli```
4.  Run ```sudo cp cert/UTD.pem /usr/local/share/ca-certificates/UTD.crt``` to add the custom CA to your system trust
5.  Run ```sudo update-ca-certificates``` to add the CA


## Using the Proxy
The Python version of the Proxy supports HTTPS interception with TLS 1.3. The C version is capable of HTTP with interception but is unable to intercept HTTPS messages. It will operate as simply a passthrough proxy in those situations.


