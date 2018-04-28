#include "p2pim.h"


#define MAX_REPLY_MSG_LEN (10 + 64 + 32 + 2)


std::unordered_map<std::string, int> commandswitch;

std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int initTimeout = 5;
int maxTimeout = 60;
// int destPort = ls
std::string optErr;


int udpSockFd, broadcastEnable;
struct sockaddr_in serverAddr, clientAddr;
socklen_t clientAddrLen; 
struct pollfd pollFd;



int main(int argc, char** argv) {
	int currTimeout = initTimeout * 1000;
    char tmp[1024];

    gethostname(tmp, 1024);
    hostName = tmp;

    commandswitch["-u"] = 1;
    commandswitch["-up"] = 2;
    commandswitch["-tp"] = 3;
    commandswitch["-dt"] = 4;
    commandswitch["-dt"] = 5;
    commandswitch["-pp"] = 6;

	for(int i = 1; i < argc; i++){
		dprint("val: %s\n", argv[i]);
        optErr = argv[i];

        if(commandswitch.find(argv[i]) != commandswitch.end()) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                    ERROR_HANDLING();
        }

		switch(commandswitch[argv[i]])
		{
			case 1: {
                commandswitch[argv[i]] = -1;
                userName = argv[i + 1];
                break;
            }
			case 2: {
                commandswitch[argv[i]] = -1;
                // TODO: Should check port number range
                udpPort = atoi(argv[i + 1]);
                break;
            }
			case 3: {
                commandswitch[argv[i]] = -1;
                // TODO: Should check port number range
                tcpPort = atoi(argv[i + 1]);
                break;
            }
			case 4: {
                commandswitch[argv[i]] = -1;
                // TODO: Should validate argument is a number
                initTimeout = atoi(argv[i + 1]);
                break;
            }
			case 5: {
                commandswitch[argv[i]] = -1;
                // TODO: Should validate argument is a number
                maxTimeout = atoi(argv[i + 1]);
                break;
            }
            // TODO:
			// case 6: {
   //              commandswitch[argv[i]] = -1;
   //              initTimeout = atoi(argv[i + 1]);
   //              break;
   //          }
			default: {
				dprint("default\n",0);
                ERROR_HANDLING();
            }
		}
	}

    dprint("Username = %s\n", userName.c_str());
    // TODO: Hostname = sp4.cs.ucdavis.edu
    dprint("Hostname = %s\n", hostName.c_str());
    dprint("UDP Port = %d\n", udpPort);
    dprint("TCP Port = %d\n", tcpPort);
    dprint("Mintimeout = %d\n", initTimeout);
    dprint("Maxtimeout = %d\n", maxTimeout);


    // Construct discovery message
    int discoveryMsgLen = 10 + hostName.length() + 1 + userName.length() + 1;
    uint8_t discoveryMsg[discoveryMsgLen], replyMsg[MAX_REPLY_MSG_LEN];
    bzero(discoveryMsg,discoveryMsgLen);

    memcpy((uint32_t*)discoveryMsg, "P2PI", 4);
    *((uint16_t*)discoveryMsg + 2) = htons(1);
    *((uint16_t*)discoveryMsg + 3) = htons(udpPort);
    *((uint16_t*)discoveryMsg + 4) = htons(tcpPort);
    memcpy(discoveryMsg + 10, hostName.c_str(), hostName.length());
    memcpy(discoveryMsg + 10 + hostName.length() + 1, userName.c_str(), 
        userName.length());

    for(int i = 0; i < discoveryMsgLen; i++)
    {
        dprint("%c", discoveryMsg[i]);
    }
    dprint("\n", 0);

    // Send discovery message
    udpSockFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(udpSockFd < 0) {
        die("Failed to open socket");
    }

    broadcastEnable = 1;
    if(setsockopt(udpSockFd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, 
        sizeof(broadcastEnable)) < 0) {
        close(udpSockFd);
        die("Failed to set socket option");
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    serverAddr.sin_port = htons(udpPort);

    // if(sendto(udpSockFd, discoveryMsg, discoveryMsgLen, 0, 
    //     (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
    //     die("Failed to send discovery message");
    // }


    // Wait for reply message
    pollFd.fd = udpSockFd;
    pollFd.events = POLLIN;

    int rc = poll(&pollFd, 1, currTimeout);

    // Timeout event
    if(0 == rc) {
        dprint("TIMEOUT\n", 0);

        // Double current timeout
        currTimeout *= 2; 
    }
    else if(rc > 0 && POLLIN == pollFd.revents) {
        dprint("RECV\n", 0);
        // Reply message
        clientAddrLen = sizeof(clientAddr);
        int recvLen = recvfrom(udpSockFd, replyMsg, MAX_REPLY_MSG_LEN, 0,
            (struct sockaddr*)&clientAddr, &clientAddrLen);

        if(recvLen > 0) {
            std::string newHostName, newUserName;
            getHostNUserName(replyMsg, newHostName, newUserName);
            dprint("%s %s\n", newHostName.c_str(), newUserName.c_str());
        }
    }
    else
        dprint("ERROR\n", 0);

	
	struct termios SavedTermAttributes;
    char RXChar;
    
   /*  SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    
    while(1){
        read(STDIN_FILENO, &RXChar, 1);
        if(0x04 == RXChar){ // C-d
            break;
        }
        else{
            if(isprint(RXChar)){
                printf("RX: '%c' 0x%02X\n",RXChar, RXChar);   
            }
            else{
                printf("RX: ' ' 0x%02X\n",RXChar);
            }
        }
    }
    
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes); */


	
    return 0;
}

void ERROR_HANDLING(){
    fprintf(stderr, "./p2pim: option requires an argument -- '%s'\n", optErr.c_str());
	exit(1);
}


void die(const char *message) {
    perror(message);
    exit(0);
}

int getType(uint8_t* message) {
    return ntohs(*((uint16_t*)message + 2));
}

void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName) {
    hostName = (char*)(message + 10);
    userName = (char*)(message + 10 + hostName.length() + 1);
}
