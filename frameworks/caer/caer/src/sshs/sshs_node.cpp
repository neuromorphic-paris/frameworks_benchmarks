#include "sshs_internal.hpp"

#include <algorithm>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/version.hpp>
#include <cfloat>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <utility>
#include <vector>

class sshs_node_attr {
private:
	struct sshs_node_attr_ranges ranges;
	int flags;
	std::string description;
	sshs_value value;

public:
	sshs_node_attr() : flags(SSHS_FLAGS_NORMAL) {
	}

	sshs_node_attr(const sshs_value &_value, const struct sshs_node_attr_ranges &_ranges, int _flags,
		const std::string &_description) :
		ranges(_ranges),
		flags(_flags),
		description(_description),
		value(_value) {
	}

	const sshs_value getValue() const noexcept {
		return (value);
	}

	void setValue(const sshs_value &v) noexcept {
		value = v;
	}

	const std::string &getDescription() const noexcept {
		return (description);
	}

	const struct sshs_node_attr_ranges &getRanges() const noexcept {
		return (ranges);
	}

	int getFlags() const noexcept {
		return (flags);
	}

	bool isFlagSet(int flag) const noexcept {
		return ((flags & flag) == flag);
	}
};

class sshs_node_listener {
private:
	sshsNodeChangeListener nodeChanged;
	void *userData;

public:
	sshs_node_listener(sshsNodeChangeListener _listener, void *_userData) :
		nodeChanged(_listener),
		userData(_userData) {
	}

	sshsNodeChangeListener getListener() const noexcept {
		return (nodeChanged);
	}

	void *getUserData() const noexcept {
		return (userData);
	}

	// Comparison operators.
	bool operator==(const sshs_node_listener &rhs) const noexcept {
		return ((nodeChanged == rhs.nodeChanged) && (userData == rhs.userData));
	}

	bool operator!=(const sshs_node_listener &rhs) const noexcept {
		return (!this->operator==(rhs));
	}
};

class sshs_attribute_listener {
private:
	sshsAttributeChangeListener attributeChanged;
	void *userData;

public:
	sshs_attribute_listener(sshsAttributeChangeListener _listener, void *_userData) :
		attributeChanged(_listener),
		userData(_userData) {
	}

	sshsAttributeChangeListener getListener() const noexcept {
		return (attributeChanged);
	}

	void *getUserData() const noexcept {
		return (userData);
	}

	// Comparison operators.
	bool operator==(const sshs_attribute_listener &rhs) const noexcept {
		return ((attributeChanged == rhs.attributeChanged) && (userData == rhs.userData));
	}

	bool operator!=(const sshs_attribute_listener &rhs) const noexcept {
		return (!this->operator==(rhs));
	}
};

static const std::regex sshsKeyRegexp("^[a-zA-Z-_\\d\\.]+$");

// struct for C compatibility
struct sshs_node {
public:
	std::string name;
	std::string path;
	sshs global;
	sshsNode parent;
	std::map<std::string, sshsNode> children;
	std::map<std::string, sshs_node_attr> attributes;
	std::vector<sshs_node_listener> nodeListeners;
	std::vector<sshs_attribute_listener> attrListeners;
	std::shared_timed_mutex traversal_lock;
	std::recursive_mutex node_lock;

	sshs_node(const std::string &_name, sshsNode _parent, sshs _global) :
		name(_name),
		global(_global),
		parent(_parent) {
		// Path is based on parent.
		if (_parent != nullptr) {
			path = parent->path + _name + "/";
		}
		else {
			// Or the root has an empty, constant path.
			path = "/";
		}
	}

