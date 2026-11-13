#include "PartitionManager.hpp"
#include "DiskManager.hpp"
#include "../storage/FileSystem.hpp"
#include <algorithm>

namespace dragon
{
	using namespace ogfx::gui;
	using namespace disk_ui;

	static u64 max_dfs_size(void) { return 0xFFFFFFFFull; }

	static String percent(f64 fraction)
	{
		return String("").add(cast<i32>(std::round(fraction * 100.0))).add("%");
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
			m_segments.push_back({ eKind::Free, 0, diskSize, -1, "No partition table", "", -1 });
			return;
		}
		stdvec<Segment> items;
		for (const auto& area : scheme->getReservedAreas(diskSize))
			items.push_back({ eKind::Reserved, area.start, area.size, -1, area.name, "", -1 });
		for (size_t i = 0; i < table->partitions.size(); i++)
		{
			const auto& part = table->partitions[i];
			items.push_back({ eKind::Partition, part.start, part.size, cast<i32>(i), part.label, "", -1 });
		}
		std::stable_sort(items.begin(), items.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
		const u64 first = scheme->getFirstUsableAddress(diskSize);
		const u64 last = scheme->getLastUsableAddress(diskSize);
		u64 cursor = 0;
		auto l_free = [&](u64 from, u64 to) {
			from = std::max(from, first);
			to = std::min(to, last);
			if (to > from)
				m_segments.push_back({ eKind::Free, from, to - from, -1, "Unallocated", "", -1 });
		};
		for (const auto& item : items)
		{
			l_free(cursor, item.start);
			m_segments.push_back(item);
			cursor = std::max(cursor, item.start + item.size);
		}
		l_free(cursor, last);
	}

	void DiskMapView::setPartitionDetail(i32 partition, const String& detail, f32 used)
	{
		for (auto& segment : m_segments)
		{
			if (segment.kind == Segment::eKind::Partition && segment.partition == partition)
			{
				segment.detail = detail;
				segment.used = used;
			}
		}
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
			const bool hovered = (cast<i32>(i) == m_hovered);
			Color text = DimColor;
			String name = segment.name;
			String second = storage::formatSize(segment.size);
			const f32 stripe = 6;
			if (segment.kind == eKind::Free)
			{
				gfx.fillRect(r, hovered ? Color(40, 44, 40) : Color(28, 31, 28));
				// Diagonal hatching.
				gfx.pushClippingRect(r, true);
				for (f32 x = r.x - r.h; x < r.x + r.w; x += 10)
					gfx.drawLine({ Vec2 { x, r.y + r.h }, Vec2 { x + r.h, r.y } }, Color(40, 45, 40), 1);
				gfx.popClippingRect();
			}
			else if (segment.kind == eKind::Reserved)
			{
				gfx.fillRect(r, hovered ? Color(70, 72, 70) : Color(56, 58, 56));
				text = { 175, 180, 175 };
			}
			else
			{
				const Color c = partitionColor(segment.partition);
				const Color body = hovered ? Color(cast<u8>(c.r * 0.55f), cast<u8>(c.g * 0.55f), cast<u8>(c.b * 0.55f)) : Color(cast<u8>(c.r * 0.42f), cast<u8>(c.g * 0.42f), cast<u8>(c.b * 0.42f));
				gfx.fillRect({ r.x, r.y + stripe, r.w, r.h - stripe }, body);
				// The used part of the file system, as in GParted.
				if (segment.used >= 0)
				{
					const f32 used = std::clamp(segment.used, 0.0f, 1.0f);
					gfx.fillRect({ r.x, r.y + stripe, std::max(2.0f, r.w * used), r.h - stripe }, Color(cast<u8>(c.r * 0.7f), cast<u8>(c.g * 0.7f), cast<u8>(c.b * 0.7f)));
				}
				gfx.fillRect({ r.x, r.y, r.w, stripe }, c);
				text = { 240, 240, 240 };
				name = String("").add(segment.partition + 1).add(segment.name == "" ? String("") : String("  ").add(segment.name));
				if (segment.detail != "")
					second = String(segment.detail).add("  ").add(second);
			}
			const bool selected = (segment.kind == eKind::Partition && segment.partition == m_selectedPartition) ||
								  (segment.kind == eKind::Free && m_selectedFree >= 0 && cast<u64>(m_selectedFree) == segment.start);
			gfx.drawRect(r, selected ? Color(245, 245, 245) : Color(62, 70, 62), selected ? 2 : 1);
			gfx.pushClippingRect({ r.x + 3, r.y + 2, std::max(0.0f, r.w - 6), r.h - 4 }, true);
			const f32 y = r.y + stripe * 0.5f + (r.h - stripe * 0.5f) * 0.5f - lineHeight;
			gfx.drawString(name, { r.x + 6, y }, text, 13);
			gfx.drawString(second, { r.x + 6, y + lineHeight }, segment.kind == eKind::Partition ? Color(200, 205, 200) : text, 13);
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
		setFontSize(16);
		setColumns({ { "#", 5 }, { "Label", 20 }, { "File system", 13 }, { "Start", 12 }, { "End", 12 }, { "Size", 11 }, { "Used", 29 }, { "Flags", 8 } });
	}

	i32 PartitionListView::getRowCount(void) const
	{
		return m_table == nullptr ? 0 : cast<i32>(m_table->partitions.size());
	}

	String PartitionListView::getEmptyText(void) const
	{
		if (m_scheme == nullptr)
			return "No partition table: pick a format and click 'New table'.";
		return "No partitions: click an unallocated area, then 'New partition'.";
	}

