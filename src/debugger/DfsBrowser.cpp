#include "DfsBrowser.hpp"
#include "PartitionManager.hpp"
#include "DiskManager.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace dragon
{
	using namespace ogfx::gui;
	using namespace disk_ui;
	namespace fs = std::filesystem;




	//FileTreeView
	void FileTreeView::onMouseMoved(const Event& event)
	{
		TreeView::onMouseMoved(event);
		if (!getWindow().isFileDragActive())
			return;
		Item* hovered = getItemAt({ event.mouse->position_x, event.mouse->position_y });
		setHighlightedItem(m_target ? m_target(hovered) : hovered);
	}

	void FileTreeView::onMouseExited(const Event& event)
	{
		TreeView::onMouseExited(event);
		setHighlightedItem(nullptr);
	}

	void FileTreeView::onUpdate(void)
	{
		TreeView::onUpdate();
		if (getHighlightedItem() != nullptr && !getWindow().isFileDragActive())
			setHighlightedItem(nullptr);
	}




	//DfsBrowserWindow
	DfsBrowserWindow::DfsBrowserWindow(Window& window, PartitionManagerWindow& manager) : SubWindow(window, "DFS Browser"), m_manager(manager)
	{
		setBounds({ 340, 120, 980, 640 });
		setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
	}

	void DfsBrowserWindow::build(void)
	{
		// Toolbar
		f32 x = 10;
		auto l_button = [&](Button& button, const String& text, const String& tooltip, f32 width, std::function<void(void)> action) {
			addWidget(button);
			setupButton(button, text, tooltip);
			place(button, 0, 0, 0, 0, x, 8, x + width, 36);
			x += width + 6;
			button.setCallback(Widget::eCallback::ActionPerformed, [action](const Event&) { action(); });
		};
		l_button(m_refreshButton, "Refresh", "Reads the file system again", 84, [this](void) { reloadTree(); });
		x += 8;
		l_button(m_importButton, "Import files...", "Copies host files into the selected directory (or drop them on the tree)", 124, [this](void) { import_files(); });
		l_button(m_importFolderButton, "Import folder...", "Copies a host folder (with everything in it) into the selected directory", 134, [this](void) { import_folder(); });
		l_button(m_extractButton, "Extract...", "Copies the selected file, or directory with everything in it, to the host", 96, [this](void) { extract(); });
		x += 8;
		l_button(m_deleteButton, "Delete", "Deletes the selected file, or directory with everything in it (click twice)", 84, [this](void) { remove(); });
		addWidget(m_pathLine);
		place(m_pathLine, 0, 0, 1, 0, x + 12, 12, -10, 36);

		// The tree, the details.
		addWidget(m_tree);
		place(m_tree, 0, 0, 0.56f, 1, 10, 44, -5, -40);
		m_tree.setFontSize(15);
		m_tree.enableIcons(false);
		m_tree.enableDragAndDrop();
		m_tree.setSelectionChangedCallback([this](stdvec<TreeView::Item*>&) { show_details(); });
		m_tree.setItemActivatedCallback([this](TreeView::Item& item) {
			if (item.hasChildren())
				item.toggleExpanded();
		});
		m_tree.setItemContextCallback([this](TreeView::Item*, const Vec2& position) { show_context_menu(position); });
		m_tree.setCallback(Widget::eCallback::DragAndDrop, [this](const Event& event) { on_drop(event, true); });
		m_tree.setDropTargetFunction([this](TreeView::Item* hovered) -> TreeView::Item* {
			if (hovered == nullptr)
				return item_for_path("/");
			const i64 index = hovered->getUserData();
			if (index < 0 || index >= cast<i64>(m_nodes.size()))
				return hovered;
			if (m_nodes[index].entry.isDir())
				return hovered;
			return hovered->getParentItem() != nullptr ? hovered->getParentItem() : hovered;
		});
		// Drops elsewhere in the window: into the selected directory.
		enableDragAndDrop();
		setCallback(Widget::eCallback::DragAndDrop, [this](const Event& event) { on_drop(event, false); });

		addWidget(m_detailsFrame);
		place(m_detailsFrame, 0.56f, 0, 1, 1, 5, 44, -10, -40);
		m_detailsFrame.setTitle("Properties");
		addWidget(m_details);
		place(m_details, 0.56f, 0, 1, 1, 17, 78, -22, -84);
		m_details.setNameWidth(96);
		addWidget(m_nameTitle);
		setupTitle(m_nameTitle, "Name");
		place(m_nameTitle, 0.56f, 1, 0.56f, 1, 17, -76, 77, -48);
		addWidget(m_nameInput);
		m_nameInput.setFontSize(14);
		m_nameInput.setTooltipText("A name for New folder (in the selected directory) or Rename (Return renames)");
		m_nameInput.enableTooltip();
		m_nameInput.setCommitCallback([](FieldInput&) {  });
		place(m_nameInput, 0.56f, 1, 1, 1, 80, -76, -220, -48);
		addWidget(m_newFolderButton);
		setupButton(m_newFolderButton, "New folder", "Creates a directory with this name in the selected directory");
		place(m_newFolderButton, 1, 1, 1, 1, -214, -76, -118, -48);
		m_newFolderButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { new_folder(); });
		addWidget(m_renameButton);
		setupButton(m_renameButton, "Rename", "Renames the selected file or directory (a path moves it)");
		place(m_renameButton, 1, 1, 1, 1, -112, -76, -22, -48);
		m_renameButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { rename(); });

		addWidget(m_statusLine);
		place(m_statusLine, 0, 1, 1, 1, 12, -34, -10, -8);
	}

	void DfsBrowserWindow::openFor(DiskDocument* document, u64 start, u64 size, const String& name)
	{
		const bool same = (document == m_document && start == m_start && size == m_size);
		m_document = document;
		m_start = start;
		m_size = size;
		m_name = name;
		setTitle(String("DFS Browser - ").add(name));
		if (!same)
		{
			m_tree.clear();
			m_nodes.clear();
		}
		set_status(document != nullptr && document->isMachineDrive() ? String("Changes are written to the machine's disk at once (Step back undoes them).")
																	  : String("Changes are written to the image file at once. Drop files on the tree to import them."), DimColor);
		reloadTree();
		open();
		fitSubWindow(*this, 1000, 660, m_placed);
	}

	void DfsBrowserWindow::documentChanged(DiskDocument* document)
	{
		if (m_document != nullptr && document != m_document && isVisible())
			close();
		if (document != m_document)
		{
			m_document = nullptr;
			m_tree.clear();
			m_nodes.clear();
		}
	}

	dfs::i32 DfsBrowserWindow::read_op(const std::function<dfs::i32(storage::DfsVolume& vol)>& op, String& outError)
	{
		if (m_document == nullptr)
		{
			outError = "no disk";
			return dfs::Invalid;
		}
		m_document->sync();
		storage::DiskRegion region(m_document->getBufferDisk(), m_start, std::min<u64>(m_size, 0xFFFFFFFFull));
		storage::DfsVolume vol(region);
		dfs::i32 r = vol.mount(true);
		if (r == dfs::Ok)
			r = op(vol);
		if (r < 0 && outError == "")
			outError = storage::DfsVolume::errorText(r);
		return r;
	}

	dfs::i32 DfsBrowserWindow::write_op(const std::function<dfs::i32(storage::DfsVolume& vol)>& op, String& outError)
	{
		if (m_document == nullptr)
		{
			outError = "no disk";
			return dfs::Invalid;
		}
		DiskDocument& document = *m_document;
		document.sync();
		RecordingDisk recording(document.getBufferDisk());
		storage::DiskRegion region(recording, m_start, std::min<u64>(m_size, 0xFFFFFFFFull));
		dfs::i32 r = dfs::Ok;
		{
			storage::DfsVolume vol(region);
			r = vol.mount(false);
			if (r == dfs::Ok)
			{
				r = op(vol);
				const dfs::i32 r2 = vol.unmount();
				if (r == dfs::Ok)
					r = r2;
			}
		}
		if (r < 0 && outError == "")
			outError = storage::DfsVolume::errorText(r);
		// What was written (even after a failure: the file system stays consistent).
		String saveError = "";
		for (const auto& [address, size] : recording.getRanges())
		{
			if (!document.saveRange(address, size, saveError))
			{
				outError = String("unable to write to the disk: ").add(saveError);
				r = dfs::IOError;
				break;
			}
		}
		if (!recording.getRanges().empty())
		{
			m_manager.getDiskManager().diskWritten(document, "");
			m_manager.contentChanged(document);
		}
		return r;
	}

	void DfsBrowserWindow::reloadTree(void)
	{
		// Keep the selection.
		const Node* current = selected_node();
		const String selectPath = (current != nullptr ? current->path : String("/"));
		stdvec<Node> nodes;
		String error = "";
		bool truncated = false;
		const dfs::i32 r = read_op([&](storage::DfsVolume& vol) -> dfs::i32 {
			const dfs::Superblock& sb = vol.superblock();
			const dfs::Layout& l = vol.layout();
			const u64 total = cast<u64>(l.total_blocks) * l.block_size;
			const u64 free = cast<u64>(sb.free_blocks) * l.block_size;
			m_fsSummary = String(storage::formatSize(total - free)).add(" used of ").add(storage::formatSize(total)).add(",  ").add(storage::formatSize(free)).add(" free");
			Node root;
			root.path = "/";
			root.entry.name = (vol.label() == "" ? String("/") : String("/  (").add(vol.label()).add(")"));
			root.entry.inode = dfs::RootInode;
			dfs::i32 r = dfs::read_inode(vol.get(), dfs::RootInode, &root.entry.node);
			if (r < 0)
				return r;
			nodes.push_back(root);
			std::function<dfs::i32(const String&, u32)> walk = [&](const String& path, u32 depth) -> dfs::i32 {
				if (depth > storage::DfsVolume::MaxDepth)
					return dfs::Corrupt;
				stdvec<storage::DfsVolume::Entry> entries;
				dfs::i32 r = vol.list(path, entries);
				if (r < 0)
					return r;
				for (const auto& entry : entries)
				{
					if (nodes.size() >= MaxNodes)
					{
						truncated = true;
						return dfs::Ok;
					}
					const String child = storage::DfsVolume::joinPath(path, entry.name);
					nodes.push_back({ child, entry });
					if (entry.isDir() && (r = walk(child, depth + 1)) < 0)
						return r;
				}
				return dfs::Ok;
			};
			return walk("/", 0);
		}, error);
		if (r < 0)
		{
			set_status(String("Unable to read the file system: ").add(error), ErrorColor);
			if (nodes.empty())
			{
				m_tree.clear();
				m_nodes.clear();
				show_details();
				return;
			}
		}
		rebuild_tree(nodes, selectPath);
		if (truncated)
			set_status(String("Only the first ").add(MaxNodes).add(" entries are shown."), WarningColor);
	}

	void DfsBrowserWindow::rebuild_tree(const stdvec<Node>& nodes, const String& selectPath)
	{
		// The open directories stay open.
		std::set<std::string> expanded;
		const bool first = m_nodes.empty();
		for (u32 i = 0; i < m_tree.getItemCount(); i++)
		{
			TreeView::Item& item = m_tree.getLine(i);
			const i64 index = item.getUserData();
			if (item.isExpanded() && index >= 0 && index < cast<i64>(m_nodes.size()))
				expanded.insert(m_nodes[index].path.cpp_str());
		}
		m_tree.clear();
		m_nodes = nodes;
		std::map<std::string, TreeView::Item*> items;
		TreeView::Item* select = nullptr;
		for (size_t i = 0; i < m_nodes.size(); i++)
		{
			const Node& node = m_nodes[i];
			TreeView::Item* item = nullptr;
			if (node.path == "/")
				item = &m_tree.addRoot(node.entry.name);
			else
			{
				const auto parent = items.find(storage::DfsVolume::parentPath(node.path).cpp_str());
				if (parent == items.end())
					continue;
				item = &m_tree.addChild(*parent->second, node.entry.isDir() ? String(node.entry.name).add("/") : node.entry.name);
			}
			item->setUserData(cast<i64>(i));
			if (node.entry.isDir())
				item->setTextColor(DirectoryColor);
			item->setExpanded(node.path == "/" || (first ? false : expanded.count(node.path.cpp_str()) > 0));
			items[node.path.cpp_str()] = item;
			if (node.path == selectPath)
				select = item;
		}
		if (select == nullptr && !items.empty())
			select = items.begin()->second;
		if (select != nullptr)
		{
			m_tree.selectItem(*select);
			m_tree.ensureItemVisible(*select);
		}
		show_details();
	}

	TreeView::Item* DfsBrowserWindow::item_for_path(const String& path)
	{
		for (u32 i = 0; i < m_tree.getItemCount(); i++)
		{
			TreeView::Item& item = m_tree.getLine(i);
			const i64 index = item.getUserData();
			if (index >= 0 && index < cast<i64>(m_nodes.size()) && m_nodes[index].path == path)
				return &item;
		}
		return nullptr;
	}

	const DfsBrowserWindow::Node* DfsBrowserWindow::selected_node(void) const
	{
		auto& selection = const_cast<FileTreeView&>(m_tree).getSelection();
		if (selection.empty())
			return nullptr;
		const i64 index = selection.back()->getUserData();
		if (index < 0 || index >= cast<i64>(m_nodes.size()))
			return nullptr;
		return &m_nodes[index];
	}

	String DfsBrowserWindow::target_dir(void) const
	{
		const Node* node = selected_node();
		if (node == nullptr)
			return "/";
		return node->entry.isDir() ? node->path : storage::DfsVolume::parentPath(node->path);
	}

	void DfsBrowserWindow::show_details(void)
	{
		m_details.clearRows();
		const Node* node = selected_node();
		m_extractButton.enable(node != nullptr);
		m_deleteButton.enable(node != nullptr && node->path != "/");
		m_renameButton.enable(node != nullptr && node->path != "/");
		m_newFolderButton.enable(m_document != nullptr && !m_nodes.empty());
		m_importButton.enable(m_document != nullptr && !m_nodes.empty());
		m_importFolderButton.enable(m_document != nullptr && !m_nodes.empty());
		if (node == nullptr)
		{
			m_detailsFrame.setSubtitle("");
			m_pathLine.setMessage(m_name, DimColor);
			m_details.setEmptyText(m_document == nullptr ? "No partition." : "Nothing selected.");
			return;
		}
		const dfs::Inode& n = node->entry.node;
		const bool isDir = node->entry.isDir();
		m_pathLine.setMessage(String(m_name).add("   ").add(node->path), DebugListView::TextColor);
		m_detailsFrame.setSubtitle(node->path == "/" ? String("root directory") : storage::DfsVolume::baseName(node->path));
		if (node->path == "/")
			m_details.addRow("Volume", m_fsSummary, AccentColor);
		m_details.addRow("Path", node->path);
		m_details.addRow("Type", storage::DfsVolume::typeName(n.mode), isDir ? DirectoryColor : ValueColor);
		if (isDir)
		{
			u32 files = 0, dirs = 0;
			u64 bytes = 0;
			const String prefix = (node->path == "/" ? String("/") : String(node->path).add("/"));
			for (const auto& other : m_nodes)
			{
				if (other.path == node->path || !other.path.startsWith(prefix))
					continue;
				if (other.entry.isDir())
					dirs++;
				else
				{
					files++;
					bytes += other.entry.node.size;
				}
			}
			m_details.addRow("Contains", String("").add(files).add(files == 1 ? " file, " : " files, ").add(dirs).add(dirs == 1 ? " directory" : " directories").add("  (").add(storage::formatSize(bytes)).add(")"));
		}
		else
			m_details.addRow("Size", String(storage::formatSize(n.size)).add(n.size >= 1024 ? String("  (").add(n.size).add(" bytes)") : String("")));
		m_details.addRow("Inode", String("#").add(node->entry.inode).add(",  ").add(n.links).add(n.links == 1 ? " link" : " links"));
		m_details.addRow("Mode", storage::DfsVolume::formatMode(n.mode));
		m_details.addRow("Owner", String("uid ").add(n.uid).add(",  gid ").add(n.gid));
		m_details.addRow("Created", storage::DfsVolume::formatTime(n.crtime));
		m_details.addRow("Modified", storage::DfsVolume::formatTime(n.mtime));
		m_details.addRow("Changed", storage::DfsVolume::formatTime(n.ctime));
		String blocks = "";
		String error = "";
		read_op([&](storage::DfsVolume& vol) -> dfs::i32 {
			const u32 count = vol.blockCount(n);
			blocks = String("").add(count).add(" of ").add(vol.layout().block_size).add(" bytes  (").add(storage::formatSize(cast<u64>(count) * vol.layout().block_size)).add(" on disk)");
			if (n.dindirect != 0)
				blocks.add(", double indirect");
			else if (n.indirect != 0)
				blocks.add(", indirect");
			return dfs::Ok;
		}, error);
		m_details.addRow("Blocks", blocks, DimColor);
		if (node->path == "/boot/kernel0")
			m_details.addRow("Boot", "Drake loads this file at 0x01410000", SuccessColor);
		m_nameInput.setText(node->path == "/" ? String("") : storage::DfsVolume::baseName(node->path));
	}

	void DfsBrowserWindow::set_status(const String& text, const Color& color)
	{
		m_statusLine.setMessage(text, color);
		if (isError(color))
			m_manager.getDiskManager().getDebugger().console().fg(ostd::ConsoleColors::Red).p("DFS Browser: ").p(text).reset().nl();
	}

	void DfsBrowserWindow::new_folder(void)
	{
		const String name = m_nameInput.getText().new_trim();
		if (name == "" || name.contains("/"))
		{
			set_status("Type a name (without '/') in the name field.", ErrorColor);
			return;
		}
		const String path = storage::DfsVolume::joinPath(target_dir(), name);
		String error = "";
		const dfs::i32 r = write_op([&](storage::DfsVolume& vol) { return vol.makeDir(path); }, error);
		if (r < 0)
		{
			set_status(String("New folder ").add(path).add(": ").add(error), ErrorColor);
			return;
		}
		reloadTree();
		if (TreeView::Item* item = item_for_path(path))
		{
			m_tree.selectItem(*item);
			m_tree.ensureItemVisible(*item);
			show_details();
		}
		set_status(String("Created ").add(path), SuccessColor);
	}

	void DfsBrowserWindow::rename(void)
	{
		const Node* node = selected_node();
		if (node == nullptr || node->path == "/")
			return;
		const String name = m_nameInput.getText().new_trim();
		if (name == "")
		{
			set_status("Type the new name in the name field.", ErrorColor);
			return;
		}
		// A name stays in the directory, a path (with '/') moves.
		const String from = node->path;
		const String to = (name.startsWith("/") ? name : storage::DfsVolume::joinPath(storage::DfsVolume::parentPath(from), name));
		if (to == from)
			return;
		String error = "";
		const dfs::i32 r = write_op([&](storage::DfsVolume& vol) { return vol.rename(from, to); }, error);
		if (r < 0)
		{
			set_status(String("Rename ").add(from).add(" to ").add(to).add(": ").add(error), ErrorColor);
			return;
		}
		reloadTree();
		if (TreeView::Item* item = item_for_path(to))
		{
			m_tree.selectItem(*item);
			m_tree.ensureItemVisible(*item);
			show_details();
		}
		set_status(String("Renamed ").add(from).add(" to ").add(to), SuccessColor);
	}

	void DfsBrowserWindow::remove(void)
	{
		const Node* node = selected_node();
		if (node == nullptr || node->path == "/")
			return;
		const String path = node->path;
		const u64 now = nowMs();
		if (now > m_confirmDeleteUntil || m_confirmDeletePath != path)
		{
			m_confirmDeleteUntil = now + ConfirmMs;
			m_confirmDeletePath = path;
			m_deleteButton.setText("Confirm");
			set_status(String("Click Confirm to delete ").add(path).add(node->entry.isDir() ? " and everything in it." : "."), WarningColor);
			return;
		}
		m_confirmDeleteUntil = 0;
		m_deleteButton.setText("Delete");
		String error = "";
		const dfs::i32 r = write_op([&](storage::DfsVolume& vol) { return vol.removeTree(path); }, error);
		if (r < 0)
		{
			set_status(String("Delete ").add(path).add(": ").add(error), ErrorColor);
			reloadTree();
			return;
		}
		const String parent = storage::DfsVolume::parentPath(path);
		reloadTree();
		if (TreeView::Item* item = item_for_path(parent))
		{
			m_tree.selectItem(*item);
			show_details();
		}
		set_status(String("Deleted ").add(path), SuccessColor);
	}

	void DfsBrowserWindow::import_files(void)
	{
		const String dir = target_dir();
		const stdvec<String> paths = getWindow().showOpenFileDialog({ { "All files", { "*" } } }, true);
		if (!paths.empty())
			import_paths(paths, dir);
	}

	void DfsBrowserWindow::import_folder(void)
	{
		const String dir = target_dir();
		const String path = getWindow().showOpenFolderDialog();
		if (path != "")
			import_paths({ path }, dir);
	}

	void DfsBrowserWindow::import_paths(const stdvec<String>& hostPaths, const String& dir)
	{
		storage::DfsVolume::CopyResult result;
		String error = "";
		const dfs::i32 r = write_op([&](storage::DfsVolume& vol) -> dfs::i32 {
			for (const auto& hostPath : hostPaths)
			{
				const dfs::i32 r = vol.importHost(hostPath, dir, result);
				if (r < 0)
					return r;
			}
			return dfs::Ok;
		}, error);
		reloadTree();
		String done = String("").add(result.files).add(result.files == 1 ? " file" : " files");
		if (result.directories > 0)
			done.add(", ").add(result.directories).add(result.directories == 1 ? " directory" : " directories");
		done.add(" (").add(storage::formatSize(result.bytes)).add(")");
		if (r < 0)
		{
			set_status(String("Import stopped at ").add(result.failedPath).add(": ").add(error).add(" (").add(done).add(" copied)"), ErrorColor);
			return;
		}
		// The imported item, when there's one.
		if (hostPaths.size() == 1)
		{
			const String path = storage::DfsVolume::joinPath(dir, String(fs::path(hostPaths[0].cpp_str()).filename().string()));
			if (TreeView::Item* item = item_for_path(path))
			{
				m_tree.selectItem(*item);
				m_tree.ensureItemVisible(*item);
				show_details();
			}
		}
		set_status(String("Imported ").add(done).add(" into ").add(dir), SuccessColor);
	}

	void DfsBrowserWindow::extract(void)
	{
		const Node* node = selected_node();
		if (node == nullptr)
			return;
		const String path = node->path;
		String hostPath = "";
		if (node->entry.isDir())
		{
			// Into a host folder, as a folder of its own name.
			const String folder = getWindow().showOpenFolderDialog();
			if (folder == "")
				return;
			String name = (path == "/" ? String("dfs-root") : storage::DfsVolume::baseName(path));
			hostPath = String((fs::path(folder.cpp_str()) / name.cpp_str()).string());
		}
		else
		{
			hostPath = getWindow().showSaveFileDialog({ { "All files", { "*" } } }, storage::DfsVolume::baseName(path));
			if (hostPath == "")
				return;
		}
		storage::DfsVolume::CopyResult result;
		String error = "";
		const dfs::i32 r = read_op([&](storage::DfsVolume& vol) { return vol.exportHost(path, hostPath, result); }, error);
		if (r < 0)
		{
			set_status(String("Extract stopped at ").add(result.failedPath).add(": ").add(error), ErrorColor);
			return;
		}
		set_status(String("Extracted ").add(path).add(" to ").add(hostPath).add(" (").add(result.files).add(result.files == 1 ? " file, " : " files, ").add(storage::formatSize(result.bytes)).add(")"), SuccessColor);
	}

	void DfsBrowserWindow::on_drop(const Event& event, bool onTree)
	{
		if (event.drop.dropType != ogfx::DropEventData::eDropType::File || event.drop.textOrFilePath == "" || m_document == nullptr || m_nodes.empty())
			return;
		String dir = target_dir();
		if (onTree && event.mouse != nullptr)
		{
			TreeView::Item* hovered = m_tree.getItemAt({ event.mouse->position_x, event.mouse->position_y });
			if (hovered == nullptr)
				dir = "/";
			else
			{
				const i64 index = hovered->getUserData();
				if (index >= 0 && index < cast<i64>(m_nodes.size()))
					dir = (m_nodes[index].entry.isDir() ? m_nodes[index].path : storage::DfsVolume::parentPath(m_nodes[index].path));
			}
		}
		// One event per file: they're imported together on the next update.
		if (!m_drops.empty() && dir != m_dropDir)
		{
			import_paths(m_drops, m_dropDir);
			m_drops.clear();
		}
		m_dropDir = dir;
		m_drops.push_back(event.drop.textOrFilePath);
		m_tree.setHighlightedItem(nullptr);
	}

	void DfsBrowserWindow::show_context_menu(const Vec2& position)
	{
		const Node* node = selected_node();
		ContextMenu::Instance menu;
		const bool root = (node == nullptr || node->path == "/");
		menu.entries.push_back({ "Import files here...", 0 });
		menu.entries.push_back({ "Import folder here...", 1 });
		menu.entries.push_back(ContextMenu::Entry::separator());
		ContextMenu::Entry extractEntry { node != nullptr && node->entry.isDir() ? "Extract folder..." : "Extract...", 2 };
		extractEntry.enabled = (node != nullptr);
		menu.entries.push_back(extractEntry);
		menu.entries.push_back({ "New folder (name field)", 3 });
		ContextMenu::Entry renameEntry { "Rename (name field)", 4 };
		renameEntry.enabled = !root;
		menu.entries.push_back(renameEntry);
		menu.entries.push_back(ContextMenu::Entry::separator());
		ContextMenu::Entry deleteEntry { "Delete", 5 };
		deleteEntry.enabled = !root;
		menu.entries.push_back(deleteEntry);
		menu.entries.push_back(ContextMenu::Entry::separator());
		menu.entries.push_back({ "Refresh", 6 });
		menu.onActivate = [this](const ContextMenu::Entry& entry) {
			switch (entry.id)
			{
				case 0: import_files(); break;
				case 1: import_folder(); break;
				case 2: extract(); break;
				case 3: new_folder(); break;
				case 4: rename(); break;
				case 5: remove(); break;
				case 6: reloadTree(); break;
				default: break;
			}
		};
		getWindow().showContextMenu(menu, position);
	}

	void DfsBrowserWindow::onUpdate(void)
	{
		SubWindow::onUpdate();
		if (!m_drops.empty())
		{
			const stdvec<String> drops = m_drops;
			m_drops.clear();
			import_paths(drops, m_dropDir);
		}
		if (m_confirmDeleteUntil != 0 && nowMs() > m_confirmDeleteUntil)
		{
			m_confirmDeleteUntil = 0;
			m_deleteButton.setText("Delete");
			set_status("", DimColor);
		}
	}
}
