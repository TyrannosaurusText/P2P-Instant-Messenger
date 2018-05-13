#include "p2pim.h"
// #define DEBUG 1

#define MAX_UDP_MSG_LEN (10 + 254 + 32 + 2)

enum Option {USERNAME, UDP_PORT, TCP_PORT, MIN_TIMEOUT, MAX_TIMEOUT, HOST};
enum Command {CONNECT, LIST, DISCONNECT, GETLIST, HELP, SWITCH, AWAY, UNAWAY, BLOCK, UNBLOCK, EXIT};

struct Client {
    std::string userName;
    std::string hostName;
    int udpPort;
    int tcpPort;
    int tcpSockFd = -1;
    int block = 0;
    std::string leftOverMsg = "";
};

struct Pair {
    std::string hostName;
    int portNum;
};

static std::unordered_map<std::string, int> optionMap {
    {"-u", USERNAME}, {"-up", UDP_PORT}, {"-tp", TCP_PORT},
    {"-dt", MIN_TIMEOUT}, {"-dm", MAX_TIMEOUT}, {"-pp", HOST}
};

static std::unordered_map<std::string, int> commandMap {
    {"\\connect", CONNECT}, {"\\c", CONNECT}, {"\\switch", SWITCH}, 
    {"\\list", LIST}, {"\\disconnect", DISCONNECT}, {"\\getlist", GETLIST},
    {"\\help", HELP}, {"\\away", AWAY}, {"\\unaway", UNAWAY},
    {"\\block", BLOCK}, {"\\unblock", UNBLOCK}, {"\\exit", EXIT}
};

std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int gMinTimeout = 5000;
int gMaxTimeout = 60000;
struct in_addr gIPAddr, gSubnetMask;

uint8_t outgoingUDPMsg[MAX_UDP_MSG_LEN];
int outgoingUDPMsgLen;
std::unordered_map<std::string, struct Client> clientMap;
std::unordered_map<int, std::string> tcpConnMap;

int udpSockFd, tcpSockFd, enable = 1;
// struct sockaddr_in udpServerAddr, udpClientAddr, tcpServerAddr, tcpClientAddr;
struct sockaddr_in udpServerAddr, udpClientAddr, tcpServerAddr;
socklen_t udpClientAddrLen, tcpClientAddrLen;

std::vector<struct pollfd> pollFd(3);
std::vector<struct Pair> unicastHosts;

int away = 0;

int bytesRead, retpoll, numcol;
struct winsize size;
char* RX[4]; //stdin buffer
std::string buffer = "";
std::string message = "";
int currentConnection = -1;
fd_set set;
struct termios SavedTermAttributes;
std::string list = "";



std::unordered_map<std::string, struct Client>::iterator findClientByFd(int fd);

int main(int argc, char** argv) {
    // Setup signal handler
    if(signal(SIGINT, SIGINT_handler) == SIG_ERR)
        die("Failed to catch signal");

    parseOptions(argc, argv);
    setupSocket();
    initUDPMsg();

    pollFd[terminalFDPOLL].fd = STDIN_FILENO;
    pollFd[terminalFDPOLL].events = POLLIN;
    pollFd[udpFDPOLL].fd = udpSockFd;
    pollFd[udpFDPOLL].events = POLLIN;
    pollFd[tcpFDPOLL].fd = tcpSockFd;
    pollFd[tcpFDPOLL].events = POLLIN;
    
    int baseTimeout = gMinTimeout;
    timeval start,end;
    int timePassed;
    int currTimeout = 0;

    printf("Please type in \"\\help\" for a list of commands available\n");

    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);

    // When no host is available and gMaxTimeout not exceeded, discovery hosts
    while(baseTimeout <= gMaxTimeout * 1000) {
        if(currTimeout <= 0) {
            if(clientMap.empty()) {
                if(!unicastHosts.empty()) {
                    for(auto h : unicastHosts) {
                        // Resolve dns
                        struct hostent* remoteHostEntry = gethostbyname(h.hostName.c_str());
                        if(!remoteHostEntry)
                            die("Failed to resolve host");

                        struct in_addr remoteAddr;
                        memcpy(&remoteAddr, remoteHostEntry->h_addr, remoteHostEntry->h_length);
                        udpServerAddr.sin_addr = remoteAddr;
                        udpServerAddr.sin_port = htons(h.portNum);
                    }
                }
                else
                    udpServerAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

                sendUDPMessage(DISCOVERY);
                currTimeout = baseTimeout;
            }
            else
                currTimeout = -1;
        }

        clearline();      
		std::string connName;
		if(tcpConnMap.find(currentConnection) == tcpConnMap.end())
			connName = "";
		else
			connName = tcpConnMap.find(currentConnection)->second;

		if(message.length()+connName.length() > numcol) //simulate loop
            printf("%s>%s", connName.c_str(), message.substr(message.length()-numcol+connName.length()+1, numcol).c_str());
        else
            printf("%s>%s", connName.c_str(), message.c_str());
        
        fflush(STDIN_FILENO); 
        // Wait for reply message
        gettimeofday(&start, NULL);
        dprint("currtimeout is %d\n", currTimeout);
        int rc = poll(pollFd.data(), pollFd.size(), currTimeout);

        gettimeofday(&end, NULL);
        timePassed = ((end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec) / 1000;
        currTimeout -= timePassed;

        // Timeout event
        if(0 == rc) {
            clearline();
            dprint("Next iteration at %d\n", currTimeout);
            if(clientMap.empty()) {
                dprint("TIMEOUT: \n", 0);
                // Double current timeout
                baseTimeout *= 2;
                dprint("%d\n", currTimeout);
            }
        }
        else if(rc > 0) {
            std::string newClientName, newUserName;
            checkUDPPort(baseTimeout, currTimeout);
            // new TCP connection
            checkTCPPort(newClientName);
            // TCP packet
            checkTCPConnections();
            checkSTDIN();
        }
        else
            fprintf(stderr, "\rPOLL ERROR\n");
    }
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    return 0;
}


