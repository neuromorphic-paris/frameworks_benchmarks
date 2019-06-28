#ifndef SSHS_H_
#define SSHS_H_

#ifdef __cplusplus

#	include <cerrno>
#	include <cinttypes>
#	include <cstdint>
#	include <cstdio>
#	include <cstdlib>

#else

#	include <errno.h>
#	include <inttypes.h>
#	include <stdbool.h>
#	include <stdint.h>
#	include <stdio.h>
#	include <stdlib.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Function deprecation.
#if defined(__GNUC__) || defined(__clang__)
#	define DEPRECATED_FUNCTION(DEPR_MSG) __attribute__((__deprecated__(DEPR_MSG)))
#elif defined(_MSC_VER)
#	define DEPRECATED_FUNCTION(DEPR_MSG) __declspec(deprecated(DEPR_MSG))
#else
#	define DEPRECATED_FUNCTION(DEPR_MSG)
#endif

// SSHS node
typedef struct sshs_node *sshsNode;

enum sshs_node_attr_value_type {
	SSHS_UNKNOWN = -1,
	SSHS_BOOL    = 0,
	SSHS_INT     = 3,
	SSHS_LONG    = 4,
	SSHS_FLOAT   = 5,
	SSHS_DOUBLE  = 6,
	SSHS_STRING  = 7,
};

union sshs_node_attr_value {
	bool boolean;
	int32_t iint;
	int64_t ilong;
	float ffloat;
	double ddouble;
	char *string;
};

union sshs_node_attr_range {
	int32_t iintRange;
	int64_t ilongRange;
	float ffloatRange;
	double ddoubleRange;
	size_t stringRange;
};

struct sshs_node_attr_ranges {
	union sshs_node_attr_range min;
	union sshs_node_attr_range max;
};

enum sshs_node_attr_flags {
	SSHS_FLAGS_NORMAL      = 0,
	SSHS_FLAGS_READ_ONLY   = 1,
	SSHS_FLAGS_NOTIFY_ONLY = 2,
	SSHS_FLAGS_NO_EXPORT   = 4,
};

enum sshs_node_node_events {
	SSHS_CHILD_NODE_ADDED   = 0,
	SSHS_CHILD_NODE_REMOVED = 1,
};

enum sshs_node_attribute_events {
	SSHS_ATTRIBUTE_ADDED    = 0,
	SSHS_ATTRIBUTE_MODIFIED = 1,
	SSHS_ATTRIBUTE_REMOVED  = 2,
};

typedef void (*sshsNodeChangeListener)(
	sshsNode node, void *userData, enum sshs_node_node_events event, const char *changeNode);

typedef void (*sshsAttributeChangeListener)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

const char *sshsNodeGetName(sshsNode node);
const char *sshsNodeGetPath(sshsNode node);

/**
 * This returns a reference to a node, and as such must be carefully mediated with
 * any sshsNodeRemoveNode() calls.
 */
sshsNode sshsNodeGetParent(sshsNode node);
/**
 * Remember to free the resulting array. This returns references to nodes,
 * and as such must be carefully mediated with any sshsNodeRemoveNode() calls.
 */
sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren); // Walk all children.

void sshsNodeAddNodeListener(sshsNode node, void *userData, sshsNodeChangeListener node_changed);
void sshsNodeRemoveNodeListener(sshsNode node, void *userData, sshsNodeChangeListener node_changed);
void sshsNodeRemoveAllNodeListeners(sshsNode node);

void sshsNodeAddAttributeListener(sshsNode node, void *userData, sshsAttributeChangeListener attribute_changed);
void sshsNodeRemoveAttributeListener(sshsNode node, void *userData, sshsAttributeChangeListener attribute_changed);
void sshsNodeRemoveAllAttributeListeners(sshsNode node);

/**
 * Careful, only use if no references exist to this node and all its children.
 * References are created by sshsGetNode(), sshsGetRelativeNode(),
 * sshsNodeGetParent() and sshsNodeGetChildren().
 */
void sshsNodeRemoveNode(sshsNode node);
void sshsNodeClearSubTree(sshsNode startNode, bool clearStartNode);

void sshsNodeCreateAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value defaultValue, const struct sshs_node_attr_ranges ranges, int flags,
	const char *description);
void sshsNodeRemoveAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type);
void sshsNodeRemoveAllAttributes(sshsNode node);
bool sshsNodeAttributeExists(sshsNode node, const char *key, enum sshs_node_attr_value_type type);
bool sshsNodePutAttribute(
	sshsNode node, const char *key, enum sshs_node_attr_value_type type, union sshs_node_attr_value value);
union sshs_node_attr_value sshsNodeGetAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type);
bool sshsNodeUpdateReadOnlyAttribute(
	sshsNode node, const char *key, enum sshs_node_attr_value_type type, union sshs_node_attr_value value);

void sshsNodeCreateBool(sshsNode node, const char *key, bool defaultValue, int flags, const char *description);
bool sshsNodePutBool(sshsNode node, const char *key, bool value);
bool sshsNodeGetBool(sshsNode node, const char *key);
void sshsNodeCreateInt(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	int flags, const char *description);
bool sshsNodePutInt(sshsNode node, const char *key, int32_t value);
int32_t sshsNodeGetInt(sshsNode node, const char *key);
void sshsNodeCreateLong(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	int flags, const char *description);
bool sshsNodePutLong(sshsNode node, const char *key, int64_t value);
int64_t sshsNodeGetLong(sshsNode node, const char *key);
void sshsNodeCreateFloat(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue, int flags,
	const char *description);
