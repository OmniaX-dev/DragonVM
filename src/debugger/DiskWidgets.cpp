#include "DiskWidgets.hpp"
#include <algorithm>
#include <chrono>

namespace dragon
{
	using namespace ogfx::gui;

	namespace disk_ui
	{
		void place(Widget& widget, f32 anchorLeft, f32 anchorTop, f32 anchorRight, f32 anchorBottom, f32 left, f32 top, f32 right, f32 bottom)
		{
			widget.setAnchors(anchorLeft, anchorTop, anchorRight, anchorBottom);
			widget.setOffsets(left, top, right, bottom);
		}

		void setupButton(Button& button, const String& text, const String& tooltip)
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

		void setupTitle(Label& label, const String& text)
		{
			label.setText(text);
			label.setFontSize(14);
			label.setSizeMode(eSizeMode::Manual);
		}

		void setupTitle(FieldLabel& label, const String& text)
		{
			label.setText(text);
		}

		u64 nowMs(void)
		{
			return cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		void fitSubWindow(SubWindow& subWindow, f32 width, f32 height, bool& placed)
		{
			const Rectangle area = subWindow.getWindow().getRootWidget().getGlobalBounds();
			const Rectangle current = subWindow.getGlobalBounds();
			if (placed && current.w <= area.w && current.h <= area.h)
				return;
			placed = true;
			const f32 w = std::max(320.0f, std::min(width, area.w - 24));
			const f32 h = std::max(240.0f, std::min(height, area.h - 24));
			// Relative to the root widget (call it after open()).
			subWindow.setBounds({ (area.w - w) * 0.5f, (area.h - h) * 0.5f, w, h });
		}

		bool isError(const Color& color)
		{
			return color.r == ErrorColor.r && color.g == ErrorColor.g && color.b == ErrorColor.b;
		}
	}




	//RecordingDisk
	stdvec<std::pair<u64, u64>> RecordingDisk::getRanges(void) const
	{
		stdvec<std::pair<u64, u64>> sorted = m_ranges;
		std::sort(sorted.begin(), sorted.end());
		stdvec<std::pair<u64, u64>> merged;
		for (const auto& [address, size] : sorted)
		{
			if (!merged.empty() && address <= merged.back().first + merged.back().second)
			{
				const u64 end = std::max(merged.back().first + merged.back().second, address + size);
				merged.back().second = end - merged.back().first;
			}
			else
				merged.push_back({ address, size });
		}
		return merged;
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




	//FieldLabel
	FieldLabel::FieldLabel(Window& window) : Widget({ 0, 0, 0, 0 }, window)
	{
		setTypeName("dragon::FieldLabel");
		setStylesheetCategoryName("dragonFieldLabel");
		disableFocus();
		disableChildren();
		validate();
	}

	void FieldLabel::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		const Rectangle b = getGlobalBounds();
		const Vec2 size = gfx.getStringDimensions(m_text, 15);
		gfx.pushClippingRect(b, true);
		gfx.drawString(m_text, { b.x, b.y + (b.h - size.y) * 0.5f }, Color(200, 205, 200), 15);
		gfx.popClippingRect();
	}




	//GroupFrame
	GroupFrame::GroupFrame(Window& window) : Widget({ 0, 0, 0, 0 }, window)
	{
		setTypeName("dragon::GroupFrame");
		setStylesheetCategoryName("dragonGroupFrame");
		disableFocus();
		disableChildren();
		validate();
	}

	void GroupFrame::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		const Rectangle b = getGlobalBounds();
		gfx.fillRect(b, FrameColor);
		gfx.fillRect({ b.x, b.y, b.w, TitleHeight }, Color(30, 34, 30));
		gfx.drawLine({ Vec2 { b.x, b.y + TitleHeight }, Vec2 { b.x + b.w, b.y + TitleHeight } }, BorderColor, 1);
		gfx.drawRect(b, BorderColor, 1);
		gfx.pushClippingRect({ b.x + 4, b.y, std::max(0.0f, b.w - 8), TitleHeight }, true);
		const f32 textY = b.y + (TitleHeight - gfx.getStringDimensions("Ag", 14).y) * 0.5f;
		gfx.drawString(m_title, { b.x + 10, textY }, TitleColor, 14);
		if (m_subtitle != "")
		{
			const f32 w = gfx.getStringDimensions(m_title, 14).x;
			gfx.drawString(m_subtitle, { b.x + 10 + w + 12, textY }, disk_ui::DimColor, 14);
		}
		gfx.popClippingRect();
	}




	//UsageBar
	UsageBar::UsageBar(Window& window) : Widget({ 0, 0, 0, 0 }, window)
	{
		setTypeName("dragon::UsageBar");
		setStylesheetCategoryName("dragonUsageBar");
		disableFocus();
		disableChildren();
		validate();
	}

	void UsageBar::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		const Rectangle b = getGlobalBounds();
		gfx.fillRect(b, Color(14, 16, 14));
		const f32 used = std::clamp(m_fraction, 0.0f, 1.0f);
		const f32 extra = std::clamp(m_extra, 0.0f, 1.0f - used);
		if (used > 0)
			gfx.fillRect({ b.x, b.y, b.w * used, b.h }, Color(m_color.r, m_color.g, m_color.b, 170));
		if (extra > 0)
			gfx.fillRect({ b.x + b.w * used, b.y, b.w * extra, b.h }, Color(m_extraColor.r, m_extraColor.g, m_extraColor.b, 120));
		gfx.drawRect(b, GroupFrame::BorderColor, 1);
		gfx.pushClippingRect(b, true);
		const Vec2 size = gfx.getStringDimensions(m_text, 13);
		gfx.drawString(m_text, { b.x + (b.w - size.x) * 0.5f, b.y + (b.h - size.y) * 0.5f }, Color(240, 240, 240), 13);
		gfx.popClippingRect();
	}




	//InfoTable
	InfoTable::InfoTable(Window& window) : Widget({ 0, 0, 0, 0 }, window)
	{
		setTypeName("dragon::InfoTable");
		setStylesheetCategoryName("dragonInfoTable");
		disableFocus();
		disableChildren();
		validate();
	}

	void InfoTable::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		const Rectangle b = getGlobalBounds();
		gfx.pushClippingRect(b, true);
		const f32 lineHeight = gfx.getStringDimensions("Ag", TextSize).y + 5;
		if (m_rows.empty() && m_emptyText != "")
			gfx.drawString(m_emptyText, { b.x + 2, b.y + 2 }, disk_ui::DimColor, TextSize);
		f32 y = b.y + 2;
		const i32 fit = std::max(1, cast<i32>((b.h - 2) / lineHeight));
		for (i32 i = 0; i < cast<i32>(m_rows.size()); i++)
		{
			const Row& row = m_rows[i];
			// The last line that fits says how many more there are.
			if (i == fit - 1 && cast<i32>(m_rows.size()) > fit)
			{
				gfx.drawString(String("... ").add(cast<i32>(m_rows.size()) - i).add(" more"), { b.x + 2, y }, disk_ui::DimColor, TextSize);
				break;
			}
			gfx.drawString(row.name, { b.x + 2, y }, disk_ui::DimColor, TextSize);
			gfx.pushClippingRect({ b.x + m_nameWidth, y, std::max(0.0f, b.w - m_nameWidth), lineHeight }, true);
			gfx.drawString(row.value, { b.x + m_nameWidth, y }, row.color, TextSize);
			gfx.popClippingRect();
			y += lineHeight;
		}
		gfx.popClippingRect();
	}
}
