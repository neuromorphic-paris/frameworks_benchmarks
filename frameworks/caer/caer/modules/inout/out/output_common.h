#ifndef OUTPUT_COMMON_H_
#define OUTPUT_COMMON_H_

#include <libcaer/ringbuffer.h>
#include "caer-sdk/module.h"
#include "../inout_common.h"
#include "libuv.h"

#ifdef HAVE_PTHREADS
#include "caer-sdk/cross/c11threads_posix.h"
#endif

#define MAX_OUTPUT_RINGBUFFER_GET 10
#define MAX_OUTPUT_QUEUED_SIZE (1 * 1024 * 1024) // 1MB outstanding writes

struct output_common_netio {
	/// Keep the full network header around, so we can easily update and write it.
	struct aedat3_network_header networkHeader;
	bool isTCP;
	bool isUDP;
	bool isPipe;
	void *address;
	uv_loop_t loop;
	uv_async_t shutdown;
	uv_idle_t ringBufferGet;
	uv_stream_t *server;
	size_t activeClients;
	size_t clientsSize;
	uv_stream_t *clients[];
};

typedef struct output_common_netio *outputCommonNetIO;

struct output_common_statistics {
	uint64_t packetsNumber;
	uint64_t packetsTotalSize;
	uint64_t packetsHeaderSize;
	uint64_t packetsDataSize;
	uint64_t dataWritten;
};

struct output_common_state {
	/// Control flag for output handling thread.
	atomic_bool running;
	/// The compression handling thread (separate as to not hold up processing).
	thrd_t compressorThread;
	/// The output handling thread (separate as to not hold up processing).
	thrd_t outputThread;
	/// Detect unrecoverable failure of output thread. Used so that the compressor
	/// thread can break out of trying to send data to the output thread, if that
	/// one is incapable of accepting it.
	atomic_bool outputThreadFailure;
	/// Track source ID (cannot change!). One source per I/O module!
	atomic_int_fast16_t sourceID;
	/// Source information string for that particular source ID.
	/// Must be set by mainloop, external threads cannot get it directly!
	char *sourceInfoString;
	/// The file descriptor for file writing.
	int fileIO;
	/// Network-like stream or file-like stream. Matters for header format.
	bool isNetworkStream;
	/// The libuv stream descriptors for network writing and server mode.
	outputCommonNetIO networkIO;
	/// Filter out invalidated events or not.
	atomic_bool validOnly;
	/// Force all incoming packets to be committed to the transfer ring-buffer.
	/// This results in no loss of data, but may slow down processing considerably.
	/// It may also block it altogether, if the output goes away for any reason.
	atomic_bool keepPackets;
	/// Transfer packets coming from a mainloop run to the compression handling thread.
	/// We use EventPacketContainers as data structure for convenience, they do exactly
	/// keep track of the data we do want to transfer and are part of libcaer.
	caerRingBuffer compressorRing;
	/// Transfer buffers to output handling thread.
	caerRingBuffer outputRing;
	/// Track last packet container's highest event timestamp that was sent out.
	int64_t lastTimestamp;
	/// Support different formats, providing data compression.
	int8_t formatID;
	/// Output module statistics collection.
	struct output_common_statistics statistics;
	/// Reference to parent module's original data.
	caerModuleData parentModule;
};

typedef struct output_common_state *outputCommonState;

bool caerOutputCommonInit(caerModuleData moduleData, int fileDescriptor, outputCommonNetIO streams);
void caerOutputCommonExit(caerModuleData moduleData);
void caerOutputCommonRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
void caerOutputCommonReset(caerModuleData moduleData, int16_t resetCallSourceID);
void caerOutputCommonOnServerConnection(uv_stream_t *server, int status);
void caerOutputCommonOnClientConnection(uv_connect_t *connectionRequest, int status);

#endif /* OUTPUT_COMMON_H_ */
