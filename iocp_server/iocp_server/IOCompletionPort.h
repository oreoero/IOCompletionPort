#pragma once
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>

#define	MAX_BUFFER		1024
#define SERVER_PORT		8000

struct stSOCKETINFO
{
	WSAOVERLAPPED	overlapped;
	WSABUF			dataBuf;
	SOCKET			socket;
	char			messageBuffer[MAX_BUFFER];
	int				recvBytes;
	int				sendBytes;
};


class IOCompletionPort
{
public:
	IOCompletionPort();
	~IOCompletionPort();

	// Socket registration and server information settings
	bool Initialize();
	// Start the server
	void StartServer();
	// Create a working thread
	bool CreateWorkerThread();
	// Working thread
	void WorkerThread();

private:
	stSOCKETINFO* m_pSocketInfo;		// About sockets
	SOCKET			m_listenSocket;		// Listening socket
	HANDLE			m_hIOCP;			// IOCP object handles
	bool			m_bAccept;			// Request action flag
	bool			m_bWorkerThread;	// Action thread action flag
	HANDLE* m_pWorkerHandle;	// Work thread handles
};
