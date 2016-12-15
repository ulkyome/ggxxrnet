#ifdef _MSC_VER
	#if (_MSC_VER >= 1400)
		#define POINTER_64 __ptr64
	#endif
#endif

//******************************************************************
// include
//******************************************************************
#include "netMgr.h"
#include "node.h"
#include "ggxxnet.h"
#include "denyList.h"
#include "zlib.h"
#include "util.h"

//******************************************************************
// macro
//******************************************************************
#define WSERR(api)\
{\
	char str[256];\
	int code = WSAGetLastError();\
	if (code == WSAEINTR)				sprintf(str, "%s = WSAEINTR\n", api);\
	else if (code == WSAEINVAL)			sprintf(str, "%s = WSAEINVAL\n", api);\
	else if (code == WSAEWOULDBLOCK)	sprintf(str, "%s = WSAEWOULDBLOCK\n", api);\
	else if (code == WSAETIMEDOUT)		sprintf(str, "%s = WSAETIMEDOUT\n", api);\
	else if (code == WSAECONNREFUSED)	sprintf(str, "%s = WSAECONNREFUSED\n", api);\
	else if (code == WSANO_DATA)		sprintf(str, "%s = WSANO_DATA\n", api);\
	else if (code == WSAECONNRESET)		sprintf(str, "%s = WSAECONNRESET\n", api);\
	else if (code == WSAENOTSOCK)		sprintf(str, "%s = WSAENOTSOCK\n", api);\
	else								sprintf(str, "%s = %08x\n", api, code);\
	DBGOUT_LOG(str);\
}

//******************************************************************
// global
//******************************************************************
CNetMgr* g_netMgr = NULL;

CNetMgr::CNetMgr(void)
{
	m_networkEnable		= false;
	m_quitApp			= false;
	m_recvThread_end	= true;
	m_lobbyThread_end	= true;
	m_recvThread		= NULL;
	m_lobbyThread		= NULL;

	m_udpSocket	= INVALID_SOCKET;
	m_tcpSocket	= INVALID_SOCKET;
	m_connect	= false;
	m_watch		= false;
	m_delay		= 0;
	m_queueSize	= 0;
	m_key		= NULL;
	m_syncChk	= NULL;
	m_playSide	= 1;
	m_time		= 0;
	m_suspend	= true;
	m_totalSlow	= 0;

	m_waitingConnectReply	= false;
	m_waitingSuspendReply	= false;
	m_waitingResumeReply	= false;
	m_waitingData			= false;
	m_waitingDataReply		= false;
	m_waitingWatchInReply	= false;
	m_waitingWatchInRootReply = EWIRReply_Idle;
	m_waitingBattleInfoRequestReply = false;

	m_waitingDataType		= -1;
	
	m_recvDataPtr	= NULL;
	m_recvDataSize	= 0;

	m_sendDataSeq = 0;

	setErrMsg("");

	m_remoteAddr_active = NULL_ADDR;
	m_remoteAddr_recv = NULL_ADDR;

	initWatchVars();

	InitializeCriticalSection(&m_csKey);
	InitializeCriticalSection(&m_csNode);
	InitializeCriticalSection(&m_csWatch);
}

CNetMgr::~CNetMgr(void)
{
	stopThread();

	if (m_udpSocket != INVALID_SOCKET) closesocket(m_udpSocket);
	if (m_tcpSocket != INVALID_SOCKET) closesocket(m_tcpSocket);
	WSACleanup();
	
	delete[] m_key;
	delete[] m_syncChk;
	delete[] m_watchRecvCompData;

	DeleteCriticalSection(&m_csKey);
	DeleteCriticalSection(&m_csNode);
	DeleteCriticalSection(&m_csWatch);
}

