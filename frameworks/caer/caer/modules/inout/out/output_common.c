/*
 * Here we handle all outputs in a common way, taking in event packets
 * as input and writing a byte buffer to a stream as output.
 * The main-loop part is responsible for gathering the event packets,
 * copying them and their events (valid or not depending on configuration),
 * and putting them on a transfer ring-buffer. A second thread, called the
 * output handler, gets the packet groups from there, orders them according
 * to the AEDAT 3.X format specification, and breaks them up into chunks as
 * directed to write them to a file descriptor efficiently (buffered I/O).
 * The AEDAT 3.X format specification specifically states that there is no
 * relation at all between packets from different sources at the output level,
 * that they behave as if independent, which we do here to simplify the system
 * considerably: one output module (or Sink) can only work with packets from
 * one source. Multiple sources will have to go to multiple output modules!
 * The other stipulation in the AEDAT 3.X specifications is on ordering of
 * events from the same source: the first timestamp of a packet determines
 * its order in the packet stream, from smallest timestamp to largest, which
 * is the logical monotonic increasing time ordering you'd expect.
 * This kind of ordering is useful and simplifies reading back data later on;
 * if you read a packet of type A with TS A-TS1, when you next read a packet of
 * the same type A, with TS A-TS2, you know you must also have read all other
 * events, of this AND all other present types, with a timestamp between A-TS1
 * and (A-TS2 - 1). This makes time-based reading and replaying of data very easy
 * and efficient, so time-slice playback or real-time playback get relatively
 * simple to implement. Data-amount based playback is always relatively easy.
 *
 * Now, outputting event packets in this particular order from an output module
 * requires some additional processing: before you can write out packet A with TS
 * A-TS1, you need to be sure no other packets with a timestamp smaller than
 * A-TS1 can come afterwards (the only solution would be to discard them at
 * that point to maintain the correct ordering, and you'd want to avoid that).
 * We cannot assume a constant and quick data flow, since at any point during a
 * recording, data producers can be turned off, packet size etc. configuration
 * changed, or some events, like Special ones, are rare to begin with during
 * normal camera operation (for example the TIMESTAMP_WRAP every 35 minutes).
 * But we'd like to write data continuously and as soon as possible!
 * Thankfully cAER/libcaer come to the rescue due to a small but important
 * detail of how input modules are implemented (input modules are all those
 * modules that create new data in some way, also called a Source).
 * They either create sequences of single packets, where the ordering is trivial,
 * or so called 'Packet Containers', which do offer timestamp-related guarantees.
 * From the libcaer/events/packetContainer.h documentation:
 *
 * "An EventPacketContainer is a logical construct that contains packets
 * of events (EventPackets) of different event types, with the aim of
 * keeping related events of differing types, such as DVS and IMU data,
 * together. Such a relation is usually based on time intervals, trying
 * to keep groups of event happening in a certain time-slice together.
 * This time-order is based on the *main* time-stamp of an event, the one
 * whose offset is referenced in the event packet header and that is
 * used by the caerGenericEvent*() functions. It's guaranteed that all
 * conforming input modules keep to this rule, generating containers
 * that include all events from all types within the given time-slice."
 *
 * Understanding this gives a simple solution to the problem above: if we
 * see all the packets contained in a packet container, which is the case
 * for each run through of the cAER mainloop (as it fetches *one* new packet
 * container each time from an input module), we can order the packets of
 * the container correctly, and write them out to a file descriptor.
 * Then we just rinse and repeat for every new packet container.
 * The assumption of one run of the mainloop getting at most one packet
 * container from each Source is correct with the current implementation,
 * and future designs of Sources should take this into account! Delays in
 * packet containers getting to the output module are still allowed, provided
 * the ordering doesn't change and single packets aren't mixed, which is
 * a sane restriction to impose anyway.
 */

#include "output_common.h"
#include "caer-sdk/buffers.h"
#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/cross/portable_threads.h"
#include "caer-sdk/mainloop.h"
#include "ext/net_rw.h"

#ifdef ENABLE_INOUT_PNG_COMPRESSION
#include <png.h>
#endif

#include <libcaer/events/common.h>
#include <libcaer/events/frame.h>
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/special.h>
#include <stdatomic.h>

static void caerOutputCommonConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

/**
 * ============================================================================
 * MAIN THREAD
 * ============================================================================
 * Handle Run and Reset operations on main thread. Data packets are copied into
 * the transferRing for processing by the compressor thread.
 * ============================================================================
 */
static void copyPacketsToTransferRing(outputCommonState state, caerEventPacketContainer packetsContainer);

void caerOutputCommonRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	outputCommonState state = moduleData->moduleState;

	copyPacketsToTransferRing(state, in);
}

void caerOutputCommonReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	outputCommonState state = moduleData->moduleState;

	if (resetCallSourceID == I16T(atomic_load_explicit(&state->sourceID, memory_order_relaxed))) {
		// The timestamp reset call came in from the Source ID this output module
		// is responsible for, so we ensure the timestamps are reset and that the
		// special event packet goes out for sure.

		// Send lone packet container with just TS_RESET.
		// Allocate packet container just for this event.
		caerEventPacketContainer tsResetContainer = caerEventPacketContainerAllocate(1);
		if (tsResetContainer == NULL) {
			caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Failed to allocate tsReset event packet container.");
			return;
		}

		// Allocate special packet just for this event.
		caerSpecialEventPacket tsResetPacket
			= caerSpecialEventPacketAllocate(1, resetCallSourceID, I32T(state->lastTimestamp >> 31));
		if (tsResetPacket == NULL) {
			caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Failed to allocate tsReset special event packet.");
			return;
		}

		// Create timestamp reset event.
		caerSpecialEvent tsResetEvent = caerSpecialEventPacketGetEvent(tsResetPacket, 0);
		caerSpecialEventSetTimestamp(tsResetEvent, INT32_MAX);
		caerSpecialEventSetType(tsResetEvent, TIMESTAMP_RESET);
		caerSpecialEventValidate(tsResetEvent, tsResetPacket);

		// Assign special packet to packet container.
		caerEventPacketContainerSetEventPacket(tsResetContainer, SPECIAL_EVENT, (caerEventPacketHeader) tsResetPacket);

		while (!caerRingBufferPut(state->compressorRing, tsResetContainer)) {
			; // Ensure this goes into the first ring-buffer.
		}

		// Reset timestamp checking.
		state->lastTimestamp = 0;
	}
}

/**
 * Copy event packets to the ring buffer for transfer to the output handler thread.
 *
 * @param state output module state.
 * @param packetsContainer a container with all the event packets to send out.
 */
