#pragma once

//******************************************************************
// libs
//******************************************************************
#pragma comment(lib, "ws2_32.lib")

//******************************************************************
// includes
//******************************************************************
#include <winsock2.h>
#include <ws2tcpip.h>
#include "resource.h"

//******************************************************************
// defines
//******************************************************************
#define USE_TCP				FALSE

/* ggxxnet connection ID ~ matched it can communicate even in Ver difference if */
/* ver1.11 later, it has fixed in all 12 ver1.12 system as an exception */
#define CONNECTION_ID		12

#define	LOBBY_VER			6		/* Lobby version */
#define REPLAY_VER			3		/* Replay version */
// 3 = 1.10-
// 2 = 1.09
// 1 = 1.06-1.08b
// 0 = 1.00-1.05
#define	PACKETMAX_SIZE		1024
#define LOG_SIZE			(1024*1024)	// 1MB

#define MAX_COMPWATCHDATASIZE	30000	// Margin as long 30kb the compression of already

#define WATCH_MAX_CHILD_BASE	3
#define WATCH_MAX_CHILD_INC		2
#define WATCH_MAX_CHILD			(WATCH_MAX_CHILD_BASE + WATCH_MAX_CHILD_INC)

#define MSPF_FLOAT	16.66666667f
#define MSPF_INT	17

/* Each timeout period (in seconds) */
#define TIMEOUT_VSLOAD	5000	/* Waiting time from after the end of the VS screen */
#define TIMEOUT_SUSPEND	5000	/* Connection ~ CS start, it may be a longer because it is Suspend timeout abnormal case until CS end ~ VS start */
#define TIMEOUT_KEY		1000	/* Key input */
#define TIMEOUT_KEY2	3000	/* Key input (freeze protection) */
#define TIMEOUT_REPLY	3000	/* Suspend, Resume, Connect, Data Reply */
#define TIMEOUT_PING	500		/* ping wait */
#define TIMEOUT_BLOCK	3000	/* Data block transfer */

#define TIMEOUT_WATCHDATA		1000	/* Watching the data does not come forever */
#define TIMEOUT_WATCHDATAREPLY	1000	/* Also it does not come watch data reply forever */

#define TIMEOUT_WATCHDATAWAIT	4000	/* Stop time limit by watching data wait */

#define WATCH_RESUME_INTERVAL	2000	/* Reclaim interval when the spectator connection broken */

const sockaddr_in NULL_ADDR = { 0, 0,						// family, port
								0, 0, 0, 0,					// addr
								0, 0, 0, 0, 0, 0, 0, 0 };	// zero

enum EPacketDataType
{
	Packet_Connect = 0,		/* Connection request */
	Packet_ConnectReply,
	Packet_Ping,
	Packet_PingReply112_3,
	Packet_PingReplyLite120,

	Packet_Key,
	Packet_VSLoadCompleted,
	Packet_Suspend,
	Packet_SuspendReply,
	Packet_Resume,
	Packet_ResumeReply,

	Packet_Data,
	Packet_DataReply,

	Packet_WatchIn,			/* Watching request */
	Packet_WatchInReply,
	Packet_WatchInRoot,		/* Direct watch request to the delivery source */
	Packet_WatchInRootReply,
	Packet_WatchData,		/* Fragmentary Ripudeta for the spectator. If retransmission is not death but that was lost, it is plus to the next transmission minute */
							/* It is assumed that you cut if too reply does not come, it does not thereafter transfer */
	Packet_WatchDataReply,	/* Reply spectator data. Retransmission will not be */
	Packet_PingReply,
	Packet_NodeAddr115_2,	/* Reuse prohibited because it still used in some versions! ! */
	Packet_NodeAddr,

	Packet_GalleryCount,		/* It will send the number of spectators at the opponents each other */
	Packet_GalleryCountForLobby,/* It provides information spectator to the node that are in the lobby */
	Packet_NameRequest,			/* It issued against UNKNOWN_NAME. To request a ping message to get a name */
	Packet_DebugInfo,			/* For investigation of SyncError */

	Packet_PingReply_BusyCasting,	/* Lighter pingrep from 1.20-2 */
	Packet_PingReply_Busy,			/* Lighter pingrep from 1.20-2 */
	Packet_PingReply_Watch,			/* Lighter pingrep from 1.20-2 */
	Packet_PingReply_Idle,			/* Lighter pingrep from 1.20-2 */
	Packet_Comment,					/* The separation of the comments from PingReply from 1.20-2 */

