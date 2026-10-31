#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <set>

using namespace geode::prelude;

namespace {
    bool g_twoFingerHeld = false;
    CCLabelBMFont* g_speedLabel = nullptr;

    int getSpamCps() {
        return std::clamp(
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("spam_cps")),
            2, 30
        );
    }

    bool spamAllowed(GJBaseGameLayer* layer) {
        return PlayLayer::get()
            && Mod::get()->getSettingValue<bool>("spam_enabled")
            && !(layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode);
    }
}

// Receives simultaneous screen touches without swallowing them from Geometry Dash.
class TwoFingerDetector : public CCLayer {
    std::set<int> m_touchIDs;

protected:
    bool init() {
        if (!CCLayer::init()) return false;

        this->setTouchEnabled(true);
        this->setTouchMode(kCCTouchesAllAtOnce);
        this->setTouchPriority(-500);
        this->setContentSize(CCDirector::sharedDirector()->getWinSize());
        return true;
    }

public:
    static TwoFingerDetector* create() {
        auto ret = new TwoFingerDetector();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void ccTouchesBegan(CCSet* touches, CCEvent*) override {
        for (auto it = touches->begin(); it != touches->end(); ++it) {
            auto touch = static_cast<CCTouch*>(*it);
            m_touchIDs.insert(touch->getID());
        }
        g_twoFingerHeld = m_touchIDs.size() >= 2;
    }

    void ccTouchesEnded(CCSet* touches, CCEvent*) override {
        for (auto it = touches->begin(); it != touches->end(); ++it) {
            auto touch = static_cast<CCTouch*>(*it);
            m_touchIDs.erase(touch->getID());
        }
        g_twoFingerHeld = m_touchIDs.size() >= 2;
    }

    void ccTouchesCancelled(CCSet*, CCEvent*) override {
        m_touchIDs.clear();
        g_twoFingerHeld = false;
    }

    void onExit() override {
        m_touchIDs.clear();
        g_twoFingerHeld = false;
        CCLayer::onExit();
    }
};

class $modify(AltSpammerPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        g_twoFingerHeld = false;
        auto detector = TwoFingerDetector::create();
        if (detector) this->addChild(detector, -1000);

        return true;
    }

    void onExit() {
        g_twoFingerHeld = false;
        PlayLayer::onExit();
    }
};

class $modify(AltSpammerLayer, GJBaseGameLayer) {
    struct Fields {
        float elapsed = 0.f;
        bool syntheticDown = false;
    };

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto* f = m_fields.operator->();
        bool active = g_twoFingerHeld && spamAllowed(this);

        if (!active) {
            f->elapsed = 0.f;
            if (f->syntheticDown) {
                GJBaseGameLayer::handleButton(false, 1, true);
                f->syntheticDown = false;
            }
            return;
        }

        // One tap is a press followed by a release. Split the interval in half
        // so the selected CPS value represents complete taps per second.
        float edgeInterval = 0.5f / static_cast<float>(getSpamCps());
        f->elapsed += std::max(0.f, dt);

        while (f->elapsed >= edgeInterval) {
            f->elapsed -= edgeInterval;
            f->syntheticDown = !f->syntheticDown;
            GJBaseGameLayer::handleButton(f->syntheticDown, 1, true);
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // While the two-finger gesture is active, replace raw screen taps with
        // the timed press/release stream generated in processCommands().
        if (button == 1 && g_twoFingerHeld && spamAllowed(this)) {
            return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(AltSpammerPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({ 78.f, winSize.height - 48.f });

        auto title = CCLabelBMFont::create("ALT SPAM", "bigFont.fnt");
        title->setScale(0.38f);
        title->setPosition({ 0.f, 0.f });
        menu->addChild(title);

        auto toggleSprite = ButtonSprite::create(
            "Toggle", "bigFont.fnt", "GJ_button_01.png", 0.55f
        );
        toggleSprite->setScale(0.55f);
        auto toggleButton = CCMenuItemSpriteExtra::create(
            toggleSprite, this, menu_selector(AltSpammerPauseLayer::onToggleSpam)
        );
        toggleButton->setPosition({ 0.f, -38.f });
        menu->addChild(toggleButton);

        auto slowerSprite = ButtonSprite::create(
            "Speed -", "bigFont.fnt", "GJ_button_02.png", 0.55f
        );
        slowerSprite->setScale(0.55f);
        auto slowerButton = CCMenuItemSpriteExtra::create(
            slowerSprite, this, menu_selector(AltSpammerPauseLayer::onSlower)
        );
        slowerButton->setPosition({ -43.f, -76.f });
        menu->addChild(slowerButton);

        auto fasterSprite = ButtonSprite::create(
            "Speed +", "bigFont.fnt", "GJ_button_02.png", 0.55f
        );
        fasterSprite->setScale(0.55f);
        auto fasterButton = CCMenuItemSpriteExtra::create(
            fasterSprite, this, menu_selector(AltSpammerPauseLayer::onFaster)
        );
        fasterButton->setPosition({ 43.f, -76.f });
        menu->addChild(fasterButton);

        g_speedLabel = CCLabelBMFont::create(
            fmt::format("{} CPS", getSpamCps()).c_str(), "bigFont.fnt"
        );
        g_speedLabel->setScale(0.38f);
        g_speedLabel->setPosition({ 0.f, -111.f });
        menu->addChild(g_speedLabel);

        this->addChild(menu, 10);
    }

    void onToggleSpam(CCObject*) {
        bool enabled = Mod::get()->getSettingValue<bool>("spam_enabled");
        Mod::get()->setSettingValue<bool>("spam_enabled", !enabled);
        FLAlertLayer::create(
            "Alt Spammer",
            !enabled ? "Two-finger spam is <cg>enabled</c>." : "Two-finger spam is <cr>disabled</c>.",
            "OK"
        )->show();
    }

    void onSlower(CCObject*) {
        int cps = std::max(2, getSpamCps() - 1);
        Mod::get()->setSettingValue<int64_t>("spam_cps", cps);
        updateSpeedLabel(cps);
    }

    void onFaster(CCObject*) {
        int cps = std::min(30, getSpamCps() + 1);
        Mod::get()->setSettingValue<int64_t>("spam_cps", cps);
        updateSpeedLabel(cps);
    }

    void updateSpeedLabel(int cps) {
        if (g_speedLabel) {
            g_speedLabel->setString(fmt::format("{} CPS", cps).c_str());
        }
    }
};

$on_mod(Loaded) {
    log::info("Alt Spammer loaded — hold two fingers to generate rapid taps.");
}
