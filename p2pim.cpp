#include "p2pim.h"
#include "EncryptionLibrary.h"
//#define DEBUG 1


#define tprint(string, ...) {clearline(); printf(string, ##__VA_ARGS__);}
#define MAX_UDP_MSG_LEN (10 + 254 + 32 + 2)



enum Option {USERNAME, UDP_PORT, TCP_PORT, MIN_TIMEOUT, MAX_TIMEOUT, HOST, TA_UDP_PORT, AUTH_HOST};
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
    {"-dt", MIN_TIMEOUT}, {"-dm", MAX_TIMEOUT}, {"-pp", HOST}, {"-ap", TA_UDP_PORT}, {"-ah", AUTH_HOST}
};

static std::unordered_map<std::string, int> commandMap {
    {"\\connect", CONNECT}, {"\\c", CONNECT}, {"\\switch", SWITCH},
    {"\\list", LIST}, {"\\disconnect", DISCONNECT}, {"\\getlist", GETLIST},
    {"\\help", HELP}, {"\\away", AWAY}, {"\\unaway", UNAWAY},
    {"\\block", BLOCK}, {"\\unblock", UNBLOCK}, {"\\exit", EXIT}
};


std::string userName = getenv("USER");
std::string hostName;
uint64_t public_key, private_key, public_key_modulus;
int udpPort = 50550;
int tcpPort = 50551;
int taUDPPort = 50552;
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


std::vector<struct sockaddr_in> taVector;
uint8_t reqAuthMsg[46];
uint32_t secretNum;
uint64_t pubKey;
uint64_t modulus;

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


    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);

	login_prompt();
	
    //tprint("Please type in \"\\help\" for a list of commands available\n");

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

					tprint("sending auth discovery\n");
                // Send request authenicated key message
                
                    int reqAuthMsgLen = 14 + userName.length() + 1;

                    memset(reqAuthMsg, 0, 46);
                    memcpy(reqAuthMsg, "P2PI", 4);
                    *((uint16_t*)(reqAuthMsg + 4)) = htons(REQUEST_AUTH_KEY);

                    // Generating random
                    secretNum = GenerateRandomValue();

                    while(secretNum == 0)
                        secretNum = GenerateRandomValue();
					
                    uint64_t secretData= secretNum;
                    // TODO: secretNum is a 32bit value, but the 1st parameter of PublicEncryptDecrypt
                    // should be a 64bit value, double check if not work
                    PublicEncryptDecrypt(secretData, P2PI_TRUST_E, P2PI_TRUST_N);
					tprint("encrypted Secret: %lu\n",secretData);
					
                    *((uint64_t*)(reqAuthMsg + 6)) =  htonl(secretData>>32);
                    *((uint64_t*)(reqAuthMsg + 10)) =  htonl(secretData);
                    memcpy(reqAuthMsg + 14, userName.c_str(), userName.length());
					tprint("out packet: \n");
							for(int i =0; i < 18; i++)
							{
								tprint("%d %c\n", reqAuthMsg[i],reqAuthMsg[i]);
							}
					
					
                    // Do actually sending
                    // if there is a trust anchor specified
                    if(!taVector.empty()) {
                        tprint("Unicasting\n");

                        struct sockaddr_in taAddr = *taVector.begin();
                        if(sendto(udpSockFd, reqAuthMsg, reqAuthMsgLen, 0, (struct sockaddr*)&taAddr, sizeof(*taVector.begin())) < 0) {
                            die("Failed to unicast trust anchor discovery");
                        }
                    }
                    // else, broadcast
                    else {
						dprint("sending auth discovery\n");
                        // change dest port to trust anchor udp port
                        udpServerAddr.sin_port = htons(taUDPPort);
                        udpServerAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

                        if(sendto(udpSockFd, reqAuthMsg, reqAuthMsgLen, 0, (struct sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
                            die("Failed to broadcast trust anchor discovery");
                        }

                        // reset dest port
                        udpServerAddr.sin_port = htons(udpPort);
                    }

                


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
		{
            printf("%s>%s", connName.c_str(), message.substr(message.length()-numcol+connName.length()+1, numcol).c_str());
        }
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
                dprint("TIMEOUT: \n");
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
            fprintf(stderr, "\nPOLL ERROR\n");
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
        fprintf(stderr, "\nFailed to find subnet mask.\n");
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
    return ntohs(*((uint16_t*)(message + 4)));
}

void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName) {
    hostName = (char*)(message + 10);
    userName = (char*)(message + 10 + hostName.length() + 1);
}