/*
printf("\033[XA"); // Move up X lines;
printfprintf("\033[XB"); // Move down X lines;
printf("\033[XC"); // Move right X column;
printf("\033[XD"); // Move left X column;
printf("\033[2J"); // Clear screen
*/
void optionError(char** argv, const char* optErr){
    fprintf(stderr, "%s: option requires an argument -- '%s'\n", argv[0], optErr);
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    exit(1);
}

void getClientName(){
    char buffer[256];

    // Get hostname
    if(-1 == gethostname(buffer, 255))
        die("Unable to find local host name.");

    struct hostent* localHostEntry = gethostbyname(buffer);

    if(!localHostEntry)
        die("Unable to resolve local host.");

    int found = 0;
    struct ifaddrs *currentIFAddr, *firstIFAddr;
    hostName = localHostEntry->h_name;
    memcpy(&gIPAddr, localHostEntry->h_addr, localHostEntry->h_length);

    if(0 > getifaddrs(&firstIFAddr))
        die("Failed to get ifaddr.");

    currentIFAddr = firstIFAddr;

    // get subnet mask
    do {
        if(AF_INET == currentIFAddr->ifa_addr->sa_family) {
            if(0 == memcmp(&((struct sockaddr_in*)currentIFAddr->ifa_addr)->sin_addr, &gIPAddr, localHostEntry->h_length)) {
                memcpy(&gSubnetMask, &((struct sockaddr_in*)currentIFAddr->ifa_netmask)->sin_addr, localHostEntry->h_length);
                found = 1;
                break;
            }
        }
        currentIFAddr = currentIFAddr->ifa_next;
    } while(currentIFAddr);

    freeifaddrs(firstIFAddr);

    if(!found) {
        fprintf(stderr, "\rFailed to find subnet mask.\n");
        ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
        exit(1);
    }
}

// Internal syscall errors
void die(const char *message) {
    perror(message);
    close(udpSockFd);
    close(tcpSockFd);
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
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

void checkIsNum(const char* str) {
    for(int i = 0; i < strlen(str); i++) {
        if(!isdigit(str[i])) {
            fprintf(stderr, "Input %s is not a number!\n", str);
            ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
            exit(1);
        }
    }
}

void checkPortRange(int portNum) {
    if(1 > portNum || 65535 < portNum) {
        fprintf(stderr, "Invalid port \"%d\", must be in range [1 65,535]\n", portNum);
        ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
        exit(1);
    }
}

void ResetCanonicalMode(int fd, struct termios *savedattributes) {
    tcsetattr(fd, TCSANOW, savedattributes);
}

void sendUDPMessage(int type) {
    *((uint16_t*)outgoingUDPMsg + 2) = htons(type);
    dprint("SEND: %d ", type);

    // Unicast
    if(type == REPLY) {
        dprint("SRC - %s : %d ", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));
        dprint("DEST - %s : %d\n", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));
        if(sendto(udpSockFd, outgoingUDPMsg, outgoingUDPMsgLen, 0,
            (struct sockaddr*)&udpClientAddr, sizeof(udpClientAddr)) < 0) {
            die("Failed to send unicast message");
        }
    }
    // Broadcast
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

    // Make sure stdin is a terminal.
    if(!isatty(fd)) {
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
        // Send disconnect messages to all connected clients
        for(auto it : clientMap) {
            if(it.second.tcpSockFd != -1) {
                sendTCPMessage(DISCONTINUE_COMM, it.second.tcpSockFd);
                close(it.second.tcpSockFd);
            }
        }
        // Broadcast closing datagram
        sendUDPMessage(CLOSING);
        close(tcpSockFd);
        close(udpSockFd);
        ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
        printf("\nBye...\n");
        exit(0);
    }
}

