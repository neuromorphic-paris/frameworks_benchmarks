#ifndef MAINLOOP_H_
#define MAINLOOP_H_

#include "caer-sdk/mainloop.h"
#include "caer-sdk/module.h"
#include "module.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct OrderedInput {
	int16_t typeId;
	int16_t afterModuleId;
	bool copyNeeded;

	OrderedInput(int16_t t, int16_t a) : typeId(t), afterModuleId(a), copyNeeded(false) {
	}

	// Comparison operators.
	bool operator==(const OrderedInput &rhs) const noexcept {
		return (typeId == rhs.typeId);
	}

	bool operator!=(const OrderedInput &rhs) const noexcept {
		return (typeId != rhs.typeId);
	}

	bool operator<(const OrderedInput &rhs) const noexcept {
		return (typeId < rhs.typeId);
	}

	bool operator>(const OrderedInput &rhs) const noexcept {
		return (typeId > rhs.typeId);
	}

	bool operator<=(const OrderedInput &rhs) const noexcept {
		return (typeId <= rhs.typeId);
	}

	bool operator>=(const OrderedInput &rhs) const noexcept {
		return (typeId >= rhs.typeId);
	}
};

struct ModuleInfo {
	// Module identification.
	int16_t id;
	const std::string name;
	// SSHS configuration node.
	sshsNode configNode;
	// Parsed moduleInput configuration.
	std::unordered_map<int16_t, std::vector<OrderedInput>> inputDefinition;
	// Connectivity graph (I/O).
	std::vector<std::pair<ssize_t, ssize_t>> inputs;
	std::unordered_map<int16_t, ssize_t> outputs;
	// Loadable module support.
	const std::string library;
	ModuleLibrary libraryHandle;
	caerModuleInfo libraryInfo;
	// Module runtime data.
	caerModuleData runtimeData;

	ModuleInfo()
		: id(-1), name(), configNode(nullptr), library(), libraryHandle(), libraryInfo(nullptr), runtimeData(nullptr) {
	}

	ModuleInfo(int16_t i, const std::string &n, sshsNode c, const std::string &l)
		: id(i), name(n), configNode(c), library(l), libraryHandle(), libraryInfo(nullptr), runtimeData(nullptr) {
	}
};

struct DependencyNode;

struct DependencyLink {
	int16_t id;
	std::shared_ptr<DependencyNode> next;

	DependencyLink(int16_t i) : id(i) {
	}

	// Comparison operators.
	bool operator==(const DependencyLink &rhs) const noexcept {
		return (id == rhs.id);
	}

	bool operator!=(const DependencyLink &rhs) const noexcept {
		return (id != rhs.id);
	}

	bool operator<(const DependencyLink &rhs) const noexcept {
		return (id < rhs.id);
	}

	bool operator>(const DependencyLink &rhs) const noexcept {
		return (id > rhs.id);
	}

	bool operator<=(const DependencyLink &rhs) const noexcept {
		return (id <= rhs.id);
	}

	bool operator>=(const DependencyLink &rhs) const noexcept {
		return (id >= rhs.id);
	}
};

struct DependencyNode {
	size_t depth;
	int16_t parentId;
	DependencyNode *parentLink;
	std::vector<DependencyLink> links;

	DependencyNode(size_t d, int16_t pId, DependencyNode *pLink) : depth(d), parentId(pId), parentLink(pLink) {
	}
};

struct ActiveStreams {
	int16_t sourceId;
	int16_t typeId;
	bool isProcessor;
	std::vector<int16_t> users;
	std::shared_ptr<DependencyNode> dependencies;

	ActiveStreams(int16_t s, int16_t t) : sourceId(s), typeId(t), isProcessor(false) {
	}

	// Comparison operators.
	bool operator==(const ActiveStreams &rhs) const noexcept {
		return (sourceId == rhs.sourceId && typeId == rhs.typeId);
	}

	bool operator!=(const ActiveStreams &rhs) const noexcept {
		return (sourceId != rhs.sourceId || typeId != rhs.typeId);
	}

	bool operator<(const ActiveStreams &rhs) const noexcept {
		return (sourceId < rhs.sourceId || (sourceId == rhs.sourceId && typeId < rhs.typeId));
	}

	bool operator>(const ActiveStreams &rhs) const noexcept {
		return (sourceId > rhs.sourceId || (sourceId == rhs.sourceId && typeId > rhs.typeId));
	}

	bool operator<=(const ActiveStreams &rhs) const noexcept {
		return (sourceId < rhs.sourceId || (sourceId == rhs.sourceId && typeId <= rhs.typeId));
	}

	bool operator>=(const ActiveStreams &rhs) const noexcept {
		return (sourceId > rhs.sourceId || (sourceId == rhs.sourceId && typeId >= rhs.typeId));
	}
};

struct MainloopData {
	sshsNode configNode;
	atomic_bool systemRunning;
	atomic_bool running;
	atomic_uint_fast32_t dataAvailable;
	size_t copyCount;
	std::unordered_map<int16_t, ModuleInfo> modules;
	std::vector<ActiveStreams> streams;
	std::vector<std::reference_wrapper<ModuleInfo>> globalExecution;
	std::vector<caerEventPacketHeader> eventPackets;
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run global mainloop (data processing).
 */
void caerMainloopRun(void);

/**
 * Only for internal usage! Do not reset the mainloop pointer!
 */
void caerMainloopSDKLibInit(MainloopData *setMainloopPtr);

#ifdef __cplusplus
}
#endif

#endif /* MAINLOOP_H_ */
