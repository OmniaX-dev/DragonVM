#pragma once 

#include <cstdint>
#include <ostd/Geometry.hpp>
#include <ostd/BaseObject.hpp>

namespace dragon
{
	class Window;
	class Widget : public ostd::Rectangle, public ostd::BaseObject
	{
		public:
			void __init(Window& parent) { m_parent = &parent; init(); }
			virtual void draw(void) {  }
			virtual void update(void) {  }
			virtual void fixedUpdate(void) {  }
			virtual void slowUpdate(void) {  }

		protected:
			virtual void init(void) {  }

		protected:
			Window* m_parent { nullptr };
	};
}