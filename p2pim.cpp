#include <iostream>
#include <unistd.h>
#include <functional>
#include <cstring>
#include <unordered_map>
#include "p2pim.h"


std::unordered_map<std::string, int> commandswitch;
//std::string userName = std::string(getenv("USERNAME"));
int udpPort = 50550;
int tcpPort = 50551;
int initTimeout = 5;
int maxTimeout = 60;
// int destPort = ls
std::string optErr;


int main(int argc, char** argv) {
	
	dprint("hi: \n", 0);
	commandswitch["-u"] = 1;
	commandswitch["-up"] = 2;
	commandswitch["-tp"] = 3;
	commandswitch["-dt"] = 4;
	commandswitch["-dt"] = 5;
	commandswitch["-pp"] = 6;

	for(int i = 1; i < argc; i++){
		dprint("val: %s\n", argv[i]);
		switch(commandswitch[argv[i]])
		{
			case 1: 
			case 2: 
			case 3: 
			case 4: 
			case 5: 
			case 6: 
				commandswitch[argv[i]]==-1;
				if(i == argc - 1 || argv[i + 1][0] == '-')
					goto ERROR_HANDLING;
				break;
			default: 
				dprint("default\n",0);
			break;
		}
	}
	
	
		
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


ERROR_HANDLING:
    fprintf(stderr, "./p2pim: option requires an argument -- '%s'\n", optErr.c_str());
	exit(1);
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