void getPorts(uint8_t* message, int& udpPort, int& tcpPort) {
    udpPort = ntohs(*((uint16_t*)(message + 6)));
    tcpPort = ntohs(*((uint16_t*)(message + 8)));
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
    *((uint16_t*)(outgoingUDPMsg + 4)) = htons(type);
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
        tprint("Bye...\n");
        ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
        exit(0);
    }
}

void parseOptions(int argc, char** argv) {
    std::string optErr;
    // Get default hostname, IP, and subnet mask
    getClientName();

    int isAHPort = 0;

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
                    case TA_UDP_PORT: {
                        optionMap[argv[i]] = -1;
                        checkIsNum(argv[i + 1]);
                        checkPortRange(atoi(argv[i + 1]));

                        if(!isAHPort)
                            taUDPPort = atoi(argv[i + 1]);

                        break;
                    }
                    case AUTH_HOST: {
                        std::string tmpArgv = argv[i + 1];
                        // Parse hostname part and port num part
                        std::size_t pos = tmpArgv.find(":");
                        tprint("pos is %lu\n", pos);
                        // If not hostname is provided
                        if(pos == 0 || pos == tmpArgv.length() - 1) {
                            die("Invalid argument -ah");
                        }
                        else {
                            std::string taHost = argv[i + 1];
                            tprint("taHost is %s\n", taHost.c_str());
                            

                            // If port number is provided
                            if(pos != std::string::npos) {
                                std::string taUDPPortStr = taHost.substr(pos + 1);
                                checkIsNum(taUDPPortStr.c_str());
                                taHost = taHost.substr(0, pos);
                                taUDPPort = std::stoi(taUDPPortStr);
                            }

                            struct hostent* remoteHostEntry = gethostbyname(taHost.c_str());
                            if(!remoteHostEntry)
                                die("Failed to resolve host");

                            struct in_addr remoteAddr;
                            struct sockaddr_in trustAnchorAddr;
                            memcpy(&remoteAddr, remoteHostEntry->h_addr, remoteHostEntry->h_length);
                            trustAnchorAddr.sin_addr = remoteAddr;
                            trustAnchorAddr.sin_port = htons(taUDPPort);
                            trustAnchorAddr.sin_family = AF_INET;

                            taVector.push_back(trustAnchorAddr);
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
    *((uint16_t*)(outgoingUDPMsg + 6)) = htons(udpPort);
    *((uint16_t*)(outgoingUDPMsg + 8)) = htons(tcpPort);
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

        dprint("CONNECTED TO NEW HOST\n");

        // Send ESTABLISH COMMUNICATION MSG
        uint8_t ECM[39];
        int ECMLen = 4 + 2 + userName.length() + 1;

        memset(ECM, 0, ECMLen);
        memcpy(ECM, "P2PI", 4);
        *((uint16_t*)(ECM + 4)) = htons(ESTABLISH_COMM);
        memcpy((uint16_t*)(ECM + 6), userName.c_str(), userName.length());
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
        tprint("Port in used\n");
        tcpServerAddr.sin_port = 0;
        if(-1 == bind(tcpSockFd, (struct sockaddr*)&tcpServerAddr, sizeof(tcpServerAddr))) {
            printf("Port in used\n");
            die("No port is available.");
        }
        socklen_t len = sizeof(tcpServerAddr);
        if(getsockname(tcpSockFd, (struct sockaddr *)&tcpServerAddr, &len) == -1)
            die("Failed to get sock name.");

        tprint("New socket %d\n", tcpServerAddr.sin_port);
        tcpPort = ntohs(tcpServerAddr.sin_port);
    }

    udpServerAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    listen(tcpSockFd, 5);
}

void sendTCPMessage(int type, int fd) {
    uint8_t outgoingTCPMsg[6];
    memcpy(outgoingTCPMsg, "P2PI", 4);
    *((uint16_t*)(outgoingTCPMsg + 4)) = htons(type);

    if(write(fd, outgoingTCPMsg, 6) < 0)
        die("Failed to send TCP message");
}

void checkTCPPort(std::string newClientName) {
    if(pollFd[tcpFDPOLL].revents == POLLIN) {
        dprint("NEW HOST TRYING TO CONNECT\n");

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
		tprint("inc packet: \n");
		for(int i =0; i < recvLen; i++)
		{
			tprint("%d %c\n", incomingUDPMsg[i],incomingUDPMsg[i]);
		}
        if(recvLen > 4 && memcmp("P2PI", incomingUDPMsg, 4) == 0) {
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
                            tprint("\r-----NEW HOST: %s-----\n", userName_a.c_str());
                            addNewClient(incomingUDPMsg);
                        }
                        // Should send reply regardless the host is already known
                        sendUDPMessage(REPLY);
                    }
                    else
                        dprint("-----SELF DISCOVERY-----\n");

                    break;
                }
                case REPLY: {
                    // Add host to map if not in map already
                    if(clientMap.find(userName_a) == clientMap.end()) {
                        tprint("\r-----NEW HOST: %s-----\n", userName_a.c_str());
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
                        tprint("\rClient %s is closing\n", it->first.c_str());
                        if(it->second.tcpSockFd == currentConnection)
                            currentConnection = -1;

                        clientMap.erase(it);
                    }
                    // If no more host is available, go back to discovery
                    if(clientMap.empty()) {
                        dprint("CLOSING...\n");
                        currTimeout = 0;
                        // Resets timeout value
                        baseTimeout = gMinTimeout;
                        dprint("basetimeout is %d\n", baseTimeout);
                    }
                    break;
                }
                case REPLY_AUTH_KEY: {
                    tprint("hi\n");
                    if(!memcmp(incomingUDPMsg + 6, reqAuthMsg + 6, 8 + userName.length())) {
                        // Parse public key
                        pubKey = ntohl(*((uint64_t*)(reqAuthMsg + 14 + userName.length() + 1)));
                        // Modulus
                        modulus = ntohl(*((uint64_t*)(reqAuthMsg + 14 + userName.length() + 9)));
                        // Checksum
                        uint64_t checkSum = ntohl(*((uint64_t*)(reqAuthMsg + 14 + userName.length() + 17)));

                        if(checkSum == AuthenticationChecksum(secretNum, (const char*)reqAuthMsg, P2PI_TRUST_E, P2PI_TRUST_N)) {
                            tprint("PUB KEY: %lu M: %lu", pubKey, modulus); 
                        }
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

            // Invalid signature, close connection
            if(memcmp("P2PI", incomingTCPMsg, 4) != 0) {
                sendTCPMessage(DISCONTINUE_COMM, it->fd);

                if(findClientByFd(it->fd) != clientMap.end())
                        findClientByFd(it->fd)->second.tcpSockFd = -1;
                tcpConnMap.erase(tcpConnMap.find(it->fd));

                if(it->fd == currentConnection) {
                    currentConnection = -1;
                }

                close(it->fd);
                it = pollFd.erase(it);
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
                        // fprintf(stderr, "\n!!!!!UNKNOWN USER TRYING TO CONNECT!!!!!\n");
                        sendTCPMessage(USER_UNAVALIBLE, it->fd);

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
                        *((uint16_t*)(ECM + 4)) = htons(USER_UNAVALIBLE);
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
                        *((uint16_t*)(ECM + 4)) = htons(ACCEPT_COMM);
                        clientMap.find(newClientName)->second.tcpSockFd = it->fd;
                        tcpConnMap[it->fd] = newClientName;
                        if(write(it->fd, ECM, 6) < 0) {
                            die("Failed to establish TCP connection.");
                        }
                        tprint("Accepting connection from: %s\n", newClientName.c_str());

                    }

                    break;
                    // }
                }
                case ACCEPT_COMM: {
                    tprint("Connected to user %s\n", tcpConnMap.find(it->fd)->second.c_str());
                    break;
                }
                case USER_UNAVALIBLE: {
                    // Close connection as well
                    tprint("The user %s is currently unavailable\n", tcpConnMap.find(it->fd)->second.c_str());
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
                    tprint("User list requested\n");
                    dprint("hi\n");
                    uint8_t ECM[10];
                    memcpy(ECM, "P2PI", 4);
                    *((uint16_t*)(ECM + 4)) = htons(REPLY_USER_LIST);
                    *((uint32_t*)(ECM + 6)) = htonl(clientMap.size());

                    if(0 > write(it->fd, ECM, 10))
                        die("Failed to send user list reply");

                    uint8_t userEntry[8 + 256 + 32 + 2];
                    int i = 0;
                    for(auto c : clientMap) {
                        memset(userEntry, 0, 8 + 256 + 32 + 2);

                        int len2Send = 6;
                        *((uint32_t*)userEntry) = htonl(i);
                        *((uint16_t*)(userEntry + 4)) = htons(c.second.udpPort);
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

                    // do {
                    //     recvLen = read(it->fd, entryCountArr + j, 1);
                    //     j++;
                    // } while(j < 4);
                    recvLen = read(it->fd, entryCountArr, 4);


                    int entryCount = ntohl(*(uint32_t*)entryCountArr);

                    dprint("Entry count is %d", entryCount);

                    uint8_t entryArr[8 + 256 + 32 + 2];
                    for(int k = 0; k < entryCount; k++) {
                        // Get entry number
                        recvLen = read(it->fd, entryArr, 4);
                        int entryNum = ntohl(*(uint32_t*)entryArr);
                        dprint("entry num is %d\n", entryNum);

                        struct Client newClient;
                        recvLen = read(it->fd, entryArr + 4, 2);
                        newClient.udpPort = ntohs(*((uint16_t*)(entryArr + 4)));
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

                        // tprint("username %s, hostname %s, tcp %d, udp %d\n", newClient.userName.c_str(), newClient.hostName.c_str(), newClient.tcpPort, newClient.udpPort);

                        if(newClient.userName != userName && clientMap.find(newClient.userName) == clientMap.end())
                            clientMap[newClient.userName] = newClient;
                    }
                    generateList();
                    tprint("\n%s\n", list.c_str());
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
                            dprint("current buffer %s\n", dataBuffer.c_str());
                            j = 0;
                        }
                    }
                    dprint("recvLen is %d\n", j);


                    // while(1) {
                    //     tprint("Hmmmmm\n");
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
					clearline();
                    printf("%s>%s\n", tcpConnMap.find(it->fd)->second.c_str(), dataBuffer.c_str());
                    break;
                }
                case DISCONTINUE_COMM: {
                    tprint("User %s wants to disconnect.\n", tcpConnMap.find(it->fd)->second.c_str());
                    dprint("map size %d\n", clientMap.size());
                    for(auto a : clientMap) {
                        dprint("User listing: %s\n", a.second.userName.c_str());
                    }
                    if(findClientByFd(it->fd) != clientMap.end())
                        findClientByFd(it->fd)->second.tcpSockFd = -1;
                    tcpConnMap.erase(tcpConnMap.find(it->fd));

                    if(it->fd == currentConnection) {
                        currentConnection = -1;
                    }

                    close(it->fd);
                    it = pollFd.erase(it);
                    continue;
                }
                default: {
                    fprintf(stderr, "\nINVALID MESSAGE\n");
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
            //tprint("%c",buffer[0]);
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
				clearline();
                printf("%s>%s\n",userName.c_str(), message.c_str());

                std::string firstWord = message.substr(0, message.find_first_of(" ",0));
                if(commandMap.find(firstWord) != commandMap.end()) {
                    switch(commandMap[firstWord]) {
                        case CONNECT: {
                           std::string target;
							if(-1==getTarget(target))
							{
								tprint("No users specified.\n");
								break;
							}
                            tprint("Looking for user: %s\n", target.c_str());
							auto client = clientMap.find(target);
                            if( client != clientMap.end() ) {
								if(client->second.block) {
									tprint("User is blocked, unblock to connect.\n")
									break;
								}
                                if(client->second.tcpSockFd == -1) {
									connectToClient(target);
                                    currentConnection = client->second.tcpSockFd;
								}
								else{
									tprint("Connected to user: %s\n", target.c_str());
									currentConnection = client->second.tcpSockFd;
								}
                            }
                            else
                                tprint("No user '%s' found.\n", target.c_str());

                            break;
                        }
                        case GETLIST: {
							if(currentConnection != -1) {
								uint8_t outgoingTCPMsg[6];  //max length data is 512
								bzero(outgoingTCPMsg, 6);
								uint16_t type = htons(REQUEST_USER_LIST);
								memcpy(outgoingTCPMsg, "P2PI", 4);
								memcpy(outgoingTCPMsg + 4, &type, 2);

								 //target is the fd that the client wishes to speak to.
								if(write(currentConnection, outgoingTCPMsg, 6) < 0){
									die("Failed to establish send data.\n");
									break;
								}
                            }
                            else
                                tprint("No connection.\n");

                            break;
						}
						case SWITCH: {
							std::string target;
							if(-1 == getTarget(target))
							{
								tprint("No users specified.\n");
								break;
							}
							if(clientMap.find(target) != clientMap.end() && clientMap.find(target)->second.tcpSockFd != -1)
								currentConnection = clientMap.find(target)->second.tcpSockFd;
							else
                                tprint("Requires a valid connection.\n");

                            break;
						}
						case DISCONNECT: {
							if(currentConnection != -1){
								tprint("Closing connection with %s\n", tcpConnMap.find(currentConnection)->second.c_str());

								uint8_t outgoingTCPMsg[6];
								memcpy(outgoingTCPMsg, "P2PI", 4);
								*((uint16_t*)(outgoingTCPMsg + 4)) = htons(DISCONTINUE_COMM);

								if(write(currentConnection, outgoingTCPMsg, 6) < 0){
									die("Failed to send TCP message.");
									//TODO:
									break;
								}
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
									tprint("Current Connection no longer exists.\n");
                                currentConnection = -1;
							}
							else
                                tprint("Connection must be the current active connection.\n");

                            break;
						}
						case LIST: {
                            generateList();
                            tprint("%s", list.c_str());
                            break;
                        }
                        case HELP: {
                            tprint("List of Commands:\n\\connect username \n\t-establishes connection to a user\n");
                            tprint("\\disconnect \n\t-closes communication channel between current connection\n");
                            tprint("\\switch username \n\t-redirect messages to the specified user if a connection\n\t has been established\n");
                            tprint("\\getlist \n\t-gets the list of users from current connection\n");
                            tprint("\\list \n\t-gets your current userlist\n");
                            tprint("\\help\n\t-it's a mystery\n");
                            tprint("\\away \n\t-sets self to away\n");
                            tprint("\\unaway\n\t-brings self back from away.\n");
                            tprint("\\block username\n\t-when you don't want to talk to that person\n");
                            tprint("\\unblock username\n\t-when you want to become friend with someone again\n");
                            break;
                        }


                        case AWAY: {
							int onFail = 0;
                            for(auto c: tcpConnMap) {
                                uint8_t outgoingTCPMsg[6];
                                memcpy(outgoingTCPMsg, "P2PI", 4);
                                *((uint16_t*)(outgoingTCPMsg + 4)) = htons(USER_UNAVALIBLE);

                                if(write(c.first, outgoingTCPMsg, 6) < 0){
									die("Failed to send away message.\n");
								}

                                clientMap.find(c.second)->second.tcpSockFd = -1;
                            }
							tprint("Set status away\n");
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
							std::string target;
							if(-1 == getTarget(target))
							{
								tprint("No users specified.\n");
								break;
							}
                            // Find user
							auto client = clientMap.find(target);
                            if(client != clientMap.end()) {
                                uint8_t outgoingTCPMsg[6];
                                memcpy(outgoingTCPMsg, "P2PI", 4);
                                *((uint16_t*)(outgoingTCPMsg + 4)) = htons(DISCONTINUE_COMM);

                                if(client->second.tcpSockFd != -1 && write(client->second.tcpSockFd, outgoingTCPMsg, 6) < 0){
                                    die("Failed to send TCP message");
								}

                                client->second.block = 1;

                                close(client->second.tcpSockFd);
                                for(auto it = pollFd.begin(); it != pollFd.end(); it++) {
                                    if(it->fd == client->second.tcpSockFd){
                                        pollFd.erase(it);
                                        break;
                                    }
                                }
                                tcpConnMap.erase(client->second.tcpSockFd);
                                if(currentConnection == client->second.tcpSockFd) {
                                    currentConnection = -1;
                                }
                                client->second.tcpSockFd = -1;
                            }
                            // User not found
                            else
                                tprint("User %s not found.\n", target.c_str());

                            break;
                        }
                        case UNBLOCK: {
                            std::string target;
							if(-1==getTarget(target))
							{
								tprint("No users specified.\n");
								break;
							}
                            // Find user
                            if(clientMap.find(target) != clientMap.end())
                               clientMap.find(target)->second.block = 0;
                            // User not found
                            else
                                tprint("User %s not found.\n", target.c_str());

                            break;
                        }
                        case EXIT: {
                            raise(SIGINT);
                        }
					}
                }
                else if(currentConnection != -1){
                    sendDataMessage(message);
				}
                else{
                    tprint("No connection established, to connect use: \\connect Username\n");
				}
                message.clear();
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
        list += "User " + std::to_string(c) + " " + i.second.userName +"@" +
		i.second.hostName + " on UDP " + std::to_string(i.second.udpPort) + ", TCP " + std::to_string(i.second.tcpPort) + (i.second.block ? " Blocked\n" : "") + (i.second.tcpSockFd != -1 ? " Connected\n" : "\n");
        c++;
    }
}

