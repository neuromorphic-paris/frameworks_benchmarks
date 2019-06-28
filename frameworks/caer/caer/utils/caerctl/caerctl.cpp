#include "caer-sdk/utils.h"

#include "src/config_server.h"
#include "utils/ext/linenoise-ng/linenoise.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <string>
#include <vector>

namespace asio   = boost::asio;
namespace asioIP = boost::asio::ip;
using asioTCP    = boost::asio::ip::tcp;
namespace po     = boost::program_options;

#if defined(OS_UNIX) && OS_UNIX == 1
#	include <pwd.h>
#	include <sys/types.h>
#endif

#define CAERCTL_HISTORY_FILE_NAME ".caer-ctl.history"

static inline boost::filesystem::path getHomeDirectory() {
	// First query main environment variables: HOME on Unix, USERPROFILE on Windows.
	const char *homeDir = getenv("HOME");

	if (homeDir || (homeDir = getenv("USERPROFILE"))) {
		return (boost::filesystem::path(std::string(homeDir)));
	}

// Unix: try to get it from the user data storage.
#if defined(OS_UNIX) && OS_UNIX == 1
	struct passwd userPasswd;
	struct passwd *userPasswdPtr;
	char userPasswdBuf[2048];

	if (getpwuid_r(getuid(), &userPasswd, userPasswdBuf, sizeof(userPasswdBuf), &userPasswdPtr) == 0) {
		return (boost::filesystem::path(std::string(userPasswd.pw_dir)));
	}
#endif

#if defined(OS_WINDOWS) && OS_WINDOWS == 1
	// Windows: try to get HOMEDRIVE and HOMEPATH from environment and concatenate them.
	const char *homeDrive = getenv("HOMEDRIVE");
	const char *homePath  = getenv("HOMEPATH");

	if (homeDrive && homePath) {
		std::string winHome(homeDrive);
		winHome += homePath;
		return (boost::filesystem::path(winHome));
	}
#endif

	// No clue about home directory.
	throw boost::filesystem::filesystem_error("Unable to get home directory.",
		boost::system::errc::make_error_code(boost::system::errc::no_such_file_or_directory));
}

static void handleInputLine(const char *buf, size_t bufLength);
static void handleCommandCompletion(const char *buf, linenoiseCompletions *autoComplete);

static void actionCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete,
	const char *partialActionString, size_t partialActionStringLength);
static void nodeCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *partialNodeString, size_t partialNodeStringLength);
static void keyCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *nodeString, size_t nodeStringLength, const char *partialKeyString, size_t partialKeyStringLength);
static void typeCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *nodeString, size_t nodeStringLength, const char *keyString, size_t keyStringLength,
	const char *partialTypeString, size_t partialTypeStringLength);
static void valueCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *nodeString, size_t nodeStringLength, const char *keyString, size_t keyStringLength,
	const char *typeString, size_t typeStringLength, const char *partialValueString, size_t partialValueStringLength);
static void addCompletionSuffix(linenoiseCompletions *lc, const char *buf, size_t completionPoint, const char *suffix,
	bool endSpace, bool endSlash);

static const struct {
	const char *name;
	size_t nameLen;
	uint8_t code;
} actions[] = {
	{"node_exists", 11, CAER_CONFIG_NODE_EXISTS},
	{"attr_exists", 11, CAER_CONFIG_ATTR_EXISTS},
	{"get", 3, CAER_CONFIG_GET},
	{"put", 3, CAER_CONFIG_PUT},
	{"help", 4, CAER_CONFIG_GET_DESCRIPTION},
	{"add_module", 10, CAER_CONFIG_ADD_MODULE},
	{"remove_module", 13, CAER_CONFIG_REMOVE_MODULE},
};
static const size_t actionsLength = sizeof(actions) / sizeof(actions[0]);

static asio::io_service ioService;
static asioTCP::socket netSocket(ioService);

