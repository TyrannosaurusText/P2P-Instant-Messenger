#include "p2pim.h"


#define MAX_UDP_MSG_LEN (10 + 254 + 32 + 2)

#define terminalFDPOLL 0
#define udpFDPOLL 1
#define tcpFDPOLL 2

enum Option {USERNAME, UDP_PORT, TCP_PORT, MIN_TIMEOUT, MAX_TIMEOUT, HOST};

static std::unordered_map<std::string, int> optionMap {
    {"-u", USERNAME}, {"-up", UDP_PORT}, {"-tp", TCP_PORT}, 
    {"-dt", MIN_TIMEOUT}, {"-dm", MAX_TIMEOUT}, {"-pp", HOST}
};


struct Client {
    std::string userName;
    std::string hostName;
    int udpPort;
    int tcpPort;
    sockaddr_in tcpClientAddr;
    int tcpSockFd = -1;
    int block = 0;
};

struct Host {
    std::string hostName;
    int portNum;
};



std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int minTimeout = 5000;
int maxTimeout = 60000;
// int destPort = ls
std::string optErr;
struct in_addr tmpIPAddr, tmpMask;


uint8_t outgoingUDPMsg[MAX_UDP_MSG_LEN];
int outgoingUDPMsgLen;
std::unordered_map<std::string, struct Client> clientMap;
std::unordered_map<int, std::string> tcpConnMap;

int udpSockFd, tcpSockFd, enable = 1;
struct sockaddr_in udpServerAddr, udpClientAddr, tcpServerAddr, tcpClientAddr;
socklen_t udpClientAddrLen, tcpClientAddrLen; 
// struct pollfd pollFd[2];
std::vector<struct pollfd> pollFd(3);

std::vector<struct Host> unicastHosts;

int away = 0;


std::unordered_map<std::string, struct Client>::iterator findClient(uint8_t* incomingUDPMsg);