static void copyPacketsToTransferRing(outputCommonState state, caerEventPacketContainer packetsContainer) {
	caerEventPacketHeaderConst packets[caerEventPacketContainerGetEventPacketsNumber(packetsContainer)];
	size_t packetsSize = 0;

	// Count how many packets are really there, skipping empty event packets.
	for (int32_t i = 0; i < caerEventPacketContainerGetEventPacketsNumber(packetsContainer); i++) {
		caerEventPacketHeaderConst packetHeader = caerEventPacketContainerGetEventPacketConst(packetsContainer, i);

		// Found non-empty event packet.
		if (packetHeader != NULL) {
			// Get source information from the event packet.
			int16_t eventSource = caerEventPacketHeaderGetEventSource(packetHeader);

			// Check that source is unique.
			int16_t sourceID = I16T(atomic_load_explicit(&state->sourceID, memory_order_relaxed));

			if (sourceID == -1) {
				sshsNode sourceInfoNode = caerMainloopGetSourceInfo(eventSource);
				if (sourceInfoNode == NULL) {
					// This should never happen, but we handle it gracefully.
					caerModuleLog(
						state->parentModule, CAER_LOG_ERROR, "Failed to get source info to setup output module.");
					return;
				}

				state->sourceInfoString = sshsNodeGetString(sourceInfoNode, "sourceString");

				atomic_store(&state->sourceID, eventSource); // Remember this!
			}
			else if (sourceID != eventSource) {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR,
					"An output module can only handle packets from the same source! "
					"A packet with source %" PRIi16
					" was sent, but this output module expects only packets from source %" PRIi16 ".",
					eventSource, sourceID);
				continue;
			}

			// Source ID is correct, packet is not empty, we got it!
			packets[packetsSize++] = packetHeader;
		}
	}

	// There was nothing in this mainloop run!
	if (packetsSize == 0) {
		return;
	}

	// Filter out the TS_RESET packet, as we ensure that that one is always present in the
	// caerOutputCommonReset() function, so that even if the special event stream is not
	// output/captured by this module, the TS_RESET event will be present in the output.
	// The TS_RESET event would be alone in a packet that is also the only one in its
	// packetContainer/mainloop cycle, so we can check for this very efficiently.
	if ((packetsSize == 1) && (caerEventPacketHeaderGetEventType(packets[0]) == SPECIAL_EVENT)
		&& (caerEventPacketHeaderGetEventNumber(packets[0]) == 1)
		&& (caerSpecialEventPacketFindEventByTypeConst((caerSpecialEventPacketConst) packets[0], TIMESTAMP_RESET)
			   != NULL)) {
		return;
	}

	// Allocate memory for event packet array structure that will get passed to output handler thread.
	caerEventPacketContainer eventPackets = caerEventPacketContainerAllocate((int32_t) packetsSize);
	if (eventPackets == NULL) {
		return;
	}

	// Handle the valid only flag here, that way we don't have to do another copy and
	// process it in the output handling thread. We get the value once here, so we do
	// the same for all packets from the same mainloop run, avoiding mid-way changes.
	bool validOnly = atomic_load_explicit(&state->validOnly, memory_order_relaxed);

	// Now copy each event packet and send the array out. Track how many packets there are.
	size_t idx               = 0;
	int64_t highestTimestamp = 0;

	for (size_t i = 0; i < packetsSize; i++) {
		if ((validOnly && (caerEventPacketHeaderGetEventValid(packets[i]) == 0))
			|| (!validOnly && (caerEventPacketHeaderGetEventNumber(packets[i]) == 0))) {
			caerModuleLog(state->parentModule, CAER_LOG_NOTICE,
				"Submitted empty event packet to output. Ignoring empty event packet.");
			continue;
		}

		const void *cpFirstEvent      = caerGenericEventGetEvent(packets[i], 0);
		int64_t cpFirstEventTimestamp = caerGenericEventGetTimestamp64(cpFirstEvent, packets[i]);

		if (cpFirstEventTimestamp < state->lastTimestamp) {
			// Smaller TS than already sent, illegal, ignore packet.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR,
				"Detected timestamp going back, expected at least %" PRIi64 " but got %" PRIi64 "."
				" Ignoring packet of type %" PRIi16 " from source %" PRIi16 ", with %" PRIi32 " events!",
				state->lastTimestamp, cpFirstEventTimestamp, caerEventPacketHeaderGetEventType(packets[i]),
				caerEventPacketHeaderGetEventSource(packets[i]), caerEventPacketHeaderGetEventNumber(packets[i]));
			continue;
		}
		else {
			// Bigger or equal TS than already sent, this is good. Strict TS ordering ensures
			// that all other packets in this container are the same.
			// Update highest timestamp for this packet container, based upon its valid packets.
			const void *cpLastEvent
				= caerGenericEventGetEvent(packets[i], caerEventPacketHeaderGetEventNumber(packets[i]) - 1);
			int64_t cpLastEventTimestamp = caerGenericEventGetTimestamp64(cpLastEvent, packets[i]);

			if (cpLastEventTimestamp > highestTimestamp) {
				highestTimestamp = cpLastEventTimestamp;
			}
		}

		if (validOnly) {
			caerEventPacketContainerSetEventPacket(
				eventPackets, (int32_t) idx, caerEventPacketCopyOnlyValidEvents(packets[i]));
		}
		else {
			caerEventPacketContainerSetEventPacket(
				eventPackets, (int32_t) idx, caerEventPacketCopyOnlyEvents(packets[i]));
		}

		if (caerEventPacketContainerGetEventPacket(eventPackets, (int32_t) idx) == NULL) {
			// Failed to copy packet. Signal but try to continue anyway.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to copy event packet to output.");
		}
		else {
			idx++;
		}
	}

	// We might have failed to copy all packets (unlikely), or skipped all of them
	// due to timestamp check failures.
	if (idx == 0) {
		caerEventPacketContainerFree(eventPackets);

		return;
	}

	// Remember highest timestamp for check in next iteration. Only update
	// if we actually got any packets through.
	state->lastTimestamp = highestTimestamp;

	// Reset packet container size so we only consider the packets we managed
	// to successfully copy.
	caerEventPacketContainerSetEventPacketsNumber(eventPackets, (int32_t) idx);

retry:
	if (!caerRingBufferPut(state->compressorRing, eventPackets)) {
		if (atomic_load_explicit(&state->keepPackets, memory_order_relaxed)) {
			// Delay by 500 µs if no change, to avoid a wasteful busy loop.
			struct timespec retrySleep = {.tv_sec = 0, .tv_nsec = 500000};
			thrd_sleep(&retrySleep, NULL);

			// Retry forever if requested.
			goto retry;
		}

		caerEventPacketContainerFree(eventPackets);

		caerModuleLog(
			state->parentModule, CAER_LOG_NOTICE, "Failed to put packet's array copy on transfer ring-buffer: full.");
	}
}

/**
 * ============================================================================
 * COMPRESSOR THREAD
 * ============================================================================
 * Handle data ordering, compression, and filling of final byte buffers, that
 * will be sent out by the Output thread.
 * ============================================================================
 */
static int compressorThread(void *stateArg);

static void orderAndSendEventPackets(outputCommonState state, caerEventPacketContainer currPacketContainer);
static int packetsFirstTimestampThenTypeCmp(const void *a, const void *b);
static void sendEventPacket(outputCommonState state, caerEventPacketHeader packet);
static size_t compressEventPacket(outputCommonState state, caerEventPacketHeader packet, size_t packetSize);
static size_t compressTimestampSerialize(outputCommonState state, caerEventPacketHeader packet);

#ifdef ENABLE_INOUT_PNG_COMPRESSION
static void caerLibPNGWriteBuffer(png_structp png_ptr, png_bytep data, png_size_t length);
static size_t compressFramePNG(outputCommonState state, caerEventPacketHeader packet);
#endif

static int compressorThread(void *stateArg) {
	outputCommonState state = stateArg;

	// Set thread name.
	size_t threadNameLength = strlen(state->parentModule->moduleSubSystemString);
	char threadName[threadNameLength + 1 + 12]; // +1 for NUL character.
	strcpy(threadName, state->parentModule->moduleSubSystemString);
	strcat(threadName, "[Compressor]");
	portable_thread_set_name(threadName);

	// If no data is available on the transfer ring-buffer, sleep for 1 ms.
	// to avoid wasting resources in a busy loop.
	struct timespec noDataSleep = {.tv_sec = 0, .tv_nsec = 1000000};

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		// Get the newest event packet container from the transfer ring-buffer.
		caerEventPacketContainer currPacketContainer = caerRingBufferGet(state->compressorRing);
		if (currPacketContainer == NULL) {
			// There is none, so we can't work on and commit this.
			// We just sleep here a little and then try again, as we need the data!
			thrd_sleep(&noDataSleep, NULL);
			continue;
		}

		// Respect time order as specified in AEDAT 3.X format: first event's main
		// timestamp decides its ordering with regards to other packets. Smaller
		// comes first. If equal, order by increasing type ID as a convenience,
		// not strictly required by specification!
		orderAndSendEventPackets(state, currPacketContainer);
	}

	// Handle shutdown, write out all content remaining in the transfer ring-buffer.
	caerEventPacketContainer packetContainer;
	while ((packetContainer = caerRingBufferGet(state->compressorRing)) != NULL) {
		orderAndSendEventPackets(state, packetContainer);
	}

	return (thrd_success);
}