void parseOptions(int argc, char** argv) {
    std::string optErr;
    // Get default hostname, IP, and subnet mask
    getClientName();

    // Iterate through all options
    for(int i = 1; i < argc; i++){
        optErr = argv[i];

        // Check if it is an option
        if(argv[i][0] == '-') {
            if(optionMap.find(argv[i]) != optionMap.end()) {
                // Make sure there is an argument provided to the option
                if(i == argc - 1 || argv[i + 1][0] == '-')
                        optionError(argv, optErr.c_str());

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

                        if(udpPort == tcpPort) {
                            fprintf(stderr, "Port conflicts!\n");
                            ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                            exit(1);
                        }
                        break;
                    }
                    case TCP_PORT: {
                        optionMap[argv[i]] = -1;
                        checkIsNum(argv[i + 1]);
                        tcpPort = atoi(argv[i + 1]);
                        checkPortRange(tcpPort);

                        if(udpPort == tcpPort) {
                            fprintf(stderr, "Port conflicts!\n");
                            ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                            exit(1);
                        }
                        break;
                    }
                    case MIN_TIMEOUT: {
                        optionMap[argv[i]] = -1;
                        checkIsNum(argv[i + 1]);
                        gMinTimeout = atoi(argv[i + 1]);
                        break;
                    }
                    case MAX_TIMEOUT: {
                        optionMap[argv[i]] = -1;
                        checkIsNum(argv[i + 1]);
                        gMaxTimeout = atoi(argv[i + 1]);

                        if(gMaxTimeout < gMinTimeout) {
                            fprintf(stderr, "Get better with your math!\n");
                            ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                            exit(1);
                        }
                        break;
                    }
                    case HOST: {
                        std::string tmpArgv = argv[i + 1];
                        // Parse hostname part and port num part
                        std::size_t pos = tmpArgv.find(":");
                        // If not hostname is provided
                        if(pos <= 0 || pos == tmpArgv.length() - 1) {
                            fprintf(stderr, "Invalid argument %s\n", argv[i + 1]);
                            ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                            exit(1);
                        }
                        std::string tmpHostName = tmpArgv.substr(0, pos);
                        std::string tmpPortNumStr = tmpArgv.substr(pos + 1);
                        checkIsNum(tmpPortNumStr.c_str());

                        int tmpPortNum = std::stoi(tmpPortNumStr);

                        // Check if host is in local subnet
                        // First resolve host's domain name
                        struct hostent* remoteHostEntry = gethostbyname(tmpHostName.c_str());
                        if(!remoteHostEntry)
                            die("Failed to resolve host");

                        struct in_addr remoteAddr;
                        char buf[256];
                        struct ifaddrs *currentIFAddr, *firstIFAddr;

                        memcpy(&remoteAddr, remoteHostEntry->h_addr, remoteHostEntry->h_length);
                        inet_ntop(AF_INET, &remoteAddr, buf, INET_ADDRSTRLEN);

                        // Make sure client is not in the same subnet
                        if((remoteAddr.s_addr & gSubnetMask.s_addr) != (gIPAddr.s_addr & gSubnetMask.s_addr)) {
                            struct Pair newUnicastHost;
                            newUnicastHost.hostName = tmpHostName;
                            newUnicastHost.portNum = tmpPortNum;

                            unicastHosts.push_back(newUnicastHost);
                        }
                        break;
                    }
                    default: {
                        fprintf(stderr, "PARSE ERROR\n");
                        ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                        exit(1);
                        break;
                    }
                }
            }
            else {
                fprintf(stderr, "Invalid options %s\n", argv[i]);
                ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
                exit(1);
            }
        }
    }
}

void initUDPMsg() {
    // Construct discovery message
    outgoingUDPMsgLen = 10 + hostName.length() + 1 + userName.length() + 1;
    memset(outgoingUDPMsg, 0, MAX_UDP_MSG_LEN);

    memcpy(outgoingUDPMsg, "P2PI", 4);
    // *((uint16_t*)outgoingUDPMsg + 2) = htons(DISCOVERY);
    *((uint16_t*)outgoingUDPMsg + 3) = htons(udpPort);
    *((uint16_t*)outgoingUDPMsg + 4) = htons(tcpPort);
    memcpy(outgoingUDPMsg + 10, hostName.c_str(), hostName.length());
    memcpy(outgoingUDPMsg + 10 + hostName.length() + 1, userName.c_str(),
        userName.length());
}

void addNewClient(uint8_t* incomingUDPMsg) {
    struct Client newClient;
    getHostNUserName(incomingUDPMsg, newClient.hostName, newClient.userName);
    getPorts(incomingUDPMsg, newClient.udpPort, newClient.tcpPort);

    // Inserting newClient to map
    if(newClient.userName != userName)
        clientMap[newClient.userName] = newClient;
}

void connectToClient(std::string clientName) {
    std::unordered_map<std::string, struct Client>::iterator it = clientMap.find(clientName);
    if(it != clientMap.end()) {
        // Resolve dns
        struct hostent* remoteHostEntry = gethostbyname(it->second.hostName.c_str());
        if(!remoteHostEntry)
            die("Failed to resolve host");

        struct in_addr remoteAddr;
        memcpy(&remoteAddr, remoteHostEntry->h_addr, remoteHostEntry->h_length);

        struct sockaddr_in client2ConnetAddr;
        client2ConnetAddr.sin_family = AF_INET;
        client2ConnetAddr.sin_addr = remoteAddr;
        client2ConnetAddr.sin_port = htons(it->second.tcpPort);

        int newConn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(0 > connect(newConn, (struct sockaddr *)&client2ConnetAddr, sizeof(client2ConnetAddr)))
            die("Failed to connect to host.");

        dprint("CONNECTED TO NEW HOST\n", 0);

        // Send ESTABLISH COMMUNICATION MSG
        uint8_t ECM[39];
        int ECMLen = 4 + 2 + userName.length() + 1;

        memset(ECM, 0, ECMLen);
        memcpy(ECM, "P2PI", 4);
        *((uint16_t*)ECM + 2) = htons(ESTABLISH_COMM);
        memcpy((uint16_t*)ECM + 3, userName.c_str(), userName.length());
        dprint("New client name is %s\n", userName.c_str());

        if(write(newConn, ECM, ECMLen) < 0)
            die("Failed to send ESTABLISH COMM message.");

        // Record the newly connected tcp socket
        clientMap.find(clientName)->second.tcpSockFd = newConn;
        tcpConnMap[newConn] = clientName;

        // Push fd to pollfd vector
        struct pollfd newPollFd;
        newPollFd.fd = newConn;
        newPollFd.events = POLLIN;
        pollFd.push_back(newPollFd);
    }
}

