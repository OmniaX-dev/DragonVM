#include "History.hpp"
#include "../hardware/VirtualHardDrive.hpp"
#include <cstring>

namespace dragon
{
	namespace debugger
	{
		// Equal bytes between two changes that still make them one run (a run header costs ~2-4 bytes).
		static constexpr u32 RunMergeGap = 6;

		static void put_varint(stdvec<u8>& out, u32 value)
		{
			while (value >= 0x80)
			{
				out.push_back(cast<u8>(value | 0x80));
				value >>= 7;
			}
			out.push_back(cast<u8>(value));
		}

		static u32 get_varint(const u8*& data)
		{
			u32 value = 0;
			u32 shift = 0;
			while (true)
			{
				const u8 byte = *data++;
				value |= cast<u32>(byte & 0x7F) << shift;
				if ((byte & 0x80) == 0)
					break;
				shift += 7;
			}
			return value;
		}

		void History::addSegment(const String& name, u8* data, u32 size, std::function<void(void)> capture, std::function<void(void)> apply)
		{
			Segment segment;
			segment.name = name;
			segment.data = data;
			segment.size = size;
			segment.offset = cast<u32>(m_shadow.size());
			segment.capture = std::move(capture);
			segment.apply = std::move(apply);
			m_segments.push_back(std::move(segment));
			m_shadow.resize(m_shadow.size() + size, 0);
		}

		void History::begin(u64 cycle)
		{
			capture_all();
			for (const auto& segment : m_segments)
				std::memcpy(m_shadow.data() + segment.offset, segment.data, segment.size);
			m_entries.clear();
			m_keyframes.clear();
			m_pendingFileWrites.clear();
			m_firstPosition = 0;
			m_firstCycle = cycle;
			m_cursor = 0;
			// The first state is a keyframe: going all the way back is one copy.
			m_keyframes.push_back({ 0, m_shadow });
			m_memoryUsage = m_shadow.size() * 2;
		}

		void History::record(u64 cycle, eEntryKind kind)
		{
			if (!isAtEnd())
				truncate_future();
			capture_all();
			m_scratch.clear();
			u32 lastEnd = 0;
			for (const auto& segment : m_segments)
				diff_segment(segment, m_scratch, lastEnd);

			CycleDiff entry;
			entry.cycle = cycle;
			entry.kind = kind;
			entry.runs.assign(m_scratch.begin(), m_scratch.end());
			if (!m_pendingFileWrites.empty())
			{
				entry.fileWrites = std::make_unique<stdvec<FileWrite>>(std::move(m_pendingFileWrites));
				m_pendingFileWrites.clear();
			}
			m_memoryUsage += entry_size(entry);
			m_entries.push_back(std::move(entry));
			m_cursor++;
			if (m_cursor % KeyframeInterval == 0)
			{
				m_keyframes.push_back({ m_cursor, m_shadow });
				m_memoryUsage += m_shadow.size() + sizeof(Keyframe);
			}
			trim();
		}

		bool History::recordIfChanged(u64 cycle, eEntryKind kind)
		{
			capture_all();
			m_scratch.clear();
			u32 lastEnd = 0;
			// diff_segment() updates the shadow, so the runs are kept for the entry below.
			for (const auto& segment : m_segments)
				diff_segment(segment, m_scratch, lastEnd);
			if (m_scratch.empty() && m_pendingFileWrites.empty())
				return false;
			if (!isAtEnd())
				truncate_future();
			CycleDiff entry;
			entry.cycle = cycle;
			entry.kind = kind;
			entry.runs.assign(m_scratch.begin(), m_scratch.end());
			if (!m_pendingFileWrites.empty())
			{
				entry.fileWrites = std::make_unique<stdvec<FileWrite>>(std::move(m_pendingFileWrites));
				m_pendingFileWrites.clear();
			}
			m_memoryUsage += entry_size(entry);
			m_entries.push_back(std::move(entry));
			m_cursor++;
			trim();
			return true;
		}

		void History::recordFileWrite(hw::VirtualHardDrive& drive, u32 address, u8 oldValue, u8 newValue)
		{
			if (m_applying)
				return;
			m_pendingFileWrites.push_back({ &drive, address, oldValue, newValue });
		}

		bool History::stepBack(void)
		{
			if (!canStepBack())
				return false;
			apply_entry(m_entries[m_cursor - 1 - m_firstPosition], true);
			m_cursor--;
			return true;
		}

