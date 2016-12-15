#ifdef _MSC_VER
	#if (_MSC_VER >= 1400)
		#define POINTER_64 __ptr64
	#endif
#endif

//******************************************************************
// include
//******************************************************************
#include "ggxxnet.h"
#include "netMgr.h"
#include "setting.h"
#include "internet.h"
#include "util.h"
#include "md5.h"

#include <locale.h>
#include <mbstring.h>

#include "internet.h"
//******************************************************************
// function
//******************************************************************

void readSetting(void)
{
	char addrS[256];
	char *server = "gg.mm06.ru";
	char *script = "lobby_";
	char buf[1024];

	sprintf(addrS, "%s/%s%s", server, script, g_authData.lobby);

	/*sprintf(buf, "{\"cmd\":\"test\"}");
	internet_post(buf, strlen(buf), 1024, server, script);

	//SETFCW(DEFAULT_CW);

	delete buf;*/
	//test();
	__strncpy(g_setting.scriptAddress, addrS, 256);
	__strncpy(g_setting.userName, g_authData.user, 20);
	__strncpy(g_setting.pass, g_authData.pass, 64);
	g_setting.enableNet = 1;
	g_setting.port = 10000;
	g_setting.delay = 4;
	g_setting.ignoreMisNode = 0;
	g_setting.ignoreSlow = 1;
	g_setting.wait = 5;
	g_setting.useEx = 0;
	g_setting.dispInvCombo = 1;
	g_setting.showfps = 0;
	g_setting.wins = 0;
	g_setting.rank = Rank_F;
	g_setting.score = 0;
	g_setting.totalBattle = 0;
	g_setting.totalWin = 0;
	g_setting.totalLose = 0;
	g_setting.totalDraw = 0;
	g_setting.totalError = 0;
	g_setting.slowRate = 0;
	g_setting.rounds = 3;
	memset(g_setting.msg, 0, 256);
	g_setting.watchBroadcast = 1;
	g_setting.watchIntrusion = 0;
	g_setting.watchSaveReplay = 0;
	g_setting.watchMaxNodes = 1;
	g_scriptCode = 0;

}

void writeSetting(void)
{
	/*
	code
	*/
}