int getTarget(std::string &target)
{
	target="";
	int pos = 0;
	if( (pos = message.find(" ")) == std::string::npos) { //loook for first ' '
		return -1;
	}
	target = message.substr(pos+1);
	if( (pos = target.find(" ")) != std::string::npos) { //loook for next ' '
		target = message.substr(target.length()+1, pos);
	}
	if(target.length() < 1) { // string is ''
		return -1;
	}
}

void clearline() {
    printf("\33[2K\r");
}

void login_prompt()
{
	std::string password;
	tprint("Enter password for %s>", userName.c_str());
	fflush(STDIN_FILENO);
	std::string passwordmask = "";
	while(1){

	poll(pollFd.data(), 1, 5000);
	if(pollFd[terminalFDPOLL].revents & POLLIN) {
        ioctl(STDOUT_FILENO,TIOCGWINSZ,&size);
        numcol = size.ws_col; //size of the terminal (column size)
        bytesRead = read(STDIN_FILENO, &RX, 4);
        if (bytesRead < 2)
        buffer.append((const char*)RX);
        //dprint("buffer len: %d\n", buffer.length());
        //dprint("buffer: %c\n", buffer[0]);
        if(bytesRead == 1) {
            //tprint("%c",buffer[0]);
            fflush(STDIN_FILENO);

            if(buffer[0] == 0x7F) { //delete char
                if(password.length() > 0) {
                    password.erase(password.length()-1);
                    passwordmask.erase(passwordmask.length()-1);
                    printf("\033[1D  "); //clears current and next char in terminal

                }
            }
			else if(buffer[0] == 0x1B)
			{
				tprint("User requested exit!\n");
				ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
				exit(0);
			}
            else if(buffer[0] != '\n'){
                password += buffer[0];
				passwordmask += '*';
			}
			else //(buffer[0] == '\n')
			{
				dprint("Exposing your password: %s\n", password.c_str());
				tprint("Logging in...\n");
				std::string str = userName.c_str();
				StringToPublicNED(str.c_str(), public_key_modulus, public_key, private_key);
				tprint("public key modulus: %lu public key: %lu private key: %lu\n", public_key_modulus, public_key, private_key);
				break;
			}

		}
        clearline();
		std::string connName;
		if(tcpConnMap.find(currentConnection) == tcpConnMap.end())
			connName = "";
		else
			connName = tcpConnMap.find(currentConnection)->second;

		printf("Enter password for %s>%s",userName.c_str(), passwordmask.c_str());

        //printf("\033[0C");
        fflush(STDIN_FILENO);
        buffer.clear();
		}
	}
}