	Packet_CompWatchData,		/* Compression Watching data */
	Packet_CompWatchDataReply,	/* Reply compression spectator data */

	Packet_BattleInfoRequest,	/* Requests for information during the competition */
	Packet_BattleInfo,			/* Information in the competition */
};

enum EBlockDataType
{
	Block_RandomTable = 0,
	Block_PlayerInfo,
	Block_KeySetting,
	Block_Palette,

	Block_NetLog = 124,
	Block_KeyLog = 125,
	Block_RndLog = 126,

	Block_TestData = 127,
};

enum EStateType
{
	State_Idle = 0,
	State_Busy,				// During the competition
	State_NoResponse,
	State_Mismatch,
	State_VersionError,
	State_NotReady,
	State_Unknown,
	State_PingOver,			// To the PingOver if Ping reference more than in watching
	
	State_Watch,			// Watching in
	State_Watch_Playable,	// It's in watching to respond to, but competition request
	State_Busy_Casting,		// You can watch in the competition
	State_Busy_Casting_NG,	// It's possible spectator in the match, but still in Kyarasere
};

enum EValidFlag{
	VF_RANK		= 0x00000001,
	VF_WINS		= 0x00000002,
	VF_EX		= 0x00000004,
	VF_ROUND	= 0x00000008,
	VF_COUNT	= 0x00000010,
	VF_VERSION	= 0x00000020,
	VF_COMMENT	= 0x00000040,
	VF_CAST		= 0x00000080,
	VF_ID		= 0x00000100,
	VF_DENY		= 0x00000200,
};

#pragma pack(push)
#pragma pack(1)

typedef struct
{
	char	packetType;
	char	data[PACKETMAX_SIZE-1];
}SPacket_Unknown;

enum { BF_IDLE=0, BF_BUSY=1, BF_BUSY_CAST_OK=2, BF_BUSY_CAST_NG=3 };
enum { WF_WATCH=0x80, WF_INTRUSION=0x40 };

typedef struct
{
	enum { SIZE115=66, SIZE120=72, };
	enum {
		VF115 = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_COUNT | VF_VERSION | VF_CAST, // Fixed a Because -16 VF_CAST also remains valid
		VF120 = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_COUNT | VF_VERSION | VF_CAST | VF_ID,
	};

	char	packetType;
	char	cid;

	DWORD	scriptCode;
	char	name[30];
	char	ver[10];
	char	mac[6];

	char	delay;
	char	ex;
	short	wins;
	char	rank;
	char	notready;
	char	ignoreSlow;
	char	round;
	bool	deny;
	bool	needDetail;
	int		gamecount;
	// 1.20
	char	hdid[4];		// mac Continued
	BYTE	watchFlags;		// 7bit : watch
							// 6bit : allow intrusion (Setting)
	char	watchMaxNode;
}SPacket_Ping;

typedef struct
{
	enum { VF = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_COUNT | VF_COMMENT | VF_CAST }; // Fixed a Because -16 VF_CAST also remains valid

	// Until 1.12-3 use here
	char	packetType;
	char	cid;

	char	msg[256];

	char	delay;
	char	ex;
	short	wins;
	char	rank;
	char	notready;
	char	ignoreSlow;
	char	round;
	bool	deny;
	int		gamecount;
}SPacket_PingReply112_3;

typedef struct
{
	enum { SIZE115=281, SIZE120=285, };
	enum {
		VF115 = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_COMMENT | VF_ID,
		VF120 = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_COUNT | VF_COMMENT | VF_ID,
	};

	char	packetType;
	char	cid;

	// HD also of serial be added in order to prevent duplication of MAC address than ver1.13
	// For taking compatibility with previous versions, and sends the code 10byte minute on of examining the counterpart version
	// Comparison of code to match the more low (HD serial is to be a wild card if ffffffff)
	char	id[10];
	char	msg[256];

	char	delay;
	char	ex;
	short	wins;
	char	rank;
	char	notready;
	char	ignoreSlow;
	char	round;
	bool	deny;
	// 1.20
	int		gamecount;
}SPacket_PingReply;

/* To reduce the network load, use here when SPacket_Ping :: needDetail is off */
/* We are sending the information of one street when anyway to send ping yourself */
typedef struct
{
	enum { SIZE115=3, SIZE120=98, };
	enum {
		VF115 = 0x00000000,
		VF120 = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_VERSION | VF_COUNT | VF_ID,
	};

	char	packetType;
	char	cid;
	char	busy;
	// 1.20
	char	name[2][30];	// Information of opponent
	char	chara[2];		// Information in the fighting characters

	char	id[10];
	char	ver[10];

	char	delay;
	char	ex;
	short	wins;
	char	rank;
	char	round;
	bool	deny;
	int		gamecount;	// Use the game identification for the spectator

	BYTE	watchFlags;	// 7bit : watch
						// 6bit : allow intrusion (Setting)
	char	watchMaxNode;
}SPacket_PingReplyLite120;

