#include <iostream>
#include <unistd.h>
#include <functional>
#include <cstring>



std::string userName = std::string(getenv("USERNAME"));
int udpPort = 50550;
int tcpPort = 50551;
int initTimeout = 5;
int maxTimeout = 60;
// int destPort = ls
std::string optErr;


int main(int argc, char** argv) {
    for(int i = 1; i < argc; i++) {
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
    }

    return 0;


ERROR_HANDLING:
    fprintf(stderr, "./p2pim: option requires an argument -- '%s'\n", optErr.c_str());
}