[[noreturn]] static inline void printHelpAndExit(po::options_description &desc) {
	std::cout << std::endl << desc << std::endl;
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	// Allowed command-line options for caer-ctl.
	po::options_description cliDescription("Command-line options");
	cliDescription.add_options()("help,h", "print help text")("ipaddress,i", po::value<std::string>(),
		"IP-address or hostname to connect to")("port,p", po::value<std::string>(), "port to connect to")("script,s",
		po::value<std::vector<std::string>>()->multitoken(),
		"script mode, sends the given command directly to the server as if typed in and exits.\n"
		"Format: <action> <node> [<attribute> <type> [<value>]]\nExample: set /caer/logger/ logLevel byte 7");

	po::variables_map cliVarMap;
	try {
		po::store(boost::program_options::parse_command_line(argc, argv, cliDescription), cliVarMap);
		po::notify(cliVarMap);
	}
	catch (...) {
		std::cout << "Failed to parse command-line options!" << std::endl;
		printHelpAndExit(cliDescription);
	}

	// Parse/check command-line options.
	if (cliVarMap.count("help")) {
		printHelpAndExit(cliDescription);
	}

	std::string ipAddress("127.0.0.1");
	if (cliVarMap.count("ipaddress")) {
		ipAddress = cliVarMap["ipaddress"].as<std::string>();
	}

	std::string portNumber("4040");
	if (cliVarMap.count("port")) {
		portNumber = cliVarMap["port"].as<std::string>();
	}

	bool scriptMode = false;
	if (cliVarMap.count("script")) {
		std::vector<std::string> commandComponents = cliVarMap["script"].as<std::vector<std::string>>();

		// At lest two components must be passed, any less is an error.
		if (commandComponents.size() < 2) {
			std::cout << "Script mode must have at least two components!" << std::endl;
			printHelpAndExit(cliDescription);
		}

		// At most five components can be passed, any more is an error.
		if (commandComponents.size() > 5) {
			std::cout << "Script mode cannot have more than five components!" << std::endl;
			printHelpAndExit(cliDescription);
		}

		if (commandComponents[0] == "quit" || commandComponents[0] == "exit") {
			std::cout << "Script mode cannot use 'quit' or 'exit' actions!" << std::endl;
			printHelpAndExit(cliDescription);
		}

		scriptMode = true;
	}

	// Generate command history file path (in user home).
	boost::filesystem::path commandHistoryFilePath;

	try {
		commandHistoryFilePath = getHomeDirectory();
	}
	catch (const boost::filesystem::filesystem_error &) {
		std::cerr << "Failed to get home directory for history file, using current working directory." << std::endl;
		commandHistoryFilePath = boost::filesystem::current_path();
	}

	commandHistoryFilePath.append(CAERCTL_HISTORY_FILE_NAME, boost::filesystem::path::codecvt());

	// Connect to the remote cAER config server.
	try {
		asioTCP::resolver resolver(ioService);
		asio::connect(netSocket, resolver.resolve({ipAddress, portNumber}));
	}
	catch (const boost::system::system_error &ex) {
		boost::format exMsg = boost::format("Failed to connect to %s:%s, error message is:\n\t%s.") % ipAddress
							  % portNumber % ex.what();
		std::cerr << exMsg.str() << std::endl;
		return (EXIT_FAILURE);
	}

	// Load command history file.
	linenoiseHistoryLoad(commandHistoryFilePath.string().c_str());

	if (scriptMode) {
		std::vector<std::string> commandComponents = cliVarMap["script"].as<std::vector<std::string>>();

		std::string inputString = boost::algorithm::join(commandComponents, " ");
		const char *inputLine   = inputString.c_str();

		// Add input to command history.
		linenoiseHistoryAdd(inputLine);

		// Try to generate a request, if there's any content.
		size_t inputLineLength = strlen(inputLine);

		if (inputLineLength > 0) {
			handleInputLine(inputLine, inputLineLength);
		}
	}
	else {
		// Create a shell prompt with the IP:Port displayed.
		boost::format shellPrompt = boost::format("cAER @ %s:%s >> ") % ipAddress % portNumber;

		// Set our own command completion function.
		linenoiseSetCompletionCallback(&handleCommandCompletion);

		while (true) {
			// Display prompt and read input (NOTE: remember to free input after use!).
			char *inputLine = linenoise(shellPrompt.str().c_str());

			// Check for EOF first.
			if (inputLine == nullptr) {
				// Exit loop.
				break;
			}

			// Add input to command history.
			linenoiseHistoryAdd(inputLine);

			// Then, after having added to history, check for termination commands.
			if (strncmp(inputLine, "quit", 4) == 0 || strncmp(inputLine, "exit", 4) == 0) {
				// Exit loop, free memory.
				free(inputLine);
				break;
			}

			// Try to generate a request, if there's any content.
			size_t inputLineLength = strlen(inputLine);

			if (inputLineLength > 0) {
				handleInputLine(inputLine, inputLineLength);
			}

			// Free input after use.
			free(inputLine);
		}
	}

	// Save command history file.
	linenoiseHistorySave(commandHistoryFilePath.string().c_str());

	return (EXIT_SUCCESS);
}

static inline void setExtraLen(uint8_t *buf, uint16_t extraLen) {
	*((uint16_t *) (buf + 2)) = htole16(extraLen);
}

static inline void setNodeLen(uint8_t *buf, uint16_t nodeLen) {
	*((uint16_t *) (buf + 4)) = htole16(nodeLen);
}

static inline void setKeyLen(uint8_t *buf, uint16_t keyLen) {
	*((uint16_t *) (buf + 6)) = htole16(keyLen);
}

static inline void setValueLen(uint8_t *buf, uint16_t valueLen) {
	*((uint16_t *) (buf + 8)) = htole16(valueLen);
}

#define MAX_CMD_PARTS 5