int main(int argc, char** argv) {
    // Setup signal handler
    if(signal(SIGINT, SIGINT_handler) == SIG_ERR)
        die("Failed to catch signal");

    // findIPNMask();
    parseOptions(argc, argv);
    initUDPMsg();
    setupSocket();

    // dprint("Username = %s\n", userName.c_str());
    // // TODO: Clientname = sp4.cs.ucdavis.edu
    // dprint("Clientname = %s\n", hostName.c_str());
    // dprint("UDP Port = %d\n", udpPort);
    // dprint("TCP Port = %d\n", tcpPort);
    // dprint("Mintimeout = %d\n", minTimeout);
    // dprint("Maxtimeout = %d\n", maxTimeout);


    pollFd[udpFDPOLL].fd = udpSockFd;
    pollFd[udpFDPOLL].events = POLLIN;
    pollFd[tcpFDPOLL].fd = tcpSockFd;
    pollFd[tcpFDPOLL].events = POLLIN;
    int baseTimeout = minTimeout;
    timeval start,end;
    int timePassed;
    int currTimeout = 0;


    // When no host is available and maxTimeout not exceeded, discovery hosts
    while(baseTimeout <= maxTimeout * 1000) {
        
        if(currTimeout <= 0) {
            if(clientMap.empty()) {
                if(!unicastHosts.empty()) {
                    dprint("size is %d\n", unicastHosts.size());
                    for(auto h : unicastHosts) {
                        // Resolve dns
                        struct hostent* remoteHostEntry = gethostbyname(h.hostName.c_str());
                        if(!remoteHostEntry) {
                            die("Failed to resolve host");
                        }

                        struct in_addr remoteAddr;
                        // char buf[256];

                        memcpy(&remoteAddr, remoteHostEntry->h_addr, remoteHostEntry->h_length);
                        // inet_ntop(AF_INET, &remoteAddr, buf, INET_ADDRSTRLEN);

                        // struct sockaddr_in addr2Send;
                        // addr2Send.sin_addr = htonl(remoteAddr.sin_addr);
                        // addr2Send.sin_port = htons(h.portNum);
                        // addr2Send.sa_family = AF_INET;
                        udpServerAddr.sin_addr = remoteAddr;
                        udpServerAddr.sin_port = htons(h.portNum);
                        // addr2Send.sa_family = AF_INET;

                        // sendUDPMessage(DISCOVERY);
                    }

                    // unicastHosts.clear();
                }
                else {
                    udpServerAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
                }
                
                currTimeout = baseTimeout;
                sendUDPMessage(DISCOVERY);

            }
            else
                currTimeout = -1;
        }


        // Wait for reply message
        gettimeofday(&start, NULL);
        //TODO: there is a potential bug, where currTimeout is set to 0 and poll returns immediately
        int rc = poll(pollFd.data() + udpFDPOLL, pollFd.size() - 1, currTimeout);
        gettimeofday(&end, NULL);
        timePassed = ((end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec) / 1000;
        currTimeout -= timePassed;

        // Timeout event
        if(0 == rc) {
            dprint("Next iteration at %d\n", currTimeout);
            if(clientMap.empty()) {
                dprint("TIMEOUT: ", 0);

                // Double current timeout
                baseTimeout *= 2;
                dprint("%d\n", currTimeout);
            }
        }
        else if(rc > 0) {
            // dprint("Something is available\n", 0);

            std::string newClientName, newUserName;

            checkUDPPort(baseTimeout, currTimeout);
            // new TCP connection
			checkTCPPort(newClientName);
            
            // TCP packet
			checkConnections();

        }
        else
            dprint("ERROR\n", 0);
    }

    /*
    struct termios SavedTermAttributes;
    char* RX[4];
    std::string buffer = "";
	pollFd[terminalFDPOLL].fd = STDIN_FILENO;
	pollFd[terminalFDPOLL].events = POLLIN;
	std::string message = "";
	SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    int bytesRead, retpoll;
	printf(">");
	struct winsize size;
	ioctl(STDOUT_FILENO,TIOCGWINSZ,&size);
	int numcol = size.ws_col;
	fflush(STDIN_FILENO);
    while(1){
		retpoll = poll(pollFd.data(), 4, 1000); // to edit
		//dprint("%d\n",retpoll);
		if(retpoll == 0) {
			//dprint("%d", buffer.length());
			
		}
		if(pollFd[terminalFDPOLL].events & POLLIN) {
			int bytesRead = read(STDIN_FILENO, &RX, 4);
			if (bytesRead < 2)
			buffer.append((const char*)RX);
			//dprint("buffer len: %d\n", buffer.length());
			
			//dprint("buffer: %c\n", buffer[0]);
			if(bytesRead == 1) {
				//printf("%c",buffer[0]);
				fflush(STDIN_FILENO);
				
				if(buffer[0] == '\n') { //send message
					printf("\r%s>%s\n",userName.c_str(), message.c_str());
					//TODO: Actually proccess message
					message.clear();
					printf("\n\033[1B");
				}
				else if(buffer[0] == 0x7F) { //delete char
					if(message.length() > 0) {
						message.erase(message.length()-1);
						printf("\033[1D  "); //clears current and next char in terminal
						
					}
				}
				else if(message.length() < 512){
					message += buffer[0];
					//eraselines(message.length()/numcol);
				}
			}
			printf("\r\b\b\n");
			printf(">%s ", message.c_str(), message.length());
			
			if(message.length()+1 > numcol) //simulate loop
				printf("\b\r%s", message.substr(message.length()-numcol, numcol).c_str());
			else
				printf("\b\r>%s", message.c_str());
		
			fflush(STDIN_FILENO);
			buffer.clear();
		}
    }
    
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes); 
    */

    return 0;
}


/*
printf("\033[XA"); // Move up X lines;
printf("\033[XB"); // Move down X lines;
printf("\033[XC"); // Move right X column;
printf("\033[XD"); // Move left X column;
printf("\033[2J"); // Clear screen
*/
void optionError(char** argv){
    fprintf(stderr, "%s: option requires an argument -- '%s'\n", argv[0], optErr.c_str());
    exit(1);
}


// void findIPNMask() {
//     char buf[256];


// }
void getClientName()
{
    char buffer[256];
    
    if(-1 == gethostname(buffer, 255)){
        die("Unable to find local host name.");
    }

    struct hostent* localHostEntry = gethostbyname(buffer);

    if(!localHostEntry) {
        die("Unable to resolve local host.");
    }

    // struct in_addr tmpIPAddr, tmpMask;
    int found = 0;

    hostName = localHostEntry->h_name;

    memcpy(&tmpIPAddr, localHostEntry->h_addr, localHostEntry->h_length);

    struct ifaddrs *currentIFAddr, *firstIFAddr;

    if(0 > getifaddrs(&firstIFAddr)) {
        die("Failed to get ifaddr.");
    }

    currentIFAddr = firstIFAddr;

    do {
        if(AF_INET == currentIFAddr->ifa_addr->sa_family) {
            if(0 == memcmp(&((struct sockaddr_in*)currentIFAddr->ifa_addr)->sin_addr, &tmpIPAddr, localHostEntry->h_length)) {
                memcpy(&tmpMask, &((struct sockaddr_in*)currentIFAddr->ifa_netmask)->sin_addr, localHostEntry->h_length);
                found = 1;
                break;
            }
        }

        currentIFAddr = currentIFAddr->ifa_next;
    } while(currentIFAddr);

    freeifaddrs(firstIFAddr);
    
    if(!found) {
        fprintf(stderr, "Failed to find subnet mask.");
        exit(1);
    }
}

// Internal syscall errors
void die(const char *message) {
    perror(message);
    shutdown(udpSockFd, 0);
    exit(1);
}

int getType(uint8_t* message) {
    return ntohs(*((uint16_t*)message + 2));
}

void getClientNUserName(uint8_t* message, std::string& hostName, std::string& userName) {
    hostName = (char*)(message + 10);
    userName = (char*)(message + 10 + hostName.length() + 1);
}

void getPorts(uint8_t* message, int& udpPort, int& tcpPort) {
    udpPort = ntohs(*((uint16_t*)message + 3));
    tcpPort = ntohs(*((uint16_t*)message + 4));
}

void dump(std::string msg) {
    for(int i = 0; i < msg.length(); i++)
    {
        dprint("%d %c \n", msg[i], msg[i]);
    }
    dprint("\n", 0);
}

void checkIsNum(const char* str) {
    for(int i = 0; i < strlen(str); i++) {
        if(!isdigit(str[i])) {
            fprintf(stderr, "Input %s is not a number!\n", str);
            exit(1);
        }
    }
}

void checkPortRange(int portNum) {
    if(1 > portNum || 65535 < portNum) {
        fprintf(stderr, "Invalid port \"%d\", must be in range [1 65,535]\n", portNum);
        exit(1);
    }
}

void ResetCanonicalMode(int fd, struct termios *savedattributes) {
    tcsetattr(fd, TCSANOW, savedattributes);
}

void sendUDPMessage(int type) {
    *((uint16_t*)outgoingUDPMsg + 2) = htons(type);  

    std::string userName_a, hostName_a;
    getClientNUserName(outgoingUDPMsg, hostName_a, userName_a);
    dprint("SEND: %d ", type);
    
    if(type == REPLY) {
        dprint("SRC - %s : %d ", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));
        dprint("DEST - %s : %d\n", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));
        if(sendto(udpSockFd, outgoingUDPMsg, outgoingUDPMsgLen, 0, 
            (struct sockaddr*)&udpClientAddr, sizeof(udpClientAddr)) < 0) {
            die("Failed to send unicast message");
        }
    }
    else {
        dprint("SRC - %s : %d ", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));            
        dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), udpServerAddr.sin_port);
        if(sendto(udpSockFd, outgoingUDPMsg, outgoingUDPMsgLen, 0, 
            (struct sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
            die("Failed to send broadcast message");
        }
    }

}