bool sshsNodePutFloat(sshsNode node, const char *key, float value);
float sshsNodeGetFloat(sshsNode node, const char *key);
void sshsNodeCreateDouble(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	int flags, const char *description);
bool sshsNodePutDouble(sshsNode node, const char *key, double value);
double sshsNodeGetDouble(sshsNode node, const char *key);
void sshsNodeCreateString(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	int flags, const char *description);
bool sshsNodePutString(sshsNode node, const char *key, const char *value);
char *sshsNodeGetString(sshsNode node, const char *key);

bool sshsNodeExportNodeToXML(sshsNode node, int fd);
bool sshsNodeExportSubTreeToXML(sshsNode node, int fd);
bool sshsNodeImportNodeFromXML(sshsNode node, int fd, bool strict);
bool sshsNodeImportSubTreeFromXML(sshsNode node, int fd, bool strict);

bool sshsNodeStringToAttributeConverter(sshsNode node, const char *key, const char *type, const char *value);
const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames);
const char **sshsNodeGetAttributeKeys(sshsNode node, size_t *numKeys);
enum sshs_node_attr_value_type sshsNodeGetAttributeType(sshsNode node, const char *key);
struct sshs_node_attr_ranges sshsNodeGetAttributeRanges(
	sshsNode node, const char *key, enum sshs_node_attr_value_type type);
int sshsNodeGetAttributeFlags(sshsNode node, const char *key, enum sshs_node_attr_value_type type);
char *sshsNodeGetAttributeDescription(sshsNode node, const char *key, enum sshs_node_attr_value_type type);

// Helper functions
const char *sshsHelperTypeToStringConverter(enum sshs_node_attr_value_type type);
enum sshs_node_attr_value_type sshsHelperStringToTypeConverter(const char *typeString);
char *sshsHelperValueToStringConverter(enum sshs_node_attr_value_type type, union sshs_node_attr_value value);
union sshs_node_attr_value sshsHelperStringToValueConverter(
	enum sshs_node_attr_value_type type, const char *valueString);

void sshsNodeCreateAttributeListOptions(
	sshsNode node, const char *key, const char *listOptions, bool allowMultipleSelections);
void sshsNodeCreateAttributeFileChooser(sshsNode node, const char *key, const char *allowedExtensions);

// SSHS
typedef struct sshs_struct *sshs;
typedef void (*sshsErrorLogCallback)(const char *msg, bool fatal);

sshs sshsGetGlobal(void);
void sshsSetGlobalErrorLogCallback(sshsErrorLogCallback error_log_cb);
sshsErrorLogCallback sshsGetGlobalErrorLogCallback(void);
sshs sshsNew(void);
bool sshsExistsNode(sshs st, const char *nodePath);
/**
 * This returns a reference to a node, and as such must be carefully mediated with
 * any sshsNodeRemoveNode() calls.
 */
sshsNode sshsGetNode(sshs st, const char *nodePath);
bool sshsExistsRelativeNode(sshsNode node, const char *nodePath);
/**
 * This returns a reference to a node, and as such must be carefully mediated with
 * any sshsNodeRemoveNode() calls.
 */
sshsNode sshsGetRelativeNode(sshsNode node, const char *nodePath);

typedef union sshs_node_attr_value (*sshsAttributeUpdater)(
	void *userData, const char *key, enum sshs_node_attr_value_type type);

void sshsAttributeUpdaterAdd(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	sshsAttributeUpdater updater, void *updaterUserData);
void sshsAttributeUpdaterRemove(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	sshsAttributeUpdater updater, void *updaterUserData);
void sshsAttributeUpdaterRemoveAllForNode(sshsNode node);
void sshsAttributeUpdaterRemoveAll(sshs tree);
bool sshsAttributeUpdaterRun(sshs tree);

/**
 * Listener must be able to deal with userData being NULL at any moment.
 * This can happen due to concurrent changes from this setter.
 */
void sshsGlobalNodeListenerSet(sshs tree, sshsNodeChangeListener node_changed, void *userData);
/**
 * Listener must be able to deal with userData being NULL at any moment.
 * This can happen due to concurrent changes from this setter.
 */
void sshsGlobalAttributeListenerSet(sshs tree, sshsAttributeChangeListener attribute_changed, void *userData);

// Deprecated.
DEPRECATED_FUNCTION("Byte type has been removed. Use integer type instead: sshsNodeCreateInt().")
void sshsNodeCreateByte(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	int flags, const char *description);
DEPRECATED_FUNCTION("Byte type has been removed. Use integer type instead: sshsNodePutInt().")
bool sshsNodePutByte(sshsNode node, const char *key, int8_t value);
DEPRECATED_FUNCTION("Byte type has been removed. Use integer type instead: sshsNodeGetInt().")
int8_t sshsNodeGetByte(sshsNode node, const char *key);
DEPRECATED_FUNCTION("Short type has been removed. Use integer type instead: sshsNodeCreateInt().")
void sshsNodeCreateShort(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	int flags, const char *description);
DEPRECATED_FUNCTION("Short type has been removed. Use integer type instead: sshsNodePutInt().")
bool sshsNodePutShort(sshsNode node, const char *key, int16_t value);
DEPRECATED_FUNCTION("Short type has been removed. Use integer type instead: sshsNodeGetInt().")
int16_t sshsNodeGetShort(sshsNode node, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* SSHS_H_ */