#define CMD_PART_ACTION 0
#define CMD_PART_NODE 1
#define CMD_PART_KEY 2
#define CMD_PART_TYPE 3
#define CMD_PART_VALUE 4

static void handleInputLine(const char *buf, size_t bufLength) {
	// First let's split up the command into its constituents.
	char *commandParts[MAX_CMD_PARTS + 1] = {nullptr};

	// Create a copy of buf, so that strtok_r() can modify it.
	char bufCopy[bufLength + 1];
	strcpy(bufCopy, buf);

	// Split string into usable parts.
	size_t idx         = 0;
	char *tokenSavePtr = nullptr, *nextCmdPart = nullptr, *currCmdPart = bufCopy;
	while ((nextCmdPart = strtok_r(currCmdPart, " ", &tokenSavePtr)) != nullptr) {
		if (idx < MAX_CMD_PARTS) {
			commandParts[idx] = nextCmdPart;
		}
		else {
			// Abort, too many parts.
			std::cerr << "Error: command is made up of too many parts." << std::endl;
			return;
		}

		idx++;
		currCmdPart = nullptr;
	}

	// Check that we got something.
	if (commandParts[CMD_PART_ACTION] == nullptr) {
		std::cerr << "Error: empty command." << std::endl;
		return;
	}

	// Let's get the action code first thing.
	uint8_t actionCode = UINT8_MAX;

	for (size_t i = 0; i < actionsLength; i++) {
		if (strcmp(commandParts[CMD_PART_ACTION], actions[i].name) == 0) {
			actionCode = actions[i].code;
		}
	}

	// Control message format: 1 byte ACTION, 1 byte TYPE, 2 bytes EXTRA_LEN,
	// 2 bytes NODE_LEN, 2 bytes KEY_LEN, 2 bytes VALUE_LEN, then up to 4086
	// bytes split between EXTRA, NODE, KEY, VALUE (with 4 bytes for NUL).
	// Basically: (EXTRA_LEN + NODE_LEN + KEY_LEN + VALUE_LEN) <= 4086.
	// EXTRA, NODE, KEY, VALUE have to be NUL terminated, and their length
	// must include the NUL termination byte.
	// This results in a maximum message size of 4096 bytes (4KB).
	uint8_t dataBuffer[CAER_CONFIG_SERVER_BUFFER_SIZE];
	size_t dataBufferLength = 0;

	// Now that we know what we want to do, let's decode the command line.
	switch (actionCode) {
		case CAER_CONFIG_NODE_EXISTS: {
			// Check parameters needed for operation.
			if (commandParts[CMD_PART_NODE] == nullptr) {
				std::cerr << "Error: missing node parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_NODE + 1] != nullptr) {
				std::cerr << "Error: too many parameters for command." << std::endl;
				return;
			}

			size_t nodeLength = strlen(commandParts[CMD_PART_NODE]) + 1; // +1 for terminating NUL byte.

			dataBuffer[0] = actionCode;
			dataBuffer[1] = 0;          // UNUSED.
			setExtraLen(dataBuffer, 0); // UNUSED.
			setNodeLen(dataBuffer, (uint16_t) nodeLength);
			setKeyLen(dataBuffer, 0);   // UNUSED.
			setValueLen(dataBuffer, 0); // UNUSED.

			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, commandParts[CMD_PART_NODE], nodeLength);

			dataBufferLength = CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength;

			break;
		}

		case CAER_CONFIG_ATTR_EXISTS:
		case CAER_CONFIG_GET:
		case CAER_CONFIG_GET_DESCRIPTION: {
			// Check parameters needed for operation.
			if (commandParts[CMD_PART_NODE] == nullptr) {
				std::cerr << "Error: missing node parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_KEY] == nullptr) {
				std::cerr << "Error: missing key parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_TYPE] == nullptr) {
				std::cerr << "Error: missing type parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_TYPE + 1] != nullptr) {
				std::cerr << "Error: too many parameters for command." << std::endl;
				return;
			}

			size_t nodeLength = strlen(commandParts[CMD_PART_NODE]) + 1; // +1 for terminating NUL byte.
			size_t keyLength  = strlen(commandParts[CMD_PART_KEY]) + 1;  // +1 for terminating NUL byte.

			enum sshs_node_attr_value_type type = sshsHelperStringToTypeConverter(commandParts[CMD_PART_TYPE]);
			if (type == SSHS_UNKNOWN) {
				std::cerr << "Error: invalid type parameter." << std::endl;
				return;
			}

			dataBuffer[0] = actionCode;
			dataBuffer[1] = (uint8_t) type;
			setExtraLen(dataBuffer, 0); // UNUSED.
			setNodeLen(dataBuffer, (uint16_t) nodeLength);
			setKeyLen(dataBuffer, (uint16_t) keyLength);
			setValueLen(dataBuffer, 0); // UNUSED.

			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, commandParts[CMD_PART_NODE], nodeLength);
			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength, commandParts[CMD_PART_KEY], keyLength);

			dataBufferLength = CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength + keyLength;

			break;
		}

		case CAER_CONFIG_PUT: {
			// Check parameters needed for operation.
			if (commandParts[CMD_PART_NODE] == nullptr) {
				std::cerr << "Error: missing node parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_KEY] == nullptr) {
				std::cerr << "Error: missing key parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_TYPE] == nullptr) {
				std::cerr << "Error: missing type parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_VALUE] == nullptr) {
				std::cerr << "Error: missing value parameter." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_VALUE + 1] != nullptr) {
				std::cerr << "Error: too many parameters for command." << std::endl;
				return;
			}

			size_t nodeLength  = strlen(commandParts[CMD_PART_NODE]) + 1;  // +1 for terminating NUL byte.
			size_t keyLength   = strlen(commandParts[CMD_PART_KEY]) + 1;   // +1 for terminating NUL byte.
			size_t valueLength = strlen(commandParts[CMD_PART_VALUE]) + 1; // +1 for terminating NUL byte.

			enum sshs_node_attr_value_type type = sshsHelperStringToTypeConverter(commandParts[CMD_PART_TYPE]);
			if (type == SSHS_UNKNOWN) {
				std::cerr << "Error: invalid type parameter." << std::endl;
				return;
			}

			dataBuffer[0] = actionCode;
			dataBuffer[1] = (uint8_t) type;
			setExtraLen(dataBuffer, 0); // UNUSED.
			setNodeLen(dataBuffer, (uint16_t) nodeLength);
			setKeyLen(dataBuffer, (uint16_t) keyLength);
			setValueLen(dataBuffer, (uint16_t) valueLength);

			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, commandParts[CMD_PART_NODE], nodeLength);
			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength, commandParts[CMD_PART_KEY], keyLength);
			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength + keyLength, commandParts[CMD_PART_VALUE],
				valueLength);

			dataBufferLength = CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength + keyLength + valueLength;

			break;
		}

		case CAER_CONFIG_ADD_MODULE: {
			// Check parameters needed for operation. Reuse node parameters.
			if (commandParts[CMD_PART_NODE] == nullptr) {
				std::cerr << "Error: missing module name." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_KEY] == nullptr) {
				std::cerr << "Error: missing library name." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_KEY + 1] != nullptr) {
				std::cerr << "Error: too many parameters for command." << std::endl;
				return;
			}

			size_t nodeLength = strlen(commandParts[CMD_PART_NODE]) + 1; // +1 for terminating NUL byte.
			size_t keyLength  = strlen(commandParts[CMD_PART_KEY]) + 1;  // +1 for terminating NUL byte.

			dataBuffer[0] = actionCode;
			dataBuffer[1] = 0;          // UNUSED.
			setExtraLen(dataBuffer, 0); // UNUSED.
			setNodeLen(dataBuffer, (uint16_t) nodeLength);
			setKeyLen(dataBuffer, (uint16_t) keyLength);
			setValueLen(dataBuffer, 0); // UNUSED.

			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, commandParts[CMD_PART_NODE], nodeLength);
			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength, commandParts[CMD_PART_KEY], keyLength);

			dataBufferLength = CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength + keyLength;

			break;
		}

		case CAER_CONFIG_REMOVE_MODULE: {
			// Check parameters needed for operation. Reuse node parameters.
			if (commandParts[CMD_PART_NODE] == nullptr) {
				std::cerr << "Error: missing module name." << std::endl;
				return;
			}
			if (commandParts[CMD_PART_NODE + 1] != nullptr) {
				std::cerr << "Error: too many parameters for command." << std::endl;
				return;
			}

			size_t nodeLength = strlen(commandParts[CMD_PART_NODE]) + 1; // +1 for terminating NUL byte.

			dataBuffer[0] = actionCode;
			dataBuffer[1] = 0;          // UNUSED.
			setExtraLen(dataBuffer, 0); // UNUSED.
			setNodeLen(dataBuffer, (uint16_t) nodeLength);
			setKeyLen(dataBuffer, 0);   // UNUSED.
			setValueLen(dataBuffer, 0); // UNUSED.

			memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, commandParts[CMD_PART_NODE], nodeLength);

			dataBufferLength = CAER_CONFIG_SERVER_HEADER_SIZE + nodeLength;

			break;
		}

		default:
			std::cerr << "Error: unknown command." << std::endl;
			return;
	}

	// Send formatted command to configuration server.
	try {
		asio::write(netSocket, asio::buffer(dataBuffer, dataBufferLength));
	}
	catch (const boost::system::system_error &ex) {
		boost::format exMsg
			= boost::format("Unable to send data to config server, error message is:\n\t%s.") % ex.what();
		std::cerr << exMsg.str() << std::endl;
		return;
	}

	// The response from the server follows a simplified version of the request
	// protocol. A byte for ACTION, a byte for TYPE, 2 bytes for MSG_LEN and then
	// up to 4092 bytes of MSG, for a maximum total of 4096 bytes again.
	// MSG must be NUL terminated, and the NUL byte shall be part of the length.
	try {
		asio::read(netSocket, asio::buffer(dataBuffer, 4));
	}
	catch (const boost::system::system_error &ex) {
		boost::format exMsg
			= boost::format("Unable to receive data from config server, error message is:\n\t%s.") % ex.what();
		std::cerr << exMsg.str() << std::endl;
		return;
	}

	// Decode response header fields (all in little-endian).
	uint8_t action     = dataBuffer[0];
	uint8_t type       = dataBuffer[1];
	uint16_t msgLength = le16toh(*(uint16_t *) (dataBuffer + 2));

	// Total length to get for response.
	try {
		asio::read(netSocket, asio::buffer(dataBuffer + 4, msgLength));
	}
	catch (const boost::system::system_error &ex) {
		boost::format exMsg
			= boost::format("Unable to receive data from config server, error message is:\n\t%s.") % ex.what();
		std::cerr << exMsg.str() << std::endl;
		return;
	}

	// Convert action back to a string.
	const char *actionString = nullptr;

	// Detect error response.
	if (action == CAER_CONFIG_ERROR) {
		actionString = "error";
	}
	else {
		for (size_t i = 0; i < actionsLength; i++) {
			if (actions[i].code == action) {
				actionString = actions[i].name;
			}
		}
	}

	// Display results.
	boost::format resultMsg = boost::format("Result: action=%s, type=%s, msgLength=%" PRIu16 ", msg='%s'.")
							  % actionString % sshsHelperTypeToStringConverter((enum sshs_node_attr_value_type) type)
							  % msgLength % (dataBuffer + 4);

	std::cout << resultMsg.str() << std::endl;
}