void SetNonCanonicalMode(int fd, struct termios *savedattributes) {
    struct termios TermAttributes;
    char *name;
    
    // Make sure stdin is a terminal. 
    if(!isatty(fd)){
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }
    
    // Save the terminal attributes so we can restore them later. 
    tcgetattr(fd, savedattributes);
    
    // Set the funny terminal modes. 
    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO); // Clear ICANON and ECHO. 
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}

// Handler for SIGINT signal
void SIGINT_handler(int signum) {
    if(signum == SIGINT) {
        // std::string input;

        // Prompt users before terminating
        // std::cout << "Do you want to terminate? ";
        // std::getline(std::cin, input);

        // if(input[0] == 'y'){
            // Shutdown socket before exiting

            // Close all tcp connections
            for(auto it : clientMap) {
                if(it.second.tcpSockFd != -1) {
                    
                    uint8_t outgoingTCPMsg[6];
                    memcpy(outgoingTCPMsg, "P2PI", 4);
                    *((uint16_t*)outgoingTCPMsg + 2) = htons(DISCONTINUE_COMM);

                    if(write(it.second.tcpSockFd, outgoingTCPMsg, 6) < 0) {
                        die("Failed to send TCP message");
                    }

                    close(it.second.tcpSockFd);
                }
            }

            sendUDPMessage(CLOSING);
            

            shutdown(tcpSockFd, 0);
            shutdown(udpSockFd, 0);
            exit(0);
        // }
    }
}