static void orderAndSendEventPackets(outputCommonState state, caerEventPacketContainer currPacketContainer) {
	// Sort container by first timestamp (required) and by type ID (convenience).
	size_t currPacketContainerSize = (size_t) caerEventPacketContainerGetEventPacketsNumber(currPacketContainer);

	qsort(currPacketContainer->eventPackets, currPacketContainerSize, sizeof(caerEventPacketHeader),
		&packetsFirstTimestampThenTypeCmp);

	for (size_t cpIdx = 0; cpIdx < currPacketContainerSize; cpIdx++) {
		// Send the packets out to the file descriptor.
		sendEventPacket(state, caerEventPacketContainerGetEventPacket(currPacketContainer, (int32_t) cpIdx));
	}

	// Free packet container. The individual packets have already been either
	// freed on error, or have been transferred out.
	free(currPacketContainer);
}

static int packetsFirstTimestampThenTypeCmp(const void *a, const void *b) {
	const caerEventPacketHeader *aa = a;
	const caerEventPacketHeader *bb = b;

	// Sort first by timestamp of the first event.
	int32_t eventTimestampA = caerGenericEventGetTimestamp(caerGenericEventGetEvent(*aa, 0), *aa);
	int32_t eventTimestampB = caerGenericEventGetTimestamp(caerGenericEventGetEvent(*bb, 0), *bb);

	if (eventTimestampA < eventTimestampB) {
		return (-1);
	}
	else if (eventTimestampA > eventTimestampB) {
		return (1);
	}
	else {
		// If equal, further sort by type ID.
		int16_t eventTypeA = caerEventPacketHeaderGetEventType(*aa);
		int16_t eventTypeB = caerEventPacketHeaderGetEventType(*bb);

		if (eventTypeA < eventTypeB) {
			return (-1);
		}
		else if (eventTypeA > eventTypeB) {
			return (1);
		}
		else {
			return (0);
		}
	}
}

static void sendEventPacket(outputCommonState state, caerEventPacketHeader packet) {
	// Calculate total size of packet, in bytes.
	size_t packetSize = CAER_EVENT_PACKET_HEADER_SIZE + (size_t)(caerEventPacketHeaderGetEventNumber(packet)
																 * caerEventPacketHeaderGetEventSize(packet));

	// Statistics support.
	state->statistics.packetsNumber++;
	state->statistics.packetsTotalSize += packetSize;
	state->statistics.packetsHeaderSize += CAER_EVENT_PACKET_HEADER_SIZE;
	state->statistics.packetsDataSize
		+= (size_t)(caerEventPacketHeaderGetEventNumber(packet) * caerEventPacketHeaderGetEventSize(packet));

	if (state->formatID != 0) {
		packetSize = compressEventPacket(state, packet, packetSize);
	}

	// Statistics support (after compression).
	state->statistics.dataWritten += packetSize;

	// Send compressed packet out to output handling thread.
	// Already format it as a libuv buffer.
	libuvWriteBuf packetBuffer = malloc(sizeof(*packetBuffer));
	if (packetBuffer == NULL) {
		free(packet);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for libuv packet buffer.");
		return;
	}

	libuvWriteBufInitWithAnyBuffer(packetBuffer, packet, packetSize);

	// Put packet buffer onto output ring-buffer. Retry until successful.
	while (!caerRingBufferPut(state->outputRing, packetBuffer)) {
		// If the output thread failed, we'd forever block here, if it can't accept
		// any more data. So we detect that condition and discard remaining packets.
		if (atomic_load_explicit(&state->outputThreadFailure, memory_order_relaxed)) {
			break;
		}

		// Delay by 500 µs if no change, to avoid a wasteful busy loop.
		struct timespec retrySleep = {.tv_sec = 0, .tv_nsec = 500000};
		thrd_sleep(&retrySleep, NULL);
	}
}

/**
 * Compress event packets.
 * Compressed event packets have the highest bit of the type field
 * set to '1' (type | 0x8000). Their eventCapacity field holds the
 * new, true length of the data portion of the packet, in bytes.
 * This takes advantage of the fact capacity always equals number
 * in any input/output stream, and as such is redundant information.
 *
 * @param state common output state.
 * @param packet the event packet to compress.
 * @param packetSize the current event packet size (header + data).
 *
 * @return the event packet size (header + data) after compression.
 *         Must be equal or smaller than the input packetSize.
 */
static size_t compressEventPacket(outputCommonState state, caerEventPacketHeader packet, size_t packetSize) {
	size_t compressedSize = packetSize;

	// Data compression technique 1: serialize timestamps for event types that tend to repeat them a lot.
	// Currently, this means polarity events.
	if ((state->formatID & 0x01) && caerEventPacketHeaderGetEventType(packet) == POLARITY_EVENT) {
		compressedSize = compressTimestampSerialize(state, packet);
	}

#ifdef ENABLE_INOUT_PNG_COMPRESSION
	// Data compression technique 2: do PNG compression on frames, Grayscale and RGB(A).
	if ((state->formatID & 0x02) && caerEventPacketHeaderGetEventType(packet) == FRAME_EVENT) {
		compressedSize = compressFramePNG(state, packet);
	}
#endif

	// If any compression was possible, we mark the packet as compressed
	// and store its data size in eventCapacity.
	if (compressedSize != packetSize) {
		packet->eventType     = htole16(le16toh(packet->eventType) | I16T(0x8000));
		packet->eventCapacity = htole32(I32T(compressedSize) - CAER_EVENT_PACKET_HEADER_SIZE);
	}

	// Return size after compression.
	return (compressedSize);
}

/**
 * Search for runs of at least 3 events with the same timestamp, and convert them to a special
 * sequence: leave first event unchanged, but mark its timestamp as special by setting the
 * highest bit (bit 31) to one (it is forbidden for timestamps in memory to have that bit set for
 * signed-integer-only language compatibility). Then, for the second event, change its timestamp
 * to a 4-byte integer saying how many more events will follow afterwards with this same timestamp
 * (this is used for decoding), so only their data portion will be given. Then follow with those
 * event's data, back to back, with their timestamps removed.
 * So let's assume there are 6 events with TS=1234. In memory this looks like this:
 * E1(data,ts), E2(data,ts), E3(data,ts), E4(data,ts), E5(data,ts), E6(data,ts)
 * After the timestamp serialization compression step:
 * E1(data,ts|0x80000000), E2(data,4), E3(data), E4(data), E5(data), E5(data)
 * This change is only in the data itself, not in the packet headers, so that we can still use the
 * eventNumber and eventSize fields to calculate memory allocation when doing decompression.
 * As such, to correctly interpret this data, the Format flags must be correctly set. All current
 * file or network formats do specify those as mandatory in their headers, so we can rely on that.
 * Also all event types where this kind of thing makes any sense do have the timestamp as their last
 * data member in their struct, so we can use that information, stored in tsOffset header field,
 * together with eventSize, to come up with a generic implementation applicable to all other event
 * types that satisfy this condition of TS-as-last-member (so we can use that offset as event size).
 * When this is enabled, it requires full iteration thorough the whole event packet, both at
 * compression and at decompression time.
 *
 * @param state common output state.
 * @param packet the packet to timestamp-compress.
 *
 * @return the event packet size (header + data) after compression.
 *         Must be equal or smaller than the input packetSize.
 */
