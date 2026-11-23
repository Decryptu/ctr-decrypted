#include "ap_chat.h"
#include "ap_net.h"
#include <platform.h>
#include <platform/native_input.h>

enum
{
	CHAT_X = 32,
	CHAT_RIGHT = 480,
	CHAT_FIELD_Y = 161,
	CHAT_COLUMNS = 34, // FONT_SMALL is at most 13 pixels per literal glyph
	CHAT_VISIBLE_ROWS = 9,
	CHAT_MAX_WRAPPED_ROWS = 1 + AP_CHAT_MESSAGE_BYTES / (CHAT_COLUMNS / 2),
};

struct ChatLine
{
	unsigned message;
	int part;
	int color;
	char text[CHAT_COLUMNS + 1];
};

static struct ChatLine chatLines[AP_CHAT_HISTORY * CHAT_MAX_WRAPPED_ROWS];
static unsigned chatRoom, chatNewest;
static int chatLineCount, chatScroll, chatEditing, chatPadPrev;
static char chatDraft[AP_CHAT_DRAFT_BYTES], chatBackup[AP_CHAT_DRAFT_BYTES];
static const char *chatNotice;
static void AP_ChatMenuProc(struct RectMenu *menu);

static struct RectMenu chatMenu = {
	.stringIndexTitle = -1,
	.state = EXECUTE_FUNCPTR | DISABLE_INPUT_ALLOW_FUNCPTRS,
	.funcPtr = AP_ChatMenuProc,
};

// The retail font uses several ASCII characters for controller icons and has
// gaps in its punctuation. Only the display is substituted; Say gets the draft.
static char AP_ChatGlyph(unsigned char c)
{
	if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		return ' ';
	if (c == '@') return 'a';
	if (c == '[' || c == '(') return '<';
	if (c == ']' || c == ')') return '>';
	if (c == '*' || c == '^' || c == '_') return '-';
	if (c == '"') return '\'';
	if (c == ';') return ':';
	if (c >= '!' && c <= '~' && (unsigned char)data.font_characterIconID[c - '!'] != 0xff)
		return (char)c;
	return '?';
}

static void AP_ChatWrap(unsigned id, const char *message, int color)
{
	char display[AP_CHAT_MESSAGE_BYTES];
	int length = 0;
	for (const unsigned char *p = (const unsigned char *)message; *p; p++)
	{
		if ((*p & 0xc0) != 0x80)
			display[length++] = AP_ChatGlyph(*p);
	}
	display[length] = '\0';

	const char *start = display;
	for (int part = 0; *start; part++)
	{
		while (*start == ' ')
			start++;
		if (!*start)
			break;
		int n = 0, lastSpace = -1;
		while (n < CHAT_COLUMNS && start[n])
		{
			if (start[n] == ' ')
				lastSpace = n;
			n++;
		}
		if (start[n] && start[n] != ' ' && lastSpace >= CHAT_COLUMNS / 2)
			n = lastSpace;
		struct ChatLine *line = &chatLines[chatLineCount++];
		line->message = id;
		line->part = part;
		line->color = color;
		memcpy(line->text, start, n);
		line->text[n] = '\0';
		start += n;
	}
}

static void AP_ChatRefresh(void)
{
	const unsigned room = AP_ChatRoomRevision();
	const unsigned newest = AP_ChatNewest();
	if (room == chatRoom && newest == chatNewest)
		return;

	struct ChatLine anchor = {0};
	if (room != chatRoom)
	{
		// Discard a draft before another room can receive it.
		if (chatEditing)
			NativeText_End();
		chatEditing = chatScroll = 0;
		chatDraft[0] = '\0';
		chatNotice = NULL;
	}
	else if (chatScroll > 0)
		anchor = chatLines[chatLineCount - chatScroll - 1];

	chatRoom = room;
	chatNewest = newest;
	chatLineCount = 0;
	const unsigned oldest = newest > AP_CHAT_HISTORY ? newest - AP_CHAT_HISTORY + 1 : 1;
	for (unsigned id = oldest; id <= newest; id++)
	{
		int highlight;
		const char *message = AP_ChatMessage(id, &highlight);
		if (message)
			AP_ChatWrap(id, message, highlight ? WHITE : GRAY);
	}
	if (anchor.message)
	{
		int bottom = 0;
		for (int i = 0; i < chatLineCount; i++)
		{
			if (chatLines[i].message == anchor.message && chatLines[i].part == anchor.part)
				bottom = i + 1;
		}
		chatScroll = chatLineCount - bottom;
	}
}

