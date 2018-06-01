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

#ifdef DEBUG
    #define dprint(string, ...) {clearline(); printf(string, ##__VA_ARGS__);}
#else
    #define dprint(string, ...)
#endif


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
#define REQUEST_AUTH_KEY 0x10
#define REPLY_AUTH_KEY 0x11
#define ESTABLISH_ENCRYPTED_COMM 0x000B
#define ACCEPT_ENCRYPTED_COMM 0x000C
#define ENCRYPTED_DATA_CHUNK_MESSAGE 0x000D
#define ESTABLISH_COMM_E 0x5555
#define ACCEPT_COMM_E 0xAAAA
#define USER_UNAVALIBLE_E 0xFF00
#define REQUEST_USER_LIST_E 0x00FF
#define REPLY_USER_LIST_E 0x5A5A
#define DATA_E 0xA5A5
#define DISCONTINUE_COMM_E 0xF0F0
#define DUMMY_E 0x0F0F


#define terminalFDPOLL 0
#define udpFDPOLL 1
#define tcpFDPOLL 2

#define SENDER 1
#define RECEIVER 0

void ResetCanonicalMode(int fd, struct termios *savedattributes);
void SetNonCanonicalMode(int fd, struct termios *savedattributes);
void optionError(char** argv, const char* optErr);
void die(const char* message);
int getType(uint8_t* message);
void getHostNUserName(uint8_t* message, std::string& hostName, std::string& userName);
void getPorts(uint8_t* message, int& udpPort, int& tcpPort);

void login_prompt();
void getHostName();
void checkIsNum(const char* str);
void checkPortRange(int portNum);
void SIGINT_handler(int signum);
void parseOptions(int argc, char** argv);
void initUDPMsg();
void addNewClient(uint8_t* replyMsg);
void setupSocket();
void checkUDPPort(int &baseTimeout, int &currTimeout);
void checkTCPPort(std::string newClientName);
void checkTCPConnections();
void sendUDPMessage(int type);
void sendTCPMessage(int type, int fd);
void checkSTDIN();
void sendDataMessage(std::string Message);
void generateList();
void clearline();
uint64_t htonll(uint64_t val);
uint64_t ntohll(uint64_t lav);
uint64_t bitswap(uint64_t lav);
void writeEncryptedDataChunk(struct Client& clientInfo, uint8_t* raw_message, uint32_t messageLength);
uint16_t processEncryptedDataChunk(struct Client& clientInfo, uint8_t* encryptedDataChunk);
int getTarget(std::string &target);
void sendReqAuthMessage(std::string name);
void checkAuth();
uint64_t sessionKeyUpdate(struct Client& clientInfo, int SendorRecv);