static size_t compressTimestampSerialize(outputCommonState state, caerEventPacketHeader packet) {
	UNUSED_ARGUMENT(state);

	size_t currPacketOffset = CAER_EVENT_PACKET_HEADER_SIZE; // Start here, no change to header.
	int32_t eventSize       = caerEventPacketHeaderGetEventSize(packet);
	int32_t eventTSOffset   = caerEventPacketHeaderGetEventTSOffset(packet);

	int32_t lastTS = -1;
	int32_t currTS = -1;
	size_t tsRun   = 0;
	bool doMemMove = false; // Initially don't move memory, until we actually shrink the size.

	for (int32_t caerIteratorCounter = 0; caerIteratorCounter <= caerEventPacketHeaderGetEventNumber(packet);
		 caerIteratorCounter++) {
		// Iterate until one element past the end, to flush the last run. In that particular case,
		// we don't get a new element or TS, as we'd be past the end of the array.
		if (caerIteratorCounter < caerEventPacketHeaderGetEventNumber(packet)) {
			const void *caerIteratorElement = caerGenericEventGetEvent(packet, caerIteratorCounter);

			currTS = caerGenericEventGetTimestamp(caerIteratorElement, packet);
			if (currTS == lastTS) {
				// Increase size of run of same TS events currently being seen.
				tsRun++;
				continue;
			}
		}

		// TS are different, at this point look if the last run was long enough
		// and if it makes sense to compress. It does starting with 3 events.
		if (tsRun >= 3) {
			// First event to remains there, we set its TS highest bit.
			const uint8_t *firstEvent = caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun--);
			caerGenericEventSetTimestamp(
				(void *) firstEvent, packet, caerGenericEventGetTimestamp(firstEvent, packet) | I32T(0x80000000));

			// Now use second event's timestamp for storing how many further events.
			const uint8_t *secondEvent = caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun--);
			caerGenericEventSetTimestamp((void *) secondEvent, packet, I32T(tsRun)); // Is at least 1.

			// Finally move modified memory where it needs to go.
			if (doMemMove) {
				memmove(((uint8_t *) packet) + currPacketOffset, firstEvent, (size_t) eventSize * 2);
			}
			else {
				doMemMove = true; // After first shrink always move memory.
			}
			currPacketOffset += (size_t) eventSize * 2;

			// Now go through remaining events and move their data close together.
			while (tsRun > 0) {
				const uint8_t *thirdEvent = caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun--);
				memmove(((uint8_t *) packet) + currPacketOffset, thirdEvent, (size_t) eventTSOffset);
				currPacketOffset += (size_t) eventTSOffset;
			}
		}
		else {
			// Just copy data unchanged if no compression is possible.
			if (doMemMove) {
				const uint8_t *startEvent = caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun);
				memmove(((uint8_t *) packet) + currPacketOffset, startEvent, (size_t) eventSize * tsRun);
			}
			currPacketOffset += (size_t) eventSize * tsRun;
		}

		// Reset values for next iteration.
		lastTS = currTS;
		tsRun  = 1;
	}

	return (currPacketOffset);
}

#ifdef ENABLE_INOUT_PNG_COMPRESSION

// Simple structure to store PNG image bytes.
struct caer_libpng_buffer {
	uint8_t *buffer;
	size_t size;
};

static void caerLibPNGWriteBuffer(png_structp png_ptr, png_bytep data, png_size_t length) {
	struct caer_libpng_buffer *p = (struct caer_libpng_buffer *) png_get_io_ptr(png_ptr);
	size_t newSize               = p->size + length;
	uint8_t *bufferSave          = p->buffer;

	// Allocate or grow buffer as needed.
	if (p->buffer) {
		p->buffer = realloc(p->buffer, newSize);
	}
	else {
		p->buffer = malloc(newSize);
	}

	if (p->buffer == NULL) {
		free(bufferSave); // Free on realloc() failure.
		png_error(png_ptr, "Write Buffer Error");
	}

	// Copy the new bytes to the end of the buffer.
	memcpy(p->buffer + p->size, data, length);
	p->size += length;
}

static inline int caerFrameEventColorToLibPNG(enum caer_frame_event_color_channels channels) {
	switch (channels) {
		case GRAYSCALE:
			return (PNG_COLOR_TYPE_GRAY);
			break;

		case RGB:
			return (PNG_COLOR_TYPE_RGB);
			break;

		case RGBA:
		default:
			return (PNG_COLOR_TYPE_RGBA);
			break;
	}
}

static inline bool caerFrameEventPNGCompress(uint8_t **outBuffer, size_t *outSize, uint16_t *inBuffer, int32_t xSize,
	int32_t ySize, enum caer_frame_event_color_channels channels) {
	png_structp png_ptr     = NULL;
	png_infop info_ptr      = NULL;
	png_bytepp row_pointers = NULL;

	// Initialize the write struct.
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		return (false);
	}

	// Initialize the info struct.
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_write_struct(&png_ptr, NULL);
		return (false);
	}

	// Set up error handling.
	if (setjmp(png_jmpbuf(png_ptr))) {
		if (row_pointers != NULL) {
			png_free(png_ptr, row_pointers);
		}
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return (false);
	}

	// Set image attributes.
	png_set_IHDR(png_ptr, info_ptr, (png_uint_32) xSize, (png_uint_32) ySize, 16, caerFrameEventColorToLibPNG(channels),
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	// Handle endianness of 16-bit depth pixels correctly.
	// PNG assumes big-endian, our Frame Event is always little-endian.
	png_set_swap(png_ptr);

	// Initialize rows of PNG.
	row_pointers = png_malloc(png_ptr, (size_t) ySize * sizeof(png_bytep));
	if (row_pointers == NULL) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return (false);
	}

	for (size_t y = 0; y < (size_t) ySize; y++) {
		row_pointers[y] = (png_bytep) &inBuffer[y * (size_t) xSize * channels];
	}

	// Set write function to buffer one.
	struct caer_libpng_buffer state = {.buffer = NULL, .size = 0};
	png_set_write_fn(png_ptr, &state, &caerLibPNGWriteBuffer, NULL);

	// Actually write the image data.
	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	// Free allocated memory for rows.
	png_free(png_ptr, row_pointers);

	// Destroy main structs.
	png_destroy_write_struct(&png_ptr, &info_ptr);

	// Pass out buffer with resulting PNG image.
	*outBuffer = state.buffer;
	*outSize   = state.size;

	return (true);
}

