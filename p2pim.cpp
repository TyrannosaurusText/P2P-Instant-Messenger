#include "p2pim.h"


#define MAX_REPLY_MSG_LEN (10 + 64 + 32 + 2)
#define udpFDPOLL 0
#define terminalFDPOLL 1

enum Option {USERNAME, UDP_PORT, TCP_PORT, MIN_TIMEOUT, MAX_TIMEOUT, HOST};

static std::unordered_map<std::string, int> optionMap {
    {"-u", USERNAME}, {"-up", UDP_PORT}, {"-tp", TCP_PORT}, 
    {"-dt", MIN_TIMEOUT}, {"dm", MAX_TIMEOUT}, {"pp", HOST}
};

std::unordered_map<std::string, val> TCPconnections;
std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int minTimeout = 5000;
int maxTimeout = 60000;
// int destPort = ls
std::string optErr;


int udpSockFd, enable = 1;
struct sockaddr_in serverAddr, clientAddr;
socklen_t clientAddrLen; 
struct pollfd* polls;
struct pollfd pollFd;
struct pollfd terminalPollfd;



int main(int argc, char** argv) {
    // Get default hostname
	polls = new pollfd[65537]; // 65535 ports + stdin
    hostName = getHostName();

    // Iterate through all options
    for(int i = 1; i < argc; i++){
        // dprint("val: %s\n", argv[i]);
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
                    // TODO: Should check port number range
                    checkIsNum(argv[i + 1]);
                    udpPort = atoi(argv[i + 1]);
                    checkPortRange(udpPort);
                    break;
                }
                case TCP_PORT: {
                    optionMap[argv[i]] = -1;
                    // TODO: Should check port number range
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
                    // TODO: Should validate argument is a number
                    checkIsNum(argv[i + 1]);
                    minTimeout = atoi(argv[i + 1]);
                    break;
                }
                case MAX_TIMEOUT: {
                    optionMap[argv[i]] = -1;
                    // TODO: Should validate argument is a number
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
                    dprint("default\n",0);
                    optionError(argv);
                }
            }
        }
    }

    dprint("Username = %s\n", userName.c_str());
    // TODO: Hostname = sp4.cs.ucdavis.edu
    dprint("Hostname = %s\n", hostName.c_str());
    dprint("UDP Port = %d\n", udpPort);
    dprint("TCP Port = %d\n", tcpPort);
    dprint("Mintimeout = %d\n", minTimeout);
    dprint("Maxtimeout = %d\n", maxTimeout);


    // Construct discovery message
    int discoveryMsgLen = 10 + hostName.length() + 1 + userName.length() + 1;
    uint8_t discoveryMsg[discoveryMsgLen], replyMsg[MAX_REPLY_MSG_LEN];
    memset(discoveryMsg, 0, discoveryMsgLen);

    memcpy((uint32_t*)discoveryMsg, "P2PI", 4);
    *((uint16_t*)discoveryMsg + 2) = htons(1);
    *((uint16_t*)discoveryMsg + 3) = htons(udpPort);
    *((uint16_t*)discoveryMsg + 4) = htons(tcpPort);
    memcpy(discoveryMsg + 10, hostName.c_str(), hostName.length());
    memcpy(discoveryMsg + 10 + hostName.length() + 1, userName.c_str(), 
        userName.length());

    // for(int i = 0; i < discoveryMsgLen; i++)
    // {
    //     dprint("%c", discoveryMsg[i]);
    // }
    // dprint("\n", 0);

    int recvReply = 0;

    // Send discovery message
    udpSockFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(udpSockFd < 0)
        die("Failed to open socket");

    // Enable broadcast capability
    enable = 1;
    if(setsockopt(udpSockFd, SOL_SOCKET, SO_BROADCAST, &enable, 
        sizeof(enable)) < 0) {
        close(udpSockFd);
        die("Failed to set socket option broadcast");
    }

    if(setsockopt(udpSockFd, SOL_SOCKET, SO_REUSEPORT, &enable, 
        sizeof(enable)) < 0) {
        close(udpSockFd);
        die("Failed to set socket option reuse address");
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    serverAddr.sin_port = htons(udpPort);

    if(-1 == bind(udpSockFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)))
        die("Failed to bind socket");
    

    pollFd.fd = udpSockFd;
    pollFd.events = POLLIN;
    int baseTimeout = minTimeout;
    timeval start,end;
    int timePassed;
    int currTimeout = 0;


