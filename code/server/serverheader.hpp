#include "../sharedheader.hpp"
#define DEFAULT_IP "0.0.0.0"
#define MAX_THREADS 1024
#define DEFAULT_THREADS 8

// File I/O
void readFile(FILE* readfd, uint64_t n_packets, uint64_t n_buffers);
void sendFile(int writefd, uint64_t last_packet_size, uint64_t n_packets);
ssize_t FullRead(void *buf, int size, size_t count, FILE* file);
// Server socket's functions
int Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int Listen(int sockfd, int backlog);
int Accept(int sockfd, sockaddr_in *addr);

uint64_t findSize(char *fileName); 
void receiveFeedback(int info_socket); // Receive feedback from client

// Synchronization vars
mutex delegatedMutex;
uint64_t delegatedPackets = 0; // N. of Packets which are delegated to a specific thread, which will send it

// Packets' queue synchronization's vars
mutex data_slots_mutex;
queue<full_packet> data_slots;
condition_variable data_slots_cv;
mutex free_slots_mutex;
queue<unsigned char*> free_slots;
condition_variable free_slots_cv;

// Checks if there is still a packet to take delegation of
bool checkDelegation(int fd, uint64_t n_packets){
	delegatedMutex.lock();
	if(delegatedPackets < n_packets){
		delegatedPackets++;
		delegatedMutex.unlock();
		return true;
	}
	close(fd);
	delegatedMutex.unlock();
	return false;
}

// Reads n_packets of pieces from a file and places it into a shared queue
void readFile(FILE* readfd, uint64_t n_packets, uint64_t n_buffers){
	uint64_t readPackets = 0;
	unsigned char* buffer;
	unsigned char* buffer_2;
	
	for(uint64_t i = 0; i < n_buffers; i++){
		free_slots.push( (unsigned char*) malloc(sizeof(unsigned char)*FULL_DATA_DIM) );
	}
	
	while(readPackets < n_packets){
		unique_lock<mutex> lock_fs(free_slots_mutex);
		free_slots_cv.wait(lock_fs, []{return !free_slots.empty();});
		buffer = free_slots.front();
		free_slots.pop();
		lock_fs.unlock();
		
		FullRead(buffer+INFO_PACKET_DIM, sizeof(unsigned char), BUFFER_SIZE, readfd);
		
		full_packet full_pack;
		full_pack.data = buffer;
		full_pack.infos.packet_n = readPackets;
		full_pack.infos.isLast = (readPackets == n_packets-1) ? 1 : 0;
		memcpy(buffer, &full_pack.infos, INFO_PACKET_DIM);

		unique_lock<mutex> lock_ds(data_slots_mutex);
		data_slots.push(full_pack);
		lock_ds.unlock();
		data_slots_cv.notify_one();
		
		buffer = buffer_2;
		readPackets++;
	}		
}

// Takes buffers from a synced queue and sends it to the client
void sendFile(int writefd, uint64_t last_packet_size, uint64_t n_packets){
	uint64_t buffer_size;
	info_packet info_pack;
	
	if(!checkDelegation(writefd, n_packets))
		return;
	
	while(1){
		unique_lock<mutex> lock_ds(data_slots_mutex);
        data_slots_cv.wait(lock_ds, []{return !data_slots.empty();});
		full_packet extracted_pack = data_slots.front();
		data_slots.pop();
		lock_ds.unlock();
		
		buffer_size = (extracted_pack.infos.isLast == 1) ? last_packet_size : BUFFER_SIZE;
		//FullWrite(writefd, &(extracted_pack.infos), INFO_PACKET_DIM); Rimosso perche' i dati sono in data, per il client
		FullWrite(writefd, extracted_pack.data, INFO_PACKET_DIM+buffer_size);
		
		unique_lock<mutex> lock_fs(free_slots_mutex);
		free_slots.push(extracted_pack.data);
		lock_fs.unlock();
		free_slots_cv.notify_one();
		
		if(!checkDelegation(writefd, n_packets))
			return;
	}
} 

// Places data from file into a buffer
ssize_t FullRead(void *buf, int size, size_t count, FILE* file){
    size_t nleft;
    ssize_t nread;
    nleft = count;
    while (nleft > 0){/* repeat until no left */
        if ( (nread = fread(buf, size, nleft, file)) < 0){
            if (errno == EINTR) /* if interrupted by system call */
                continue;
            else
                exit(EXIT_FAILURE);
        } 
        else if (nread == 0)
            break;
        nleft -= nread; /* set left to read */
        buf   += nread; /* set pointer */
    }
    return (count-nleft);
}

// Wrapper functions for connections
int Bind(int sockfd, const struct sockaddr *addr,socklen_t addrlen){
    if (bind(sockfd,addr,addrlen)<0){
        perror("bind error: ");
        return(-1);
    }
	return 0;
}

int Listen(int sockfd, int backlog){
    if (listen(sockfd,backlog)<0){
        perror("listen error: ");
        exit(EXIT_FAILURE);
    }
	return 0;
}

int Accept(int sockfd, sockaddr_in *addr){
    int clientfd;
	socklen_t addrlen = sizeof(*addr);
    if ((clientfd = accept(sockfd,(struct sockaddr*)addr,&addrlen))<0){
        perror("accept error: ");
        exit(EXIT_FAILURE);
    }
    return clientfd;
}

uint64_t findSize(char *fileName){
	struct stat statistics;
	if(lstat(fileName, &statistics) < 0 ){
		perror("lstat error: ");
		exit(EXIT_FAILURE);
	}
	uint64_t size = statistics.st_size;
    return size;
}

void printUsage(char* progName){
	cerr << "usage: " << progName << " -f fileName [-t n_threads] [-a address] [-p port] [-b n_buffers] [-h]" << endl << endl;
	cerr << " -f <filename> .......... File to share." << endl;
	cerr << " -t <n_threads> ......... Threads used (default is " << DEFAULT_THREADS << " )." << endl;
	cerr << " -a <address> ........... IP address where to listen for connections. By default: all." << endl;
	cerr << " -p <port> .............. Server port.(default is " << DEFAULT_PORT << " )." << endl;
	cerr << " -b <n_buffers>.......... Number of buffers to use." << endl;
	cerr << " -h ..................... This help." << endl;
	exit(EXIT_FAILURE);
}

void receiveFeedback(int info_socket){
	unsigned char ending_response;
	FullRead(info_socket, &ending_response, sizeof(ending_response));
	if(ending_response == 'Y' ){
		cout << "Transfer completed with success." << endl;
	}else if(ending_response == 'F'){
		cerr << "Transfer canceled on client side due to an error." << endl;
		exit(EXIT_FAILURE); // TODO: Shouldn't exit in a thread
	}
}