static size_t compressFramePNG(outputCommonState state, caerEventPacketHeader packet) {
	size_t currPacketOffset = CAER_EVENT_PACKET_HEADER_SIZE; // Start here, no change to header.
	// '- sizeof(uint16_t)' to compensate for pixels[1] at end of struct for C++ compatibility.
	size_t frameEventHeaderSize = (sizeof(struct caer_frame_event) - sizeof(uint16_t));

	CAER_FRAME_ITERATOR_ALL_START((caerFrameEventPacket) packet)
	size_t pixelSize = caerFrameEventGetPixelsSize(caerFrameIteratorElement);

	uint8_t *outBuffer;
	size_t outSize;
	if (!caerFrameEventPNGCompress(&outBuffer, &outSize, caerFrameEventGetPixelArrayUnsafe(caerFrameIteratorElement),
			caerFrameEventGetLengthX(caerFrameIteratorElement), caerFrameEventGetLengthY(caerFrameIteratorElement),
			caerFrameEventGetChannelNumber(caerFrameIteratorElement))) {
		// Failed to generate PNG.
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to compress frame event. "
			"PNG generation from frame failed. Keeping uncompressed frame.");

		// Copy this frame uncompressed. Don't want to loose data.
		size_t fullCopySize = frameEventHeaderSize + pixelSize;
		memmove(((uint8_t *) packet) + currPacketOffset, caerFrameIteratorElement, fullCopySize);
		currPacketOffset += fullCopySize;

		continue;
	}

	// Add integer needed for storing PNG block length.
	size_t pngSize = outSize + sizeof(int32_t);

	// Check that the image didn't actually grow or fail to compress.
	// If we don't gain any size advantages, just keep it uncompressed.
	if (pngSize >= pixelSize) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to compress frame event. "
			"Image didn't shrink, original: %zu, compressed: %zu, difference: %zu.",
			pixelSize, pngSize, (pngSize - pixelSize));

		// Copy this frame uncompressed. Don't want to loose data.
		size_t fullCopySize = frameEventHeaderSize + pixelSize;
		memmove(((uint8_t *) packet) + currPacketOffset, caerFrameIteratorElement, fullCopySize);
		currPacketOffset += fullCopySize;

		free(outBuffer);
		continue;
	}

	// Mark frame as PNG compressed. Use info member in frame event header struct,
	// to store highest bit equals one.
	SET_NUMBITS32(caerFrameIteratorElement->info, 31, 0x01, 1);

	// Keep frame event header intact, copy all image data, move memory close together.
	memmove(((uint8_t *) packet) + currPacketOffset, caerFrameIteratorElement, frameEventHeaderSize);
	currPacketOffset += frameEventHeaderSize;

	// Store size of PNG image block as 4 byte integer.
	*((int32_t *) (((uint8_t *) packet) + currPacketOffset)) = htole32(I32T(outSize));
	currPacketOffset += sizeof(int32_t);

	memcpy(((uint8_t *) packet) + currPacketOffset, outBuffer, outSize);
	currPacketOffset += outSize;

	// Free allocated PNG block memory.
	free(outBuffer);
}

return (currPacketOffset);
}

#endif

/**
 * ============================================================================
 * OUTPUT THREAD
 * ============================================================================
 * Handle writing of data to output. Uses libuv/eventloop for network outputs,
 * while simple FD+writeUntilDone() for normal files.
 * ============================================================================
 */
static int outputThread(void *stateArg);
static void libuvRingBufferGet(uv_idle_t *handle);
static void libuvAsyncShutdown(uv_async_t *handle);
static void libuvClientShutdown(uv_shutdown_t *clientShutdown, int status);
static void libuvWriteStatusCheck(uv_handle_t *handle, int status);
static void writePacket(outputCommonState state, libuvWriteBuf packetBuffer);
static void initializeNetworkHeader(outputCommonState state);
static bool writeNetworkHeader(outputCommonNetIO streams, libuvWriteBuf buf, bool startOfUDPPacket);
static void writeFileHeader(outputCommonState state);

static inline _Noreturn void errorExit(outputCommonState state, libuvWriteBuf packetBuffer) {
	// Free currently held memory.
	if (packetBuffer != NULL) {
		free(packetBuffer->freeBuf);
		free(packetBuffer);
	}

	// Signal failure to compressor thread.
	atomic_store(&state->outputThreadFailure, true);

	// Ensure parent also shuts down on unrecoverable failures, taking the
	// compressor thread with it.
	sshsNodePutBool(state->parentModule->moduleNode, "running", false);

	thrd_exit(thrd_error);
}

static int outputThread(void *stateArg) {
	outputCommonState state = stateArg;

	// Set thread name.
	size_t threadNameLength = strlen(state->parentModule->moduleSubSystemString);
	char threadName[threadNameLength + 1 + 8]; // +1 for NUL character.
	strcpy(threadName, state->parentModule->moduleSubSystemString);
	strcat(threadName, "[Output]");
	portable_thread_set_name(threadName);

	bool headerSent = false;

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		// Wait for source to be defined.
		int16_t sourceID = I16T(atomic_load_explicit(&state->sourceID, memory_order_relaxed));
		if (sourceID == -1) {
			// Delay by 1 ms if no data, to avoid a wasteful busy loop.
			struct timespec delaySleep = {.tv_sec = 0, .tv_nsec = 1000000};
			thrd_sleep(&delaySleep, NULL);

			continue;
		}

		// Send appropriate header.
		if (state->isNetworkStream) {
			initializeNetworkHeader(state);
		}
		else {
			writeFileHeader(state);
		}

		headerSent = true;
		break;
	}

	// If no header sent, it means we exited (running=false) without ever getting any
	// event packet with a source ID, so we don't have to process anything.
	// But we make sure to empty the transfer ring-buffer, as something may have been
	// put there in the meantime, so we ensure it's checked and freed. This because
	// in caerOutputCommonExit() we expect the ring-buffer to always be empty!
	if (!headerSent) {
		libuvWriteBuf packetBuffer;
		while ((packetBuffer = caerRingBufferGet(state->outputRing)) != NULL) {
			free(packetBuffer->freeBuf);
			free(packetBuffer);
		}

		return (thrd_success);
	}

	// If destination is a file, just loop and write to it. Else start a libuv event loop.
	if (state->isNetworkStream) {
		// libuv network IO (state->networkIO != NULL).
		// Start libuv event loop.
		int retVal = uv_run(&state->networkIO->loop, UV_RUN_DEFAULT);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_run", errorExit(state, NULL));

		// Treat anything being still alive as an error too. Async shutdown should have closed everything!
		if (retVal > 0) {
			caerModuleLog(state->parentModule, CAER_LOG_WARNING, "uv_run() exited with still active handles.");
			errorExit(state, NULL);
		}
	}
	else {
		// If no data is available on the transfer ring-buffer, sleep for 1 ms.
		// to avoid wasting resources in a busy loop.
		struct timespec noDataSleep = {.tv_sec = 0, .tv_nsec = 1000000};

		while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
			libuvWriteBuf packetBuffer = caerRingBufferGet(state->outputRing);
			if (packetBuffer == NULL) {
				// There is none, so we can't work on and commit this.
				// We just sleep here a little and then try again, as we need the data!
				thrd_sleep(&noDataSleep, NULL);
				continue;
			}

			// Write buffer to file descriptor.
			if (!writeUntilDone(state->fileIO, (uint8_t *) packetBuffer->buf.base, packetBuffer->buf.len)) {
				errorExit(state, packetBuffer);
			}

			free(packetBuffer->freeBuf);
			free(packetBuffer);
		}

		// Write all remaining buffers to file.
		libuvWriteBuf packetBuffer;
		while ((packetBuffer = caerRingBufferGet(state->outputRing)) != NULL) {
			if (!writeUntilDone(state->fileIO, (uint8_t *) packetBuffer->buf.base, packetBuffer->buf.len)) {
				errorExit(state, packetBuffer);
			}

			free(packetBuffer->freeBuf);
			free(packetBuffer);
		}
	}

	return (thrd_success);
}

static void libuvRingBufferGet(uv_idle_t *handle) {
	outputCommonState state = handle->data;

	// Write all packets that are currently available out in order,
	// but never more than 10 at a time.
	size_t count = 0;
	libuvWriteBuf packetBuffer;
	while (count < MAX_OUTPUT_RINGBUFFER_GET && (packetBuffer = caerRingBufferGet(state->outputRing)) != NULL) {
		writePacket(state, packetBuffer);
		count++;
	}

	// If nothing, avoid busy loop within libuv event loop by sleeping a little.
	if (count == 0) {
		// Sleep for 1 ms.
		struct timespec noDataSleep = {.tv_sec = 0, .tv_nsec = 1000000};
		thrd_sleep(&noDataSleep, NULL);
	}
}