	void createAttribute(const std::string &key, const sshs_value &defaultValue,
		const struct sshs_node_attr_ranges &ranges, int flags, const std::string &description) {
		// Check key name string against allowed characters via regexp.
		if (!std::regex_match(key, sshsKeyRegexp)) {
			boost::format errorMsg = boost::format("Invalid key name format: '%s'.") % key;

			sshsNodeError("sshsNodeCreateAttribute", key, defaultValue.getType(), errorMsg.str());
		}

		// Strings are special, their length range goes from 0 to SIZE_MAX, but we
		// have to restrict that to from 0 to INT32_MAX for languages like Java
		// that only support integer string lengths. It's also reasonable.
		if (defaultValue.getType() == SSHS_STRING) {
			if ((ranges.min.stringRange > INT32_MAX) || (ranges.max.stringRange > INT32_MAX)) {
				boost::format errorMsg = boost::format("minimum/maximum string range value outside allowed limits. "
													   "Please make sure the value is positive, between 0 and %d!")
										 % INT32_MAX;

				sshsNodeError("sshsNodeCreateAttribute", key, SSHS_STRING, errorMsg.str());
			}
		}

		// Check that value conforms to range limits.
		if (!defaultValue.inRange(ranges)) {
			// Fail on wrong default value. Must be within range!
			boost::format errorMsg = boost::format("default value '%s' is out of specified range. "
												   "Please make sure the default value is within the given range!")
									 % sshsHelperCppValueToStringConverter(defaultValue);

			sshsNodeError("sshsNodeCreateAttribute", key, defaultValue.getType(), errorMsg.str());
		}

		// Restrict NOTIFY_ONLY flag to booleans only, for button-like behavior.
		if ((flags & SSHS_FLAGS_NOTIFY_ONLY) && defaultValue.getType() != SSHS_BOOL) {
			// Fail on wrong notify-only flag usage.
			sshsNodeError("sshsNodeCreateAttribute", key, defaultValue.getType(),
				"the NOTIFY_ONLY flag is set, but "
				"attribute is not of type BOOL. Only "
				"booleans can have this flag set!");
		}

		// Restrict NOTIFY_ONLY flag to a default value of false only. This avoids
		// strange inverted logic for buttons.
		if ((flags & SSHS_FLAGS_NOTIFY_ONLY) && defaultValue.getBool() != false) {
			// Fail on wrong notify-only flag usage.
			sshsNodeError("sshsNodeCreateAttribute", key, defaultValue.getType(),
				"the NOTIFY_ONLY flag is set for this BOOL type attribute, only 'false' can be used as default value.");
		}

		sshs_node_attr newAttr(defaultValue, ranges, flags, description);

		std::lock_guard<std::recursive_mutex> lock(node_lock);

		// Add if not present. Else update value (below).
		if (!attributes.count(key)) {
			attributes[key] = newAttr;

			// Listener support. Call only on change, which is always the case here.
			sshsAttributeChangeListener globalListener = sshsGlobalAttributeListenerGetFunction(this->global);
			if (globalListener != nullptr) {
				// Global listener support.
				(*globalListener)(this, sshsGlobalAttributeListenerGetUserData(this->global), SSHS_ATTRIBUTE_ADDED,
					key.c_str(), newAttr.getValue().getType(), newAttr.getValue().toCUnion(true));
			}

			for (const auto &l : attrListeners) {
				(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_ADDED, key.c_str(),
					newAttr.getValue().getType(), newAttr.getValue().toCUnion(true));
			}
		}
		else {
			const sshs_node_attr &oldAttr  = attributes[key];
			const sshs_value &oldAttrValue = oldAttr.getValue();

			// To simplify things, we don't support multiple types per key (though the API does).
			if (oldAttrValue.getType() != newAttr.getValue().getType()) {
				boost::format errorMsg
					= boost::format("value with this key already exists and has a different type of '%s'")
					  % sshsHelperCppTypeToStringConverter(oldAttrValue.getType());

				sshsNodeError("sshsNodeCreateAttribute", key, newAttr.getValue().getType(), errorMsg.str());
			}

			// Check if the current value is still fine and within range; if it is
			// we use it, else just use the new value.
			if (oldAttrValue.inRange(ranges)) {
				// Only update value, then use newAttr. No listeners called since this
				// is by definition the old value and as such nothing can have changed.
				newAttr.setValue(oldAttrValue);
				attributes[key] = newAttr;
			}
			else {
				// If the old value is not in range anymore, the new value must be different,
				// since it is guaranteed to be inside the new range. So we call the listeners.
				attributes[key] = newAttr;

				// Listener support. Call only on change, which is always the case here.
				sshsAttributeChangeListener globalListener = sshsGlobalAttributeListenerGetFunction(this->global);
				if (globalListener != nullptr) {
					// Global listener support.
					(*globalListener)(this, sshsGlobalAttributeListenerGetUserData(this->global),
						SSHS_ATTRIBUTE_MODIFIED, key.c_str(), newAttr.getValue().getType(),
						newAttr.getValue().toCUnion(true));
				}

				for (const auto &l : attrListeners) {
					(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_MODIFIED, key.c_str(),
						newAttr.getValue().getType(), newAttr.getValue().toCUnion(true));
				}
			}
		}
	}

	void removeAttribute(const std::string &key, enum sshs_node_attr_value_type type) {
		std::lock_guard<std::recursive_mutex> lock(node_lock);

		if (!attributeExists(key, type)) {
			// Ignore calls on non-existent attributes for remove, as it is used
			// to clean-up attributes before re-creating them in a consistent way.
			return;
		}

		sshs_node_attr &attr = attributes[key];

		// Listener support.
		sshsAttributeChangeListener globalListener = sshsGlobalAttributeListenerGetFunction(this->global);
		if (globalListener != nullptr) {
			// Global listener support.
			(*globalListener)(this, sshsGlobalAttributeListenerGetUserData(this->global), SSHS_ATTRIBUTE_REMOVED,
				key.c_str(), attr.getValue().getType(), attr.getValue().toCUnion(true));
		}

		for (const auto &l : attrListeners) {
			(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_REMOVED, key.c_str(), attr.getValue().getType(),
				attr.getValue().toCUnion(true));
		}

		// Remove attribute from node.
		attributes.erase(key);
	}

	void removeAllAttributes() {
		std::lock_guard<std::recursive_mutex> lock(node_lock);

		for (const auto &attr : attributes) {
			sshsAttributeChangeListener globalListener = sshsGlobalAttributeListenerGetFunction(this->global);
			if (globalListener != nullptr) {
				// Global listener support.
				(*globalListener)(this, sshsGlobalAttributeListenerGetUserData(this->global), SSHS_ATTRIBUTE_REMOVED,
					attr.first.c_str(), attr.second.getValue().getType(), attr.second.getValue().toCUnion(true));
			}

			for (const auto &l : attrListeners) {
				(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_REMOVED, attr.first.c_str(),
					attr.second.getValue().getType(), attr.second.getValue().toCUnion(true));
			}
		}

		attributes.clear();
	}

