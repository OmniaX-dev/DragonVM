#include "VirtualMMU.hpp"

namespace dragon
{
	namespace hw
	{
		VirtualMMU& VirtualMMU::init(void)
		{
			m_data.init(VirtualMMU::MMUSize);
			m_data.enableAutoResize(false);
			return *this;
		}
	}
}