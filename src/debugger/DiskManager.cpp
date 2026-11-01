#include "DiskManager.hpp"
#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "../storage/FileSystem.hpp"
#include <chrono>
#include <filesystem>

namespace dragon
{
	using namespace ogfx::gui;

	static void place(Widget& widget, f32 anchorLeft, f32 anchorTop, f32 anchorRight, f32 anchorBottom, f32 left, f32 top, f32 right, f32 bottom)
	{
		widget.setAnchors(anchorLeft, anchorTop, anchorRight, anchorBottom);
		widget.setOffsets(left, top, right, bottom);
	}

	static u64 now_ms(void)
	{
		return cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	static const Color ErrorColor { 240, 100, 100 };
	static const Color SuccessColor { 120, 215, 120 };
	static const Color WarningColor { 235, 200, 90 };
	static const Color DimColor { 130, 140, 130 };

	static void setup_button(Button& button, const String& text, const String& tooltip)
	{
		button.setText(text);
		button.setFontSize(14);
		button.setSizeMode(eSizeMode::Manual);
		if (tooltip != "")
		{
			button.setTooltipText(tooltip);
			button.enableTooltip();
		}
	}

	static void setup_title(Label& label, const String& text)
	{
		label.setText(text);
		label.setFontSize(14);
		label.setSizeMode(eSizeMode::Manual);
	}

	// Records the ranges written through it (to save them from the buffer afterwards).
	class RecordingDisk : public storage::Disk
	{
		public:
			inline RecordingDisk(storage::Disk& disk) : m_disk(disk) {  }
			inline u64 getSize(void) const override { return m_disk.getSize(); }
			inline bool read(u64 address, std::span<u8> out) override { return m_disk.read(address, out); }
			inline bool write(u64 address, std::span<const u8> data) override
			{
				if (!m_disk.write(address, data))
					return false;
				m_ranges.push_back({ address, data.size() });
				return true;
			}
			inline const stdvec<std::pair<u64, u64>>& getRanges(void) const { return m_ranges; }

		private:
			storage::Disk& m_disk;
			stdvec<std::pair<u64, u64>> m_ranges;
	};




	//DiskDocument
	std::unique_ptr<DiskDocument> DiskDocument::openDrive(i32 driveID, hw::VirtualHardDrive& drive, const String& path, String& outError)
	{
		if (!drive.isInitialized())
		{
			outError = String("Disk ").add(driveID).add(" isn't mounted.");
			return nullptr;
		}
		std::unique_ptr<DiskDocument> document(new DiskDocument());
		document->m_driveID = driveID;
		document->m_drive = &drive;
		document->m_path = path;
		document->m_disk = std::make_unique<storage::DriveDisk>(drive);
		if (!document->reload(outError))
			return nullptr;
		return document;
	}

	std::unique_ptr<DiskDocument> DiskDocument::openFile(const String& path, String& outError)
	{
		auto disk = std::make_unique<storage::FileDisk>();
		if (!disk->open(path))
		{
			outError = String("Unable to open the file: ").add(path);
			return nullptr;
		}
		std::unique_ptr<DiskDocument> document(new DiskDocument());
		document->m_path = path;
		document->m_disk = std::move(disk);
		if (!document->reload(outError))
			return nullptr;
		return document;
	}

	String DiskDocument::getName(void) const
	{
		return String(std::filesystem::path(m_path.cpp_str()).filename().string());
	}

	void DiskDocument::init_buffer_disk(void)
	{
		m_bufferDisk = std::make_unique<storage::MemoryDisk>(std::span<u8>(m_buffer));
		m_bufferDisk->setWriteCallback([this](u64 address, u64 size) { markChanged(address, size); });
	}

	bool DiskDocument::reload(String& outError)
	{
		const u64 size = m_disk->getSize();
		stdvec<u8> data(size, 0);
		if (size > 0 && !m_disk->read(0, data))
		{
			outError = "Unable to read the disk.";
			return false;
		}
		// Same size: the buffer stays where it is (the hex editor points at it).
		if (m_buffer.size() == size)
			std::copy(data.begin(), data.end(), m_buffer.begin());
		else
			m_buffer = std::move(data);
		m_changed.assign(size, false);
		m_changedCount = 0;
		m_changedMin = m_changedMax = 0;
		if (m_drive != nullptr)
			m_seenWriteCount = m_drive->getWriteCount();
		init_buffer_disk();
		return true;
	}

	bool DiskDocument::sync(void)
	{
		if (m_drive == nullptr || m_drive->getWriteCount() == m_seenWriteCount)
			return false;
		m_seenWriteCount = m_drive->getWriteCount();
		stdvec<u8> data(m_buffer.size(), 0);
		if (data.empty() || !m_disk->read(0, data))
			return false;
		bool changed = false;
		for (size_t i = 0; i < data.size(); i++)
		{
			if (m_buffer[i] == data[i] || m_changed[i])
				continue;
			m_buffer[i] = data[i];
			changed = true;
		}
		return changed;
	}

	bool DiskDocument::save(String& outError)
	{
		u64 i = m_changedMin;
		while (m_changedCount > 0 && i < m_changedMax)
		{
			if (!m_changed[i])
			{
				i++;
				continue;
			}
			u64 end = i;
			while (end < m_changedMax && m_changed[end])
				end++;
			if (!saveRange(i, end - i, outError))
				return false;
			i = end;
		}
		return true;
	}

	bool DiskDocument::saveRange(u64 address, u64 size, String& outError)
	{
		if (address > m_buffer.size() || size > m_buffer.size() - address)
		{
			outError = "Outside of the disk.";
			return false;
		}
		if (size == 0)
			return true;
		if (!m_disk->write(address, std::span<const u8>(m_buffer.data() + address, size)))
		{
			outError = String("Unable to write to the disk at ").add(String::getHexStr(address, true, 4)).add(".");
			return false;
		}
		clear_changes(address, address + size);
		if (m_drive != nullptr)
			m_seenWriteCount = m_drive->getWriteCount();
		return true;
	}

	void DiskDocument::markChanged(u64 address, u64 size)
	{
		const u64 end = std::min<u64>(address + size, m_buffer.size());
		for (u64 i = address; i < end; i++)
		{
			if (m_changed[i])
				continue;
			m_changed[i] = true;
			if (m_changedCount == 0)
			{
				m_changedMin = i;
				m_changedMax = i + 1;
			}
			m_changedMin = std::min(m_changedMin, i);
			m_changedMax = std::max(m_changedMax, i + 1);
			m_changedCount++;
		}
	}

	void DiskDocument::clear_changes(u64 from, u64 to)
	{
		if (m_changedCount == 0)
			return;
		from = std::max(from, m_changedMin);
		to = std::min(to, m_changedMax);
		for (u64 i = from; i < to; i++)
		{
			if (!m_changed[i])
				continue;
			m_changed[i] = false;
			m_changedCount--;
		}
		if (m_changedCount == 0)
			m_changedMin = m_changedMax = 0;
	}




	//DiskMapView
	DiskMapView::DiskMapView(Window& window) : Widget({ 0, 0, 0, 0 }, window)
	{
		setTypeName("dragon::DiskMapView");
		setStylesheetCategoryName("dragonDiskMap");
		disableFocus();
		disableChildren();
		validate();
	}

	Color DiskMapView::partitionColor(i32 index)
	{
		const Color colors[] = { { 70, 130, 200 }, { 205, 145, 60 }, { 90, 170, 90 }, { 170, 95, 175 }, { 195, 85, 85 }, { 70, 170, 170 } };
		return colors[std::max(0, index) % 6];
	}

	void DiskMapView::setLayout(const storage::PartitionScheme* scheme, const storage::tPartitionTable* table, u64 diskSize)
	{
		using eKind = Segment::eKind;
		m_segments.clear();
		m_diskSize = diskSize;
		if (scheme == nullptr || table == nullptr)
		{
			m_segments.push_back({ eKind::Free, 0, diskSize, -1, "no partition table" });
			return;
		}
		stdvec<Segment> items;
		for (const auto& area : scheme->getReservedAreas(diskSize))
			items.push_back({ eKind::Reserved, area.start, area.size, -1, area.name });
		for (size_t i = 0; i < table->partitions.size(); i++)
		{
			const auto& part = table->partitions[i];
			items.push_back({ eKind::Partition, part.start, part.size, cast<i32>(i), part.label });
		}
		std::stable_sort(items.begin(), items.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
		const u64 first = scheme->getFirstUsableAddress(diskSize);
		const u64 last = scheme->getLastUsableAddress(diskSize);
		u64 cursor = 0;
		auto l_free = [&](u64 from, u64 to) {
			from = std::max(from, first);
			to = std::min(to, last);
			if (to > from)
				m_segments.push_back({ eKind::Free, from, to - from, -1, "free" });
		};
		for (const auto& item : items)
		{
			l_free(cursor, item.start);
			m_segments.push_back(item);
			cursor = std::max(cursor, item.start + item.size);
		}
		l_free(cursor, last);
	}

	stdvec<Rectangle> DiskMapView::segment_rects(void) const
	{
		stdvec<Rectangle> rects;
		const Rectangle bounds = getGlobalBounds();
		const f32 n = cast<f32>(m_segments.size());
		if (m_segments.empty())
			return rects;
		f64 total = 0;
		for (const auto& segment : m_segments)
			total += cast<f64>(segment.size);
		const f32 minWidth = std::min(MinSegmentWidth, bounds.w / n);
		const f32 spare = std::max(0.0f, bounds.w - minWidth * n);
		f32 x = bounds.x;
		for (const auto& segment : m_segments)
		{
			const f32 w = minWidth + (total > 0 ? spare * cast<f32>(cast<f64>(segment.size) / total) : spare / n);
			rects.push_back({ x, bounds.y, w, bounds.h });
			x += w;
		}
		return rects;
	}

	i32 DiskMapView::segment_at(const Vec2& position) const
	{
		const auto rects = segment_rects();
		for (size_t i = 0; i < rects.size(); i++)
		{
			if (rects[i].contains(position, true))
				return cast<i32>(i);
		}
		return -1;
	}

	void DiskMapView::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		using eKind = Segment::eKind;
		const Rectangle bounds = getGlobalBounds();
		gfx.fillRect(bounds, DebugListView::BackgroundColor);
		const auto rects = segment_rects();
		const f32 lineHeight = gfx.getStringDimensions("Ag", 13).y;
		for (size_t i = 0; i < m_segments.size(); i++)
		{
			const Segment& segment = m_segments[i];
			const Rectangle r { rects[i].x + 1, rects[i].y + 1, rects[i].w - 2, rects[i].h - 2 };
			Color fill { 34, 38, 34 };
			Color text = DimColor;
			String name = segment.name;
			if (segment.kind == eKind::Reserved)
			{
				fill = { 58, 60, 58 };
				text = { 175, 180, 175 };
			}
			else if (segment.kind == eKind::Partition)
			{
				const Color c = partitionColor(segment.partition);
				fill = { cast<u8>(c.r / 2), cast<u8>(c.g / 2), cast<u8>(c.b / 2) };
				text = { 235, 235, 235 };
				name = String("").add(segment.partition + 1).add(segment.name == "" ? String("") : String(": ").add(segment.name));
				gfx.fillRect({ r.x, r.y, r.w, 4 }, c);
			}
			if (cast<i32>(i) == m_hovered)
				fill = { cast<u8>(std::min(255, fill.r + 18)), cast<u8>(std::min(255, fill.g + 18)), cast<u8>(std::min(255, fill.b + 18)) };
			gfx.fillRect({ r.x, segment.kind == eKind::Partition ? r.y + 4 : r.y, r.w, segment.kind == eKind::Partition ? r.h - 4 : r.h }, fill);
			const bool selected = (segment.kind == eKind::Partition && segment.partition == m_selectedPartition) ||
								  (segment.kind == eKind::Free && m_selectedFree >= 0 && cast<u64>(m_selectedFree) == segment.start);
			gfx.drawRect(r, selected ? Color(240, 240, 240) : Color(70, 78, 70), selected ? 2 : 1);
			gfx.pushClippingRect({ r.x + 3, r.y + 2, std::max(0.0f, r.w - 6), r.h - 4 }, true);
			const f32 y = r.y + r.h * 0.5f - lineHeight;
			gfx.drawString(name, { r.x + 5, y }, text, 13);
			gfx.drawString(storage::formatSize(segment.size), { r.x + 5, y + lineHeight }, text, 13);
			gfx.popClippingRect();
		}
	}

	void DiskMapView::onMousePressed(const Event& event)
	{
		const i32 index = segment_at({ event.mouse->position_x, event.mouse->position_y });
		if (index < 0)
			return;
		const Segment& segment = m_segments[index];
		if (segment.kind == Segment::eKind::Partition)
			setSelectedPartition(segment.partition);
		else if (segment.kind == Segment::eKind::Free)
			setSelectedFree(segment.start);
		if (m_onSelect)
			m_onSelect(segment);
	}

	void DiskMapView::onMouseMoved(const Event& event)
	{
		m_hovered = segment_at({ event.mouse->position_x, event.mouse->position_y });
	}

	void DiskMapView::onMouseExited(const Event& event)
	{
		m_hovered = -1;
	}




	//PartitionListView
	PartitionListView::PartitionListView(Window& window) : DebugListView(window)
	{
		setTypeName("dragon::PartitionListView");
		setColumns({ { "#", 6 }, { "Label", 24 }, { "Start", 12 }, { "End", 12 }, { "Size", 12 }, { "Flags", 10 }, { "File system", 12 } });
	}

	i32 PartitionListView::getRowCount(void) const
	{
		return m_table == nullptr ? 0 : cast<i32>(m_table->partitions.size());
	}

	String PartitionListView::getEmptyText(void) const
	{
		if (m_scheme == nullptr)
			return "No partition table: pick a format and click 'New table'.";
		return "No partitions: click a free area, then 'New partition'.";
	}

	void PartitionListView::drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds)
	{
		const storage::tPartition& part = m_table->partitions[row];
		switch (column)
		{
			case 0:
				gfx.fillRect({ bounds.x, bounds.y + bounds.h * 0.5f - 5, 10, 10 }, DiskMapView::partitionColor(row));
				text(gfx, String("").add(row + 1), bounds.x + 16, bounds.y, TextColor);
				break;
			case 1: text(gfx, part.label == "" ? String("<no label>") : part.label, bounds.x, bounds.y, part.label == "" ? DimTextColor : Color(110, 220, 230)); break;
			case 2: text(gfx, String::getHexStr(part.start, true, 4), bounds.x, bounds.y, Color(235, 215, 90)); break;
			case 3: text(gfx, String::getHexStr(part.end(), true, 4), bounds.x, bounds.y, Color(235, 215, 90)); break;
			case 4: text(gfx, storage::formatSize(part.size), bounds.x, bounds.y, TextColor); break;
			case 5: text(gfx, m_scheme == nullptr ? String("") : m_scheme->flagsToString(part.flags), bounds.x, bounds.y, Color(240, 170, 60)); break;
			case 6:
				if (m_fileSystems != nullptr && row < cast<i32>(m_fileSystems->size()))
					text(gfx, (*m_fileSystems)[row], bounds.x, bounds.y, DimTextColor);
				break;
			default: break;
		}
	}

