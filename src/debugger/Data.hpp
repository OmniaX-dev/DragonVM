#pragma once

#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace debugger
	{
		struct Run
		{
			AddressType offset;
			stdvec<i8> old_bytes;
			stdvec<i8> new_bytes;
		};
		struct Keyframe
		{
			u64 cycle;
			stdvec<i8> full_state; // entire memory image at this cycle
		};
		struct CycleDiff
		{
			u64 cycle;
			stdvec<Run> runs; // empty if nothing changed
		};
		struct History
		{
			u32 keyframe_interval { 1000 };
			stdvec<Keyframe>  keyframes; // one every `interval` cycles
			stdvec<CycleDiff> diffs; // one per cycle (including keyframe cycles, optional)
		};
	}
}
