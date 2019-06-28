#include "module.h"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <iterator>
#include <mutex>
#include <regex>
#include <thread>
#include <vector>

static struct {
	std::vector<boost::filesystem::path> modulePaths;
	std::recursive_mutex modulePathsMutex;
} glModuleData;

static void caerModuleShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void caerModuleLogLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

void caerModuleConfigInit(sshsNode moduleNode) {
	// Per-module log level support. Initialize with global log level value.
	sshsNodeCreateInt(moduleNode, "logLevel", caerLogLevelGet(), CAER_LOG_EMERGENCY, CAER_LOG_DEBUG, SSHS_FLAGS_NORMAL,
		"Module-specific log-level.");

	// Initialize shutdown controls. By default modules always run.
	sshsNodeCreateBool(moduleNode, "runAtStartup", true, SSHS_FLAGS_NORMAL,
		"Start this module when the mainloop starts."); // Allow for users to disable a module at start.

	// Call module's configInit function to create default static config.
	const std::string moduleName = sshsNodeGetStdString(moduleNode, "moduleLibrary");

	// Load library to get module functions.
	std::pair<ModuleLibrary, caerModuleInfo> mLoad;

	try {
		mLoad = caerLoadModuleLibrary(moduleName);
	}
	catch (const std::exception &ex) {
		boost::format exMsg = boost::format("moduleConfigInit() load for '%s': %s") % moduleName % ex.what();
		libcaer::log::log(libcaer::log::logLevel::ERROR, sshsNodeGetName(moduleNode), exMsg.str().c_str());
		return;
	}

	if (mLoad.second->functions->moduleConfigInit != nullptr) {
		try {
			mLoad.second->functions->moduleConfigInit(moduleNode);
		}
		catch (const std::exception &ex) {
			boost::format exMsg = boost::format("moduleConfigInit() for '%s': %s") % moduleName % ex.what();
			libcaer::log::log(libcaer::log::logLevel::ERROR, sshsNodeGetName(moduleNode), exMsg.str().c_str());
		}
	}

	caerUnloadModuleLibrary(mLoad.first);
}

