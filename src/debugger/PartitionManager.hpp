#pragma once

#include "DiskWidgets.hpp"
#include "DfsBrowser.hpp"
#include "../storage/PartitionTable.hpp"
#include "../storage/DfsVolume.hpp"
#include <deque>

namespace dragon
{
	class DiskDocument;
	class DiskManagerWindow;

	// The partitions of a table as a bar (GParted style): the reserved areas, the
	// partitions and the free space, in address order, each at least MinSegmentWidth
	// wide. A partition's file system usage is drawn as a darker part. Clicking
	// selects a partition (or a free area, for a new partition).
	class DiskMapView : public ogfx::gui::Widget
	{
		public: struct Segment
		{
			enum class eKind : u8 { Reserved = 0, Partition, Free };
			eKind kind { eKind::Free };
			u64 start { 0 };
			u64 size { 0 };
			i32 partition { -1 };
			String name { "" };
			String detail { "" };		// e.g. the file system
			f32 used { -1 };			// the used fraction (< 0: unknown)
		};
		public: using SelectCallback = std::function<void(const Segment& segment)>;

		public:
			DiskMapView(ogfx::gui::Window& window);
			void setLayout(const storage::PartitionScheme* scheme, const storage::tPartitionTable* table, u64 diskSize);
			// After setLayout(): a partition's file system name and used fraction.
			void setPartitionDetail(i32 partition, const String& detail, f32 used);
			inline void setSelectCallback(SelectCallback callback) { m_onSelect = std::move(callback); }
			inline void setSelectedPartition(i32 index) { m_selectedPartition = index; m_selectedFree = -1; }
			inline void setSelectedFree(u64 start) { m_selectedPartition = -1; m_selectedFree = cast<i64>(start); }
			inline const stdvec<Segment>& getSegments(void) const { return m_segments; }

			void onDraw(ogfx::BasicRenderer2D& gfx) override;
			void onMousePressed(const ogfx::gui::Event& event) override;
			void onMouseMoved(const ogfx::gui::Event& event) override;
			void onMouseExited(const ogfx::gui::Event& event) override;

			static Color partitionColor(i32 index);

		private:
			// The segments' x ranges (global).
			stdvec<Rectangle> segment_rects(void) const;
			i32 segment_at(const Vec2& position) const;

		private:
			stdvec<Segment> m_segments;
			u64 m_diskSize { 0 };
			i32 m_selectedPartition { -1 };
			i64 m_selectedFree { -1 };
			i32 m_hovered { -1 };
			SelectCallback m_onSelect { nullptr };

		public:
			inline static constexpr f32 MinSegmentWidth { 56 };
	};

	// What a partition holds, read from the disk (the Disk Manager's copy).
	struct PartitionContent
	{
		String fileSystem { "-" };	// "DFS", "unknown", "empty" (zeros), "-" (none: outside the disk)
		bool dfs { false };
		bool readable { false };	// DFS: mounted
		String error { "" };		// DFS: why it can't be mounted
		String label { "" };
		String uuid { "" };
		dfs::Superblock sb {};
		dfs::Layout layout {};
		u64 fsSize { 0 };			// the file system's size (may be less than the partition's)
		u64 usedBytes { 0 };
		u64 freeBytes { 0 };
		u32 minSize { 0 };			// the smallest size it can shrink to
		bool bootHeader { false };	// the partition starts with the DPT boot header (F1 C4)
		bool hasKernel { false };	// DFS: /boot/kernel0 is a file
		u32 kernelSize { 0 };

		inline f32 usedFraction(void) const { return fsSize == 0 ? -1.0f : cast<f32>(cast<f64>(usedBytes) / cast<f64>(fsSize)); }
	};

	// The partitions of the table being edited.
	class PartitionListView : public DebugListView
	{
		public: struct RowInfo
		{
			String fileSystem { "" };
			String used { "" };
			Color color { DebugListView::DimTextColor };
			bool pending { false };	// changed since the table was read
		};
		public: using RowCallback = std::function<void(i32 row)>;
		public:
			PartitionListView(ogfx::gui::Window& window);
			inline void setTable(const storage::PartitionScheme* scheme, const storage::tPartitionTable* table, const stdvec<RowInfo>* rows) { m_scheme = scheme; m_table = table; m_rows = rows; }
			inline void setSelectCallback(RowCallback callback) { m_onSelect = std::move(callback); }
			inline void setActivateCallback(RowCallback callback) { m_onActivate = std::move(callback); }

