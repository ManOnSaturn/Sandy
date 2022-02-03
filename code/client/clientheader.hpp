#include "../sharedheader.hpp"

extern int last_exit;
#define EXIT(x) (exit)(last_exit = (x))

void receiveFile(uint16_t readfd, uint64_t n_packets, uint64_t last_packet_size);
void writeFile(FILE* writefd, uint64_t n_packets, uint64_t last_packet_size, bool verboseOutput, uint64_t n_buffers);
ssize_t FullWrite(const void *buf, int size, size_t count, FILE* file);
int Connect(uint16_t sockfd, const struct sockaddr *addr,socklen_t addrlen);
void remindUsage(char* progName);
void printUsage(char* progName);
void checkFile(char* fileName, bool delete_existing);
void progressBar(float receivedPackets, float n_packets);

// For atexit
void setAtexit(int info_socket);
void atexit_handler_1();
int last_exit; 
int server_info_socket;

// Synchronization vars
mutex data_slots_mutex;
queue<full_packet> data_slots;
condition_variable data_slots_cv;
mutex free_slots_mutex;
queue<unsigned char*> free_slots;
condition_variable free_slots_cv;

void receiveFile(uint16_t readfd, uint64_t n_packets, uint64_t last_packet_size){
	uint64_t n_read;
	uint64_t buffer_size;
	full_packet full_pack;
	full_pack.data = (unsigned char*) malloc(sizeof(unsigned char)*BUFFER_SIZE);
	unsigned char* temp;
	while(1){
		n_read = FullRead(readfd, &(full_pack.infos), INFO_PACKET_DIM);
		if(n_read != INFO_PACKET_DIM){ // If socket is closed from server
			free(full_pack.data);
			return;
		}
		buffer_size = (full_pack.infos.isLast == 1) ? last_packet_size : BUFFER_SIZE;
	    n_read = FullRead(readfd, full_pack.data, buffer_size);
		unique_lock<mutex> lock_fs(free_slots_mutex);
		free_slots_cv.wait(lock_fs, []{return !free_slots.empty();});
		temp = free_slots.front();
		free_slots.pop();
		lock_fs.unlock();
		
		unique_lock<mutex> lock_ds(data_slots_mutex);
		data_slots.push(full_pack);
		lock_ds.unlock();
		data_slots_cv.notify_one();
		
		full_pack.data = temp;
	}
}

void writeFile(FILE* writefd, uint64_t n_packets, uint64_t last_packet_size, bool verboseOutput, uint64_t n_buffers){
	for(uint64_t i = 0; i < n_buffers; i++){
		free_slots.push( (unsigned char*) malloc(sizeof(unsigned char)*BUFFER_SIZE) );
	}
	uint64_t n_written;
	uint64_t buffer_size;
	uint64_t receivedPackets = 0;
	uint64_t startingPoint;
	uint16_t last_printed = 0;
	while(receivedPackets < n_packets){
		unique_lock<mutex> lock_ds(data_slots_mutex);
        data_slots_cv.wait(lock_ds, []{return !data_slots.empty();});
		full_packet extracted_pack = data_slots.front();
		data_slots.pop();
		lock_ds.unlock();
		
		startingPoint = (uint64_t)extracted_pack.infos.packet_n * (uint64_t)BUFFER_SIZE;

		fseeko(writefd, startingPoint, SEEK_SET);
		buffer_size = (extracted_pack.infos.isLast == 1) ? last_packet_size : BUFFER_SIZE;
		n_written = FullWrite(extracted_pack.data, sizeof(unsigned char), buffer_size, writefd);


		unique_lock<mutex> lock_fs(free_slots_mutex);
		free_slots.push(extracted_pack.data);
		lock_fs.unlock();
		free_slots_cv.notify_one();
		
		receivedPackets++;
		if(verboseOutput)
			progressBar(float(receivedPackets), float(n_packets));
	}
}

ssize_t FullWrite(const void *buf, int size, size_t count, FILE* file){
    size_t nleft;
    ssize_t nwritten;
    nleft = count;
    while (nleft > 0){
        if ((nwritten = fwrite(buf, size, nleft, file)) < 0){
            if (errno == EINTR)/* if interrupted by system call */
                continue;
            else
                exit(-1);          
        }
        nleft -= nwritten; /* set left to write */
        buf   += nwritten; /* set pointer */
    }
    return (count-nleft);
}

int Connect(uint16_t sockfd, const struct sockaddr *addr,socklen_t addrlen){
    if (connect(sockfd,addr,addrlen)<0){
        perror("connect error: ");
        exit(-1);
    }
	return 0;
}

void remindUsage(char* progName){
	cerr << "Server's IP must be set with flag -a. Example:" << endl;
	cerr << progName << " -a 34.54.144.200" << endl;
	EXIT(EXIT_FAILURE);
}

void printUsage(char* progName){
	cerr << "usage: " << progName << " -a address [-p port] [-b n_buffers] [-F] [-v] [-h]" << endl << endl;
	cerr << " -a <address> ........... Server IP address." << endl;
	cerr << " -p <port> .............. Server port." << endl;
	cerr << " -b <n_buffers>.......... Number of buffers to use." << endl;
	cerr << " -F ..................... Force file deletion if already exists." << endl;
	cerr << " -v ..................... Show progress." << endl;
	cerr << " -h ..................... This help." << endl;
	EXIT(EXIT_FAILURE);
}

void checkFile(char* fileName, bool delete_existing){
	if(fs::exists(fileName)){
		if(delete_existing){
			int status;
			status = remove(fileName);
			if(status!=0){
				cerr << "File deletion failed. Exiting." << endl;
				EXIT(EXIT_FAILURE);
			}
		}else{
			cerr << "Shared file already exists. Delete it first or use -F flag to overwrite it." << endl;
			EXIT(EXIT_FAILURE);
		}
	}
}

void setAtexit(int info_socket){
	server_info_socket = info_socket;
	atexit(atexit_handler_1);
}

void atexit_handler_1(){
	unsigned char ending_response = 'Y';
	if(last_exit == EXIT_FAILURE){
		ending_response = 'F';
	}
	FullWrite(server_info_socket, &ending_response, sizeof(ending_response));
}

void progressBar(float receivedPackets, float n_packets){
	float perc = receivedPackets/n_packets;
	int barWidth = 70;
	cout << "\r[";
	int pos = barWidth * perc;
	for (int i = 0; i < barWidth; ++i) {
		if (i < pos) cout << "=";
		else if (i == pos) cout << ">";
		else cout << " ";
	}
	cout << "] " << int(perc * 100.0) << " %";
	cout.flush();
}