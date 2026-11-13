#pragma once

#include "DiskWidgets.hpp"
#include "../storage/DfsVolume.hpp"
#include <functional>

namespace dragon
{
	class DiskDocument;
	class PartitionManagerWindow;

	// The browser's tree: while files are dragged over it from another application,
	// the directory they would go to is outlined.
	class FileTreeView : public ogfx::gui::TreeView
	{
		public: using TargetFunction = std::function<Item*(Item* hovered)>;
		public:
			inline FileTreeView(ogfx::gui::Window& window) : TreeView(window) { setTypeName("dragon::FileTreeView"); }
			inline void setDropTargetFunction(TargetFunction function) { m_target = std::move(function); }
			void onMouseMoved(const ogfx::gui::Event& event) override;
			void onMouseExited(const ogfx::gui::Event& event) override;
			void onUpdate(void) override;

		private:
			TargetFunction m_target { nullptr };
	};

	// ============================================================================
	// DfsBrowserWindow — the files of a DFS partition (of a Disk Manager disk).
	//
	// The tree shows the directories and files (double-click a directory to open
	// it), the panel on the right the selected one's properties. Files go in with
	// Import (files or a folder) or by dropping them on the tree (on a directory:
	// into it), and out with Extract (a directory with everything in it). New
	// folder / Rename use the name field; Delete asks for a second click.
	//
	// Every change is written to the disk at once (on a machine's disk: an edit in
	// the debugger's history). The file system is mounted for each operation only.
	// ============================================================================
	class DfsBrowserWindow : public ogfx::gui::SubWindow
	{
		public: struct Node
		{
			String path { "" };
			storage::DfsVolume::Entry entry;
		};

		public:
			DfsBrowserWindow(ogfx::gui::Window& window, PartitionManagerWindow& manager);
			void build(void);
			// The partition (start / size on the document's disk) to browse.
			void openFor(DiskDocument* document, u64 start, u64 size, const String& name);
			// The document went away / changed under the browser.
			void documentChanged(DiskDocument* document);
			inline bool isBrowsing(const DiskDocument* document, u64 start) const { return isVisible() && m_document == document && m_start == start; }
			void onUpdate(void) override;
			// Reads the tree again (keeps the selection and the open directories).
			void reloadTree(void);

		private:
			// Runs a read only operation on the mounted volume.
			dfs::i32 read_op(const std::function<dfs::i32(storage::DfsVolume& vol)>& op, String& outError);
			// Runs an operation that writes, then saves what it wrote to the disk.
			dfs::i32 write_op(const std::function<dfs::i32(storage::DfsVolume& vol)>& op, String& outError);
			void rebuild_tree(const stdvec<Node>& nodes, const String& selectPath);
			const Node* selected_node(void) const;
			// The directory new files go to: the selected one, or the selected file's.
			String target_dir(void) const;
			void show_details(void);
			void set_status(const String& text, const Color& color);
			void new_folder(void);
			void rename(void);
			void remove(void);
			void import_files(void);
			void import_folder(void);
			void import_paths(const stdvec<String>& hostPaths, const String& dir);
			void extract(void);
			void on_drop(const ogfx::gui::Event& event, bool onTree);
			void show_context_menu(const Vec2& position);
			ogfx::gui::TreeView::Item* item_for_path(const String& path);

		private:
			PartitionManagerWindow& m_manager;
			DiskDocument* m_document { nullptr };
			u64 m_start { 0 };
			u64 m_size { 0 };
			String m_name { "" };
			stdvec<Node> m_nodes;			// the tree items' user data: an index in here
			String m_fsSummary { "" };
			u64 m_confirmDeleteUntil { 0 };
			String m_confirmDeletePath { "" };
			// Dropped files (one event per file), imported together on the next update.
			stdvec<String> m_drops;
			String m_dropDir { "" };
			bool m_placed { false };

			ogfx::gui::Button m_refreshButton { getWindow() };
			ogfx::gui::Button m_importButton { getWindow() };
			ogfx::gui::Button m_importFolderButton { getWindow() };
			ogfx::gui::Button m_extractButton { getWindow() };
			ogfx::gui::Button m_deleteButton { getWindow() };
			MessageLine m_pathLine { getWindow() };
			FileTreeView m_tree { getWindow() };
			GroupFrame m_detailsFrame { getWindow() };
			InfoTable m_details { getWindow() };
			FieldLabel m_nameTitle { getWindow() };
			FieldInput m_nameInput { getWindow() };
			ogfx::gui::Button m_newFolderButton { getWindow() };
			ogfx::gui::Button m_renameButton { getWindow() };
			MessageLine m_statusLine { getWindow() };

		public:
			inline static constexpr u64 ConfirmMs { 4000 };
			inline static constexpr u32 MaxNodes { 50000 };
	};
}
