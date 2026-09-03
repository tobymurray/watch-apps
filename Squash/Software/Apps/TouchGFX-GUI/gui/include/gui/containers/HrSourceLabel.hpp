#ifndef HRSOURCELABEL_HPP
#define HRSOURCELABEL_HPP

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @brief One letter naming the sensor that is feeding heart rate.
 *
 * `E` external, `O` optical, `-` nothing yet.
 *
 * It goes on every face that shows a heart rate, because the first attempt put
 * it beside the heart icon on the status face alone -- and the status face is
 * one of three, reached by pressing L1, so a wearer on the default face never
 * saw it. The icon has the same problem and always has: `SensorStatusRow` lives
 * on the status face, so its steady-versus-flashing cue is invisible from the
 * face people actually watch.
 *
 * A separate widget rather than three copies of the wildcard-buffer
 * boilerplate, since the only thing that differs per face is where it sits.
 */
class HrSourceLabel : public touchgfx::TextAreaWithOneWildcard
{
public:
    /// Place it and give it the font. Call once, from the face's initialize().
    void configure(int16_t x, int16_t y)
    {
        setPosition(x, y, kW, kH);
        setColor(touchgfx::Color::getColorFromRGB(192, 192, 192));
        setLinespacing(0);
        setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_18));
        mBuffer[0] = 0;
        setWildcard(mBuffer);
    }

    /// @param source SDK::SensorDataParser::HeartRateEx::Source: 1 optical, 2 external.
    void set(uint8_t source)
    {
        const touchgfx::Unicode::UnicodeChar letter =
            (source == 2) ? 'E' : ((source == 1) ? 'O' : '-');
        if (mBuffer[0] == letter) {
            return;
        }
        invalidate();
        mBuffer[0] = letter;
        mBuffer[1] = 0;
        setWildcard(mBuffer);
        invalidate();
    }

private:
    static const int16_t kW = 22;
    static const int16_t kH = 23;

    touchgfx::Unicode::UnicodeChar mBuffer[2] = {};
};

#endif // HRSOURCELABEL_HPP
