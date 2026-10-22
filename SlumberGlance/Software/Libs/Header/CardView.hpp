#ifndef SLUMBER_GLANCE_CARD_VIEW_HPP
#define SLUMBER_GLANCE_CARD_VIEW_HPP

#include <cstdint>
#include <cstdio>

#include "CardStore.hpp"
#include "SlumberCard.hpp"

/// What the two lines of the card say, decided without the SDK so it can be
/// tested on a desk: Slumber stopped, else last night, else that none is found yet.
namespace CardView
{

/// Two missed heartbeats and a minute's grace before calling the writer stopped.
constexpr uint32_t kStaleAfterS = 2u * SlumberCard::kHeartbeatS + 60u;

enum class Tone : uint8_t { Normal, Muted, Warning };

struct View {
    char top[32]    = {};
    char bottom[32] = {};
    Tone tone       = Tone::Normal;
};

inline void hm(char *out, size_t n, uint32_t seconds)
{
    const uint32_t min = seconds / 60u;
    std::snprintf(out, n, "%luh%02lu", static_cast<unsigned long>(min / 60u),
                  static_cast<unsigned long>(min % 60u));
}

/// `last` is the card shown before, for a Busy read to keep; null if none.
/// `withIcon` is whether the crescent stands beside the lines in place of the word "Night".
inline View make(SlumberCard::Read read, const SlumberCard::Card &card,
                 const SlumberCard::Card *last, uint32_t nowUtc, bool withIcon)
{
    using SlumberCard::Read;
    View v;

    if (read == Read::Absent) {
        std::snprintf(v.top, sizeof(v.top), "Slumber");
        std::snprintf(v.bottom, sizeof(v.bottom), "open Slumber once");
        v.tone = Tone::Muted;
        return v;
    }
    const SlumberCard::Card *shown = (read == Read::Busy) ? last : &card;
    if (shown == nullptr) {
        std::snprintf(v.top, sizeof(v.top), "Slumber");
        std::snprintf(v.bottom, sizeof(v.bottom), "one moment");
        v.tone = Tone::Muted;
        return v;
    }

    if (nowUtc == 0 || shown->heartbeatUtc == 0) {
        std::snprintf(v.top, sizeof(v.top), "Slumber");
        std::snprintf(v.bottom, sizeof(v.bottom), "waiting for clock");
        v.tone = Tone::Muted;
        return v;
    }

    const uint32_t sinceBeat = nowUtc > shown->heartbeatUtc ? nowUtc - shown->heartbeatUtc : 0u;
    char span[12];
    if (sinceBeat > kStaleAfterS) {
        hm(span, sizeof(span), sinceBeat);
        std::snprintf(v.top, sizeof(v.top), "Slumber stopped");
        std::snprintf(v.bottom, sizeof(v.bottom), "last seen %s ago", span);
        v.tone = Tone::Warning;
        return v;
    }

    const bool fresh = shown->validUntilUtc == 0 || nowUtc <= shown->validUntilUtc;
    if (fresh && (shown->state == SlumberCard::State::Report ||
                  shown->state == SlumberCard::State::Provisional)) {
        std::snprintf(v.top, sizeof(v.top), "%s%02u:%02u-%02u:%02u", withIcon ? "" : "Night ",
                      (shown->onsetMinOfDay / 60u) % 24u, shown->onsetMinOfDay % 60u,
                      (shown->wakeMinOfDay / 60u) % 24u, shown->wakeMinOfDay % 60u);
        hm(span, sizeof(span), shown->restMin * 60u);
        std::snprintf(v.bottom, sizeof(v.bottom), "%s %s", span,
                      shown->state == SlumberCard::State::Report ? "at rest" : "so far");
        return v;
    }
    if (fresh && shown->state == SlumberCard::State::NotWorn) {
        std::snprintf(v.top, sizeof(v.top), "No night");
        std::snprintf(v.bottom, sizeof(v.bottom), "watch was off");
        v.tone = Tone::Muted;
        return v;
    }

    std::snprintf(v.top, sizeof(v.top), "No night yet");
    std::snprintf(v.bottom, sizeof(v.bottom), "wear it to bed");
    v.tone = Tone::Muted;
    return v;
}

} // namespace CardView

#endif // SLUMBER_GLANCE_CARD_VIEW_HPP
