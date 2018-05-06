#include "p2pim.h"


#define MAX_UDP_MSG_LEN (10 + 64 + 32 + 2)
#define udpFDPOLL 0
#define terminalFDPOLL 1

enum Option {USERNAME, UDP_PORT, TCP_PORT, MIN_TIMEOUT, MAX_TIMEOUT, HOST};

static std::unordered_map<std::string, int> optionMap {
    {"-u", USERNAME}, {"-up", UDP_PORT}, {"-tp", TCP_PORT}, 
    {"-dt", MIN_TIMEOUT}, {"dm", MAX_TIMEOUT}, {"pp", HOST}
};


struct Host {
    std::string userName;
    std::string hostName;
    int udpPort;
    int tcpPort;
};



std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int minTimeout = 5000;
int maxTimeout = 60000;
// int destPort = ls
std::string optErr;

uint8_t outgoingUDPMsg[MAX_UDP_MSG_LEN];
int outgoingUDPMsgLen;
std::unordered_map<std::string, struct Host> hostMap;

int udpSockFd, tcpSockFd, enable = 1;
struct sockaddr_in udpServerAddr, clientAddr;
socklen_t clientAddrLen; 
struct pollfd pollFd[2];


std::unordered_map<std::string, struct Host>::iterator findHost(uint8_t* replyMsg);


int main(int argc, char** argv) {
    // Setup signal handler
    if(signal(SIGINT, SIGINT_handler) == SIG_ERR)
        die("Failed to catch signal");

    parseOptions(argc, argv);
    initUDPMsg();
    setupSocket();

    // dprint("Username = %s\n", userName.c_str());
    // // TODO: Hostname = sp4.cs.ucdavis.edu
    // dprint("Hostname = %s\n", hostName.c_str());
    // dprint("UDP Port = %d\n", udpPort);
    // dprint("TCP Port = %d\n", tcpPort);
    // dprint("Mintimeout = %d\n", minTimeout);
    // dprint("Maxtimeout = %d\n", maxTimeout);


    pollFd[0].fd = udpSockFd;
    pollFd[0].events = POLLIN;
    // pollFd[1].fd = tcpSockFd;
    // pollFd[1].events = POLLIN;
    int baseTimeout = minTimeout;
    timeval start,end;
    int timePassed;
    int currTimeout = 0;


    // When no host is available and maxTimeout not exceeded, discovery hosts
    while(baseTimeout <= maxTimeout * 1000) {
        
        if(currTimeout <= 0 && hostMap.empty()) {
            sendUDPMessage(DISCOVERY);
            currTimeout = baseTimeout;
        }


        // Wait for reply message
        gettimeofday(&start, NULL);
        //TODO: there is a potential bug, where currTimeout is set to 0 and poll returns immediately
        int rc = poll(pollFd, 1, currTimeout);
        gettimeofday(&end, NULL);
        timePassed = ((end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec) / 1000;
        currTimeout -= timePassed;

        // Timeout event
        if(0 == rc) {
            if(hostMap.empty()) {
                dprint("TIMEOUT: ", 0);

                // Double current timeout
                baseTimeout *= 2;
                dprint("%d\n", currTimeout);
            }
        }
        else if(rc > 0) {
            std::string newHostName, newUserName;

            // Reply message
            uint8_t incomingUDPMsg[MAX_UDP_MSG_LEN];
            clientAddrLen = sizeof(clientAddr);
            // clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
            int recvLen;

            // UDP packet
            if(pollFd[0].revents == POLLIN) {
                recvLen = recvfrom(udpSockFd, incomingUDPMsg, MAX_UDP_MSG_LEN, 0,
                    (struct sockaddr*)&clientAddr, &clientAddrLen);

                if(recvLen > 0) {
                    int type = getType(incomingUDPMsg);

                    dprint("RECV: %d ", type);
                    dprint("SRC - %s : %d ", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
                    dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));

                    // for(int i = 0; i < recvLen; i++)
                    // {
                    //     dprint("%c", incomingUDPMsg[i]);
                    // }

                    switch (type) {
                        case DISCOVERY: {
                            // Add host to hostMap if not self discovery and not in
                            // map already

                            // Check if packet is from another host
                            if(memcmp(incomingUDPMsg + 6, outgoingUDPMsg + 6, outgoingUDPMsgLen - 6)) {
                                // Check if host is already discovered
                                if(findHost(incomingUDPMsg) == hostMap.end()) {
                                    dprint("-----NEW HOST-----\n", 0);
                                    addNewHost(incomingUDPMsg);
                                }
                                sendUDPMessage(REPLY);
                                
                                // std::string userName_a, hostName_a;
                                // getHostNUserName(outgoingUDPMsg, hostName_a, userName_a);
                                // dprint("Replying as %s\n", userName_a.c_str());
                            }
                            else
                                dprint("-----SELF DISCOVERY-----\n", 0);

                            // Send reply message

                            // Wait for tcp connection

                            break;
                        }

                        case REPLY: {
                            // Add host to map if not in map already
                            if(findHost(incomingUDPMsg) == hostMap.end()) {
                                // std::string userName_a, hostName_a;
                                // getHostNUserName(incomingUDPMsg, hostName_a, userName_a);
                                dprint("-----NEW HOST-----\n", 0);
                                addNewHost(incomingUDPMsg);
                            }

                            // Try to initiate tcp connection with host

                            break;
                        }

                        case CLOSING: {
                            // Remove host from map
                            std::unordered_map<std::string, struct Host>::iterator it = findHost(incomingUDPMsg);
                            if(it != hostMap.end())
                                hostMap.erase(it);

                            // If no more host is available, go back to discovery
                            if(hostMap.empty()) {
                                currTimeout = 0;

                                // Resets timeout value
                                baseTimeout = minTimeout;
                            }
                            break;
                        }
                    }
                }
            }

            // TCP packet
            // if(pollFd[1].revents == POLLIN) {

            // }
        }
        else
            dprint("ERROR\n", 0);
    }


    struct termios SavedTermAttributes;
    char* RX[4];
    std::string buffer = "";
	pollFd[terminalFDPOLL].fd = STDIN_FILENO;
	pollFd[terminalFDPOLL].events = POLLIN;
	std::string message = "";
	SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    int bytesRead, retpoll;
	printf(">");
	fflush(STDIN_FILENO);
    while(1){
		retpoll = poll(pollFd, 4, 1000); // to edit
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
				}
				else if(buffer[0] == 0x7F) { //delete char
					if(message.length() > 0) {
						message.erase(message.length()-1);
					}
				}
				else if(message.length() < 192) {
					message += buffer[0];
				}
			}
			printf("\r\b\b\n");
			printf(">%s ", message.c_str(), message.length());
			
			printf("\033[0D"); //mv cursor
			fflush(STDIN_FILENO);
			buffer.clear();
		}
    }
    
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes); 


    
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



