// main.cpp: Define the entry point of the console application
//

#include "stdafx.h"
#include "IOCompletionPort.h"

int main()
{
	IOCompletionPort iocp_server;
	if (iocp_server.Initialize())
	{
		iocp_server.StartServer();
	}
	return 0;
}

