#include "sshs_internal.hpp"

static const std::string typeStrings[] = {"bool", "byte", "short", "int", "long", "float", "double", "string"};

const std::string &sshsHelperCppTypeToStringConverter(enum sshs_node_attr_value_type type) {
	// Convert the value and its type into a string for XML output.
	switch (type) {
		case SSHS_BOOL:
		case SSHS_INT:
		case SSHS_LONG:
		case SSHS_FLOAT:
		case SSHS_DOUBLE:
		case SSHS_STRING:
			return (typeStrings[type]);

		case SSHS_UNKNOWN:
		default:
			throw std::runtime_error("sshsHelperCppTypeToStringConverter(): invalid type given.");
	}
}

enum sshs_node_attr_value_type sshsHelperCppStringToTypeConverter(const std::string &typeString) {
	// Convert the value string back into the internal type representation.
	if (typeString == typeStrings[SSHS_BOOL]) {
		return (SSHS_BOOL);
	}
	else if (typeString == typeStrings[1]) {
		return (SSHS_INT);
	}
	else if (typeString == typeStrings[2]) {
		return (SSHS_INT);
	}
	else if (typeString == typeStrings[SSHS_INT]) {
		return (SSHS_INT);
	}
	else if (typeString == typeStrings[SSHS_LONG]) {
		return (SSHS_LONG);
	}
	else if (typeString == typeStrings[SSHS_FLOAT]) {
		return (SSHS_FLOAT);
	}
	else if (typeString == typeStrings[SSHS_DOUBLE]) {
		return (SSHS_DOUBLE);
	}
	else if (typeString == typeStrings[SSHS_STRING]) {
		return (SSHS_STRING);
	}

	return (SSHS_UNKNOWN); // UNKNOWN TYPE.
}

std::string sshsHelperCppValueToStringConverter(const sshs_value &val) {
	// Convert the value and its type into a string for XML output.
	switch (val.getType()) {
		case SSHS_BOOL:
			// Manually generate true or false.
			return ((val.getBool()) ? ("true") : ("false"));

		case SSHS_INT:
			return (std::to_string(val.getInt()));

		case SSHS_LONG:
			return (std::to_string(val.getLong()));

		case SSHS_FLOAT:
			return (std::to_string(val.getFloat()));

		case SSHS_DOUBLE:
			return (std::to_string(val.getDouble()));

		case SSHS_STRING:
			return (val.getString());

		case SSHS_UNKNOWN:
		default:
			throw std::runtime_error("sshsHelperCppValueToStringConverter(): value has invalid type.");
	}
}

// Return false on failure (unknown type / faulty conversion), the content of
// value is undefined. For the STRING type, the returned value.string is a copy
// of the input string. Remember to free() it after use!
sshs_value sshsHelperCppStringToValueConverter(enum sshs_node_attr_value_type type, const std::string &valueString) {
	sshs_value value;

	switch (type) {
		case SSHS_BOOL:
			// Boolean uses custom true/false strings.
			value.setBool(valueString == "true");
			break;

		case SSHS_INT:
			value.setInt((int32_t) std::stol(valueString));
			break;

		case SSHS_LONG:
			value.setLong((int64_t) std::stoll(valueString));
			break;

		case SSHS_FLOAT:
			value.setFloat(std::stof(valueString));
			break;

		case SSHS_DOUBLE:
			value.setDouble(std::stod(valueString));
			break;

		case SSHS_STRING:
			value.setString(valueString);
			break;

		case SSHS_UNKNOWN:
		default:
			throw std::runtime_error("sshsHelperCppStringToValueConverter(): invalid type given.");
			break;
	}

	return (value);
}

/**
 * C11 wrappers for external use.
 */

// Do not free or modify the resulting string in any way!
const char *sshsHelperTypeToStringConverter(enum sshs_node_attr_value_type type) {
	return (sshsHelperCppTypeToStringConverter(type).c_str());
}

enum sshs_node_attr_value_type sshsHelperStringToTypeConverter(const char *typeString) {
	return (sshsHelperCppStringToTypeConverter(typeString));
}

// Remember to free the resulting string!
char *sshsHelperValueToStringConverter(enum sshs_node_attr_value_type type, union sshs_node_attr_value value) {
	sshs_value val;
	val.fromCUnion(value, type);

	const std::string typeString = sshsHelperCppValueToStringConverter(val);

	char *resultString = strdup(typeString.c_str());
	sshsMemoryCheck(resultString, __func__);

	return (resultString);
}

// Remember to free the resulting union's "string" member, if the type was SSHS_STRING!
union sshs_node_attr_value sshsHelperStringToValueConverter(
	enum sshs_node_attr_value_type type, const char *valueString) {
	if ((type == SSHS_STRING) && (valueString == nullptr)) {
		// Empty string.
		valueString = "";
	}

	return (sshsHelperCppStringToValueConverter(type, valueString).toCUnion());
}