// It returns a different message for each situation from 120-2
typedef struct
{
	enum { VF = VF_RANK | VF_WINS | VF_COUNT };

	char	packetType;
	char	cid;
	bool	casting;
	short	wins;
	char	rank;
	int		gamecount;		// Use the game identification for the spectator
}SPacket_PingReply_BusyCasting;

typedef struct
{
	enum { VF = 0x00000000 };

	char	packetType;
	char	cid;
}SPacket_PingReply_Busy;

typedef struct
{
	enum { VF = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_VERSION | VF_COUNT | VF_CAST | VF_ID };

	char	packetType;
	char	cid;

	char	id[10];
	char	ver[10];

	char	delay;
	char	ex;
	short	wins;
	char	rank;
	char	round;
	char	ignoreSlow;
	bool	deny;
	int		gamecount;		// Use the game identification for the spectator

	bool	allowIntrusion;
	char	watchMaxNode;
}SPacket_PingReply_Watch;

typedef struct
{
	enum { VF = VF_COMMENT };

	// When the ping of needDetail = true,
	// It will send at the same timing as pingrep
	char	packetType;
	BYTE	size;
	char	msg[256];
}SPacket_Comment;

typedef struct
{
	enum { VF = VF_DENY | VF_RANK | VF_WINS | VF_EX | VF_ROUND | VF_VERSION | VF_COUNT | VF_CAST | VF_ID };

	char	packetType;
	char	cid;
	bool	notready;

	char	ver[10];
	char	id[10];

	char	delay;
	char	ex;
	short	wins;
	char	rank;
	char	round;
	char	ignoreSlow;
	bool	deny;
	int		gamecount;	// Use the game identification for the spectator
	
	bool	allowIntrusion;
	char	watchMaxNode;
}SPacket_PingReply_Idle;

typedef struct
{
	char	packetType;
	DWORD	time;
	struct
	{
		WORD	key;
		BYTE	syncChk;
	}cell[32];
}SPacket_Key;

typedef struct
{
	enum { SIZE120=2, SIZE120_2=4, };

	char	packetType;
	char	cid;
	// 1.20-2
	short	maxPacketSize;	// The maximum packet size in block transfer
}SPacket_Connect, SPacket_ConnectReply;

typedef struct
{
	char	packetType;
	char	cid;
}SPacket_VSLoadCompleted, SPacket_Suspend, SPacket_SuspendReply, SPacket_Resume, SPacket_ResumeReply, SPacket_NameRequest;

typedef struct
{
	enum { PACKET_HEADER_SIZE = 12, };

	char	packetType;
	char	type;
	int		seq;
	WORD	dataSize;
	DWORD	dataOffset;
	char	data[PACKETMAX_SIZE - PACKET_HEADER_SIZE];
}SPacket_Data;

typedef struct
{
	char	packetType;
	DWORD	seq;
}SPacket_DataReply;

// Connect directly to the distribution source
typedef struct
{
	enum { SIZE120=6, SIZE120_2=13, };

	char	packetType;
	char	cid;
	int		dataOffset;		// First byte of requested data (also because if it is required from the middle)
	// 1.20-2
	int		targetGameCount;// Delivery game count
	char	format;			// Supported formats (0 | undefined = RawData only, 1 = supports compression)
	short	maxPacketSize;	// The maximum packet size
}SPacket_WatchInRoot;

typedef struct
{
	enum { SIZE120_FMT1=87, SIZE120_2_FMT1=87 };
	enum { SIZE120_FMT2=19, SIZE120_2_FMT2=27 };
	
	char		packetType;
	char		cid;
	char		accept;
	//sockaddr_in	myAddr;		// It is not sent because it should know your address
	sockaddr_in	enemyAddr;		// Teach an opponent address to the child

	union
	{
		struct
		{
			// accept == 1‚Ì‚Æ‚«
			char myName[30];		// Tell me your name to the child
			char enemyName[30];		// Tell me the name of the person to the child
			int  myGameCount;		// Teach my game ID to the child
			int  enemyGameCount;	// Teach an opponent game ID to the child
		}format1;
		struct
		{
			// When 1.20-2 later accept% 3D% 3D 0
			int  myGameCount;		// Teach my game ID to the child
			int  enemyGameCount;	// Teach an opponent game ID to the child
		}format2;
	}extra;
}SPacket_WatchInRootReply;

