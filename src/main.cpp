#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <deque>

using namespace geode::prelude;

class $modify(AltSpammerLayer, GJBaseGameLayer) {
    struct QueuedInput {
        bool down;
        int button;
        bool player1;
        int dueStep;
    };

    struct Fields {
        int step = 0;
        int lastPressStep = -100000;
        int assistUntil = -1;
        bool queuedHeld = false;
        std::deque<QueuedInput> queue;
    };

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* f = m_fields.operator->();
        ++f->step;

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (!PlayLayer::get()) {
            f->queue.clear();
            f->queuedHeld = false;
            f->assistUntil = -1;
            f->lastPressStep = -100000;
            return;
        }

        while (!f->queue.empty() && f->queue.front().dueStep <= f->step) {
            auto event = f->queue.front();
            f->queue.pop_front();
            GJBaseGameLayer::handleButton(event.down, event.button, event.player1);
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto* f = m_fields.operator->();

        // Only assist the normal jump input in one-player levels.
        // Dual portals inside one-player levels still use this same input.
        if (!PlayLayer::get() || button != 1 ||
            (m_levelSettings && m_levelSettings->m_twoPlayerMode) ||
            !Mod::get()->getSettingValue<bool>("assist_enabled")) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        int bufferFrames = static_cast<int>(
            Mod::get()->getSettingValue<int64_t>("buffer_frames")
        );
        int detectFrames = static_cast<int>(
            Mod::get()->getSettingValue<int64_t>("detect_window_frames")
        );

        bufferFrames = std::clamp(bufferFrames, 0, 30);
        detectFrames = std::clamp(detectFrames, 1, 30);

        if (down) {
            int sinceLastPress = f->step - f->lastPressStep;
            f->lastPressStep = f->step;

            // A quick next press activates assistance for subsequent taps.
            if (sinceLastPress >= 0 && sinceLastPress <= detectFrames * 4) {
                f->assistUntil = f->step + detectFrames * 4;
            }

            if (bufferFrames > 0 && f->step <= f->assistUntil) {
                f->queue.push_back({true, button, isPlayer1, f->step + bufferFrames * 4});
                f->queuedHeld = true;
                return;
            }
        } else if (bufferFrames > 0 &&
                   (f->step <= f->assistUntil || f->queuedHeld)) {
            f->queue.push_back({false, button, isPlayer1, f->step + bufferFrames * 4});
            f->queuedHeld = false;
            return;
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

$on_mod(Loaded) {
    log::info("Alt Spammer loaded — adaptive tap assistance enabled.");
}