void caerModuleSM(caerModuleFunctions moduleFunctions, caerModuleData moduleData, size_t memSize,
	caerEventPacketContainer in, caerEventPacketContainer *out) {
	bool running = moduleData->running.load(std::memory_order_relaxed);

	if (moduleData->moduleStatus == CAER_MODULE_RUNNING && running) {
		if (moduleData->configUpdate.load(std::memory_order_relaxed) != 0) {
			moduleData->configUpdate.store(0);

			if (moduleFunctions->moduleConfig != nullptr) {
				// Call config function. 'configUpdate' variable reset is done above.
				try {
					moduleFunctions->moduleConfig(moduleData);
				}
				catch (const std::exception &ex) {
					libcaer::log::log(libcaer::log::logLevel::ERROR, moduleData->moduleSubSystemString,
						"moduleConfig(): '%s', disabling module.", ex.what());
					sshsNodePut(moduleData->moduleNode, "running", false);
					return;
				}
			}
		}

		if (moduleFunctions->moduleRun != nullptr) {
			try {
				moduleFunctions->moduleRun(moduleData, in, out);
			}
			catch (const std::exception &ex) {
				libcaer::log::log(libcaer::log::logLevel::ERROR, moduleData->moduleSubSystemString,
					"moduleRun(): '%s', disabling module.", ex.what());
				sshsNodePut(moduleData->moduleNode, "running", false);
				return;
			}
		}

		if (moduleData->doReset.load(std::memory_order_relaxed) != 0) {
			int16_t resetCallSourceID = I16T(moduleData->doReset.exchange(0));

			if (moduleFunctions->moduleReset != nullptr) {
				// Call reset function. 'doReset' variable reset is done above.
				try {
					moduleFunctions->moduleReset(moduleData, resetCallSourceID);
				}
				catch (const std::exception &ex) {
					libcaer::log::log(libcaer::log::logLevel::ERROR, moduleData->moduleSubSystemString,
						"moduleReset(): '%s', disabling module.", ex.what());
					sshsNodePut(moduleData->moduleNode, "running", false);
					return;
				}
			}
		}
	}
	else if (moduleData->moduleStatus == CAER_MODULE_STOPPED && running) {
		// Check that all modules this module depends on are also running.
		int16_t *neededModules;
		size_t neededModulesSize = caerMainloopModuleGetInputDeps(moduleData->moduleID, &neededModules);

		if (neededModulesSize > 0) {
			for (size_t i = 0; i < neededModulesSize; i++) {
				if (caerMainloopModuleGetStatus(neededModules[i]) != CAER_MODULE_RUNNING) {
					free(neededModules);
					return;
				}
			}

			free(neededModules);
		}

		// Allocate memory for module state.
		if (memSize != 0) {
			moduleData->moduleState = calloc(1, memSize);
			if (moduleData->moduleState == nullptr) {
				return;
			}
		}
		else {
			// memSize is zero, so moduleState must be nullptr.
			moduleData->moduleState = nullptr;
		}

		// Reset variables, as the following Init() is stronger than a reset
		// and implies a full configuration update. This avoids stale state
		// forcing an update and/or reset right away in the first run of
		// the module, which is unneeded and wasteful.
		moduleData->configUpdate.store(0);
		moduleData->doReset.store(0);

		if (moduleFunctions->moduleInit != nullptr) {
			try {
				if (!moduleFunctions->moduleInit(moduleData)) {
					throw std::runtime_error("Failed to initialize module.");
				}
			}
			catch (const std::exception &ex) {
				libcaer::log::log(
					libcaer::log::logLevel::ERROR, moduleData->moduleSubSystemString, "moduleInit(): %s", ex.what());

				if (memSize != 0) {
					// Only deallocate if we were the original allocator.
					free(moduleData->moduleState);
				}
				moduleData->moduleState = nullptr;

				return;
			}
		}

		moduleData->moduleStatus = CAER_MODULE_RUNNING;

		// After starting successfully, try to enable dependent
		// modules if their 'runAtStartup' is true. Else shutting down
		// an input would kill everything until mainloop restart.
		// This is consistent with initial mainloop start.
		int16_t *dependantModules;
		size_t dependantModulesSize = caerMainloopModuleGetOutputRevDeps(moduleData->moduleID, &dependantModules);

		if (dependantModulesSize > 0) {
			for (size_t i = 0; i < dependantModulesSize; i++) {
				sshsNode moduleConfigNode = caerMainloopModuleGetConfigNode(dependantModules[i]);

				if (sshsNodeGetBool(moduleConfigNode, "runAtStartup")) {
					sshsNodePut(moduleConfigNode, "running", true);
				}
			}

			free(dependantModules);
		}
	}
	else if (moduleData->moduleStatus == CAER_MODULE_RUNNING && !running) {
		moduleData->moduleStatus = CAER_MODULE_STOPPED;

		if (moduleFunctions->moduleExit != nullptr) {
			try {
				moduleFunctions->moduleExit(moduleData);
			}
			catch (const std::exception &ex) {
				libcaer::log::log(
					libcaer::log::logLevel::ERROR, moduleData->moduleSubSystemString, "moduleExit(): %s", ex.what());
			}
		}

		if (memSize != 0) {
			// Only deallocate if we were the original allocator.
			free(moduleData->moduleState);
		}
		moduleData->moduleState = nullptr;

		// Shutdown of module: ensure all modules depending on this
		// one also get stopped (running set to false).
		int16_t *dependantModules;
		size_t dependantModulesSize = caerMainloopModuleGetOutputRevDeps(moduleData->moduleID, &dependantModules);

		if (dependantModulesSize > 0) {
			for (size_t i = 0; i < dependantModulesSize; i++) {
				sshsNode moduleConfigNode = caerMainloopModuleGetConfigNode(dependantModules[i]);

				sshsNodePut(moduleConfigNode, "running", false);
			}

			free(dependantModules);
		}
	}
}

