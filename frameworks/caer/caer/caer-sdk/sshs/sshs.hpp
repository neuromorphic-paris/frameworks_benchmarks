#ifndef SSHS_HPP_
#define SSHS_HPP_

#include "sshs.h"

#include <string>

inline void sshsNodeCreate(sshsNode node, const char *key, bool defaultValue, int flags, const char *description) {
	sshsNodeCreateBool(node, key, defaultValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateLong(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue,
	int flags, const char *description) {
	sshsNodeCreateFloat(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	int flags, const char *description) {
	sshsNodeCreateDouble(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	int flags, const char *description) {
	sshsNodeCreateString(node, key, defaultValue, minLength, maxLength, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, const std::string &defaultValue, size_t minLength,
	size_t maxLength, int flags, const std::string &description) {
	sshsNodeCreateString(node, key, defaultValue.c_str(), minLength, maxLength, flags, description.c_str());
}

inline bool sshsNodePut(sshsNode node, const char *key, bool value) {
	return (sshsNodePutBool(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, int32_t value) {
	return (sshsNodePutInt(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, int64_t value) {
	return (sshsNodePutLong(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, float value) {
	return (sshsNodePutFloat(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, double value) {
	return (sshsNodePutDouble(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, const char *value) {
	return (sshsNodePutString(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, const std::string &value) {
	return (sshsNodePutString(node, key, value.c_str()));
}

// Additional getter for std::string.
inline std::string sshsNodeGetStdString(sshsNode node, const char *key) {
	char *str = sshsNodeGetString(node, key);
	std::string cppStr(str);
	free(str);
	return (cppStr);
}

// Additional updater for std::string.
inline bool sshsNodeUpdateReadOnlyAttribute(sshsNode node, const char *key, const std::string &value) {
	union sshs_node_attr_value newValue;
	newValue.string = const_cast<char *>(value.c_str());
	return (sshsNodeUpdateReadOnlyAttribute(node, key, SSHS_STRING, newValue));
}

inline void sshsNodeCreateAttributeListOptions(
	sshsNode node, const std::string &key, const std::string &listOptions, bool allowMultipleSelections) {
	sshsNodeCreateAttributeListOptions(node, key.c_str(), listOptions.c_str(), allowMultipleSelections);
}

inline void sshsNodeCreateAttributeFileChooser(
	sshsNode node, const std::string &key, const std::string &allowedExtensions) {
	sshsNodeCreateAttributeFileChooser(node, key.c_str(), allowedExtensions.c_str());
}

// std::string variants of node getters.
inline bool sshsExistsNode(sshs st, const std::string &nodePath) {
	return (sshsExistsNode(st, nodePath.c_str()));
}

inline sshsNode sshsGetNode(sshs st, const std::string &nodePath) {
	return (sshsGetNode(st, nodePath.c_str()));
}

inline bool sshsExistsRelativeNode(sshsNode node, const std::string &nodePath) {
	return (sshsExistsRelativeNode(node, nodePath.c_str()));
}

inline sshsNode sshsGetRelativeNode(sshsNode node, const std::string &nodePath) {
	return (sshsGetRelativeNode(node, nodePath.c_str()));
}

#endif /* SSHS_HPP_ */
