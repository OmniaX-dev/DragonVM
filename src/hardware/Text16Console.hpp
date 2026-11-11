#pragma once

#include <ostd/data/Types.hpp>
#include <bit>
#include <cstring>
#include <type_traits>

namespace dragon
{
	namespace hw
	{
		class MemoryMapper;

		// The video card's "hardware accelerated" dr::Text16Console methods: the program sends a
		// console signal with the console's address (and the method's arguments) in the VGA
		// registers, and the card does the work directly on the console in guest memory: its
		// fields and its screen buffer. Nothing is stored in the card between two signals, so
		// any number of consoles can use it.
		//
		// Shared by everything that handles video signals (VirtualDisplay, drun).
		namespace text16console
		{
			// A big-endian value in guest memory (the Dragon is big-endian), readable and
			// writable as a plain T.
			template<typename T> struct BE
			{
				u8 b[sizeof(T)];
				inline operator T() const { T v; std::memcpy(&v, b, sizeof(T)); return std::byteswap(v); }
				inline BE& operator=(T v) { v = std::byteswap(v); std::memcpy(b, &v, sizeof(T)); return *this; }
			};

			// dr::Text16Console's fields, byte for byte (print.ds): keep the two in sync.
			#pragma pack(push, 1)
			struct tLayout
			{
				enum class Errors : u8 {
					NoError					= 0x00,
					ScreenBufferAllocFail	= 0x01,
					Uninitialized			= 0x02,
					WrongVideoMode			= 0x03,
					VerticalOverflow		= 0x04,
					HorizontalOverflow		= 0x05,
					InvalidOverflowPolicy	= 0x06,
					InvalidState			= 0x07
				};
				enum class VOverflowPolicy : u8 {
					Ignore					= 0x00,
					Scroll					= 0x01,
					Error					= 0x02
				};
				enum class HOverflowPolicy : u8 {
					Ignore					= 0x00,
					Wrap					= 0x01,
					Error					= 0x02
				};

				BE<u16> m_cursor_x;
				BE<u16> m_cursor_y;
				BE<u16> m_stored_cursor_x;
				BE<u16> m_stored_cursor_y;
				u8 m_bg_col;
				u8 m_fg_col;
				u8 m_stored_bg_col;
				u8 m_stored_fg_col;
				BE<u32> m_screen_x;
				BE<u32> m_screen_y;
				BE<u32> m_screenBufferSize;
				BE<u32> m_screenBufferPtr;
				BE<u32> m_yOffset;
				BE<u32> m_scrollOffset;
				bool m_initialized;
				Errors m_error;
				HOverflowPolicy m_overflowPolicyH;
				VOverflowPolicy m_overflowPolicyV;
			};
			#pragma pack(pop)
			static_assert(std::is_trivially_copyable_v<tLayout>);
			static_assert(sizeof(tLayout) == 40);
			static_assert(alignof(tLayout) == 1);

			// Text16Console::print(text): the console at consoleAddr prints the zero-terminated
			// string at textAddr, with the same result as the DScript version (cursor, colors,
			// overflow policies, scrolling, errors). Does nothing if the console isn't usable
			// (not initialized, already in error); sets InvalidState if its screen buffer isn't
			// valid memory. Returns false if the console itself isn't valid memory.
			bool print(MemoryMapper& mem, u32 consoleAddr, u32 textAddr);
		}
	}
}