bool CNetMgr::init(int p_port, int p_delay, bool p_useLobby)
{
	try
	{
		m_delay		= p_delay;
		m_queueSize	= m_delay * 2;
		m_key		= new DWORD[m_queueSize];
		m_syncChk	= new WORD[m_queueSize];

		m_watchRecvCompData = new char[MAX_COMPWATCHDATASIZE];

		WSADATA wsadata;
		int err = WSAStartup(MAKEWORD(2, 0), &wsadata);
		if (err != 0)
		{
			DBGOUT_LOG("err WSAStartup\n");
			return false;
		}
		
		DBGOUT_LOG("WSAStartup ok\n");

		/* Own address */
		sockaddr_in	addr;
		addr = NULL_ADDR;
		addr.sin_port		= htons(p_port);
		addr.sin_family		= AF_INET;
		addr.sin_addr.s_addr= INADDR_ANY;

		// UDP Socket
		m_udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (m_udpSocket == INVALID_SOCKET) throw "socket";

		DBGOUT_LOG("udp socket() ok\n");

		if (bind(m_udpSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) throw "bind";
		
		DBGOUT_LOG("udp bind() ok\n");

		/* UDP is non-blocking */
		unsigned long val = 1;
		if (ioctlsocket(m_udpSocket, FIONBIO, &val) == SOCKET_ERROR) throw "ioctlsocket";
		
		DBGOUT_LOG("udp ioctlsocket(FIONBIO) ok\n");

#if USE_TCP
		// TCP Socket
		m_tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (m_tcpSocket == INVALID_SOCKET) throw "socket";

		DBGOUT_LOG("tcp socket() ok\n");

		char option = 1;
		if (setsockopt(m_tcpSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == SOCKET_ERROR)
		{
			throw "tcp setsockopt(SO_REUSEADDR) err";
		}
		
		DBGOUT_LOG("tcp setsockopt(SO_REUSEADDR) ok\n");

		if (bind(m_tcpSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) throw "bind";

		DBGOUT_LOG("tcp bind() ok\n");

		if (listen(m_tcpSocket, 1) == SOCKET_ERROR) throw "listen";

		DBGOUT_LOG("tcp listen() ok\n");
#endif

		DWORD tid;
		/* Create a receiving thread */
		m_recvThread = CreateThread(NULL, 0, _recvThreadProc, this, CREATE_SUSPENDED, &tid);

		//  1 = THREAD_PRIORITY_ABOVE_NORMAL
		//  0 = THREAD_PRIORITY_NORMAL
		// -1 = THREAD_PRIORITY_BELOW_NORMAL
		SetThreadPriority(m_recvThread, g_iniFileInfo.m_recvThreadPriority);

		if (m_recvThread == NULL) throw "err CreateThread _lobbyThreadProc\n";
		m_recvThread_end	= false;

		DBGOUT_LOG("recv thread ok\n");

		/* Create a lobby thread */
		if (p_useLobby)
		{
			m_lobbyThread = CreateThread(NULL, 0, _lobbyThreadProc, this, 0, &tid);
			//SetThreadPriority(m_lobbyThread, THREAD_PRIORITY_BELOW_NORMAL);
			if (m_lobbyThread == NULL) throw "err CreateThread _lobbyThreadProc\n";
			m_lobbyThread_end	= false;
		}

		DBGOUT_LOG("lobby thread ok\n");

		return true;
	}
	catch(char* msg)
	{
		if (WSAGetLastError() != 0)
		{
			WSERR(msg);
		}
		else
		{
			DBGOUT_LOG(msg);
		}
		if (m_udpSocket != INVALID_SOCKET) closesocket(m_udpSocket);
		if (m_tcpSocket != INVALID_SOCKET) closesocket(m_tcpSocket);
		WSACleanup();

		return false;
	}
}

void CNetMgr::initWatchVars(void)
{
	m_watch = false;
	m_1stCaster = false;
	m_watchRootAddr[0] = NULL_ADDR;
	m_watchRootAddr[1] = NULL_ADDR;
	__strncpy(m_watchRootName[0], "", 29);
	__strncpy(m_watchRootName[1], "", 29);
	m_watchRootGameCount[0] = 0;
	m_watchRootGameCount[1] = 0;
	m_watchRecvComplete = false;
	m_watchRecvSize = 0;

	for (int i = 0; i < WATCH_MAX_CHILD; i++) m_watcher[i].init();
	m_recvGalleryCount = 0;
	m_totalGalleryCount = 0;
	m_watchFailCount = 0;

	m_watchRecvCompSize = 0;

	DBGOUT_NET("---------------------initWatchVars\n");
}

void CNetMgr::startThread(void)
{
	ResumeThread(m_recvThread);
	ResumeThread(m_lobbyThread);
}

void CNetMgr::stopThread(void)
{
	m_quitApp = true;
	/* Wait until the thread is finished */
	for (int i = 0; i < 100; i++)
	{
		if (m_recvThread_end && m_lobbyThread_end) break;
		Sleep(20); /* Timeout in 2 seconds */
	}

	if (m_recvThread) CloseHandle(m_recvThread);
	if (m_lobbyThread) CloseHandle(m_lobbyThread);
}

void CNetMgr::connect(void)
{
	/* Clear the input of their own-partner */
	for (int i = 0; i < m_queueSize; i++)
	{
		m_key[i] = 0x00000000;
	}
	/* Synchronization information Clear */
	for (int i = 0; i < m_queueSize; i++) m_syncChk[i] = 0;
	/* Opponent of information clear */
	g_enemyInfo.clear();

	m_connect = true;
	m_watch   = false;
	m_1stCaster = true;
	m_suspend = true;
	m_suspendFrame = 0;
	m_vsloadFrame = -1;	/* -1 I represent that not being vsload */
	m_recvSuspend = true;
	m_recvDataSeq = -1;  

	m_enMaxPacketSize = 256;

	DBGOUT_NET("connect\n");
}

void CNetMgr::disconnect(char* p_cause)
{
	if (strcmp(p_cause, "SYNC ERROR!!") == 0)
	{
		g_replay.m_data.inputData[g_replay.m_frameCount++] = INPUT_SYNCERROR;
	}
	else if (strcmp(p_cause, "endbattle") == 0)
	{
		g_replay.m_data.inputData[g_replay.m_frameCount++] = INPUT_COMPLETE;
	}
	else
	{
		g_replay.m_data.inputData[g_replay.m_frameCount++] = INPUT_DISCONNECT;
	}

	// To give time to send Finally, with a margin
	ENTERCS(&g_netMgr->m_csWatch);
	for (int i = 0; i < 3; i++)
	{
		for (int i = 0; i < g_setting.watchMaxNodes + (m_watch ? WATCH_MAX_CHILD_INC : 0); i++)
		{
			if (g_netMgr->m_watcher[i].isActive())
			{
				// There is no reply from immediately after sending even wait one second
				if (g_netMgr->m_watcher[i].m_sendTime != 0xffffffff &&
					timeGetTime() - g_netMgr->m_watcher[i].m_sendTime > TIMEOUT_WATCHDATAREPLY)
				{
					g_netMgr->m_watcher[i].init();
				}
				else
				{
					g_netMgr->send_watchData(i);
				}
			}
		}
		Sleep(10);
	}
	LEAVECS(&g_netMgr->m_csWatch);

#if !TESTER
	// Save the replay, such as rewriting of ggxx variable
	onDisconnect(p_cause);
#endif

	if (m_connect)
	{
		m_connect = false;
		DBGOUT_NET("************************* disconnect %s *************************\n", p_cause);
		
		ENTERCS(&g_netMgr->m_csNode);

		char addrstr[32];
		int idx = g_nodeMgr->findNodeIdx_address(getStringFromAddr(&m_remoteAddr_active, addrstr));
		if (idx != -1)
		{
			char* id = g_nodeMgr->getNode(idx)->m_id;

			DBGOUT_LOG("disconnect (%s) %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\t#%s\n", p_cause,
				(BYTE)id[0], (BYTE)id[1], (BYTE)id[2], (BYTE)id[3], (BYTE)id[4], (BYTE)id[5],
				(BYTE)id[6], (BYTE)id[7], (BYTE)id[8], (BYTE)id[9], g_nodeMgr->getNode(idx)->m_name);
		}
		LEAVECS(&g_netMgr->m_csNode);
	}

	if (m_watch)
	{
		DBGOUT_NET("************************* disconnect (watch) %s *************************\n", p_cause);
		
		ENTERCS(&g_netMgr->m_csNode);
		for (int i = 0; i < 2; i++)
		{
			char addrstr[32];
			int idx = g_nodeMgr->findNodeIdx_address(getStringFromAddr(&m_watchRootAddr[i], addrstr));
			if (idx != -1)
			{
				char* id = g_nodeMgr->getNode(idx)->m_id;

				DBGOUT_LOG("disconnect watch P%d : %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\t#%s\n", i + 1,
					(BYTE)id[0], (BYTE)id[1], (BYTE)id[2], (BYTE)id[3], (BYTE)id[4], (BYTE)id[5],
					(BYTE)id[6], (BYTE)id[7], (BYTE)id[8], (BYTE)id[9], g_nodeMgr->getNode(idx)->m_name);
			}
		}
		LEAVECS(&g_netMgr->m_csNode);

		g_replay.m_playing = false;
	}

	initWatchVars();

	m_remoteAddr_active = NULL_ADDR;

	m_lobbyFrame = -1;
}

void CNetMgr::resume(void)
{
	if (m_connect && send_resume() == false)
	{
		disconnect("resume");
		return;
	}

	m_suspend = false;
	m_time = 0;
	m_totalSlow = 0;
	
	/* Clear the input of the other party */
	for (int i = 0; i < m_queueSize; i++)
	{
		//m_key[i] |= (m_playSide == 1) ? 0xffff0000 : 0x0000ffff;
		m_key[i] = 0;
	}
	/* Synchronization information Clear */
	for (int i = 0; i < m_queueSize; i++) m_syncChk[i] = 0;

	DBGOUT_NET("resume\n");
}

void CNetMgr::suspend(void)
{
	if (m_connect && send_suspend() == false)
	{
		disconnect("suspend");
		return;
	}

	m_suspend = true;
	m_suspendFrame = 0;
	DBGOUT_NET("suspend\n");
}

void CNetMgr::setErrMsg(char* p_msg)
{
	if (p_msg[0] == '\0' || m_errMsg[0] == '\0')
	{
		m_errMsgTime = 0;
		strcpy(m_errMsg, p_msg);
	}
}

char* CNetMgr::getStringFromAddr(sockaddr_in* p_addr, char* p_output)
{
	/* string from sockaddr_in: to convert to the format of the (ip1.ip2.ip3.ip4 port) */
	DWORD	ip = p_addr->sin_addr.S_un.S_addr;
	WORD	port = p_addr->sin_port;
	port = ((port << 8) & 0xFF00) | ((port >> 8) & 0xFF);
	sprintf(p_output, "%d.%d.%d.%d:%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, port);

	return p_output;
}

sockaddr_in CNetMgr::getAddrFromString(char* p_str)
{
	/* String: Convert from (ip1.ip2.ip3.ip4 port) in the form of a sockaddr_in */
	sockaddr_in addr;

	int		port;
	char	ip[32];
	CNodeMgr::getNodeInfoFromString(p_str, ip, &port);

	addr = NULL_ADDR;
	addr.sin_port			= htons(port);
	addr.sin_family			= AF_INET;
	addr.sin_addr.s_addr	= inet_addr(ip);
	return addr;
}

bool CNetMgr::watch(char* p_targetName, sockaddr_in* p_targetAddr, int p_targetGameCount, bool /*p_blockingMode*/)
{
	// and performs the watchIn first, likely to be concentrated on either the root node.
	// No particular problem because the delay is not applied also 100ms due to overlapping hierarchies.
	// watch in root throw a watch request directly to the specified node in the argument.
	// If you refused to accept, because the opponent of the address is sent,
	// There you throw again watch in root in.
	// [Concerns]
	// And be in trouble with the tree is filled with port 0
	// And I do not complain if the tree is constructed evenly if possible, but ...

	// Parent address, the compressed data is discarded during reconnection
	m_watchParentAddr = NULL_ADDR;
	m_watchRecvCompSize = 0;

	// watch in
	bool success = false;
	bool result = send_watchIn(p_targetName, p_targetAddr, p_targetGameCount);
	if (result) { DBGOUT_NET("watchin succeed!!\n"); return true; }
	DBGOUT_NET("watchin failed!!\n");

	/* It connects directly to the distributor so as there are no spectators */

	// watch in root[0]
	//m_remoteAddr_active = *p_targetAddr;
	result = send_watchInRoot(p_targetAddr, p_targetGameCount, success);
	if (result && success) { DBGOUT_NET("watchin root[0] succeed!!\n"); return true; }
	if (result == false)   { DBGOUT_NET("watchin root[0] failed!!\n"); goto FAILED; }

	/* To retransmit the opponent of address received and if capacity over */

	// watch in root[1]
	//m_remoteAddr_active = m_watchRootAddr[1];
	result = send_watchInRoot(&m_watchRootAddr[1], m_watchRootGameCount[1], success);
	if (result && success) { DBGOUT_NET("watchin root[1] succeed!!\n"); return true; }
	if (result == false)   { DBGOUT_NET("watchin root[1] failed!!\n"); goto FAILED; }

FAILED:
	// Stop watch mode Once you fail to reconnect
	DBGOUT_NET("failed to request watch\n");
	return false;
}

int CNetMgr::findFreeWatchEntry(sockaddr_in* p_addr)
{
	// Return it if already there
	for (int i = 0; i < g_setting.watchMaxNodes + (m_watch ? WATCH_MAX_CHILD_INC : 0); i++)
	{
		if (m_watcher[i].m_remoteAddr.sin_addr.S_un.S_addr == p_addr->sin_addr.S_un.S_addr &&
			m_watcher[i].m_remoteAddr.sin_port == p_addr->sin_port) return i;
	}
	// Find a blank unless have
	for (int i = 0; i < g_setting.watchMaxNodes + (m_watch ? WATCH_MAX_CHILD_INC : 0); i++)
	{
		if (m_watcher[i].isActive() == false) return i;
	}
	return -1; // None sky
}

int CNetMgr::getChildWatcherCount(void)
{
	int count = 0;
	for (int i = 0; i < WATCH_MAX_CHILD; i++)
	{
		if (m_watcher[i].isActive()) count += m_watcher[i].m_childCount;
	}
	return count;
}

bool CNetMgr::send_connect(sockaddr_in* p_addr)
{
	SPacket_Connect	data;
	data.packetType		= Packet_Connect;
	data.cid			= CONNECTION_ID;
	data.maxPacketSize	= g_iniFileInfo.m_maxPacketSize;
	udpsend(p_addr, (char*)&data, sizeof(data));

	m_waitingConnectReply = true;
	for (int i = 0; i * 10 < TIMEOUT_REPLY; i++)
	{
		Sleep(10);
		if (!m_waitingConnectReply)
		{
			DBGOUT_NET("send_connect success\n");
			return true;
		}
		/* Retransmission */
		if (i % 5 == 0) udpsend(p_addr, (char*)&data, sizeof(data));
	}
	DBGOUT_NET("send_connect timeout\n");
	m_waitingConnectReply = false;
	return false;
}

void CNetMgr::send_connectReply(void)
{
	SPacket_Connect	data;
	data.packetType		= Packet_ConnectReply;
	data.cid			= CONNECTION_ID;
	data.maxPacketSize	= g_iniFileInfo.m_maxPacketSize;
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

void CNetMgr::send_key(int p_time)
{
	SPacket_Key data;
	data.packetType = Packet_Key;
	data.time		= p_time;
	
	for (int i = 0; i < m_queueSize; i++)
	{
		if (m_playSide == 1)
		{
			data.cell[i].key = (WORD)m_key[i];
			data.cell[i].syncChk = (BYTE)m_syncChk[i];
		}
		else if (m_playSide == 2)
		{
			data.cell[i].key = (WORD)(m_key[i] >> 16);
			data.cell[i].syncChk = (BYTE)(m_syncChk[i] >> 8);
		}
	}

	udpsend(&m_remoteAddr_active, (char*)&data, 5 + m_queueSize * 3);
}

bool CNetMgr::send_watchInRoot(sockaddr_in* p_addr, int p_targetGameCount, bool& p_success)
{
	SPacket_WatchInRoot	data;
	data.packetType		= Packet_WatchInRoot;
	data.cid			= CONNECTION_ID;
	data.dataOffset		= m_watchRecvSize;
	data.targetGameCount= p_targetGameCount;
	data.format			= 1;	// Compression support
	data.maxPacketSize	= g_iniFileInfo.m_maxPacketSize;

	m_waitingWatchInRootReply = EWIRReply_Wait;
	for (int i = 0; i < 5; i++)
	{	
		/* Retransmission */
		udpsend(p_addr, (char*)&data, sizeof(data));
		Sleep(10);
		if (m_waitingWatchInRootReply == EWIRReply_Success ||
			m_waitingWatchInRootReply == EWIRReply_Fail)
		{
			p_success = m_waitingWatchInRootReply == EWIRReply_Success;
			DBGOUT_NET("send_watchinroot %s\n", p_success ? "success" : "fail");
			return true;
		}
	}
	DBGOUT_NET("send_watchinroot timeout\n");
	m_waitingWatchInRootReply = EWIRReply_Idle;
	p_success = false;
	return false;
}

void CNetMgr::send_watchInRootReply(bool p_accept)
{
	SPacket_WatchInRootReply	data;
	data.packetType	= Packet_WatchInRootReply;
	data.cid		= CONNECTION_ID;
	if (p_accept)
	{
		// Use format1
		data.accept = 1;
		getNameTrip(data.extra.format1.myName);
		__strncpy(data.extra.format1.enemyName, g_enemyInfo.m_name, 29);
		data.extra.format1.myGameCount = g_setting.totalBattle;
		data.extra.format1.enemyGameCount = g_enemyInfo.m_gameCount;
		memcpy(&data.enemyAddr, &m_remoteAddr_active, sizeof(m_remoteAddr_active));
		udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
	}
	else
	{
		// Use format2
		// It wants to send an opponent of address because their is not connected
		// Watching requester attempts to connect to the other party
		data.accept = 0;
		data.extra.format2.myGameCount = g_setting.totalBattle;
		data.extra.format2.enemyGameCount = g_enemyInfo.m_gameCount;
		memcpy(&data.enemyAddr, &m_remoteAddr_active, sizeof(m_remoteAddr_active));
		udpsend(&m_remoteAddr_recv, (char*)&data, SPacket_WatchInRootReply::SIZE120_2_FMT2);
	}
}

bool CNetMgr::send_watchIn(char* p_targetName, sockaddr_in* p_targetIP, int p_targetGameCount)
{
	SPacket_WatchIn	data;
	data.packetType		= Packet_WatchIn;
	data.cid			= CONNECTION_ID;
	data.targetIP		= p_targetIP->sin_addr;
	__strncpy(data.targetName, p_targetName, 29);
	data.targetGameCount= p_targetGameCount;
	data.dataOffset		= m_watchRecvSize;
	data.format			= 1;	// Compression support
	data.maxPacketSize	= g_iniFileInfo.m_maxPacketSize;

	m_waitingWatchInReply = true;
	for (int i = 0; i < 5; i++)
	{	
		/* Retransmission */
		for (int j = 0; j < g_nodeMgr->getNodeCount(); j++)
		{
			CNode* node = g_nodeMgr->getNode(j);
			if (node->m_state == State_Watch || node->m_state == State_Watch_Playable)
			{
				sockaddr_in addr = g_netMgr->getAddrFromString(node->m_addr);
				udpsend(&addr, (char*)&data, sizeof(data));
			}
		}
		Sleep(10);
		if (!m_waitingWatchInReply)
		{
			DBGOUT_NET("send_watchin success\n");
			return true;
		}
	}
	DBGOUT_NET("send_watchin timeout\n");
	m_waitingWatchInReply = false;
	return false;
}

void CNetMgr::send_watchInReply(void)
{
	SPacket_WatchInReply	data;
	data.packetType	= Packet_WatchInReply;
	data.cid		= CONNECTION_ID;
	__strncpy(data.rootName[0], m_watchRootName[0], 29);
	__strncpy(data.rootName[1], m_watchRootName[1], 29);
	data.rootIP[0] = m_watchRootAddr[0];
	data.rootIP[1] = m_watchRootAddr[1];
	data.rootGameCount[0] = m_watchRootGameCount[0];
	data.rootGameCount[1] = m_watchRootGameCount[1];
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

void CNetMgr::send_watchData(int p_idx)
{
	// The exclusion that of watch-related outside

	// Get the current recorded of Ripusaizu
	// Should the header portion is already present at the time of the spectator accepted
	// Watching at the time I live a reception worth even in unplayed
	// And it determines whether to send the compressed data to see the remaining data size
	DWORD size = 0;
	if (m_1stCaster)
	{
		if (g_replay.m_frameCount > 1) size = REPLAY_HEADER_SIZE + sizeof(DWORD) * g_replay.m_frameCount;
	}
	else
	{
		size = m_watchRecvSize;
	}

	WRITE_REPLAY_RAWDATA(size);

	// Compression supports, created if there is no compressed data being transferred size 128byte more
	if (m_watcher[p_idx].m_supportedFormat > 0 && (int)size - (int)m_watcher[p_idx].m_sendSize > 128 && m_watcher[p_idx].m_compSize == 0)
	{
		m_watcher[p_idx].m_compOffset = m_watcher[p_idx].m_sendSize;
		m_watcher[p_idx].m_compSize = zmwrite((char*)&g_replay.m_data + m_watcher[p_idx].m_sendSize, size - m_watcher[p_idx].m_sendSize, m_watcher[p_idx].m_compData, MAX_COMPWATCHDATASIZE);
		m_watcher[p_idx].m_compSendSize = 0;
		DBGOUT_NET("data compression org:%d comp:%d\n", size - m_watcher[p_idx].m_sendSize, m_watcher[p_idx].m_compSize);
	}

	if (m_watcher[p_idx].m_compSendSize < m_watcher[p_idx].m_compSize)
	{
		DBGOUT_NET("compdata %d/%d\n", m_watcher[p_idx].m_compSendSize, m_watcher[p_idx].m_compSize);

		// Send it if there is a compressed data
		SPacket_CompWatchData data;
		data.packetType			= Packet_CompWatchData;
		data.compblock_offset	= m_watcher[p_idx].m_compSendSize;
		data.compall_offset		= m_watcher[p_idx].m_compOffset;
		data.compall_size		= m_watcher[p_idx].m_compSize;
		data.galleryCount		= m_totalGalleryCount;
		data.compblock_size		= m_watcher[p_idx].m_compSize - m_watcher[p_idx].m_compSendSize;
		
		// Size limit in the lesser of the maximum packet of their own and the other party
		if (data.compblock_size > m_watcher[p_idx].m_maxPacketSize)	data.compblock_size = m_watcher[p_idx].m_maxPacketSize;
		if (data.compblock_size > g_iniFileInfo.m_maxPacketSize)	data.compblock_size = g_iniFileInfo.m_maxPacketSize;
		memcpy(data.data, m_watcher[p_idx].m_compData + data.compblock_offset, data.compblock_size);

		DBGOUT_NET("send_compWatchData %d(%d) last4 = %08x\n", data.compblock_offset, data.compblock_size, *((DWORD*)(m_watcher[p_idx].m_compData + data.compblock_offset + data.compblock_size - 4)));

		udpsend(&m_watcher[p_idx].m_remoteAddr, (char*)&data, data.compblock_size + SPacket_CompWatchData::PACKET_HEADER_SIZE);
	}
	else
	{
		SPacket_WatchData data;
		data.packetType = Packet_WatchData;
		data.offset		= m_watcher[p_idx].m_sendSize;
		data.galleryCount = m_totalGalleryCount;
		//DBGOUT_LOG("xxx send_watchData() %d\n", m_totalGalleryCount);

		if (m_watcher[p_idx].m_sendSize < size)
		{
			data.size = (WORD)(size - m_watcher[p_idx].m_sendSize);

			// dest size limit
			if (data.size > m_watcher[p_idx].m_maxPacketSize)	data.size = m_watcher[p_idx].m_maxPacketSize;
			if (data.size > g_iniFileInfo.m_maxPacketSize)		data.size = g_iniFileInfo.m_maxPacketSize;
	
			// src size limit
			if (m_watcher[p_idx].m_sendSize + data.size > sizeof(ReplayFile)) data.size = (WORD)(m_watcher[p_idx].m_sendSize + data.size - sizeof(ReplayFile));
			memcpy(data.data, (char*)&g_replay.m_data + m_watcher[p_idx].m_sendSize, data.size);
		}
		else data.size = 0;

		DBGOUT_NET("send_watchData %d(%d)\n", data.offset, data.size);

		// Already it is confirmed that the connection by sending at pre sent continues
		udpsend(&m_watcher[p_idx].m_remoteAddr, (char*)&data, data.size + SPacket_WatchData::PACKET_HEADER_SIZE);
	}
	if (m_watcher[p_idx].m_sendTime == 0xffffffff) m_watcher[p_idx].m_sendTime = timeGetTime();
}

void CNetMgr::send_watchDataReply(int p_size)
{
	SPacket_WatchDataReply	data;
	data.packetType	= Packet_WatchDataReply;
	data.reserved	= 0;
	data.size		= p_size;
	data.childCount	= 1 + getChildWatcherCount();
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

void CNetMgr::send_compWatchDataReply(int p_compsize, int p_rawsize)
{
	SPacket_CompWatchDataReply	data;
	data.packetType	= Packet_CompWatchDataReply;
	data.reserved	= 0;
	data.compsize	= p_compsize;
	data.rawsize	= p_rawsize;
	data.childCount	= 1 + getChildWatcherCount();
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

bool CNetMgr::send_ping(sockaddr_in* p_addr, int p_selNodeIdx)
{
	CNode* node = g_nodeMgr->getNode(p_selNodeIdx);

	node->m_sendpingtime = timeGetTime();

	SPacket_Ping data;
	data.packetType	= Packet_Ping;
	data.cid		= CONNECTION_ID;

	data.scriptCode	= g_scriptCode;
	getNameTrip(data.name);
	__strncpy(data.ver, GGNVERSTR, 9);
	memcpy(data.mac, g_machineID, 6);

	data.delay		= g_setting.delay;
	data.ex			= g_setting.useEx;
	data.wins		= g_setting.wins;
	data.rank		= g_setting.rank;
	data.notready	= g_vsnet.m_menu_visible || (m_lobbyFrame < g_setting.wait * 60 + 30);
	data.ignoreSlow	= g_setting.ignoreSlow;
	data.round		= g_setting.rounds;
	data.deny		= (g_denyListMgr->find(node->m_id) >= 0);	// It has refused
	data.needDetail = node->m_needDetail;
	data.gamecount	= g_setting.totalBattle;
	memcpy(data.hdid, &g_machineID[6], 4);
	data.watchFlags	= (m_watch ? WF_WATCH : 0) | (g_setting.watchIntrusion ? WF_INTRUSION : 0);
	data.watchMaxNode = g_setting.watchBroadcast ? g_setting.watchMaxNodes : -16;
	udpsend(p_addr, (char*)&data, sizeof(data));
	
	return true;
}

void CNetMgr::send_pingReply120(bool p_needDetail, bool p_deny, bool p_underV113)
{
	/* Reply to address that sent the ping */
	/* ver1.20 and exists only for compatibility with previous */
	if (p_needDetail && !m_connect && !m_watch)
	{
		if (p_underV113)
		{
			SPacket_PingReply112_3 data;
			data.packetType	= Packet_PingReply112_3;
			data.cid		= CONNECTION_ID;
			data.delay		= g_setting.delay;
			data.ex			= g_setting.useEx;
			data.wins		= g_setting.wins;
			data.rank		= g_setting.rank;
			data.notready	= g_vsnet.m_menu_visible || (m_lobbyFrame < g_setting.wait * 60 + 30);
			data.ignoreSlow	= g_setting.ignoreSlow;
			data.round		= g_setting.rounds;
			data.deny		= p_deny;
			data.gamecount	= g_setting.totalBattle;
			__strncpy(data.msg, g_setting.msg, 255);
			udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
		}
		else	// ver1.13 more
		{
			SPacket_PingReply data;
			data.packetType	= Packet_PingReply;
			data.cid		= CONNECTION_ID;
			data.delay		= g_setting.delay;
			data.ex			= g_setting.useEx;
			data.wins		= g_setting.wins;
			data.rank		= g_setting.rank;
			data.notready	= g_vsnet.m_menu_visible || (m_lobbyFrame < g_setting.wait * 60 + 30);
			data.ignoreSlow	= g_setting.ignoreSlow;
			data.round		= g_setting.rounds;
			data.deny		= p_deny;
			data.gamecount	= g_setting.totalBattle;
			__strncpy(data.msg, g_setting.msg, 255);
			memcpy(data.id, g_machineID, 10);
			udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
		}
	}
	else
	{
		/* Already get the message / competition, in watching more information unnecessary */
		SPacket_PingReplyLite120 data;
		data.packetType	= Packet_PingReplyLite120;
		data.cid		= CONNECTION_ID;
		data.delay		= g_setting.delay;
		data.ex			= g_setting.useEx;
		data.wins		= g_setting.wins;
		data.rank		= g_setting.rank;
		data.round		= g_setting.rounds;
		data.deny		= p_deny;
		data.gamecount	= g_setting.totalBattle;

		// Send your opponent Cara information if during the competition
		if (m_connect && g_replay.m_repRecording)
		{
			__strncpy(data.name[0], g_replay.m_data.name1P, 29);
			__strncpy(data.name[1], g_replay.m_data.name2P, 29);
			data.chara[0] = (char)g_replay.m_data.chara1P;
			data.chara[1] = (char)g_replay.m_data.chara2P;
		}
		else
		{
			data.name[0][0] = '\0';
			data.name[1][0] = '\0';
			data.chara[0] = CID_SOL;
			data.chara[1] = CID_SOL;
		}

		memcpy(data.id, g_machineID, 10);
		__strncpy(data.ver, GGNVERSTR, 9);

		data.watchFlags		= (m_watch ? WF_WATCH : 0) | (g_setting.watchIntrusion ? WF_INTRUSION : 0);
		data.watchMaxNode	= g_setting.watchBroadcast ? g_setting.watchMaxNodes : -16;

		// The total number accepted node with the other one or more if possible watch it (have been rejected if -16 set)
		bool casting = m_connect;
		casting &= (g_setting.watchBroadcast ? g_setting.watchMaxNodes : -16) + g_enemyInfo.m_watchMaxNode > 0;

		// It will not be in Busy when g_enemyInfo.m_rank == -1
		// Leave g_enemyInfo of information is not receive, and will be sent in Busy
		// Since then not send Ping, update will not work if the other party was Casting = On
		if (m_watch == false)
		{
			if (g_enemyInfo.m_rank == -1)			data.busy = BF_IDLE;		// idle [white]
			else if (casting && getGGXXMODE2() != 6)data.busy = BF_BUSY_CAST_NG;// busy (casting) to be [Yes] In previous ver busy handling
			else if (casting && getGGXXMODE2() == 6)data.busy = BF_BUSY_CAST_OK;// busy (casting) [white] As will be busy handling the old ver
			else if (m_connect)						data.busy = BF_BUSY;		// busy [Yes]
			else									data.busy = BF_IDLE;		// idle [white]
		}
		else data.busy = BF_BUSY;	// busy [Yes] (I want you to become a Busy even during the Watch in the old version)

		udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
	}
	//DBGOUT_NET("send_pingReply120\n");
}

void CNetMgr::send_pingReply(bool p_deny)
{
	/* Reply to address that sent the ping */

	// it is not treated as a Busy time of g_enemyInfo.m_rank == -1
	// Leave g_enemyInfo of information is not receive, and will be sent in Busy
	// Since then not send Ping, update will not work if the other party was Casting = On
	bool idle = (m_connect && g_enemyInfo.m_rank == -1);

	if (!m_connect && !m_watch || idle)	// idle
	{
		SPacket_PingReply_Idle data;
		data.packetType	= Packet_PingReply_Idle;
		data.cid		= CONNECTION_ID;

		data.notready = g_vsnet.m_menu_visible || (m_lobbyFrame < g_setting.wait * 60 + 30);

		__strncpy(data.ver, GGNVERSTR, 9);
		memcpy(data.id, g_machineID, 10);
		
		data.delay		= g_setting.delay;
		data.ex			= g_setting.useEx;
		data.wins		= g_setting.wins;
		data.rank		= g_setting.rank;
		data.round		= g_setting.rounds;
		data.ignoreSlow	= g_setting.ignoreSlow;
		data.deny		= p_deny;
		data.gamecount	= g_setting.totalBattle;
		
		data.allowIntrusion = g_setting.watchIntrusion != 0;
		data.watchMaxNode	= g_setting.watchBroadcast ? g_setting.watchMaxNodes : -16;

		udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
	}
	else if (m_watch)	// watch
	{
		SPacket_PingReply_Watch data;
		data.packetType	= Packet_PingReply_Watch;
		data.cid		= CONNECTION_ID;

		__strncpy(data.ver, GGNVERSTR, 9);
		memcpy(data.id, g_machineID, 10);

		data.delay		= g_setting.delay;
		data.ex			= g_setting.useEx;
		data.wins		= g_setting.wins;
		data.rank		= g_setting.rank;
		data.round		= g_setting.rounds;
		data.ignoreSlow	= g_setting.ignoreSlow;
		data.deny		= p_deny;
		data.gamecount	= g_setting.totalBattle;
		
		data.allowIntrusion = g_setting.watchIntrusion != 0;
		data.watchMaxNode	= g_setting.watchBroadcast ? g_setting.watchMaxNodes : -16;

		udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
	}
	else if (m_connect)
	{
		// The total number accepted node with the other one or more if possible watch it (have been rejected if -16 set)
		bool casting = (g_setting.watchBroadcast ? g_setting.watchMaxNodes : -16) + g_enemyInfo.m_watchMaxNode > 0;
		
		if (casting)	// busy casting
		{
			SPacket_PingReply_BusyCasting data;
			data.packetType	= Packet_PingReply_BusyCasting;
			data.cid		= CONNECTION_ID;

#if TESTER
			data.casting = 1;
#else
			data.casting = getGGXXMODE2() == 6; // And if during the competition OK
#endif
			data.wins		= g_setting.wins;
			data.rank		= g_setting.rank;
			data.gamecount	= g_setting.totalBattle;

			udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
		}
		else			// busy
		{
			SPacket_PingReply_Busy data;
			data.packetType	= Packet_PingReply_Busy;
			data.cid		= CONNECTION_ID;

			udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
		}
	}
	//DBGOUT_NET("send_pingReply\n");
}

void CNetMgr::send_comment(void)
{
	SPacket_Comment data;
	data.packetType	= Packet_Comment;
	data.size = strnlen(g_setting.msg, 256);
	__strncpy(data.msg, g_setting.msg, data.size);
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(char) * 3 + data.size);
}

void CNetMgr::send_vsLoadCompleted(void)
{
	SPacket_VSLoadCompleted data;
	data.packetType	= Packet_VSLoadCompleted;
	data.cid		= CONNECTION_ID;
	udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));
}

bool CNetMgr::send_suspend(void)
{
	SPacket_Suspend data;
	data.packetType	= Packet_Suspend;
	data.cid		= CONNECTION_ID;
	udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));

	m_waitingSuspendReply = true;
	for (int i = 0; i * 10 < TIMEOUT_REPLY; i++)
	{
		Sleep(10);
		if (!m_waitingSuspendReply)
		{
			DBGOUT_NET("send_suspend success\n");
			return true;
		}
		/* Retransmission */
		if (i % 5 == 0) udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));
	}
	DBGOUT_NET("send_suspend timeout\n");
	m_waitingSuspendReply = false;
	return false;
}

void CNetMgr::send_suspendReply(void)
{
	SPacket_SuspendReply data;
	data.packetType	= Packet_SuspendReply;
	data.cid		= CONNECTION_ID;
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

bool CNetMgr::send_resume(void)
{
	SPacket_Resume data;
	data.packetType	= Packet_Resume;
	data.cid		= CONNECTION_ID;
	udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));

	m_waitingResumeReply = true;
	for (int i = 0; i * 10 < TIMEOUT_REPLY; i++)
	{
		Sleep(10);
		if (!m_waitingResumeReply)
		{
			DBGOUT_NET("send_resume success\n");
			return true;
		}
		/* Retransmission */
		if (i % 5 == 0) udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));
	}
	DBGOUT_NET("send_resume timeout\n");
	m_waitingResumeReply = false;
	return false;
}