void parseOptions(int argc, char** argv) {
    // Get default hostname, IP, and subnet mask
    getClientName();

    // Iterate through all options
    for(int i = 1; i < argc; i++){
        optErr = argv[i];

        // Check if option is valid
        if(argv[i][0] == '-' && optionMap.find(argv[i]) != optionMap.end()) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                    optionError(argv);

            // Handle options
            switch(optionMap[argv[i]])
            {
                case USERNAME: {
                    optionMap[argv[i]] = -1;
                    userName = argv[i + 1];
                    break;
                }
                case UDP_PORT: {
                    optionMap[argv[i]] = -1;
                    checkIsNum(argv[i + 1]);
                    udpPort = atoi(argv[i + 1]);
                    checkPortRange(udpPort);
                    break;
                }
                case TCP_PORT: {
                    optionMap[argv[i]] = -1;
                    checkIsNum(argv[i + 1]);
                    tcpPort = atoi(argv[i + 1]);
                    checkPortRange(tcpPort);

                    if(udpPort == tcpPort) {
                        fprintf(stderr, "Port conflicts!\n");
                        exit(1);
                    }

                    break;
                }
                case MIN_TIMEOUT: {
                    optionMap[argv[i]] = -1;
                    checkIsNum(argv[i + 1]);
                    minTimeout = atoi(argv[i + 1]);
                    break;
                }
                case MAX_TIMEOUT: {
                    optionMap[argv[i]] = -1;
                    checkIsNum(argv[i + 1]);
                    maxTimeout = atoi(argv[i + 1]);

                    if(maxTimeout < minTimeout) {
                        fprintf(stderr, "Get better with your math!\n");
                        exit(1);
                    }

                    break;
                }
                // TODO:
                case HOST: {
                    // optionMap[argv[i]] = -1;
                    std::string tmpArgv = argv[i + 1];

                    // Parse hostname part and port num part
                    std::size_t pos = tmpArgv.find(":");
                    if(pos <= 0 || pos == tmpArgv.length() - 1) {
                        fprintf(stderr, "Invalid argument %s\n", argv[i + 1]);
                        exit(1);
                    }

                    std::string tmpHostName = tmpArgv.substr(0, pos);
                    std::string tmpPortNumStr = tmpArgv.substr(pos + 1);
                    checkIsNum(tmpPortNumStr.c_str());

                    int tmpPortNum = std::stoi(tmpPortNumStr);

                    dprint("%s %d\n", tmpHostName.c_str(), tmpPortNum);

                    // Check if host is in local subnet
                    // First resolve host's domain name
                    struct hostent* remoteHostEntry = gethostbyname(tmpHostName.c_str());
                    if(!remoteHostEntry) {
                        die("Failed to resolve host");
                    }

                    struct in_addr remoteAddr;
                    char buf[256];
                    struct ifaddrs *currentIFAddr, *firstIFAddr;

                    memcpy(&remoteAddr, remoteHostEntry->h_addr, remoteHostEntry->h_length);
                    // dprint("%s\n", std::string(remoteHostEntry->h_addr).c_str());
                    inet_ntop(AF_INET, &remoteAddr, buf, INET_ADDRSTRLEN);
                    // dprint("%s\n", buf);
                    // dprint("%lu\n", remoteAddr.s_addr & tmpMask.s_addr);
                    // dprint("%lu\n", tmpIPAddr.s_addr & tmpMask.s_addr);

                    // TODO:
                    if((remoteAddr.s_addr & tmpMask.s_addr) == (tmpIPAddr.s_addr & tmpMask.s_addr)) {
                        // dprint("Same subnet\n", 0);
                        struct Host newUnicastHost;
                        newUnicastHost.hostName = tmpHostName;
                        newUnicastHost.portNum = tmpPortNum;

                        unicastHosts.push_back(newUnicastHost);
                    } 

                    // dprint("%s", inet_ntoa(remoteHostEntry->h_addr));
                    // dprint("%s", remoteHostEntry->h_addr);
                    // minTimeout = atoi(argv[i + 1]);
                    break;
                }
                default: {
                    optionError(argv);
                }
            }
        }
    }
}

