// DS Download Play (multiboot) and the WVR library: not available; every request fails the way the SDK reports an
// unusable wireless module. Local wireless play (the wm library) works: see wm7.c.
// Generated from the decomp's prototypes (see git history); keep the signatures in sync with the headers.
#include "port.h"
#include <string.h>
#include "types.h"
#include "multiboot_checksum.h"
#include "multiboot_communication.h"
#include "multiboot_data_pieces.h"
#include "multiboot_file_register.h"
#include "multiboot_game_entry.h"
#include "multiboot_game_info_beacon.h"
#include "multiboot_parent_desc.h"
#include "multiboot_parent_state_machine.h"
#include "multiboot_scan_lock.h"
#include "multiboot_segment_read.h"
#include "multiboot_task_thread.h"
#include "wireless_beacon_wep_settings.h"
#include "wireless_driver_startup.h"
#include "wireless_key_data_sharing.h"
#include "wireless_link_control.h"
#include "wireless_system_commands.h"

#define WM_ERRCODE_WM_DISABLE 4

void *MB_CommGetChildUser(u32 child) { (void) child; return NULL;}
u32 MB_CommIsBootable(u32 child) { (void) child; return 0;}
u32 MB_CommResponseRequest(u32 child, u32 pairIndex) { (void) child; (void) pairIndex; return 0;}
void MB_CommSetParentStateCallback(MBParentStateCallback parentStateCallback) { (void) parentStateCallback; }
void MB_CountGameInfoLifetime(EntryCallbackFunc callback, u32 value) { (void) callback; (void) value; }
void MB_DisconnectChild(u32 childIndex) { (void) childIndex; }
void MB_EndToIdle(void) { }
u32 MB_FakeGetParentBssDesc(u32 index, WMBssDesc *destination) { (void) index; (void) destination; return 0;}
u32 MB_FakeReadParentBssDesc(u32 entryIndex, WMBssDesc *scanBlock, u16 parentMaxSize, u16 childMaxSize, u32 ksFlag, u32 flagTwo) { (void) entryIndex; (void) scanBlock; (void) parentMaxSize; (void) childMaxSize; (void) ksFlag; (void) flagTwo; return 0;}
MBGameInfoRecvList *MB_GetGameInfoRecvList() { return NULL;}
u32 MB_GetSegmentLength(FSFile *file) { (void) file; return 0;}
s32 MB_Init(void *buffer, MBUserInfo *user, u32 ggid, u32 tgid, u32 dmaNo) { (void) buffer; (void) user; (void) ggid; (void) tgid; (void) dmaNo; return WM_ERRCODE_WM_DISABLE;}
void MB_InitRecvGameInfoStatus(void) { }
void MB_ReadMultiBootParentBssDesc(WMBssDesc *bssDesc, u16 parentMaxSize, u16 childMaxSize, u32 ksFlag, u32 flagTwo) { (void) bssDesc; (void) parentMaxSize; (void) childMaxSize; (void) ksFlag; (void) flagTwo; }
int MB_ReadSegment(FSFile *file, void *buffer, u32 bufferSize) { (void) file; (void) buffer; (void) bufferSize; return WM_ERRCODE_WM_DISABLE;}
u32 MB_RecvGameInfoBeacon(EntryCallbackFunc callback, u32 value, WMBssDesc *scanBlock) { (void) callback; (void) value; (void) scanBlock; return 0;}
u32 MB_RegisterFile(void *rangeTable, SegmentBuffer *source) { (void) rangeTable; (void) source; return 0;}
u32 MB_SetParentCommParam(u32 sendDataSize, u32 maxChildren) { (void) sendDataSize; (void) maxChildren; return 0;}
s32 MB_StartParentFromIdle(u16 channel) { (void) channel; return WM_ERRCODE_WM_DISABLE;}
void MBi_CommChangeParentStateCallbackOnly(u32 child, u32 value, void *data) { (void) child; (void) value; (void) data; }
u8 *MBi_MakeChildSendBuffer(ChildMessage *message, u8 *buffer) { (void) message; (void) buffer; return NULL;}
u8 MBi_SendRequestDataPiece(void *output, void *pieces) { (void) output; (void) pieces; return 0;}
void MBi_SetBeaconRecvStatusBuffer(void *entryTable) { (void) entryTable; }
void MBi_SetChildMPMaxSize(s32 packetSize) { (void) packetSize; }
u8 *MBi_SetRecvBufferFromParent(ParentMessage *message, u8 *buffer) { (void) message; (void) buffer; return NULL;}
void MBi_SetScanLockFunc(void (*setCallback)(const u8 *), void (*clearCallback)(void)) { }
s32 WVR_StartUpAsync(u32 vram, void (*callback)(u32, s32), u32 arg) { (void) vram; (void) arg; if (callback) callback(arg, 0); return 1;}
s32 WVR_TerminateAsync(void (*callback)(u32, s32), u32 arg) { (void) arg; if (callback) callback(arg, 0); return 1;}
