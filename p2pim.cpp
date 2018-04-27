#include <iostream>
#include <unistd.h>
#include <functional>
#include <cstring>
#include <unordered_map>

#define DEBUG 1
#ifdef DEBUG
#define dprint(string, ...) printf(string,__VA_ARGS__)
#else
#define dprint(string, ...) 
#endif
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
/*     for(int i = 1; i < argc; i++) {
        optErr = argv[i];
        if(strcmp("-u", argv[i]) == 0) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                goto ERROR_HANDLING;

            userName = argv[i];
        }
        else if(strcmp("-up", argv[i]) == 0) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                goto ERROR_HANDLING;
        }
        else if(strcmp("-tp", argv[i]) == 0) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                goto ERROR_HANDLING;
        }
        else if(strcmp("-dt", argv[i]) == 0) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                goto ERROR_HANDLING;
        }
        else if(strcmp("-dm", argv[i]) == 0) {
            if(i == argc - 1 || argv[i + 1][0] == '-')
                goto ERROR_HANDLING;
        }
        // else if(strcmp("-pp", argv[i]) == 0) {
            
        // }
    } */
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
	
    return 0;


ERROR_HANDLING:
    fprintf(stderr, "./p2pim: option requires an argument -- '%s'\n", optErr.c_str());
	exit(1);
}

