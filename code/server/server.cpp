#include "serverheader.hpp"

int main(int argc, char** argv){
	int opt;
	char *fileName = NULL;
	int n_threads = DEFAULT_THREADS;
	string IP = DEFAULT_IP;
	int port = DEFAULT_PORT;
	uint64_t n_buffers = DEFAULT_BUFFERS;

	while( (opt = getopt(argc, argv, "f:t:a:p:b:h")) != -1){
		char *end_ptr;
		switch(opt){
			case 'f':
				fileName = optarg;
				break;
			case 't':
				n_threads = strtol(optarg, &end_ptr, 10);
				break;
			case 'a':
				IP = optarg;
				break;
			case 'p':
				port = strtol(optarg, &end_ptr, 10);
				break;
			case 'b':
				port = strtol(optarg, &end_ptr, 10);
				break;
			case 'h':
				printUsage(argv[0]);
				/* NOTREACHED */;	
			case '?':
				remindHelp();
				/* NOTREACHED */
		}
	}
	
	/* Check input validity */
	if(fileName == NULL){
		cerr << "File name not set. Exiting." << endl;
		remindHelp();
		exit(EXIT_FAILURE);
	}
    if(access(fileName, F_OK)!=0){
        cerr << "File named " << fileName << " does not exist. Exiting" << endl;
		exit(EXIT_FAILURE);
    }
	if(n_threads > MAX_THREADS){
		cout << "Exceed max threads allowed. Setting number of threads to " << MAX_THREADS << "." << endl;
		n_threads = MAX_THREADS;
	}
	
	/* Open connection */
	unsigned int listfd;
	struct sockaddr_in addr, connected;
	listfd = Socket(AF_INET, SOCK_STREAM, 0);
	int var = 1;
    if (setsockopt(listfd, SOL_SOCKET, SO_REUSEADDR, &var, sizeof(int)) < 0){
        perror("Setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
	char* char_ip = &IP[0];

	addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(char_ip);
    addr.sin_port        = htons(port);
	if(Bind(listfd, (struct sockaddr *) &addr, sizeof(addr)) == -1){
		exit(EXIT_FAILURE);
	}
	/* Waiting for connection */
	Listen(listfd, 1);
	cout << "Server listening on port " << port << endl;
	unsigned int info_socket = Accept(listfd, &connected); 
	cout << "Connection accepted." << endl;
	// Open file descriptor
	FILE* readfd = fopen(fileName, "rb");
	
	// Info pack to inform the client of file details and transmission instructions
	fileinfo_packet fileinfo_pack;
	// Size
	uint64_t fileSize;
	fileSize = findSize(fileName);
	fileinfo_pack.fileSize = fileSize;
	//File name
	sprintf(fileinfo_pack.fileName, "received_%s", fileName);
	// N_threads
	fileinfo_pack.n_threads = n_threads;
	// N_packets and sizes
	fileinfo_pack.n_packets = (fileSize/BUFFER_SIZE) + 1;
	fileinfo_pack.last_packet_size = fileSize%BUFFER_SIZE;
	
	/* Start file reading */
	thread readFileThread(readFile, readfd, fileinfo_pack.n_packets, n_buffers);
	// Send info packet 
	FullWrite(info_socket, &fileinfo_pack, sizeof(fileinfo_pack));
	thread feedbackThread(receiveFeedback, info_socket);
	// Open n_threads connections with client 
	vector<int> writefd;
	while(writefd.size() < n_threads){
		int socket = Accept(listfd, &connected);
		writefd.push_back(socket);
	}
	vector<thread> threads;
	for(uint64_t i = 0; i < n_threads; i++){
		threads.push_back(thread(sendFile, writefd.at(i), fileinfo_pack.last_packet_size, fileinfo_pack.n_packets));
	}
	feedbackThread.join();

	/* Ending */
	fclose(readfd);
	exit(EXIT_SUCCESS);
}
