#include "stdafx.h"
#include "IOCompletionPort.h"
#include <process.h>

unsigned int WINAPI CallWorkerThread(LPVOID p)
{
	IOCompletionPort* pOverlappedEvent = (IOCompletionPort*)p;
	pOverlappedEvent->WorkerThread();
	return 0;
}

IOCompletionPort::IOCompletionPort()
{
	m_bWorkerThread = true;
	m_bAccept = true;
}


IOCompletionPort::~IOCompletionPort()
{
	// winsock end 
	WSACleanup();
	// Delete used objects
	if (m_pSocketInfo)
	{
		delete[] m_pSocketInfo;
		m_pSocketInfo = NULL;
	}

	if (m_pWorkerHandle)
	{
		delete[] m_pWorkerHandle;
		m_pWorkerHandle = NULL;
	}
}

bool IOCompletionPort::Initialize()
{
	WSADATA wsaData;
	int nResult;
	// winsock 2.2
	nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (nResult != 0) 
	{
		printf_s("[ERROR] winsock Initialization failed\n");
		return false;
	}

	// Create a socket
	m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (m_listenSocket == INVALID_SOCKET)
	{
		printf_s("[ERROR] Socket creation failed\n");
		return false;
	}

	// Set up server information
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// Socket settings
	nResult = bind(m_listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	if (nResult == SOCKET_ERROR)
	{
		printf_s("[ERROR] bind failure\n");
		closesocket(m_listenSocket);
		WSACleanup();
		return false;
	}

	// Create an incoming queue
	nResult = listen(m_listenSocket, 5);
	if (nResult == SOCKET_ERROR)
	{
		printf_s("[ERROR] listen failure\n");
		closesocket(m_listenSocket);
		WSACleanup();
		return false;
	}

	return true;
}

void IOCompletionPort::StartServer()
{
	int nResult;
	// Client information
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	SOCKET clientSocket;
	DWORD recvBytes;
	DWORD flags;

	// Completion Port creating
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	// Worker Thread creating
	if (!CreateWorkerThread()) return;

	printf_s("[INFO]starting server..\n");

	// Receiving client access
	while (m_bAccept)
	{		
		clientSocket = WSAAccept(
			m_listenSocket, (struct sockaddr *)&clientAddr, &addrLen, NULL, NULL
		);

		if (clientSocket == INVALID_SOCKET)
		{
			printf_s("[ERROR] Accept failure\n");
			return;
		}

		m_pSocketInfo = new stSOCKETINFO();
		m_pSocketInfo->socket = clientSocket;
		m_pSocketInfo->recvBytes = 0;
		m_pSocketInfo->sendBytes = 0;
		m_pSocketInfo->dataBuf.len = MAX_BUFFER;
		m_pSocketInfo->dataBuf.buf = m_pSocketInfo->messageBuffer;
		flags = 0;

		m_hIOCP = CreateIoCompletionPort(
			(HANDLE)clientSocket, m_hIOCP, (DWORD)m_pSocketInfo, 0
		);

		// Specify a nested socket and hand over a function to be executed upon completion.
		nResult = WSARecv(
			m_pSocketInfo->socket,
			&m_pSocketInfo->dataBuf,
			1,
			&recvBytes,
			&flags,
			&(m_pSocketInfo->overlapped),
			NULL
		);

		if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
		{
			printf_s("[ERROR] IO Pending failure: %d", WSAGetLastError());
			return;
		}
	}

}

bool IOCompletionPort::CreateWorkerThread()
{
	unsigned int threadId;
	// Getting system information
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	printf_s("[INFO] CPU amount : %d\n", sysInfo.dwNumberOfProcessors);
	// The number of appropriate work threads (CPU * 2) + 1
	int nThreadCnt = sysInfo.dwNumberOfProcessors * 2;
	
	m_pWorkerHandle = new HANDLE[nThreadCnt];
	// thread creating
	for (int i = 0; i < nThreadCnt; i++)
	{		
		m_pWorkerHandle[i] = (HANDLE *)_beginthreadex(
			NULL, 0, &CallWorkerThread, this, CREATE_SUSPENDED, &threadId
		);
		if (m_pWorkerHandle[i] == NULL) 
		{
			printf_s("[ERROR] Worker thread creation failure\n");
			return false;
		}
		ResumeThread(m_pWorkerHandle[i]);
	}
	printf_s("[INFO] Worker Thread start...\n");
	return true;
}

void IOCompletionPort::WorkerThread()
{		
	// Is the function call successful?
	BOOL	bResult;
	int		nResult;
	// Overlapped The size of the data sent by the operation
	DWORD	recvBytes;
	DWORD	sendBytes;
	// Completion Key
	stSOCKETINFO *	pCompletionKey;
	//  Pointer to receive overlapped structure requested for I/O operation	
	stSOCKETINFO *	pSocketInfo;
	// 
	DWORD	dwFlags = 0;

	while (m_bWorkerThread)
	{
		/**
		 * This function causes threads to be put on hold in the WaitingThread Queue, 
		 which will take the completed work from the IOCP Queue and process it after 
		 the overlapped I/O operation occurs.		 	 
		 */
		bResult = GetQueuedCompletionStatus(m_hIOCP,
			&recvBytes,				// Bytes actually sent
			(PULONG_PTR)&pCompletionKey,	// completion key
			(LPOVERLAPPED *)&pSocketInfo,			// overlapped I/O 
			INFINITE				
		);

		if (!bResult && recvBytes == 0)
		{
			printf_s("[INFO] socket(%d) connection disrupted n", pSocketInfo->socket);
			closesocket(pSocketInfo->socket);
			free(pSocketInfo);
			continue;
		}

		pSocketInfo->dataBuf.len = recvBytes;

		if (recvBytes == 0)
		{
			closesocket(pSocketInfo->socket);
			free(pSocketInfo);
			continue;
		}
		else
		{
			printf_s("[INFO] ¸Message received  Bytes : [%d], Msg : [%s]\n",
				pSocketInfo->dataBuf.len, pSocketInfo->dataBuf.buf);

			// Send the client's response as it is				
			nResult = WSASend(
				pSocketInfo->socket,
				&(pSocketInfo->dataBuf),
				1,
				&sendBytes,
				dwFlags,
				NULL,
				NULL
			);

			if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
			{
				printf_s("[ERROR] WSASend failure: ", WSAGetLastError());
			}

			printf_s("[INFO] Send message - Bytes : [%d], Msg : [%s]\n",
				pSocketInfo->dataBuf.len, pSocketInfo->dataBuf.buf);

			// stSOCKETINFO data wipe
			ZeroMemory(&(pSocketInfo->overlapped), sizeof(OVERLAPPED));
			pSocketInfo->dataBuf.len = MAX_BUFFER;
			pSocketInfo->dataBuf.buf = pSocketInfo->messageBuffer;
 			ZeroMemory(pSocketInfo->messageBuffer, MAX_BUFFER);			
 			pSocketInfo->recvBytes = 0;
 			pSocketInfo->sendBytes = 0;
 			
			dwFlags = 0;			

			// Call WSARecv to get a response back from the client
			nResult = WSARecv(
				pSocketInfo->socket,
				&(pSocketInfo->dataBuf),
				1,
				&recvBytes,
				&dwFlags,
				(LPWSAOVERLAPPED)&(pSocketInfo->overlapped),
				NULL
			);

			if (nResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
			{
				printf_s("[ERROR] WSARecv failure : ", WSAGetLastError());
			}
		}
	}
}
