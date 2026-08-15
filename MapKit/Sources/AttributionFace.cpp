#include <MapKit/AttributionFace.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include <cstdio>
#include <cstring>

namespace MapKit
{

namespace
{

/// The panel is round: at `dy` pixels from the centre the visible chord is
/// 2·sqrt(120² − dy²) wide. Every box below is inside that at its own worst
/// row, which is why they are not simply 240 wide.
///
///   y = 56  → 205 px visible   (body sits in 192)
///   y = 184 → 202 px visible   (body's last row still fits 192)
///   y = 196 → 187 px visible   (hint sits in 144, as TrackFaceMap proved)
constexpr int16_t kBodyY    = 56;
constexpr int16_t kBodyH    = 128;
constexpr int16_t kHintY    = 194;

void setUnicode(touchgfx::Unicode::UnicodeChar* buf, uint16_t cap, const char* text)
{
    touchgfx::Unicode::strncpy(buf, text, cap - 1);
    buf[cap - 1] = 0;
}

} // namespace

bool AttributionFace::sShownThisLaunch = false;

AttributionFace::AttributionFace()
{
    setPosition(0, 0, kSize, kSize);

    // Opaque, because this is a screen and not an overlay: whatever the
    // launcher drew must not show through a licence notice.
    mBackground.setPosition(0, 0, kSize, kSize);
    mBackground.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    add(mBackground);

    for (size_t i = 0; i < kMaxLines; ++i) {
        mLines[i].setPosition(24, static_cast<int16_t>(kBodyY + i * kLineHeight), 192,
                              kLineHeight);
        mLines[i].setTypedText(touchgfx::TypedText(T_TMP_REGULAR_14));
        mLines[i].setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
        mLineBuf[i][0] = 0;
        mLines[i].setWildcard(mLineBuf[i]);
        add(mLines[i]);
    }

    // 144 wide at this row: the chord width TrackFaceMap already established
    // as safe near the bottom of the glass.
    mHint.setPosition(48, kHintY, 144, 18);
    mHint.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_10));
    mHint.setColor(touchgfx::Color::getColorFromRGB(150, 150, 150));
    setUnicode(mHintBuf, sizeof(mHintBuf) / sizeof(mHintBuf[0]), "Press any key");
    mHint.setWildcard(mHintBuf);
    add(mHint);
}

namespace
{

/// Replace the characters the generated fonts do not carry.
///
/// The app fonts are generated from the strings in each app's texts.xml, so
/// they contain the ASCII those strings use and nothing else — `©` (U+00A9)
/// and `·` (U+00B7) are both absent, and they are the only two non-ASCII
/// characters the compliance appendix's attribution strings use. Rendered
/// as-is they come out as gaps, which reads as a mangled credit.
///
/// Transliterating them is a *rendering* substitution, not a rewording: the
/// credit still names every party it named. Regenerating the fonts with the
/// two glyphs would be better and needs the TouchGFX Designer this
/// environment does not have.
void appendTransliterated(char* out, size_t outSize, size_t& used, const char* in)
{
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
        const char* replacement = nullptr;
        if (p[0] == 0xC2 && p[1] == 0xA9) {        // ©
            replacement = "(c)";
            ++p;
        } else if (p[0] == 0xC2 && p[1] == 0xB7) { // ·
            replacement = "-";
            ++p;
        }
        if (replacement != nullptr) {
            for (const char* r = replacement; *r && used + 1 < outSize; ++r) {
                out[used++] = *r;
            }
        } else if (used + 1 < outSize) {
            out[used++] = static_cast<char>(*p);
        }
    }
    out[used] = '\0';
}

} // namespace

void AttributionFace::setSources(const MapSession& session)
{
    const size_t total   = session.attributionCount();
    const size_t visible = total < kVisibleStrings ? total : kVisibleStrings;

    // Built as one UTF-8 string and converted once. Blank line between
    // credits: two sources are two separate statements, and running them
    // together reads as one long one.
    char text[kBodyBufSize] = { 0 };
    size_t used = 0;
    for (size_t i = 0; i < visible; ++i) {
        const char* s = session.attributionAt(i);
        if (s == nullptr) {
            break;
        }
        if (used != 0) {
            appendTransliterated(text, sizeof(text), used, "\n");
        }
        appendTransliterated(text, sizeof(text), used, s);
    }

    // Say how many were left out rather than quietly showing fewer. A wearer
    // who is owed four credits and shown two has been told something false
    // unless the screen admits it.
    if (total > visible) {
        char more[32];
        std::snprintf(more, sizeof(more), "\n+%u more, see About",
                      static_cast<unsigned>(total - visible));
        appendTransliterated(text, sizeof(text), used, more);
    }

    // Break into lines at word boundaries. Splitting mid-word would be worse
    // than running short: a credit has to stay readable as the names it is
    // crediting.
    size_t line = 0;
    size_t pos  = 0;
    const size_t len = std::strlen(text);
    while (pos < len && line < kMaxLines) {
        while (pos < len && text[pos] == ' ') {
            ++pos;
        }
        if (pos >= len) {
            break;
        }
        size_t end = pos;
        size_t lastSpace = 0;
        while (end < len && text[end] != '\n' && (end - pos) < kCharsPerLine) {
            if (text[end] == ' ') {
                lastSpace = end;
            }
            ++end;
        }
        if (end < len && text[end] != '\n' && lastSpace > pos) {
            end = lastSpace;    // back up to the last word boundary
        }

        const size_t n = (end - pos) < (kLineBufSize - 1) ? (end - pos) : (kLineBufSize - 1);
        char buf[kLineBufSize];
        std::memcpy(buf, text + pos, n);
        buf[n] = '\0';
        setUnicode(mLineBuf[line], kLineBufSize, buf);
        mLines[line].setWildcard(mLineBuf[line]);
        mLines[line].invalidate();
        ++line;

        pos = end;
        if (pos < len && text[pos] == '\n') {
            ++pos;
        }
    }
    for (; line < kMaxLines; ++line) {
        mLineBuf[line][0] = 0;
        mLines[line].setWildcard(mLineBuf[line]);
        mLines[line].invalidate();
    }
}

} // namespace MapKit
