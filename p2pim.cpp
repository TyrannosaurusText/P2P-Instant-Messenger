
#include "p2pim.h"


#define MAX_REPLY_MSG_LEN (10 + 64 + 32 + 2)


std::unordered_map<std::string, int> commandswitch;

std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int initTimeout = 5000;
int maxTimeout = 60000;
// int destPort = ls
std::string optErr;


int udpSockFd, broadcastEnable;
struct sockaddr_in serverAddr, clientAddr;
socklen_t clientAddrLen; 
struct pollfd pollFd;



int main(int argc, char** argv) {
    hostName = std::string(getHostName());

    commandswitch["-u"] = 1;
    commandswitch["-up"] = 2;
    commandswitch["-tp"] = 3;
    commandswitch["-dt"] = 4;
    commandswitch["-dt"] = 5;
    commandswitch["-pp"] = 6;

	for(int i = 1; i < argc; i++){
		// dprint("val: %s\n", argv[i]);
        optErr = argv[i];

        if(commandswitch.find(argv[i]) != commandswitch.end()) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                    ERROR_HANDLING(argv);
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
                ERROR_HANDLING(argv);
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

    int recvReply = 0;

    // Send discovery message
    udpSockFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(udpSockFd < 0) {
        die("Failed to open socket");
    }

    broadcastEnable = 1;
    if(setsockopt(udpSockFd, SOL_SOCKET, SO_REUSEPORT & SO_BROADCAST, &broadcastEnable, 
        sizeof(broadcastEnable)) < 0) {
        close(udpSockFd);
        die("Failed to set socket option");
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    serverAddr.sin_port = htons(udpPort);

    if(-1 == bind(udpSockFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)))
        die("Failed to bind socket");
    



    pollFd.fd = udpSockFd;
    pollFd.events = POLLIN;
    int baseTimeout = initTimeout;
    timeval start,end;
    int timePassed;


    int currTimeout = 0;

    while(!recvReply && currTimeout <= maxTimeout * 1000) {
        
        if(currTimeout <= 0) {
            if(sendto(udpSockFd, discoveryMsg, discoveryMsgLen, 0, 
                (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                die("Failed to send discovery message");

            }
            currTimeout=baseTimeout;
            dprint("YO\n", 0);
        }


        // Wait for reply message
    
        gettimeofday(&start, NULL);
        int rc = poll(&pollFd, 1, currTimeout);
        gettimeofday(&end, NULL);
        timePassed = ((end.tv_sec-start.tv_sec)*1000000 + end.tv_usec-start.tv_usec)/1000;
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

    dprint("Bye...\n", 0);

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



void ERROR_HANDLING(char** argv){
    fprintf(stderr, "%s: option requires an argument -- '%s'\n", argv[0], optErr.c_str());
	exit(1);
}
std::string getHostName()
{
	char Buffer[256];

	
	if(-1 == gethostname(Buffer, 255)){
        printf("Unable to resolve host name.");
		exit(-1);
    }

    struct hostent *LocalHostEntry = gethostbyname(Buffer);
	strcpy(Buffer, LocalHostEntry->h_name);
	std::string temp(Buffer);
	return temp;
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