	void PartitionListView::onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick)
	{
		if (row < getRowCount() && m_onSelect)
			m_onSelect(row);
	}




	//FieldInput
	void FieldInput::onKeyPressed(const Event& event)
	{
		const i32 key = event.keyboard->keyCode;
		if (key == ogfx::KeyCode::Return || key == ogfx::KeyCode::KpEnter)
		{
			if (m_onCommit)
				m_onCommit(*this);
			event.handle();
			return;
		}
		TextEdit::onKeyPressed(event);
	}

	void FieldInput::onFocusLost(const Event& event)
	{
		TextEdit::onFocusLost(event);
		if (m_onCommit)
			m_onCommit(*this);
	}




	//PartitionManagerWindow
	PartitionManagerWindow::PartitionManagerWindow(Window& window, DiskManagerWindow& diskManager) : SubWindow(window, "Partition Manager"), m_diskManager(diskManager)
	{
		setBounds({ 300, 110, 960, 640 });
		setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
	}

	void PartitionManagerWindow::build(void)
	{
		addWidget(m_infoLine);
		place(m_infoLine, 0, 0, 1, 0, 8, 8, -330, 30);
		addWidget(m_schemeCombo);
		place(m_schemeCombo, 1, 0, 1, 0, -322, 4, -200, 32);
		for (const auto& scheme : storage::PartitionSchemes::list())
			m_schemeCombo.addMenuItem(scheme->getName());
		m_schemeCombo.setItemCallback([](const ContextMenu::Entry&) {  });
		m_schemeCombo.setTooltipText("The partition table format for 'New table'");
		m_schemeCombo.enableTooltip();
		addWidget(m_newTableButton);
		place(m_newTableButton, 1, 0, 1, 0, -194, 4, -8, 32);
		setup_button(m_newTableButton, "New table", "Starts an empty table of the chosen format (written by Apply)");
		m_newTableButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_table(); });

		addWidget(m_map);
		place(m_map, 0, 0, 1, 0, 8, 40, -8, 104);
		m_map.setSelectCallback([this](const DiskMapView::Segment& segment) {
			if (segment.kind == DiskMapView::Segment::eKind::Partition)
				select_partition(segment.partition);
			else if (segment.kind == DiskMapView::Segment::eKind::Free)
			{
				select_partition(-1);
				m_selectedFree = cast<i64>(segment.start);
				m_map.setSelectedFree(segment.start);
			}
		});
		addWidget(m_list);
		place(m_list, 0, 0, 1, 1, 8, 112, -8, -176);
		m_list.setSelectCallback([this](i32 row) { select_partition(row); });

		// The selected partition.
		auto l_field = [this](Label& title, const String& text, FieldInput& input, f32 x, f32 titleWidth, f32 inputWidth, const String& tooltip) {
			addWidget(title);
			setup_title(title, text);
			place(title, 0, 1, 0, 1, x, -175, x + titleWidth, -147);
			addWidget(input);
			input.setFontSize(14);
			input.setTooltipText(tooltip);
			input.enableTooltip();
			place(input, 0, 1, 0, 1, x + titleWidth + 4, -168, x + titleWidth + 4 + inputWidth, -140);
			input.setCommitCallback([this](FieldInput& field) { commit_field(field); });
		};
		l_field(m_labelTitle, "Label", m_labelInput, 8, 64, 250, "The partition's label (Return applies it)");
		l_field(m_startTitle, "Start", m_startInput, 334, 60, 130, "Start address: 0x400, 1024, 1K... (Return applies it)");
		l_field(m_sizeTitle, "Size", m_sizeInput, 532, 54, 130, "Size: 0x1400, 5120, 5K, 1.5M... (Return applies it)");
		addWidget(m_growButton);
		place(m_growButton, 0, 1, 0, 1, 724, -168, 796, -140);
		setup_button(m_growButton, "Max", "Grows the partition up to the next one (or the end of the disk)");
		m_growButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { grow_partition(); });
		addWidget(m_newButton);
		place(m_newButton, 1, 1, 1, 1, -262, -134, -130, -106);
		setup_button(m_newButton, "New partition", "A partition in the selected free area (or the largest one)");
		m_newButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_partition(); });
		addWidget(m_deleteButton);
		place(m_deleteButton, 1, 1, 1, 1, -124, -134, -8, -106);
		setup_button(m_deleteButton, "Delete", "Removes the selected partition (its data stays on the disk)");
		m_deleteButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { delete_partition(); });

		addWidget(m_messageLine);
		place(m_messageLine, 0, 1, 1, 1, 8, -100, -280, -6);
		addWidget(m_revertButton);
		place(m_revertButton, 1, 1, 1, 1, -262, -36, -130, -6);
		setup_button(m_revertButton, "Revert", "Drops the pending changes (reads the table from the disk again)");
		m_revertButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) {
			m_note = "";
			load_table();
		});
		addWidget(m_applyButton);
		place(m_applyButton, 1, 1, 1, 1, -124, -36, -8, -6);
		setup_button(m_applyButton, "Apply", "Writes the table to the disk (click twice)");
		m_applyButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { apply(); });
	}

	void PartitionManagerWindow::openFor(DiskDocument* document)
	{
		m_document = document;
		m_note = "";
		load_table();
		open();
	}

	void PartitionManagerWindow::documentChanged(DiskDocument* document)
	{
		if (!isVisible())
		{
			m_document = document;
			return;
		}
		if (document == m_document)
			return;
		m_document = document;
		m_note = "";
		load_table();
	}

	void PartitionManagerWindow::load_table(void)
	{
		m_scheme = nullptr;
		m_table = storage::tPartitionTable();
		m_original = m_table;
		m_hasOriginal = false;
		m_selected = -1;
		m_selectedFree = -1;
		m_fieldsFor = -1;
		m_confirmUntil = 0;
		if (m_document != nullptr)
		{
			storage::Disk& disk = m_document->getBufferDisk();
			m_scheme = storage::PartitionSchemes::detect(disk);
			if (m_scheme != nullptr)
			{
				String error = "";
				if (m_scheme->read(disk, m_table, error))
				{
					m_original = m_table;
					m_hasOriginal = true;
				}
				else
				{
					m_note = String("Unable to read the ").add(m_scheme->getName()).add(" table: ").add(error);
					m_noteColor = ErrorColor;
					m_scheme = nullptr;
					m_table = storage::tPartitionTable();
				}
			}
			if (m_scheme != nullptr)
			{
				for (i32 i = 0; i < m_schemeCombo.getItemCount(); i++)
				{
					if (storage::PartitionSchemes::list()[i]->getName() == m_scheme->getName())
						m_schemeCombo.setSelectedItem(i);
				}
			}
		}
		select_partition(m_table.partitions.empty() ? -1 : 0);
	}

	bool PartitionManagerWindow::is_pending(void) const
	{
		if (!m_hasOriginal)
			return m_scheme != nullptr;
		return !(m_table == m_original);
	}

	void PartitionManagerWindow::set_message(const String& text, const Color& color)
	{
		m_note = text;
		m_noteColor = color;
	}

	void PartitionManagerWindow::refresh_view(void)
	{
		const u64 diskSize = (m_document != nullptr ? m_document->getSize() : 0);
		const storage::tPartitionTable* table = (m_scheme != nullptr ? &m_table : nullptr);
		m_map.setLayout(m_scheme, table, diskSize);
		if (m_selected >= 0)
			m_map.setSelectedPartition(m_selected);
		else if (m_selectedFree >= 0)
			m_map.setSelectedFree(cast<u64>(m_selectedFree));
		else
			m_map.setSelectedPartition(-1);

		// The file system of each partition.
		m_fileSystems.clear();
		for (const auto& part : m_table.partitions)
		{
			String name = "-";
			if (m_document != nullptr && m_document->getBufferDisk().contains(part.start, part.size) && part.size > 0)
			{
				storage::DiskRegion region(m_document->getBufferDisk(), part.start, part.size);
				const storage::FileSystem* fileSystem = storage::FileSystems::detect(region);
				name = (fileSystem != nullptr ? fileSystem->getName() : String("unknown"));
			}
			m_fileSystems.push_back(name);
		}
		m_list.setTable(m_scheme, table, &m_fileSystems);
		m_list.setSelectedRow(m_selected);

		// Info
		if (m_document == nullptr)
			m_infoLine.setMessage("No disk: pick one in the Disk Manager.", DimColor);
		else
		{
			String info = String(m_document->isMachineDrive() ? String("Disk ").add(m_document->getDriveID()).add(": ") : String("File: ")).add(m_document->getName());
			info.add("  -  ").add(storage::formatSize(diskSize));
			if (m_scheme == nullptr)
				info.add("  -  no partition table");
			else
			{
				info.add("  -  ").add(m_scheme->getName());
				if (m_hasOriginal && m_table.version != "")
					info.add(" ").add(m_table.version);
				info.add(" (").add(m_scheme->getDescription()).add("), ").add(cast<u64>(m_table.partitions.size())).add(" / ").add(m_scheme->getMaxPartitions()).add(" partitions");
			}
			m_infoLine.setMessage(info, DebugListView::TextColor);
		}

		// Messages: the last action, then the table's problems.
		String message = m_note;
		Color color = m_noteColor;
		const stdvec<String> errors = (m_scheme != nullptr ? m_scheme->validate(m_table, diskSize) : stdvec<String>());
		if (!errors.empty())
		{
			for (const auto& error : errors)
				message.add(message == "" ? "" : "\n").add(error);
			color = ErrorColor;
		}
		else if (is_pending() && m_note == "")
		{
			message = "Changes pending: Apply writes the table to the disk.";
			color = WarningColor;
		}
		m_messageLine.setMessage(message, color);

		const bool hasPart = (m_selected >= 0 && m_selected < cast<i32>(m_table.partitions.size()));
		m_labelInput.enable(hasPart);
		m_startInput.enable(hasPart);
		m_sizeInput.enable(hasPart);
		m_growButton.enable(hasPart);
		m_deleteButton.enable(hasPart);
		m_newButton.enable(m_scheme != nullptr && m_table.partitions.size() < m_scheme->getMaxPartitions());
		m_applyButton.enable(m_document != nullptr && m_scheme != nullptr && is_pending() && errors.empty());
		m_revertButton.enable(m_document != nullptr && is_pending());
		m_newTableButton.enable(m_document != nullptr);
		for (auto& check : m_flagChecks)
			check.enable(hasPart);
	}

	void PartitionManagerWindow::select_partition(i32 index)
	{
		m_selected = (index >= 0 && index < cast<i32>(m_table.partitions.size()) ? index : -1);
		if (m_selected >= 0)
			m_selectedFree = -1;
		fill_fields();
		refresh_view();
	}

	void PartitionManagerWindow::fill_fields(void)
	{
		m_fillingFields = true;
		// One check box per flag of the format.
		const stdvec<storage::tPartitionFlag> flags = (m_scheme != nullptr ? m_scheme->getFlags() : stdvec<storage::tPartitionFlag>());
		while (m_flagChecks.size() < flags.size())
		{
			CheckBox& check = m_flagChecks.emplace_back(getWindow());
			addWidget(check);
			const f32 x = 8 + 120 * cast<f32>(m_flagChecks.size() - 1);
			place(check, 0, 1, 0, 1, x, -132, x + 114, -108);
		}
		for (size_t i = 0; i < m_flagChecks.size(); i++)
		{
			CheckBox& check = m_flagChecks[i];
			check.setVisible(i < flags.size());
			if (i >= flags.size())
				continue;
			const u8 bit = flags[i].bit;
			check.setText(flags[i].name);
			check.setTooltipText(flags[i].description);
			check.enableTooltip();
			check.setStateChangedCallback([this, bit](CheckBox&, bool checked) {
				if (m_fillingFields || m_selected < 0)
					return;
				m_table.partitions[m_selected].setFlag(bit, checked);
				m_note = "";
				refresh_view();
			});
			check.setChecked(m_selected >= 0 && m_table.partitions[m_selected].hasFlag(bit));
		}
		m_fieldsFor = m_selected;
		if (m_selected >= 0)
		{
			const storage::tPartition& part = m_table.partitions[m_selected];
			m_labelInput.setText(part.label);
			m_startInput.setText(String::getHexStr(part.start, true, 4));
			m_sizeInput.setText(String::getHexStr(part.size, true, 4));
		}
		else
		{
			m_labelInput.setText("");
			m_startInput.setText("");
			m_sizeInput.setText("");
		}
		m_fillingFields = false;
	}

	void PartitionManagerWindow::commit_field(FieldInput& field)
	{
		if (m_fillingFields || m_fieldsFor < 0 || m_fieldsFor >= cast<i32>(m_table.partitions.size()) || m_scheme == nullptr)
			return;
		storage::tPartition& part = m_table.partitions[m_fieldsFor];
		const String text = field.getText();
		const String name = String("Partition ").add(m_fieldsFor + 1);
		if (&field == &m_labelInput)
		{
			const String label = text.new_trim();
			if (label == part.label)
				return;
			if (label.len() > m_scheme->getMaxLabelLength())
			{
				set_message(String(name).add(": the label is too long (max ").add(m_scheme->getMaxLabelLength()).add(" bytes)."), ErrorColor);
				refresh_view();
				return;
			}
			part.label = label;
		}
		else
		{
			u64 value = 0;
			if (!storage::parseSize(text, value))
			{
				set_message(String(name).add(": '").add(text).add("' isn't a number (0x400, 1024, 4K, 1.5M...)."), ErrorColor);
				refresh_view();
				return;
			}
			u64& target = (&field == &m_startInput ? part.start : part.size);
			if (value == target)
				return;
			target = value;
		}
		m_note = "";
		const bool focused = field.isFocused();
		if (m_fieldsFor == m_selected)
		{
			// Normalized (hex).
			m_fillingFields = true;
			if (!focused || &field != &m_labelInput)
				fill_fields();
			m_fillingFields = false;
		}
		refresh_view();
	}

	bool PartitionManagerWindow::find_free_area(u64& outStart, u64& outSize) const
	{
		bool found = false;
		for (const auto& segment : m_map.getSegments())
		{
			if (segment.kind != DiskMapView::Segment::eKind::Free || segment.size == 0)
				continue;
			if (m_selectedFree >= 0 && segment.start == cast<u64>(m_selectedFree))
			{
				outStart = segment.start;
				outSize = segment.size;
				return true;
			}
			if (!found || segment.size > outSize)
			{
				outStart = segment.start;
				outSize = segment.size;
				found = true;
			}
		}
		return found;
	}

	void PartitionManagerWindow::new_partition(void)
	{
		if (m_scheme == nullptr)
			return;
		if (m_table.partitions.size() >= m_scheme->getMaxPartitions())
		{
			set_message(String(m_scheme->getName()).add(" allows ").add(m_scheme->getMaxPartitions()).add(" partitions."), ErrorColor);
			refresh_view();
			return;
		}
		u64 start = 0, size = 0;
		if (!find_free_area(start, size))
		{
			set_message("No free space on the disk: shrink or delete a partition first.", ErrorColor);
			refresh_view();
			return;
		}
		storage::tPartition part;
		part.start = start;
		part.size = std::min<u64>(size, m_scheme->getMaxAddress() - start);
		m_table.partitions.push_back(part);
		m_selectedFree = -1;
		set_message(String("Partition ").add(cast<u64>(m_table.partitions.size())).add(" added: set its label, start and size."), DebugListView::TextColor);
		select_partition(cast<i32>(m_table.partitions.size()) - 1);
	}

	void PartitionManagerWindow::delete_partition(void)
	{
		if (m_selected < 0 || m_selected >= cast<i32>(m_table.partitions.size()))
			return;
		const i32 index = m_selected;
		m_table.partitions.erase(m_table.partitions.begin() + index);
		set_message(String("Partition ").add(index + 1).add(" removed (the following ones moved up)."), DebugListView::TextColor);
		select_partition(std::min(index, cast<i32>(m_table.partitions.size()) - 1));
	}

	void PartitionManagerWindow::grow_partition(void)
	{
		if (m_scheme == nullptr || m_document == nullptr || m_selected < 0 || m_selected >= cast<i32>(m_table.partitions.size()))
			return;
		storage::tPartition& part = m_table.partitions[m_selected];
		u64 next = m_scheme->getLastUsableAddress(m_document->getSize());
		for (size_t i = 0; i < m_table.partitions.size(); i++)
		{
			const auto& other = m_table.partitions[i];
			if (cast<i32>(i) != m_selected && other.start > part.start)
				next = std::min(next, other.start);
		}
		if (next <= part.start)
			return;
		part.size = next - part.start;
		m_note = "";
		select_partition(m_selected);
	}

	void PartitionManagerWindow::new_table(void)
	{
		if (m_document == nullptr)
			return;
		const String name = m_schemeCombo.getSelectedEntry().text;
		const storage::PartitionScheme* scheme = storage::PartitionSchemes::find(name);
		if (scheme == nullptr)
			return;
		if (m_document->getSize() < scheme->getMinDiskSize())
		{
			set_message(String("The disk is too small for a ").add(name).add(" table."), ErrorColor);
			refresh_view();
			return;
		}
		m_scheme = scheme;
		m_table = storage::tPartitionTable();
		m_table.scheme = scheme->getName();
		// Not the disk's table anymore: Apply writes it even if it's the same.
		m_hasOriginal = false;
		m_selectedFree = -1;
		set_message(String("New empty ").add(name).add(" table: add the partitions, then Apply."), DebugListView::TextColor);
		select_partition(-1);
	}

	void PartitionManagerWindow::apply(void)
	{
		if (m_document == nullptr || m_scheme == nullptr || !is_pending())
			return;
		if (!m_scheme->validate(m_table, m_document->getSize()).empty())
		{
			refresh_view();
			return;
		}
		const u64 now = now_ms();
		if (now > m_confirmUntil)
		{
			m_confirmUntil = now + ConfirmMs;
			m_applyButton.setText("Confirm");
			set_message(String("Click Confirm to write the ").add(m_scheme->getName()).add(" table to ").add(m_document->getName()).add("."), WarningColor);
			refresh_view();
			return;
		}
		m_confirmUntil = 0;
		m_applyButton.setText("Apply");
		// Into the Disk Manager's copy, then those bytes to the disk.
		RecordingDisk disk(m_document->getBufferDisk());
		String error = "";
		bool ok = m_scheme->write(disk, m_table, error);
		for (const auto& [address, size] : disk.getRanges())
		{
			if (ok)
				ok = m_document->saveRange(address, size, error);
		}
		if (!ok)
		{
			set_message(String("Unable to write the table: ").add(error), ErrorColor);
			refresh_view();
			return;
		}
		m_diskManager.tableWritten(*m_document);
		const i32 selected = m_selected;
		m_note = "";
		load_table();
		set_message(String(m_scheme != nullptr ? m_scheme->getName() : String("The")).add(" table written to ").add(m_document->getName()).add("."), SuccessColor);
		select_partition(selected);
	}

	void PartitionManagerWindow::onUpdate(void)
	{
		SubWindow::onUpdate();
		if (m_confirmUntil != 0 && now_ms() > m_confirmUntil)
		{
			m_confirmUntil = 0;
			m_applyButton.setText("Apply");
			m_note = "";
			refresh_view();
		}
	}




	//DiskManagerWindow
	DiskManagerWindow::DiskManagerWindow(Window& window, Debugger& debugger) : SubWindow(window, "Disk Manager"), m_debugger(debugger), m_partitionManager(window, *this)
	{
		setBounds({ 220, 140, 980, 640 });
		setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
	}

	void DiskManagerWindow::build(void)
	{
		// Row 1: the disk, the image files.
		addWidget(m_diskCombo);
		place(m_diskCombo, 0, 0, 1, 0, 8, 6, -476, 34);
		m_diskCombo.setTooltipText("The machine's disks and the image files opened here");
		m_diskCombo.enableTooltip();
		m_diskCombo.setItemCallback([this](const ContextMenu::Entry& entry) { select(entry.id); });
		addWidget(m_openButton);
		place(m_openButton, 1, 0, 1, 0, -468, 6, -384, 34);
		setup_button(m_openButton, "Open...", "Opens a disk image file (not connected to the machine)");
		m_openButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { open_file(); });
		addWidget(m_closeButton);
		place(m_closeButton, 1, 0, 1, 0, -378, 6, -306, 34);
		setup_button(m_closeButton, "Close", "Closes the image file (the machine's disks stay)");
		m_closeButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { close_file(); });
		addWidget(m_newSizeTitle);
		place(m_newSizeTitle, 1, 0, 1, 0, -312, 0, -254, 28);
		setup_title(m_newSizeTitle, "Size");
		addWidget(m_newSizeInput);
		place(m_newSizeInput, 1, 0, 1, 0, -252, 6, -168, 34);
		m_newSizeInput.setFontSize(14);
		m_newSizeInput.setText("4M");
		m_newSizeInput.setTooltipText("Size of a new disk: 4M, 512K, 0x400000, 4194304...");
		m_newSizeInput.enableTooltip();
		m_newSizeInput.setCommitCallback([](FieldInput&) {  });
		addWidget(m_newButton);
		place(m_newButton, 1, 0, 1, 0, -162, 6, -8, 34);
		setup_button(m_newButton, "New disk...", "Creates a new disk image file of that size (zeros) and opens it");
		m_newButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_disk(); });

		// Row 2: saving, loading a binary, the partitions.
		addWidget(m_saveButton);
		place(m_saveButton, 0, 0, 0, 0, 8, 40, 88, 68);
		setup_button(m_saveButton, "Save", "Writes the changed bytes to the disk");
		m_saveButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { save(); });
		addWidget(m_reloadButton);
		place(m_reloadButton, 0, 0, 0, 0, 94, 40, 174, 68);
		setup_button(m_reloadButton, "Reload", "Reads the disk again (drops the changes)");
		m_reloadButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { reload(); });
		addWidget(m_loadAddressTitle);
		place(m_loadAddressTitle, 0, 0, 0, 0, 196, 40, 256, 68);
		setup_title(m_loadAddressTitle, "Load at");
		addWidget(m_loadAddressInput);
		place(m_loadAddressInput, 0, 0, 0, 0, 260, 40, 380, 68);
		m_loadAddressInput.setFontSize(14);
		m_loadAddressInput.setText("0x00000000");
		m_loadAddressInput.setTooltipText("Disk address for 'Load binary': 0x18000, 98304...");
		m_loadAddressInput.enableTooltip();
		m_loadAddressInput.setCommitCallback([](FieldInput&) {  });
		addWidget(m_loadButton);
		place(m_loadButton, 0, 0, 0, 0, 386, 40, 516, 68);
		setup_button(m_loadButton, "Load binary...", "Copies a file to the disk at the address (then Save writes it)");
		m_loadButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { load_binary(); });
		addWidget(m_partitionsButton);
		place(m_partitionsButton, 1, 0, 1, 0, -162, 40, -8, 68);
		setup_button(m_partitionsButton, "Partitions...", "Opens the partition editor for this disk");
		m_partitionsButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) {
			if (current() != nullptr)
				m_partitionManager.openFor(current());
		});

		addWidget(m_infoLine);
		place(m_infoLine, 0, 0, 1, 0, 8, 74, -8, 96);
		addWidget(m_statusLine);
		place(m_statusLine, 0, 0, 1, 0, 8, 98, -8, 120);
		addWidget(m_hex);
		place(m_hex, 0, 0, 1, 1, 8, 124, -8, -8);
		m_hex.setBigEndian(true);
		m_hex.setWriteCallback([this](ogfx::gui::StreamIndex index, u64 value, u8 size) {
			DiskDocument* document = current();
			if (document == nullptr)
				return false;
			document->markChanged(index, size);
			update_info();
			update_combo();
			return true;
		});

		m_partitionManager.build();
		m_built = true;
		select(-1);
	}

	void DiskManagerWindow::start(void)
	{
		add_machine_drives();
		select(m_documents.empty() ? -1 : 0);
	}

	void DiskManagerWindow::add_machine_drives(void)
	{
		std::map<i32, hw::VirtualHardDrive*> drives;
		for (auto& [id, drive] : DragonRuntime::vDisks)
			drives[id] = &drive;
		for (auto& [id, drive] : drives)
		{
			const auto it = DragonRuntime::machine_config.vdisk_paths.find(id);
			const String path = (it != DragonRuntime::machine_config.vdisk_paths.end() ? it->second : String("disk ").add(id));
			String error = "";
			auto document = DiskDocument::openDrive(id, *drive, path, error);
			if (document == nullptr)
			{
				set_status(error, ErrorColor);
				continue;
			}
			m_documents.push_back(std::move(document));
		}
	}

	DiskDocument* DiskManagerWindow::current(void)
	{
		if (m_current < 0 || m_current >= cast<i32>(m_documents.size()))
			return nullptr;
		return m_documents[m_current].get();
	}

	void DiskManagerWindow::update_combo(void)
	{
		m_diskCombo.clearMenuItems();
		for (size_t i = 0; i < m_documents.size(); i++)
		{
			const DiskDocument& document = *m_documents[i];
			String text = (document.isMachineDrive() ? String("Disk ").add(document.getDriveID()).add(": ") : String("File: "));
			text.add(document.getName()).add("  (").add(storage::formatSize(document.getSize())).add(")");
			if (document.getChangedCount() > 0)
				text.add("  *");
			// Ids restart at 0 after a clear: they're the documents' indices.
			m_diskCombo.addMenuItem(text);
		}
		if (m_current >= 0)
			m_diskCombo.setSelectedItem(m_current);
	}

	void DiskManagerWindow::select(i32 index)
	{
		m_current = (index >= 0 && index < cast<i32>(m_documents.size()) ? index : -1);
		show_document(false);
		update_combo();
		m_partitionManager.documentChanged(current());
	}

	void DiskManagerWindow::show_document(bool keepView)
	{
		DiskDocument* document = current();
		m_hex.clearHighlights();
		if (document == nullptr)
		{
			m_hex.setData(std::span<i8>(), 0, 16, 0, -1, 1, "");
			update_info();
			return;
		}
		auto& buffer = document->getBuffer();
		const std::span<i8> data(reinterpret_cast<i8*>(buffer.data()), buffer.size());
		if (keepView)
			m_hex.updateData(data);
		else
			m_hex.setData(data, 0, 16, 0, -1, 1, document->getName());
		// The table's areas and partitions.
		storage::Disk& disk = document->getBufferDisk();
		const storage::PartitionScheme* scheme = storage::PartitionSchemes::detect(disk);
		if (scheme != nullptr)
		{
			for (const auto& area : scheme->getReservedAreas(disk.getSize()))
				m_hex.addHighlight(area.start, cast<u32>(area.size), Color(150, 150, 150, 35), area.name);
			storage::tPartitionTable table;
			String error = "";
			if (scheme->read(disk, table, error))
			{
				for (size_t i = 0; i < table.partitions.size(); i++)
				{
					const auto& part = table.partitions[i];
					if (part.size == 0 || part.start >= disk.getSize())
						continue;
					const Color c = DiskMapView::partitionColor(cast<i32>(i));
					m_hex.addHighlight(part.start, cast<u32>(std::min<u64>(part.size, disk.getSize() - part.start)), Color(c.r, c.g, c.b, 40),
									   String("Partition ").add(cast<u64>(i + 1)).add(part.label == "" ? String("") : String(": ").add(part.label)));
				}
			}
		}
		update_info();
	}

	void DiskManagerWindow::update_info(void)
	{
		DiskDocument* document = current();
		m_saveButton.enable(document != nullptr && document->getChangedCount() > 0);
		m_reloadButton.enable(document != nullptr);
		m_loadButton.enable(document != nullptr);
		m_partitionsButton.enable(document != nullptr);
		m_closeButton.enable(document != nullptr && !document->isMachineDrive());
		if (document == nullptr)
		{
			m_infoLine.setMessage("No disk: open an image file or create a new one.", DimColor);
			return;
		}
		String info = String(document->getPath()).add("  -  ").add(document->getSize()).add(" bytes (").add(storage::formatSize(document->getSize())).add(")  -  ");
		storage::Disk& disk = document->getBufferDisk();
		const storage::PartitionScheme* scheme = storage::PartitionSchemes::detect(disk);
		storage::tPartitionTable table;
		String error = "";
		if (scheme == nullptr)
			info.add("no partition table");
		else if (scheme->read(disk, table, error))
			info.add(scheme->getName()).add(" ").add(table.version).add(", ").add(cast<u64>(table.partitions.size())).add(table.partitions.size() == 1 ? " partition" : " partitions");
		else
			info.add(scheme->getName()).add(": ").add(error);
		if (document->getChangedCount() > 0)
			info.add("  -  ").add(document->getChangedCount()).add(" bytes not saved");
		m_infoLine.setMessage(info, document->getChangedCount() > 0 ? WarningColor : DebugListView::TextColor);
	}

	void DiskManagerWindow::set_status(const String& text, const Color& color)
	{
		m_statusLine.setMessage(text, color);
		if (color.r == ErrorColor.r && color.g == ErrorColor.g && color.b == ErrorColor.b)
			m_debugger.console().fg(ostd::ConsoleColors::Red).p("Disk Manager: ").p(text).reset().nl();
	}

	String DiskManagerWindow::default_folder(void) const
	{
		std::error_code ec;
		return String(std::filesystem::current_path(ec).string());
	}

	void DiskManagerWindow::open_file(void)
	{
		const String path = getWindow().showOpenFileDialog({ { "Dragon disk images", { "drd", "dr" } }, { "All files", { "*" } } });
		if (path == "")
			return;
		// Already open (one of the machine's disks too)?
		std::error_code ec;
		const auto canonical = std::filesystem::weakly_canonical(path.cpp_str(), ec);
		for (size_t i = 0; i < m_documents.size(); i++)
		{
			std::error_code ec2;
			if (!ec && std::filesystem::weakly_canonical(m_documents[i]->getPath().cpp_str(), ec2) == canonical && !ec2)
			{
				select(cast<i32>(i));
				set_status(String(m_documents[i]->isMachineDrive() ? "It's one of the machine's disks: " : "Already open: ").add(path), DebugListView::TextColor);
				return;
			}
		}
		String error = "";
		auto document = DiskDocument::openFile(path, error);
		if (document == nullptr)
		{
			set_status(error, ErrorColor);
			return;
		}
		m_documents.push_back(std::move(document));
		select(cast<i32>(m_documents.size()) - 1);
		set_status(String("Opened ").add(path), SuccessColor);
	}

	void DiskManagerWindow::new_disk(void)
	{
		u64 size = 0;
		if (!storage::parseSize(m_newSizeInput.getText(), size) || size == 0)
		{
			set_status(String("Invalid size: '").add(m_newSizeInput.getText()).add("' (4M, 512K, 0x400000, 4194304...)."), ErrorColor);
			return;
		}
		if (size > 0x100000000ull)
		{
			set_status("Too big: the machine's disks are at most 4 GiB.", ErrorColor);
			return;
		}
		const String path = getWindow().showSaveFileDialog({ { "Dragon disk images", { "drd" } } });
		if (path == "")
			return;
		String error = "";
		if (!storage::createDiskFile(path, size, error))
		{
			set_status(error, ErrorColor);
			return;
		}
		auto document = DiskDocument::openFile(path, error);
		if (document == nullptr)
		{
			set_status(error, ErrorColor);
			return;
		}
		m_documents.push_back(std::move(document));
		select(cast<i32>(m_documents.size()) - 1);
		set_status(String("Created ").add(path).add(" (").add(storage::formatSize(size)).add("): use Partitions to give it a table."), SuccessColor);
	}

	void DiskManagerWindow::close_file(void)
	{
		DiskDocument* document = current();
		if (document == nullptr || document->isMachineDrive())
			return;
		const String name = document->getName();
		const bool changed = document->getChangedCount() > 0;
		m_documents.erase(m_documents.begin() + m_current);
		select(std::min(m_current, cast<i32>(m_documents.size()) - 1));
		set_status(String("Closed ").add(name).add(changed ? " (the changes weren't saved)" : ""), changed ? WarningColor : DebugListView::TextColor);
	}

	void DiskManagerWindow::save(void)
	{
		DiskDocument* document = current();
		if (document == nullptr || document->getChangedCount() == 0)
			return;
		const u64 count = document->getChangedCount();
		String error = "";
		const bool ok = document->save(error);
		if (document->isMachineDrive())
			m_debugger.commitEdit();
		if (!ok)
			set_status(String("Save failed: ").add(error), ErrorColor);
		else
			set_status(String("Saved ").add(count).add(count == 1 ? " byte to " : " bytes to ").add(document->getName()).add(document->isMachineDrive() ? " (an edit in the history: Step back undoes it)" : ""), SuccessColor);
		show_document(true);
		update_combo();
	}

	void DiskManagerWindow::reload(void)
	{
		DiskDocument* document = current();
		if (document == nullptr)
			return;
		const u64 dropped = document->getChangedCount();
		String error = "";
		if (!document->reload(error))
			set_status(error, ErrorColor);
		else
			set_status(dropped > 0 ? String("Reloaded: ").add(dropped).add(" changed bytes dropped.") : String("Reloaded."), DebugListView::TextColor);
		show_document(true);
		update_combo();
		m_partitionManager.documentChanged(nullptr);
		m_partitionManager.documentChanged(document);
	}

	void DiskManagerWindow::load_binary(void)
	{
		DiskDocument* document = current();
		if (document == nullptr)
			return;
		u64 address = 0;
		if (!storage::parseSize(m_loadAddressInput.getText(), address))
		{
			set_status(String("Invalid address: '").add(m_loadAddressInput.getText()).add("' (0x18000, 98304...)."), ErrorColor);
			return;
		}
		const String path = getWindow().showOpenFileDialog({ { "Binaries", { "bin" } }, { "All files", { "*" } } });
		if (path == "")
			return;
		u64 size = 0;
		String error = "";
		// Into the copy: the bytes are marked as changed, Save writes them.
		if (!storage::loadBinary(document->getBufferDisk(), path, address, size, error))
		{
			set_status(error, ErrorColor);
			return;
		}
		show_document(true);
		m_hex.jumpTo(address);
		update_combo();
		set_status(String("Loaded ").add(std::filesystem::path(path.cpp_str()).filename().string()).add(" (").add(storage::formatSize(size)).add(") at ").add(String::getHexStr(address, true, 4)).add(": Save writes it to the disk."), WarningColor);
	}

	void DiskManagerWindow::tableWritten(DiskDocument& document)
	{
		if (document.isMachineDrive())
			m_debugger.commitEdit();
		if (&document == current())
			show_document(true);
		update_combo();
		set_status(String("Partition table written to ").add(document.getName()).add(document.isMachineDrive() ? " (an edit in the history)" : ""), SuccessColor);
	}

	void DiskManagerWindow::syncWithMachine(void)
	{
		if (!m_built || !isVisible())
			return;
		DiskDocument* document = current();
		if (document != nullptr && document->sync())
			show_document(true);
	}
}
