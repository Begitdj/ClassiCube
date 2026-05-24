#include "Chat.h"
#include "Commands.h"
#include "String_.h"
#include "Stream.h"
#include "Event.h"
#include "Game.h"
#include "Logger.h"
#include "Server.h"
#include "Funcs.h"
#include "Utils.h"
#include "Options.h"
#include "Drawer2D.h"
 
static char status[5][STRING_SIZE];
static char bottom[3][STRING_SIZE];
static char client[2][STRING_SIZE];
static char announcement[STRING_SIZE];
static char bigAnnouncement[STRING_SIZE];
static char smallAnnouncement[STRING_SIZE];

cc_string Chat_Status[5]       = { String_FromArray(status[0]), String_FromArray(status[1]), String_FromArray(status[2]),
                                                                String_FromArray(status[3]), String_FromArray(status[4]) };
cc_string Chat_BottomRight[3]  = { String_FromArray(bottom[0]), String_FromArray(bottom[1]), String_FromArray(bottom[2]) };
cc_string Chat_ClientStatus[2] = { String_FromArray(client[0]), String_FromArray(client[1]) };

cc_string Chat_Announcement = String_FromArray(announcement);
cc_string Chat_BigAnnouncement = String_FromArray(bigAnnouncement);
cc_string Chat_SmallAnnouncement = String_FromArray(smallAnnouncement);

float Chat_AnnouncementLeft;
float Chat_BigAnnouncementLeft;
float Chat_SmallAnnouncementLeft;

CC_BIG_VAR struct StringsBuffer Chat_Log, Chat_InputLog;
cc_bool Chat_Logging;

/*########################################################################################################################*
*-------------------------------------------------------Chat logging------------------------------------------------------*
*#########################################################################################################################*/
double Chat_RecentLogTimes[CHATLOG_TIME_MASK + 1];

static void ClearChatLogs(void) {
	Mem_Set(Chat_RecentLogTimes, 0, sizeof(Chat_RecentLogTimes));
	StringsBuffer_Clear(&Chat_Log);
}

static char      logNameBuffer[STRING_SIZE];
static cc_string logName = String_FromArray(logNameBuffer);
static char      logPathBuffer[FILENAME_SIZE];
static cc_string logPath = String_FromArray(logPathBuffer);

static struct Stream logStream;
static int lastLogDay, lastLogMonth, lastLogYear;

static void ResetLogFile(void) {
	logName.length = 0;
	lastLogYear    = -123;
}

static void CloseLogFile(void) {
	cc_result res;
	if (!logStream.meta.file) return;

	res = logStream.Close(&logStream);
	if (res) { Logger_SysWarn2(res, "closing", &logPath); }
}