		bool History::stepForward(void)
		{
			if (!canStepForward())
				return false;
			apply_entry(m_entries[m_cursor - m_firstPosition], false);
			m_cursor++;
			return true;
		}

		void History::seek(u64 position)
		{
			position = std::clamp(position, m_firstPosition, getLastPosition());
			if (position == m_cursor)
				return;
			const u64 distance = (position > m_cursor ? position - m_cursor : m_cursor - position);
			// A keyframe closer to the target than the cursor saves applying entries one by one.
			const Keyframe* best = nullptr;
			u64 bestDistance = distance;
			for (const auto& keyframe : m_keyframes)
			{
				if (keyframe.position < m_firstPosition || keyframe.position > getLastPosition())
					continue;
				const u64 d = (keyframe.position > position ? keyframe.position - position : position - keyframe.position);
				if (d + 1 < bestDistance)
				{
					best = &keyframe;
					bestDistance = d;
				}
			}
			if (best != nullptr)
			{
				// The disk images aren't in the keyframes: their writes are replayed (or undone) on the way.
				m_applying = true;
				if (best->position < m_cursor)
				{
					for (u64 p = m_cursor; p > best->position; p--)
						apply_file_writes(m_entries[p - 1 - m_firstPosition], true);
				}
				else
				{
					for (u64 p = m_cursor + 1; p <= best->position; p++)
						apply_file_writes(m_entries[p - 1 - m_firstPosition], false);
				}
				m_applying = false;
				load_keyframe(*best);
				m_cursor = best->position;
			}
			while (m_cursor < position)
				stepForward();
			while (m_cursor > position)
				stepBack();
		}

		u64 History::getCycleAt(u64 position) const
		{
			if (position <= m_firstPosition || m_entries.empty())
				return m_firstCycle;
			position = std::min(position, getLastPosition());
			return m_entries[position - 1 - m_firstPosition].cycle;
		}

		const History::CycleDiff* History::getEntryTo(u64 position) const
		{
			if (position <= m_firstPosition || position > getLastPosition())
				return nullptr;
			return &m_entries[position - 1 - m_firstPosition];
		}

		bool History::entryChanges(u64 position, u32 stateOffset, u32 size) const
		{
			const CycleDiff* entry = getEntryTo(position);
			if (entry == nullptr)
				return false;
			const u8* p = entry->runs.data();
			const u8* end = p + entry->runs.size();
			u32 lastEnd = 0;
			while (p < end)
			{
				const u32 offset = lastEnd + get_varint(p);
				const u32 length = get_varint(p);
				p += cast<size_t>(length) * 2;
				if (offset < stateOffset + size && stateOffset < offset + length)
					return true;
				lastEnd = offset + length;
			}
			return false;
		}

		i64 History::getStateOffset(const String& segmentName, u32 offsetInSegment) const
		{
			for (const auto& segment : m_segments)
			{
				if (segment.name == segmentName && offsetInSegment < segment.size)
					return cast<i64>(segment.offset) + offsetInSegment;
			}
			return -1;
		}

		void History::capture_all(void)
		{
			for (const auto& segment : m_segments)
			{
				if (segment.capture)
					segment.capture();
			}
		}

		void History::diff_segment(const Segment& segment, stdvec<u8>& outRuns, u32& lastEnd)
		{
			const u8* live = segment.data;
			u8* shadow = m_shadow.data() + segment.offset;
			const u32 size = segment.size;
			u32 i = 0;
			while (i < size)
			{
				// Most of the state doesn't change: skip it a block at a time.
				if (size - i >= 64 && std::memcmp(live + i, shadow + i, 64) == 0)
				{
					i += 64;
					continue;
				}
				if (live[i] == shadow[i])
				{
					i++;
					continue;
				}
				const u32 start = i;
				u32 end = i + 1;
				for (u32 j = i + 1; j < size && j - end < RunMergeGap; j++)
				{
					if (live[j] != shadow[j])
						end = j + 1;
				}
				const u32 length = end - start;
				const u32 offset = segment.offset + start;
				put_varint(outRuns, offset - lastEnd);
				put_varint(outRuns, length);
				outRuns.insert(outRuns.end(), shadow + start, shadow + end);
				outRuns.insert(outRuns.end(), live + start, live + end);
				std::memcpy(shadow + start, live + start, length);
				lastEnd = offset + length;
				i = end;
			}
		}