std::unordered_map<std::string, struct Client>::iterator findClientByFd(int fd) {
    return clientMap.find(tcpConnMap.find(fd)->second);
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

    if(-1 == bind(tcpSockFd, (struct sockaddr*)&tcpServerAddr, sizeof(tcpServerAddr))) {
        // die("Failed to bind tcp socket");
        printf("Port in used\n");
        tcpServerAddr.sin_port = 0;
        if(-1 == bind(tcpSockFd, (struct sockaddr*)&tcpServerAddr, sizeof(tcpServerAddr))) {
            printf("Port in used\n");
            die("No port is available.");
        }
        socklen_t len = sizeof(tcpServerAddr);
        if(getsockname(tcpSockFd, (struct sockaddr *)&tcpServerAddr, &len) == -1)
            die("Failed to get sock name.");

        printf("new socket %d\n", tcpServerAddr.sin_port);
        tcpPort = ntohs(tcpServerAddr.sin_port);
    }

    udpServerAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    listen(tcpSockFd, 5);
}

void sendTCPMessage(int type, int fd) {
    uint8_t outgoingTCPMsg[6];
    memcpy(outgoingTCPMsg, "P2PI", 4);
    *((uint16_t*)outgoingTCPMsg + 2) = htons(type);

    if(write(fd, outgoingTCPMsg, 6) < 0)
        die("Failed to send TCP message");
}

void checkTCPPort(std::string newClientName) {
    if(pollFd[tcpFDPOLL].revents == POLLIN) {
        dprint("NEW HOST TRYING TO CONNECT\n", 0);

        struct sockaddr_in tcpClientAddr;
        socklen_t tcpClientAddrLen = sizeof(tcpClientAddr);
        int newConn = accept(tcpSockFd, (struct sockaddr*)&tcpClientAddr, &tcpClientAddrLen);

        dprint("NEW HOST CONNECTED at %d\n", newConn);

        // Push fd to pollfd vector
        struct pollfd newPollFd;
        newPollFd.fd = newConn;
        newPollFd.events = POLLIN;
        pollFd.push_back(newPollFd);
    }
}

void checkUDPPort(int &baseTimeout, int &currTimeout) {
    // Reply message
    uint8_t incomingUDPMsg[MAX_UDP_MSG_LEN];
    udpClientAddrLen = sizeof(udpClientAddr);
    int recvLen;

    // UDP packet
    if(pollFd[udpFDPOLL].revents == POLLIN) {
        recvLen = recvfrom(udpSockFd, incomingUDPMsg, MAX_UDP_MSG_LEN, 0,
            (struct sockaddr*)&udpClientAddr, &udpClientAddrLen);

        if(recvLen > 0) {
            int type = getType(incomingUDPMsg);
            std::string userName_a, hostName_a;
            getHostNUserName(incomingUDPMsg, hostName_a, userName_a);

            dprint("RECV: %d ", type);
            dprint("SRC - %s : %d ", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));
            dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));
           
            switch (type) {
                case DISCOVERY: {
                    // Add host to clientMap if not self discovery and not in
                    // map already

                    // Check if packet is from another host
                    if(memcmp(incomingUDPMsg + 6, outgoingUDPMsg + 6, outgoingUDPMsgLen - 6)) {
                        // Check if host is already discovered
                        if(clientMap.find(userName_a) == clientMap.end()) {
                            printf("\r-----FOUND HOST: %s-----\n", userName_a.c_str());
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
                    if(clientMap.find(userName_a) == clientMap.end()) {
                        printf("\r-----FOUND HOST: %s-----\n", userName_a.c_str());
                        addNewClient(incomingUDPMsg);
                        // Try to initiate tcp connection with host
                        dprint("%s\n", clientMap.begin()->first.c_str());
                        // connectToClient(clientMap.begin()->first);
                    }
                    break;
                }
                case CLOSING: {
                    // Remove host from map
                    std::unordered_map<std::string, struct Client>::iterator it = clientMap.find(userName_a);

                    if(it != clientMap.end()) {
                        printf("\r-----HOST CLOSING: %s-----\n", it->first.c_str());
                        if(it->second.tcpSockFd == currentConnection)
                            currentConnection = -1;

                        clientMap.erase(it);
                    }
                    // If no more host is available, go back to discovery
                    if(clientMap.empty()) {
                        dprint("CLOSING...\n", 0);
                        currTimeout = 0;
                        // Resets timeout value
                        baseTimeout = gMinTimeout;
                        dprint("basetimeout is %d\n", baseTimeout);
                    }
                    break;
                }
            }
        }
    }
}