static cc_bool AllowedLogNameChar(char c) {
	return
		c == '{' || c == '}' || c == '[' || c == ']' || c == '(' || c == ')' ||
		(c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void Chat_SetLogName(const cc_string* name) {
	char c;
	int i;
	if (logName.length) return;

	for (i = 0; i < name->length; i++) {
		c = name->buffer[i];

		if (AllowedLogNameChar(c)) {
			String_Append(&logName, c);
		} else if (c == '&') {
			i++; 
		}
	}
}

void Chat_DisableLogging(void) {
	Chat_Logging = false;
	lastLogYear  = -321;
	Chat_AddRaw("&cDisabling chat logging");
	CloseLogFile();
}

static cc_bool CreateLogsDirectory(void) {
	static const cc_string dir = String_FromConst("logs");
	cc_filepath raw_dir;
	cc_result res;
	
	Platform_EncodePath(&raw_dir, &dir);
	res = Directory_Create2(&raw_dir);
	if (!res || res == ReturnCode_DirectoryExists) return true;

	Chat_DisableLogging();
	Logger_IOWarn2(res, "creating directory", &raw_dir); 
	return false;
}

static void OpenChatLog(struct cc_datetime* now) {
	cc_filepath raw_path;
	cc_result res;
	int i;
	if (Platform_ReadonlyFilesystem || !CreateLogsDirectory()) return;

	for (i = 0; i < 20; i++) {
		logPath.length = 0;
		String_Format3(&logPath, "logs/%p4-%p2-%p2 ", &now->year, &now->month, &now->day);

		if (i > 0) {
			String_Format2(&logPath, "%s _%i.txt", &logName, &i);
		} else {
			String_Format1(&logPath, "%s.txt", &logName);
		}

		Platform_EncodePath(&raw_path, &logPath);
		res = Stream_AppendPath(&logStream, &raw_path);

		if (res && res != ReturnCode_FileShareViolation) {
			Chat_DisableLogging();
			Logger_IOWarn2(res, "appending to", &raw_path);
			return;
		}

		if (res == ReturnCode_FileShareViolation) continue;
		return;
	}

	Chat_DisableLogging();
	Chat_Add1("&cFailed to open a chat log file after %i tries, giving up", &i);	
}

static void AppendChatLog(const cc_string* text) {
	cc_string str; char strBuffer[DRAWER2D_MAX_TEXT_LENGTH];
	struct cc_datetime now;
	cc_result res;	

	if (!logName.length || !Chat_Logging) return;
	DateTime_CurrentLocal(&now);

	if (now.day != lastLogDay || now.month != lastLogMonth || now.year != lastLogYear) {
		CloseLogFile();
		OpenChatLog(&now);
	}

	lastLogDay = now.day; lastLogMonth = now.month; lastLogYear = now.year;
	if (!logStream.meta.file) return;

	String_InitArray(str, strBuffer);
	String_Format3(&str, "[%p2:%p2:%p2] ", &now.hour, &now.minute, &now.second);
	Drawer2D_WithoutColors(&str, text);

	res = Stream_WriteLine(&logStream, &str);
	if (!res) return;
	Chat_DisableLogging();
	Logger_SysWarn2(res, "writing to", &logPath);
}

void Chat_Add1(const char* format, const void* a1) {
	Chat_Add4(format, a1, NULL, NULL, NULL);
}
void Chat_Add2(const char* format, const void* a1, const void* a2) {
	Chat_Add4(format, a1, a2, NULL, NULL);
}
void Chat_Add3(const char* format, const void* a1, const void* a2, const void* a3) {
	Chat_Add4(format, a1, a2, a3, NULL);
}
void Chat_Add4(const char* format, const void* a1, const void* a2, const void* a3, const void* a4) {
	cc_string msg; char msgBuffer[STRING_SIZE * 2];
	String_InitArray(msg, msgBuffer);

	String_Format4(&msg, format, a1, a2, a3, a4);
	Chat_AddOf(&msg, MSG_TYPE_NORMAL);
}

void Chat_AddRaw(const char* raw) {
	cc_string str = String_FromReadonly(raw);
	Chat_AddOf(&str, MSG_TYPE_NORMAL);
}
void Chat_Add(const cc_string* text) { Chat_AddOf(text, MSG_TYPE_NORMAL); }

void Chat_AddOf(const cc_string* text, int msgType) {
	cc_string str;
	if (msgType == MSG_TYPE_NORMAL) {
		if (Chat_Log.totalLength > 8388000) {
			ClearChatLogs();
			Chat_AddRaw("&cChat log cleared as it hit 8.3 million character limit");
		}

		str        = *text; 
		str.length = min(str.length, DRAWER2D_MAX_TEXT_LENGTH);

		Chat_GetLogTime(Chat_Log.count) = Game.Time;
		AppendChatLog(&str);
		StringsBuffer_Add(&Chat_Log, &str);
	} else if (msgType >= MSG_TYPE_STATUS_1 && msgType <= MSG_TYPE_STATUS_3) {
		String_Copy(&Chat_Status[2 + (msgType - MSG_TYPE_STATUS_1)], text);
	} else if (msgType >= MSG_TYPE_BOTTOMRIGHT_1 && msgType <= MSG_TYPE_BOTTOMRIGHT_3) {	
		String_Copy(&Chat_BottomRight[msgType - MSG_TYPE_BOTTOMRIGHT_1], text);
	} else if (msgType == MSG_TYPE_ANNOUNCEMENT) {
		String_Copy(&Chat_Announcement, text);
		Chat_AnnouncementLeft = 5.0f;
	} else if (msgType == MSG_TYPE_BIGANNOUNCEMENT) {
		String_Copy(&Chat_BigAnnouncement, text);
		Chat_BigAnnouncementLeft = 5.0f;
	} else if (msgType == MSG_TYPE_SMALLANNOUNCEMENT) {
		String_Copy(&Chat_SmallAnnouncement, text);
		Chat_SmallAnnouncementLeft = 5.0f;
	} else if (msgType >= MSG_TYPE_CLIENTSTATUS_1 && msgType <= MSG_TYPE_CLIENTSTATUS_2) {
		String_Copy(&Chat_ClientStatus[msgType - MSG_TYPE_CLIENTSTATUS_1], text);
	} else if (msgType >= MSG_TYPE_EXTRASTATUS_1 && msgType <= MSG_TYPE_EXTRASTATUS_2) {
		String_Copy(&Chat_Status[msgType - MSG_TYPE_EXTRASTATUS_1], text);
	} 

	Event_RaiseChat(&ChatEvents.ChatReceived, text, msgType);
}

static void LogInputUsage(const cc_string* text) {
	if (Chat_InputLog.count) {
		int lastIndex  = Chat_InputLog.count - 1;
		cc_string last = StringsBuffer_UNSAFE_Get(&Chat_InputLog, lastIndex);

		if (String_Equals(text, &last)) return;
	}
	StringsBuffer_Add(&Chat_InputLog, text);
}

void Chat_Send(const cc_string* text, cc_bool logUsage) {
	if (!text->length) return;
	Event_RaiseChat(&ChatEvents.ChatSending, text, 0);
	if (logUsage) LogInputUsage(text);

	if (!Commands_Execute(text)) {
		Server.SendChat(text);
	}
}

static void OnInit(void) {
#if defined CC_BUILD_MOBILE || defined CC_BUILD_WEB
	Chat_Logging = Options_GetBool(OPT_CHAT_LOGGING, false);
#else
	Chat_Logging = Options_GetBool(OPT_CHAT_LOGGING, true);
#endif
}

static void ClearCPEMessages(void) {
	Chat_AddOf(&String_Empty, MSG_TYPE_ANNOUNCEMENT);
	Chat_AddOf(&String_Empty, MSG_TYPE_BIGANNOUNCEMENT);
	Chat_AddOf(&String_Empty, MSG_TYPE_SMALLANNOUNCEMENT);
	Chat_AddOf(&String_Empty, MSG_TYPE_STATUS_1);
	Chat_AddOf(&String_Empty, MSG_TYPE_STATUS_2);
	Chat_AddOf(&String_Empty, MSG_TYPE_STATUS_3);
	Chat_AddOf(&String_Empty, MSG_TYPE_BOTTOMRIGHT_1);
	Chat_AddOf(&String_Empty, MSG_TYPE_BOTTOMRIGHT_2);
	Chat_AddOf(&String_Empty, MSG_TYPE_BOTTOMRIGHT_3);
}

static void OnReset(void) {
	CloseLogFile();
	ResetLogFile();
	ClearCPEMessages();
	
	/* WELCOME MESSAGE */
	Chat_AddRaw("&eTeraCota Client &floaded!");
}

static void OnFree(void) {
	CloseLogFile();
	ClearCPEMessages();

	ClearChatLogs();
	StringsBuffer_Clear(&Chat_InputLog);
}

struct IGameComponent Chat_Component = {
	OnInit, /* Init  */
	OnFree, /* Free  */
	OnReset /* Reset */
};