static void libuvAsyncShutdown(uv_async_t *handle) {
	// This is only ever called in response to caerOutputCommonExit().
	outputCommonState state = handle->data;

	// Shutdown, write remaining buffers to network.
	// First we stop and close the idle handles checking for new data,
	// then we manually schedule writes for the remaining data.
	int retVal = uv_idle_stop(&state->networkIO->ringBufferGet);
	UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_idle_stop", );

	uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, NULL);

	// Then we empty the ring-buffer and write out all data.
	libuvWriteBuf packetBuffer;
	while ((packetBuffer = caerRingBufferGet(state->outputRing)) != NULL) {
		writePacket(state, packetBuffer);
	}

	// Shutdown server (if it exists).
	if (state->networkIO->server != NULL) {
		uv_close((uv_handle_t *) state->networkIO->server, &libuvCloseFree);
	}

	// Then we tell all network outputs to shutdown, which will then close them once done.
	for (size_t i = 0; i < state->networkIO->clientsSize; i++) {
		uv_stream_t *client = state->networkIO->clients[i];

		if (client == NULL) {
			continue;
		}

		if (state->networkIO->isUDP) {
			// UDP has no shutdown, just close.
			uv_close((uv_handle_t *) client, &libuvCloseFree);
			continue;
		}

		uv_shutdown_t *clientShutdown = calloc(1, sizeof(*clientShutdown));
		if (clientShutdown == NULL) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for client shutdown.");

			// Hard close.
			uv_close((uv_handle_t *) client, &libuvCloseFree);
			continue;
		}

		retVal = uv_shutdown(clientShutdown, client, &libuvClientShutdown);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_shutdown", free(clientShutdown);
					 uv_close((uv_handle_t *) client, &libuvCloseFree));
	}

	// Close shutdown itself.
	uv_close((uv_handle_t *) &state->networkIO->shutdown, NULL);
}

static void libuvClientShutdown(uv_shutdown_t *clientShutdown, int status) {
	uv_close((uv_handle_t *) clientShutdown->handle, &libuvCloseFree);

	free(clientShutdown);

	UV_RET_CHECK(status, __func__, "AfterShutdown", );
}

static void libuvWriteStatusCheck(uv_handle_t *handle, int status) {
	if (status < 0) {
		// Write failed!
		caerLog(CAER_LOG_ERROR, __func__, "Write failed with error %d (%s). Closing connection.", status,
			uv_err_name(status));

		// Remove connection from list of active clients.
		outputCommonNetIO streams = handle->data;

		for (size_t i = 0; i < streams->clientsSize; i++) {
			if ((uv_handle_t *) streams->clients[i] == handle) {
				streams->clients[i] = NULL;
				streams->activeClients--;

				// Close connection and free its memory.
				uv_close(handle, &libuvCloseFree);

				break;
			}
		}
	}
}

static void writePacket(outputCommonState state, libuvWriteBuf packetBuffer) {
	// If no active clients exist, don't write anything.
	if (state->networkIO->activeClients == 0) {
		free(packetBuffer->freeBuf);
		free(packetBuffer);

		return;
	}

	// Write packets to network. TCP/Pipe have their header already written in the
	// Connection callbacks. Also, the size of the written data doesn't matter, as
	// they are stream transports, and the network stack will take care of things
	// like buffering and packet sizes.
	// Only UDP needs special treatment here to write the proper header and split
	// the packets up into manageable sizes (<=64K), together with keeping track
	// of the sequence number.
	if (state->networkIO->isUDP) {
		// UDP output.
		// If too much data waiting to be sent, just skip current packet.
		if (((uv_udp_t *) state->networkIO->clients[0])->send_queue_size > MAX_OUTPUT_QUEUED_SIZE) {
			goto freePacketBufferUDP;
		}

		size_t packetSize  = packetBuffer->buf.len;
		size_t packetIndex = 0;
		bool firstChunk    = true;

		// Split packets up into chunks for UDP. Send each chunk with its own
		// header and increasing sequence number. The very first packet of a chunk is
		// identifiable by having a negative sequence number (highest bit set to one).
		while (packetSize > 0) {
			libuvWriteMultiBuf buffers = libuvWriteBufAlloc(2); // One for network header, one for data.
			if (buffers == NULL) {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for network buffers.");

				goto freePacketBufferUDP;
			}

			buffers->statusCheck = &libuvWriteStatusCheck;

			// Write header into first buffer.
			if (!writeNetworkHeader(state->networkIO, &buffers->buffers[0], firstChunk)) {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to write network header.");

				libuvWriteBufFree(buffers);
				goto freePacketBufferUDP;
			}

			firstChunk = false;

			// Write data into second buffer.
			size_t sendSize = (packetSize > AEDAT3_MAX_UDP_SIZE) ? (AEDAT3_MAX_UDP_SIZE) : (packetSize);

			libuvWriteBufInit(&buffers->buffers[1], sendSize);
			if (buffers->buffers[1].buf.base == NULL) {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for data buffer.");

				libuvWriteBufFree(buffers);
				goto freePacketBufferUDP;
			}

			memcpy(buffers->buffers[1].buf.base, packetBuffer->buf.base + packetIndex, sendSize);

			// For UDP we only support client mode to ONE outside address.
			int retVal = libuvWriteUDP((uv_udp_t *) state->networkIO->clients[0], state->networkIO->address, buffers);
			UV_RET_CHECK(
				retVal, state->parentModule->moduleSubSystemString, "libuvWriteUDP", libuvWriteBufFree(buffers);
				goto freePacketBufferUDP);

			// Update loop indexes.
			packetSize -= sendSize;
			packetIndex += sendSize;
		}

	// Free all packet memory.
	freePacketBufferUDP : {
		free(packetBuffer->freeBuf);
		free(packetBuffer);
	}
	}
	else {
		// TCP/Pipe outputs.
		// Prepare buffers, increase reference count.
		libuvWriteMultiBuf buffers = libuvWriteBufAlloc(1);
		if (buffers == NULL) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for network buffers.");

			free(packetBuffer->freeBuf);
			free(packetBuffer);
			return;
		}

		buffers->statusCheck = &libuvWriteStatusCheck;

		buffers->refCount = state->networkIO->activeClients;

		buffers->buffers[0] = *packetBuffer;
		free(packetBuffer);

		// Write to each client, but use common reference-counted buffer.
		for (size_t i = 0; i < state->networkIO->clientsSize; i++) {
			uv_stream_t *client = state->networkIO->clients[i];

			if (client == NULL) {
				continue;
			}

			// If too much data waiting to be sent, just skip current packet.
			if (client->write_queue_size > MAX_OUTPUT_QUEUED_SIZE) {
				libuvWriteBufFree(buffers);
				return;
			}

			int retVal = libuvWrite(client, buffers);
			UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "libuvWrite", libuvWriteBufFree(buffers));
		}
	}
}

static void initializeNetworkHeader(outputCommonState state) {
	// Generate AEDAT 3.1 header for network streams (20 bytes total).
	state->networkIO->networkHeader.magicNumber    = htole64(AEDAT3_NETWORK_MAGIC_NUMBER);
	state->networkIO->networkHeader.sequenceNumber = htole64(0);
	state->networkIO->networkHeader.versionNumber  = AEDAT3_NETWORK_VERSION;
	state->networkIO->networkHeader.formatNumber   = state->formatID; // Send numeric format ID.
	state->networkIO->networkHeader.sourceID
		= htole16(I16T(atomic_load(&state->sourceID))); // Always one source per output module.
}