void initUDPMsg() {
    // Construct discovery message
    outgoingUDPMsgLen = 10 + hostName.length() + 1 + userName.length() + 1;
    memset(outgoingUDPMsg, 0, MAX_UDP_MSG_LEN);

    memcpy(outgoingUDPMsg, "P2PI", 4);
    *((uint16_t*)outgoingUDPMsg + 2) = htons(DISCOVERY);
    *((uint16_t*)outgoingUDPMsg + 3) = htons(udpPort);
    *((uint16_t*)outgoingUDPMsg + 4) = htons(tcpPort);
    memcpy(outgoingUDPMsg + 10, hostName.c_str(), hostName.length());
    memcpy(outgoingUDPMsg + 10 + hostName.length() + 1, userName.c_str(), 
        userName.length());
}

// void addNewClient(uint8_t* incomingUDPMsg, int fd) { 
void addNewClient(uint8_t* incomingUDPMsg) { 
    struct Client newClient;
    getClientNUserName(incomingUDPMsg, newClient.hostName, newClient.userName);
    getPorts(incomingUDPMsg, newClient.udpPort, newClient.tcpPort);

    // Store the TCP address
    struct sockaddr_in tcpClientAddr = udpClientAddr;
    tcpClientAddr.sin_port = htons(newClient.tcpPort);
    newClient.tcpClientAddr = tcpClientAddr;

    // Inserting newClient to map
    clientMap[newClient.userName] = newClient;
}

void connectToClient(std::string clientName) {
    std::unordered_map<std::string, struct Client>::iterator it = clientMap.find(clientName);
    if(it != clientMap.end()) {
        struct sockaddr_in client2ConnetAddr = it->second.tcpClientAddr;
        int newConn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(0 > connect(newConn, (struct sockaddr *)&client2ConnetAddr, sizeof(client2ConnetAddr)))
            die("Failed to connect to host.");

        dprint("%d\n", newConn);
        // close(tmpTCPSock);

        dprint("CONNECTED TO NEW HOST\n", 0);

        // Send ESTABLISH COMMUNICATION MSG
        uint8_t ECM[39];
        int ECMLen = 4 + 2 + userName.length() + 1;
        // int ECMLen = 4 + 2;

        memset(ECM, 0, ECMLen);
        memcpy(ECM, "P2PI", 4);
        *((uint16_t*)ECM + 2) = htons(ESTABLISH_COMM);
        memcpy((uint16_t*)ECM + 3, userName.c_str(), userName.length());

        if(write(newConn, ECM, ECMLen) < 0) {
            die("Failed to send ESTABLISH COMM message.");
        }

        tcpConnMap[newConn] = clientName;

        // Push fd to pollfd vector
        struct pollfd newPollFd;
        newPollFd.fd = newConn;
        newPollFd.events = POLLIN;
        pollFd.push_back(newPollFd);

        // Record the newly connected tcp socket
        clientMap.find(clientName)->second.tcpSockFd = newConn;
    }
}

std::unordered_map<std::string, struct Client>::iterator findClient(uint8_t* incomingUDPMsg) {
    struct Client newClient;
    getClientNUserName(incomingUDPMsg, newClient.hostName, newClient.userName);
    // getPorts(incomingUDPMsg, newClient.udpPort, newClient.tcpPort);
    
    return clientMap.find(newClient.userName);
}

void setupSocket() {
    udpSockFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    tcpSockFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(udpSockFd < 0 || tcpSockFd < 0)
        die("Failed to open socket");

    // Enable broadcast capability
    enable = 1;
    if(setsockopt(udpSockFd, SOL_SOCKET, SO_BROADCAST, &enable, 
        sizeof(enable)) < 0) {
        die("Failed to set socket option broadcast");
    }

    if(setsockopt(udpSockFd, SOL_SOCKET, SO_REUSEPORT, &enable, 
        sizeof(enable)) < 0) {
        die("Failed to set socket option reuse address");
    }

    udpServerAddr.sin_family = AF_INET;
    udpServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    udpServerAddr.sin_port = htons(udpPort);

    tcpServerAddr.sin_family = AF_INET;
    tcpServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcpServerAddr.sin_port = htons(tcpPort);

    if(-1 == bind(udpSockFd, (struct sockaddr*)&udpServerAddr, sizeof(udpServerAddr)))
        die("Failed to bind udp socket");

    if(-1 == bind(tcpSockFd, (struct sockaddr*)&tcpServerAddr, sizeof(tcpServerAddr)))
        die("Failed to bind tcp socket");

    listen(tcpSockFd, 5);
}

