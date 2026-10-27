#pragma once

#include "../tools/GlobalData.hpp"
#include <deque>
#include <functional>
#include <memory>

namespace dragon
{
	namespace hw { class VirtualHardDrive; }
	namespace debugger
	{
		// ============================================================================
		// History — the machine's past, one entry per executed cycle (and one per
		// change made from the outside, e.g. by the user), so the debugger can step
		// backwards and forwards through it.
		//
		// The machine state is a list of segments: plain byte ranges, either the
		// devices' own memory (RAM, VRAM, ...) or a copy made by a capture function
		// (CPU registers, display text, ...). record() compares the segments with a
		// shadow copy of the state and stores only the bytes that changed, with their
		// old and new values (runs): an entry can be undone and redone without knowing
		// who changed what, so nothing has to hook the devices' reads and writes (and
		// changes made by interrupts, devices or the user are caught like the CPU's).
		// Bytes written to the disk images (files, not part of the state) are
		// journaled with the entry instead (FileWrite).
		//
		// Positions: position p is the state after p entries. The cursor is the
		// current position; record() at a position before the last one drops the
		// entries after it (the future is rewritten). Keyframes (a full copy of the
		// state every KeyframeInterval entries) make long jumps fast; one step back or
		// forth just applies one entry. The oldest entries are dropped when the memory
		// limit is reached.
		// ============================================================================
		class History
		{
			public: struct Segment
			{
				String name { "" };
				u8* data { nullptr };
				u32 size { 0 };
				u32 offset { 0 };							// in the flat state
				std::function<void(void)> capture { nullptr };	// live state -> data (before diffing)
				std::function<void(void)> apply { nullptr };	// data -> live state (after restoring)
			};
			public: struct FileWrite
			{
				hw::VirtualHardDrive* drive { nullptr };
				u32 address { 0 };
				u8 oldValue { 0 };
				u8 newValue { 0 };
			};
			public: enum class eEntryKind : u8 { Cycle = 0, Edit, External };
			// One entry. The runs are encoded in a byte vector (they are many and small):
			// [offset delta (varint), length (varint), old bytes, new bytes]...
			public: struct CycleDiff
			{
				u64 cycle { 0 };			// cycles executed in the state after this entry
				eEntryKind kind { eEntryKind::Cycle };
				stdvec<u8> runs;
				std::unique_ptr<stdvec<FileWrite>> fileWrites;
			};
			public: struct Keyframe
			{
				u64 position { 0 };
				stdvec<u8> state;
			};

			public:
				inline static constexpr u64 KeyframeInterval { 8192 };
				inline static constexpr u64 DefaultMemoryLimit { 512ull * 1024 * 1024 };

			public:
				// Segments must be added before begin().
				void addSegment(const String& name, u8* data, u32 size, std::function<void(void)> capture = nullptr, std::function<void(void)> apply = nullptr);
				inline const stdvec<Segment>& getSegments(void) const { return m_segments; }
				inline u32 getStateSize(void) const { return cast<u32>(m_shadow.size()); }

				// Forgets everything and starts from the current state (position 0).
				void begin(u64 cycle);
				// Records the changes since the last record (as one entry, even if nothing changed).
				void record(u64 cycle, eEntryKind kind = eEntryKind::Cycle);
				// Records an entry only if something changed (edits, changes made while paused).
				bool recordIfChanged(u64 cycle, eEntryKind kind);
				// A disk byte written by the machine: it goes with the next recorded entry.
				void recordFileWrite(hw::VirtualHardDrive& drive, u32 address, u8 oldValue, u8 newValue);

				bool stepBack(void);
				bool stepForward(void);
				// Moves to any recorded position (clamped).
				void seek(u64 position);

				inline u64 getPosition(void) const { return m_cursor; }
				inline u64 getFirstPosition(void) const { return m_firstPosition; }
				inline u64 getLastPosition(void) const { return m_firstPosition + m_entries.size(); }
				inline bool canStepBack(void) const { return m_cursor > m_firstPosition; }
				inline bool canStepForward(void) const { return m_cursor < getLastPosition(); }
				inline bool isAtEnd(void) const { return m_cursor == getLastPosition(); }
				inline u64 getEntryCount(void) const { return m_entries.size(); }
				// Cycles executed in the state at `position`.
				u64 getCycleAt(u64 position) const;
				// The entry that leads to `position` (position - 1 -> position); nullptr for the first one.
				const CycleDiff* getEntryTo(u64 position) const;
				// Does the entry leading to `position` change the byte at the flat state offset?
				bool entryChanges(u64 position, u32 stateOffset, u32 size = 1) const;
				// Flat state offset of a byte of a segment; -1 if not found.
				i64 getStateOffset(const String& segmentName, u32 offsetInSegment) const;

				inline bool isApplying(void) const { return m_applying; }
				inline void setMemoryLimit(u64 bytes) { m_memoryLimit = std::max<u64>(bytes, 16ull * 1024 * 1024); trim(); }
				inline u64 getMemoryLimit(void) const { return m_memoryLimit; }
				inline u64 getMemoryUsage(void) const { return m_memoryUsage; }

			private:
				void capture_all(void);
				void diff_segment(const Segment& segment, stdvec<u8>& outRuns, u32& lastEnd);
				// Writes the old (undo) or new (redo) bytes of an entry into the state.
				void apply_entry(const CycleDiff& entry, bool undo);
				void apply_file_writes(const CycleDiff& entry, bool undo);
				void apply_segments(const stdvec<bool>& dirty);
				void load_keyframe(const Keyframe& keyframe);
				void truncate_future(void);
				void trim(void);
				u64 entry_size(const CycleDiff& entry) const;
				i32 segment_at(u32 offset) const;

			private:
				stdvec<Segment> m_segments;
				stdvec<u8> m_shadow;			// the state at the cursor
				std::deque<CycleDiff> m_entries;
				std::deque<Keyframe> m_keyframes;
				stdvec<FileWrite> m_pendingFileWrites;
				stdvec<u8> m_scratch;
				u64 m_firstPosition { 0 };
				u64 m_firstCycle { 0 };
				u64 m_cursor { 0 };
				u64 m_memoryUsage { 0 };
				u64 m_memoryLimit { DefaultMemoryLimit };
				bool m_applying { false };
		};
	}
}
