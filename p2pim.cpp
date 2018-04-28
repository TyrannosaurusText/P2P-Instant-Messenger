#include <iostream>
#include <unistd.h>
#include <functional>
#include <cstring>
#include <unordered_map>
#include <arpa/inet.h>

#define DEBUG 1
#ifdef DEBUG
#define dprint(string, ...) printf(string,__VA_ARGS__)
#else
#define dprint(string, ...) 
#endif

void ERROR_HANDLING();



std::unordered_map<std::string, int> commandswitch;
std::string userName = getenv("USER");
char hostName[1024];
int udpPort = 50550;
int tcpPort = 50551;
int initTimeout = 5;
int maxTimeout = 60;
// int destPort = ls
std::string optErr;




int main(int argc, char** argv) {
	
    gethostname(hostName, 1024);
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
                udpPort = atoi(argv[i + 1]);
                break;
            }
			case 3: {
                commandswitch[argv[i]] = -1;
                tcpPort = atoi(argv[i + 1]);
                break;
            }
			case 4: {
                commandswitch[argv[i]] = -1;
                initTimeout = atoi(argv[i + 1]);
                break;
            }
			case 5: {
                commandswitch[argv[i]] = -1;
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
    dprint("Hostname = %s\n", hostName);
    dprint("UDP Port = %d\n", udpPort);
    dprint("TCP Port = %d\n", tcpPort);
    dprint("Mintimeout = %d\n", initTimeout);
    dprint("Maxtimeout = %d\n", maxTimeout);


    // Construct discovery message
    int discoveryMsgLen = 4 + 2 + 2 + 2 + strlen(hostName) + 1 + userName.length() + 1;
    uint8_t discoveryMsg[discoveryMsgLen];
    bzero(discoveryMsg,discoveryMsgLen);

    memcpy((uint32_t*)discoveryMsg, "P2PI", 4);
    *((uint16_t*)discoveryMsg + 2) = htons(1);
    *((uint16_t*)discoveryMsg + 3) = htons(udpPort);
    *((uint16_t*)discoveryMsg + 4) = htons(tcpPort);
    memcpy((uint16_t*)discoveryMsg + 5, hostName, strlen(hostName));
    memcpy((uint16_t*)discoveryMsg + 5 + strlen(hostName) + 1, userName.c_str(), userName.length());

    for(int i = 0; i < discoveryMsgLen; i++)
    {
        dprint("%c", discoveryMsg[i]);
    }
    dprint("\n", 0);
    // Send discovery message

	
    return 0;
}


void ERROR_HANDLING(){
    fprintf(stderr, "./p2pim: option requires an argument -- '%s'\n", optErr.c_str());
	exit(1);
}

