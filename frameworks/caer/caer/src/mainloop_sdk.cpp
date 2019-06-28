#include "mainloop.h"

static MainloopData *glMainloopDataPtr;

void caerMainloopSDKLibInit(MainloopData *setMainloopPtr) {
	glMainloopDataPtr = setMainloopPtr;
}

void caerMainloopDataNotifyIncrease(void *p) {
	UNUSED_ARGUMENT(p);

	glMainloopDataPtr->dataAvailable.fetch_add(1, std::memory_order_release);
}

void caerMainloopDataNotifyDecrease(void *p) {
	UNUSED_ARGUMENT(p);

	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	glMainloopDataPtr->dataAvailable.fetch_sub(1, std::memory_order_relaxed);
}

bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId) {
	return (findBool(
		glMainloopDataPtr->streams.cbegin(), glMainloopDataPtr->streams.cend(), ActiveStreams(sourceId, typeId)));
}

bool caerMainloopModuleExists(int16_t id) {
	return (glMainloopDataPtr->modules.count(id) == 1);
}

enum caer_module_type caerMainloopModuleGetType(int16_t id) {
	return (glMainloopDataPtr->modules.at(id).libraryInfo->type);
}

uint32_t caerMainloopModuleGetVersion(int16_t id) {
	return (glMainloopDataPtr->modules.at(id).libraryInfo->version);
}

enum caer_module_status caerMainloopModuleGetStatus(int16_t id) {
	return (glMainloopDataPtr->modules.at(id).runtimeData->moduleStatus);
}

sshsNode caerMainloopModuleGetConfigNode(int16_t id) {
	return (glMainloopDataPtr->modules.at(id).runtimeData->moduleNode);
}

size_t caerMainloopModuleGetInputDeps(int16_t id, int16_t **inputDepIds) {
	// Ensure is set to NULL for error return.
	// Support passing in NULL directly if not interested in result.
	if (inputDepIds != nullptr) {
		*inputDepIds = nullptr;
	}

	// Only makes sense to be called from PROCESSORs or OUTPUTs, as INPUTs
	// do not have inputs themselves.
	if (caerMainloopModuleGetType(id) == CAER_MODULE_INPUT) {
		return (0);
	}

	std::vector<int16_t> inputModuleIds;
	inputModuleIds.reserve(glMainloopDataPtr->modules.at(id).inputDefinition.size());

	// Get all module IDs of inputs to this module (each present only once in
	// 'inputDefinition' of module), then sort them and return if so requested.
	for (auto &in : glMainloopDataPtr->modules.at(id).inputDefinition) {
		inputModuleIds.push_back(in.first);
	}

	std::sort(inputModuleIds.begin(), inputModuleIds.end());

	if (inputDepIds != nullptr && !inputModuleIds.empty()) {
		*inputDepIds = (int16_t *) malloc(inputModuleIds.size() * sizeof(int16_t));
		if (*inputDepIds == nullptr) {
			// Memory allocation failure, report as if nothing found.
			return (0);
		}

		memcpy(*inputDepIds, inputModuleIds.data(), inputModuleIds.size() * sizeof(int16_t));
	}

	return (inputModuleIds.size());
}

size_t caerMainloopModuleGetOutputRevDeps(int16_t id, int16_t **outputRevDepIds) {
	// Ensure is set to NULL for error return.
	// Support passing in NULL directly if not interested in result.
	if (outputRevDepIds != nullptr) {
		*outputRevDepIds = nullptr;
	}

	// Only makes sense to be called from INPUTs or PROCESSORs, as OUTPUTs
	// do not have outputs themselves.
	if (caerMainloopModuleGetType(id) == CAER_MODULE_OUTPUT) {
		return (0);
	}

	std::vector<int16_t> outputModuleIds;

	// Get all IDs of modules that depend on outputs of this module.
	// Those are usually called reverse dependencies.
	// Look at all the streams that originate from this module ID,
	// if any exist take their users and then remove duplicates.
	for (const auto &st : glMainloopDataPtr->streams) {
		if (st.sourceId == id) {
			outputModuleIds.insert(outputModuleIds.end(), st.users.cbegin(), st.users.cend());
		}
	}

	vectorSortUnique(outputModuleIds);

	if (outputRevDepIds != nullptr && !outputModuleIds.empty()) {
		*outputRevDepIds = (int16_t *) malloc(outputModuleIds.size() * sizeof(int16_t));
		if (*outputRevDepIds == nullptr) {
			// Memory allocation failure, report as if nothing found.
			return (0);
		}

		memcpy(*outputRevDepIds, outputModuleIds.data(), outputModuleIds.size() * sizeof(int16_t));
	}

	return (outputModuleIds.size());
}

size_t caerMainloopModuleResetOutputRevDeps(int16_t id) {
	// Find and reset all reverse dependencies of a particular module.
	int16_t *outputRevDepIds;
	size_t numRevDeps = caerMainloopModuleGetOutputRevDeps(id, &outputRevDepIds);

	if (numRevDeps > 0) {
		for (size_t i = 0; i < numRevDeps; i++) {
			if (glMainloopDataPtr->modules.at(outputRevDepIds[i]).runtimeData->moduleStatus == CAER_MODULE_RUNNING) {
				glMainloopDataPtr->modules.at(outputRevDepIds[i]).runtimeData->doReset.store(id);
			}
		}

		free(outputRevDepIds);
	}

	return (numRevDeps);
}

sshsNode caerMainloopModuleGetSourceNodeForInput(int16_t id, size_t inputNum) {
	int16_t *inputModules;
	size_t inputModulesNum = caerMainloopModuleGetInputDeps(id, &inputModules);

	if (inputNum >= inputModulesNum) {
		return (nullptr);
	}

	int16_t sourceId = inputModules[inputNum];

	free(inputModules);

	return (caerMainloopGetSourceNode(sourceId));
}

sshsNode caerMainloopModuleGetSourceInfoForInput(int16_t id, size_t inputNum) {
	int16_t *inputModules;
	size_t inputModulesNum = caerMainloopModuleGetInputDeps(id, &inputModules);

	if (inputNum >= inputModulesNum) {
		return (nullptr);
	}

	int16_t sourceId = inputModules[inputNum];

	free(inputModules);

	return (caerMainloopGetSourceInfo(sourceId));
}

static inline caerModuleData caerMainloopGetSourceData(int16_t sourceID) {
	// Sources must be INPUTs or PROCESSORs.
	if (caerMainloopModuleGetType(sourceID) == CAER_MODULE_OUTPUT) {
		return (nullptr);
	}

	// Sources must actually produce some event stream.
	bool isSource = false;

	for (const auto &st : glMainloopDataPtr->streams) {
		if (st.sourceId == sourceID) {
			isSource = true;
			break;
		}
	}

	if (!isSource) {
		return (nullptr);
	}

	return (glMainloopDataPtr->modules.at(sourceID).runtimeData);
}

sshsNode caerMainloopGetSourceNode(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleNode);
}

void *caerMainloopGetSourceState(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleState);
}

sshsNode caerMainloopGetSourceInfo(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	// All sources should have a sub-node in SSHS called 'sourceInfo/',
	// while they are running only (so check running and existence).
	if (moduleData->moduleStatus == CAER_MODULE_STOPPED) {
		return (nullptr);
	}

	if (!sshsExistsRelativeNode(moduleData->moduleNode, "sourceInfo/")) {
		return (nullptr);
	}

	return (sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/"));
}