std::string getHostName()
{
    char buffer[256];
    
    if(-1 == gethostname(buffer, 255)){
        fprintf(stderr, "Unable to resolve host name.");
        exit(-1);
    }

    struct hostent* localHostEntry = gethostbyname(buffer);
    strcpy(buffer, localHostEntry->h_name);
    return std::string(buffer);
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



void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName) {
    hostName = (char*)(message + 10);
    userName = (char*)(message + 10 + hostName.length() + 1);
}



void getPorts(uint8_t* message, int& udpPort, int& tcpPort) {
    udpPort = ntohs(*((uint16_t*)message + 3));
    tcpPort = ntohs(*((uint16_t*)message + 4));
}



void dump(std::string msg)
{
    for(int i = 0; i < msg.length(); i++)
    {
        dprint("%d %c \n", msg[i], msg[i]);
    }
    dprint("\n", 0);
}



void checkIsNum(char* str) {
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



void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
}



void sendUDPMessage(int type) {
    *((uint16_t*)outgoingUDPMsg + 2) = htons(type);  

    std::string userName_a, hostName_a;
    getHostNUserName(outgoingUDPMsg, hostName_a, userName_a);
    dprint("SEND: %d ", type);
    
    if(type == REPLY) {
        dprint("SRC - %s : %d ", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));
        dprint("DEST - %s : %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        if(sendto(udpSockFd, outgoingUDPMsg, outgoingUDPMsgLen, 0, 
            (struct sockaddr*)&clientAddr, sizeof(clientAddr)) < 0) {
            die("Failed to send unicast message");
        }
    }
    else {
        dprint("SRC - %s : %d ", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));            
        dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), udpServerAddr.sin_port);
        if(sendto(udpSockFd, outgoingUDPMsg, outgoingUDPMsgLen, 0, 
            (struct sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
            die("Failed to send broadcast message");
        }
    }

}



void SetNonCanonicalMode(int fd, struct termios *savedattributes){
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
            sendUDPMessage(CLOSING);
            shutdown(udpSockFd, 0);
            exit(0);
        // }
    }
}



void parseOptions(int argc, char** argv) {
    // Get default hostname
    hostName = getHostName();

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
                // case HOST: {
       //              optionMap[argv[i]] = -1;
       //              minTimeout = atoi(argv[i + 1]);
       //              break;
       //          }
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

    memcpy((uint32_t*)outgoingUDPMsg, "P2PI", 4);
    *((uint16_t*)outgoingUDPMsg + 2) = htons(DISCOVERY);
    *((uint16_t*)outgoingUDPMsg + 3) = htons(udpPort);
    *((uint16_t*)outgoingUDPMsg + 4) = htons(tcpPort);
    memcpy(outgoingUDPMsg + 10, hostName.c_str(), hostName.length());
    memcpy(outgoingUDPMsg + 10 + hostName.length() + 1, userName.c_str(), 
        userName.length());
}



void addNewHost(uint8_t* incomingUDPMsg) { 
    struct Host newHost;
    getHostNUserName(incomingUDPMsg, newHost.hostName, newHost.userName);
    getPorts(incomingUDPMsg, newHost.udpPort, newHost.tcpPort);

    hostMap[newHost.userName + "@" + newHost.hostName + ":" + std::to_string(newHost.tcpPort)] = newHost;
}



// void addNewHost(uint8_t* incomingUDPMsg) { 
//     struct Host newHost;
//     getHostNUserName(incomingUDPMsg, newHost.hostName, newHost.userName);
//     getPorts(incomingUDPMsg, newHost.udpPort, newHost.tcpPort);

//     hostMap[newHost.userName + "@" + newHost.hostName + ":" + std::to_string(newHost.tcpPort)] = newHost;
// }


std::unordered_map<std::string, struct Host>::iterator findHost(uint8_t* incomingUDPMsg) {
    struct Host newHost;
    getHostNUserName(incomingUDPMsg, newHost.hostName, newHost.userName);
    getPorts(incomingUDPMsg, newHost.udpPort, newHost.tcpPort);
    
    return hostMap.find(newHost.userName + "@" + newHost.hostName + ":" + std::to_string(newHost.tcpPort));
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

    if(-1 == bind(udpSockFd, (struct sockaddr*)&udpServerAddr, sizeof(udpServerAddr)))
        die("Failed to bind socket");

    udpServerAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST); 
}