void CNetMgr::send_resumeReply(void)
{
	SPacket_ResumeReply data;
	data.packetType	= Packet_ResumeReply;
	data.cid		= CONNECTION_ID;
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

void CNetMgr::send_dataReply(int p_seq)
{
	SPacket_DataReply data;
	data.packetType	= Packet_DataReply;
	data.seq		= p_seq;
	udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));
}

void CNetMgr::send_nodeaddr115_3(sockaddr_in* p_addr, CNode* p_node)
{
	if (p_node->m_scriptCode != 0)
	{
		SPacket_NodeAddr data;
		data.packetType = Packet_NodeAddr;
		data.scriptCode = p_node->m_scriptCode;
		memcpy(data.name, p_node->m_name, 30);
		memcpy(data.addr, p_node->m_addr, 32);
		udpsend(p_addr, (char*)&data, sizeof(data));
	}
}

void CNetMgr::send_galleryCount(void)
{
	SPacket_GalleryCount data;
	data.packetType		= Packet_GalleryCount;
	data.galleryCount	= getChildWatcherCount();
	udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));
}

void CNetMgr::send_galleryCountForLobby(void)
{
	if (m_watchRecvSize > REPLAY_HEADER_SIZE)
	{
		SPacket_GalleryCountForLobby data;
		data.packetType		= Packet_GalleryCountForLobby;
		data.galleryCount	= m_totalGalleryCount;
		__strncpy(data.name[0], m_watchRootName[0], 29);
		__strncpy(data.name[1], m_watchRootName[1], 29);
		data.ip[0] = m_watchRootAddr[0].sin_addr.S_un.S_addr;
		data.ip[1] = m_watchRootAddr[1].sin_addr.S_un.S_addr;
		data.gameCount[0] = m_watchRootGameCount[0];
		data.gameCount[1] = m_watchRootGameCount[1];
		udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
	}
}