static void handleCommandCompletion(const char *buf, linenoiseCompletions *autoComplete) {
	size_t bufLength = strlen(buf);

	// First let's split up the command into its constituents.
	char *commandParts[MAX_CMD_PARTS + 1] = {nullptr};

	// Create a copy of buf, so that strtok_r() can modify it.
	char bufCopy[bufLength + 1];
	strcpy(bufCopy, buf);

	// Split string into usable parts.
	size_t idx         = 0;
	char *tokenSavePtr = nullptr, *nextCmdPart = nullptr, *currCmdPart = bufCopy;
	while ((nextCmdPart = strtok_r(currCmdPart, " ", &tokenSavePtr)) != nullptr) {
		if (idx < MAX_CMD_PARTS) {
			commandParts[idx] = nextCmdPart;
		}
		else {
			// Abort, too many parts.
			return;
		}

		idx++;
		currCmdPart = nullptr;
	}

	// Also calculate number of commands already present in line (word-depth).
	// This is actually much more useful to understand where we are and what to do.
	size_t commandDepth = idx;

	if (commandDepth > 0 && bufLength > 0 && buf[bufLength - 1] != ' ') {
		// If commands are present, ensure they have been "confirmed" by at least
		// one terminating spacing character. Else don't calculate the last command.
		commandDepth--;
	}

	// Check that we got something.
	if (commandDepth == 0) {
		// Always start off with a command/action.
		size_t cmdActionLength = 0;
		if (commandParts[CMD_PART_ACTION] != nullptr) {
			cmdActionLength = strlen(commandParts[CMD_PART_ACTION]);
		}

		actionCompletion(buf, bufLength, autoComplete, commandParts[CMD_PART_ACTION], cmdActionLength);

		return;
	}

	// Let's get the action code first thing.
	uint8_t actionCode = UINT8_MAX;

	for (size_t i = 0; i < actionsLength; i++) {
		if (strcmp(commandParts[CMD_PART_ACTION], actions[i].name) == 0) {
			actionCode = actions[i].code;
		}
	}

	switch (actionCode) {
		case CAER_CONFIG_NODE_EXISTS:
			if (commandDepth == 1) {
				size_t cmdNodeLength = 0;
				if (commandParts[CMD_PART_NODE] != nullptr) {
					cmdNodeLength = strlen(commandParts[CMD_PART_NODE]);
				}

				nodeCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE], cmdNodeLength);
			}

			break;

		case CAER_CONFIG_ATTR_EXISTS:
		case CAER_CONFIG_GET:
		case CAER_CONFIG_GET_DESCRIPTION:
			if (commandDepth == 1) {
				size_t cmdNodeLength = 0;
				if (commandParts[CMD_PART_NODE] != nullptr) {
					cmdNodeLength = strlen(commandParts[CMD_PART_NODE]);
				}

				nodeCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE], cmdNodeLength);
			}
			if (commandDepth == 2) {
				size_t cmdKeyLength = 0;
				if (commandParts[CMD_PART_KEY] != nullptr) {
					cmdKeyLength = strlen(commandParts[CMD_PART_KEY]);
				}

				keyCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE],
					strlen(commandParts[CMD_PART_NODE]), commandParts[CMD_PART_KEY], cmdKeyLength);
			}
			if (commandDepth == 3) {
				size_t cmdTypeLength = 0;
				if (commandParts[CMD_PART_TYPE] != nullptr) {
					cmdTypeLength = strlen(commandParts[CMD_PART_TYPE]);
				}

				typeCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE],
					strlen(commandParts[CMD_PART_NODE]), commandParts[CMD_PART_KEY], strlen(commandParts[CMD_PART_KEY]),
					commandParts[CMD_PART_TYPE], cmdTypeLength);
			}

			break;

		case CAER_CONFIG_PUT:
			if (commandDepth == 1) {
				size_t cmdNodeLength = 0;
				if (commandParts[CMD_PART_NODE] != nullptr) {
					cmdNodeLength = strlen(commandParts[CMD_PART_NODE]);
				}

				nodeCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE], cmdNodeLength);
			}
			if (commandDepth == 2) {
				size_t cmdKeyLength = 0;
				if (commandParts[CMD_PART_KEY] != nullptr) {
					cmdKeyLength = strlen(commandParts[CMD_PART_KEY]);
				}

				keyCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE],
					strlen(commandParts[CMD_PART_NODE]), commandParts[CMD_PART_KEY], cmdKeyLength);
			}
			if (commandDepth == 3) {
				size_t cmdTypeLength = 0;
				if (commandParts[CMD_PART_TYPE] != nullptr) {
					cmdTypeLength = strlen(commandParts[CMD_PART_TYPE]);
				}

				typeCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE],
					strlen(commandParts[CMD_PART_NODE]), commandParts[CMD_PART_KEY], strlen(commandParts[CMD_PART_KEY]),
					commandParts[CMD_PART_TYPE], cmdTypeLength);
			}
			if (commandDepth == 4) {
				size_t cmdValueLength = 0;
				if (commandParts[CMD_PART_VALUE] != nullptr) {
					cmdValueLength = strlen(commandParts[CMD_PART_VALUE]);
				}

				valueCompletion(buf, bufLength, autoComplete, actionCode, commandParts[CMD_PART_NODE],
					strlen(commandParts[CMD_PART_NODE]), commandParts[CMD_PART_KEY], strlen(commandParts[CMD_PART_KEY]),
					commandParts[CMD_PART_TYPE], strlen(commandParts[CMD_PART_TYPE]), commandParts[CMD_PART_VALUE],
					cmdValueLength);
			}

			break;
	}
}