void sendTCPMessage(int type, std::string userName) {
    uint8_t outgoingTCPMsg[6];
    memcpy(outgoingTCPMsg, "P2PI", 4);
    *((uint16_t*)outgoingTCPMsg + 2) = htons(type);
    int fd = clientMap.find(userName)->second.tcpSockFd;

    if(write(fd, outgoingTCPMsg, 6) < 0) {
        die("Failed to send TCP message");
    }
}

void checkTCPPort(std::string newClientName) {
	if(pollFd[tcpFDPOLL].revents == POLLIN) {
        dprint("NEW HOST TRYING TO CONNECT\n", 0);

		int newConn = accept(tcpSockFd, (struct sockaddr*)&tcpClientAddr, &tcpClientAddrLen);
		
        dprint("NEW HOST CONNECTED at %d\n", newConn);

        struct pollfd newPollFd;
        newPollFd.fd = newConn;
        newPollFd.events = POLLIN;
        pollFd.push_back(newPollFd);
	}
}

void checkUDPPort(int baseTimeout, int &currTimeout) {
     // Reply message
    uint8_t incomingUDPMsg[MAX_UDP_MSG_LEN];
    udpClientAddrLen = sizeof(udpClientAddr);
    // udpClientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    int recvLen;

    // UDP packet
    if(pollFd[udpFDPOLL].revents == POLLIN) {
        recvLen = recvfrom(udpSockFd, incomingUDPMsg, MAX_UDP_MSG_LEN, 0,
            (struct sockaddr*)&udpClientAddr, &udpClientAddrLen);

        if(recvLen > 0) {
            int type = getType(incomingUDPMsg);

            dprint("RECV: %d ", type);
            dprint("SRC - %s : %d ", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));
            dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));

            // for(int i = 0; i < recvLen; i++)
            // {
            //     dprint("%c", incomingUDPMsg[i]);
            // }

            switch (type) {
                case DISCOVERY: {
                    // Add host to clientMap if not self discovery and not in
                    // map already

                    // Check if packet is from another host
                    if(memcmp(incomingUDPMsg + 6, outgoingUDPMsg + 6, outgoingUDPMsgLen - 6)) {
                        // Check if host is already discovered
                        if(findClient(incomingUDPMsg) == clientMap.end()) {
                            std::string userName_a, hostName_a;
                            getClientNUserName(incomingUDPMsg, hostName_a, userName_a);
                            dprint("-----NEW HOST: %s-----\n", userName_a.c_str());
                            
                            addNewClient(incomingUDPMsg);
                        }

                        // Should send reply regardless the host is already known
                        sendUDPMessage(REPLY);
                    }
                    else
                        dprint("-----SELF DISCOVERY-----\n", 0);

                    break;
                }

                case REPLY: {
                    // Add host to map if not in map already
                    if(findClient(incomingUDPMsg) == clientMap.end()) {
                        std::string userName_a, hostName_a;
                        getClientNUserName(incomingUDPMsg, hostName_a, userName_a);
                        dprint("-----NEW HOST: %s-----\n", userName_a.c_str());
                        addNewClient(incomingUDPMsg);

                        // Try to initiate tcp connection with host
                        dprint("%s\n", clientMap.begin()->first.c_str());
                        // connectToClient(clientMap.begin()->first);
                    }
                    break;
                }

                case CLOSING: {
                    // Remove host from map
                    std::unordered_map<std::string, struct Client>::iterator it = findClient(incomingUDPMsg);

                    if(it != clientMap.end()) {
                    //     if(it->second.tcpSockFd != -1) {
                    //         dprint("CLOSING connection\n", 0);
                    //         close(it->second.tcpSockFd);
                    //     }
                        clientMap.erase(it);
                    }

                    // If no more host is available, go back to discovery
                    if(clientMap.empty()) {
                        currTimeout = 0;

                        // Resets timeout value
                        baseTimeout = minTimeout;
                    }
                    break;
                }
            }
        }
    }
}

