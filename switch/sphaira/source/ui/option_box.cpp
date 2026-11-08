#include "ui/option_box.hpp"
#include "ui/option_box_layout.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"

namespace sphaira::ui {

OptionBoxEntry::OptionBoxEntry(const std::string& text, const Vec4& pos)
: m_text{text} {
    m_pos = pos;
    m_text_pos = Vec2{m_pos.x + (m_pos.w / 2.f), m_pos.y + (m_pos.h / 2.f)};
}

auto OptionBoxEntry::Draw(NVGcontext* vg, Theme* theme) -> void {
    if (m_selected) {
        gfx::drawRectOutline(vg, theme, 4.f, m_pos);
        gfx::drawText(vg, m_text_pos, 26.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), m_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    } else {
        gfx::drawText(vg, m_text_pos, 26.f, theme->GetColour(ThemeEntryID_TEXT), m_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
}

auto OptionBoxEntry::Selected(bool enable) -> void {
    m_selected = enable;
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    LayoutMessage();

    auto box = m_pos;
    box.y += m_pos.h - OptionBoxLayout::ButtonHeight;
    box.h = OptionBoxLayout::ButtonHeight;
    m_entries.emplace_back(a, box);

    Setup(0);
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, const Callback& cb, int image, bool own_image)
: OptionBox{message, a, b, 0, cb, image, own_image} {

}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, s64 index, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    LayoutMessage();

    auto box = m_pos;
    box.w /= 2.f;
    box.y += m_pos.h - OptionBoxLayout::ButtonHeight;
    box.h = OptionBoxLayout::ButtonHeight;
    m_entries.emplace_back(a, box);
    box.x += box.w;
    m_entries.emplace_back(b, box);

    Setup(index);
}

OptionBox::~OptionBox() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }
}

auto OptionBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (m_scrollable && touch->is_scroll &&
        (m_touch_scrolling || touch->in_range(m_text_area))) {
        if (!m_touch_scrolling) m_touch_scroll_start = m_scroll_offset;
        m_touch_scrolling = true;
        m_scroll_offset = std::clamp(m_touch_scroll_start +
            static_cast<float>(touch->initial.y) - static_cast<float>(touch->cur.y),
            0.f, m_text_height - m_text_area.h);
    }
    const bool scrolled = m_touch_scrolling || touch->is_scroll;
    if (touch->is_end) m_touch_scrolling = false;

    if (touch->is_clicked && !scrolled) {
        for (s64 i = 0; i < m_entries.size(); i++) {
            auto& e = m_entries[i];
            if (touch->in_range(e.GetPos())) {
                SetIndex(i);
                FireAction(Button::A);
                break;
            }
        }
    }
}

auto OptionBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5);

    nvgSave(vg);
    nvgTextLineHeight(vg, 1.35f);
    if (m_image) {
        const auto body_height = m_pos.h - OptionBoxLayout::ButtonHeight;
        gfx::drawImage(vg, Vec4{m_pos.x + OptionBoxLayout::Padding,
            m_pos.y + (body_height - 150.f) / 2.f, 150.f, 150.f}, m_image, 5);
    }
    // Text is measured and drawn with the same width, font size and alignment.
    // Clip only the message viewport: it can never paint over the buttons.
    nvgIntersectScissor(vg, m_text_area.x - 3, m_text_area.y - 3,
        m_text_area.w + 6, m_text_area.h + 6);
    const auto text_y = m_text_area.y + std::max(0.f, (m_text_area.h - m_text_height) / 2.f)
        - m_text_top - m_scroll_offset;
    gfx::drawTextBox(vg, m_text_area.x, text_y, m_image ? 22.f : 24.f,
        m_text_area.w, theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(),
        (m_image ? NVG_ALIGN_LEFT : NVG_ALIGN_CENTER) | NVG_ALIGN_TOP);
    nvgRestore(vg);

    if (m_scrollable) {
        gfx::drawText(vg, Vec2{m_pos.x + m_pos.w / 2.f, m_spacer_line.y - 15.f}, 16.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Up / Down or drag to scroll",
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    gfx::drawRect(vg, m_spacer_line, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    for (auto&p: m_entries) {
        p.Draw(vg, theme);
    }
}

auto OptionBox::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto OptionBox::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

void OptionBox::LayoutMessage() {
    constexpr float padding = OptionBoxLayout::Padding;
    m_pos.w = 770.f;
    m_text_area.w = m_pos.w - padding * 2 - (m_image ? 150.f + padding : 0.f);
    auto* vg = App::GetVg();
    float bounds[4]{};
    nvgSave(vg);
    nvgFontSize(vg, m_image ? 22.f : 24.f);
    nvgTextLineHeight(vg, 1.35f);
    nvgTextAlign(vg, (m_image ? NVG_ALIGN_LEFT : NVG_ALIGN_CENTER) | NVG_ALIGN_TOP);
    nvgTextBoxBounds(vg, 0, 0, m_text_area.w, m_message.c_str(), nullptr, bounds);
    nvgRestore(vg);
    m_text_top = bounds[1];
    m_text_height = bounds[3] - bounds[1];
    const auto layout = CalculateOptionBoxLayout(m_text_height, m_image != 0);
    m_pos.h = layout.height;
    m_pos.x = (SCREEN_WIDTH - m_pos.w) / 2.f;
    m_pos.y = (SCREEN_HEIGHT - m_pos.h) / 2.f;
    m_text_area.x = m_pos.x + padding + (m_image ? 150.f + padding : 0.f);
    m_text_area.y = m_pos.y + padding;
    m_text_area.h = layout.text_area_height;
    m_scrollable = layout.scrollable;
}

void OptionBox::ScrollMessage(float amount) {
    m_scroll_offset = std::clamp(m_scroll_offset + amount,
        0.f, std::max(0.f, m_text_height - m_text_area.h));
}

auto OptionBox::Setup(s64 index) -> void {
    m_index = std::min<s64>(m_entries.size() - 1, index);
    m_entries[m_index].Selected(true);
    m_spacer_line = Vec4{m_pos.x, m_pos.y + m_pos.h - OptionBoxLayout::ButtonHeight - 2.f, m_pos.w, 2.f};

    SetActions(
        std::make_pair(Button::LEFT, Action{[this](){
            if (m_index) {
                SetIndex(m_index - 1);
            }
        }}),
        std::make_pair(Button::RIGHT, Action{[this](){
            if (m_index < (m_entries.size() - 1)) {
                SetIndex(m_index + 1);
            }
        }}),
        std::make_pair(Button::A, Action{[this](){
            m_callback(m_index);
            SetPop();
        }}),
        std::make_pair(Button::B, Action{[this](){
            m_callback({});
            SetPop();
        }})
    );
    if (m_scrollable) {
        SetAction(Button::UP, Action{[this](){ ScrollMessage(-32.f); }});
        SetAction(Button::DOWN, Action{[this](){ ScrollMessage(32.f); }});
    }
}

void OptionBox::SetIndex(s64 index) {
    if (m_index != index) {
        m_entries[m_index].Selected(false);
        m_index = index;
        m_entries[m_index].Selected(true);
    }
}

} // namespace sphaira::ui
