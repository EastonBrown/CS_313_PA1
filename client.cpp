/*
	Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20
	
	Please include your Name, UIN, and the date below
	Name: Easton
	UIN: <Your UIN>
	Date: 9/28/2025
*/

#include "common.h"
#include "FIFORequestChannel.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

using namespace std;

void request_data_point(FIFORequestChannel& chan, int person, double time, int ecgno) {
	char buf[MAX_MESSAGE];
	datamsg x(person, time, ecgno);
	memcpy(buf, &x, sizeof(datamsg));
	chan.cwrite(buf, sizeof(datamsg));

	double reply;
	chan.cread(&reply, sizeof(double));
	cout << "For person " << person << ", at time " << time << ", the value of ecg " << ecgno << " is " << reply << endl;
}

void request_all_data(FIFORequestChannel& chan, int person) {
	ofstream fout("received/x1.csv");
	for (int ecg = 1; ecg <= 2; ecg++) {
		for (int i = 0; i < 1000; i++) {
			char buf[MAX_MESSAGE];
			datamsg x(person, i * 0.004, ecg);
			memcpy(buf, &x, sizeof(datamsg));
			chan.cwrite(buf, sizeof(datamsg));

			double reply;
			chan.cread(&reply, sizeof(double));
			fout << reply << "\n";
		}
	}
	fout.close();
	cout << "Collected first 1000 data points for person " << person << " into received/x1.csv\n";
}

void request_file(FIFORequestChannel& chan, const string& filename) {
	string out_dir = "received/";
	mkdir(out_dir.c_str(), 0777); // Ensure directory exists

	// Step 1 — Get file size
	filemsg fm(0, 0);
	int len = sizeof(filemsg) + filename.size() + 1;
	char* buf2 = new char[len];
	memcpy(buf2, &fm, sizeof(filemsg));
	strcpy(buf2 + sizeof(filemsg), filename.c_str());
	chan.cwrite(buf2, len);

	__int64_t file_size;
	chan.cread(&file_size, sizeof(__int64_t));
	delete[] buf2;

	// Step 2 — Transfer file in chunks
	ofstream fout(out_dir + filename, ios::binary);
	__int64_t offset = 0;
	int chunk_size = MAX_MESSAGE;

	while (offset < file_size) {
		int bytes_to_read = min(chunk_size, (int)(file_size - offset));
		filemsg fm(offset, bytes_to_read);

		int len2 = sizeof(filemsg) + filename.size() + 1;
		char* buf3 = new char[len2];
		memcpy(buf3, &fm, sizeof(filemsg));
		strcpy(buf3 + sizeof(filemsg), filename.c_str());
		chan.cwrite(buf3, len2);

		char* file_buf = new char[bytes_to_read];
		chan.cread(file_buf, bytes_to_read);

		fout.write(file_buf, bytes_to_read);

		delete[] buf3;
		delete[] file_buf;

		offset += bytes_to_read;
	}
	fout.close();
	cout << "Transferred file " << filename << " into received/" << filename << endl;
}

int main (int argc, char *argv[]) {
	int opt;
	int p = 1;
	double t = 0.0;
	int e = 1;
	string filename = "";
	bool new_channel_flag = false;
	int buffer_capacity = MAX_MESSAGE;

	while ((opt = getopt(argc, argv, "p:t:e:f:m:c")) != -1) {
		switch (opt) {
			case 'p':
				p = atoi(optarg);
				break;
			case 't':
				t = atof(optarg);
				break;
			case 'e':
				e = atoi(optarg);
				break;
			case 'f':
				filename = optarg;
				break;
			case 'm':
				buffer_capacity = atoi(optarg);
				break;
			case 'c':
				new_channel_flag = true;
				break;
			default:
				cerr << "Invalid option" << endl;
				exit(1);
		}
	}

	// ===== Fork server =====
	pid_t pid = fork();
	if (pid < 0) {
		cerr << "Fork failed" << endl;
		exit(1);
	}
	if (pid == 0) {
		char m_arg[10];
		snprintf(m_arg, sizeof(m_arg), "%d", buffer_capacity);
		char* args[] = {(char*) "./server", (char*) "-m", m_arg, NULL};
		execvp(args[0], args);
		cerr << "Exec failed" << endl;
		exit(1);
	}
	sleep(1);

	FIFORequestChannel chan("control", FIFORequestChannel::CLIENT_SIDE);

	// ===== New channel request if -c used =====
	if (new_channel_flag) {
		MESSAGE_TYPE m = NEWCHANNEL_MSG;
		chan.cwrite(&m, sizeof(MESSAGE_TYPE));

		char new_channel_name[256];
		chan.cread(new_channel_name, sizeof(new_channel_name));

		FIFORequestChannel new_chan(new_channel_name, FIFORequestChannel::CLIENT_SIDE);
		cout << "Using new channel: " << new_channel_name << endl;

		if (!filename.empty()) {
			request_file(new_chan, filename);
		} else if (p > 0 && t > 0 && e > 0) {
			request_data_point(new_chan, p, t, e);
		} else if (p > 0) {
			request_all_data(new_chan, p);
		}

		MESSAGE_TYPE quit = QUIT_MSG;
		new_chan.cwrite(&quit, sizeof(MESSAGE_TYPE));
	} else {
		if (!filename.empty()) {
			request_file(chan, filename);
		} else if (p > 0 && t > 0 && e > 0) {
			request_data_point(chan, p, t, e);
		} else if (p > 0) {
			request_all_data(chan, p);
		}
	}

	// ===== Close control channel =====
	MESSAGE_TYPE m = QUIT_MSG;
	chan.cwrite(&m, sizeof(MESSAGE_TYPE));

	wait(NULL);
	cout << "Client-side is done and exited" << endl;
	return 0;
}