caerModuleData caerModuleInitialize(int16_t moduleID, const char *moduleName, sshsNode moduleNode) {
	// Allocate memory for the module.
	caerModuleData moduleData = (caerModuleData) calloc(1, sizeof(struct caer_module_data));
	if (moduleData == nullptr) {
		caerLog(CAER_LOG_ALERT, moduleName, "Failed to allocate memory for module. Error: %d.", errno);
		return (nullptr);
	}

	// Set module ID for later identification (used as quick key often).
	moduleData->moduleID = moduleID;

	// Set configuration node (so it's user accessible).
	moduleData->moduleNode = moduleNode;

	// Put module into startup state. 'running' flag is updated later based on user startup wishes.
	moduleData->moduleStatus = CAER_MODULE_STOPPED;

	// Setup default full log string name.
	size_t nameLength                 = strlen(moduleName);
	moduleData->moduleSubSystemString = (char *) malloc(nameLength + 1);
	if (moduleData->moduleSubSystemString == nullptr) {
		free(moduleData);

		caerLog(CAER_LOG_ALERT, moduleName, "Failed to allocate subsystem string for module.");
		return (nullptr);
	}

	strncpy(moduleData->moduleSubSystemString, moduleName, nameLength);
	moduleData->moduleSubSystemString[nameLength] = '\0';

	// Ensure static configuration is created on each module initialization.
	caerModuleConfigInit(moduleNode);

	// Per-module log level support.
	uint8_t logLevel = U8T(sshsNodeGetInt(moduleData->moduleNode, "logLevel"));

	moduleData->moduleLogLevel.store(logLevel, std::memory_order_relaxed);
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleLogLevelListener);

	// Initialize shutdown controls.
	bool runModule = sshsNodeGetBool(moduleData->moduleNode, "runAtStartup");

	sshsNodeCreateBool(
		moduleData->moduleNode, "running", false, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "Module start/stop.");
	sshsNodePutBool(moduleData->moduleNode, "running", runModule);

	moduleData->running.store(runModule, std::memory_order_relaxed);
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleShutdownListener);

	std::atomic_thread_fence(std::memory_order_release);

	return (moduleData);
}

void caerModuleDestroy(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleShutdownListener);
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleLogLevelListener);

	// Deallocate module memory. Module state has already been destroyed.
	free(moduleData->moduleSubSystemString);
	free(moduleData);
}

static void caerModuleShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData data = (caerModuleData) userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		atomic_store(&data->running, changeValue.boolean);
	}
}

static void caerModuleLogLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData data = (caerModuleData) userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_INT && caerStrEquals(changeKey, "logLevel")) {
		atomic_store(&data->moduleLogLevel, U8T(changeValue.iint));
	}
}

