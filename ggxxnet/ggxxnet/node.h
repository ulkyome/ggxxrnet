#pragma once

//******************************************************************
// include
//******************************************************************
#include "dynArray.h"
#include "ggxxnet.h"

//******************************************************************
// class
//******************************************************************
class CNode
{
public:
	CNode(void) {}
	CNode(CNode& p_org)
	{
		*this = p_org;
	}
	~CNode(void)
	{
		m_tmpaddr.deleteAll();
	}	/* It is a memory leak */

	bool isNetSpeedGood(void);
	int  getPingAv(void);
	void recordPing(void);
	void clearPing(void);

	void operator=(CNode& p_org)
	{
		memcpy(m_name, p_org.m_name, 30);
		memcpy(m_addr, p_org.m_addr, 32);
		memcpy(m_ping, p_org.m_ping, sizeof(int) * 10);
		m_state		= p_org.m_state;
		m_rank		= p_org.m_rank;
		m_delay		= p_org.m_delay;
		m_ex		= p_org.m_ex;
		m_win		= p_org.m_win;
		m_ignoreSlow= p_org.m_ignoreSlow;
		m_round		= p_org.m_round;
		memcpy(m_ver, p_org.m_ver, 10);
		memcpy(m_id, p_org.m_id, 10);
		memcpy(m_msg, p_org.m_msg, 256);

		memcpy(m_battleInfoName[0], p_org.m_battleInfoName[0], 30);
		memcpy(m_battleInfoName[1], p_org.m_battleInfoName[1], 30);
		m_battleInfoIP[0] = p_org.m_battleInfoIP[0];
		m_battleInfoIP[1] = p_org.m_battleInfoIP[1];
		m_battleInfoGameCount[0] = p_org.m_battleInfoGameCount[0];
		m_battleInfoGameCount[1] = p_org.m_battleInfoGameCount[1];
		m_battleInfoChara[0] = p_org.m_battleInfoChara[0];
		m_battleInfoChara[1] = p_org.m_battleInfoChara[1];

		m_deny		= p_org.m_deny;
		m_needDetail= p_org.m_needDetail;
		m_gamecount	= p_org.m_gamecount;
		m_uhpNodeIdx= p_org.m_uhpNodeIdx;

		m_allowIntrusion	= p_org.m_allowIntrusion;
		m_watchMaxNode		= p_org.m_watchMaxNode;
		m_galleryCount		= p_org.m_galleryCount;
		m_galleryUpdateTime = p_org.m_galleryUpdateTime;

		m_sendpingtime		= p_org.m_sendpingtime;
		m_scriptCode		= p_org.m_scriptCode;

		m_tmpaddr.deleteAll();
		for (int i = 0; i < p_org.m_tmpaddr.size(); i++)
		{
			m_tmpaddr.add(new char[32]);
			memcpy(m_tmpaddr[i], p_org.m_tmpaddr[i], 32);
		}
		m_validInfo = p_org.m_validInfo;
	}

	bool isExistBattleInfo(void) { return m_battleInfoChara[0] != -1; }
	void clearBattleInfo(void)
	{
		m_battleInfoName[0][0] = '\0';
		m_battleInfoName[1][0] = '\0';
		m_battleInfoIP[0] = NULL;
		m_battleInfoIP[1] = NULL;
		m_battleInfoGameCount[0] = 0;
		m_battleInfoGameCount[1] = 0;
		m_battleInfoChara[0] = -1;
		m_battleInfoChara[1] = -1;
	}

public:
	char	m_name[30];
	char	m_addr[32];
	int		m_ping[10];		/* It wants to determine the status of the line in the PING average of the past 10 times */
	int		m_state;
	char	m_rank;
	char	m_delay;
	char	m_ex;
	short	m_win;
	char	m_ignoreSlow;
	char	m_round;
	char	m_ver[10];
	int		m_idsize;
	char	m_id[10];
	char	m_msg[256];
	bool	m_deny;			/* I represent that this node is rejecting his */
	bool	m_needDetail;	/* The request details such as comments pingReply */
	int		m_gamecount;	/* Also available for the game identification spectator */
	int		m_uhpNodeIdx;	/* Node index to request the address diffusion */

	bool	m_allowIntrusion;		/* Chaos into the license */
	char	m_watchMaxNode;			/* Number of nodes to direct connection permission */
	int		m_galleryCount;			/* The number of spectators */
	DWORD	m_galleryUpdateTime;	/* time you update the m_galleryCount */

	/* Match information */
	char	m_battleInfoName[2][30];
	DWORD	m_battleInfoIP[2];
	DWORD	m_battleInfoGameCount[2];
	char	m_battleInfoChara[2];

	DWORD	m_sendpingtime;	/* time you send a ping */
	DWORD	m_scriptCode;
	
	/* Structure that can be managed together a plurality of ports for UDP Hole Punching is required */
	/* NIt is to ping ores use all the m_tmpaddr, you use the usual m_addr other than it. */
	
	TDynArray<char*>	m_tmpaddr;	/* Tentative address of up to establish a connection */

	// Already acquired information combines EValidFlag to flag
	DWORD	m_validInfo;
};

class CNodeMgr
{
public:
	CNodeMgr(void);
	~CNodeMgr(void);
	
	CNode*	getNode(int p_idx) { return &m_nodeList[p_idx]; }
	int		getNodeCount(void) { return m_nodeList.size(); }
	
	int		addNode(char* p_addr, char* p_name, bool p_busy, bool p_needUHP);

	void	removeNode(int p_idx);

	void	removeAllNode(void);
	int		findNodeIdx_address(char* p_addr);
	int		findNodeIdx_name_ip(char* p_name, DWORD p_ip);

	void	sortNodeList(int p_type);

	static void getNodeInfoFromString(char* p_str, char* p_ip, int* p_port);
	
	char*	getOwnNode(void) { return m_ownNode; }
	void	setOwnNode(char* p_str) { __strncpy(m_ownNode, p_str, 31); }

private:
	TDynArray<CNode>	m_nodeList;
	char			m_ownNode[32];
};

//******************************************************************
// extern
//******************************************************************
extern CNodeMgr* g_nodeMgr;