typedef struct
{
	enum { SIZE120=44, SIZE120_2=47, };

	char	packetType;
	char	cid;
	char	targetName[30];
	in_addr	targetIP;		// You want to deliver the original IP awaited route because the relay is to be able to connect even in the NAT
	int		targetGameCount;// Delivery game count
	int		dataOffset;		// First byte of requested data (also because if it is required from the middle)
	// 1.20-2
	char	format;			// Supported formats (0 | undefined = RawData only, 1 = supports compression)
	short	maxPacketSize;	// The maximum packet size
}SPacket_WatchIn;

typedef struct
{
	char		packetType;
	char		cid;
	char		rootName[2][30];	// Teach a distribution source of the name to the child
	sockaddr_in	rootIP[2];			// Teach the delivery source address
	int			rootGameCount[2];	// Delivery game count
}SPacket_WatchInReply;

typedef struct
{
	enum {
		MINBUFFERSIZE = 64,
		MAXBUFFERSIZE = 512,
		PACKET_HEADER_SIZE = 8,// sizeof(packetType) + sizeof(offset) + sizeof(size) + sizeof(galleryCount)
	};
	char	packetType;
	DWORD	offset;
	WORD	size;
	BYTE	galleryCount;			/* The notification the total number of spectators of this match from the delivery person */
	char	data[MAXBUFFERSIZE];	/* Actually send in as much necessary */
}SPacket_WatchData;

typedef struct
{
	char	packetType;
	BYTE	reserved;			/* Unused (= 0) */
	DWORD	size;
	BYTE	childCount;			/* Report your child spectator number to the parent */
}SPacket_WatchDataReply;

typedef struct
{
	enum {
		MINBUFFERSIZE = 64,
		MAXBUFFERSIZE = 512,
		PACKET_HEADER_SIZE = 12,
	};
	char	packetType;
	WORD	compblock_offset;		/* Offset of the compressed data block (start on the compressed data of this time the transferred data) */
	WORD	compblock_size;			/* The size of the compressed data block */
	DWORD	compall_offset;			/* Overall offset of the compressed data (where Did compressed from raw data) */
	WORD	compall_size;			/* Overall size of the compressed data */
	BYTE	galleryCount;			/* The notification the total number of spectators of this match from the delivery person */
	char	data[MAXBUFFERSIZE];	/* Actually send in as much necessary */
}SPacket_CompWatchData;

typedef struct
{
	char	packetType;
	BYTE	reserved;			/* Unused (= 0) */
	WORD	compsize;			/* Received compressed data size */
	DWORD	rawsize;			/* Report size that could be deployed as raw data */
	BYTE	childCount;			/* Report your child spectator number to the parent */
}SPacket_CompWatchDataReply;

//typedef struct
//{
//	char	packetType;
//	char	name[30];
//	char	addr[32];
//}SPacket_NodeAddr115_2;

// Can communicate IP: tell the Port to other nodes
typedef struct
{
	char	packetType;
	DWORD	scriptCode;
	char	name[30];
	char	addr[32];
}SPacket_NodeAddr;

// Each other node in the competition is throw the number of spectators of both
typedef struct
{
	char	packetType;
	int		galleryCount;
}SPacket_GalleryCount;

// Spectator to report the number of spectators of the competition as a reply to Ping from the lobby
typedef struct
{
	enum { SIZE120=73, SIZE120_2=81, };

	char	packetType;
	char	name[2][30];
	DWORD	ip[2];
	int		galleryCount;
	// 1.20-2
	int		gameCount[2];	// Add to for the game identification
}SPacket_GalleryCountForLobby;

typedef struct
{
	char	packetType;
	char	cpu_name[49];
	DWORD	cpu_eax;
	DWORD	cpu_edx;
	DWORD	cpu_ecx;
	WORD	fcw;
	char	analog[2];
	char	ggmode;
}SPacket_DebugInfo;

typedef struct
{
	char	packetType;
	char	targetName[30];	// Delivery's name
	in_addr	targetIP;		// Distribution source IP
	int		targetGameCount;// Delivery game count
}SPacket_BattleInfoRequest;

typedef struct
{
	char	packetType;
	char	name[2][30];
	in_addr	ip[2];
	int		gamecount[2];
	char	chara[2];
}SPacket_BattleInfo;

