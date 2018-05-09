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
#include <sys/types.h>
#include <string.h> 
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <sys/time.h>
#include <cctype>
#include <signal.h>
#include <ifaddrs.h>
#include <vector>
#include <sys/ioctl.h>



#define DISCOVERY 1
#define REPLY 2
#define CLOSING 3
#define ESTABLISH_COMM 4
#define ACCEPT_COMM 5
#define USER_UNAVALIBLE 6
#define REQUEST_USER_LIST 7
#define REPLY_USER_LIST 8
#define DATA 9
#define DISCONTINUE_COMM 10

#define CONNECT 1
#define LIST 2
#define DISCONNECT 3
#define GETLIST 4
#define HELP 5


void ResetCanonicalMode(int fd, struct termios *savedattributes);
void SetNonCanonicalMode(int fd, struct termios *savedattributes);
void optionError(char** argv);
void die(const char* message);
int getType(uint8_t* message);
void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName);
void getPorts(uint8_t* message, int& udpPort, int& tcpPort);



void getHostName();
void checkIsNum(const char* str);
void checkPortRange(int portNum);
void SIGINT_handler(int signum);
void parseOptions(int argc, char** argv);
void initUDPMsg();
void addNewClient(uint8_t* replyMsg);
void setupSocket();
void checkUDPPort(int baseTimeout, int &currTimeout);
void checkTCPPort(std::string newClientName);
void checkConnections();
void sendUDPMessage(int type);
void checkSTDIN();
void sendDataMessage(std::string Message);