void checkTCPConnections() {
    for(auto it = pollFd.begin() + 3; it != pollFd.end();) {
        // dprint("Checking %d at %d\n", i, pollFd[i].fd);
        if(it->revents == POLLIN) {
            dprint("%d has something, size %d\n", it->fd, pollFd.size());
            uint8_t incomingTCPMsg[518];
            memset(incomingTCPMsg, 0, 518);
            int recvLen, j = 0;

            while(j < 6) {
                recvLen = read(it->fd, incomingTCPMsg + j, 6 - j);
                j += recvLen;
            }

            int type = getType(incomingTCPMsg);

            dprint("RECV: %d\n", type);
            dprint("SRC - %s : %d ", inet_ntoa(udpClientAddr.sin_addr), ntohs(udpClientAddr.sin_port));
            dprint("DEST - %s : %d\n", inet_ntoa(udpServerAddr.sin_addr), ntohs(udpServerAddr.sin_port));

            switch(type) {
                case ESTABLISH_COMM: {
                    int j = 0, recvLen;
                    char newClientNameArr[32];
                    
                    // Read until the first null byte
                    do {
                        recvLen = read(it->fd, newClientNameArr + j, 1);
                        if(newClientNameArr[j] == 0)
                            break;
                        j++;
                    } while(1);
                    // recvLen = read(it->fd, newClientNameArr, 32);

                    std::string newClientName = newClientNameArr;

                    if(clientMap.find(newClientName) == clientMap.end()) {
                        fprintf(stderr, "\n!!!!!UNKNOWN USER TRYING TO CONNECT!!!!!\n");
                        // Close connection
                        close(it->fd);
                        it = pollFd.erase(it);
                        continue;
                    }

                    uint8_t ECM[6];
                    int ECMLen = 6;
                    memcpy(ECM, "P2PI", 4);

                    if(away == 1 || clientMap.find(newClientName)->second.block) {
                        // Send user unavailable message
                        *((uint16_t*)ECM + 2) = htons(USER_UNAVALIBLE);
                        if(write(it->fd, ECM, 6) < 0) {
                            die("Failed to establish TCP connection.");
                        }

                        // Close connection
                        close(it->fd);
                        it = pollFd.erase(it);
                        continue;
                    }
                    else {
                        // Send accept comm message
                        *((uint16_t*)ECM + 2) = htons(ACCEPT_COMM);
                        clientMap.find(newClientName)->second.tcpSockFd = it->fd;
                        tcpConnMap[it->fd] = newClientName;
                        if(write(it->fd, ECM, 6) < 0) {
                            die("Failed to establish TCP connection.");
                        }
                        printf("\rAccepted connection from %s.\n", newClientName.c_str());

                    }

                    break;
                    // }
                }
                case ACCEPT_COMM: {
                    printf("\rConnected to user %s.\n", tcpConnMap.find(it->fd)->second.c_str());
                    break;
                }
                case USER_UNAVALIBLE: {
                    // Close connection as well
                    printf("\rUser %s is currently unavailable.\n", tcpConnMap.find(it->fd)->second.c_str());
                    if(findClientByFd(it->fd) != clientMap.end())
                        findClientByFd(it->fd)->second.tcpSockFd = -1;
                    if(tcpConnMap.find(it->fd) != tcpConnMap.end()) {
                        tcpConnMap.erase(tcpConnMap.find(it->fd));
                    }
                    close(it->fd);
                    it = pollFd.erase(it);

                    currentConnection = -1;
                    continue;
                }
                case REQUEST_USER_LIST: {
                    printf("\rUser list requested by %s.\n", findClientByFd(it->fd)->second.userName.c_str());
                    uint8_t ECM[10];
                    memcpy(ECM, "P2PI", 4);
                    *((uint16_t*)ECM + 2) = htons(REPLY_USER_LIST);
                    *((uint32_t*)((uint16_t*)ECM + 3)) = htonl(clientMap.size());

                    if(0 > write(it->fd, ECM, 10))
                        die("Failed to send user list reply");

                    uint8_t userEntry[8 + 256 + 32 + 2];
                    int i = 0;
                    for(auto c : clientMap) {
                        memset(userEntry, 0, 8 + 256 + 32 + 2);

                        int len2Send = 6;
                        *((uint32_t*)userEntry) = htonl(i);
                        *((uint16_t*)userEntry + 2) = htons(c.second.udpPort);
                        memcpy(userEntry + 6, c.second.hostName.c_str(), c.second.hostName.length());
                        len2Send += c.second.hostName.length() + 1;

                        *((uint16_t*)(userEntry + len2Send)) = htons(c.second.tcpPort);
                        len2Send += 2;

                        memcpy(userEntry + len2Send, c.second.userName.c_str(), c.second.userName.length());
                        len2Send += c.second.userName.length() + 1;

                        if(0 > write(it->fd, userEntry, len2Send))
                            die("Failed to send user list entry");
                       
                        i++;
                    }
                    break;
                }
                case REPLY_USER_LIST: {
                    // Get the entry count
                    int j = 0, recvLen;
                    uint8_t entryCountArr[5];
                    memset(entryCountArr, 0, 5);
                    recvLen = read(it->fd, entryCountArr, 4);

                    int entryCount = ntohl(*(uint32_t*)entryCountArr);
                    uint8_t entryArr[8 + 256 + 32 + 2];

                    for(int k = 0; k < entryCount; k++) {
                        // Get entry number
                        recvLen = read(it->fd, entryArr, 4);
                        int entryNum = ntohl(*(uint32_t*)entryArr);
                        dprint("entry num is %d\n", entryNum);

                        struct Client newClient;
                        recvLen = read(it->fd, entryArr + 4, 2);
                        newClient.udpPort = ntohs(*((uint16_t*)entryArr + 2));
                        dprint("udpPort is %d\n", newClient.udpPort);

                        int n = 0;
                        // Get hostname up to the first null byte
                        do {
                            recvLen = read(it->fd, entryArr + 6 + n, 1);
                            dprint("%d %c\n", n, *(char*)(entryArr + 6 + n));
                            if(entryArr[6 + n] == 0)
                                break;
                            n++;
                        } while(1);

                        newClient.hostName = (char*)(entryArr + 6);
                        dprint("hostname is %s\n", newClient.hostName.c_str());
                        recvLen = read(it->fd, entryArr + 6 + newClient.hostName.length() + 1, 2);
                        newClient.tcpPort = ntohs(*((uint16_t*)(entryArr + 6 + newClient.hostName.length() + 1)));
                        dprint("tcpPort is %d\n", newClient.tcpPort);

                        n = 0;
                        // get username
                        do {
                            recvLen = read(it->fd, entryArr + 6 + n + newClient.hostName.length() + 1 + 2, 1);
                            dprint("%d %c\n", n, *(char*)(entryArr + 6 + n + newClient.hostName.length() + 1 + 2));

                            if(entryArr[6 + n + newClient.hostName.length() + 1 + 2] == 0)
                                break;
                            n++;
                        } while(1);

                        newClient.userName = (char*)(entryArr + 6 + newClient.hostName.length() + 2 + 1);

                        // printf("username %s, hostname %s, tcp %d, udp %d\n", newClient.userName.c_str(), newClient.hostName.c_str(), newClient.tcpPort, newClient.udpPort);

                        if(newClient.userName != userName && clientMap.find(newClient.userName) == clientMap.end())
                            clientMap[newClient.userName] = newClient;
                    }
                    generateList();
                    printf("\r%s\n", list.c_str());
                    break;
                }
                case DATA: {
                    // Read in data
                    int j = 0, recvLen;
                    std::string dataBuffer = "";
                    char dataMsg[513];

                    while(1) {
                        recvLen = read(it->fd, dataMsg + j, 1);
                        if(dataMsg[j] == '\0')
                            break;
                        j++;
                        if(j == 512) {
                            dataMsg[j] = '\0';
                            dataBuffer += dataMsg;
                            dprint("current buffer %s.\n", dataBuffer.c_str());
                            j = 0;
                        }
                    }
                    dprint("recvLen is %d\n", j);


                    // while(1) {
                    //     printf("Hmmmmm\n");
                    //     recvLen = read(it->fd, dataMsg, 512);
                    //     dprint("recvLen %d\n", recvLen);
                    //     for(int l = 0; l < recvLen; l++) {
                    //         dprint("%c\n", dataMsg[l]);
                    //     }
                    //     if(dataMsg[recvLen - 1] == '\0')
                    //         break;

                    //     // j++;

                    //     if(recvLen == 512) {
                    //         dataMsg[512] = '\0';
                    //         dataBuffer += dataMsg;
                    //         dprint("current buffer %s\n", dataBuffer.c_str());
                    //         // j = 0;
                    //     }

                    // }
                    // dprint("recvLen is %d\n", recvLen);

                    dataBuffer += dataMsg;
                    
                    dprint("User %s message.\n", tcpConnMap.find(it->fd)->second.c_str());
                    printf("\r%s>%s\n", tcpConnMap.find(it->fd)->second.c_str(), dataBuffer.c_str());
                    break;
                }
                case DISCONTINUE_COMM: {
                    printf("\rUser %s wants to disconnect.\n", tcpConnMap.find(it->fd)->second.c_str());
                    dprint("map size %d\n", clientMap.size());
                    // for(auto a : clientMap) {
                    //     dprint("User listing: %s\n", a.second.userName.c_str());
                    // }
                    if(findClientByFd(it->fd) != clientMap.end())
                        findClientByFd(it->fd)->second.tcpSockFd = -1;
                    tcpConnMap.erase(tcpConnMap.find(it->fd));

                    if(it->fd == currentConnection)
                        currentConnection = -1;

                    close(it->fd);
                    it = pollFd.erase(it);
                    continue;
                }
                default: {
                    fprintf(stderr, "\rINVALID MESSAGE\n.", 0);
                    break;
                }
            }
        }
        it++;
    }
}