void CNetMgr::send_nameRequest(sockaddr_in* p_addr)
{
	SPacket_NameRequest data;
	data.packetType		= Packet_NameRequest;
	data.cid			= CONNECTION_ID;
	udpsend(p_addr, (char*)&data, sizeof(data));
}

void CNetMgr::send_debugInfo(void)
{
	SPacket_DebugInfo data;
	data.packetType	= Packet_DebugInfo;
	memcpy(data.cpu_name, g_cpuid.getCpuName(), 49);
	data.cpu_eax	= g_cpuid.getEAX();
	data.cpu_ecx	= g_cpuid.getECX();
	data.cpu_edx	= g_cpuid.getEDX();
	data.fcw		= GETFCW();
	data.analog[0]	= *((char*)0x602910);
	data.analog[1]	= *((char*)0x602944);
	data.ggmode		= *((char*)0x602962);

	for (int i = 0; i < 5; i++)
	{
		udpsend(&m_remoteAddr_active, (char*)&data, sizeof(data));
		Sleep(10);
	}
}

bool CNetMgr::send_battleInfoRequest(char* p_targetName, sockaddr_in* p_targetIP, int p_targetGameCount)
{
	SPacket_BattleInfoRequest	data;
	data.packetType	= Packet_BattleInfoRequest;
	data.targetIP	= p_targetIP->sin_addr;
	__strncpy(data.targetName, p_targetName, 29);
	data.targetGameCount = p_targetGameCount;

	m_waitingBattleInfoRequestReply = true;
	for (int i = 0; i < 5; i++)
	{
		for (int j = 0; j < g_nodeMgr->getNodeCount(); j++)
		{
			CNode* node = g_nodeMgr->getNode(j);
			if (node->m_state == State_Watch || node->m_state == State_Watch_Playable ||
				node->m_state == State_Idle || node->m_state == State_NotReady)
			{
				sockaddr_in addr = g_netMgr->getAddrFromString(node->m_addr);
				udpsend(&addr, (char*)&data, sizeof(data));
			}
		}
		Sleep(10);
		if (!m_waitingBattleInfoRequestReply)
		{
			DBGOUT_NET("send_batinf_request success\n");
			return true;
		}
	}
	// It connects directly because there is no response from anyone
	for (int i = 0; i < 5; i++)
	{
		/* Retransmission */
		udpsend(p_targetIP, (char*)&data, sizeof(data));
		Sleep(10);
		if (!m_waitingBattleInfoRequestReply)
		{
			DBGOUT_NET("send_batinf_request(root) success\n");
			return true;
		}
	}
	DBGOUT_NET("send_batinf_request timeout\n");
	m_waitingBattleInfoRequestReply = false;
	return false;
}

void CNetMgr::send_battleInfo(char* p_name1, char* p_name2, DWORD p_ip1, DWORD p_ip2, DWORD p_gamecount1, DWORD p_gamecount2, char p_chara1, char p_chara2)
{
	SPacket_BattleInfo data;
	data.packetType	= Packet_BattleInfo;
	__strncpy(data.name[0], p_name1, 29);
	__strncpy(data.name[1], p_name2, 29);
	data.ip[0].S_un.S_addr = p_ip1;
	data.ip[1].S_un.S_addr = p_ip2;
	data.gamecount[0] = p_gamecount1;
	data.gamecount[1] = p_gamecount2;
	data.chara[0] = p_chara1;
	data.chara[1] = p_chara2;
	udpsend(&m_remoteAddr_recv, (char*)&data, sizeof(data));
}

bool CNetMgr::sendDataBlock(char p_type, char* p_data, int p_dataSize, int p_timeout)
{
#if USE_TCP
	char* data = new char[p_dataSize + 1];
	data[0] = p_type;
	if (p_data) memcpy(&data[1], p_data, p_dataSize);
	bool result = (g_netMgr->tcpsend(data, p_dataSize + 1, p_timeout) > 0);
	
	delete data;
	return result;
#else
	/* Use the udp send to ensure a large amount of data */

	/* Adapt to the smaller on their own and the other party */
	int maxDataSize = g_iniFileInfo.m_maxPacketSize;
	if (maxDataSize > m_enMaxPacketSize) maxDataSize = m_enMaxPacketSize;
	for (int i = 0; i * maxDataSize < p_dataSize; i++)
	{
		int size = p_dataSize - i * maxDataSize;
		if (size > maxDataSize) size = maxDataSize;
		
		/* Split into possible transmission size */
		SPacket_Data	data;
		data.packetType	= Packet_Data;
		data.type		= p_type;
		data.seq		= m_sendDataSeq;
		data.dataSize	= size;
		data.dataOffset	= maxDataSize * i;
		memcpy(data.data, &p_data[maxDataSize * i], size);
		
		bool success = false;
		m_waitingDataReply = true;
		for (int j = 0; j * 10 < p_timeout; j++)
		{
			if (!m_waitingDataReply)
			{
				DBGOUT_NET("send_data %d success\n", i);
				success = true;
				break;
			}
			/* Retransmission */
			udpsend(&m_remoteAddr_active, (char*)&data, g_iniFileInfo.m_maxPacketSize + SPacket_Data::PACKET_HEADER_SIZE);
			Sleep(10);
		}
		if (success == false)
		{
			m_waitingDataReply = false;
			return false;
		}
		m_sendDataSeq++;
	}
	return true;
#endif
}

bool CNetMgr::recvDataBlock(char p_type, char* p_data, int p_dataSize, int p_timeout)
{
#if USE_TCP
	char* data = new char[p_dataSize + 1];
	
	int readsize = 0;
	while (readsize == 0)
	{
		readsize = g_netMgr->tcprecv(data, p_dataSize + 1, p_timeout);

		if (readsize == 0) break;

		if (data[0] == p_type)
		{
			memcpy(p_data, &data[1], readsize > p_dataSize ? p_dataSize : readsize);
			readsize -= 1; /* Pull type minute */
		}
		else readsize = 0;
	}
	delete data;
	return (readsize > 0);
#else
	/* Use the udp to receive to ensure a large volume of data */
	m_recvDataPtr  = p_data;
	m_recvDataSize = p_dataSize;

	m_waitingDataType = p_type;
	m_waitingData = true;
	for (int i = 0; i * 10 < p_timeout; i++)
	{
		Sleep(10);
		if (!m_waitingData)
		{
			DBGOUT_NET("recvdata success\n");
			m_recvDataPtr = NULL;
			m_recvDataSize = 0;
			return true;
		}
	}
	DBGOUT_NET("recvdata timeout\n");
	m_waitingData = false;
	m_waitingDataType = -1;
	return false;
#endif
}

