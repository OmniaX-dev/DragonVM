#include "DiskManager.hpp"
#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "../storage/FileSystem.hpp"
#include <chrono>
#include <filesystem>

namespace dragon
{
	using namespace ogfx::gui;

	using namespace disk_ui;




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
		setupButton(m_openButton, "Open...", "Opens a disk image file (not connected to the machine)");
		m_openButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { open_file(); });
		addWidget(m_closeButton);
		place(m_closeButton, 1, 0, 1, 0, -378, 6, -306, 34);
		setupButton(m_closeButton, "Close", "Closes the image file (the machine's disks stay)");
		m_closeButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { close_file(); });
		addWidget(m_newSizeTitle);
		place(m_newSizeTitle, 1, 0, 1, 0, -312, 0, -254, 28);
		setupTitle(m_newSizeTitle, "Size");
		addWidget(m_newSizeInput);
		place(m_newSizeInput, 1, 0, 1, 0, -252, 6, -168, 34);
		m_newSizeInput.setFontSize(14);
		m_newSizeInput.setText("4M");
		m_newSizeInput.setTooltipText("Size of a new disk: 32M, 8192K, 0x2000000, 33554432...");
		m_newSizeInput.enableTooltip();
		m_newSizeInput.setCommitCallback([](FieldInput&) {  });
		addWidget(m_newButton);
		place(m_newButton, 1, 0, 1, 0, -162, 6, -8, 34);
		setupButton(m_newButton, "New disk...", "Creates a new disk image file of that size (zeros) and opens it");
		m_newButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_disk(); });

		// Row 2: saving, loading a binary, the partitions.
		addWidget(m_saveButton);
		place(m_saveButton, 0, 0, 0, 0, 8, 40, 88, 68);
		setupButton(m_saveButton, "Save", "Writes the changed bytes to the disk");
		m_saveButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { save(); });
		addWidget(m_reloadButton);
		place(m_reloadButton, 0, 0, 0, 0, 94, 40, 174, 68);
		setupButton(m_reloadButton, "Reload", "Reads the disk again (drops the changes)");
		m_reloadButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { reload(); });
		addWidget(m_loadAddressTitle);
		place(m_loadAddressTitle, 0, 0, 0, 0, 196, 40, 256, 68);
		setupTitle(m_loadAddressTitle, "Load at");
		addWidget(m_loadAddressInput);
		place(m_loadAddressInput, 0, 0, 0, 0, 260, 40, 380, 68);
		m_loadAddressInput.setFontSize(14);
		m_loadAddressInput.setText("0x00000000");
		m_loadAddressInput.setTooltipText("Disk address for 'Load binary': 0x18000, 98304...");
		m_loadAddressInput.enableTooltip();
		m_loadAddressInput.setCommitCallback([](FieldInput&) {  });
		addWidget(m_loadButton);
		place(m_loadButton, 0, 0, 0, 0, 386, 40, 516, 68);
		setupButton(m_loadButton, "Load binary...", "Copies a file to the disk at the address (then Save writes it)");
		m_loadButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { load_binary(); });
		addWidget(m_partitionsButton);
		place(m_partitionsButton, 1, 0, 1, 0, -162, 40, -8, 68);
		setupButton(m_partitionsButton, "Partitions...", "Opens the partition editor for this disk");
		m_partitionsButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { openPartitionManager(); });

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
		if (isError(color))
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
			set_status(String("Invalid size: '").add(m_newSizeInput.getText()).add("' (32M, 8192K, 0x2000000, 33554432...)."), ErrorColor);
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

	void DiskManagerWindow::openPartitionManager(void)
	{
		if (current() != nullptr)
			m_partitionManager.openFor(current());
	}

	void DiskManagerWindow::diskWritten(DiskDocument& document, const String& what)
	{
		if (document.isMachineDrive())
			m_debugger.commitEdit();
		if (&document == current())
			show_document(true);
		update_combo();
		if (what != "")
			set_status(String(what).add(" (").add(document.getName()).add(document.isMachineDrive() ? ", an edit in the history)" : ")"), SuccessColor);
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