	bool attributeExists(const std::string &key, enum sshs_node_attr_value_type type) {
		std::lock_guard<std::recursive_mutex> lockNode(node_lock);

		if ((!attributes.count(key)) || (attributes[key].getValue().getType() != type)) {
			errno = ENOENT;
			return (false);
		}

		// The specified attribute exists and has a matching type.
		return (true);
	}

	const sshs_value getAttribute(const std::string &key, enum sshs_node_attr_value_type type) {
		std::lock_guard<std::recursive_mutex> lockNode(node_lock);

		if (!attributeExists(key, type)) {
			sshsNodeErrorNoAttribute("sshsNodeGetAttribute", key, type);
		}

		// Return a copy of the final value.
		return (attributes[key].getValue());
	}

	bool putAttribute(const std::string &key, const sshs_value &value, bool forceReadOnlyUpdate = false) {
		std::lock_guard<std::recursive_mutex> lockNode(node_lock);

		if (!attributeExists(key, value.getType())) {
			sshsNodeErrorNoAttribute("sshsNodePutAttribute", key, value.getType());
		}

		sshs_node_attr &attr = attributes[key];

		// Value must be present, so update old one, after checking range and flags.
		if ((!forceReadOnlyUpdate && attr.isFlagSet(SSHS_FLAGS_READ_ONLY))
			|| (forceReadOnlyUpdate && !attr.isFlagSet(SSHS_FLAGS_READ_ONLY))) {
			// Read-only flag set, cannot put new value!
			errno = EPERM;
			return (false);
		}

		if (!value.inRange(attr.getRanges())) {
			// New value out of range, cannot put new value!
			errno = ERANGE;
			return (false);
		}

		// Key and valueType have to be the same, so we first check that the
		// actual values, that we want to update, are different. If not, there's
		// nothing to do, no listeners to call, and it doesn't make sense to
		// set the value twice to the same content.
		if (attr.getValue() != value) {
			if (!attr.isFlagSet(SSHS_FLAGS_NOTIFY_ONLY)) {
				// Only update stored value if NOTIFY_ONLY is not set.
				attr.setValue(value);
			}

			// Call the appropriate listeners, on change only, which is always
			// true at this point. We use the new value directly, to support
			// the case where NOTIFY_ONLY prevented the updated of the stored
			// attribute, but the call to the listeners has to happen with the
			// new value (call-listeners-only behavior).
			sshsAttributeChangeListener globalListener = sshsGlobalAttributeListenerGetFunction(this->global);
			if (globalListener != nullptr) {
				// Global listener support.
				(*globalListener)(this, sshsGlobalAttributeListenerGetUserData(this->global), SSHS_ATTRIBUTE_MODIFIED,
					key.c_str(), value.getType(), value.toCUnion(true));
			}

			for (const auto &l : attrListeners) {
				(*l.getListener())(
					this, l.getUserData(), SSHS_ATTRIBUTE_MODIFIED, key.c_str(), value.getType(), value.toCUnion(true));
			}
		}

		return (true);
	}
};

static void sshsNodeDestroy(sshsNode node);
static void sshsNodeRemoveSubTree(sshsNode node);
static void sshsNodeRemoveChild(sshsNode node, const std::string childName);
static void sshsNodeRemoveAllChildren(sshsNode node);

#define XML_INDENT_SPACES 4

static bool sshsNodeToXML(sshsNode node, int fd, bool recursive);
static boost::property_tree::ptree sshsNodeGenerateXML(sshsNode node, bool recursive);
static bool sshsNodeFromXML(sshsNode node, int fd, bool recursive, bool strict);
static void sshsNodeConsumeXML(sshsNode node, const boost::property_tree::ptree &content, bool recursive);

sshsNode sshsNodeNew(const char *nodeName, sshsNode parent, sshs global) {
	sshsNode newNode = new (std::nothrow) sshs_node(nodeName, parent, global);
	sshsMemoryCheck(newNode, __func__);

	return (newNode);
}

// children, attributes, and listeners must be cleaned up prior to this call.
static void sshsNodeDestroy(sshsNode node) {
	delete node;
}

const char *sshsNodeGetName(sshsNode node) {
	return (node->name.c_str());
}

const char *sshsNodeGetPath(sshsNode node) {
	return (node->path.c_str());
}

sshsNode sshsNodeGetParent(sshsNode node) {
	return (node->parent);
}

sshs sshsNodeGetGlobal(sshsNode node) {
	return (node->global);
}

sshsNode sshsNodeAddChild(sshsNode node, const char *childName) {
	std::unique_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	if (node->children.count(childName)) {
		return (node->children[childName]);
	}
	else {
		// Create new child node with appropriate name and parent.
		sshsNode newChild = sshsNodeNew(childName, node, node->global);

		// No node present, let's add it.
		node->children[childName] = newChild;

		// Listener support (only on new addition!).
		std::lock_guard<std::recursive_mutex> nodeLock(node->node_lock);

		sshsNodeChangeListener globalListener = sshsGlobalNodeListenerGetFunction(node->global);
		if (globalListener != nullptr) {
			// Global listener support.
			(*globalListener)(node, sshsGlobalNodeListenerGetUserData(node->global), SSHS_CHILD_NODE_ADDED, childName);
		}

		for (const auto &l : node->nodeListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_ADDED, childName);
		}

		return (newChild);
	}
}

sshsNode sshsNodeGetChild(sshsNode node, const char *childName) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	if (node->children.count(childName)) {
		return (node->children[childName]);
	}
	else {
		return (nullptr);
	}
}

sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	size_t childrenCount = node->children.size();

	// If none, exit gracefully.
	if (childrenCount == 0) {
		*numChildren = 0;
		return (nullptr);
	}

	sshsNode *children = (sshsNode *) malloc(childrenCount * sizeof(*children));
	sshsMemoryCheck(children, __func__);

	size_t i = 0;
	for (const auto &n : node->children) {
		children[i++] = n.second;
	}

	*numChildren = childrenCount;
	return (children);
}

void sshsNodeAddNodeListener(sshsNode node, void *userData, sshsNodeChangeListener node_changed) {
	sshs_node_listener listener(node_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	if (!findBool(node->nodeListeners.begin(), node->nodeListeners.end(), listener)) {
		node->nodeListeners.push_back(listener);
	}
}

void sshsNodeRemoveNodeListener(sshsNode node, void *userData, sshsNodeChangeListener node_changed) {
	sshs_node_listener listener(node_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	node->nodeListeners.erase(
		std::remove(node->nodeListeners.begin(), node->nodeListeners.end(), listener), node->nodeListeners.end());
}

void sshsNodeRemoveAllNodeListeners(sshsNode node) {
	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	node->nodeListeners.clear();
}

void sshsNodeAddAttributeListener(sshsNode node, void *userData, sshsAttributeChangeListener attribute_changed) {
	sshs_attribute_listener listener(attribute_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	if (!findBool(node->attrListeners.begin(), node->attrListeners.end(), listener)) {
		node->attrListeners.push_back(listener);
	}
}

void sshsNodeRemoveAttributeListener(sshsNode node, void *userData, sshsAttributeChangeListener attribute_changed) {
	sshs_attribute_listener listener(attribute_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	node->attrListeners.erase(
		std::remove(node->attrListeners.begin(), node->attrListeners.end(), listener), node->attrListeners.end());
}

void sshsNodeRemoveAllAttributeListeners(sshsNode node) {
	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	node->attrListeners.clear();
}

void sshsNodeClearSubTree(sshsNode startNode, bool clearStartNode) {
	std::lock_guard<std::recursive_mutex> lockNode(startNode->node_lock);

	// Clear this node's attributes, if requested.
	if (clearStartNode) {
		sshsNodeRemoveAllAttributes(startNode);
		sshsNodeRemoveAllAttributeListeners(startNode);
	}

	// Recurse down children and remove all attributes.
	size_t numChildren;
	sshsNode *children = sshsNodeGetChildren(startNode, &numChildren);

	for (size_t i = 0; i < numChildren; i++) {
		sshsNodeClearSubTree(children[i], true);
	}

	free(children);
}

// Eliminates this node and any children. Nobody can have a reference, or
// be in the process of getting one, to this node or any of its children.
// You need to make sure of this in your application!
void sshsNodeRemoveNode(sshsNode node) {
	{
		std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

		// Now we can clear the subtree from all attribute related data.
		sshsNodeClearSubTree(node, true);

		// And finally remove the node related data and the node itself.
		sshsNodeRemoveSubTree(node);
	}

	// If this is the root node (parent == nullptr), it isn't fully removed.
	if (node->parent != nullptr) {
		// Unlink this node from the parent.
		// This also destroys the memory associated with the node.
		// Any later access is illegal!
		sshsNodeRemoveChild(node->parent, node->name);
	}
}

static void sshsNodeRemoveSubTree(sshsNode node) {
	// Recurse down first, we remove from the bottom up.
	size_t numChildren;
	sshsNode *children = sshsNodeGetChildren(node, &numChildren);

	for (size_t i = 0; i < numChildren; i++) {
		sshsNodeRemoveSubTree(children[i]);
	}

	free(children);

	// Delete node listeners and children.
	sshsNodeRemoveAllChildren(node);
	sshsNodeRemoveAllNodeListeners(node);
}

// children, attributes, and listeners for the child to be removed
// must be cleaned up prior to this call.
static void sshsNodeRemoveChild(sshsNode node, const std::string childName) {
	std::unique_lock<std::shared_timed_mutex> lock(node->traversal_lock);
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->children.count(childName)) {
		// Verify that a valid node exists, else simply return
		// without doing anything. Node was already deleted.
		return;
	}

	// Listener support.
	sshsNodeChangeListener globalListener = sshsGlobalNodeListenerGetFunction(node->global);
	if (globalListener != nullptr) {
		// Global listener support.
		(*globalListener)(
			node, sshsGlobalNodeListenerGetUserData(node->global), SSHS_CHILD_NODE_REMOVED, childName.c_str());
	}

	for (const auto &l : node->nodeListeners) {
		(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_REMOVED, childName.c_str());
	}

	sshsNodeDestroy(node->children[childName]);

	// Remove attribute from node.
	node->children.erase(childName);
}

// children, attributes, and listeners for the children to be removed
// must be cleaned up prior to this call.
static void sshsNodeRemoveAllChildren(sshsNode node) {
	std::unique_lock<std::shared_timed_mutex> lock(node->traversal_lock);
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	for (const auto &child : node->children) {
		sshsNodeChangeListener globalListener = sshsGlobalNodeListenerGetFunction(node->global);
		if (globalListener != nullptr) {
			// Global listener support.
			(*globalListener)(
				node, sshsGlobalNodeListenerGetUserData(node->global), SSHS_CHILD_NODE_REMOVED, child.first.c_str());
		}

		for (const auto &l : node->nodeListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_REMOVED, child.first.c_str());
		}

		sshsNodeDestroy(child.second);
	}

	node->children.clear();
}

void sshsNodeCreateAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value defaultValue, const struct sshs_node_attr_ranges ranges, int flags,
	const char *description) {
	sshs_value val;
	val.fromCUnion(defaultValue, type);

	node->createAttribute(key, val, ranges, flags, description);
}

void sshsNodeRemoveAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	node->removeAttribute(key, type);
}

void sshsNodeRemoveAllAttributes(sshsNode node) {
	node->removeAllAttributes();
}

bool sshsNodeAttributeExists(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	return (node->attributeExists(key, type));
}

bool sshsNodePutAttribute(
	sshsNode node, const char *key, enum sshs_node_attr_value_type type, union sshs_node_attr_value value) {
	sshs_value val;
	val.fromCUnion(value, type);

	return (node->putAttribute(key, val));
}

union sshs_node_attr_value sshsNodeGetAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	return (node->getAttribute(key, type).toCUnion());
}

bool sshsNodeUpdateReadOnlyAttribute(
	sshsNode node, const char *key, enum sshs_node_attr_value_type type, union sshs_node_attr_value value) {
	sshs_value val;
	val.fromCUnion(value, type);

	return (node->putAttribute(key, val, true));
}

void sshsNodeCreateBool(sshsNode node, const char *key, bool defaultValue, int flags, const char *description) {
	sshs_value uValue;
	uValue.setBool(defaultValue);

	// No range for booleans.
	struct sshs_node_attr_ranges ranges;
	ranges.min.ilongRange = 0;
	ranges.max.ilongRange = 0;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutBool(sshsNode node, const char *key, bool value) {
	sshs_value uValue;
	uValue.setBool(value);

	return (node->putAttribute(key, uValue));
}

bool sshsNodeGetBool(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_BOOL).getBool());
}

void sshsNodeCreateInt(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setInt(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.iintRange = minValue;
	ranges.max.iintRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutInt(sshsNode node, const char *key, int32_t value) {
	sshs_value uValue;
	uValue.setInt(value);

	return (node->putAttribute(key, uValue));
}

int32_t sshsNodeGetInt(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_INT).getInt());
}

void sshsNodeCreateLong(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setLong(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ilongRange = minValue;
	ranges.max.ilongRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutLong(sshsNode node, const char *key, int64_t value) {
	sshs_value uValue;
	uValue.setLong(value);

	return (node->putAttribute(key, uValue));
}

int64_t sshsNodeGetLong(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_LONG).getLong());
}

void sshsNodeCreateFloat(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue, int flags,
	const char *description) {
	sshs_value uValue;
	uValue.setFloat(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ffloatRange = minValue;
	ranges.max.ffloatRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutFloat(sshsNode node, const char *key, float value) {
	sshs_value uValue;
	uValue.setFloat(value);

	return (node->putAttribute(key, uValue));
}

float sshsNodeGetFloat(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_FLOAT).getFloat());
}

void sshsNodeCreateDouble(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setDouble(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ddoubleRange = minValue;
	ranges.max.ddoubleRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutDouble(sshsNode node, const char *key, double value) {
	sshs_value uValue;
	uValue.setDouble(value);

	return (node->putAttribute(key, uValue));
}

double sshsNodeGetDouble(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_DOUBLE).getDouble());
}

void sshsNodeCreateString(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setString(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.stringRange = minLength;
	ranges.max.stringRange = maxLength;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutString(sshsNode node, const char *key, const char *value) {
	sshs_value uValue;
	uValue.setString(value);

	return (node->putAttribute(key, uValue));
}

// This is a copy of the string on the heap, remember to free() when done!
char *sshsNodeGetString(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_STRING).toCUnion().string);
}

bool sshsNodeExportNodeToXML(sshsNode node, int fd) {
	return (sshsNodeToXML(node, fd, false));
}

bool sshsNodeExportSubTreeToXML(sshsNode node, int fd) {
	return (sshsNodeToXML(node, fd, true));
}

static bool sshsNodeToXML(sshsNode node, int fd, bool recursive) {
	boost::iostreams::stream_buffer<boost::iostreams::file_descriptor_sink> fdStream(
		fd, boost::iostreams::never_close_handle);

	std::ostream outStream(&fdStream);

	boost::property_tree::ptree xmlTree;

	// Add main SSHS node and version.
	xmlTree.put("sshs.<xmlattr>.version", "1.0");

	// Generate recursive XML for all nodes.
	xmlTree.put_child("sshs.node", sshsNodeGenerateXML(node, recursive));

	try {
#if defined(BOOST_VERSION) && (BOOST_VERSION / 100000) == 1 && (BOOST_VERSION / 100 % 1000) < 56
		boost::property_tree::xml_parser::xml_writer_settings<boost::property_tree::ptree::key_type::value_type>
			xmlIndent(' ', XML_INDENT_SPACES);
#else
		boost::property_tree::xml_parser::xml_writer_settings<boost::property_tree::ptree::key_type> xmlIndent(
			' ', XML_INDENT_SPACES);
#endif

		// We manually call write_xml_element() here instead of write_xml() because
		// the latter always prepends the XML declaration, which we don't want.
		boost::property_tree::xml_parser::write_xml_element(
			outStream, boost::property_tree::ptree::key_type(), xmlTree, -1, xmlIndent);
		if (!outStream) {
			throw boost::property_tree::xml_parser_error("write error.", std::string(), 0);
		}
	}
	catch (const boost::property_tree::xml_parser_error &ex) {
		const std::string errorMsg = std::string("Failed to write XML to output stream. Exception: ") + ex.what();
		(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str(), false);
		return (false);
	}

	return (true);
}

static boost::property_tree::ptree sshsNodeGenerateXML(sshsNode node, bool recursive) {
	boost::property_tree::ptree content;

	// First recurse down all the way to the leaf children, where attributes are kept.
	if (recursive) {
		std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

		for (const auto &child : node->children) {
			auto childContent = sshsNodeGenerateXML(child.second, recursive);

			if (!childContent.empty()) {
				// Only add in nodes that have content (attributes or other nodes).
				content.add_child("node", childContent);
			}
		}
	}

	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	// Then it's attributes (key:value pairs).
	auto attrFirstIterator = content.begin();
	for (const auto &attr : node->attributes) {
		// If an attribute is marked NO_EXPORT, we skip it.
		if (attr.second.isFlagSet(SSHS_FLAGS_NO_EXPORT)) {
			continue;
		}

		const std::string type  = sshsHelperCppTypeToStringConverter(attr.second.getValue().getType());
		const std::string value = sshsHelperCppValueToStringConverter(attr.second.getValue());

		boost::property_tree::ptree attrNode(value);
		attrNode.put("<xmlattr>.key", attr.first);
		attrNode.put("<xmlattr>.type", type);

		// Attributes should be in order, but at the start of the node (before
		// other nodes), so we insert() them instead of just adding to the back.
		attrFirstIterator
			= content.insert(attrFirstIterator, boost::property_tree::ptree::value_type("attr", attrNode));
		attrFirstIterator++;
	}

	if (!content.empty()) {
		// Only add elements (name, path) if the node has any content
		// (attributes or othern odes), so that empty nodes are really empty.
		content.put("<xmlattr>.name", node->name);
		content.put("<xmlattr>.path", node->path);
	}

	return (content);
}

bool sshsNodeImportNodeFromXML(sshsNode node, int fd, bool strict) {
	return (sshsNodeFromXML(node, fd, false, strict));
}

bool sshsNodeImportSubTreeFromXML(sshsNode node, int fd, bool strict) {
	return (sshsNodeFromXML(node, fd, true, strict));
}

static std::vector<std::reference_wrapper<const boost::property_tree::ptree>> sshsNodeXMLFilterChildNodes(
	const boost::property_tree::ptree &content, const std::string &name) {
	std::vector<std::reference_wrapper<const boost::property_tree::ptree>> result;

	for (const auto &elem : content) {
		if (elem.first == name) {
			result.push_back(elem.second);
		}
	}

	return (result);
}

static bool sshsNodeFromXML(sshsNode node, int fd, bool recursive, bool strict) {
	boost::iostreams::stream_buffer<boost::iostreams::file_descriptor_source> fdStream(
		fd, boost::iostreams::never_close_handle);

	std::istream inStream(&fdStream);

	boost::property_tree::ptree xmlTree;

	try {
		boost::property_tree::xml_parser::read_xml(
			inStream, xmlTree, boost::property_tree::xml_parser::trim_whitespace);
	}
	catch (const boost::property_tree::xml_parser_error &ex) {
		const std::string errorMsg = std::string("Failed to load XML from input stream. Exception: ") + ex.what();
		(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str(), false);
		return (false);
	}

	// Check name and version for compliance.
	try {
		const auto sshsVersion = xmlTree.get<std::string>("sshs.<xmlattr>.version");
		if (sshsVersion != "1.0") {
			throw boost::property_tree::ptree_error("unsupported SSHS version (supported: '1.0').");
		}
	}
	catch (const boost::property_tree::ptree_error &ex) {
		const std::string errorMsg = std::string("Invalid XML content. Exception: ") + ex.what();
		(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str(), false);
		return (false);
	}

	auto root = sshsNodeXMLFilterChildNodes(xmlTree.get_child("sshs"), "node");

	if (root.size() != 1) {
		(*sshsGetGlobalErrorLogCallback())("Multiple or no root child nodes present.", false);
		return (false);
	}

	auto &rootNode = root.front().get();

	// Strict mode: check if names match.
	if (strict) {
		try {
			const auto rootNodeName = rootNode.get<std::string>("<xmlattr>.name");

			if (rootNodeName != node->name) {
				throw boost::property_tree::ptree_error("names don't match (required in 'strict' mode).");
			}
		}
		catch (const boost::property_tree::ptree_error &ex) {
			const std::string errorMsg = std::string("Invalid root node. Exception: ") + ex.what();
			(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str(), false);
			return (false);
		}
	}

	sshsNodeConsumeXML(node, rootNode, recursive);

	return (true);
}

static void sshsNodeConsumeXML(sshsNode node, const boost::property_tree::ptree &content, bool recursive) {
	auto attributes = sshsNodeXMLFilterChildNodes(content, "attr");

	for (auto &attr : attributes) {
		// Check that the proper attributes exist.
		const auto key  = attr.get().get("<xmlattr>.key", "");
		const auto type = attr.get().get("<xmlattr>.type", "");

		if (key.empty() || type.empty()) {
			continue;
		}

		// Get the needed values.
		const auto value = attr.get().get_value("");

		if (!sshsNodeStringToAttributeConverter(node, key.c_str(), type.c_str(), value.c_str())) {
			// Ignore read-only/range errors.
			if (errno == EPERM || errno == ERANGE) {
				continue;
			}

			boost::format errorMsg
				= boost::format("failed to convert attribute from XML, value string was '%s'") % value;

			sshsNodeError("sshsNodeConsumeXML", key, sshsHelperCppStringToTypeConverter(type), errorMsg.str(), false);
		}
	}

	if (recursive) {
		auto children = sshsNodeXMLFilterChildNodes(content, "node");

		for (auto &child : children) {
			// Check that the proper attributes exist.
			const auto childName = child.get().get("<xmlattr>.name", "");

			if (childName.empty()) {
				continue;
			}

			// Get the child node.
			sshsNode childNode = sshsNodeGetChild(node, childName.c_str());

			// If not existing, try to create.
			if (childNode == nullptr) {
				childNode = sshsNodeAddChild(node, childName.c_str());
			}

			// And call recursively.
			sshsNodeConsumeXML(childNode, child.get(), recursive);
		}
	}
}

// For more precise failure reason, look at errno.
bool sshsNodeStringToAttributeConverter(sshsNode node, const char *key, const char *typeStr, const char *valueStr) {
	// Parse the values according to type and put them in the node.
	enum sshs_node_attr_value_type type;
	type = sshsHelperCppStringToTypeConverter(typeStr);

	if (type == SSHS_UNKNOWN) {
		errno = EINVAL;
		return (false);
	}

	if ((type == SSHS_STRING) && (valueStr == nullptr)) {
		// Empty string.
		valueStr = "";
	}

	sshs_value value;
	try {
		value = sshsHelperCppStringToValueConverter(type, valueStr);
	}
	catch (const std::invalid_argument &) {
		errno = EINVAL;
		return (false);
	}
	catch (const std::out_of_range &) {
		errno = EINVAL;
		return (false);
	}

	// IFF attribute already exists, we update it using sshsNodePut(), else
	// we create the attribute with maximum range and a default description.
	// These XMl-loaded attributes are also marked NO_EXPORT.
	// This happens on XML load only. More restrictive ranges and flags can be
	// enabled later by calling sshsNodeCreate*() again as needed.
	bool result = false;

	if (node->attributeExists(key, type)) {
		result = node->putAttribute(key, value);
	}
	else {
		// Create never fails, it may exit the program, but not fail!
		result = true;
		struct sshs_node_attr_ranges ranges;

		switch (type) {
			case SSHS_BOOL:
				ranges.min.ilongRange = 0;
				ranges.max.ilongRange = 0;
				node->createAttribute(
					key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_INT:
				ranges.min.iintRange = INT32_MIN;
				ranges.max.iintRange = INT32_MAX;
				node->createAttribute(
					key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_LONG:
				ranges.min.ilongRange = INT64_MIN;
				ranges.max.ilongRange = INT64_MAX;
				node->createAttribute(
					key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_FLOAT:
				ranges.min.ffloatRange = -FLT_MAX;
				ranges.max.ffloatRange = FLT_MAX;
				node->createAttribute(
					key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_DOUBLE:
				ranges.min.ddoubleRange = -DBL_MAX;
				ranges.max.ddoubleRange = DBL_MAX;
				node->createAttribute(
					key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_STRING:
				ranges.min.stringRange = 0;
				ranges.max.stringRange = INT32_MAX;
				node->createAttribute(
					key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_UNKNOWN:
				errno  = EINVAL;
				result = false;
				break;
		}
	}

	return (result);
}

// Remember to free the resulting array.
const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	if (node->children.empty()) {
		*numNames = 0;
		errno     = ENOENT;
		return (nullptr);
	}

	size_t numChildren = node->children.size();

	// Nodes can be deleted, so we copy the string's contents into
	// memory that will be guaranteed to exist.
	size_t childNamesLength = 0;

	for (const auto &child : node->children) {
		// Length plus one for terminating NUL byte.
		childNamesLength += child.first.length() + 1;
	}

	char **childNames = (char **) malloc((numChildren * sizeof(char *)) + childNamesLength);
	sshsMemoryCheck(childNames, __func__);

	size_t offset = (numChildren * sizeof(char *));

	size_t i = 0;
	for (const auto &child : node->children) {
		// We have all the memory, so now copy the strings over and set the
		// pointers as if an array of pointers was the only result.
		childNames[i] = (char *) (((uint8_t *) childNames) + offset);
		strcpy(childNames[i], child.first.c_str());

		// Length plus one for terminating NUL byte.
		offset += child.first.length() + 1;
		i++;
	}

	*numNames = numChildren;
	return (const_cast<const char **>(childNames));
}

// Remember to free the resulting array.
const char **sshsNodeGetAttributeKeys(sshsNode node, size_t *numKeys) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (node->attributes.empty()) {
		*numKeys = 0;
		errno    = ENOENT;
		return (nullptr);
	}

	size_t numAttributes = node->attributes.size();

	// Attributes can be deleted, so we copy the key string's contents into
	// memory that will be guaranteed to exist.
	size_t attributeKeysLength = 0;

	for (const auto &attr : node->attributes) {
		// Length plus one for terminating NUL byte.
		attributeKeysLength += attr.first.length() + 1;
	}

	char **attributeKeys = (char **) malloc((numAttributes * sizeof(char *)) + attributeKeysLength);
	sshsMemoryCheck(attributeKeys, __func__);

	size_t offset = (numAttributes * sizeof(char *));

	size_t i = 0;
	for (const auto &attr : node->attributes) {
		// We have all the memory, so now copy the strings over and set the
		// pointers as if an array of pointers was the only result.
		attributeKeys[i] = (char *) (((uint8_t *) attributeKeys) + offset);
		strcpy(attributeKeys[i], attr.first.c_str());

		// Length plus one for terminating NUL byte.
		offset += attr.first.length() + 1;
		i++;
	}

	*numKeys = numAttributes;
	return (const_cast<const char **>(attributeKeys));
}

// Remember to free the resulting array.
enum sshs_node_attr_value_type sshsNodeGetAttributeType(sshsNode node, const char *key) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributes.count(key)) {
		errno = ENOENT;
		return (SSHS_UNKNOWN);
	}

	// There is exactly one type for one specific attribute key.
	return (node->attributes[key].getValue().getType());
}

struct sshs_node_attr_ranges sshsNodeGetAttributeRanges(
	sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributeExists(key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeRanges", key, type);
	}

	return (node->attributes[key].getRanges());
}

int sshsNodeGetAttributeFlags(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributeExists(key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeFlags", key, type);
	}

	return (node->attributes[key].getFlags());
}

// Remember to free the resulting string.
char *sshsNodeGetAttributeDescription(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributeExists(key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeDescription", key, type);
	}

	char *descriptionCopy = strdup(node->attributes[key].getDescription().c_str());
	sshsMemoryCheck(descriptionCopy, __func__);

	return (descriptionCopy);
}

void sshsNodeCreateAttributeListOptions(
	sshsNode node, const char *key, const char *listOptions, bool allowMultipleSelections) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributeExists(key, SSHS_STRING)) {
		sshsNodeErrorNoAttribute("sshsNodeCreateAttributeListOptions", key, SSHS_STRING);
	}

	std::string fullKey(key);
	fullKey += "ListOptions";

	if (allowMultipleSelections) {
		fullKey += "Multi";
	}

	sshsNodeCreateString(node, fullKey.c_str(), listOptions, 1, INT32_MAX, SSHS_FLAGS_READ_ONLY,
		"Comma separated list of possible associated attribute values.");
}

void sshsNodeCreateAttributeFileChooser(sshsNode node, const char *key, const char *allowedExtensions) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributeExists(key, SSHS_STRING)) {
		sshsNodeErrorNoAttribute("sshsNodeCreateAttributeFileChooser", key, SSHS_STRING);
	}

	std::string fullKey(key);
	fullKey += "FileChooser";

	sshsNodeCreateString(node, fullKey.c_str(), allowedExtensions, 1, INT32_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Comma separated list of allowed extensions for the file chooser dialog.");
}

// Deprecated.
void sshsNodeCreateByte(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags, description);
}

bool sshsNodePutByte(sshsNode node, const char *key, int8_t value) {
	return (sshsNodePutInt(node, key, value));
}

int8_t sshsNodeGetByte(sshsNode node, const char *key) {
	return ((int8_t) sshsNodeGetInt(node, key));
}

void sshsNodeCreateShort(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags, description);
}

bool sshsNodePutShort(sshsNode node, const char *key, int16_t value) {
	return (sshsNodePutInt(node, key, value));
}

int16_t sshsNodeGetShort(sshsNode node, const char *key) {
	return ((int16_t) sshsNodeGetInt(node, key));
}