/*     while(!recvReply && currTimeout <= maxTimeout * 1000) {
        
        if(currTimeout <= 0) {
            if(sendto(udpSockFd, discoveryMsg, discoveryMsgLen, 0, 
                (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                die("Failed to send discovery message");

            }
            currTimeout=baseTimeout;
            // dprint("YO\n", 0);
        }


        // Wait for reply message
    
        gettimeofday(&start, NULL);
        int rc = poll(&pollFd, 1, currTimeout);
        gettimeofday(&end, NULL);
        timePassed = ((end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec) / 1000;
        currTimeout -= timePassed;

        // Timeout event
        if(0 == rc) {
            dprint("TIMEOUT\n", 0);

            // Double current timeout
            baseTimeout *= 2;
            dprint("%d\n", currTimeout);

        }
        else if(rc > 0 && POLLIN == pollFd.revents) {
            std::string newHostName, newUserName;

            dprint("RECV\n", 0);
            // Reply message
            clientAddrLen = sizeof(clientAddr);
            int recvLen = recvfrom(udpSockFd, replyMsg, MAX_REPLY_MSG_LEN, 0,
                (struct sockaddr*)&clientAddr, &clientAddrLen);

            if(recvLen > 0) {
                getHostNUserName(replyMsg, newHostName, newUserName);
                dprint("%s %s\n", newHostName.c_str(), newUserName.c_str());
            }

            if(newHostName == hostName)
                dprint("Self discovery\n", 0);
            else
                recvReply = 1;
        }
        else
            dprint("ERROR\n", 0);
    }


    uint8_t closingMsg[sizeof(discoveryMsg)];
    memcpy(closingMsg, discoveryMsg, sizeof(discoveryMsg));

    *((uint16_t*)closingMsg + 2) = htons(3);  

    if(sendto(udpSockFd, closingMsg, discoveryMsgLen, 0, 
        (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        die("Failed to send discovery message");
    }

    dprint("Bye...\n", 0); */

    struct termios SavedTermAttributes;
    char* RX[4];
    std::string buffer = "";
    terminalPollfd.fd = STDIN_FILENO;
    terminalPollfd.events = POLLIN;
	polls[udpFDPOLL].fd = udpSockFd;
	polls[udpFDPOLL].fd = POLLIN;
	polls[terminalFDPOLL].fd = STDIN_FILENO;
	polls[terminalFDPOLL].events = POLLIN;
	std::string message = "";
	SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    int bytesRead, retpoll;
	printf(">");
	fflush(STDIN_FILENO);
    while(1){
		retpoll = poll(polls, 4, 1000); // to edit
		//dprint("%d\n",retpoll);
		if(retpoll == 0) {
			//dprint("%d", buffer.length());
			
		}
		if(polls[terminalFDPOLL].events & POLLIN){
			int bytesRead = read(STDIN_FILENO, &RX, 4);
			if (bytesRead < 2)
			buffer.append((const char*)RX);
			//dprint("buffer len: %d\n", buffer.length());
			
			//dprint("buffer: %c\n", buffer[0]);
			if(bytesRead == 1){
				//printf("%c",buffer[0]);
				fflush(STDIN_FILENO);
				
				if(buffer[0] == '\n' ){ //send message
					printf("\r%s>%s\n",userName.c_str(), message.c_str());
					//TODO: Actually proccess message
					message.clear();
				}
				else
				if(buffer[0] == 0x7F ){ //delete char
					if(message.length() > 0){
						message.erase(message.length()-1);
					}
				}
				else if(message.length() < 192){
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
    exit(1);
}



int getType(uint8_t* message) {
    return ntohs(*((uint16_t*)message + 2));
}



void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName) {
    hostName = (char*)(message + 10);
    userName = (char*)(message + 10 + hostName.length() + 1);
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
void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
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



void checkPortRange(int portNum) {
    if(1 > portNum || 65535 < portNum) {
        fprintf(stderr, "Invalid port \"%d\", must be in range [1 65,535]\n", portNum);
        exit(1);
    }
}