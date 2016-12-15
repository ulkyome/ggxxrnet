#pragma once

//******************************************************************
// structures
//******************************************************************
#pragma pack(push)
#pragma pack(1)

typedef struct
{
	int		ver;
	char	scriptAddress[256];

	char	userName[41];	// In fact, until 20byte Although there 40byte minute
	char	pass[64];
	char	enableNet;
	WORD	port;
	char	delay;

	char	ignoreMisNode;
	char	ignoreSlow;
	short	wait;

	char	useEx;

	char	dispInvCombo;
	char	showfps;
	WORD	wins;
	char	rank;
	int		score;
	int		totalBattle;
	int		totalWin;
	int		totalLose;
	int		totalDraw;
	int		totalError;
	int		slowRate;	/* And to 100% if it exceeds the processing drop rate always 60fps */
	char	rounds;
	char	msg[256];
	char	watchBroadcast;		// To deliver
	char	watchIntrusion;		// Chaos into the license
	char	watchSaveReplay;	// You can either save the watch and replay?
	char	watchMaxNodes;		// 1 with the letter number payment
}SettingInfo;	// ver1.20~

#pragma pack(pop)

//******************************************************************
// proto types
//******************************************************************
void readSetting(void);
void writeSetting(void);