typedef struct
{
	char	nametrip[30];
	char	rank;
	char	round;
	WORD	wins;
	WORD	oldcs;
	char	ex;
}SBlock_PlayerInfo;

#pragma pack(pop)

class CWatcher
{
public:
	CWatcher(void) { m_compData = new char[MAX_COMPWATCHDATASIZE]; }
	~CWatcher(void) { delete[] m_compData; }

	inline bool isActive(void) { return m_remoteAddr.sin_port != 0; }

	void init(void)
	{
		m_remoteAddr = NULL_ADDR;
		m_sendSize = 0;
		m_sendTime = 0xffffffff;
		m_childCount = 0;
		m_supportedFormat = 0;
		m_compOffset = 0;
		m_compSize = 0;
		m_compSendSize = 0;
	}

public:
	sockaddr_in	m_remoteAddr;
	DWORD		m_sendSize;			// Sent data size
	int			m_childCount;		// And to report to the parent is allowed to report from the child in order to know the number of a few comprehensive spectator of child
	DWORD		m_sendTime;			// Clear Upon receiving the last time you send a watchData to watchDataReply
	char		m_supportedFormat;	// Support Format (0 = RawData only, 1 = compression support)
	short		m_maxPacketSize;	// The maximum packet size

// Support for compressed data (data size is I use the compressed data Dattara 128 or more)
	char*		m_compData;			// And it holds the compressed data until you send
	int			m_compOffset;		// Offset of the compressed data to the raw data
	int			m_compSize;			// The overall size of the compressed data
	int			m_compSendSize;		// The size of the sent compressed data
};

class CNetMgr
{
public:
	CNetMgr(void);
	~CNetMgr(void);
	
	bool init(int p_port, int p_delay, bool p_useLobby);
	void startThread(void);
	void stopThread(void);

	void connect(void);
	void disconnect(char* p_cause);
	void resume(void);
	void suspend(void);

	void setErrMsg(char* p_msg);

	char*		getStringFromAddr(sockaddr_in* p_addr, char* p_output);
	sockaddr_in getAddrFromString(char* p_str);

	bool watch(char* p_targetName, sockaddr_in* p_targetAddr, int p_targetGameCount, bool p_blockingMode);
	int  findFreeWatchEntry(sockaddr_in* p_addr);
	int  getChildWatcherCount(void);

	bool send_connect(sockaddr_in* p_addr);
	void send_connectReply(void);
	void send_key(int p_time);
	bool send_watchInRoot(sockaddr_in* p_addr, int p_targetGameCount, bool& p_success);
	void send_watchInRootReply(bool p_accept);
	bool send_watchIn(char* p_targetName, sockaddr_in* p_targetIP, int p_targetGameCount);
	void send_watchInReply(void);
	void send_watchData(int p_idx);
	void send_watchDataReply(int p_size);
	void send_compWatchDataReply(int p_compsize, int p_rawsize);
	bool send_ping(sockaddr_in* p_addr, int p_selNodeIdx);
	void send_pingReply120(bool p_needDetail, bool p_deny, bool p_underV113);
	void send_pingReply(bool p_deny);
	void send_comment(void);
	void send_vsLoadCompleted(void);
	bool send_suspend(void);
	void send_suspendReply(void);
	bool send_resume(void);
	void send_resumeReply(void);
	void send_dataReply(int p_seq);
	void send_nodeaddr115_3(sockaddr_in* p_addr, class CNode* p_node);
	void send_galleryCount(void);
	void send_galleryCountForLobby(void);
	void send_nameRequest(sockaddr_in* p_addr);
	void send_debugInfo(void);

	bool send_battleInfoRequest(char* p_targetName, sockaddr_in* p_targetIP, int p_targetGameCount);
	void send_battleInfo(char* p_name1, char* p_name2, DWORD p_ip1, DWORD p_ip2, DWORD p_gamecount1, DWORD p_gamecount2, char p_chara1, char p_chara2);

	bool sendDataBlock(char p_type, char* p_data, int p_dataSize, int p_timeout);
	bool recvDataBlock(char p_type, char* p_data, int p_dataSize, int p_timeout);

	void initWatchVars(void);

private:
	bool talking(void);

	int udpsend(sockaddr_in* p_addr, char* p_data, int p_dataSize);
	int udprecv(char* p_buf, int p_bufSize);

#if USE_TCP
	int tcpsend(char* p_data, int p_dataSize, int p_timeout);
	int tcprecv(char* p_buf, int p_bufSize, int p_timeout);
#endif

