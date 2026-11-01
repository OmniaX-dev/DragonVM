#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/Widgets.hpp>
#include "../storage/PartitionTable.hpp"
#include "DebuggerWidgets.hpp"

namespace dragon
{
	namespace hw { class VirtualHardDrive; }
	class Debugger;

	// ============================================================================
	// DiskDocument — a disk open in the Disk Manager: one of the machine's drives
	// or an image file. The whole disk is copied in a buffer, which the editors
	// change (the changed bytes are tracked); save() writes the changed bytes to
	// the disk.
	//
	// A machine drive is read and written through the drive (the machine sees the
	// writes, the debugger's history journals them). The machine can write to it
	// meanwhile: sync() re-reads the bytes that weren't changed here.
	// ============================================================================
	class DiskDocument
	{
		public:
			static std::unique_ptr<DiskDocument> openDrive(i32 driveID, hw::VirtualHardDrive& drive, const String& path, String& outError);
			static std::unique_ptr<DiskDocument> openFile(const String& path, String& outError);

			// Re-reads the whole disk (the changes are lost).
			bool reload(String& outError);
			// Machine drives: re-reads the bytes not changed here, if the drive was written
			// since. Returns whether the buffer changed.
			bool sync(void);
			bool save(String& outError);
			// Writes a range of the buffer to the disk now (and forgets its changes).
			bool saveRange(u64 address, u64 size, String& outError);
			void markChanged(u64 address, u64 size);

			inline bool isMachineDrive(void) const { return m_driveID >= 0; }
			inline i32 getDriveID(void) const { return m_driveID; }
			inline const String& getPath(void) const { return m_path; }
			String getName(void) const;
			inline u64 getSize(void) const { return m_buffer.size(); }
			inline stdvec<u8>& getBuffer(void) { return m_buffer; }
			inline u64 getChangedCount(void) const { return m_changedCount; }
			inline bool isChanged(u64 address) const { return address < m_changed.size() && m_changed[address]; }
			// The disk itself.
			inline storage::Disk& getDisk(void) { return *m_disk; }
			// The buffer as a disk: writes to it are marked as changes.
			inline storage::MemoryDisk& getBufferDisk(void) { return *m_bufferDisk; }

		private:
			DiskDocument(void) = default;
			void init_buffer_disk(void);
			void clear_changes(u64 from, u64 to);

		private:
			i32 m_driveID { -1 };
			hw::VirtualHardDrive* m_drive { nullptr };
			String m_path { "" };
			std::unique_ptr<storage::Disk> m_disk;
			std::unique_ptr<storage::MemoryDisk> m_bufferDisk;
			stdvec<u8> m_buffer;
			std::vector<bool> m_changed;
			u64 m_changedCount { 0 };
			u64 m_changedMin { 0 };
			u64 m_changedMax { 0 };		// exclusive
			u64 m_seenWriteCount { 0 };
	};

