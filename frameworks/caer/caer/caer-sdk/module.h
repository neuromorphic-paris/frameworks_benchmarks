/*
 * Public header for support library.
 * Modules can use this and link to it.
 */

#ifndef CAER_SDK_MODULE_H_
#define CAER_SDK_MODULE_H_

#include "utils.h"

#ifdef __cplusplus

#include <atomic>
using atomic_bool          = std::atomic_bool;
using atomic_uint_fast8_t  = std::atomic_uint_fast8_t;
using atomic_uint_fast32_t = std::atomic_uint_fast32_t;
using atomic_int_fast16_t  = std::atomic_int_fast16_t;

#else

#include <stdatomic.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Module-related definitions.
enum caer_module_status {
	CAER_MODULE_STOPPED = 0,
	CAER_MODULE_RUNNING = 1,
};

/**
 * Input modules strictly create data, as such they have no input event
 * streams and at least 1 output event stream.
 * Output modules consume data, without modifying it, so they have at
 * least 1 input event stream, and no output event streams. They must
 * set the 'readOnly' flag to true on all their input event streams.
 * Processor modules do something with data, filtering it or creating
 * new data out of it, as such they must have at least 1 input event
 * stream, and at least 1 output event stream (implicit or explicit).
 * Explicit output streams in this case are new data that is declared
 * as output event stream explicitly, while implicit are input streams
 * with their 'readOnly' flag set to false, meaning the data is modified.
 * Output streams can either be undefined and later be determined at
 * runtime, or be well defined. Only one output stream per type is allowed.
 */
enum caer_module_type {
	CAER_MODULE_INPUT     = 0,
	CAER_MODULE_OUTPUT    = 1,
	CAER_MODULE_PROCESSOR = 2,
};

static inline const char *caerModuleTypeToString(enum caer_module_type type) {
	switch (type) {
		case CAER_MODULE_INPUT:
			return ("INPUT");
			break;

		case CAER_MODULE_OUTPUT:
			return ("OUTPUT");
			break;

		case CAER_MODULE_PROCESSOR:
			return ("PROCESSOR");
			break;

		default:
			return ("UNKNOWN");
			break;
	}
}

struct caer_event_stream_in {
	int16_t type;   // Use -1 for any type.
	int16_t number; // Use -1 for any number of.
	bool readOnly;  // True if input is never modified.
};

typedef struct caer_event_stream_in const *caerEventStreamIn;

#define CAER_EVENT_STREAM_IN_SIZE(x) (sizeof(x) / sizeof(struct caer_event_stream_in))

struct caer_event_stream_out {
	int16_t type; // Use -1 for undefined output (determined at runtime from configuration).
};

typedef struct caer_event_stream_out const *caerEventStreamOut;

#define CAER_EVENT_STREAM_OUT_SIZE(x) (sizeof(x) / sizeof(struct caer_event_stream_out))

struct caer_module_data {
	int16_t moduleID;
	sshsNode moduleNode;
	enum caer_module_status moduleStatus;
	atomic_bool running;
	atomic_uint_fast8_t moduleLogLevel;
	atomic_uint_fast32_t configUpdate;
	atomic_int_fast16_t doReset;
	void *moduleState;
	char *moduleSubSystemString;
};

typedef struct caer_module_data *caerModuleData;

struct caer_module_functions {
	void (*const moduleConfigInit)(sshsNode moduleNode); // Can be NULL.
	bool (*const moduleInit)(caerModuleData moduleData); // Can be NULL.
	void (*const moduleRun)(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
	void (*const moduleConfig)(caerModuleData moduleData);                           // Can be NULL.
	void (*const moduleExit)(caerModuleData moduleData);                             // Can be NULL.
	void (*const moduleReset)(caerModuleData moduleData, int16_t resetCallSourceID); // Can be NULL.
};

typedef struct caer_module_functions const *caerModuleFunctions;

struct caer_module_info {
	uint32_t version;
	const char *name;
	const char *description;
	enum caer_module_type type;
	size_t memSize;
	caerModuleFunctions functions;
	size_t inputStreamsSize;
	caerEventStreamIn inputStreams;
	size_t outputStreamsSize;
	caerEventStreamOut outputStreams;
};

typedef struct caer_module_info const *caerModuleInfo;

// Function to be implemented by modules:
caerModuleInfo caerModuleGetInfo(void);

// Functions available to call:
void caerModuleLog(caerModuleData moduleData, enum caer_log_level logLevel, const char *format, ...)
	ATTRIBUTE_FORMAT(3);
bool caerModuleSetSubSystemString(caerModuleData moduleData, const char *subSystemString);
void caerModuleConfigDefaultListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

#ifdef __cplusplus
}
#endif

#endif /* CAER_SDK_MODULE_H_ */