static void AP_ChatDraw(int connected)
{
	u32 *ot = GAME_TRACKER->pushBuffer_UI.ptrOT;
	RECT panel = {16, 6, 480, 204};
	RECT separator = {CHAT_X, 34, CHAT_RIGHT - CHAT_X, 2};
	RECT field = {CHAT_X - 4, CHAT_FIELD_Y - 3, CHAT_RIGHT - CHAT_X + 8, 15};
	Color edge = ColorCode_Load(&sdata->battleSetup_Color_UI_1);

	DecalFont_DrawLine("ARCHIPELAGO CHAT", 256, 14, FONT_BIG, JUSTIFY_CENTER | ORANGE);
	DecalFont_DrawLine(connected ? "CONNECTED" : "DISCONNECTED", CHAT_X, 41,
	                   FONT_SMALL, connected ? GRAY : ORANGE);
	DecalFont_DrawLine(chatScroll ? "EARLIER MESSAGES" : "LATEST", CHAT_RIGHT, 41,
	                   FONT_SMALL, JUSTIFY_RIGHT | GRAY);

	const int bottom = chatLineCount - chatScroll;
	const int top = bottom > CHAT_VISIBLE_ROWS ? bottom - CHAT_VISIBLE_ROWS : 0;
	if (!chatLineCount)
		DecalFont_DrawLine("NO MESSAGES YET", 256, 95, FONT_SMALL, JUSTIFY_CENTER | GRAY);
	for (int i = top; i < bottom; i++)
		DecalFont_DrawLine(chatLines[i].text, CHAT_X, 55 + (i - top) * 11, FONT_SMALL, chatLines[i].color);

	char line[CHAT_COLUMNS + 1];
	const int cursor = chatEditing ? NativeText_Cursor() : (int)strlen(chatDraft);
	const int start = cursor >= CHAT_COLUMNS ? cursor - CHAT_COLUMNS + 1 : 0;
	int n = 0;
	while (n < CHAT_COLUMNS && chatDraft[start + n])
	{
		line[n] = AP_ChatGlyph((unsigned char)chatDraft[start + n]);
		n++;
	}
	line[n] = '\0';
	DecalFont_DrawLine(n || chatEditing ? line : "MESSAGE OR !COMMAND", CHAT_X, CHAT_FIELD_Y,
	                   FONT_SMALL, n ? WHITE : GRAY);
	if (chatEditing)
	{
		int x = CHAT_X + DecalFont_GetLineWidthStrlen(line, cursor - start, FONT_SMALL);
		RECT caret = {x, CHAT_FIELD_Y + 9, 7, 1};
		RECTMENU_DrawOuterRect_Edge(&caret, &edge, 0x20, ot);
	}
	const char *hint = chatEditing ? "ENTER OR * SEND   ESC OR ^ BACK"
		: !connected ? "^ BACK   UP/DOWN SCROLL"
		: chatDraft[0] ? "* EDIT   ^ BACK   UP/DOWN SCROLL"
		: "* TYPE   [ !HINT   ^ BACK   UP/DOWN";
	DecalFont_DrawLine((char *)hint, 256, 195, FONT_SMALL, JUSTIFY_CENTER | WHITE);
	if (chatNotice)
		DecalFont_DrawLine((char *)chatNotice, CHAT_X, 178, FONT_SMALL, ORANGE);
	CTR_Box_DrawClearBox(&field, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL,
	                    ot, &GAME_TRACKER->backBuffer->primMem);
	RECTMENU_DrawOuterRect_Edge(&separator, &edge, 0x20, ot);
	RECTMENU_DrawInnerRect(&panel, 5, ot);
}

static void AP_ChatMenuProc(struct RectMenu *menu)
{
	(void)menu;
	MainFreeze_SafeAdvDestroy();
	AP_ChatRefresh();
	const int connected = ap_net_is_connected();
	const int tap = sdata->gGamepads->gamepad[0].buttonsTapped;
	const int pad = Platform_InputRawGamepadButtons();
	const int padTap = pad & ~chatPadPrev;
	chatPadPrev = pad;
	// The global Start handler runs after the menu. Chat owns that press.
	sdata->AnyPlayerTap &= ~BTN_START;

	if (chatEditing)
	{
		if (padTap & (RAW_BTN_TRIANGLE | RAW_BTN_START))
			NativeText_Resolve(2);
		else if (padTap & (RAW_BTN_CROSS | RAW_BTN_CIRCLE))
			NativeText_Resolve(1);
		const int result = NativeText_Result();
		if (result)
		{
			NativeText_End();
			chatEditing = 0;
			if (result == 2)
				strcpy(chatDraft, chatBackup);
			else if (chatDraft[strspn(chatDraft, " ")] == '\0')
				chatDraft[0] = '\0';
			else if (AP_ChatSend(chatDraft))
			{
				chatDraft[0] = '\0';
				chatScroll = 0;
				OtherFX_Play(1, 1);
			}
			else
				chatNotice = connected ? "SEND FAILED. DRAFT KEPT" : "DISCONNECTED. DRAFT KEPT";
			RECTMENU_ClearInput();
		}
	}
	else if (tap & (BTN_TRIANGLE | BTN_START))
	{
		OtherFX_Play(2, 1);
		RECTMENU_ClearInput();
		sdata->ptrDesiredMenu = MainFreeze_GetMenuPtr();
		return;
	}
	else if (connected && ((tap & (BTN_CROSS | BTN_CIRCLE)) || ((tap & BTN_SQUARE) && !chatDraft[0])))
	{
		strcpy(chatBackup, chatDraft);
		if (tap & BTN_SQUARE)
			strcpy(chatDraft, "!hint");
		chatNotice = NULL;
		NativeText_Begin(chatDraft, sizeof chatDraft, CHAT_X, CHAT_FIELD_Y, CHAT_RIGHT - CHAT_X, 12, 0);
		NativeText_EnableCursor();
		chatEditing = 1;
		OtherFX_Play(1, 1);
	}
	else
	{
		if (tap & BTN_UP)
			chatScroll++;
		else if (tap & BTN_DOWN)
			chatScroll--;
		const int maxScroll = chatLineCount - CHAT_VISIBLE_ROWS;
		if (chatScroll > maxScroll)
			chatScroll = maxScroll;
		if (chatScroll < 0)
			chatScroll = 0;
	}
	AP_ChatDraw(connected);
}

void AP_ChatOpen(void)
{
	chatScroll = 0;
	chatNotice = NULL;
	chatPadPrev = Platform_InputRawGamepadButtons();
	sdata->ptrDesiredMenu = &chatMenu;
}

void AP_ChatCheckMenu(void)
{
	if (chatEditing && (sdata->ptrActiveMenu != &chatMenu || sdata->Loading.stage != LOAD_IDLE))
	{
		NativeText_End();
		chatEditing = 0;
		strcpy(chatDraft, chatBackup);
	}
}