void checkSTDIN() {
    if(pollFd[terminalFDPOLL].revents & POLLIN) {
        ioctl(STDOUT_FILENO,TIOCGWINSZ,&size);
        numcol = size.ws_col; //size of the terminal (column size)
        bytesRead = read(STDIN_FILENO, &RX, 4);
        if (bytesRead < 2)
        buffer.append((const char*)RX);
        //dprint("buffer len: %d\n", buffer.length());
        //dprint("buffer: %c\n", buffer[0]);
        if(bytesRead == 1) {
            //printf("%c",buffer[0]);
            fflush(STDIN_FILENO);

            if(buffer[0] == 0x7F) { //delete char
                if(message.length() > 0) {
                    message.erase(message.length()-1);
                    printf("\033[1D  "); //clears current and next char in terminal
                }
            }
            else if(buffer[0] != '\n')
                message += buffer[0];
			
            if(buffer[0] == '\n') { //send message
                //echos current message onto terminal
                printf("\r%s>%s",userName.c_str(), message.c_str());
                
                std::string firstWord= message.substr(0, message.find_first_of(" ",0));
                if(commandMap.find(firstWord) != commandMap.end()) {
                    switch(commandMap[firstWord]) {
                        case CONNECT: {
                            std::string target="";
							if(message.find_first_of(" ",0) == std::string::npos){
								printf("\nNo users specified.");
								break;
							}
							target = message.substr(firstWord.length()+1, message.find_first_of(" ",0));
							
							if(target.length() < 1) {
								printf("\nNo users specified.");
								break;
							}
                            // dprint("\nLooking for user: %s\n", target.c_str());
                            if(clientMap.find(target) != clientMap.end()) {
                                if(clientMap.find(target)->second.tcpSockFd == -1) {
									connectToClient(target);
                                    currentConnection = clientMap.find(target)->second.tcpSockFd;
									// printf("\nConnected to user: %s.", target.c_str());
								}
								else
									currentConnection = clientMap.find(target)->second.tcpSockFd;
                            }
                            else
                                printf("\nNo user '%s' found.", target.c_str());

                            break;
                        }
                        case GETLIST: {
							if(currentConnection != -1) {
								// uint8_t outgoingTCPMsg[6];  //max length data is 512
								// bzero(outgoingTCPMsg, 6);
								// uint16_t type = htons(REQUEST_USER_LIST);
								// memcpy(outgoingTCPMsg, "P2PI", 4);
								// memcpy(outgoingTCPMsg + 4, &type, 2);

								//target is the fd that the client wishes to speak to.
								// if(write(currentConnection, outgoingTCPMsg, 6) < 0)
								// 	die("Failed to establish send data.");
                                sendTCPMessage(REQUEST_USER_LIST, currentConnection);
                            }
                            else
                                printf("\nNo connection.");

                            break;
						}
						case SWITCH: {							
							std::string target="";
							if(message.find_first_of(" ",0) == std::string::npos) {
								printf("\nNo users specified.");
								break;
							}
							target = message.substr(firstWord.length()+1, message.find_first_of(" ",0));
							
							if(target.length() < 1) {
								printf("\nNo users specified.");
								break;
							}
							if(clientMap.find(target) != clientMap.end() && clientMap.find(target)->second.tcpSockFd != -1)
								currentConnection = clientMap.find(target)->second.tcpSockFd;
							else
                                printf("\nRequires a valid connection.", target.c_str());
                            
                            break;
						}
						case DISCONNECT: {						
							if(currentConnection != -1){
								printf("\nClosing connection.");
								
								// uint8_t outgoingTCPMsg[6];
								// memcpy(outgoingTCPMsg, "P2PI", 4);
								// *((uint16_t*)outgoingTCPMsg + 2) = htons(DISCONTINUE_COMM);

								// if(write(currentConnection, outgoingTCPMsg, 6) < 0)
								// 	die("Failed to send TCP message");
                                sendTCPMessage(DISCONTINUE_COMM, currentConnection);
								
								if(tcpConnMap.find(currentConnection) != tcpConnMap.end()) {
									std::string connName = tcpConnMap.find(currentConnection)->second;
									close(clientMap.find(connName)->second.tcpSockFd);
									
									for(auto it = pollFd.begin(); it != pollFd.end(); it++) {
										if(it->fd == currentConnection){
											pollFd.erase(it);
											break;
										}
									}
									tcpConnMap.erase(currentConnection);
									clientMap.find(connName)->second.tcpSockFd = -1;
								}
								else
									printf("\nCurrent Connection no longer exists.");
                                currentConnection = -1;
							}
							else
                                printf("\nConnection must be the current active connection.");
						
                            break;
						}
						case LIST: {
                            generateList();
                            printf("\n%s", list.c_str());
                            break;
                        }
                        case HELP: {
                            printf("\nList of Commands:\n\\connect username \n\t-establishes connection to a user\n\\disconnect \n\t-closes communication channel between current connection \n\\switch user \n\t-redirect messages to the specified user if a connection has been established. \n\\getlist \n\t- gets the list of users from current connection \n\\list \n\t- gets your current userlist\n\\help\n\t-it's a mystery\n\\away \n\t-Sets self to away\n\\unaway\n\t-brings self back from away.");
                            break;
                        }
                        case AWAY: {
                            for(auto c: tcpConnMap) {
                                // uint8_t outgoingTCPMsg[6];
                                // memcpy(outgoingTCPMsg, "P2PI", 4);
                                // *((uint16_t*)outgoingTCPMsg + 2) = htons(USER_UNAVALIBLE);

                                // if(write(c.first, outgoingTCPMsg, 6) < 0)
                                //     die("Failed to send away message");

                                sendTCPMessage(USER_UNAVALIBLE, c.first);
                                clientMap.find(c.second)->second.tcpSockFd = -1;
                            }

                            tcpConnMap.clear();
                            for(auto it = pollFd.begin() + 3; it != pollFd.end();)
                                it = pollFd.erase(it);
                            away = 1;
                            break;
                        }
                        case UNAWAY: {
                            away = 0;
                            break;
                        }

                        case BLOCK: {
                            std::string target="";
                            if(message.find_first_of(" ",0) == std::string::npos) {
                                printf("\nNo users specified.");
                                break;
                            }
                            target = message.substr(firstWord.length()+1, message.find_first_of(" ",0));
                            
                            if(target.length() < 1) {
                                printf("\nNo users specified.");
                                break;
                            }
                            // Find user
                            if(clientMap.find(target) != clientMap.end()) {
                                // uint8_t outgoingTCPMsg[6];
                                // memcpy(outgoingTCPMsg, "P2PI", 4);
                                // *((uint16_t*)outgoingTCPMsg + 2) = htons(DISCONTINUE_COMM);

                                // if(write(clientMap.find(target)->second.tcpSockFd, outgoingTCPMsg, 6) < 0)
                                //     die("Failed to send TCP message");
                                sendTCPMessage(DISCONTINUE_COMM, clientMap.find(target)->second.tcpSockFd);
                                clientMap.find(target)->second.block = 1;

                                close(clientMap.find(target)->second.tcpSockFd);
                                for(auto it = pollFd.begin(); it != pollFd.end(); it++) {
                                    if(it->fd == clientMap.find(target)->second.tcpSockFd){
                                        pollFd.erase(it);
                                        break;
                                    }
                                }
                                tcpConnMap.erase(clientMap.find(target)->second.tcpSockFd);
                                clientMap.find(target)->second.tcpSockFd = -1;
                            }
                            // User not found
                            else
                                printf("\nUser %s not found.", target.c_str());
                            
                            break;
                        }
                        case UNBLOCK: {
                            std::string target="";
                            if(message.find_first_of(" ",0) == std::string::npos) {
                                printf("\nNo users specified.");
                                break;
                            }
                            target = message.substr(firstWord.length()+1, message.find_first_of(" ",0));
                            
                            if(target.length() < 1) {
                                printf("\nNo users specified.");
                                break;
                            }
                            // Find user
                            if(clientMap.find(target) != clientMap.end())
                               clientMap.find(target)->second.block = 0; 
                            // User not found
                            else
                                printf("\nUser %s not found.", target.c_str());
                            
                            break;
                        }
                        case EXIT: {
                            raise(SIGINT);
                        }
					}
                }
                 
                else if(currentConnection != -1)
                    sendDataMessage(message);
                else
                    printf("\nNo connection established, to connect use: \\connect Username.");
                message.clear();
                printf("\n\033[1B"); //prints down key.
            }
                //eraselines(message.length()/numcol);
        }
        clearline();
		std::string connName;
		if(tcpConnMap.find(currentConnection) == tcpConnMap.end())
			connName = "";
		else
			connName = tcpConnMap.find(currentConnection)->second;

        if(message.length()+connName.length()+1 > numcol) //simulate loop
            printf("%s>%s", connName.c_str(), message.substr(message.length()+connName.length()+1-numcol, numcol).c_str());
        else
            printf("%s>%s", connName.c_str(), message.c_str());
        //printf("\033[0C");
        fflush(STDIN_FILENO);
        buffer.clear();
    }
}

