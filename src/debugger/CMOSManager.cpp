#include "CMOSManager.hpp"
#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	using namespace ogfx::gui;
	using CMOS = data::CMOSRegisters;

	static void place(Widget& widget, f32 anchorLeft, f32 anchorTop, f32 anchorRight, f32 anchorBottom, f32 left, f32 top, f32 right, f32 bottom)
	{
		widget.setAnchors(anchorLeft, anchorTop, anchorRight, anchorBottom);
		widget.setOffsets(left, top, right, bottom);
	}

	static const Color ErrorColor { 240, 100, 100 };
	static const Color SuccessColor { 120, 215, 120 };
	static const Color WarningColor { 235, 200, 90 };
	static const Color DimColor { 130, 140, 130 };
	static const Color LiveColor { 110, 200, 230 };

	inline static constexpr u16 CMOSSize { data::MemoryMapAddresses::CMOS_End - data::MemoryMapAddresses::CMOS_Start + 1 };




	//CMOSRegisterView
	CMOSRegisterView::CMOSRegisterView(Window& window) : DebugListView(window)
	{
		setTypeName("dragon::CMOSRegisterView");
		setColumns({ { "Address", 9 }, { "Register", 18 }, { "Size", 5 }, { "Value", 24 }, { "Kind", 9 }, { "Description", 30 } });
	}

	i32 CMOSRegisterView::getRowCount(void) const
	{
		return cast<i32>(std::size(CMOS::List));
	}

	u32 CMOSRegisterView::valueOf(i32 row) const
	{
		const auto& reg = CMOS::List[row];
		u32 value = 0;
		for (u8 i = 0; i < reg.size; i++)
			value = (value << 8) | (cast<size_t>(reg.address + i) < m_data.size() ? m_data[reg.address + i] : 0);
		return value;
	}

	void CMOSRegisterView::drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds)
	{
		const auto& reg = CMOS::List[row];
		bool changed = false;
		for (u8 i = 0; i < reg.size && m_file != nullptr; i++)
			changed = changed || m_file->isChanged(reg.address + i);
		switch (column)
		{
			case 0: text(gfx, String::getHexStr(data::MemoryMapAddresses::CMOS_Start + reg.address, true, 2), bounds.x, bounds.y, Color(235, 215, 90)); break;
			case 1: text(gfx, reg.name, bounds.x, bounds.y, TextColor); break;
			case 2: text(gfx, String("").add(reg.size), bounds.x, bounds.y, DimTextColor); break;
			case 3:
			{
				const u32 value = valueOf(row);
				String str = String::getHexStr(value, true, reg.size).add("  ").add(value);
				if (reg.address == CMOS::DiskList)
				{
					String disks = "";
					for (u8 bit = 0; bit < 16; bit++)
					{
						if ((value >> bit) & 1)
							disks.add(disks == "" ? "" : ",").add(bit);
					}
					str.add("  (").add(disks == "" ? String("none") : String("disk ").add(disks)).add(")");
				}
				text(gfx, str, bounds.x, bounds.y, changed ? ChangedColor : TextColor);
				break;
			}
			case 4: text(gfx, reg.liveData ? "live" : "setting", bounds.x, bounds.y, reg.liveData ? LiveColor : WarningColor); break;
			case 5: text(gfx, reg.description, bounds.x, bounds.y, DimTextColor); break;
			default: break;
		}
	}

	void CMOSRegisterView::onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick)
	{
		if (row < getRowCount() && m_onSelect)
			m_onSelect(row, doubleClick);
	}




	//CMOSManagerWindow
	CMOSManagerWindow::CMOSManagerWindow(Window& window, Debugger& debugger) : SubWindow(window, "CMOS Manager"), m_debugger(debugger)
	{
		setBounds({ 240, 150, 900, 620 });
		setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
		m_liveData.assign(CMOSSize, 0);
	}

	void CMOSManagerWindow::build(void)
	{
		addWidget(m_liveCheck);
		place(m_liveCheck, 0, 0, 0, 0, 8, 6, 130, 34);
		m_liveCheck.setText("Live chip");
		m_liveCheck.setTooltipText("Off: edit the cmos.dr file. On: edit the running machine's chip (through the memory map, in BIOS mode)");
		m_liveCheck.enableTooltip();
		m_liveCheck.setStateChangedCallback([this](CheckBox&, bool checked) { set_live(checked); });
		addWidget(m_infoLine);
		place(m_infoLine, 0, 0, 1, 0, 140, 10, -180, 34);
		addWidget(m_saveButton);
		place(m_saveButton, 1, 0, 1, 0, -172, 6, -92, 34);
		m_saveButton.setText("Save");
		m_saveButton.setFontSize(14);
		m_saveButton.setSizeMode(eSizeMode::Manual);
		m_saveButton.setTooltipText("Writes the changed bytes to the cmos.dr file");
		m_saveButton.enableTooltip();
		m_saveButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { save(); });
		addWidget(m_reloadButton);
		place(m_reloadButton, 1, 0, 1, 0, -86, 6, -8, 34);
		m_reloadButton.setText("Reload");
		m_reloadButton.setFontSize(14);
		m_reloadButton.setSizeMode(eSizeMode::Manual);
		m_reloadButton.setTooltipText("Reads the cmos.dr file again (drops the changes)");
		m_reloadButton.enableTooltip();
		m_reloadButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { reload(); });
		addWidget(m_statusLine);
		place(m_statusLine, 0, 0, 1, 0, 8, 40, -8, 62);

		addWidget(m_hex);
		place(m_hex, 0, 0, 1, 0, 8, 66, -8, 330);
		m_hex.setBigEndian(true);
		m_hex.setWriteCallback([this](ogfx::gui::StreamIndex index, u64 value, u8 size) {
			if (!m_live)
			{
				// The editor wrote the copy.
				if (m_file != nullptr)
					m_file->markChanged(index, size);
				update_info();
				return true;
			}
			stdvec<u8> bytes(size, 0);
			for (u8 i = 0; i < size; i++)
				bytes[i] = cast<u8>(value >> (8 * (size - 1 - i)));
			return write_live(cast<u16>(index), bytes);
		});
		addWidget(m_registers);
		place(m_registers, 0, 0, 1, 1, 8, 338, -8, -42);
		m_registers.setSelectCallback([this](i32 row, bool doubleClick) {
			select_register(row);
			if (doubleClick)
				getWindow().getFocusManager().requestFocus(m_valueInput);
		});

		addWidget(m_valueTitle);
		place(m_valueTitle, 0, 1, 0, 1, 8, -43, 70, -15);
		m_valueTitle.setText("Value");
		m_valueTitle.setFontSize(14);
		m_valueTitle.setSizeMode(eSizeMode::Manual);
		addWidget(m_valueInput);
		place(m_valueInput, 0, 1, 0, 1, 74, -36, 234, -8);
		m_valueInput.setFontSize(14);
		m_valueInput.setTooltipText("The selected register's new value: decimal or 0x hex (Return sets it)");
		m_valueInput.enableTooltip();
		m_valueInput.setCommitCallback([this](FieldInput& field) {
			if (field.isFocused())
				set_value();
		});
		addWidget(m_setButton);
		place(m_setButton, 0, 1, 0, 1, 240, -36, 310, -8);
		m_setButton.setText("Set");
		m_setButton.setFontSize(14);
		m_setButton.setSizeMode(eSizeMode::Manual);
		m_setButton.setTooltipText("Writes the value to the selected register");
		m_setButton.enableTooltip();
		m_setButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { set_value(); });
		addWidget(m_hintLine);
		place(m_hintLine, 0, 1, 1, 1, 320, -32, -8, -8);

		m_built = true;
		show_data(false);
		select_register(-1);
	}

	void CMOSManagerWindow::start(void)
	{
		load_file();
		show_data(false);
		select_register(-1);
	}

	void CMOSManagerWindow::load_file(void)
	{
		const String path = DragonRuntime::machine_config.cmos_path;
		String error = "";
		m_file = DiskDocument::openFile(path, error);
		if (m_file == nullptr)
			set_status(error, ErrorColor);
		else if (m_file->getSize() != CMOSSize)
			set_status(String("The file has ").add(m_file->getSize()).add(" bytes, the chip ").add(CMOSSize).add("."), WarningColor);
	}

	void CMOSManagerWindow::set_live(bool live)
	{
		m_live = live;
		if (!live && m_file != nullptr && m_file->getChangedCount() == 0)
		{
			String error = "";
			m_file->reload(error);
		}
		show_data(false);
		set_status(live ? "Live chip: every write is an edit in the history (Step back undoes it)." : "cmos.dr file: Save writes the changes, the machine reads them when it starts.", DebugListView::TextColor);
	}

	void CMOSManagerWindow::show_data(bool keepView)
	{
		if (m_live)
		{
			const ostd::ByteStream* stream = DragonRuntime::vCMOS.getByteStream();
			if (stream != nullptr)
				std::copy_n(reinterpret_cast<const u8*>(stream->data()), std::min<size_t>(stream->size(), m_liveData.size()), m_liveData.begin());
			const std::span<const i8> data(reinterpret_cast<const i8*>(m_liveData.data()), m_liveData.size());
			if (keepView)
				m_hex.updateData(data);
			else
				m_hex.setData(data, 0, 16, 0, -1, 1, "Live chip");
			m_registers.setData(m_liveData, nullptr);
		}
		else if (m_file != nullptr)
		{
			auto& buffer = m_file->getBuffer();
			const std::span<i8> data(reinterpret_cast<i8*>(buffer.data()), buffer.size());
			if (keepView)
				m_hex.updateData(data);
			else
				m_hex.setData(data, 0, 16, 0, -1, 1, m_file->getName());
			m_registers.setData(buffer, m_file.get());
		}
		else
		{
			m_hex.setData(std::span<i8>(), 0, 16, 0, -1, 1, "");
			m_registers.setData({}, nullptr);
		}
		m_hex.setBaseAddress(m_live ? data::MemoryMapAddresses::CMOS_Start : 0);
		m_hex.clearHighlights();
		for (const auto& reg : CMOS::List)
			m_hex.addHighlight(reg.address, reg.size, reg.liveData ? Color(110, 200, 230, 40) : Color(235, 200, 90, 50), reg.name);
		update_info();
	}

	void CMOSManagerWindow::update_info(void)
	{
		m_saveButton.enable(!m_live && m_file != nullptr && m_file->getChangedCount() > 0);
		m_reloadButton.enable(!m_live && m_file != nullptr);
		if (m_live)
		{
			m_infoLine.setMessage(String("Live chip at ").add(String::getHexStr(data::MemoryMapAddresses::CMOS_Start, true, 2)).add(" - ").add(String::getHexStr(data::MemoryMapAddresses::CMOS_End, true, 2)).add(": writes go through the memory map, in BIOS mode"), LiveColor);
			return;
		}
		if (m_file == nullptr)
		{
			m_infoLine.setMessage(String("No CMOS file (").add(DragonRuntime::machine_config.cmos_path).add(")"), ErrorColor);
			return;
		}
		String info = String("File: ").add(m_file->getPath()).add("  (").add(m_file->getSize()).add(" bytes)");
		if (m_file->getChangedCount() > 0)
			info.add("  -  ").add(m_file->getChangedCount()).add(" bytes not saved");
		m_infoLine.setMessage(info, m_file->getChangedCount() > 0 ? WarningColor : DebugListView::TextColor);
	}

	void CMOSManagerWindow::select_register(i32 row)
	{
		m_selected = (row >= 0 && row < m_registers.getRowCount() ? row : -1);
		m_valueInput.enable(m_selected >= 0);
		m_setButton.enable(m_selected >= 0);
		if (m_selected < 0)
		{
			m_valueInput.setText("");
			m_hintLine.setMessage("Click a register to edit it (or double-click a byte in the hex view).", DimColor);
			return;
		}
		const auto& reg = CMOS::List[m_selected];
		m_registers.setSelectedRow(m_selected);
		m_hex.setUnitSize(reg.size);
		m_hex.select(reg.address);
		m_hex.ensureVisible(reg.address, reg.size);
		m_valueInput.setText(String("").add(m_registers.valueOf(m_selected)));
		if (reg.liveData)
			m_hintLine.setMessage(String(reg.name).add(" is live data: the runtime writes it at every start."), LiveColor);
		else
			m_hintLine.setMessage(String(reg.name).add(": ").add(reg.description).add("."), DimColor);
	}

	void CMOSManagerWindow::set_value(void)
	{
		if (m_selected < 0)
			return;
		const auto& reg = CMOS::List[m_selected];
		u64 value = 0;
		if (!storage::parseSize(m_valueInput.getText(), value))
		{
			set_status(String("'").add(m_valueInput.getText()).add("' isn't a number (decimal or 0x hex)."), ErrorColor);
			return;
		}
		const u64 max = (reg.size >= 4 ? 0xFFFFFFFFull : (1ull << (8 * reg.size)) - 1);
		if (value > max)
		{
			set_status(String(reg.name).add(" holds ").add(reg.size).add(reg.size == 1 ? " byte" : " bytes").add(": at most ").add(max).add("."), ErrorColor);
			return;
		}
		stdvec<u8> bytes(reg.size, 0);
		for (u8 i = 0; i < reg.size; i++)
			bytes[i] = cast<u8>(value >> (8 * (reg.size - 1 - i)));
		if (write_bytes(reg.address, bytes))
			set_status(String(reg.name).add(" = ").add(value).add(m_live ? " (live chip)" : " (not saved yet)"), m_live ? SuccessColor : WarningColor);
		select_register(m_selected);
	}

	bool CMOSManagerWindow::write_bytes(u16 offset, std::span<const u8> bytes)
	{
		if (m_live)
			return write_live(offset, bytes);
		if (m_file == nullptr || !m_file->getBufferDisk().write(offset, bytes))
			return false;
		show_data(true);
		return true;
	}

	bool CMOSManagerWindow::write_live(u16 offset, std::span<const u8> bytes)
	{
		auto& cpu = DragonRuntime::cpu;
		const bool biosMode = cpu.isInBIOSMOde();
		bool ok = true;
		for (size_t i = 0; i < bytes.size(); i++)
		{
			const u32 address = cast<u32>(data::MemoryMapAddresses::CMOS_Start) + offset + cast<u32>(i);
			if (address > data::MemoryMapAddresses::CMOS_End)
			{
				ok = false;
				break;
			}
			// The chip only takes writes in BIOS mode.
			cpu.setBIOSMode(true);
			DragonRuntime::memMap.write8(cast<u16>(address), cast<i8>(bytes[i]));
		}
		cpu.setBIOSMode(biosMode);
		while (data::ErrorHandler::hasError())
		{
			ok = false;
			auto error = data::ErrorHandler::popError();
			set_status(String("CMOS write failed: ").add(error.text), ErrorColor);
		}
		m_debugger.commitEdit();
		show_data(true);
		return ok;
	}

	void CMOSManagerWindow::save(void)
	{
		if (m_live || m_file == nullptr || m_file->getChangedCount() == 0)
			return;
		const u64 count = m_file->getChangedCount();
		String error = "";
		if (m_file->save(error))
			set_status(String("Saved ").add(count).add(count == 1 ? " byte to " : " bytes to ").add(m_file->getName()).add(": the machine reads it when it starts."), SuccessColor);
		else
			set_status(String("Save failed: ").add(error), ErrorColor);
		show_data(true);
	}

	void CMOSManagerWindow::reload(void)
	{
		if (m_live)
			return;
		if (m_file == nullptr)
			load_file();
		else
		{
			String error = "";
			if (!m_file->reload(error))
				set_status(error, ErrorColor);
			else
				set_status("Reloaded.", DebugListView::TextColor);
		}
		show_data(true);
		select_register(m_selected);
	}

	void CMOSManagerWindow::set_status(const String& text, const Color& color)
	{
		m_statusLine.setMessage(text, color);
		if (color.r == ErrorColor.r && color.g == ErrorColor.g && color.b == ErrorColor.b)
			m_debugger.console().fg(ostd::ConsoleColors::Red).p("CMOS Manager: ").p(text).reset().nl();
	}

	void CMOSManagerWindow::syncWithMachine(void)
	{
		if (!m_built || !isVisible() || !m_live)
			return;
		show_data(true);
	}
}
