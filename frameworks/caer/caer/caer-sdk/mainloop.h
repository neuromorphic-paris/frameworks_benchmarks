/*
 * Public header for support library.
 * Modules can use this and link to it.
 */

#ifndef CAER_SDK_MAINLOOP_H_
#define CAER_SDK_MAINLOOP_H_

#include "module.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

void caerMainloopDataNotifyIncrease(void *p);
void caerMainloopDataNotifyDecrease(void *p);

bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId);

bool caerMainloopModuleExists(int16_t id);
enum caer_module_type caerMainloopModuleGetType(int16_t id);
uint32_t caerMainloopModuleGetVersion(int16_t id);
enum caer_module_status caerMainloopModuleGetStatus(int16_t id);
sshsNode caerMainloopModuleGetConfigNode(int16_t id);
size_t caerMainloopModuleGetInputDeps(int16_t id, int16_t **inputDepIds);
size_t caerMainloopModuleGetOutputRevDeps(int16_t id, int16_t **outputRevDepIds);
size_t caerMainloopModuleResetOutputRevDeps(int16_t id);
sshsNode caerMainloopModuleGetSourceNodeForInput(int16_t id, size_t inputNum);
sshsNode caerMainloopModuleGetSourceInfoForInput(int16_t id, size_t inputNum);

sshsNode caerMainloopGetSourceNode(int16_t sourceID); // Can be NULL.
void *caerMainloopGetSourceState(int16_t sourceID);   // Can be NULL.
sshsNode caerMainloopGetSourceInfo(int16_t sourceID); // Can be NULL.

#ifdef __cplusplus
}
#endif

#endif /* CAER_SDK_MAINLOOP_H_ */
