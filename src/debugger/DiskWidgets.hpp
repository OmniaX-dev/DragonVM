#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/Widgets.hpp>
#include "../storage/Disk.hpp"
#include "DebuggerWidgets.hpp"

// The widgets and helpers the disk tools share (Disk Manager, Partition Manager,
// DFS browser).

namespace dragon
{
	class FieldLabel;

	namespace disk_ui
	{
		inline const Color ErrorColor { 240, 100, 100 };
		inline const Color SuccessColor { 120, 215, 120 };
		inline const Color WarningColor { 235, 200, 90 };
		inline const Color DimColor { 130, 140, 130 };
		inline const Color AccentColor { 90, 170, 235 };
		inline const Color DirectoryColor { 110, 180, 245 };
		inline const Color ValueColor { 220, 225, 220 };

		void place(ogfx::gui::Widget& widget, f32 anchorLeft, f32 anchorTop, f32 anchorRight, f32 anchorBottom, f32 left, f32 top, f32 right, f32 bottom);
		void setupButton(ogfx::gui::Button& button, const String& text, const String& tooltip);
		void setupTitle(ogfx::gui::Label& label, const String& text);
		void setupTitle(FieldLabel& label, const String& text);
		u64 nowMs(void);
		bool isError(const Color& color);
		// The first time (placed is false): sizes a SubWindow to its preferred size (at most
		// the area between the window's bars, less a margin) and centers it. Afterwards
		// only when it doesn't fit anymore. Call it after open().
		void fitSubWindow(ogfx::gui::SubWindow& subWindow, f32 width, f32 height, bool& placed);
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
			// The written ranges, sorted and merged.
			stdvec<std::pair<u64, u64>> getRanges(void) const;

		private:
			storage::Disk& m_disk;
			stdvec<std::pair<u64, u64>> m_ranges;
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

	// A field's title: left aligned, vertically centered (place it like the field).
	class FieldLabel : public ogfx::gui::Widget
	{
		public:
			FieldLabel(ogfx::gui::Window& window);
			inline void setText(const String& text) { m_text = text; }
			inline const String& getText(void) const { return m_text; }
			void onDraw(ogfx::BasicRenderer2D& gfx) override;

		private:
			String m_text { "" };
	};

	// A titled frame drawn behind a group of widgets (they are siblings, added after it).
	class GroupFrame : public ogfx::gui::Widget
	{
		public:
			GroupFrame(ogfx::gui::Window& window);
			inline void setTitle(const String& title) { m_title = title; }
			// Shown right of the title, dimmed.
			inline void setSubtitle(const String& subtitle) { m_subtitle = subtitle; }
			void onDraw(ogfx::BasicRenderer2D& gfx) override;

		private:
			String m_title { "" };
			String m_subtitle { "" };

		public:
			inline static constexpr f32 TitleHeight { 26 };
			inline static const Color FrameColor { 24, 27, 24 };
			inline static const Color BorderColor { 52, 60, 52 };
			inline static const Color TitleColor { 230, 200, 90 };
	};

	// A horizontal bar: the used fraction filled, a text in it.
	class UsageBar : public ogfx::gui::Widget
	{
		public:
			UsageBar(ogfx::gui::Window& window);
			// fraction in [0, 1]; a second fraction (e.g. what a change adds) drawn after it.
			inline void setValue(f32 fraction, const String& text, const Color& color = disk_ui::AccentColor) { m_fraction = fraction; m_text = text; m_color = color; m_extra = 0; }
			inline void setExtra(f32 fraction, const Color& color) { m_extra = fraction; m_extraColor = color; }
			void onDraw(ogfx::BasicRenderer2D& gfx) override;

		private:
			f32 m_fraction { 0 };
			f32 m_extra { 0 };
			String m_text { "" };
			Color m_color { disk_ui::AccentColor };
			Color m_extraColor { disk_ui::WarningColor };
	};

	// Name / value rows (two columns), e.g. a file's or a file system's properties.
	class InfoTable : public ogfx::gui::Widget
	{
		public: struct Row
		{
			String name { "" };
			String value { "" };
			Color color { disk_ui::ValueColor };
		};
		public:
			InfoTable(ogfx::gui::Window& window);
			inline void setRows(const stdvec<Row>& rows) { m_rows = rows; }
			inline void clearRows(void) { m_rows.clear(); }
			inline void addRow(const String& name, const String& value, const Color& color = disk_ui::ValueColor) { m_rows.push_back({ name, value, color }); }
			inline void setNameWidth(f32 width) { m_nameWidth = width; }
			inline void setEmptyText(const String& text) { m_emptyText = text; }
			void onDraw(ogfx::BasicRenderer2D& gfx) override;

		private:
			stdvec<Row> m_rows;
			f32 m_nameWidth { 120 };
			String m_emptyText { "" };

		public:
			inline static constexpr i32 TextSize { 14 };
	};
}