static void actionCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete,
	const char *partialActionString, size_t partialActionStringLength) {
	UNUSED_ARGUMENT(buf);
	UNUSED_ARGUMENT(bufLength);

	// Always start off with a command.
	for (size_t i = 0; i < actionsLength; i++) {
		if (strncmp(actions[i].name, partialActionString, partialActionStringLength) == 0) {
			addCompletionSuffix(autoComplete, "", 0, actions[i].name, true, false);
		}
	}

	// Add quit and exit too.
	if (strncmp("exit", partialActionString, partialActionStringLength) == 0) {
		addCompletionSuffix(autoComplete, "", 0, "exit", true, false);
	}
	if (strncmp("quit", partialActionString, partialActionStringLength) == 0) {
		addCompletionSuffix(autoComplete, "", 0, "quit", true, false);
	}
}

static void nodeCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *partialNodeString, size_t partialNodeStringLength) {
	UNUSED_ARGUMENT(actionCode);

	// If partialNodeString is still empty, the first thing is to complete the root.
	if (partialNodeStringLength == 0) {
		addCompletionSuffix(autoComplete, buf, bufLength, "/", false, false);
		return;
	}

	// Get all the children of the last fully defined node (/ or /../../).
	const char *lastNode = strrchr(partialNodeString, '/');
	if (lastNode == nullptr) {
		// No / found, invalid, cannot auto-complete.
		return;
	}

	size_t lastNodeLength = (size_t)(lastNode - partialNodeString) + 1;

	uint8_t dataBuffer[CAER_CONFIG_SERVER_BUFFER_SIZE];

	// Send request for all children names.
	dataBuffer[0] = CAER_CONFIG_GET_CHILDREN;
	dataBuffer[1] = 0;                                      // UNUSED.
	setExtraLen(dataBuffer, 0);                             // UNUSED.
	setNodeLen(dataBuffer, (uint16_t)(lastNodeLength + 1)); // +1 for terminating NUL byte.
	setKeyLen(dataBuffer, 0);                               // UNUSED.
	setValueLen(dataBuffer, 0);                             // UNUSED.

	memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, partialNodeString, lastNodeLength);
	dataBuffer[CAER_CONFIG_SERVER_HEADER_SIZE + lastNodeLength] = '\0';

	try {
		asio::write(netSocket, asio::buffer(dataBuffer, CAER_CONFIG_SERVER_HEADER_SIZE + lastNodeLength + 1));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	try {
		asio::read(netSocket, asio::buffer(dataBuffer, 4));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	// Decode response header fields (all in little-endian).
	uint8_t action     = dataBuffer[0];
	uint8_t type       = dataBuffer[1];
	uint16_t msgLength = le16toh(*(uint16_t *) (dataBuffer + 2));

	// Total length to get for response.
	try {
		asio::read(netSocket, asio::buffer(dataBuffer + 4, msgLength));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	if (action == CAER_CONFIG_ERROR || type != SSHS_STRING) {
		// Invalid request made, no auto-completion.
		return;
	}

	// At this point we made a valid request and got back a full response.
	for (size_t i = 0; i < msgLength; i++) {
		if (strncasecmp((const char *) dataBuffer + 4 + i, lastNode + 1, strlen(lastNode + 1)) == 0) {
			addCompletionSuffix(
				autoComplete, buf, bufLength - strlen(lastNode + 1), (const char *) dataBuffer + 4 + i, false, true);
		}

		// Jump to the NUL character after this string.
		i += strlen((const char *) dataBuffer + 4 + i);
	}
}

static void keyCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *nodeString, size_t nodeStringLength, const char *partialKeyString, size_t partialKeyStringLength) {
	UNUSED_ARGUMENT(actionCode);

	uint8_t dataBuffer[CAER_CONFIG_SERVER_BUFFER_SIZE];

	// Send request for all attribute names for this node.
	dataBuffer[0] = CAER_CONFIG_GET_ATTRIBUTES;
	dataBuffer[1] = 0;                                        // UNUSED.
	setExtraLen(dataBuffer, 0);                               // UNUSED.
	setNodeLen(dataBuffer, (uint16_t)(nodeStringLength + 1)); // +1 for terminating NUL byte.
	setKeyLen(dataBuffer, 0);                                 // UNUSED.
	setValueLen(dataBuffer, 0);                               // UNUSED.

	memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, nodeString, nodeStringLength);
	dataBuffer[CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength] = '\0';

	try {
		asio::write(netSocket, asio::buffer(dataBuffer, CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	try {
		asio::read(netSocket, asio::buffer(dataBuffer, 4));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	// Decode response header fields (all in little-endian).
	uint8_t action     = dataBuffer[0];
	uint8_t type       = dataBuffer[1];
	uint16_t msgLength = le16toh(*(uint16_t *) (dataBuffer + 2));

	// Total length to get for response.
	try {
		asio::read(netSocket, asio::buffer(dataBuffer + 4, msgLength));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	if (action == CAER_CONFIG_ERROR || type != SSHS_STRING) {
		// Invalid request made, no auto-completion.
		return;
	}

	// At this point we made a valid request and got back a full response.
	for (size_t i = 0; i < msgLength; i++) {
		if (strncasecmp((const char *) dataBuffer + 4 + i, partialKeyString, partialKeyStringLength) == 0) {
			addCompletionSuffix(
				autoComplete, buf, bufLength - partialKeyStringLength, (const char *) dataBuffer + 4 + i, true, false);
		}

		// Jump to the NUL character after this string.
		i += strlen((const char *) dataBuffer + 4 + i);
	}
}

static void typeCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *nodeString, size_t nodeStringLength, const char *keyString, size_t keyStringLength,
	const char *partialTypeString, size_t partialTypeStringLength) {
	UNUSED_ARGUMENT(actionCode);

	uint8_t dataBuffer[CAER_CONFIG_SERVER_BUFFER_SIZE];

	// Send request for the type name for this key on this node.
	dataBuffer[0] = CAER_CONFIG_GET_TYPE;
	dataBuffer[1] = 0;                                        // UNUSED.
	setExtraLen(dataBuffer, 0);                               // UNUSED.
	setNodeLen(dataBuffer, (uint16_t)(nodeStringLength + 1)); // +1 for terminating NUL byte.
	setKeyLen(dataBuffer, (uint16_t)(keyStringLength + 1));   // +1 for terminating NUL byte.
	setValueLen(dataBuffer, 0);                               // UNUSED.

	memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, nodeString, nodeStringLength);
	dataBuffer[CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength] = '\0';

	memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1, keyString, keyStringLength);
	dataBuffer[CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1 + keyStringLength] = '\0';

	try {
		asio::write(netSocket,
			asio::buffer(dataBuffer, CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1 + keyStringLength + 1));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	try {
		asio::read(netSocket, asio::buffer(dataBuffer, 4));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	// Decode response header fields (all in little-endian).
	uint8_t action     = dataBuffer[0];
	uint8_t type       = dataBuffer[1];
	uint16_t msgLength = le16toh(*(uint16_t *) (dataBuffer + 2));

	// Total length to get for response.
	try {
		asio::read(netSocket, asio::buffer(dataBuffer + 4, msgLength));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	if (action == CAER_CONFIG_ERROR || type != SSHS_STRING) {
		// Invalid request made, no auto-completion.
		return;
	}

	// At this point we made a valid request and got back a full response.
	if (strncasecmp((const char *) dataBuffer + 4, partialTypeString, partialTypeStringLength) == 0) {
		addCompletionSuffix(
			autoComplete, buf, bufLength - partialTypeStringLength, (const char *) dataBuffer + 4, true, false);
	}
}

static void valueCompletion(const char *buf, size_t bufLength, linenoiseCompletions *autoComplete, uint8_t actionCode,
	const char *nodeString, size_t nodeStringLength, const char *keyString, size_t keyStringLength,
	const char *typeString, size_t typeStringLength, const char *partialValueString, size_t partialValueStringLength) {
	UNUSED_ARGUMENT(actionCode);
	UNUSED_ARGUMENT(typeStringLength);

	enum sshs_node_attr_value_type type = sshsHelperStringToTypeConverter(typeString);
	if (type == SSHS_UNKNOWN) {
		// Invalid type, no auto-completion.
		return;
	}

	if (partialValueStringLength != 0) {
		// If there already is content, we can't do any auto-completion here, as
		// we have no idea about what a valid value would be to complete ...
		// Unless this is a boolean, then we can propose true/false strings.
		if (type == SSHS_BOOL) {
			if (strncmp("true", partialValueString, partialValueStringLength) == 0) {
				addCompletionSuffix(autoComplete, buf, bufLength - partialValueStringLength, "true", false, false);
			}
			if (strncmp("false", partialValueString, partialValueStringLength) == 0) {
				addCompletionSuffix(autoComplete, buf, bufLength - partialValueStringLength, "false", false, false);
			}
		}

		return;
	}

	uint8_t dataBuffer[CAER_CONFIG_SERVER_BUFFER_SIZE];

	// Send request for the current value, so we can auto-complete with it as default.
	dataBuffer[0] = CAER_CONFIG_GET;
	dataBuffer[1] = (uint8_t) type;
	setExtraLen(dataBuffer, 0);                               // UNUSED.
	setNodeLen(dataBuffer, (uint16_t)(nodeStringLength + 1)); // +1 for terminating NUL byte.
	setKeyLen(dataBuffer, (uint16_t)(keyStringLength + 1));   // +1 for terminating NUL byte.
	setValueLen(dataBuffer, 0);                               // UNUSED.

	memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE, nodeString, nodeStringLength);
	dataBuffer[CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength] = '\0';

	memcpy(dataBuffer + CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1, keyString, keyStringLength);
	dataBuffer[CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1 + keyStringLength] = '\0';

	try {
		asio::write(netSocket,
			asio::buffer(dataBuffer, CAER_CONFIG_SERVER_HEADER_SIZE + nodeStringLength + 1 + keyStringLength + 1));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	try {
		asio::read(netSocket, asio::buffer(dataBuffer, 4));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	// Decode response header fields (all in little-endian).
	uint8_t action     = dataBuffer[0];
	uint16_t msgLength = le16toh(*(uint16_t *) (dataBuffer + 2));

	// Total length to get for response.
	try {
		asio::read(netSocket, asio::buffer(dataBuffer + 4, msgLength));
	}
	catch (const boost::system::system_error &) {
		// Failed to contact remote host, no auto-completion!
		return;
	}

	if (action == CAER_CONFIG_ERROR) {
		// Invalid request made, no auto-completion.
		return;
	}

	// At this point we made a valid request and got back a full response.
	// We can just use it directly and paste it in as completion.
	addCompletionSuffix(autoComplete, buf, bufLength, (const char *) dataBuffer + 4, false, false);

	// If this is a boolean value, we can also add the inverse as a second completion.
	if (type == SSHS_BOOL) {
		if (strcmp((const char *) dataBuffer + 4, "true") == 0) {
			addCompletionSuffix(autoComplete, buf, bufLength, "false", false, false);
		}
		else {
			addCompletionSuffix(autoComplete, buf, bufLength, "true", false, false);
		}
	}
}

static void addCompletionSuffix(linenoiseCompletions *autoComplete, const char *buf, size_t completionPoint,
	const char *suffix, bool endSpace, bool endSlash) {
	char concat[2048];

	if (endSpace) {
		if (endSlash) {
			snprintf(concat, 2048, "%.*s%s/ ", (int) completionPoint, buf, suffix);
		}
		else {
			snprintf(concat, 2048, "%.*s%s ", (int) completionPoint, buf, suffix);
		}
	}
	else {
		if (endSlash) {
			snprintf(concat, 2048, "%.*s%s/", (int) completionPoint, buf, suffix);
		}
		else {
			snprintf(concat, 2048, "%.*s%s", (int) completionPoint, buf, suffix);
		}
	}

	linenoiseAddCompletion(autoComplete, concat);
}
