#include "clientheader.hpp"

int main(int argc, char** argv){
	int opt;
	string IP;
	int port = DEFAULT_PORT;
	uint64_t n_buffers = DEFAULT_BUFFERS;
	bool delete_existing = false;
	bool verbose_output = false;
	while( (opt = getopt(argc, argv, "a:p:b:Fvh")) != -1){
		char *end_ptr;
		switch(opt){
			case 'a':
				IP = optarg;
				break;
			case 'p':
				port = strtol(optarg, &end_ptr, 10);
				break;
			case 'b':
				n_buffers = strtol(optarg, &end_ptr, 10);
				break;
			case 'F':
				delete_existing = true;
				break;
			case 'v':
				verbose_output = true;
				break;
			case 'h':
				printUsage(argv[0]);
				/* NOTREACHED */;
			case '?':
				remindHelp();
				/* NOTREACHED */
		}
	}
	if(IP.empty()){
		remindUsage(argv[0]);
		/* NOTREACHED */
	}

	char* char_ip = &IP[0];
	struct sockaddr_in         servaddr;
	int                        server_info_socket;
	server_info_socket       = Socket(AF_INET,SOCK_STREAM,0);
	servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(char_ip);
    servaddr.sin_port        = htons(port);
	
	auto start               = chrono::steady_clock::now(); // Clock start

    Connect(server_info_socket, (const struct sockaddr*)&servaddr, sizeof(servaddr));    
    cout << "Connected to the server." << endl;
	setAtexit(server_info_socket);
	
	fileinfo_packet fileinfo_pack;
	FullRead(server_info_socket, &fileinfo_pack, sizeof(fileinfo_pack));

	checkFile(fileinfo_pack.fileName, delete_existing);
	cout << "Downloading: " << fileinfo_pack.fileName << endl;
	/* Open File descriptors and resize */
	FILE* writefd;
	uint64_t n_threads = fileinfo_pack.n_threads;
	writefd = fopen(fileinfo_pack.fileName, "wb");
	if(writefd == NULL){
		cerr << "Target file opening failed." << endl;
		EXIT(EXIT_FAILURE);
	}
	thread writeFileThread(writeFile, writefd, fileinfo_pack.n_packets, fileinfo_pack.last_packet_size, verbose_output, n_buffers);
	fs::path p = fs::current_path() / fileinfo_pack.fileName; 
	fs::resize_file(p, fileinfo_pack.fileSize);
	vector<int> readfd;
	while(readfd.size() < n_threads){
		int server_sock = Socket(AF_INET, SOCK_STREAM, 0);
		Connect(server_sock, (const struct sockaddr*)&servaddr, sizeof(servaddr));
		readfd.push_back(server_sock);
	}
	
	vector<thread> threads;
	for(short i = 0; i < n_threads; i++){
		threads.push_back(thread(receiveFile, readfd.at(i), fileinfo_pack.n_packets, fileinfo_pack.last_packet_size));
	}
	// Threads joining 
	for(thread &t : threads){
		if(t.thread::joinable())
			t.thread::join();
	}
	if(writeFileThread.thread::joinable()){
		writeFileThread.thread::join();
	}
	
	if(verbose_output){
		auto end = chrono::steady_clock::now(); // Clock end
		uint64_t seconds = chrono::duration_cast<chrono::seconds>(end - start).count();
		cout << endl << "Completed. Elapsed time in seconds: "
			<< seconds
			<< " s" << endl;
		cout << "Avg. speed: " << (fileinfo_pack.fileSize/seconds)/100000 << " Mbit/s"<<endl;
	}
	
	closefiles(readfd, n_threads);
	fclose(writefd);
	
	exit(EXIT_SUCCESS);
}