void sendDataMessage(std::string message){
    uint8_t outgoingTCPMsg[6 + message.length() + 1];  //max length data is 512
    bzero(outgoingTCPMsg, 6 + message.length() + 1);
    uint16_t type = htons(DATA);
    memcpy(outgoingTCPMsg, "P2PI", 4);
    memcpy(outgoingTCPMsg + 4, &type, 2);
    memcpy(outgoingTCPMsg + 6, message.c_str(), message.length());

    //currentConnection is the fd that the client wishes to speak to.
    if(write(currentConnection,outgoingTCPMsg, message.length() + 7) < 0)
        die("Failed to establish send data.");

    message.clear();
}

void generateList() {
    list = "";
    int c = 0;
    dprint("Map size is: %d\n", clientMap.size());
    for( auto i : clientMap )
    {
        list.append("User ");
        list.append(" ");
        list.append( std::to_string(c) );
        list.append(" ");
        list.append(i.second.userName);

        list.append("@");
        list.append(i.second.hostName);
        list.append(" on UDP ");
        list.append(std::to_string(i.second.udpPort));
        list.append(" , TCP ");
        list.append(std::to_string(i.second.tcpPort));
        if(i.second.block)
            list.append(" , BLOCKED");
        // list.append("\n");
        c++;
    }
}

void clearline() {
    printf("\r");
    ioctl(STDOUT_FILENO,TIOCGWINSZ,&size);
    int COLS = size.ws_col;

    char str[COLS+1];
    bzero(str, COLS+1);
    memset(str, ' ', COLS-2);
    printf("%s\r",str);
}