	// The partitions of a table as a bar (GParted style): the reserved areas, the
	// partitions and the free space, in address order, each at least MinSegmentWidth
	// wide. Clicking selects a partition (or a free area, for a new partition).
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
		};
		public: using SelectCallback = std::function<void(const Segment& segment)>;

		public:
			DiskMapView(ogfx::gui::Window& window);
			void setLayout(const storage::PartitionScheme* scheme, const storage::tPartitionTable* table, u64 diskSize);
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
			inline static constexpr f32 MinSegmentWidth { 46 };
	};

	// The partitions of the table being edited.
	class PartitionListView : public DebugListView
	{
		public: using RowCallback = std::function<void(i32 row)>;
		public:
			PartitionListView(ogfx::gui::Window& window);
			inline void setTable(const storage::PartitionScheme* scheme, const storage::tPartitionTable* table, const stdvec<String>* fileSystems) { m_scheme = scheme; m_table = table; m_fileSystems = fileSystems; }
			inline void setSelectCallback(RowCallback callback) { m_onSelect = std::move(callback); }

			i32 getRowCount(void) const override;
			void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) override;
			void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) override;
			String getEmptyText(void) const override;

		private:
			const storage::PartitionScheme* m_scheme { nullptr };
			const storage::tPartitionTable* m_table { nullptr };
			const stdvec<String>* m_fileSystems { nullptr };
			RowCallback m_onSelect { nullptr };
	};

	// A text field that reports its value on Return and when it loses the focus.
	class FieldInput : public ogfx::gui::TextEdit
	{
		public: using CommitCallback = std::function<void(FieldInput& field)>;
		public:
			inline FieldInput(ogfx::gui::Window& window) : TextEdit(window) { setTypeName("dragon::FieldInput"); }
			inline void setCommitCallback(CommitCallback callback) { m_onCommit = std::move(callback); }
			void onKeyPressed(const ogfx::gui::Event& event) override;
			void onFocusLost(const ogfx::gui::Event& event) override;

		private:
			CommitCallback m_onCommit { nullptr };
	};

	class DiskManagerWindow;

	// ============================================================================
	// PartitionManagerWindow — the partition editor of the Disk Manager's disk.
	//
	// The table is edited as a copy (the pending changes): the bar and the list
	// show it, the fields edit the selected partition (start / size accept 0x hex,
	// decimal and K / M / G sizes). "New table" starts an empty table of the
	// chosen format. Apply checks the table (PartitionScheme::validate()) and,
	// after a second click, writes it to the disk.
	// ============================================================================
	class PartitionManagerWindow : public ogfx::gui::SubWindow
	{
		public:
			PartitionManagerWindow(ogfx::gui::Window& window, DiskManagerWindow& diskManager);
			void build(void);
			// Shows the Disk Manager's current disk (re-reading its table).
			void openFor(DiskDocument* document);
			// The disk was closed / changed under the editor.
			void documentChanged(DiskDocument* document);
			void onUpdate(void) override;

		private:
			void load_table(void);
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
			void set_message(const String& text, const Color& color);
			// The largest free area (or the selected one).
			bool find_free_area(u64& outStart, u64& outSize) const;

		private:
			DiskManagerWindow& m_diskManager;
			DiskDocument* m_document { nullptr };
			const storage::PartitionScheme* m_scheme { nullptr };
			storage::tPartitionTable m_table;
			storage::tPartitionTable m_original;
			bool m_hasOriginal { false };
			stdvec<String> m_fileSystems;
			i32 m_selected { -1 };
			i64 m_selectedFree { -1 };
			// The partition the fields show (a field can be committed after the selection moved).
			i32 m_fieldsFor { -1 };
			bool m_fillingFields { false };
			String m_note { "" };
			Color m_noteColor { DebugListView::TextColor };
			u64 m_confirmUntil { 0 };

			MessageLine m_infoLine { getWindow() };
			ogfx::gui::ComboBox m_schemeCombo { getWindow() };
			ogfx::gui::Button m_newTableButton { getWindow() };
			DiskMapView m_map { getWindow() };
			PartitionListView m_list { getWindow() };
			ogfx::gui::Label m_labelTitle { getWindow() };
			FieldInput m_labelInput { getWindow() };
			ogfx::gui::Label m_startTitle { getWindow() };
			FieldInput m_startInput { getWindow() };
			ogfx::gui::Label m_sizeTitle { getWindow() };
			FieldInput m_sizeInput { getWindow() };
			ogfx::gui::Button m_growButton { getWindow() };
			std::deque<ogfx::gui::CheckBox> m_flagChecks;
			ogfx::gui::Button m_newButton { getWindow() };
			ogfx::gui::Button m_deleteButton { getWindow() };
			MessageLine m_messageLine { getWindow() };
			ogfx::gui::Button m_revertButton { getWindow() };
			ogfx::gui::Button m_applyButton { getWindow() };

		public:
			inline static constexpr u64 ConfirmMs { 4000 };
	};

	// ============================================================================
	// DiskManagerWindow — the machine's disks and other disk images.
	//
	// The combo box lists the machine's drives and the image files opened here
	// (Open / New); the hex editor edits the selected disk's copy (the reserved
	// areas and the partitions highlighted), Save writes the changed bytes to the
	// disk (for a machine drive: an edit in the history, so it can be undone),
	// Reload drops the changes. "Load binary" copies a file into the copy at the
	// address (saved with Save). "Partitions" opens the partition editor.
	// ============================================================================
	class DiskManagerWindow : public ogfx::gui::SubWindow
	{
		public:
			DiskManagerWindow(ogfx::gui::Window& window, Debugger& debugger);
			void build(void);
			// Once the machine is loaded: lists its drives.
			void start(void);
			// While open: follows the machine's writes to the shown drive.
			void syncWithMachine(void);
			inline Debugger& getDebugger(void) { return m_debugger; }
			inline DiskDocument* getDocument(void) { return current(); }
			// After the partition editor wrote to the document's disk.
			void tableWritten(DiskDocument& document);

		private:
			void add_machine_drives(void);
			void update_combo(void);
			void select(i32 index);
			void show_document(bool keepView);
			void update_info(void);
			void open_file(void);
			void new_disk(void);
			void close_file(void);
			void save(void);
			void reload(void);
			void load_binary(void);
			void set_status(const String& text, const Color& color);
			DiskDocument* current(void);
			String default_folder(void) const;

		private:
			Debugger& m_debugger;
			stdvec<std::unique_ptr<DiskDocument>> m_documents;
			i32 m_current { -1 };
			bool m_built { false };

			ogfx::gui::ComboBox m_diskCombo { getWindow() };
			ogfx::gui::Button m_openButton { getWindow() };
			ogfx::gui::Button m_closeButton { getWindow() };
			ogfx::gui::Label m_newSizeTitle { getWindow() };
			FieldInput m_newSizeInput { getWindow() };
			ogfx::gui::Button m_newButton { getWindow() };
			ogfx::gui::Button m_saveButton { getWindow() };
			ogfx::gui::Button m_reloadButton { getWindow() };
			ogfx::gui::Label m_loadAddressTitle { getWindow() };
			FieldInput m_loadAddressInput { getWindow() };
			ogfx::gui::Button m_loadButton { getWindow() };
			ogfx::gui::Button m_partitionsButton { getWindow() };
			MessageLine m_infoLine { getWindow() };
			MessageLine m_statusLine { getWindow() };
			ogfx::gui::HexEditor m_hex { getWindow() };
			PartitionManagerWindow m_partitionManager;
	};
}
