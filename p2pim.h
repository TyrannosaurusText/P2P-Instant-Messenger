#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <iostream>
#include <functional>
#include <cstring>
#include <unordered_map>
#include <arpa/inet.h>
#include <poll.h>



#define DEBUG 1

#ifdef DEBUG
    #define dprint(string, ...) printf(string,__VA_ARGS__)
#else
    #define dprint(string, ...) 
#endif


void ResetCanonicalMode(int fd, struct termios *savedattributes);
void SetNonCanonicalMode(int fd, struct termios *savedattributes);
void ERROR_HANDLING(char** argv);
void die(const char* message);
int getType(uint8_t* message);
void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName);