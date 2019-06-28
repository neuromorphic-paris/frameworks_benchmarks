#ifndef SSHS_INTERNAL_HPP_
#define SSHS_INTERNAL_HPP_

// Implementation relevant common includes.
#include "caer-sdk/sshs/sshs.h"

#include <boost/format.hpp>
#include <cstring>
#include <stdexcept>
#include <string>

// C linkage to guarantee no name mangling.
extern "C" {
// Internal node functions.
sshsNode sshsNodeNew(const char *nodeName, sshsNode parent, sshs global);
/**
 * This returns a reference to a node, and as such must be carefully mediated with
 * any sshsNodeRemoveNode() calls.
 */
sshsNode sshsNodeAddChild(sshsNode node, const char *childName);
/**
 * This returns a reference to a node, and as such must be carefully mediated with
 * any sshsNodeRemoveNode() calls.
 */
sshsNode sshsNodeGetChild(sshsNode node, const char *childName);
/**
 * Get link to global SSHS tree.
 */
sshs sshsNodeGetGlobal(sshsNode node);

// Internal global functions.
sshsNodeChangeListener sshsGlobalNodeListenerGetFunction(sshs tree);
void *sshsGlobalNodeListenerGetUserData(sshs tree);
sshsAttributeChangeListener sshsGlobalAttributeListenerGetFunction(sshs tree);
void *sshsGlobalAttributeListenerGetUserData(sshs tree);
}

template<typename InIter, typename Elem> static inline bool findBool(InIter begin, InIter end, const Elem &val) {
	const auto result = std::find(begin, end, val);

	if (result == end) {
		return (false);
	}

	return (true);
}

// Terminate process on failed memory allocation.
template<typename T> static inline void sshsMemoryCheck(T *ptr, const std::string &funcName) {
	if (ptr == nullptr) {
		boost::format errorMsg = boost::format("%s(): unable to allocate memory.") % funcName;

		(*sshsGetGlobalErrorLogCallback())(errorMsg.str().c_str(), true);
	}
}

class sshs_value {
private:
	enum sshs_node_attr_value_type type;
	union {
		bool boolean;
		int32_t iint;
		int64_t ilong;
		float ffloat;
		double ddouble;
	} value;
	std::string valueString; // Separate for easy memory management.

public:
	sshs_value() {
		type        = SSHS_UNKNOWN;
		value.ilong = 0;
	}

	enum sshs_node_attr_value_type getType() const noexcept {
		return (type);
	}

	bool getBool() const {
		if (type != SSHS_BOOL) {
			throw std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.boolean);
	}

	void setBool(bool v) noexcept {
		type          = SSHS_BOOL;
		value.boolean = v;
	}

	int32_t getInt() const {
		if (type != SSHS_INT) {
			throw std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.iint);
	}

	void setInt(int32_t v) noexcept {
		type       = SSHS_INT;
		value.iint = v;
	}

	int64_t getLong() const {
		if (type != SSHS_LONG) {
			throw std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ilong);
	}

	void setLong(int64_t v) noexcept {
		type        = SSHS_LONG;
		value.ilong = v;
	}

	float getFloat() const {
		if (type != SSHS_FLOAT) {
			throw std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ffloat);
	}

	void setFloat(float v) noexcept {
		type         = SSHS_FLOAT;
		value.ffloat = v;
	}

	double getDouble() const {
		if (type != SSHS_DOUBLE) {
			throw std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ddouble);
	}

	void setDouble(double v) noexcept {
		type          = SSHS_DOUBLE;
		value.ddouble = v;
	}

	const std::string &getString() const {
		if (type != SSHS_STRING) {
			throw std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (valueString);
	}

	void setString(const std::string &v) noexcept {
		type        = SSHS_STRING;
		valueString = v;
	}

	bool inRange(const struct sshs_node_attr_ranges &ranges) const {
		switch (type) {
			case SSHS_BOOL:
				// No check for bool, because no range exists.
				return (true);

			case SSHS_INT:
				return (value.iint >= ranges.min.iintRange && value.iint <= ranges.max.iintRange);

			case SSHS_LONG:
				return (value.ilong >= ranges.min.ilongRange && value.ilong <= ranges.max.ilongRange);

			case SSHS_FLOAT:
				return (value.ffloat >= ranges.min.ffloatRange && value.ffloat <= ranges.max.ffloatRange);

			case SSHS_DOUBLE:
				return (value.ddouble >= ranges.min.ddoubleRange && value.ddouble <= ranges.max.ddoubleRange);

			case SSHS_STRING:
				return (
					valueString.length() >= ranges.min.stringRange && valueString.length() <= ranges.max.stringRange);

			case SSHS_UNKNOWN:
			default:
				return (false);
		}
	}

	void fromCUnion(union sshs_node_attr_value vu, enum sshs_node_attr_value_type tu) {
		switch (tu) {
			case SSHS_BOOL:
				setBool(vu.boolean);
				break;

			case SSHS_INT:
				setInt(vu.iint);
				break;

			case SSHS_LONG:
				setLong(vu.ilong);
				break;

			case SSHS_FLOAT:
				setFloat(vu.ffloat);
				break;

			case SSHS_DOUBLE:
				setDouble(vu.ddouble);
				break;

			case SSHS_STRING:
				setString(vu.string);
				break;

			case SSHS_UNKNOWN:
			default:
				throw std::runtime_error("SSHS: provided union value type does not match any valid type.");
				break;
		}
	}

	union sshs_node_attr_value toCUnion(bool readOnlyString = false) const {
		union sshs_node_attr_value vu;

		switch (type) {
			case SSHS_BOOL:
				vu.boolean = getBool();
				break;

			case SSHS_INT:
				vu.iint = getInt();
				break;

			case SSHS_LONG:
				vu.ilong = getLong();
				break;

			case SSHS_FLOAT:
				vu.ffloat = getFloat();
				break;

			case SSHS_DOUBLE:
				vu.ddouble = getDouble();
				break;

			case SSHS_STRING:
				if (readOnlyString) {
					vu.string = const_cast<char *>(getString().c_str());
				}
				else {
					vu.string = strdup(getString().c_str());
					sshsMemoryCheck(vu.string, "sshs_value.toCUnion");
				}
				break;

			case SSHS_UNKNOWN:
			default:
				throw std::runtime_error("SSHS: internal value type does not match any valid type.");
				break;
		}

		return (vu);
	}

	// Comparison operators.
	bool operator==(const sshs_value &rhs) const {
		switch (type) {
			case SSHS_BOOL:
				return (getBool() == rhs.getBool());

			case SSHS_INT:
				return (getInt() == rhs.getInt());

			case SSHS_LONG:
				return (getLong() == rhs.getLong());

			case SSHS_FLOAT:
				return (getFloat() == rhs.getFloat());

			case SSHS_DOUBLE:
				return (getDouble() == rhs.getDouble());

			case SSHS_STRING:
				return (getString() == rhs.getString());

			case SSHS_UNKNOWN:
			default:
				return (false);
		}
	}

	bool operator!=(const sshs_value &rhs) const {
		return (!this->operator==(rhs));
	}
};

// C++ helper functions.
const std::string &sshsHelperCppTypeToStringConverter(enum sshs_node_attr_value_type type);
enum sshs_node_attr_value_type sshsHelperCppStringToTypeConverter(const std::string &typeString);
std::string sshsHelperCppValueToStringConverter(const sshs_value &val);
sshs_value sshsHelperCppStringToValueConverter(enum sshs_node_attr_value_type type, const std::string &valueString);

// We don't care about unlocking anything here, as we exit hard on error anyway.
static inline void sshsNodeError(const std::string &funcName, const std::string &key,
	enum sshs_node_attr_value_type type, const std::string &msg, bool fatal = true) {
	boost::format errorMsg = boost::format("%s(): attribute '%s' (type '%s'): %s.") % funcName % key
							 % sshsHelperCppTypeToStringConverter(type) % msg;

	(*sshsGetGlobalErrorLogCallback())(errorMsg.str().c_str(), fatal);
}

static inline void sshsNodeErrorNoAttribute(
	const std::string &funcName, const std::string &key, enum sshs_node_attr_value_type type) {
	sshsNodeError(funcName, key, type, "attribute doesn't exist, you must create it first");
}

#endif /* SSHS_INTERNAL_HPP_ */