	void PartitionListView::drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds)
	{
		const storage::tPartition& part = m_table->partitions[row];
		const RowInfo* info = (m_rows != nullptr && row < cast<i32>(m_rows->size()) ? &(*m_rows)[row] : nullptr);
		switch (column)
		{
			case 0:
				gfx.fillRect({ bounds.x, bounds.y + bounds.h * 0.5f - 5, 10, 10 }, DiskMapView::partitionColor(row));
				text(gfx, String("").add(row + 1).add(info != nullptr && info->pending ? "*" : ""), bounds.x + 16, bounds.y, info != nullptr && info->pending ? WarningColor : TextColor);
				break;
			case 1: text(gfx, part.label == "" ? String("<no label>") : part.label, bounds.x, bounds.y, part.label == "" ? DimTextColor : Color(110, 220, 230)); break;
			case 2: if (info != nullptr) text(gfx, info->fileSystem, bounds.x, bounds.y, info->color); break;
			case 3: text(gfx, String::getHexStr(part.start, true, 4), bounds.x, bounds.y, Color(235, 215, 90)); break;
			case 4: text(gfx, String::getHexStr(part.end(), true, 4), bounds.x, bounds.y, Color(235, 215, 90)); break;
			case 5: text(gfx, storage::formatSize(part.size), bounds.x, bounds.y, TextColor); break;
			case 6: if (info != nullptr) text(gfx, info->used, bounds.x, bounds.y, TextColor); break;
			case 7: text(gfx, m_scheme == nullptr ? String("") : m_scheme->flagsToString(part.flags), bounds.x, bounds.y, Color(240, 170, 60)); break;
			default: break;
		}
	}

	void PartitionListView::onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick)
	{
		if (row >= getRowCount())
			return;
		if (m_onSelect)
			m_onSelect(row);
		if (doubleClick && m_onActivate)
			m_onActivate(row);
	}




	//DfsFormatWindow
	DfsFormatWindow::DfsFormatWindow(Window& window, PartitionManagerWindow& manager) : SubWindow(window, "Format as DFS"), m_manager(manager)
	{
		setBounds({ 420, 200, 520, 420 });
		setButtons(SubWindow::Buttons::Close);
	}

	void DfsFormatWindow::build(void)
	{
		addWidget(m_targetLine);
		place(m_targetLine, 0, 0, 1, 0, 16, 12, -16, 52);
		auto l_row = [this](FieldLabel& title, const String& text, Widget& input, f32 y, const String& tooltip) {
			addWidget(title);
			setupTitle(title, text);
			place(title, 0, 0, 0, 0, 16, y, 150, y + 28);
			addWidget(input);
			input.setFontSize(14);
			input.setTooltipText(tooltip);
			input.enableTooltip();
			place(input, 0, 0, 1, 0, 156, y, -16, y + 28);
		};
		l_row(m_labelTitle, "Label", m_labelInput, 62, "The volume's label (up to 16 characters)");
		m_labelInput.setCommitCallback([this](FieldInput&) { update_preview(); });
		l_row(m_blockTitle, "Block size", m_blockCombo, 98, "Bigger blocks: less overhead, more space lost in small files");
		for (const char* size : { "512 bytes", "1 KiB", "2 KiB", "4 KiB" })
			m_blockCombo.addMenuItem(size);
		m_blockCombo.setSelectedItem(3);
		m_blockCombo.setItemCallback([this](const ContextMenu::Entry&) { update_preview(); });
		l_row(m_inodeTitle, "Bytes per inode", m_inodeCombo, 134, "One inode (file or directory) per this many bytes: the most files the volume can hold");
		for (const char* size : { "4 KiB", "8 KiB", "16 KiB", "32 KiB", "64 KiB" })
			m_inodeCombo.addMenuItem(size);
		m_inodeCombo.setSelectedItem(2);
		m_inodeCombo.setItemCallback([this](const ContextMenu::Entry&) { update_preview(); });

		addWidget(m_preview);
		place(m_preview, 0, 0, 1, 1, 16, 176, -16, -96);
		m_preview.setNameWidth(150);
		addWidget(m_warningLine);
		place(m_warningLine, 0, 1, 1, 1, 16, -90, -16, -46);
		addWidget(m_cancelButton);
		place(m_cancelButton, 1, 1, 1, 1, -252, -40, -136, -12);
		setupButton(m_cancelButton, "Cancel", "");
		m_cancelButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { close(); });
		addWidget(m_formatButton);
		place(m_formatButton, 1, 1, 1, 1, -128, -40, -16, -12);
		setupButton(m_formatButton, "Format", "Writes a new, empty file system (click twice)");
		m_formatButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { format(); });
	}

	void DfsFormatWindow::openFor(DiskDocument* document, i32 partition, const storage::tPartition& part, const PartitionContent& content)
	{
		m_document = document;
		m_partition = partition;
		m_part = part;
		m_confirmUntil = 0;
		m_formatButton.setText("Format");
		m_labelInput.setText(content.dfs && content.label != "" ? content.label : String(part.label).new_trim());
		m_targetLine.setMessage(String("Partition ").add(partition + 1).add(part.label == "" ? String("") : String(" (").add(part.label).add(")"))
								.add("  -  ").add(storage::formatSize(part.size)).add(" at ").add(String::getHexStr(part.start, true, 4))
								.add("\nNow: ").add(content.fileSystem).add(content.dfs && content.readable ? String(", ").add(storage::formatSize(content.usedBytes)).add(" used") : String("")),
								DebugListView::TextColor);
		update_preview();
		open();
		fitSubWindow(*this, 560, 500, m_placed);
	}

	u32 DfsFormatWindow::block_size(void) const
	{
		return 512u << std::clamp(m_blockCombo.getSelectedEntry().id, 0, 3);
	}

	u32 DfsFormatWindow::bytes_per_inode(void) const
	{
		return 4096u << std::clamp(m_inodeCombo.getSelectedEntry().id, 0, 4);
	}

	void DfsFormatWindow::update_preview(void)
	{
		m_preview.clearRows();
		const u64 size = std::min<u64>(m_part.size, max_dfs_size());
		u32 log2 = 0;
		while ((1u << log2) < block_size())
			log2++;
		dfs::Layout l;
		if (dfs::layout_from_params(&l, cast<u32>(size), log2, bytes_per_inode()) < 0)
		{
			m_preview.addRow("Layout", "The partition is too small for these settings", ErrorColor);
			m_formatButton.enable(false);
			return;
		}
		const u64 bs = l.block_size;
		const u64 meta = cast<u64>(l.first_data_block) * bs;
		m_preview.addRow("File system size", String(storage::formatSize(cast<u64>(l.total_blocks) * bs)).add(" (").add(l.total_blocks).add(" blocks)"));
		m_preview.addRow("Usable space", storage::formatSize((cast<u64>(l.total_blocks) - l.first_data_block - 1) * bs));
		m_preview.addRow("Metadata", String(storage::formatSize(meta)).add("  (bitmaps ").add(l.inode_bitmap_len + l.block_bitmap_len).add(" blocks, inode table ").add(l.inode_table_len).add(")"), DimColor);
		m_preview.addRow("Inodes", String("").add(l.inode_count).add("  (up to ").add(l.inode_count - dfs::FirstInode).add(" files and directories)"));
		m_preview.addRow("Largest file", storage::formatSize(std::min<u64>(0xFFFFFFFFull, (cast<u64>(dfs::DirectPointers) + l.pointers_per_block + cast<u64>(l.pointers_per_block) * l.pointers_per_block) * bs)), DimColor);
		if (size < m_part.size)
			m_preview.addRow("Note", "DFS volumes are at most 4 GiB: the rest stays unused", WarningColor);
		const String label = m_labelInput.getText().new_trim();
		if (label.len() > 16)
			m_warningLine.setMessage("The label is too long (16 characters at most).", ErrorColor);
		else
			m_warningLine.setMessage("Everything on the partition is lost.\n(The DPT boot header in its first 32 bytes stays.)", WarningColor);
		m_formatButton.enable(label.len() <= 16 && m_document != nullptr);
	}

	void DfsFormatWindow::format(void)
	{
		if (m_document == nullptr)
			return;
		const u64 now = nowMs();
		if (now > m_confirmUntil)
		{
			m_confirmUntil = now + ConfirmMs;
			m_formatButton.setText("Confirm");
			return;
		}
		m_confirmUntil = 0;
		m_formatButton.setText("Format");
		const String label = m_labelInput.getText().new_trim();
		// On a copy, then what mkfs wrote to the disk.
		DiskDocument& document = *m_document;
		stdvec<u8> scratch = document.getBuffer();
		storage::MemoryDisk scratchDisk { std::span<u8>(scratch) };
		RecordingDisk recording(scratchDisk);
		storage::DiskRegion region(recording, m_part.start, std::min<u64>(m_part.size, max_dfs_size()));
		const dfs::i32 r = storage::DfsVolume::format(region, block_size(), bytes_per_inode(), label);
		String error = "";
		if (r < 0)
		{
			m_warningLine.setMessage(String("Format failed: ").add(storage::DfsVolume::errorText(r)), ErrorColor);
			return;
		}
		if (!m_manager.commitScratch(document, scratch, recording.getRanges(), error))
		{
			m_warningLine.setMessage(String("Unable to write to the disk: ").add(error), ErrorColor);
			return;
		}
		m_manager.getDiskManager().diskWritten(document, String("Partition ").add(m_partition + 1).add(" formatted as DFS"));
		m_manager.contentChanged(document);
		close();
	}

	void DfsFormatWindow::onUpdate(void)
	{
		SubWindow::onUpdate();
		if (m_confirmUntil != 0 && nowMs() > m_confirmUntil)
		{
			m_confirmUntil = 0;
			m_formatButton.setText("Format");
		}
	}




	//PartitionManagerWindow
	PartitionManagerWindow::PartitionManagerWindow(Window& window, DiskManagerWindow& diskManager)
		: SubWindow(window, "Partition Manager"), m_diskManager(diskManager), m_formatWindow(window, *this), m_browser(window, *this)
	{
		setBounds({ 260, 70, 1080, 780 });
		setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
	}

	void PartitionManagerWindow::build(void)
	{
		// Header: the disk, the table format.
		addWidget(m_infoLine);
		place(m_infoLine, 0, 0, 1, 0, 10, 8, -340, 46);
		addWidget(m_schemeCombo);
		place(m_schemeCombo, 1, 0, 1, 0, -332, 10, -200, 38);
		for (const auto& scheme : storage::PartitionSchemes::list())
			m_schemeCombo.addMenuItem(scheme->getName());
		m_schemeCombo.setItemCallback([](const ContextMenu::Entry&) {  });
		m_schemeCombo.setTooltipText("The partition table format for 'New table'");
		m_schemeCombo.enableTooltip();
		addWidget(m_newTableButton);
		place(m_newTableButton, 1, 0, 1, 0, -194, 10, -10, 38);
		setupButton(m_newTableButton, "New table", "Starts an empty table of the chosen format (written by Apply)");
		m_newTableButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_table(); });

		addWidget(m_map);
		place(m_map, 0, 0, 1, 0, 10, 50, -10, 128);
		m_map.setSelectCallback([this](const DiskMapView::Segment& segment) {
			if (segment.kind == DiskMapView::Segment::eKind::Partition)
				select_partition(segment.partition);
			else if (segment.kind == DiskMapView::Segment::eKind::Free)
			{
				select_partition(-1);
				m_selectedFree = cast<i64>(segment.start);
				m_map.setSelectedFree(segment.start);
				refresh_view();
			}
		});
		addWidget(m_list);
		place(m_list, 0, 0, 1, 1, 10, 136, -10, GroupTop - 8);
		m_list.setSelectCallback([this](i32 row) { select_partition(row); });
		m_list.setActivateCallback([this](i32 row) {
			const PartitionContent* content = content_of(row);
			if (content != nullptr && content->dfs && content->readable && is_on_disk(row))
				open_browser();
		});

		// The Partition group.
		const f32 top = GroupTop;
		const f32 bottom = -176;
		addWidget(m_partFrame);
		place(m_partFrame, 0, 1, 0.5f, 1, 10, top, -5, bottom);
		m_partFrame.setTitle("Partition");
		auto l_field = [this](FieldLabel& title, const String& text, FieldInput& input, f32 y, f32 inputRight, f32 inputRightAnchor, const String& tooltip) {
			addWidget(title);
			setupTitle(title, text);
			place(title, 0, 1, 0, 1, 22, y, 82, y + 28);
			addWidget(input);
			input.setFontSize(14);
			input.setTooltipText(tooltip);
			input.enableTooltip();
			place(input, 0, 1, inputRightAnchor, 1, 86, y, inputRight, y + 28);
			input.setCommitCallback([this](FieldInput& field) { commit_field(field); });
		};
		l_field(m_labelTitle, "Label", m_labelInput, top + 38, -18, 0.5f, "The partition's label (Return applies it)");
		l_field(m_startTitle, "Start", m_startInput, top + 74, 260, 0, "Start address: 0x400200, 4M, 4096K... (Return applies it)");
		l_field(m_sizeTitle, "Size", m_sizeInput, top + 110, 260, 0, "Size: 0x1400, 5120, 5K, 1.5M... (Return applies it)");
		addWidget(m_growButton);
		place(m_growButton, 0, 1, 0, 1, 266, top + 110, 330, top + 138);
		setupButton(m_growButton, "Max", "Grows the partition up to the next one (or the end of the disk)");
		m_growButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { grow_partition(); });
		addWidget(m_flagsTitle);
		setupTitle(m_flagsTitle, "Flags");
		place(m_flagsTitle, 0, 1, 0, 1, 282, top + 74, 334, top + 100);
		addWidget(m_partInfo);
		place(m_partInfo, 0, 1, 0.5f, 1, 22, top + 148, -18, top + 190);
		m_partInfo.setTextSize(13);
		addWidget(m_newButton);
		place(m_newButton, 0, 1, 0, 1, 22, bottom - 40, 152, bottom - 12);
		setupButton(m_newButton, "New partition", "A partition in the selected unallocated area (or the largest one)");
		m_newButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_partition(); });
		addWidget(m_deleteButton);
		place(m_deleteButton, 0, 1, 0, 1, 158, bottom - 40, 252, bottom - 12);
		setupButton(m_deleteButton, "Delete", "Removes the selected partition from the table (its data stays on the disk)");
		m_deleteButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { delete_partition(); });

		// The File system group.
		addWidget(m_fsFrame);
		place(m_fsFrame, 0.5f, 1, 1, 1, 5, top, -10, bottom);
		m_fsFrame.setTitle("File system");
		addWidget(m_usageBar);
		place(m_usageBar, 0.5f, 1, 1, 1, 17, top + 36, -22, top + 60);
		addWidget(m_fsInfo);
		place(m_fsInfo, 0.5f, 1, 1, 1, 17, top + 66, -22, bottom - 46);
		m_fsInfo.setNameWidth(112);
		addWidget(m_formatButton);
		place(m_formatButton, 0.5f, 1, 0.5f, 1, 17, bottom - 40, 137, bottom - 12);
		setupButton(m_formatButton, "Format...", "Formats the partition as DFS (the partition must be on the disk: Apply the table first)");
		m_formatButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { open_format(); });
		addWidget(m_checkButton);
		place(m_checkButton, 0.5f, 1, 0.5f, 1, 143, bottom - 40, 233, bottom - 12);
		setupButton(m_checkButton, "Check", "Checks the file system (fsck, read only)");
		m_checkButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { check_file_system(); });
		addWidget(m_browseButton);
		place(m_browseButton, 0.5f, 1, 0.5f, 1, 239, bottom - 40, 359, bottom - 12);
		setupButton(m_browseButton, "Browse...", "Opens the file system's files (import, extract, rename, delete)");
		m_browseButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { open_browser(); });

		// Pending operations, Apply.
		addWidget(m_opsFrame);
		place(m_opsFrame, 0, 1, 1, 1, 10, bottom + 8, -10, -10);
		m_opsFrame.setTitle("Pending operations");
		addWidget(m_messageLine);
		place(m_messageLine, 0, 1, 1, 1, 20, bottom + 40, -300, bottom + 62);
		addWidget(m_opsList);
		place(m_opsList, 0, 1, 1, 1, 20, bottom + 64, -300, -16);
		m_opsList.setNameWidth(16);
		addWidget(m_contentsCheck);
		place(m_contentsCheck, 1, 1, 1, 1, -286, bottom + 40, -20, bottom + 64);
		m_contentsCheck.setText("Move and resize contents");
		m_contentsCheck.setTooltipText("A partition that moves takes its data along, a DFS partition that changes size has its file system resized (off: only the table is written)");
		m_contentsCheck.enableTooltip();
		m_contentsCheck.setChecked(true);
		m_contentsCheck.setStateChangedCallback([this](CheckBox&, bool checked) {
			m_moveContents = checked;
			refresh_view();
		});
		addWidget(m_revertButton);
		place(m_revertButton, 1, 1, 1, 1, -286, -48, -156, -20);
		setupButton(m_revertButton, "Revert", "Drops the pending changes (reads the table from the disk again)");
		m_revertButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) {
			m_note = "";
			load_table();
		});
		addWidget(m_applyButton);
		place(m_applyButton, 1, 1, 1, 1, -150, -48, -20, -20);
		setupButton(m_applyButton, "Apply", "Writes the table (and moves / resizes the contents) to the disk (click twice)");
		m_applyButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { apply(); });

		m_formatWindow.build();
		m_browser.build();
	}

	void PartitionManagerWindow::openFor(DiskDocument* document)
	{
		m_document = document;
		m_note = "";
		load_table();
		open();
		fitSubWindow(*this, 1080, 780, m_placed);
	}

	void PartitionManagerWindow::documentChanged(DiskDocument* document)
	{
		m_browser.documentChanged(document);
		if (m_formatWindow.isVisible() && document != m_document)
			m_formatWindow.close();
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

	void PartitionManagerWindow::contentChanged(DiskDocument& document)
	{
		if (&document != m_document)
			return;
		read_contents();
		refresh_view();
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
		m_applyButton.setText("Apply");
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
		m_origin.clear();
		for (size_t i = 0; i < m_table.partitions.size(); i++)
			m_origin.push_back(cast<i32>(i));
		read_contents();
		select_partition(m_table.partitions.empty() ? -1 : 0);
	}

	void PartitionManagerWindow::read_contents(void)
	{
		m_contents.clear();
		for (const auto& part : m_original.partitions)
		{
			PartitionContent c;
			if (m_document == nullptr || part.size == 0 || !m_document->getBufferDisk().contains(part.start, part.size))
			{
				m_contents.push_back(c);
				continue;
			}
			storage::DiskRegion region(m_document->getBufferDisk(), part.start, part.size);
			u8 head[2] = { 0, 0 };
			c.bootHeader = (part.size >= 2 && region.read(0, std::span<u8>(head, 2)) && head[0] == 0xF1 && head[1] == 0xC4);
			const storage::FileSystem* fileSystem = storage::FileSystems::detect(region);
			if (fileSystem != nullptr && fileSystem->getName() == "DFS")
			{
				c.dfs = true;
				c.fileSystem = "DFS";
				storage::DfsVolume vol(region);
				const dfs::i32 r = vol.mount(true);
				if (r < 0)
					c.error = storage::DfsVolume::errorText(r);
				else
				{
					c.readable = true;
					c.sb = vol.superblock();
					c.layout = vol.layout();
					c.label = vol.label();
					c.uuid = vol.uuid();
					const u64 bs = c.layout.block_size;
					c.fsSize = cast<u64>(c.layout.total_blocks) * bs;
					c.freeBytes = cast<u64>(c.sb.free_blocks) * bs;
					c.usedBytes = c.fsSize - c.freeBytes;
					dfs::ResizeInfo info;
					dfs::resize_plan(vol.get(), cast<u32>(std::min<u64>(part.size, max_dfs_size())), info);
					c.minSize = info.min_size;
					dfs::Inode kernel;
					if (vol.stat("/boot/kernel0", kernel) == dfs::Ok && (kernel.mode & dfs::ModeTypeMask) == dfs::ModeFile)
					{
						c.hasKernel = true;
						c.kernelSize = kernel.size;
					}
				}
			}
			else if (fileSystem != nullptr)
				c.fileSystem = fileSystem->getName();
			else
			{
				// Zeros at the start (64 KiB): empty.
				stdvec<u8> data(std::min<u64>(part.size, 65536), 0);
				const bool zeros = region.read(0, data) && std::all_of(data.begin(), data.end(), [](u8 b) { return b == 0; });
				c.fileSystem = (zeros ? "empty" : (c.bootHeader ? "raw (boot)" : "unknown"));
			}
			m_contents.push_back(c);
		}
	}

	const PartitionContent* PartitionManagerWindow::content_of(i32 index) const
	{
		if (index < 0 || index >= cast<i32>(m_origin.size()))
			return nullptr;
		const i32 origin = m_origin[index];
		if (origin < 0 || origin >= cast<i32>(m_contents.size()))
			return nullptr;
		return &m_contents[origin];
	}

	bool PartitionManagerWindow::is_on_disk(i32 index) const
	{
		if (!m_hasOriginal || index < 0 || index >= cast<i32>(m_origin.size()) || m_origin[index] < 0)
			return false;
		const auto& part = m_table.partitions[index];
		const auto& old = m_original.partitions[m_origin[index]];
		return part.start == old.start && part.size == old.size;
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

	stdvec<PartitionManagerWindow::Operation> PartitionManagerWindow::plan_operations(void) const
	{
		stdvec<Operation> ops;
		if (m_document == nullptr || m_scheme == nullptr)
			return ops;
		if (!m_hasOriginal)
		{
			ops.push_back({ String("Write a new ").add(m_scheme->getName()).add(" table (the old partitions' data stays on the disk)"), WarningColor, false });
			return ops;
		}
		// Partitions removed from the table.
		for (size_t o = 0; o < m_original.partitions.size(); o++)
		{
			if (std::find(m_origin.begin(), m_origin.end(), cast<i32>(o)) != m_origin.end())
				continue;
			const auto& old = m_original.partitions[o];
			ops.push_back({ String("Delete partition ").add(cast<u64>(o + 1)).add(old.label == "" ? String("") : String(" (").add(old.label).add(")"))
							.add(": its data stays on the disk"), DebugListView::TextColor, false });
		}
		for (size_t i = 0; i < m_table.partitions.size(); i++)
		{
			const auto& part = m_table.partitions[i];
			const String name = String("Partition ").add(cast<u64>(i + 1));
			const i32 origin = m_origin[i];
			if (origin < 0)
			{
				ops.push_back({ String("Create ").add(name).add(": ").add(storage::formatSize(part.size)).add(" at ").add(String::getHexStr(part.start, true, 4)).add(" (not formatted)"), DebugListView::TextColor, false });
				continue;
			}
			const auto& old = m_original.partitions[origin];
			const PartitionContent& c = m_contents[origin];
			if (part.label != old.label || part.flags != old.flags)
				ops.push_back({ String("Change ").add(name).add("'s ").add(part.label != old.label ? "label" : "flags").add(part.label != old.label && part.flags != old.flags ? " and flags" : ""), DebugListView::TextColor, false });
			const bool moved = part.start != old.start;
			const bool resized = part.size != old.size;
			if (!moved && !resized)
				continue;
			const bool hasData = (c.fileSystem != "empty" && c.fileSystem != "-");
			if (!m_moveContents)
			{
				ops.push_back({ String("Change ").add(name).add("'s ").add(moved && resized ? "start and size" : (moved ? "start" : "size")).add(" (only the table)"), DebugListView::TextColor, false });
				if (moved && hasData)
					ops.push_back({ String("  ").add(name).add("'s data stays at ").add(String::getHexStr(old.start, true, 4)).add(": the partition won't find it"), WarningColor, false });
				if (c.dfs && c.readable && part.size < c.fsSize)
					ops.push_back({ String("  ").add(name).add(" becomes smaller than its file system (").add(storage::formatSize(c.fsSize)).add("): it will be broken"), ErrorColor, false });
				continue;
			}
			// The file system first (shrinking), the data, the file system (growing).
			const u64 target = std::min<u64>(part.size, max_dfs_size());
			if (c.dfs && c.readable && target < c.fsSize)
			{
				if (target < c.minSize)
				{
					ops.push_back({ String(name).add(": DFS needs at least ").add(storage::formatSize(c.minSize)).add(" (").add(storage::formatSize(c.usedBytes)).add(" used)"), ErrorColor, true });
					continue;
				}
				storage::DiskRegion region(m_document->getBufferDisk(), old.start, old.size);
				storage::DfsVolume vol(region);
				dfs::ResizeInfo info;
				if (vol.mount(true) == dfs::Ok)
					dfs::resize_plan(vol.get(), cast<u32>(target), info);
				ops.push_back({ String("Shrink ").add(name).add("'s DFS from ").add(storage::formatSize(c.fsSize)).add(" to ").add(storage::formatSize(target))
								.add(info.moved_blocks > 0 ? String(" (").add(info.moved_blocks).add(" blocks move)") : String("")), DebugListView::TextColor, false });
			}
			if (moved)
			{
				const u64 count = std::min(old.size, part.size);
				ops.push_back({ String("Move ").add(name).add("'s data: ").add(storage::formatSize(count)).add(" from ").add(String::getHexStr(old.start, true, 4)).add(" to ").add(String::getHexStr(part.start, true, 4)), DebugListView::TextColor, false });
			}
			else if (resized && !c.dfs)
				ops.push_back({ String("Resize ").add(name).add(" to ").add(storage::formatSize(part.size)).add(part.size < old.size && hasData ? ": the data past the new end is lost" : ""),
								part.size < old.size && hasData ? WarningColor : DebugListView::TextColor, false });
			if (c.dfs && c.readable && target > c.fsSize)
				ops.push_back({ String("Grow ").add(name).add("'s DFS from ").add(storage::formatSize(c.fsSize)).add(" to ").add(storage::formatSize(target)), DebugListView::TextColor, false });
			if (c.dfs && !c.readable)
				ops.push_back({ String("  ").add(name).add("'s DFS can't be read (").add(c.error).add("): it isn't resized"), WarningColor, false });
		}
		return ops;
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

		// The rows: what each partition holds (what the disk has, for the ones on it).
		m_rows.clear();
		for (size_t i = 0; i < m_table.partitions.size(); i++)
		{
			PartitionListView::RowInfo row;
			const PartitionContent* c = content_of(cast<i32>(i));
			const i32 origin = (i < m_origin.size() ? m_origin[i] : -1);
			row.pending = (origin < 0 || !(m_table.partitions[i] == m_original.partitions[origin]));
			if (c == nullptr)
			{
				row.fileSystem = "new";
				row.color = WarningColor;
			}
			else
			{
				row.fileSystem = c->fileSystem;
				row.color = (c->dfs ? (c->readable ? SuccessColor : ErrorColor) : DebugListView::DimTextColor);
				if (c->dfs && c->readable)
				{
					row.used = String(storage::formatSize(c->usedBytes)).add(" / ").add(storage::formatSize(c->fsSize)).add("  (").add(percent(c->usedFraction())).add(")");
					m_map.setPartitionDetail(cast<i32>(i), String("DFS"), c->usedFraction());
				}
				else
					m_map.setPartitionDetail(cast<i32>(i), c->fileSystem == "-" ? String("") : c->fileSystem, -1);
			}
			m_rows.push_back(row);
		}
		m_list.setTable(m_scheme, table, &m_rows);
		m_list.setSelectedRow(m_selected);

		// Header
		if (m_document == nullptr)
			m_infoLine.setMessage("No disk: pick one in the Disk Manager.", DimColor);
		else
		{
			String info = String(m_document->isMachineDrive() ? String("Disk ").add(m_document->getDriveID()).add(":  ") : String("Image:  ")).add(m_document->getName());
			info.add("   ").add(storage::formatSize(diskSize));
			String second = "";
			if (m_scheme == nullptr)
				second = "No partition table";
			else
			{
				second = m_scheme->getName();
				if (m_hasOriginal && m_table.version != "")
					second.add(" ").add(m_table.version);
				second.add("  -  ").add(m_scheme->getDescription()).add("  -  ").add(cast<u64>(m_table.partitions.size())).add(" of ").add(m_scheme->getMaxPartitions()).add(" partitions");
			}
			m_infoLine.setMessage(info.add("\n").add(second), DebugListView::TextColor);
		}

		// Pending operations: the last action, the table's problems, what Apply does.
		const stdvec<String> errors = (m_scheme != nullptr ? m_scheme->validate(m_table, diskSize) : stdvec<String>());
		const stdvec<Operation> ops = (is_pending() ? plan_operations() : stdvec<Operation>());
		bool blocking = !errors.empty();
		m_opsList.clearRows();
		for (const auto& error : errors)
			m_opsList.addRow("!", error, ErrorColor);
		for (const auto& op : ops)
		{
			m_opsList.addRow(op.text.startsWith("  ") ? "" : "-", op.text.new_trim(), op.color);
			blocking = blocking || op.blocking;
		}
		if (m_note != "")
			m_messageLine.setMessage(m_note, m_noteColor);
		else if (!is_pending())
			m_messageLine.setMessage(m_document == nullptr ? String("") : String("No pending changes."), DimColor);
		else
			m_messageLine.setMessage(blocking ? String("Fix the problems below to Apply.") : String("Apply writes these changes to ").add(m_document->getName()).add(":"), blocking ? ErrorColor : WarningColor);
		m_opsFrame.setSubtitle(is_pending() ? String("").add(cast<u64>(ops.size())).add(ops.size() == 1 ? " change" : " changes") : String(""));

		const bool hasPart = (m_selected >= 0 && m_selected < cast<i32>(m_table.partitions.size()));
		m_labelInput.enable(hasPart);
		m_startInput.enable(hasPart);
		m_sizeInput.enable(hasPart);
		m_growButton.enable(hasPart);
		m_deleteButton.enable(hasPart);
		m_newButton.enable(m_scheme != nullptr && m_table.partitions.size() < m_scheme->getMaxPartitions());
		m_applyButton.enable(m_document != nullptr && m_scheme != nullptr && is_pending() && !blocking);
		m_revertButton.enable(m_document != nullptr && is_pending());
		m_newTableButton.enable(m_document != nullptr);
		for (auto& check : m_flagChecks)
			check.enable(hasPart);
		if (hasPart)
		{
			const auto& part = m_table.partitions[m_selected];
			m_partFrame.setSubtitle(String("#").add(m_selected + 1).add(part.label == "" ? String("") : String("  ").add(part.label)));
			String info = String("Ends at ").add(String::getHexStr(part.end(), true, 4)).add("  -  ").add(storage::formatSize(part.size));
			const i32 origin = m_origin[m_selected];
			if (origin < 0)
				info.add("\nNew: Apply writes it, then Format it.");
			else if (!is_on_disk(m_selected))
			{
				const auto& old = m_original.partitions[origin];
				info.add("\nOn the disk: ").add(String::getHexStr(old.start, true, 4)).add(", ").add(storage::formatSize(old.size));
			}
			m_partInfo.setMessage(info, DimColor);
		}
		else
		{
			m_partFrame.setSubtitle(m_selectedFree >= 0 ? String("unallocated space selected") : String(""));
			m_partInfo.setMessage(m_selectedFree >= 0 ? String("'New partition' creates one here.") : String("Select a partition in the bar or the list."), DimColor);
		}
		update_fs_group();
	}

	void PartitionManagerWindow::update_fs_group(void)
	{
		m_fsInfo.clearRows();
		const PartitionContent* c = content_of(m_selected);
		const bool onDisk = is_on_disk(m_selected);
		const bool canFormat = (m_document != nullptr && onDisk);
		m_formatButton.enable(canFormat);
		m_checkButton.enable(c != nullptr && c->dfs && c->readable);
		m_browseButton.enable(c != nullptr && c->dfs && c->readable && onDisk);
		if (m_selected < 0 || m_selected >= cast<i32>(m_table.partitions.size()))
		{
			m_fsFrame.setSubtitle("");
			m_usageBar.setValue(0, "");
			m_fsInfo.setEmptyText("No partition selected.");
			return;
		}
		const auto& part = m_table.partitions[m_selected];
		if (c == nullptr)
		{
			m_fsFrame.setSubtitle("new partition");
			m_usageBar.setValue(0, "Not on the disk yet");
			m_fsInfo.setEmptyText("Apply the table, then Format the partition.");
			return;
		}
		if (!c->dfs)
		{
			m_fsFrame.setSubtitle(c->fileSystem);
			m_usageBar.setValue(0, c->fileSystem == "empty" ? String("Empty (zeros)") : String("No file system"));
			m_fsInfo.setEmptyText("");
			if (c->bootHeader)
				m_fsInfo.addRow("Boot header", "F1 C4: a raw kernel partition (Drake loads its first 1 MiB)", WarningColor);
			m_fsInfo.addRow("Format", onDisk ? String("Format... makes it a DFS volume") : String("Apply the table first, then Format..."), DimColor);
			return;
		}
		if (!c->readable)
		{
			m_fsFrame.setSubtitle("DFS (damaged)");
			m_usageBar.setValue(0, "Can't be mounted");
			m_fsInfo.addRow("Error", c->error, ErrorColor);
			m_fsInfo.addRow("Format", "Format... writes a new, empty file system", DimColor);
			return;
		}
		m_fsFrame.setSubtitle(String("DFS").add(c->label == "" ? String("") : String("  \"").add(c->label).add("\"")));
		const f32 used = c->usedFraction();
		m_usageBar.setValue(used, String(storage::formatSize(c->usedBytes)).add(" used of ").add(storage::formatSize(c->fsSize)).add("  (").add(percent(used)).add(")  -  ")
							.add(storage::formatSize(c->freeBytes)).add(" free"), used > 0.9f ? ErrorColor : (used > 0.75f ? WarningColor : AccentColor));
		const dfs::Layout& l = c->layout;
		const u32 usedInodes = l.inode_count - c->sb.free_inodes - dfs::FirstInode + 1;
		m_fsInfo.addRow("Layout", String("").add(l.block_size).add("-byte blocks (").add(l.total_blocks).add("),  ").add(usedInodes).add(" of ").add(l.inode_count - dfs::FirstInode + 1).add(" inodes used"));
		m_fsInfo.addRow("State", String(c->sb.state == dfs::StateClean ? "clean" : "dirty (not unmounted)").add(",  mounted ").add(c->sb.mount_count).add(c->sb.mount_count == 1 ? " time" : " times").add(",  last write ").add(storage::DfsVolume::formatTime(c->sb.last_write)),
						c->sb.state == dfs::StateClean ? ValueColor : WarningColor);
		// Resizing: what the partition's size allows.
		m_fsInfo.addRow("Resizable", String("from ").add(storage::formatSize(c->minSize)).add(" (what it uses) to 4 GiB"), DimColor);
		const u64 unused = std::min<u64>(part.size, max_dfs_size()) - std::min<u64>(c->fsSize, std::min<u64>(part.size, max_dfs_size()));
		if (onDisk && unused >= c->layout.block_size)
			m_fsInfo.addRow("Unused", String(storage::formatSize(unused)).add(" at the end of the partition (grow the file system: resize the partition)"), WarningColor);
		if (!onDisk && m_origin[m_selected] >= 0)
		{
			const u64 target = std::min<u64>(part.size, max_dfs_size());
			if (!m_moveContents)
				m_fsInfo.addRow("Pending", "The table only (contents not moved / resized)", WarningColor);
			else if (target < c->minSize)
				m_fsInfo.addRow("Pending", String("Too small: at least ").add(storage::formatSize(c->minSize)), ErrorColor);
			else if (target != c->fsSize)
				m_fsInfo.addRow("Pending", String(target < c->fsSize ? "Shrinks" : "Grows").add(" to ").add(storage::formatSize(target)), WarningColor);
			else
				m_fsInfo.addRow("Pending", "Moves with the partition", WarningColor);
		}
		if (c->hasKernel)
			m_fsInfo.addRow("Boot", String("/boot/kernel0 (").add(storage::formatSize(c->kernelSize)).add(")").add(part.hasFlag(0) && c->bootHeader ? ": Drake can boot it" : (c->bootHeader ? ": set the boot flag" : ": no boot header (dtools install-kernel)")),
							part.hasFlag(0) && c->bootHeader ? SuccessColor : WarningColor);
		else
			m_fsInfo.addRow("Boot", "No /boot/kernel0", DimColor);
		m_fsInfo.addRow("UUID", c->uuid, DimColor);
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
			const f32 x = 338 + 100 * cast<f32>(m_flagChecks.size() - 1);
			place(check, 0, 1, 0, 1, x, GroupTop + 74, x + 96, GroupTop + 100);
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
		m_flagsTitle.setVisible(!flags.empty());
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
			set_message("No unallocated space on the disk: shrink or delete a partition first.", ErrorColor);
			refresh_view();
			return;
		}
		storage::tPartition part;
		part.start = start;
		part.size = std::min<u64>(size, m_scheme->getMaxAddress() - start);
		m_table.partitions.push_back(part);
		m_origin.push_back(-1);
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
		m_origin.erase(m_origin.begin() + index);
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
		m_origin.clear();
		// Not the disk's table anymore: Apply writes it even if it's the same.
		m_hasOriginal = false;
		m_selectedFree = -1;
		set_message(String("New empty ").add(name).add(" table: add the partitions, then Apply."), DebugListView::TextColor);
		select_partition(-1);
	}

	bool PartitionManagerWindow::commitScratch(DiskDocument& document, const stdvec<u8>& scratch, const stdvec<std::pair<u64, u64>>& ranges, String& outError)
	{
		for (const auto& [address, size] : ranges)
		{
			if (address > scratch.size() || size > scratch.size() - address)
			{
				outError = "Outside of the disk.";
				return false;
			}
			if (!document.getBufferDisk().write(address, std::span<const u8>(scratch.data() + address, size)))
			{
				outError = "Unable to update the disk's copy.";
				return false;
			}
			if (!document.saveRange(address, size, outError))
				return false;
		}
		return true;
	}

	void PartitionManagerWindow::apply(void)
	{
		if (m_document == nullptr || m_scheme == nullptr || !is_pending())
			return;
		const stdvec<Operation> ops = plan_operations();
		bool blocking = !m_scheme->validate(m_table, m_document->getSize()).empty();
		for (const auto& op : ops)
			blocking = blocking || op.blocking;
		if (blocking)
		{
			refresh_view();
			return;
		}
		const u64 now = nowMs();
		if (now > m_confirmUntil)
		{
			m_confirmUntil = now + ConfirmMs;
			m_applyButton.setText("Confirm");
			set_message(String("Click Confirm to apply the ").add(cast<u64>(ops.size())).add(ops.size() == 1 ? " change to " : " changes to ").add(m_document->getName()).add("."), WarningColor);
			refresh_view();
			return;
		}
		m_confirmUntil = 0;
		m_applyButton.setText("Apply");

		// On a copy of the disk: the contents (shrink, move, grow), then the table.
		DiskDocument& document = *m_document;
		stdvec<u8> scratch = document.getBuffer();
		storage::MemoryDisk scratchDisk { std::span<u8>(scratch) };
		RecordingDisk disk(scratchDisk);
		String error = "";
		bool ok = true;
		if (m_hasOriginal && m_moveContents)
		{
			struct Change { i32 index; storage::tPartition from, to; const PartitionContent* content; };
			stdvec<Change> changes;
			for (size_t i = 0; i < m_table.partitions.size(); i++)
			{
				const i32 origin = m_origin[i];
				if (origin < 0)
					continue;
				const auto& from = m_original.partitions[origin];
				const auto& to = m_table.partitions[i];
				if (from.start != to.start || from.size != to.size)
					changes.push_back({ cast<i32>(i), from, to, &m_contents[origin] });
			}
			auto l_resize = [&](const Change& change, u64 start, u64 size) {
				storage::DiskRegion region(disk, start, std::min<u64>(size, max_dfs_size()));
				const dfs::Device dev = storage::DFS::makeDevice(region);
				std::unique_ptr<dfs::Volume> vol(new dfs::Volume());
				dfs::ResizeInfo info;
				dfs::FsckResult check;
				const dfs::i32 r = dfs::resize(vol.get(), &dev, cast<u32>(std::min<u64>(change.to.size, max_dfs_size())), &info, &check);
				if (r < 0)
				{
					error = String("partition ").add(change.index + 1).add(": resizing DFS failed: ").add(storage::DfsVolume::errorText(r));
					if (!check.messages.empty())
						error.add(" (").add(check.messages[0]).add(")");
					return false;
				}
				return true;
			};
			// 1. Shrink the file systems where they are.
			for (const auto& change : changes)
			{
				const PartitionContent& c = *change.content;
				if (ok && c.dfs && c.readable && std::min<u64>(change.to.size, max_dfs_size()) < c.fsSize)
					ok = l_resize(change, change.from.start, change.from.size);
			}
			// 2. Move the data (all read first: a partition can move where another one was).
			stdvec<std::pair<u64, stdvec<u8>>> moved;
			for (const auto& change : changes)
			{
				if (!ok || change.from.start == change.to.start)
					continue;
				stdvec<u8> data(std::min(change.from.size, change.to.size));
				if (!disk.read(change.from.start, data))
				{
					error = String("partition ").add(change.index + 1).add(": unable to read its data");
					ok = false;
				}
				moved.push_back({ change.to.start, std::move(data) });
			}
			for (const auto& [address, data] : moved)
			{
				if (ok && !disk.write(address, data))
				{
					error = String("unable to write at ").add(String::getHexStr(address, true, 4));
					ok = false;
				}
			}
			// 3. Grow the file systems where they are now.
			for (const auto& change : changes)
			{
				const PartitionContent& c = *change.content;
				if (ok && c.dfs && c.readable && std::min<u64>(change.to.size, max_dfs_size()) > c.fsSize)
					ok = l_resize(change, change.to.start, change.to.size);
			}
		}
		if (ok)
			ok = m_scheme->write(disk, m_table, error);
		if (ok)
			ok = commitScratch(document, scratch, disk.getRanges(), error);
		if (!ok)
		{
			set_message(String("Nothing was written: ").add(error), ErrorColor);
			refresh_view();
			return;
		}
		m_diskManager.diskWritten(document, String(m_scheme->getName()).add(" table written"));
		const i32 selected = m_selected;
		m_note = "";
		load_table();
		set_message(String("Done: ").add(cast<u64>(ops.size())).add(ops.size() == 1 ? " change" : " changes").add(" written to ").add(document.getName()).add("."), SuccessColor);
		select_partition(selected);
	}

	void PartitionManagerWindow::check_file_system(void)
	{
		const PartitionContent* c = content_of(m_selected);
		if (c == nullptr || !c->dfs || m_document == nullptr)
			return;
		const auto& old = m_original.partitions[m_origin[m_selected]];
		storage::DiskRegion region(m_document->getBufferDisk(), old.start, old.size);
		storage::DfsVolume vol(region);
		dfs::i32 r = vol.mount(true);
		dfs::FsckResult result;
		if (r == dfs::Ok)
			r = dfs::fsck(vol.get(), result);
		auto& console = m_diskManager.getDebugger().console();
		const String name = String("Partition ").add(m_selected + 1);
		if (r < 0)
		{
			set_message(String(name).add(": the check stopped: ").add(storage::DfsVolume::errorText(r)), ErrorColor);
			refresh_view();
			return;
		}
		console.fg(ostd::ConsoleColors::Blue).p("fsck ").p(name).p(": ").reset().p(result.directories).p(" directories, ").p(result.files).p(" files").nl();
		for (const auto& message : result.messages)
			console.fg(ostd::ConsoleColors::Yellow).p("  ").p(message).reset().nl();
		if (result.clean())
			set_message(String(name).add(": the file system is clean (").add(result.directories).add(" directories, ").add(result.files).add(" files)."), SuccessColor);
		else
			set_message(String(name).add(": ").add(result.errors).add(" errors, ").add(result.leaked_blocks).add(" leaked blocks, ").add(result.leaked_inodes).add(" leaked inodes (see the console)."), ErrorColor);
		refresh_view();
	}

	void PartitionManagerWindow::open_browser(void)
	{
		const PartitionContent* c = content_of(m_selected);
		if (c == nullptr || !c->dfs || !c->readable || !is_on_disk(m_selected) || m_document == nullptr)
			return;
		const auto& part = m_table.partitions[m_selected];
		String name = String("Partition ").add(m_selected + 1);
		if (c->label != "")
			name.add("  \"").add(c->label).add("\"");
		m_browser.openFor(m_document, part.start, part.size, name);
	}

	void PartitionManagerWindow::open_format(void)
	{
		if (m_document == nullptr || !is_on_disk(m_selected))
			return;
		const PartitionContent* c = content_of(m_selected);
		m_formatWindow.openFor(m_document, m_selected, m_table.partitions[m_selected], c != nullptr ? *c : PartitionContent());
	}

	void PartitionManagerWindow::onUpdate(void)
	{
		SubWindow::onUpdate();
		if (m_confirmUntil != 0 && nowMs() > m_confirmUntil)
		{
			m_confirmUntil = 0;
			m_applyButton.setText("Apply");
			m_note = "";
			refresh_view();
		}
	}
}
