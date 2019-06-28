#ifndef INPUT_COMMON_H_
#define INPUT_COMMON_H_

#include <libcaer/ringbuffer.h>
#include "caer-sdk/buffers.h"
#include "caer-sdk/module.h"
#include "../inout_common.h"
#include "ext/uthash/utarray.h"
#include <unistd.h>

#ifdef HAVE_PTHREADS
#include "caer-sdk/cross/c11threads_posix.h"
#endif

struct input_common_header_info {
	/// Header has been completely read and is valid.
	atomic_bool isValidHeader;
	/// Format is AEDAT 3.
	bool isAEDAT3;
	/// Major AEDAT format version (X.y).
	int16_t majorVersion;
	/// Minor AEDAT format version (x.Y)
	int8_t minorVersion;
	/// AEDAT 3 Format ID (from Format header), used for decoding.
	int8_t formatID;
	/// Track source ID (cannot change!) to read data for. One source per I/O module!
	int16_t sourceID;
	/// Keep track of the sequence number for message-based protocols.
	int64_t networkSequenceNumber;
};

struct input_packet_data {
	/// Numerical ID of a packet. First packet has ID 0.
	size_t id;
	/// Data offset, in bytes.
	size_t offset;
	/// Data size, in bytes.
	size_t size;
	/// Is this packet compressed?
	bool isCompressed;
	/// Contained event type.
	int16_t eventType;
	/// Size of contained events, in bytes.
	int32_t eventSize;
	/// Contained number of events.
	int32_t eventNumber;
	/// Contained number of valid events.
	int32_t eventValid;
	/// First (lowest) timestamp.
	int64_t startTimestamp;
	/// Last (highest) timestamp.
	int64_t endTimestamp;
	/// Doubly-linked list pointers.
	struct input_packet_data *prev, *next;
};

typedef struct input_packet_data *packetData;

struct input_common_packet_data {
	/// Current packet header, to support headers being split across buffers.
	uint8_t currPacketHeader[CAER_EVENT_PACKET_HEADER_SIZE];
	/// Current packet header length (determines if complete or not).
	size_t currPacketHeaderSize;
	/// Current packet, to get filled up with data.
	caerEventPacketHeader currPacket;
	/// Current packet data length.
	size_t currPacketDataSize;
	/// Current packet offset, index into data.
	size_t currPacketDataOffset;
	/// Skip over packets coming from other sources. We only support one!
	size_t skipSize;
	/// Current packet data for packet list book-keeping.
	packetData currPacketData;
	/// List of data on all parsed original packets from the input.
	packetData packetsList;
	/// Global packet counter.
	size_t packetCount;
};

struct input_common_packet_container_data {
	/// Current events, merged into packets, sorted by type.
	UT_array *eventPackets;
	/// The first main timestamp (the one relevant for packet ordering in streams)
	/// of the last event packet that was handled.
	int64_t lastPacketTimestamp;
	/// Track tsOverflow value. On change, we must commit the current packet
	/// container content and empty it out.
	int32_t lastTimestampOverflow;
	/// Size limit reached in any packet.
	bool sizeLimitHit;
	/// The timestamp that needs to be read up to, so that the size limit can
	/// actually be committed, because we know no other events are around.
	int64_t sizeLimitTimestamp;
	/// The timestamp up to which we want to (have to!) read, so that we can
	/// output the next packet container (in time-slice mode).
	int64_t newContainerTimestampEnd;
	/// The size limit that triggered the hit above.
	int32_t newContainerSizeLimit;
	/// Size slice (in events), for which to generate a packet container.
	atomic_int_fast32_t sizeSlice;
	/// Time slice (in µs), for which to generate a packet container.
	atomic_int_fast32_t timeSlice;
	/// Time delay (in µs) between the start of two consecutive time slices.
	/// This is used for real-time slow-down.
	atomic_int_fast32_t timeDelay;
	/// Time when the last packet container was sent out, used to calculate
	/// sleep time to reach user configured 'timeDelay'.
	struct timespec lastCommitTime;
};

struct input_common_state {
	/// Control flag for input handling threads.
	atomic_bool running;
	/// Reader thread state, to signal conditions like EOF or error to
	/// the assembler thread.
	atomic_int_fast32_t inputReaderThreadState;
	/// The first input handling thread (separate as to only wake up mainloop
	/// processing when there is new data available): takes care of data
	/// reading and parsing, decompression from the input channel.
	thrd_t inputReaderThread;
	/// The first input handling thread (separate as to only wake up mainloop
	/// processing when there is new data available): takes care of assembling
	/// packet containers that respect the specs using the packets read by
	/// the inputReadThread. This is separate so that delay operations don't
	/// use up resources that could be doing read/decompression work.
	thrd_t inputAssemblerThread;
	/// Network-like stream or file-like stream. Matters for header format.
	bool isNetworkStream;
	/// For network-like inputs, we differentiate between stream and message
	/// based protocols, like TCP and UDP. Matters for header/sequence number.
	bool isNetworkMessageBased;
	/// Filter out invalidated events or not.
	atomic_bool validOnly;
	/// Force all incoming packets to be committed to the transfer ring-buffer.
	/// This results in no loss of data, but may deviate from the requested
	/// real-time play-back expectations.
	atomic_bool keepPackets;
	/// Pause support.
	atomic_bool pause;
	/// Transfer packets coming from the input reading thread to the assembly
	/// thread. Normal EventPackets are used here.
	caerRingBuffer transferRingPackets;
	/// Transfer packet containers coming from the input assembly thread to
	/// the mainloop. We use EventPacketContainers, as that is the standard
	/// data structure returned from an input module.
	caerRingBuffer transferRingPacketContainers;
	/// Track how many packet containers are in the ring-buffer, ready for
	/// consumption by the user. The Mainloop's 'dataAvailable' variable already
	/// does this at a global level, but we also need to keep track at a local
	/// (module) level of this, to avoid confusion in the case multiple Inputs
	/// are inside the same Mainloop, which is entirely possible and supported.
	atomic_uint_fast32_t dataAvailableModule;
	/// Header parsing results.
	struct input_common_header_info header;
	/// Packet data parsing structures.
	struct input_common_packet_data packets;
	/// Packet container data structure, to generate from packets.
	struct input_common_packet_container_data packetContainer;
	/// The file descriptor for reading.
	int fileDescriptor;
	/// Data buffer for reading from file descriptor (buffered I/O).
	simpleBuffer dataBuffer;
	/// Offset for current data buffer.
	size_t dataBufferOffset;
	/// Flag to signal update to buffer configuration asynchronously.
	atomic_bool bufferUpdate;
	/// Reference to parent module's original data.
	caerModuleData parentModule;
	/// Reference to sourceInfo node (to avoid getting it each time again).
	sshsNode sourceInfoNode;
};

typedef struct input_common_state *inputCommonState;

bool caerInputCommonInit(caerModuleData moduleData, int readFd, bool isNetworkStream, bool isNetworkMessageBased);
void caerInputCommonExit(caerModuleData moduleData);
void caerInputCommonRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);

#endif /* INPUT_COMMON_H_ */
