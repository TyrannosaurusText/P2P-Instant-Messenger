
#include "p2pim.h"

std::unordered_map<std::string, int> commandswitch;
std::string userName = getenv("USER");
std::string hostName;
int udpPort = 50550;
int tcpPort = 50551;
int initTimeout = 5;
int maxTimeout = 60;
// int destPort = ls
std::string optErr;


int main(int argc, char** argv) {
	
	hostName = std::string(getHostName());
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
    dprint("Hostname = %s\n", hostName.c_str());
    dprint("UDP Port = %d\n", udpPort);
    dprint("TCP Port = %d\n", tcpPort);
    dprint("Mintimeout = %d\n", initTimeout);
    dprint("Maxtimeout = %d\n", maxTimeout);


    // Construct discovery message
    int discoveryMsgLen = 4 + 2 + 2 + 2 + hostName.length() + 1 + userName.length() + 1;
    uint8_t discoveryMsg[discoveryMsgLen];
    bzero(discoveryMsg,discoveryMsgLen);

    memcpy((uint32_t*)discoveryMsg, "P2PI", 4);
    *((uint16_t*)discoveryMsg + 2) = htons(1);
    *((uint16_t*)discoveryMsg + 3) = htons(udpPort);
    *((uint16_t*)discoveryMsg + 4) = htons(tcpPort);
    memcpy((uint16_t*)discoveryMsg + 5, hostName.c_str(), hostName.length());
    memcpy((uint16_t*)discoveryMsg + 5 + hostName.length() + 1, userName.c_str(), userName.length());

    
    // Send discovery message

	
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

void dump(std::string msg)
{
	for(int i = 0; i < msg.length(); i++)
    {
        dprint("%d %c \n", msg[i], msg[i]);
    }
    dprint("\n", 0);
}