		void History::apply_entry(const CycleDiff& entry, bool undo)
		{
			m_applying = true;
			stdvec<bool> dirty(m_segments.size(), false);
			const u8* p = entry.runs.data();
			const u8* end = p + entry.runs.size();
			u32 lastEnd = 0;
			while (p < end)
			{
				const u32 offset = lastEnd + get_varint(p);
				const u32 length = get_varint(p);
				const u8* bytes = (undo ? p : p + length);
				p += cast<size_t>(length) * 2;
				lastEnd = offset + length;
				std::memcpy(m_shadow.data() + offset, bytes, length);
				const i32 index = segment_at(offset);
				if (index < 0)
					continue;
				const Segment& segment = m_segments[index];
				std::memcpy(segment.data + (offset - segment.offset), bytes, length);
				dirty[index] = true;
			}
			apply_segments(dirty);
			apply_file_writes(entry, undo);
			m_applying = false;
		}

		void History::apply_file_writes(const CycleDiff& entry, bool undo)
		{
			if (!entry.fileWrites)
				return;
			const auto& writes = *entry.fileWrites;
			if (undo)
			{
				for (auto it = writes.rbegin(); it != writes.rend(); ++it)
					it->drive->restoreByte(it->address, it->oldValue);
			}
			else
			{
				for (const auto& write : writes)
					write.drive->restoreByte(write.address, write.newValue);
			}
		}

		void History::apply_segments(const stdvec<bool>& dirty)
		{
			for (size_t i = 0; i < m_segments.size(); i++)
			{
				const Segment& segment = m_segments[i];
				if (!dirty[i] || !segment.apply)
					continue;
				// The staging copy is made whole from the shadow first: the live state may have
				// moved on since the last capture.
				if (segment.capture)
					std::memcpy(segment.data, m_shadow.data() + segment.offset, segment.size);
				segment.apply();
			}
		}

		void History::load_keyframe(const Keyframe& keyframe)
		{
			m_applying = true;
			m_shadow = keyframe.state;
			for (const auto& segment : m_segments)
				std::memcpy(segment.data, m_shadow.data() + segment.offset, segment.size);
			apply_segments(stdvec<bool>(m_segments.size(), true));
			m_applying = false;
		}

		void History::truncate_future(void)
		{
			while (getLastPosition() > m_cursor)
			{
				m_memoryUsage -= std::min(m_memoryUsage, entry_size(m_entries.back()));
				m_entries.pop_back();
			}
			while (!m_keyframes.empty() && m_keyframes.back().position > m_cursor)
			{
				m_memoryUsage -= std::min<u64>(m_memoryUsage, m_keyframes.back().state.size() + sizeof(Keyframe));
				m_keyframes.pop_back();
			}
		}

		void History::trim(void)
		{
			while (m_memoryUsage > m_memoryLimit && m_firstPosition < m_cursor && !m_entries.empty())
			{
				m_firstCycle = m_entries.front().cycle;
				m_memoryUsage -= std::min(m_memoryUsage, entry_size(m_entries.front()));
				m_entries.pop_front();
				m_firstPosition++;
				while (!m_keyframes.empty() && m_keyframes.front().position < m_firstPosition)
				{
					m_memoryUsage -= std::min<u64>(m_memoryUsage, m_keyframes.front().state.size() + sizeof(Keyframe));
					m_keyframes.pop_front();
				}
			}
		}

		u64 History::entry_size(const CycleDiff& entry) const
		{
			u64 size = sizeof(CycleDiff) + entry.runs.capacity() + 16;
			if (entry.fileWrites)
				size += sizeof(stdvec<FileWrite>) + entry.fileWrites->capacity() * sizeof(FileWrite);
			return size;
		}

		i32 History::segment_at(u32 offset) const
		{
			i32 lo = 0, hi = cast<i32>(m_segments.size()) - 1;
			while (lo <= hi)
			{
				const i32 mid = (lo + hi) / 2;
				const Segment& segment = m_segments[mid];
				if (offset < segment.offset)
					hi = mid - 1;
				else if (offset >= segment.offset + segment.size)
					lo = mid + 1;
				else
					return mid;
			}
			return -1;
		}
	}
}
