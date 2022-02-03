#define _FILE_OFFSET_BITS 64 // For fseek
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <getopt.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <filesystem>
#include <iostream>
#include <vector>

#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>

#define DEFAULT_BUFFERS 12
#define DEFAULT_PORT 34951
#define BUFFER_SIZE 2097152 // 2^21

#define INFO_PACKET_DIM sizeof(info_packet)
#define FULL_DATA_DIM (INFO_PACKET_DIM + BUFFER_SIZE)
#define	FULL_PACKET_DIM (INFO_PACKET_DIM + FULL_DATA_DIM)

using namespace std;
namespace fs = filesystem;

typedef struct{
	uint64_t n_threads; // Desired n_threads to be used for transmission
	char fileName[256];
	uint64_t fileSize;
	uint64_t n_packets;
	uint64_t last_packet_size;
}fileinfo_packet;

typedef struct{
	uint64_t packet_n;
	uint64_t isLast; // 0 == false
}info_packet;

typedef struct{
	info_packet infos;
	unsigned char* data;
}full_packet;

ssize_t FullWrite(int fd, const void *buf, size_t count);
ssize_t FullRead(int fd, void *buf, size_t count);
int Socket(int domain,int type,int protocol);
void closefiles(vector<int>, int);
void remindHelp();


ssize_t FullWrite(int fd, const void *buf, size_t count){
    size_t nleft;
    ssize_t nwritten;
    nleft = count;
    while (nleft > 0){
        if ((nwritten = write(fd, buf, nleft)) < 0){
            if (errno == EINTR) // if interrupted by system call 
                continue;
            else
                exit(EXIT_FAILURE);          
        }
        nleft -= nwritten; // set left to write 
        buf   += nwritten; // set pointer 
    }
    return (count-nleft);
}

ssize_t FullRead(int fd, void *buf, size_t count){
    size_t nleft;
    ssize_t nread;
    nleft = count;
    while (nleft > 0){ //repeat until no left 
        if ( (nread = read(fd, buf, nleft)) < 0){
            if (errno == EINTR){ // if interrupted by system call 
                continue;
            }else{
                exit(-1);
			}
        } 
        else if (nread == 0){
			break;
		}
        nleft -= nread; // set left to read 
        buf   += nread; //set pointer 
    }
    return (count-nleft);
}

int Socket(int domain, int type, int protocol){
    int sockfd;
    if (( sockfd = socket(domain,type,protocol) ) < 0){
        perror("socket error: ");
        exit(-1);
    }
    return sockfd;
}

// Closes {fds} file descriptors inside a vector
void closefiles(vector<int> files, int fds){
    for(int i=0; i < fds; i++)
    	close(files.at(i));
}

void remindHelp(){
	cout << "Use option \"-h\" for help." << endl;
	exit(EXIT_FAILURE);
}