static bool writeNetworkHeader(outputCommonNetIO streams, libuvWriteBuf buf, bool startOfUDPPacket) {
	// Create memory chunk for network header to be sent via libuv.
	// libuv takes care of freeing memory. This is also needed for UDP
	// to have different sequence numbers in flight.
	libuvWriteBufInit(buf, AEDAT3_NETWORK_HEADER_LENGTH);
	if (buf->buf.base == NULL) {
		return (false);
	}

	if (streams->isUDP && startOfUDPPacket) {
		// Set highest bit of sequence number to one.
		streams->networkHeader.sequenceNumber
			= I64T(htole64(le64toh(U64T(streams->networkHeader.sequenceNumber)) | 0x8000000000000000LLU));
	}

	// Copy in current header.
	memcpy(buf->buf.base, &streams->networkHeader, AEDAT3_NETWORK_HEADER_LENGTH);

	if (streams->isUDP) {
		if (startOfUDPPacket) {
			// Unset highest bit of sequence number (back to zero).
			streams->networkHeader.sequenceNumber
				= I64T(htole64(le64toh(U64T(streams->networkHeader.sequenceNumber)) & 0x7FFFFFFFFFFFFFFFLLU));
		}

		// Increase sequence number for successive headers, if this is a
		// message-based network protocol (UDP for example).
		streams->networkHeader.sequenceNumber = I64T(htole64(le64toh(U64T(streams->networkHeader.sequenceNumber)) + 1));
	}

	return (true);
}

static void writeFileHeader(outputCommonState state) {
	// Write AEDAT 3.1 header.
	writeUntilDone(
		state->fileIO, (const uint8_t *) "#!AER-DAT" AEDAT3_FILE_VERSION "\r\n", 11 + strlen(AEDAT3_FILE_VERSION));

	// Write format header for all supported formats.
	writeUntilDone(state->fileIO, (const uint8_t *) "#Format: ", 9);

	if (state->formatID == 0x00) {
		writeUntilDone(state->fileIO, (const uint8_t *) "RAW", 3);
	}
	else {
		// Support the various formats and their mixing.
		if (state->formatID == 0x01) {
			writeUntilDone(state->fileIO, (const uint8_t *) "SerializedTS", 12);
		}

		if (state->formatID == 0x02) {
			writeUntilDone(state->fileIO, (const uint8_t *) "PNGFrames", 9);
		}

		if (state->formatID == 0x03) {
			// Serial and PNG together.
			writeUntilDone(state->fileIO, (const uint8_t *) "SerializedTS,PNGFrames", 12 + 1 + 9);
		}
	}

	writeUntilDone(state->fileIO, (const uint8_t *) "\r\n", 2);

	writeUntilDone(state->fileIO, (const uint8_t *) state->sourceInfoString, strlen(state->sourceInfoString));

	// First prepend the time.
	time_t currentTimeEpoch = time(NULL);

#if defined(OS_WINDOWS)
	// localtime() is thread-safe on Windows (and there is no localtime_r() at all).
	struct tm *currentTime = localtime(&currentTimeEpoch);

	// Windows doesn't support %z (numerical timezone), so no TZ info here.
	// Following time format uses exactly 34 characters (20 separators/characters,
	// 4 year, 2 month, 2 day, 2 hours, 2 minutes, 2 seconds).
	size_t currentTimeStringLength = 34;
	char currentTimeString[currentTimeStringLength + 1]; // + 1 for terminating NUL byte.
	strftime(currentTimeString, currentTimeStringLength + 1, "#Start-Time: %Y-%m-%d %H:%M:%S\r\n", currentTime);
#else
	// From localtime_r() man-page: "According to POSIX.1-2004, localtime()
	// is required to behave as though tzset(3) was called, while
	// localtime_r() does not have this requirement."
	// So we make sure to call it here, to be portable.
	tzset();

	struct tm currentTime;
	localtime_r(&currentTimeEpoch, &currentTime);

	// Following time format uses exactly 44 characters (25 separators/characters,
	// 4 year, 2 month, 2 day, 2 hours, 2 minutes, 2 seconds, 5 time-zone).
	size_t currentTimeStringLength = 44;
	char currentTimeString[currentTimeStringLength + 1]; // + 1 for terminating NUL byte.
	strftime(currentTimeString, currentTimeStringLength + 1, "#Start-Time: %Y-%m-%d %H:%M:%S (TZ%z)\r\n", &currentTime);
#endif

	writeUntilDone(state->fileIO, (const uint8_t *) currentTimeString, currentTimeStringLength);

	writeUntilDone(state->fileIO, (const uint8_t *) "#!END-HEADER\r\n", 14);
}

void caerOutputCommonOnServerConnection(uv_stream_t *server, int status) {
	outputCommonNetIO streams = server->data;

	UV_RET_CHECK(status, __func__, "Connection", return );

	uv_stream_t *client = NULL;

	if (streams->isTCP) {
		client = malloc(sizeof(uv_tcp_t));
	}
	else {
		client = malloc(sizeof(uv_pipe_t));
	}

	if (client == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Failed to allocate memory for new client.");
		return;
	}

	int retVal;

	if (streams->isTCP) {
		retVal = uv_tcp_init(server->loop, (uv_tcp_t *) client);
		UV_RET_CHECK(retVal, __func__, "uv_tcp_init", free(client); return );
	}
	else {
		retVal = uv_pipe_init(server->loop, (uv_pipe_t *) client, false);
		UV_RET_CHECK(retVal, __func__, "uv_pipe_init", free(client); return );
	}

	// All clients need to remember the main streams structure.
	client->data = streams;

	retVal = uv_accept(server, client);
	UV_RET_CHECK(retVal, __func__, "uv_accept", goto killConnection);

	// Find place for new connection. If all exhausted, we've reached maximum
	// number of clients and just kill the connection.
	for (size_t i = 0; i < streams->clientsSize; i++) {
		if (streams->clients[i] == NULL) {
			// TCP/PIPE: send out initial header. Only those two can call this function!
			libuvWriteMultiBuf buffers = libuvWriteBufAlloc(1);
			if (buffers == NULL) {
				caerLog(CAER_LOG_ERROR, __func__, "Failed to allocate memory for network header buffers.");

				goto killConnection;
			}

			buffers->statusCheck = &libuvWriteStatusCheck;

			if (!writeNetworkHeader(streams, &buffers->buffers[0], false)) {
				caerLog(CAER_LOG_ERROR, __func__, "Failed to write network header.");

				libuvWriteBufFree(buffers);
				goto killConnection;
			}

			retVal = libuvWrite(client, buffers);
			UV_RET_CHECK(retVal, __func__, "libuvWrite", libuvWriteBufFree(buffers); goto killConnection);

			// Ready now for more data, so set client field for writePacket().
			streams->clients[i] = client;
			streams->activeClients++;

			// TODO: add client IP to connected clients list.

			return;
		}
	}

// Kill connection if maximum number reached.
killConnection : { uv_close((uv_handle_t *) client, &libuvCloseFree); }
}

void caerOutputCommonOnClientConnection(uv_connect_t *connectionRequest, int status) {
	outputCommonNetIO streams = connectionRequest->handle->data;

	UV_RET_CHECK(status, __func__, "Connection", goto cleanupRequest);

	// TCP/PIPE: send out initial header. Only those two can call this function!
	libuvWriteMultiBuf buffers = libuvWriteBufAlloc(1);
	if (buffers == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Failed to allocate memory for network header buffers.");

		goto cleanupRequest;
	}

	buffers->statusCheck = &libuvWriteStatusCheck;

	if (!writeNetworkHeader(streams, &buffers->buffers[0], false)) {
		caerLog(CAER_LOG_ERROR, __func__, "Failed to write network header.");

		libuvWriteBufFree(buffers);
		goto cleanupRequest;
	}

	int retVal = libuvWrite(connectionRequest->handle, buffers);
	UV_RET_CHECK(retVal, __func__, "libuvWrite", libuvWriteBufFree(buffers); goto cleanupRequest);

	// Ready now for more data, so set client field for writePacket().
	streams->clients[0] = connectionRequest->handle;
	streams->activeClients++;

cleanupRequest : { free(connectionRequest); }
}