bool CNetMgr::talking(void)
{
	if (m_networkEnable == false) return false; 

	int packetSize = udprecv((char*)&m_buf, PACKETMAX_SIZE);
	if (packetSize <= 0) return false;
	
//#if DEBUG_OUTPUT_NET
//	char packetStr[256] = "";
//	if (m_buf.packetType == Packet_Connect)				strcpy(packetStr, "Packet_Connect");
//	else if (m_buf.packetType == Packet_ConnectReply)	strcpy(packetStr, "Packet_ConnectReply");
//	else if (m_buf.packetType == Packet_Resume)			strcpy(packetStr, "Packet_Resume");
//	else if (m_buf.packetType == Packet_ResumeReply)	strcpy(packetStr, "Packet_ResumeReply");
//	else if (m_buf.packetType == Packet_Suspend)		strcpy(packetStr, "Packet_Suspend");
//	else if (m_buf.packetType == Packet_SuspendReply)	strcpy(packetStr, "Packet_SuspendReply");
//	else if (m_buf.packetType == Packet_Data)			strcpy(packetStr, "Packet_Data");
//	else if (m_buf.packetType == Packet_DataReply)		strcpy(packetStr, "Packet_DataReply");
//	else if (m_buf.packetType == Packet_WatchIn)		strcpy(packetStr, "Packet_WatchIn");
//	else if (m_buf.packetType == Packet_WatchInRoot)	strcpy(packetStr, "Packet_WatchInRoot");
//	else if (m_buf.packetType == Packet_WatchData)		strcpy(packetStr, "Packet_WatchData");
//	else if (m_buf.packetType == Packet_WatchDataReply)	strcpy(packetStr, "Packet_WatchDataReply");
//
//	if (strcmp(packetStr, "") != 0)
//	{
//		DBGOUT_NET("recv %s from %d.%d.%d.%d\n", packetStr,
//				m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b1,
//				m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b2,
//				m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b3,
//				m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b4);
//	}
//#endif

	switch (m_buf.packetType)
	{
	case Packet_Ping:
		{
			SPacket_Ping *data = (SPacket_Ping*)&m_buf;
			
			/* Ignore if the server is different */
			if (useLobbyServer() && data->scriptCode != g_scriptCode) break;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx == -1)
			{
				if (useLobbyServer())
				{
					// Registering you come ping from the node that you do not know
					// Since the criterion of findNodeIdx_address has changed from ver1.15-3,
					// It does not call here if the port is changed by NAT
					idx = g_nodeMgr->addNode(addrstr, UNKNOWN_NAME, true, true);
					if (idx == -1)
					{
						LEAVECS(&g_netMgr->m_csNode);
						break;
					}
				}
				else
				{
					/* It does not respond to ping */
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}
			}

			CNode* node = g_nodeMgr->getNode(idx);

			// Update with a valid address
			__strncpy(node->m_addr, addrstr, 31);

			/* VersionError check */
			if (data->cid != CONNECTION_ID)
			{
				node->m_state = State_VersionError;
				LEAVECS(&g_netMgr->m_csNode);
				break;
			}

			/* Updated opponent of information */
			node->m_delay		= data->delay;
			node->m_ex			= data->ex;
			node->m_rank		= data->rank;
			node->m_win			= data->wins;
			node->m_ignoreSlow	= data->ignoreSlow;
			node->m_round		= data->round;
			node->m_deny		= data->deny;
			node->m_gamecount	= data->gamecount;
			node->m_scriptCode	= data->scriptCode;
			__strncpy(node->m_name, data->name, 29);
			__strncpy(node->m_ver, data->ver, 9);

			node->clearBattleInfo();

			// Incomplete code test
			memcpy(node->m_id, data->mac, 6);
			if (packetSize >= SPacket_Ping::SIZE120)
			{
				// 1.20 and can acquire reject code of 10byte than the ping
				memcpy(&node->m_id[6], data->hdid, 4);
			}

			/* is busy, not a no response that ping came */
			node->m_state = State_Idle;
			if (data->notready == 1)		node->m_state = State_NotReady;
			
			// 1.20 Support for watching state from
			if (packetSize >= SPacket_Ping::SIZE120)
			{
				node->m_validInfo |= data->VF120;
				node->m_allowIntrusion  = (data->watchFlags & WF_INTRUSION) != 0;
				node->m_watchMaxNode	= data->watchMaxNode;

				if (data->watchFlags & WF_WATCH) node->m_state = State_Watch;
				if (node->m_state == State_Watch && node->m_allowIntrusion) node->m_state = State_Watch_Playable;
			}
			else
			{
				node->m_validInfo |= data->VF115;
				node->m_watchMaxNode = -16;
			}

			bool netSpeedGood = node->isNetSpeedGood();
			bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
			if (!netSpeedGood && ignoreSlow)		node->m_state = State_PingOver;

			bool underV113 = strcmp(node->m_ver, "1.13") < 0;

			if (node->m_delay != g_setting.delay)			node->m_state = State_Mismatch;
			if (underV113 && node->m_ex != g_setting.useEx)	node->m_state = State_Mismatch;

			/* Hit the reply */
			if (strcmp(node->m_ver, "1.20") <= 0)
			{
				send_pingReply120(data->needDetail, g_denyListMgr->find(node->m_id) >= 0, underV113);
			}
			else
			{
				send_pingReply(g_denyListMgr->find(node->m_id) >= 0);

				if (data->needDetail && !m_connect)
				{
					send_comment();
				}
			}

			/* Information spectator also reply */
			if (m_watch)
			{
				if (strcmp(node->m_ver, "1.20") >= 0) send_galleryCountForLobby();
			}

			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReply112_3:
		{
			SPacket_PingReply112_3 *data = (SPacket_PingReply112_3*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);

				/* ping value measurement */
				node->recordPing();

				// Update with a valid address
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				/* Updated opponent of information */
				node->m_validInfo |= data->VF;
				node->m_delay		= data->delay;
				node->m_ex			= data->ex;
				node->m_rank		= data->rank;
				node->m_win			= data->wins;
				node->m_ignoreSlow	= data->ignoreSlow;
				node->m_round		= data->round;
				node->m_deny		= data->deny;
				node->m_gamecount	= data->gamecount;
				node->m_needDetail	= false;
				__strncpy(node->m_msg, data->msg, 255);

				node->m_watchMaxNode = -16;

				/* pingReply (details) that came is busy, not a no response */
				node->m_state = State_Idle;
				if (data->notready == 1)				node->m_state = State_NotReady;

				bool netSpeedGood = node->isNetSpeedGood();
				bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
				if (!netSpeedGood && ignoreSlow) node->m_state = State_PingOver;

				if (node->m_delay != g_setting.delay)									node->m_state = State_Mismatch;
				if (strcmp(node->m_ver, "1.13") < 0 && node->m_ex != g_setting.useEx)	node->m_state = State_Mismatch;
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReply:
		{
			SPacket_PingReply *data = (SPacket_PingReply*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);

				/* ping value measurement */
				node->recordPing();

				// Update with a valid address
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				/* Updated opponent of information */
				node->m_delay		= data->delay;
				node->m_ex			= data->ex;
				node->m_rank		= data->rank;
				node->m_win			= data->wins;
				node->m_ignoreSlow	= data->ignoreSlow;
				node->m_round		= data->round;
				node->m_deny		= data->deny;
				node->m_needDetail	= false;
				__strncpy(node->m_msg, data->msg, 255);

				if (packetSize >= SPacket_PingReply::SIZE120)
				{
					node->m_validInfo |= data->VF115;
					node->m_gamecount = data->gamecount;
				}
				else
				{
					node->m_validInfo |= data->VF120;
				}

				memcpy(node->m_id, data->id, 10);

				/* pingReply (details) that came is busy, not a no response */
				node->m_state = State_Idle;
				if (data->notready == 1)				node->m_state = State_NotReady;

				bool netSpeedGood = node->isNetSpeedGood();
				bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
				if (!netSpeedGood && ignoreSlow)		node->m_state = State_PingOver;

				if (node->m_delay != g_setting.delay)	node->m_state = State_Mismatch;
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReplyLite120:
		{
			SPacket_PingReplyLite120 *data = (SPacket_PingReplyLite120*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);
				
				/* ping value measurement */
				node->recordPing();

				// Update with a valid address
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				// 1.20 Support for watching state from
				// It was decided that deny information is also sent to because there is a possibility that stormed to
				if (packetSize >= SPacket_PingReplyLite120::SIZE120)
				{
					node->m_validInfo |= data->VF120;

					node->m_delay		= data->delay;
					node->m_ex			= data->ex;
					node->m_rank		= data->rank;
					node->m_win			= data->wins;
					//node->m_ignoreSlow	= 1;
					node->m_round		= data->round;
					node->m_deny		= data->deny;
					node->m_gamecount	= data->gamecount;

					// I use enough so as prima facie their information
					if (data->chara[0]-1 >= 0 && data->chara[0]-1 < CHARACOUNT &&
						data->chara[1]-1 >= 0 && data->chara[1]-1 < CHARACOUNT)
					{
						__strncpy(node->m_battleInfoName[0], data->name[0], 29);
						__strncpy(node->m_battleInfoName[1], data->name[1], 29);
						node->m_battleInfoIP[0] = 0;
						node->m_battleInfoIP[1] = 0;
						node->m_battleInfoGameCount[0] = -1;
						node->m_battleInfoGameCount[1] = -1;
						node->m_battleInfoChara[0] = data->chara[0];
						node->m_battleInfoChara[1] = data->chara[1];
					}

					memcpy(node->m_id, data->id, 10);
					__strncpy(node->m_ver, data->ver, 9);

					node->m_allowIntrusion  = (data->watchFlags & WF_INTRUSION) != 0;
					node->m_watchMaxNode	= data->watchMaxNode;

					if (data->watchFlags & WF_WATCH)
					{
						node->m_state = node->m_allowIntrusion ? State_Watch_Playable : State_Watch;
						
						bool netSpeedGood = node->isNetSpeedGood();
						bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
						if (!netSpeedGood && ignoreSlow) node->m_state = State_PingOver;

						if (node->m_delay != g_setting.delay) node->m_state = State_Mismatch;
					}
					else
					{
						bool netSpeedGood = node->isNetSpeedGood();
						bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
						if (!netSpeedGood && ignoreSlow) node->m_state = State_PingOver;

						/* The busy if it is not sent any more, waits for the sent from the other party */
						switch (data->busy)
						{
						case BF_IDLE:			break;
						case BF_BUSY:			node->m_state = State_Busy; break;
						case BF_BUSY_CAST_OK:	node->m_state = State_Busy_Casting; break;
						case BF_BUSY_CAST_NG:	node->m_state = State_Busy_Casting_NG; break;
						default:				node->m_state = State_Busy; break;
						}

						if (node->m_state == State_Busy_Casting ||
							node->m_state == State_Busy_Casting_NG) 
						{
							if (node->m_delay != g_setting.delay) node->m_state = State_Mismatch;
						}
					}
				}
				else
				{
					node->m_validInfo |= data->VF115;

					if (data->busy != BF_IDLE) node->m_state = State_Busy;
					node->m_watchMaxNode = -16;
				}
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReply_BusyCasting:
		{
			SPacket_PingReply_BusyCasting *data = (SPacket_PingReply_BusyCasting*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);

				/* Update with a valid address */
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				/* ping value measurement */
				node->recordPing();

				/* Updated opponent of information */
				node->m_validInfo |= data->VF;
				node->m_win			= data->wins;
				node->m_rank		= data->rank;
				node->m_gamecount	= data->gamecount;

				/* Update of State */
				node->m_state = data->casting ? State_Busy_Casting : State_Busy_Casting_NG;
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReply_Busy:
		{
			SPacket_PingReply_Busy *data = (SPacket_PingReply_Busy*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);
				
				node->clearBattleInfo();

				/* Update with a valid address */
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				/* ping value measurement */
				node->recordPing();
				
				/* Updated opponent of information */
				node->m_validInfo |= data->VF;

				/* Update of State */
				node->m_state = State_Busy;
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReply_Watch:
		{
			SPacket_PingReply_Watch *data = (SPacket_PingReply_Watch*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);
				
				node->clearBattleInfo();

				/* Update with a valid address */
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				/* ping value measurement */
				node->recordPing();
				
				/* Updated opponent of information */
				node->m_validInfo |= data->VF;
				__strncpy(node->m_ver, data->ver, 9);
				memcpy(node->m_id, data->id, 10);

				node->m_delay		= data->delay;
				node->m_ex			= data->ex;
				node->m_win			= data->wins;
				node->m_rank		= data->rank;
				node->m_round		= data->round;
				node->m_ignoreSlow	= data->ignoreSlow;
				node->m_deny		= data->deny;
				node->m_gamecount	= data->gamecount;
				
				node->m_allowIntrusion = data->allowIntrusion;
				node->m_watchMaxNode = data->watchMaxNode;

				/* Update of State */
				node->m_state = node->m_allowIntrusion ? State_Watch_Playable : State_Watch;

				if (node->m_state == State_Watch_Playable)
				{
					// I Ping / DelayCheck only case of stormed Allowed
					bool netSpeedGood = node->isNetSpeedGood();
					bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
					if (!netSpeedGood && ignoreSlow) node->m_state = State_PingOver;

					if (node->m_delay != g_setting.delay) node->m_state = State_Mismatch;
				}
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_PingReply_Idle:
		{
			SPacket_PingReply_Idle *data = (SPacket_PingReply_Idle*)&m_buf;

			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);
				
				node->clearBattleInfo();

				/* Update with a valid address */
				__strncpy(node->m_addr, addrstr, 31);

				/* VersionError check */
				if (data->cid != CONNECTION_ID)
				{
					node->m_state = State_VersionError;
					LEAVECS(&g_netMgr->m_csNode);
					break;
				}

				/* ping value measurement */
				node->recordPing();
				
				/* Updated opponent of information */
				node->m_validInfo |= data->VF;
				__strncpy(node->m_ver, data->ver, 9);
				memcpy(node->m_id, data->id, 10);

				node->m_delay		= data->delay;
				node->m_ex			= data->ex;
				node->m_win			= data->wins;
				node->m_rank		= data->rank;
				node->m_round		= data->round;
				node->m_ignoreSlow	= data->ignoreSlow;
				node->m_deny		= data->deny;
				node->m_gamecount	= data->gamecount;
				
				node->m_allowIntrusion = data->allowIntrusion;
				node->m_watchMaxNode = data->watchMaxNode;

				/* Update of State */
				node->m_state = data->notready ? State_NotReady : State_Idle;

				bool netSpeedGood = node->isNetSpeedGood();
				bool ignoreSlow = g_setting.ignoreSlow || node->m_ignoreSlow;
				if (!netSpeedGood && ignoreSlow) node->m_state = State_PingOver;

				if (node->m_delay != g_setting.delay) node->m_state = State_Mismatch;
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_Comment:
		{
			SPacket_Comment *data = (SPacket_Comment*)&m_buf;
			
			ENTERCS(&g_netMgr->m_csNode);
			
			char addrstr[32];
			getStringFromAddr(&m_remoteAddr_recv, addrstr);
			int idx = g_nodeMgr->findNodeIdx_address(addrstr);
			if (idx != -1)
			{
				CNode* node = g_nodeMgr->getNode(idx);
				node->m_needDetail = false;
				__strncpy(node->m_msg, data->msg, data->size);
				node->m_msg[data->size] = '\0';
				node->m_validInfo |= data->VF;
			}
			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_Connect:
		/*
		Reject connections from other nodes while he is issued the connection request
		Auto Connect Wait can be connected from the outside when I finished
		It may prove to be funny when you connect immediately to the same node immediately after cutting,
		and should the node can not connect transmission since should be a busy until it is cut with a timeout
		Myself is connect state, since there is a possibility that not arrived connectReply opponent m_connect do not check
		*/
		// lobbyFrame check I quit because the failure appears while watching. There is no significant problem because such should be the NotReady in connection side
		if ((!m_watch || g_setting.watchIntrusion) && m_waitingConnectReply == false)
		{
			SPacket_Connect *data = (SPacket_Connect*)&m_buf;
			
			if (data->cid == CONNECTION_ID)
			{
				bool connectok = true;

				ENTERCS(&g_netMgr->m_csNode);

				char addrstr[32];
				getStringFromAddr(&m_remoteAddr_recv, addrstr);
				int idx = g_nodeMgr->findNodeIdx_address(addrstr);
				if (idx != -1)
				{
					// 08/02/04 have no time to intrudes spectator that aimed ping
					// And that would be connected by a no check so request be coming is not a PingOver If you see from the other party
					// 08/02/18 is to not receive a certain level of ping value, it can be expected decent value for one single ping values
					// for once to examine the watch state is sending a ping, to check the ping value at that time

					/* In the case of low-speed line denial, and determine the ping value */
					/* When the drawing performance of the other PC is not caught up, it can not be determined here */
					if (g_setting.ignoreSlow && g_nodeMgr->getNode(idx)->isNetSpeedGood() == false)
					{
						/* The rejected because likely below the 60fps */
						connectok = false;
						DBGOUT_NET("slow connection was ignored.\n");
					}
					else if (g_denyListMgr->find(g_nodeMgr->getNode(idx)->m_id) != -1)
					{
						/* Partner has refused */
						connectok = false;
						DBGOUT_NET("denied player was ignored.\n");
					}
				}
				else
				{
					/* Denial if it does not find the node */
					connectok = false;
					DBGOUT_NET("node not found.\n");
				}
				
				LEAVECS(&g_netMgr->m_csNode);

				if (connectok)
				{
					/* Myself is connect state, Retransmission is necessary because there is a possibility that not arrived connectReply to opponent */
					/* If Retransmission to the unconnected or already connected to the other party to Retransmission the Reply. */
					if (m_remoteAddr_active.sin_port == 0 || memcmp(&m_remoteAddr_active, &m_remoteAddr_recv, sizeof(m_remoteAddr_recv)) == 0)
					{
						/* Cut if during watching */
						if (m_watch) disconnect("intrusion!!");

						/* And notify the successful connection */
						g_netMgr->send_connectReply();
						connect();
						m_playSide = 2;

						/* Setting if the maximum packet size is specified */
						if (packetSize >= SPacket_Connect::SIZE120_2)
						{
							m_enMaxPacketSize = data->maxPacketSize;
						}

						/* And stores the address of the connection partner here */
						m_remoteAddr_active = m_remoteAddr_recv;
						DBGOUT_NET("active remote address updated!!\n");
					}
				}
			}
		}
		break;
	case Packet_ConnectReply:
		/* Only accept the connection between yourself just in case has issued the connection request */
		if (!m_connect && !m_watch && m_waitingConnectReply)
		{
			SPacket_ConnectReply *data = (SPacket_ConnectReply*)&m_buf;

			if (data->cid == CONNECTION_ID)
			{
				connect();
				m_playSide = 1;

				/* Setting if the maximum packet size is specified */
				if (packetSize >= SPacket_ConnectReply::SIZE120_2)
				{
					m_enMaxPacketSize = data->maxPacketSize;
				}

				/* Transmission completion */
				m_waitingConnectReply = false;
			}
		}
		else
		{
			DBGOUT_NET("connection was duplicated!!\n");
		}
		break;
	case Packet_Key:
		if (m_connect)
		{
			SPacket_Key *data = (SPacket_Key*)&m_buf;

			ENTERCS(&m_csKey);
			
			int diff = m_time - data->time;
			
			/* View delay */
			//DBGOUT_NET("%d:%d\n", m_time, diff);

			for (int i = 0; i < m_queueSize; i++)
			{
				int idx1 = (diff > 0) ? (i + diff) : i;
				int idx2 = (diff < 0) ? (i - diff) : i;

				if (idx1 >= m_queueSize || idx2 >= m_queueSize) break;

				if (m_playSide == 1)
				{
					m_key[idx1]		= (m_key[idx1] & 0x0000FFFF) | (data->cell[idx2].key << 16);
					m_syncChk[idx1] = (m_syncChk[idx1] & 0x00FF) | (data->cell[idx2].syncChk << 8);
				}
				if (m_playSide == 2)
				{
					m_key[idx1]		= (m_key[idx1] & 0xFFFF0000) | data->cell[idx2].key;
					m_syncChk[idx1]	= (m_syncChk[idx1] & 0xFF00) | data->cell[idx2].syncChk;
				}
			}
			LEAVECS(&m_csKey);
		}
		break;
	case Packet_VSLoadCompleted:
		if (m_connect && g_netMgr->m_recvVSLoadCompleted == false)
		{
			SPacket_VSLoadCompleted *data = (SPacket_VSLoadCompleted*)&m_buf;

			if (data->cid == CONNECTION_ID)
			{
				g_netMgr->m_recvVSLoadCompleted = true;
				DBGOUT_NET("VsLoadCompleted!!\n");
			}
		}
		break;
	case Packet_Suspend:
		/* the processing is performed even if a no m_recvSuspend == false */
		/* Because suspendReply there is a possibility not reach the other party */
		if (m_connect)
		{
			SPacket_Suspend *data = (SPacket_Suspend*)&m_buf;
			if (data->cid == CONNECTION_ID)
			{
				/* To notify the successful reception */
				send_suspendReply();
				m_recvSuspend = true;
			}
		}
		break;
	case Packet_SuspendReply:
		if (m_connect && m_waitingSuspendReply)
		{
			SPacket_SuspendReply *data = (SPacket_SuspendReply*)&m_buf;
			if (data->cid == CONNECTION_ID)
			{
				/* Transmission completion */
				m_waitingSuspendReply = false;
			}
		}
		break;
	case Packet_Resume:
		/* the processing is performed even if a no m_recvSuspend == true */
		/* Because resumeReply there is a possibility not reach the other party */
		if (m_connect)
		{
			SPacket_Resume *data = (SPacket_Resume*)&m_buf;
			if (data->cid == CONNECTION_ID)
			{
				/* To notify the successful reception */
				send_resumeReply();
				m_recvSuspend = false;
			}
		}
		break;
	case Packet_ResumeReply:
		if (m_connect && m_waitingResumeReply)
		{
			SPacket_ResumeReply *data = (SPacket_ResumeReply*)&m_buf;
			if (data->cid == CONNECTION_ID)
			{
				/* Transmission completion */
				m_waitingResumeReply = false;
			}
		}
		break;
	case Packet_Data:
		if (m_connect)
		{
			SPacket_Data *data = (SPacket_Data*)&m_buf;

			if (data->type == m_waitingDataType)
			{
				if (m_waitingData && m_recvDataSeq < data->seq)
				{
					if (m_recvDataSize >= data->dataOffset + data->dataSize)
					{
						memcpy(&m_recvDataPtr[data->dataOffset], data->data, data->dataSize);
					}
					else
					{
						DBGOUT_NET("m_recvDataPtr overflow!! type = %d\n", data->type);
					}
					DBGOUT_NET("testdata seq %3d,offset=%08x\n", data->seq, data->dataOffset);
					if (data->dataOffset + data->dataSize == m_recvDataSize) m_waitingData = false; /* Data reception completion */
					m_recvDataSeq = data->seq;
				}
				/* Notify the successful reception (and also send in the old sequence for Retransmission) */
				if (m_recvDataSeq >= data->seq)
				{
					send_dataReply(data->seq);
					DBGOUT_NET("send testdatareply seq %3d, wait=%d, recvseq=%d\n", data->seq, m_waitingData, m_recvDataSeq);
				}
			}
		}
		break;
	case Packet_DataReply:
		if (m_connect && m_waitingDataReply)
		{
			SPacket_DataReply *data = (SPacket_DataReply*)&m_buf;
			if (data->seq == m_sendDataSeq)
			{
				/* Transmission completion */
				m_waitingDataReply = false;
			}
		}
		DBGOUT_NET("testdatareply seq %3d, %d%d\n", m_sendDataSeq, m_connect, m_waitingDataReply);
		break;
	case Packet_WatchInRoot:
		if (m_connect && g_replay.m_repRecording)
		{
			SPacket_WatchInRoot	*data = (SPacket_WatchInRoot*)&m_buf;
			
			if (data->cid != CONNECTION_ID) break;

			short	maxPacketSize = 256;
			char	format = 0;
			int  targetGameCount = -1;
			if (packetSize >= SPacket_WatchInRoot::SIZE120_2)
			{
				targetGameCount = data->targetGameCount;
				format = data->format;
				maxPacketSize = data->maxPacketSize;
			}

			// Or match count is consistent?
			if (targetGameCount == -1 || targetGameCount == g_setting.totalBattle)
			{
				int idx = findFreeWatchEntry(&m_remoteAddr_recv);
#if _DEBUG
				// It does not accept the watchin
				if (g_ignoreWatchIn) idx = -1;
#endif
				if (idx != -1)
				{
					/* And stores the address of the spectators here */
					ENTERCS(&m_csWatch);
					m_watcher[idx].m_remoteAddr = m_remoteAddr_recv;
					m_watcher[idx].m_sendSize = data->dataOffset;
					m_watcher[idx].m_childCount = 0;
					m_watcher[idx].m_sendTime = 0xffffffff;
					m_watcher[idx].m_supportedFormat = format;
					m_watcher[idx].m_compOffset = 0;
					m_watcher[idx].m_compSize = 0;
					m_watcher[idx].m_compSendSize = 0;
					m_watcher[idx].m_maxPacketSize = maxPacketSize;
					LEAVECS(&m_csWatch);

					DBGOUT_NET("watchinroot %d.%d.%d.%d:%d = %s\n",
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b1,
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b2,
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b3,
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b4,
						ntohs(m_remoteAddr_recv.sin_port),
						format == 0 ? "rawdata only" : "compression ok");
					send_watchInRootReply(1);
				}
				else
				{
					// There is no free
					send_watchInRootReply(0);
				}
			}
		}
		break;
	case Packet_WatchInRootReply:
		if (m_waitingWatchInRootReply == EWIRReply_Wait)
		{
			SPacket_WatchInRootReply *data = (SPacket_WatchInRootReply*)&m_buf;

			if (data->cid != CONNECTION_ID) break;
			
			if (data->accept)
			{
				// Format1
				m_watch = true;
				m_1stCaster = false;
				m_lastWatchDataTime = timeGetTime();

				// Save the distribution source of the name, the game ID, an address
				__strncpy(m_watchRootName[0], data->extra.format1.myName, 29);
				memcpy(&m_watchRootAddr[0], &m_remoteAddr_recv, sizeof(m_remoteAddr_recv));
				m_watchRootGameCount[0] = data->extra.format1.myGameCount;
				__strncpy(m_watchRootName[1], data->extra.format1.enemyName, 29);
				memcpy(&m_watchRootAddr[1], &data->enemyAddr, sizeof(data->enemyAddr));
				m_watchRootGameCount[1] = data->extra.format1.enemyGameCount;

				// Save the parent of the address to communicate directly
				m_watchParentAddr = m_remoteAddr_recv;

				/* Transmission completion (connectable) */
				m_waitingWatchInRootReply = EWIRReply_Success;
				DBGOUT_NET("recv Packet_WatchInRootReply(success)\n");
			}
			else
			{
				// Format2
				// If you can not watch, to get the only address.
				// Since the Root name is only being used to watching destination collation for carrying out the secondary delivery, it does not mean as long as you do not want to watch
				memcpy(&m_watchRootAddr[0], &m_remoteAddr_recv, sizeof(m_remoteAddr_recv));
				memcpy(&m_watchRootAddr[1], &data->enemyAddr, sizeof(data->enemyAddr));

				// 1.20-2
				if (packetSize >= SPacket_WatchInRootReply::SIZE120_2_FMT2)
				{
					m_watchRootGameCount[0] = data->extra.format2.myGameCount;
					m_watchRootGameCount[1] = data->extra.format2.enemyGameCount;
				}
				/* Transmission completion(Not connected) */
				m_waitingWatchInRootReply = EWIRReply_Fail;
				DBGOUT_NET("recv Packet_WatchInRootReply(failed)\n");
			}
		}
		break;
	case Packet_WatchIn:
#if _DEBUG
		// It does not accept the watchin
		if (g_ignoreWatchIn) break;
#endif
		if (m_watch)
		{
			SPacket_WatchIn	*data = (SPacket_WatchIn*)&m_buf;

			if (data->cid != CONNECTION_ID) break;
			
			short maxPacketSize = 256;
			char  format = 0;
			if (packetSize >= SPacket_WatchIn::SIZE120_2)
			{
				format = data->format;
				maxPacketSize = data->maxPacketSize;
			}

			for (int i = 0; i < 2; i++)
			{
				// Or game count and IP are the same?
				if (data->targetGameCount != m_watchRootGameCount[i]) continue;
				if (memcmp(&data->targetIP, &m_watchRootAddr[i].sin_addr, sizeof(in_addr)) != 0) continue;
				
				int idx;
				char addrstr[32];
				idx = g_nodeMgr->findNodeIdx_address(g_netMgr->getStringFromAddr(&m_watchRootAddr[i], addrstr));
				if (idx < 0) continue;

				// Whether the name is consistent?
				if (strncmp(data->targetName, g_nodeMgr->getNode(idx)->m_name, 29) != 0) continue;
				
				idx = findFreeWatchEntry(&m_remoteAddr_recv);
				if (idx != -1)
				{
					/* And stores the address of the spectators here */
					ENTERCS(&m_csWatch);
					m_watcher[idx].m_remoteAddr = m_remoteAddr_recv;
					m_watcher[idx].m_sendSize = data->dataOffset;
					m_watcher[idx].m_childCount = 0;
					m_watcher[idx].m_sendTime = 0xffffffff;
					m_watcher[idx].m_supportedFormat = format;
					m_watcher[idx].m_compOffset = 0;
					m_watcher[idx].m_compSize = 0;
					m_watcher[idx].m_compSendSize = 0;
					m_watcher[idx].m_maxPacketSize = maxPacketSize;
					LEAVECS(&m_csWatch);
					DBGOUT_NET("watchin %d.%d.%d.%d:%d = %s\n",
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b1,
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b2,
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b3,
						m_remoteAddr_recv.sin_addr.S_un.S_un_b.s_b4,
						ntohs(m_remoteAddr_recv.sin_port),
						format == 0 ? "rawdata only" : "compression ok");
					send_watchInReply();
					break;
				}
			}
		}
		break;
	case Packet_WatchInReply:
		if (m_waitingWatchInReply)
		{
			SPacket_WatchInReply *data = (SPacket_WatchInReply*)&m_buf;

			if (data->cid != CONNECTION_ID) break;
			
			m_watch = true;
			m_1stCaster = false;
			m_lastWatchDataTime = timeGetTime();
			// You receive a distribution source name
			__strncpy(m_watchRootName[0], data->rootName[0], 29);
			__strncpy(m_watchRootName[1], data->rootName[1], 29);
			// You receive a delivery source address
			m_watchRootAddr[0] = data->rootIP[0];
			m_watchRootAddr[1] = data->rootIP[1];
			m_watchRootGameCount[0] = data->rootGameCount[0];
			m_watchRootGameCount[1] = data->rootGameCount[1];

			// Save the parent of the address to communicate directly
			m_watchParentAddr = m_remoteAddr_recv;

			/* Transmission completion */
			m_waitingWatchInReply = false;
		}
		break;
	case Packet_WatchData:
		if (m_watch)
		{
			// I am not receiving communication from other than the node that is recognized as the parent
			if (m_watchParentAddr.sin_addr.S_un.S_addr != m_remoteAddr_recv.sin_addr.S_un.S_addr ||
				m_watchParentAddr.sin_port != m_remoteAddr_recv.sin_port)
			{
				break;
			}

			ENTERCS(&m_csWatch);

			SPacket_WatchData *data = (SPacket_WatchData*)&m_buf;
			
			// It does receive acceptance and reply if Received size and offset coincides
			// The also said next incoming request and therefore sends, including receiving sizes reply
			// Play started After received data header minute + margin buried
			if (data->offset == m_watchRecvSize && data->offset + data->size <= sizeof(ReplayFile))
			{
				// You receive a total spectator
				m_totalGalleryCount = data->galleryCount;
				//DBGOUT_LOG("xxx recv Packet_WatchData %d\n", data->galleryCount);

				if (data->size > 0)
				{
					m_watchRecvSize += data->size;
					memcpy((char*)&g_replay.m_data + data->offset, data->data, data->size);

					if (m_watchRecvSize > REPLAY_HEADER_SIZE)
					{
						int frameCount = (g_netMgr->m_watchRecvSize - REPLAY_HEADER_SIZE) / sizeof(DWORD);
						if (g_replay.m_data.inputData[frameCount - 1] == INPUT_SYNCERROR ||
							g_replay.m_data.inputData[frameCount - 1] == INPUT_DISCONNECT ||
							g_replay.m_data.inputData[frameCount - 1] == INPUT_COMPLETE)
						{
							// Watching request is not issued because the received completion
							m_watchRecvComplete = true;
						}
					}
					WRITE_REPLAY_RAWDATA(g_netMgr->m_watchRecvSize);
				}
				DBGOUT_NET("recv watchData %d(%d)\n", data->offset, data->size);

				// Finally, record the time it receives the data
				// Is not updated if the offset is also not coincide
				m_lastWatchDataTime = timeGetTime();
			}
			else DBGOUT_NET("watchData error (need:%d recv:%d)\n", m_watchRecvSize, data->offset);

			// Since watchDataReply might have to be lost, it will reply regardless of the match offset.
			if (m_watchRecvComplete == false)
			{
				send_watchDataReply(g_netMgr->m_watchRecvSize);
			}

			LEAVECS(&m_csWatch);
		}
		break;
	case Packet_CompWatchData:
		if (m_watch)
		{
			// I am not receiving communication from other than the node that is recognized as the parent
			if (m_watchParentAddr.sin_addr.S_un.S_addr != m_remoteAddr_recv.sin_addr.S_un.S_addr ||
				m_watchParentAddr.sin_port != m_remoteAddr_recv.sin_port)
			{
				break;
			}

			ENTERCS(&m_csWatch);

			SPacket_CompWatchData *data = (SPacket_CompWatchData*)&m_buf;
			
			// It does receive acceptance and reply if Received size and offset coincides
			// The also said next incoming request and therefore sends, including receiving sizes reply
			// Play started After received data header minute + margin buried
			if (data->compblock_offset == m_watchRecvCompSize && data->compblock_offset + data->compblock_size <= data->compall_size)
			{
				// You receive a total spectator
				m_totalGalleryCount = data->galleryCount;
				//DBGOUT_LOG("xxx recv Packet_WatchData %d\n", data->galleryCount);

				if (data->compblock_size > 0)
				{
					m_watchRecvCompSize += data->compblock_size;
					memcpy(m_watchRecvCompData + data->compblock_offset, data->data, data->compblock_size);

					// Expand the compressed data of current Received
					int actualsize = zmread(m_watchRecvCompData, m_watchRecvCompSize, (char*)&g_replay.m_data + data->compall_offset, sizeof(ReplayFile) - m_watchRecvSize);
					DBGOUT_NET("comp=%d raw=%d rawoffset=%d last4=%08x\n", m_watchRecvCompSize, actualsize, data->compall_offset, *(DWORD*)(m_watchRecvCompData + m_watchRecvCompSize - 4));

					// Input data (1F per 4 bytes) is truncated extra data so as not to odd update already-received raw data
					int inputframe = ((int)data->compall_offset + actualsize - (int)REPLAY_HEADER_SIZE) / (int)sizeof(DWORD);
					if (inputframe > 0)
					{
						m_watchRecvSize = REPLAY_HEADER_SIZE + inputframe * sizeof(DWORD);
					}
					else
					{
						m_watchRecvSize = data->compall_offset + actualsize;
					}

					if (m_watchRecvSize > REPLAY_HEADER_SIZE)
					{
						int frameCount = (g_netMgr->m_watchRecvSize - REPLAY_HEADER_SIZE) / sizeof(DWORD);
						if (g_replay.m_data.inputData[frameCount - 1] == INPUT_SYNCERROR ||
							g_replay.m_data.inputData[frameCount - 1] == INPUT_DISCONNECT ||
							g_replay.m_data.inputData[frameCount - 1] == INPUT_COMPLETE)
						{
							// Watching request is not issued because the received completion
							m_watchRecvComplete = true;
						}
					}
					WRITE_REPLAY_RAWDATA(g_netMgr->m_watchRecvSize);
				}

				// All of the compressed data I received
				if (m_watchRecvCompSize == data->compall_size) DBGOUT_NET("compdata complete!!\n");

				DBGOUT_NET("recv compWatchData %d(%d) of %d\n", data->compblock_offset, data->compblock_size, data->compall_size);
			
				// Finally, record the time it receives the data
				// Is not updated if the offset is also not coincide
				m_lastWatchDataTime = timeGetTime();
			}
			else DBGOUT_NET("compWatchData error (need:%d recv:%d) of %d\n", m_watchRecvCompSize, data->compblock_offset, data->compall_size);

			// Since compWatchDataReply might have to be lost, it will reply regardless of the match offset.
			if (m_watchRecvComplete == false)
			{
				send_compWatchDataReply(m_watchRecvCompSize, m_watchRecvSize);
			}

			LEAVECS(&m_csWatch);
		}
		break;
	case Packet_WatchDataReply:
		if (m_connect || m_watch)
		{
			ENTERCS(&m_csWatch);

			SPacket_WatchDataReply *data = (SPacket_WatchDataReply*)&m_buf;

			for (int i = 0; i < WATCH_MAX_CHILD; i++)
			{
				if (m_watcher[i].m_remoteAddr.sin_addr.S_un.S_addr == m_remoteAddr_recv.sin_addr.S_un.S_addr &&
					m_watcher[i].m_remoteAddr.sin_port == m_remoteAddr_recv.sin_port)
				{
					// You receive a child spectator
					m_watcher[i].m_childCount = data->childCount;

					// Update If you have increased Sent data, and the offset of the next transmission data
					if (data->size > m_watcher[i].m_sendSize) m_watcher[i].m_sendSize = data->size;
					m_watcher[i].m_sendTime = 0xffffffff;
					break;
				}
			}
			LEAVECS(&m_csWatch);
		}
		break;
	case Packet_CompWatchDataReply:
		if (m_connect || m_watch)
		{
			ENTERCS(&m_csWatch);

			SPacket_CompWatchDataReply *data = (SPacket_CompWatchDataReply*)&m_buf;

			for (int i = 0; i < WATCH_MAX_CHILD; i++)
			{
				if (m_watcher[i].m_remoteAddr.sin_addr.S_un.S_addr == m_remoteAddr_recv.sin_addr.S_un.S_addr &&
					m_watcher[i].m_remoteAddr.sin_port == m_remoteAddr_recv.sin_port)
				{
					// You receive a child spectator
					m_watcher[i].m_childCount = data->childCount;
					
					if (data->compsize > m_watcher[i].m_compSendSize)
					{
						m_watcher[i].m_compSendSize = data->compsize;
						if (m_watcher[i].m_compSendSize == m_watcher[i].m_compSize)
						{
							// Reception of the compressed data is completed
							DBGOUT_NET("compWatchData complete %d bytes\n", m_watcher[i].m_compSize);
							m_watcher[i].m_compOffset = 0;
							m_watcher[i].m_compSendSize = 0;
							m_watcher[i].m_compSize = 0;
						}
					}
					if (data->rawsize > m_watcher[i].m_sendSize) m_watcher[i].m_sendSize = data->rawsize;
					m_watcher[i].m_sendTime = 0xffffffff;
					break;
				}
			}
			LEAVECS(&m_csWatch);
		}
		break;
	case Packet_NodeAddr115_2:
		//{
		//	SPacket_NodeAddr115_2 *data = (SPacket_NodeAddr115_2*)&m_buf;
		//	ENTERCS(&g_netMgr->m_csNode);
		//	g_nodeMgr->addNode(data->addr, data->name, false, false);
		//	LEAVECS(&g_netMgr->m_csNode);
		//	DBGOUT_NET("Packet_NodeAddr115_2 %s %s\n", data->name, data->addr);
		//}
		break;
	case Packet_NodeAddr:
		{
			SPacket_NodeAddr *data = (SPacket_NodeAddr*)&m_buf;
			
			if (data->scriptCode == g_scriptCode)
			{
				ENTERCS(&g_netMgr->m_csNode);
				g_nodeMgr->addNode(data->addr, data->name, false, false);
				LEAVECS(&g_netMgr->m_csNode);

				//DBGOUT_NET("Packet_NodeAddr %s %s\n", data->name, data->addr);
			}
		}
		break;
	case Packet_GalleryCount:
		{
			SPacket_GalleryCount *data = (SPacket_GalleryCount*)&m_buf;
			m_recvGalleryCount  = data->galleryCount;
		}
		break;
	case Packet_GalleryCountForLobby:
		{
			SPacket_GalleryCountForLobby *data = (SPacket_GalleryCountForLobby*)&m_buf;

			if (packetSize >= SPacket_GalleryCountForLobby::SIZE120_2)
			{
				// Ignored because information of the old Ver might gallery number of the previous game
				ENTERCS(&g_netMgr->m_csNode);

				int idx1 = g_nodeMgr->findNodeIdx_name_ip(data->name[0], data->ip[0]);
				if (idx1 >= 0 && g_nodeMgr->getNode(idx1)->m_gamecount == data->gameCount[0])
				{
					g_nodeMgr->getNode(idx1)->m_galleryCount = data->galleryCount;
					g_nodeMgr->getNode(idx1)->m_galleryUpdateTime = timeGetTime();
				}
				int idx2 = g_nodeMgr->findNodeIdx_name_ip(data->name[1], data->ip[1]);
				if (idx2 >= 0 && g_nodeMgr->getNode(idx2)->m_gamecount == data->gameCount[1])
				{
					g_nodeMgr->getNode(idx2)->m_galleryCount = data->galleryCount;
					g_nodeMgr->getNode(idx2)->m_galleryUpdateTime = timeGetTime();
				}

				LEAVECS(&g_netMgr->m_csNode);
			}
		}
		break;
	case Packet_NameRequest:
		{
			ENTERCS(&g_netMgr->m_csNode);

			char addrstr[32];
			int idx = g_nodeMgr->findNodeIdx_address(g_netMgr->getStringFromAddr(&m_remoteAddr_recv, addrstr));
			if (idx >= 0) send_ping(&m_remoteAddr_recv, idx);

			LEAVECS(&g_netMgr->m_csNode);
		}
		break;
	case Packet_DebugInfo:
#if _DEBUG
		{
			SPacket_DebugInfo *data = (SPacket_DebugInfo*)&m_buf;
			data->cpu_name[49] = '\0';

			DBGOUT_NET("======= Sync Error Debug Info=======\n");
			DBGOUT_NET("name=%s eax=%p ecx=%p edx=%p\n", data->cpu_name, data->cpu_eax, data->cpu_ecx, data->cpu_edx);
			DBGOUT_NET("fcw=%04x analog=%d-%d ggmode=%d\n", data->fcw, data->analog[0], data->analog[1], data->ggmode);
			DBGOUT_NET("========================\n");
		}
#endif
		break;
	case Packet_BattleInfoRequest:
		{
			SPacket_BattleInfoRequest *data = (SPacket_BattleInfoRequest*)&m_buf;

			char  name[2][30];
			DWORD ip[2];
			int   gamecount[2];
			char  chara[2];
			if (m_watch && g_replay.m_playing)
			{
				// After sending the requested node does not match the one in the spectator
				bool hit = false;
				hit |= data->targetGameCount == m_watchRootGameCount[0] &&
					strcmp(data->targetName, m_watchRootName[0]) == 0 &&
					data->targetIP.S_un.S_addr == m_watchRootAddr[0].sin_addr.S_un.S_addr;

				hit |= data->targetGameCount == m_watchRootGameCount[1] &&
					strcmp(data->targetName, m_watchRootName[1]) == 0 &&
					data->targetIP.S_un.S_addr == m_watchRootAddr[1].sin_addr.S_un.S_addr;

				if (!hit) break;
				
				__strncpy(name[0], g_replay.m_data.name1P, 29);
				__strncpy(name[1], g_replay.m_data.name2P, 29);
				chara[0] = (char)g_replay.m_data.chara1P;
				chara[1] = (char)g_replay.m_data.chara2P;

				if (strcmp(g_replay.m_data.name1P, m_watchRootName[0]) == 0)
				{
					ip[0] = m_watchRootAddr[0].sin_addr.S_un.S_addr;
					ip[1] = m_watchRootAddr[1].sin_addr.S_un.S_addr;
					gamecount[0] = m_watchRootGameCount[0];
					gamecount[1] = m_watchRootGameCount[1];
				}
				else
				{
					ip[0] = m_watchRootAddr[1].sin_addr.S_un.S_addr;
					ip[1] = m_watchRootAddr[0].sin_addr.S_un.S_addr;
					gamecount[0] = m_watchRootGameCount[1];
					gamecount[1] = m_watchRootGameCount[0];
				}
				DBGOUT_NET("Battle Info Request Watch\n");
			}
			else if (m_connect && g_replay.m_repRecording)
			{
				// Unconditionally send When direct request comes
				__strncpy(name[0], g_replay.m_data.name1P, 29);
				__strncpy(name[1], g_replay.m_data.name2P, 29);
				chara[0] = (char)g_replay.m_data.chara1P;
				chara[1] = (char)g_replay.m_data.chara2P;

				sockaddr_in myaddr = getAddrFromString(g_nodeMgr->getOwnNode());
				if (g_netMgr->m_playSide == 1)
				{
					ip[0] = myaddr.sin_addr.S_un.S_addr;
					ip[1] = m_remoteAddr_active.sin_addr.S_un.S_addr;
					gamecount[0] = g_setting.totalBattle;
					gamecount[1] = g_enemyInfo.m_gameCount;
				}
				else
				{
					ip[0] = m_remoteAddr_active.sin_addr.S_un.S_addr;
					ip[1] = myaddr.sin_addr.S_un.S_addr;
					gamecount[0] = g_enemyInfo.m_gameCount;
					gamecount[1] = g_setting.totalBattle;
				}
				DBGOUT_NET("Battle Info Request Busy\n");
			}
			else if (!m_connect && !m_watch)
			{
				// Send if there is information that matches the node list
				CNode* node = NULL;
				bool hit = false;
				for (int i = 0; i < g_nodeMgr->getNodeCount(); i++)
				{
					node = g_nodeMgr->getNode(i);

					hit |= data->targetGameCount == node->m_battleInfoGameCount[0] &&
						strcmp(data->targetName, node->m_battleInfoName[0]) == 0 &&
						data->targetIP.S_un.S_addr == node->m_battleInfoIP[0];

					hit |= data->targetGameCount == node->m_battleInfoGameCount[1] &&
						strcmp(data->targetName, node->m_battleInfoName[1]) == 0 &&
						data->targetIP.S_un.S_addr == node->m_battleInfoIP[1];
					if (hit) break;
				}
				if (!hit) break;
				__strncpy(name[0], node->m_battleInfoName[0], 29);
				__strncpy(name[1], node->m_battleInfoName[1], 29);
				chara[0] = node->m_battleInfoChara[0];
				chara[1] = node->m_battleInfoChara[1];
				ip[0] = node->m_battleInfoIP[0];
				ip[1] = node->m_battleInfoIP[1];
				gamecount[0] = node->m_battleInfoGameCount[0];
				gamecount[1] = node->m_battleInfoGameCount[1];

				DBGOUT_NET("Battle Info Request Idle\n");
			}
			send_battleInfo(name[0], name[1], ip[0], ip[1], gamecount[0], gamecount[1], chara[0], chara[1]);
		}
		break;
	case Packet_BattleInfo:
		if (!m_connect && !m_watch && m_waitingBattleInfoRequestReply)
		{
			SPacket_BattleInfo *data = (SPacket_BattleInfo*)&m_buf;
			
			if (data->chara[0]-1 >= 0 && data->chara[0]-1 < CHARACOUNT &&
				data->chara[1]-1 >= 0 && data->chara[1]-1 < CHARACOUNT)
			{
				ENTERCS(&g_netMgr->m_csNode);

				for (int i = 0; i < 2; i++)
				{
					int idx = g_nodeMgr->findNodeIdx_name_ip(data->name[i], data->ip[i].S_un.S_addr);
					if (idx >= 0)
					{
						CNode* node = g_nodeMgr->getNode(idx);
						__strncpy(node->m_battleInfoName[0], data->name[0], 29);
						__strncpy(node->m_battleInfoName[1], data->name[1], 29);
						node->m_battleInfoIP[0] = data->ip[0].S_un.S_addr;
						node->m_battleInfoIP[1] = data->ip[1].S_un.S_addr;
						node->m_battleInfoChara[0] = data->chara[0];
						node->m_battleInfoChara[1] = data->chara[1];
						node->m_battleInfoGameCount[0] = data->gamecount[0];
						node->m_battleInfoGameCount[1] = data->gamecount[1];
					}
				}
				LEAVECS(&g_netMgr->m_csNode);
			}

			/* Transmission completion */
			m_waitingBattleInfoRequestReply = false;
		}
		break;
	}

	return true;
}

int CNetMgr::udpsend(sockaddr_in* p_addr, char* p_data, int p_dataSize)
{
	return sendto(m_udpSocket, p_data, p_dataSize, 0, (sockaddr*)p_addr, sizeof(sockaddr_in));
}

int CNetMgr::udprecv(char* p_buf, int p_bufSize)
{
	int	raLen = sizeof(m_remoteAddr_recv);
	return recvfrom(m_udpSocket, p_buf, p_bufSize, 0, (sockaddr*)&m_remoteAddr_recv, &raLen);
}

#if USE_TCP
int CNetMgr::tcpsend(char* p_data, int p_dataSize, int p_timeout)
{
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	{
		WSERR("socket");
		return 0;
	}

	int stTime = timeGetTime();
	int sendsize = 0;
	while ((int)timeGetTime() - stTime < p_timeout)
	{
		if (::connect(sock, (sockaddr*)&m_remoteAddr_active, sizeof(m_remoteAddr_active)) != SOCKET_ERROR)
		{
			sendsize = send(sock, p_data, p_dataSize, 0);
			if (sendsize == -1) WSERR("SEND");
			break;
		}
	}
	shutdown(sock, SD_BOTH);
	closesocket(sock);

	return sendsize;
}

int CNetMgr::tcprecv(char* p_buf, int p_bufSize, int p_timeout)
{
	timeval tv = { 0, p_timeout * 1000 };
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(m_tcpSocket, &fds);
	if (select(0, &fds, NULL, NULL, &tv) == 0)
	{
		WSERR("select(timeout)");
		return 0;
	}

	int readSize = 0;
	sockaddr_in addr2;
	int addrlen = sizeof(addr2);
	SOCKET sock = accept(m_tcpSocket, (sockaddr*)&addr2, &addrlen);
	if (sock == INVALID_SOCKET)
	{
		WSERR("accept");
		return 0;
	}
	for (int i = 0;; i++)
	{
		int size = recv(sock, &p_buf[readSize], p_bufSize - readSize, 0);
		if (size == -1) { WSERR("recv"); continue; }
		if (size == 0) break;
		readSize += size;
	}
	shutdown(sock, SD_BOTH);
	closesocket(sock);
	
	return readSize;
}
#endif