			i32 getRowCount(void) const override;
			void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) override;
			void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) override;
			String getEmptyText(void) const override;

		private:
			const storage::PartitionScheme* m_scheme { nullptr };
			const storage::tPartitionTable* m_table { nullptr };
			const stdvec<RowInfo>* m_rows { nullptr };
			RowCallback m_onSelect { nullptr };
			RowCallback m_onActivate { nullptr };
	};

	class PartitionManagerWindow;

	// ============================================================================
	// DfsFormatWindow — formats a partition as DFS: label, block size, bytes per
	// inode (the resulting layout is shown as they change). Format asks for a
	// second click; the file system is written to the disk at once.
	// ============================================================================
	class DfsFormatWindow : public ogfx::gui::SubWindow
	{
		public:
			DfsFormatWindow(ogfx::gui::Window& window, PartitionManagerWindow& manager);
			void build(void);
			void openFor(DiskDocument* document, i32 partition, const storage::tPartition& part, const PartitionContent& content);
			void onUpdate(void) override;

		private:
			void update_preview(void);
			void format(void);
			u32 block_size(void) const;
			u32 bytes_per_inode(void) const;

		private:
			PartitionManagerWindow& m_manager;
			DiskDocument* m_document { nullptr };
			i32 m_partition { -1 };
			storage::tPartition m_part;
			u64 m_confirmUntil { 0 };
			bool m_placed { false };

			MessageLine m_targetLine { getWindow() };
			FieldLabel m_labelTitle { getWindow() };
			FieldInput m_labelInput { getWindow() };
			FieldLabel m_blockTitle { getWindow() };
			ogfx::gui::ComboBox m_blockCombo { getWindow() };
			FieldLabel m_inodeTitle { getWindow() };
			ogfx::gui::ComboBox m_inodeCombo { getWindow() };
			InfoTable m_preview { getWindow() };
			MessageLine m_warningLine { getWindow() };
			ogfx::gui::Button m_cancelButton { getWindow() };
			ogfx::gui::Button m_formatButton { getWindow() };

		public:
			inline static constexpr u64 ConfirmMs { 4000 };
	};

	// ============================================================================
	// PartitionManagerWindow — the partition editor of the Disk Manager's disk.
	//
	// The table is edited as a copy (the pending changes): the bar and the list
	// show it, the Partition group edits the selected partition (start / size
	// accept 0x hex, decimal and K / M / G sizes). "New table" starts an empty table
	// of the chosen format. The File system group shows what the selected partition
	// holds (DFS: usage, layout, state), and formats, checks or browses it.
	//
	// Apply writes the table (after a second click). With "Move and resize
	// contents", a partition whose start changed has its data moved, and a DFS
	// partition whose size changed has its file system resized with it (shrinking
	// can't go below what the files use); the pending operations are listed before.
	// Everything is done on a copy of the disk first, so a failure changes nothing.
	// ============================================================================
	class PartitionManagerWindow : public ogfx::gui::SubWindow
	{
		public: struct Operation
		{
			String text { "" };
			Color color { DebugListView::TextColor };
			bool blocking { false };	// Apply is disabled
		};

		public:
			PartitionManagerWindow(ogfx::gui::Window& window, DiskManagerWindow& diskManager);
			void build(void);
			// Shows the Disk Manager's current disk (re-reading its table).
			void openFor(DiskDocument* document);
			// The disk was closed / changed under the editor.
			void documentChanged(DiskDocument* document);
			// The disk's content changed (formatted, written by the browser): re-reads the
			// partitions' contents, keeps the pending table.
			void contentChanged(DiskDocument& document);
			void onUpdate(void) override;
			inline DiskManagerWindow& getDiskManager(void) { return m_diskManager; }
			// Writes the given ranges of a scratch copy of the document's buffer to the
			// document and the disk.
			bool commitScratch(DiskDocument& document, const stdvec<u8>& scratch, const stdvec<std::pair<u64, u64>>& ranges, String& outError);

		private:
			void load_table(void);
			void read_contents(void);
			void refresh_view(void);
			void select_partition(i32 index);
			void fill_fields(void);
			void commit_field(FieldInput& field);
			void new_partition(void);
			void delete_partition(void);
			void grow_partition(void);
			void new_table(void);
			void apply(void);
			bool is_pending(void) const;
			// The selected partition is on the disk as it is in the table (formatting and
			// browsing need that).
			bool is_on_disk(i32 index) const;
			void set_message(const String& text, const Color& color);
			// The largest free area (or the selected one).
			bool find_free_area(u64& outStart, u64& outSize) const;
			stdvec<Operation> plan_operations(void) const;
			void update_fs_group(void);
			void check_file_system(void);
			void open_browser(void);
			void open_format(void);
			const PartitionContent* content_of(i32 index) const;

		private:
			DiskManagerWindow& m_diskManager;
			DiskDocument* m_document { nullptr };
			const storage::PartitionScheme* m_scheme { nullptr };
			storage::tPartitionTable m_table;
			storage::tPartitionTable m_original;
			// For each partition of m_table: its index in m_original (-1: new).
			stdvec<i32> m_origin;
			// For each partition of m_original: what it holds.
			stdvec<PartitionContent> m_contents;
			stdvec<PartitionListView::RowInfo> m_rows;
			bool m_hasOriginal { false };
			bool m_moveContents { true };
			i32 m_selected { -1 };
			i64 m_selectedFree { -1 };
			// The partition the fields show (a field can be committed after the selection moved).
			i32 m_fieldsFor { -1 };
			bool m_fillingFields { false };
			String m_note { "" };
			Color m_noteColor { DebugListView::TextColor };
			u64 m_confirmUntil { 0 };
			bool m_placed { false };

			// Header
			MessageLine m_infoLine { getWindow() };
			ogfx::gui::ComboBox m_schemeCombo { getWindow() };
			ogfx::gui::Button m_newTableButton { getWindow() };
			DiskMapView m_map { getWindow() };
			PartitionListView m_list { getWindow() };
			// Partition group
			GroupFrame m_partFrame { getWindow() };
			FieldLabel m_labelTitle { getWindow() };
			FieldInput m_labelInput { getWindow() };
			FieldLabel m_startTitle { getWindow() };
			FieldInput m_startInput { getWindow() };
			FieldLabel m_sizeTitle { getWindow() };
			FieldInput m_sizeInput { getWindow() };
			ogfx::gui::Button m_growButton { getWindow() };
			FieldLabel m_flagsTitle { getWindow() };
			std::deque<ogfx::gui::CheckBox> m_flagChecks;
			MessageLine m_partInfo { getWindow() };
			ogfx::gui::Button m_newButton { getWindow() };
			ogfx::gui::Button m_deleteButton { getWindow() };
			// File system group
			GroupFrame m_fsFrame { getWindow() };
			UsageBar m_usageBar { getWindow() };
			InfoTable m_fsInfo { getWindow() };
			ogfx::gui::Button m_formatButton { getWindow() };
			ogfx::gui::Button m_checkButton { getWindow() };
			ogfx::gui::Button m_browseButton { getWindow() };
			// Pending operations
			GroupFrame m_opsFrame { getWindow() };
			MessageLine m_messageLine { getWindow() };
			InfoTable m_opsList { getWindow() };
			ogfx::gui::CheckBox m_contentsCheck { getWindow() };
			ogfx::gui::Button m_revertButton { getWindow() };
			ogfx::gui::Button m_applyButton { getWindow() };

			DfsFormatWindow m_formatWindow;
			DfsBrowserWindow m_browser;

		public:
			inline static constexpr u64 ConfirmMs { 4000 };
			// The Partition / File system groups' top (from the bottom of the window).
			inline static constexpr f32 GroupTop { -434 };
	};
}