bool caerOutputCommonInit(caerModuleData moduleData, int fileDescriptor, outputCommonNetIO streams) {
	outputCommonState state = moduleData->moduleState;

	state->parentModule = moduleData;

	// Check for invalid input combinations.
	if ((fileDescriptor < 0 && streams == NULL) || (fileDescriptor != -1 && streams != NULL)) {
		return (false);
	}

	// Store network/file, message-based or not information.
	state->isNetworkStream = (streams != NULL);
	state->fileIO          = fileDescriptor;
	state->networkIO       = streams;

	// If in server mode, add SSHS attribute to track connected client IPs.
	if (state->isNetworkStream && state->networkIO->server != NULL) {
		sshsNodeCreateString(state->parentModule->moduleNode, "connectedClients", "", 0, INT32_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "IPs of clients currently connected to output server.");
	}

	// Initial source ID has to be -1 (invalid).
	atomic_store(&state->sourceID, -1);

	// Handle configuration.
	sshsNodeCreateBool(moduleData->moduleNode, "validOnly", false, SSHS_FLAGS_NORMAL, "Only send valid events.");
	sshsNodeCreateBool(moduleData->moduleNode, "keepPackets", false, SSHS_FLAGS_NORMAL,
		"Ensure all packets are kept (stall output if transfer-buffer full).");
	sshsNodeCreateInt(moduleData->moduleNode, "ringBufferSize", 512, 8, 4096, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer and EventPacket queues, used for transfers between mainloop and output threads.");

	atomic_store(&state->validOnly, sshsNodeGetBool(moduleData->moduleNode, "validOnly"));
	atomic_store(&state->keepPackets, sshsNodeGetBool(moduleData->moduleNode, "keepPackets"));
	int ringSize = sshsNodeGetInt(moduleData->moduleNode, "ringBufferSize");

	// Format configuration (compression modes).
	state->formatID = 0x00; // RAW format by default.

	// Initialize compressor ring-buffer. ringBufferSize only changes here at init time!
	state->compressorRing = caerRingBufferInit((size_t) ringSize);
	if (state->compressorRing == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate compressor ring-buffer.");
		return (false);
	}

	// Initialize output ring-buffer. ringBufferSize only changes here at init time!
	state->outputRing = caerRingBufferInit((size_t) ringSize);
	if (state->outputRing == NULL) {
		caerRingBufferFree(state->compressorRing);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate output ring-buffer.");
		return (false);
	}

	// If network output, initialize common libuv components.
	if (state->isNetworkStream) {
		// Add support for asynchronous shutdown (from caerOutputCommonExit()).
		state->networkIO->shutdown.data = state;
		int retVal = uv_async_init(&state->networkIO->loop, &state->networkIO->shutdown, &libuvAsyncShutdown);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_async_init",
					 caerRingBufferFree(state->compressorRing);
					 caerRingBufferFree(state->outputRing); return (false));

		// Use idle handles to check for new data on every loop run.
		state->networkIO->ringBufferGet.data = state;
		retVal                               = uv_idle_init(&state->networkIO->loop, &state->networkIO->ringBufferGet);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_idle_init",
					 uv_close((uv_handle_t *) &state->networkIO->shutdown, NULL);
					 caerRingBufferFree(state->compressorRing); caerRingBufferFree(state->outputRing); return (false));

		retVal = uv_idle_start(&state->networkIO->ringBufferGet, &libuvRingBufferGet);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_idle_start",
					 uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, NULL);
					 uv_close((uv_handle_t *) &state->networkIO->shutdown, NULL);
					 caerRingBufferFree(state->compressorRing); caerRingBufferFree(state->outputRing); return (false));
	}

	// Start output handling thread.
	atomic_store(&state->running, true);

	if (thrd_create(&state->compressorThread, &compressorThread, state) != thrd_success) {
		if (state->isNetworkStream) {
			uv_idle_stop(&state->networkIO->ringBufferGet);
			uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, NULL);
			uv_close((uv_handle_t *) &state->networkIO->shutdown, NULL);
		}
		caerRingBufferFree(state->compressorRing);
		caerRingBufferFree(state->outputRing);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start compressor thread.");
		return (false);
	}

	if (thrd_create(&state->outputThread, &outputThread, state) != thrd_success) {
		// Stop compressor thread (started just above) and wait on it.
		atomic_store(&state->running, false);

		if ((errno = thrd_join(state->compressorThread, NULL)) != thrd_success) {
			// This should never happen!
			caerModuleLog(
				state->parentModule, CAER_LOG_CRITICAL, "Failed to join compressor thread. Error: %d.", errno);
		}

		if (state->isNetworkStream) {
			uv_idle_stop(&state->networkIO->ringBufferGet);
			uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, NULL);
			uv_close((uv_handle_t *) &state->networkIO->shutdown, NULL);
		}
		caerRingBufferFree(state->compressorRing);
		caerRingBufferFree(state->outputRing);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start output thread.");
		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerOutputCommonConfigListener);

	return (true);
}

void caerOutputCommonExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerOutputCommonConfigListener);

	outputCommonState state = moduleData->moduleState;

	// Stop output thread and wait on it.
	atomic_store(&state->running, false);
	if (state->isNetworkStream) {
		uv_async_send(&state->networkIO->shutdown);
	}

	if ((errno = thrd_join(state->compressorThread, NULL)) != thrd_success) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to join compressor thread. Error: %d.", errno);
	}

	if ((errno = thrd_join(state->outputThread, NULL)) != thrd_success) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to join output thread. Error: %d.", errno);
	}

	// Now clean up the ring-buffers: they should be empty, so sanity check!
	caerEventPacketContainer packetContainer;

	while ((packetContainer = caerRingBufferGet(state->compressorRing)) != NULL) {
		caerEventPacketContainerFree(packetContainer);

		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Compressor ring-buffer was not empty!");
	}

	caerRingBufferFree(state->compressorRing);

	libuvWriteBuf packetBuffer;

	while ((packetBuffer = caerRingBufferGet(state->outputRing)) != NULL) {
		free(packetBuffer);

		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Output ring-buffer was not empty!");
	}

	caerRingBufferFree(state->outputRing);

	// Cleanup IO resources.
	if (state->isNetworkStream) {
		if (state->networkIO->server != NULL) {
			// Server shut down, no more clients.
			sshsNodeRemoveAttribute(state->parentModule->moduleNode, "connectedClients", SSHS_STRING);
		}

		// Cleanup all remaining handles and run until all callbacks are done.
		int retVal = libuvCloseLoopHandles(&state->networkIO->loop);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "libuvCloseLoopHandles", );

		retVal = uv_loop_close(&state->networkIO->loop);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_loop_close", );

		// Free allocated memory. libuv already frees all client/server related memory.
		free(state->networkIO->address);
		free(state->networkIO);
	}
	else {
		// Ensure all data written to disk.
		portable_fsync(state->fileIO);

		// Close file descriptor.
		close(state->fileIO);
	}

	free(state->sourceInfoString);

	// Print final statistics results.
	caerModuleLog(state->parentModule, CAER_LOG_INFO,
		"Statistics: wrote %" PRIu64 " packets, for a total uncompressed size of %" PRIu64 " bytes (%" PRIu64
		" bytes header + %" PRIu64 " bytes data). "
		"Actually written to output were %" PRIu64 " bytes (after compression), resulting in a saving of %" PRIu64
		" bytes.",
		state->statistics.packetsNumber, state->statistics.packetsTotalSize, state->statistics.packetsHeaderSize,
		state->statistics.packetsDataSize, state->statistics.dataWritten,
		(state->statistics.packetsTotalSize - state->statistics.dataWritten));
}

static void caerOutputCommonConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;
	outputCommonState state   = moduleData->moduleState;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "validOnly")) {
			// Set valid only flag to given value.
			atomic_store(&state->validOnly, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "keepPackets")) {
			// Set keep packets flag to given value.
			atomic_store(&state->keepPackets, changeValue.boolean);
		}
	}
}