std::pair<ModuleLibrary, caerModuleInfo> caerLoadModuleLibrary(const std::string &moduleName) {
	// For each module, we search if a path exists to load it from.
	// If yes, we do so. The various OS's shared library load mechanisms
	// will keep track of reference count if same module is loaded
	// multiple times.
	boost::filesystem::path modulePath;

	{
		std::lock_guard<std::recursive_mutex> lock(glModuleData.modulePathsMutex);

		for (const auto &p : glModuleData.modulePaths) {
			if (moduleName == p.stem().string()) {
				// Found a module with same name!
				modulePath = p;
			}
		}
	}

	if (modulePath.empty()) {
		boost::format exMsg = boost::format("No module library for '%s' found.") % moduleName;
		throw std::runtime_error(exMsg.str());
	}

#if BOOST_HAS_DLL_LOAD
	ModuleLibrary moduleLibrary;
	try {
		moduleLibrary.load(modulePath.c_str(), boost::dll::load_mode::rtld_now);
	}
	catch (const std::exception &ex) {
		// Failed to load shared library!
		boost::format exMsg
			= boost::format("Failed to load library '%s', error: '%s'.") % modulePath.string() % ex.what();
		throw std::runtime_error(exMsg.str());
	}

	caerModuleInfo (*getInfo)(void);
	try {
		getInfo = moduleLibrary.get<caerModuleInfo(void)>("caerModuleGetInfo");
	}
	catch (const std::exception &ex) {
		// Failed to find symbol in shared library!
		caerUnloadModuleLibrary(moduleLibrary);
		boost::format exMsg
			= boost::format("Failed to find symbol in library '%s', error: '%s'.") % modulePath.string() % ex.what();
		throw std::runtime_error(exMsg.str());
	}
#else
	void *moduleLibrary = dlopen(modulePath.c_str(), RTLD_NOW);
	if (moduleLibrary == nullptr) {
		// Failed to load shared library!
		boost::format exMsg
			= boost::format("Failed to load library '%s', error: '%s'.") % modulePath.string() % dlerror();
		throw std::runtime_error(exMsg.str());
	}

	caerModuleInfo (*getInfo)(void) = (caerModuleInfo(*)(void)) dlsym(moduleLibrary, "caerModuleGetInfo");
	if (getInfo == nullptr) {
		// Failed to find symbol in shared library!
		caerUnloadModuleLibrary(moduleLibrary);
		boost::format exMsg
			= boost::format("Failed to find symbol in library '%s', error: '%s'.") % modulePath.string() % dlerror();
		throw std::runtime_error(exMsg.str());
	}
#endif

	caerModuleInfo info = (*getInfo)();
	if (info == nullptr) {
		caerUnloadModuleLibrary(moduleLibrary);
		boost::format exMsg = boost::format("Failed to get info from library '%s'.") % modulePath.string();
		throw std::runtime_error(exMsg.str());
	}

	return (std::pair<ModuleLibrary, caerModuleInfo>(moduleLibrary, info));
}

// Small helper to unload libraries on error.
void caerUnloadModuleLibrary(ModuleLibrary &moduleLibrary) {
#if BOOST_HAS_DLL_LOAD
	moduleLibrary.unload();
#else
	dlclose(moduleLibrary);
#endif
}

