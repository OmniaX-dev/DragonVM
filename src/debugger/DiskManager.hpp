#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/Widgets.hpp>
#include "../storage/PartitionTable.hpp"
#include "DebuggerWidgets.hpp"
#include "DiskWidgets.hpp"
#include "PartitionManager.hpp"

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
			// Opens the Partition Manager for the current disk.
			void openPartitionManager(void);
			// After a tool (partition editor, format, DFS browser) wrote to the document's
			// disk: an edit in the history (machine drives), the view refreshed. what goes
			// to the status line (when not empty).
			void diskWritten(DiskDocument& document, const String& what);

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