void checkConnections()
{
	for(auto it = pollFd.begin() + 3; it != pollFd.end();) {
        // dprint("Checking %d at %d\n", i, pollFd[i].fd);
    	if(it->revents == POLLIN) {
            dprint("%d has something, size %d\n", it->fd, pollFd.size());
    		uint8_t incomingTCPMsg[518];
    		int recvLen = read(it->fd, incomingTCPMsg, 518);

            dump(std::string((char*)incomingTCPMsg));

    		if(recvLen > 0) {
    			int type = getType(incomingTCPMsg);

    			dprint("RECV: %d\n", type);
    			// dprint("SRC - %s : %d ", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));
    			// dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));

    			switch(type) {
                    case ESTABLISH_COMM: {
                        // uint8_t incomingTCPMsg[518];
                        // int recvLen = read(newConn, incomingTCPMsg, 518);

                        // if(recvLen > 0 && getType(incomingTCPMsg) == ESTABLISH_COMM) {
                        dprint("ESTABLISH_COM length is %d\n", recvLen);
                        std::string newClientName = (char*)((uint16_t*)incomingTCPMsg + 3);
                        uint8_t ECM[6];
                        int ECMLen = 6;
                        memcpy(ECM, "P2PI", 4);
                        
                        dprint("New Client Name is %s\n", newClientName.c_str());
                        if(clientMap.find(newClientName) == clientMap.end()) {
                            dprint("WHO?\n", 0);
                        }

                        if(away || clientMap.find(newClientName)->second.block) {
                            // Send user unavailable message
                            *((uint16_t*)ECM + 2) = htons(USER_UNAVALIBLE);

                            if(write(it->fd, ECM, 6) < 0) {
                                die("Failed to establish TCP connection.");
                            }

                            // Close connection
                            close(it->fd);
                            it = pollFd.erase(it);
                        }
                        else {
                            // Send accept comm message
                            dprint("GOOD\n", 0);
                            *((uint16_t*)ECM + 2) = htons(ACCEPT_COMM);

                            clientMap.find(newClientName)->second.tcpSockFd = it->fd;

                            tcpConnMap[it->fd] = newClientName;

                            // Push fd to pollfd vector
                            // struct pollfd newPollFd;
                            // newPollFd.fd = newConn;
                            // newPollFd.events = POLLIN;
                            // pollFd.push_back(newPollFd);

                            if(write(it->fd, ECM, 6) < 0) {
                                die("Failed to establish TCP connection.");
                            }
                        }

                            
                        // }
                    }
    				case ACCEPT_COMM: {
                        dprint("Connected to user %s\n", tcpConnMap.find(it->fd)->second.c_str());

                        uint8_t ECM[8];
                        int ECMLen = 8;
                        memcpy(ECM, "P2PI", 4);
                        *((uint16_t*)ECM + 2) = htons(DATA);
                        memcpy(ECM + 6, "a\0", 2);
                        dprint("Sending...\n", 0);
                        // if(write(it->fd, ECM, 8) < 0) {
                        //     die("Failed to send data.");
                        // }

    					break;
    				}
    				case USER_UNAVALIBLE: {
    					// Close connection as well
                        dprint("The user %s is currently unavailable\n", tcpConnMap.find(it->fd)->second.c_str());
    					close(it->fd);
    					it = pollFd.erase(it);
                        continue;
    				}
    				case REQUEST_USER_LIST: {

    					break;
    				}
    				case REPLY_USER_LIST: {

    					break;
    				}
    				case DATA: {
                        char buffer[512];
                        memcpy(buffer, incomingTCPMsg + 6, recvLen);
                        dprint("Message is %s\n", buffer);
    					break;
    				}
    				case DISCONTINUE_COMM: {
                        dprint("User %s wants to discontinue communication.\n", tcpConnMap.find(it->fd)->second.c_str());
                        clientMap.find(tcpConnMap.find(it->fd)->second)->second.tcpSockFd = -1;
                        tcpConnMap.erase(tcpConnMap.find(it->fd));
                        close(it->fd);
                        it = pollFd.erase(it);
                        continue;
    				}
    			}

                
    		}
    	}
        
        
        it++;
    }
}