static void checkInputOutputStreamDefinitions(caerModuleInfo info) {
	if (info->type == CAER_MODULE_INPUT) {
		if (info->inputStreams != nullptr || info->inputStreamsSize != 0 || info->outputStreams == nullptr
			|| info->outputStreamsSize == 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type INPUT.");
		}
	}
	else if (info->type == CAER_MODULE_OUTPUT) {
		if (info->inputStreams == nullptr || info->inputStreamsSize == 0 || info->outputStreams != nullptr
			|| info->outputStreamsSize != 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type OUTPUT.");
		}

		// Also ensure that all input streams of an output module are marked read-only.
		bool readOnlyError = false;

		for (size_t i = 0; i < info->inputStreamsSize; i++) {
			if (!info->inputStreams[i].readOnly) {
				readOnlyError = true;
				break;
			}
		}

		if (readOnlyError) {
			throw std::domain_error("Input event streams not marked read-only for type OUTPUT.");
		}
	}
	else {
		// CAER_MODULE_PROCESSOR
		if (info->inputStreams == nullptr || info->inputStreamsSize == 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type PROCESSOR.");
		}

		// If no output streams are defined, then at least one input event
		// stream must not be readOnly, so that there is modified data to output.
		if (info->outputStreams == nullptr || info->outputStreamsSize == 0) {
			bool readOnlyError = true;

			for (size_t i = 0; i < info->inputStreamsSize; i++) {
				if (!info->inputStreams[i].readOnly) {
					readOnlyError = false;
					break;
				}
			}

			if (readOnlyError) {
				throw std::domain_error(
					"No output streams and all input streams are marked read-only for type PROCESSOR.");
			}
		}
	}
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * Number must be either -1 or well defined (1-INT16_MAX). Zero not allowed.
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then number must also be -1; having a defined
 * number in this case makes no sense (N of any type???), a special exception
 * is made for the number 1 (1 of any type) with inputs, which can be useful.
 * Also this must then be the only definition.
 * If number is -1, then either the type is also -1 and this is the
 * only event stream definition (same as rule above), OR the type is well
 * defined and this is the only event stream definition for that type.
 */
static void checkInputStreamDefinitions(caerEventStreamIn inputStreams, size_t inputStreamsSize) {
	for (size_t i = 0; i < inputStreamsSize; i++) {
		// Check type range.
		if (inputStreams[i].type < -1) {
			throw std::domain_error("Input stream has invalid type value.");
		}

		// Check number range.
		if (inputStreams[i].number < -1 || inputStreams[i].number == 0) {
			throw std::domain_error("Input stream has invalid number value.");
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && inputStreams[i - 1].type >= inputStreams[i].type) {
			throw std::domain_error("Input stream has invalid order of declaration or duplicates.");
		}

		// Check that any type is always together with any number or 1, and the
		// only definition present in that case.
		if (inputStreams[i].type == -1
			&& ((inputStreams[i].number != -1 && inputStreams[i].number != 1) || inputStreamsSize != 1)) {
			throw std::domain_error("Input stream has invalid any declaration.");
		}
	}
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then this must then be the only definition.
 */
static void checkOutputStreamDefinitions(caerEventStreamOut outputStreams, size_t outputStreamsSize) {
	// If type is any, must be the only definition.
	if (outputStreamsSize == 1 && outputStreams[0].type == -1) {
		return;
	}

	for (size_t i = 0; i < outputStreamsSize; i++) {
		// Check type range.
		if (outputStreams[i].type < 0) {
			throw std::domain_error("Output stream has invalid type value.");
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && outputStreams[i - 1].type >= outputStreams[i].type) {
			throw std::domain_error("Output stream has invalid order of declaration or duplicates.");
		}
	}
}

void caerUpdateModulesInformation() {
	std::lock_guard<std::recursive_mutex> lock(glModuleData.modulePathsMutex);

	sshsNode modulesNode = sshsGetNode(sshsGetGlobal(), "/caer/modules/");

	// Clear out modules information.
	sshsNodeClearSubTree(modulesNode, false);
	glModuleData.modulePaths.clear();

	// Search for available modules. Will be loaded as needed later.
	const std::string modulesSearchPath = sshsNodeGetStdString(modulesNode, "modulesSearchPath");

	// Split on '|'.
	std::vector<std::string> searchPaths;
	boost::algorithm::split(searchPaths, modulesSearchPath, boost::is_any_of("|"));

	// Search is recursive for binary shared libraries.
	const std::regex moduleRegex("\\w+\\.(so|dll|dylib)");

	for (const auto &sPath : searchPaths) {
		if (!boost::filesystem::exists(sPath)) {
			continue;
		}

		std::for_each(boost::filesystem::recursive_directory_iterator(sPath),
			boost::filesystem::recursive_directory_iterator(),
			[&moduleRegex](const boost::filesystem::directory_entry &e) {
				if (boost::filesystem::exists(e.path()) && boost::filesystem::is_regular_file(e.path())
					&& std::regex_match(e.path().filename().string(), moduleRegex)) {
					glModuleData.modulePaths.push_back(e.path());
				}
			});
	}

	// Sort and unique.
	vectorSortUnique(glModuleData.modulePaths);

	// No modules, cannot start!
	if (glModuleData.modulePaths.empty()) {
		boost::format exMsg = boost::format("Failed to find any modules on path(s) '%s'.") % modulesSearchPath;
		throw std::runtime_error(exMsg.str());
	}

	// Generate nodes for each module, with their in/out information as attributes.
	// This also checks basic validity of the module's information.
	auto iter = std::begin(glModuleData.modulePaths);

	while (iter != std::end(glModuleData.modulePaths)) {
		std::string moduleName = iter->stem().string();

		// Load library.
		std::pair<ModuleLibrary, caerModuleInfo> mLoad;

		try {
			mLoad = caerLoadModuleLibrary(moduleName);
		}
		catch (const std::exception &ex) {
			boost::format exMsg = boost::format("Module '%s': %s") % moduleName % ex.what();
			libcaer::log::log(libcaer::log::logLevel::ERROR, "Module", exMsg.str().c_str());

			iter = glModuleData.modulePaths.erase(iter);
			continue;
		}

		try {
			// Check that the modules respect the basic I/O definition requirements.
			checkInputOutputStreamDefinitions(mLoad.second);

			// Check I/O event stream definitions for correctness.
			if (mLoad.second->inputStreams != nullptr) {
				checkInputStreamDefinitions(mLoad.second->inputStreams, mLoad.second->inputStreamsSize);
			}

			if (mLoad.second->outputStreams != nullptr) {
				checkOutputStreamDefinitions(mLoad.second->outputStreams, mLoad.second->outputStreamsSize);
			}
		}
		catch (const std::exception &ex) {
			boost::format exMsg = boost::format("Module '%s': %s") % moduleName % ex.what();
			libcaer::log::log(libcaer::log::logLevel::ERROR, "Module", exMsg.str().c_str());

			caerUnloadModuleLibrary(mLoad.first);

			iter = glModuleData.modulePaths.erase(iter);
			continue;
		}

		// Get SSHS node under /caer/modules/.
		sshsNode moduleNode = sshsGetRelativeNode(modulesNode, moduleName + "/");

		// Parse caerModuleInfo into SSHS.
		sshsNodeCreate(moduleNode, "version", I32T(mLoad.second->version), 0, INT32_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Module version.");
		sshsNodeCreate(moduleNode, "name", mLoad.second->name, 1, 256, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Module name.");
		sshsNodeCreate(moduleNode, "description", mLoad.second->description, 1, 8192,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Module description.");
		sshsNodeCreate(moduleNode, "type", caerModuleTypeToString(mLoad.second->type), 1, 64,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Module type.");

		if (mLoad.second->inputStreamsSize > 0) {
			sshsNode inputStreamsNode = sshsGetRelativeNode(moduleNode, "inputStreams/");

			sshsNodeCreate(inputStreamsNode, "size", I32T(mLoad.second->inputStreamsSize), 1, INT16_MAX,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of input streams.");

			for (size_t i = 0; i < mLoad.second->inputStreamsSize; i++) {
				sshsNode inputStreamNode      = sshsGetRelativeNode(inputStreamsNode, std::to_string(i) + "/");
				caerEventStreamIn inputStream = &mLoad.second->inputStreams[i];

				sshsNodeCreate(inputStreamNode, "type", inputStream->type, I16T(-1), I16T(INT16_MAX),
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Input event type (-1 for any type).");
				sshsNodeCreate(inputStreamNode, "number", inputStream->number, I16T(-1), I16T(INT16_MAX),
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of inputs of this type (-1 for any number).");
				sshsNodeCreate(inputStreamNode, "readOnly", inputStream->readOnly,
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Whether this input is modified or not.");
			}
		}

		if (mLoad.second->outputStreamsSize > 0) {
			sshsNode outputStreamsNode = sshsGetRelativeNode(moduleNode, "outputStreams/");

			sshsNodeCreate(outputStreamsNode, "size", I32T(mLoad.second->outputStreamsSize), 1, INT16_MAX,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of output streams.");

			for (size_t i = 0; i < mLoad.second->outputStreamsSize; i++) {
				sshsNode outputStreamNode       = sshsGetRelativeNode(outputStreamsNode, std::to_string(i) + "/");
				caerEventStreamOut outputStream = &mLoad.second->outputStreams[i];

				sshsNodeCreate(outputStreamNode, "type", outputStream->type, I16T(-1), I16T(INT16_MAX),
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
					"Output event type (-1 for undefined output determined at runtime).");
			}
		}

		// Done, unload library.
		caerUnloadModuleLibrary(mLoad.first);

		iter++;
	}

	// Got all available modules, expose them as a sorted list.
	std::vector<std::string> modulePathsSorted;
	for (const auto &modulePath : glModuleData.modulePaths) {
		modulePathsSorted.push_back(modulePath.stem().string());
	}

	std::sort(modulePathsSorted.begin(), modulePathsSorted.end());

	std::string modulesList;
	for (const auto &modulePath : modulePathsSorted) {
		modulesList += (modulePath + ",");
	}
	modulesList.pop_back(); // Remove trailing comma.

	sshsNodeUpdateReadOnlyAttribute(modulesNode, "modulesListOptions", modulesList);
}
