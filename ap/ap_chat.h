#ifndef AP_CHAT_H
#define AP_CHAT_H

#define AP_CHAT_HISTORY 64
#define AP_CHAT_MESSAGE_BYTES 512
#define AP_CHAT_DRAFT_BYTES 201

#ifdef __cplusplus
extern "C" {
#endif
unsigned AP_ChatRoomRevision(void);
unsigned AP_ChatNewest(void);
const char *AP_ChatMessage(unsigned id, int *highlight);
int AP_ChatSend(const char *text);
#ifdef __cplusplus
}
#else
void AP_ChatOpen(void);
void AP_ChatCheckMenu(void);
#endif
#endif
