#pragma once

#include "DiskManager.hpp"

namespace dragon
{
	// The CMOS registers (data::CMOSRegisters::List) with their values.
	class CMOSRegisterView : public DebugListView
	{
		public: using RowCallback = std::function<void(i32 row, bool doubleClick)>;
		public:
			CMOSRegisterView(ogfx::gui::Window& window);
			// file: its bytes not saved yet are marked (nullptr: none).
			inline void setData(std::span<const u8> data, const DiskDocument* file) { m_data = data; m_file = file; }
			inline void setSelectCallback(RowCallback callback) { m_onSelect = std::move(callback); }
			// The register's value (big endian).
			u32 valueOf(i32 row) const;

			i32 getRowCount(void) const override;
			void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) override;
			void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) override;

		private:
			std::span<const u8> m_data;
			const DiskDocument* m_file { nullptr };
			RowCallback m_onSelect { nullptr };
	};

	// ============================================================================
	// CMOSManagerWindow — the CMOS chip's 128 bytes: a hex editor and the
	// registers in a readable form (the live data ones are written by the
	// runtime at every start).
	//
	// Two modes ("Live chip"):
	//   file  the cmos.dr file of the machine config: edited as a copy, Save
	//         writes the changed bytes (the machine reads the file when it starts)
	//   live  the chip of the running machine: every write goes through the memory
	//         map, with the CPU put in BIOS mode for it (the chip is write protected
	//         outside of it) and back; each one is an edit in the history
	// ============================================================================
	class CMOSManagerWindow : public ogfx::gui::SubWindow
	{
		public:
			CMOSManagerWindow(ogfx::gui::Window& window, Debugger& debugger);
			void build(void);
			// Once the machine is loaded: reads its cmos.dr file.
			void start(void);
			// While open: the live chip's content.
			void syncWithMachine(void);

		private:
			void set_live(bool live);
			void load_file(void);
			void show_data(bool keepView);
			void update_info(void);
			void select_register(i32 row);
			void set_value(void);
			// Writes the bytes at the CMOS offset (live: through the memory map; file: in the copy).
			bool write_bytes(u16 offset, std::span<const u8> bytes);
			bool write_live(u16 offset, std::span<const u8> bytes);
			void save(void);
			void reload(void);
			void set_status(const String& text, const Color& color);

		private:
			Debugger& m_debugger;
			bool m_live { false };
			bool m_built { false };
			std::unique_ptr<DiskDocument> m_file;
			stdvec<u8> m_liveData;
			i32 m_selected { -1 };

			ogfx::gui::CheckBox m_liveCheck { getWindow() };
			MessageLine m_infoLine { getWindow() };
			ogfx::gui::Button m_saveButton { getWindow() };
			ogfx::gui::Button m_reloadButton { getWindow() };
			MessageLine m_statusLine { getWindow() };
			ogfx::gui::HexEditor m_hex { getWindow() };
			CMOSRegisterView m_registers { getWindow() };
			ogfx::gui::Label m_valueTitle { getWindow() };
			FieldInput m_valueInput { getWindow() };
			ogfx::gui::Button m_setButton { getWindow() };
			MessageLine m_hintLine { getWindow() };
	};
}