	friend DWORD WINAPI _recvThreadProc(LPVOID lpParameter);
	friend DWORD WINAPI _lobbyThreadProc(LPVOID lpParameter);

private:
	volatile bool	m_quitApp;
	volatile bool	m_recvThread_end;
	volatile bool	m_lobbyThread_end;
	HANDLE			m_recvThread;
	HANDLE			m_lobbyThread;

public:
	SPacket_Unknown	m_buf;

	sockaddr_in	m_remoteAddr_recv;		/* address of a temporary partner in recvfrom etc. */
	sockaddr_in	m_remoteAddr_active;	/* fixed partner of the address after the connection establishment */
										/* are stored here also receive the source address of the watch */
	SOCKET		m_udpSocket;
	SOCKET		m_tcpSocket;

	bool		m_networkEnable;
	bool		m_connect;		/* Or during the competition? */
	int			m_queueSize;
	int			m_delay;
	int			m_playSide;		/* 1=1P, 2=2P, 3=Watch */
	DWORD		m_time;
	DWORD*		m_key;
	WORD*		m_syncChk;		/* Synchronization check */

	bool		m_suspend;		/* Do not take the synchronization of the key */
	int			m_suspendFrame;	/* The Suspend to have time */
	int			m_vsloadFrame;	/* elapsed frame of vsload */
	int			m_totalSlow;	/* Time stopped in the network of convenience. Especially Tsukaimichi not */
	int			m_lobbyFrame;
	bool		m_initKeySet;	/* Key settings or send and receive already? */

	int			m_enMaxPacketSize;	/* The maximum packet size of opponent */

	/* Data waiting flag from the opponent */
	volatile bool	m_waitingConnectReply;
	volatile bool	m_waitingSuspendReply;
	volatile bool	m_waitingResumeReply;
	volatile bool	m_waitingData;
	volatile bool	m_waitingDataReply;
	volatile bool	m_waitingWatchInReply;
	volatile char	m_waitingWatchInRootReply;	/* You do not wait = 0, waiting = 1, connectable = 2, not be connected = 3 */
	volatile bool	m_waitingBattleInfoRequestReply;

	enum { EWIRReply_Idle = 0, EWIRReply_Wait, EWIRReply_Success, EWIRReply_Fail, };

	volatile int	m_waitingDataType;		/* Data type of the reception waiting in Packet_Data command */

	volatile bool	m_recvSuspend;			/* Opponent of Suspend situation */
	volatile bool	m_recvVSLoadCompleted;	/* Men load status of */
	
	char*		m_recvDataPtr;			/* Buffer pointer to be received by the Packet_Data command */
	DWORD		m_recvDataSize;			/* Buffer size to be received by the Packet_Data command */

	int			m_sendDataSeq;	/* Transmitted data sequence number */
	int			m_recvDataSeq;	/* Received data sequence number */
								/* It will be reset each time the connect */
								/* Below this value of data is not received because the reception of already */

	CRITICAL_SECTION	m_csKey;
	CRITICAL_SECTION	m_csNode;
	CRITICAL_SECTION	m_csWatch;

	char		m_errMsg[1024];
	int			m_errMsgTime;

	// for watch client
	bool		m_watch;				// During operation as a spectator client
	bool		m_1stCaster;			// Or primary delivery business?
	bool		m_watchRecvComplete;	// Whether the data reception has been completed?
	char		m_watchRootName[2][30];	// Distributor of name
	sockaddr_in	m_watchRootAddr[2];		// Delivery source address
	int			m_watchRootGameCount[2];// Distributor of game ID
	int			m_watchRecvSize;		// Received data size
	DWORD		m_lastWatchDataTime;	// Time it was last received a Packet_WatchData
	int			m_totalGalleryCount;	// General spectator number (although if delivery's match ends in watching will not be updated as it is, it will be the specification)
	int			m_watchFailCount;		// The number of times it fails to automatic watch request in a continuous
	sockaddr_in	m_watchParentAddr;		// Parent of address

	// Support for compressed data (received data size is I use the compressed data Dattara 128 or more)
	char*		m_watchRecvCompData;	// Keep the compressed data until all finishes reception
	int			m_watchRecvCompSize;	// Size of the received already compressed data

	// for watch server
	CWatcher	m_watcher[WATCH_MAX_CHILD];
	int			m_recvGalleryCount;		// Spectator number of the other party
};

// extern
extern CNetMgr* g_netMgr;
