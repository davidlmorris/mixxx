#include "analyzer/analyzersilence.h"

#include "analyzer/analyzertrack.h"
#include "analyzer/constants.h"
#include "track/track.h"

namespace {

// This threshold must not be changed, because this value is also used to
// verify that the track samples have not changed since the last analysis
constexpr CSAMPLE kSilenceThreshold = 0.001f; // -60 dB
// TODO: Change the above line to:
// constexpr CSAMPLE kSilenceThreshold = db2ratio(-60.0f);

// These are in dBV expressed as Volts RMS (which seems, sensibly,
// the way Mixxx works).
// Don't change these to const as they are used only to feed the
// fade thresholds which are consts below themselves while we work
// out which one is best, and you'll just be adding to exe size.
#define N_10DB_FADEOUT_THRESHOLD 0.3162f
#define N_12DB_FADEOUT_THRESHOLD 0.2511f
#define N_15DB_FADEOUT_THRESHOLD 0.1778f
#define N_18DB_FADEOUT_THRESHOLD 0.1259f
#define N_20DB_FADEOUT_THRESHOLD 0.1f
#define N_24DB_FADEOUT_THRESHOLD 0.0631f
#define N_25DB_FADEOUT_THRESHOLD 0.0562f
#define N_27DB_FADEOUT_THRESHOLD 0.0447f
#define N_30DB_FADEOUT_THRESHOLD 0.0316f
#define N_40DB_FADEOUT_THRESHOLD 0.01f

constexpr CSAMPLE kFadeInThreshold = N_27DB_FADEOUT_THRESHOLD;
constexpr CSAMPLE kFadeOutThreshold = N_12DB_FADEOUT_THRESHOLD;

bool shouldAnalyze(TrackPointer pTrack) {
    CuePointer pIntroCue = pTrack->findCueByType(mixxx::CueType::Intro);
    CuePointer pOutroCue = pTrack->findCueByType(mixxx::CueType::Outro);
    CuePointer pN60dBSound = pTrack->findCueByType(mixxx::CueType::N60dBSound);
    CuePointer pFadeIn = pTrack->findCueByType(mixxx::CueType::FadeIn);
    CuePointer pFadeOut = pTrack->findCueByType(mixxx::CueType::FadeOut);

    if (!pFadeIn || !pFadeOut || !pIntroCue || !pOutroCue || !pN60dBSound || pN60dBSound->getLengthFrames() <= 0) {
        return true;
    }
    return false;
}

template<typename Iterator>
Iterator first_sound(Iterator begin, Iterator end) {
    return std::find_if(begin, end, [](const auto elem) {
        return fabs(elem) >= kSilenceThreshold;
    });
}

template<typename ForwardIterator>
ForwardIterator last_fade_in_sound(ForwardIterator begin, ForwardIterator end) {
    return std::find_if(begin, end, [](const auto elem) {
        return fabs(elem) >= kFadeInThreshold;
    });
}

template<typename Iterator>
Iterator first_fade_out_sound(Iterator begin, Iterator end) {
    return std::find_if(begin, end, [](const auto elem) {
        return fabs(elem) >= kFadeOutThreshold;
    });
}

} // anonymous namespace

AnalyzerSilence::AnalyzerSilence(UserSettingsPointer pConfig)
        : m_pConfig(pConfig),
          m_framesProcessed(0),
          m_signalStart(-1),
          m_signalEnd(-1),
          m_fadeThresholdFadeInEnd(-1),
          m_fadeThresholdFadeOutStart(-1) {
}

bool AnalyzerSilence::initialize(const AnalyzerTrack& track,
        mixxx::audio::SampleRate sampleRate,
        mixxx::audio::ChannelCount channelCount,
        SINT frameLength) {
    Q_UNUSED(sampleRate);
    Q_UNUSED(frameLength);

    if (!shouldAnalyze(track.getTrack())) {
        return false;
    }

    m_framesProcessed = 0;
    m_signalStart = -1;
    m_signalEnd = -1;
    m_fadeThresholdFadeInEnd = -1;
    m_fadeThresholdFadeOutStart = -1;
    m_channelCount = channelCount;

    return true;
}

// static
SINT AnalyzerSilence::findFirstSoundInChunk(std::span<const CSAMPLE> samples) {
    return std::distance(samples.begin(), first_sound(samples.begin(), samples.end()));
}

// static
SINT AnalyzerSilence::findLastSoundInChunk(std::span<const CSAMPLE> samples) {
    // -1 is required, because the distance from the fist sample index (0) to crend() is 1,
    SINT ret = std::distance(first_sound(samples.rbegin(), samples.rend()), samples.rend()) - 1;
    return ret;
}

// static
SINT AnalyzerSilence::findFirstFadeOutChunk(std::span<const CSAMPLE> samples) {
    // -1 is required, because the distance from the fist sample index (0) to crend() is 1,
    SINT ret = std::distance(first_fade_out_sound(samples.rbegin(), samples.rend()), samples.rend()) - 1;
    if (ret == -1) {
        ret = samples.size();
    }
    return ret;
}

// static
SINT AnalyzerSilence::findLastFadeInChunk(std::span<const CSAMPLE> samples) {
    SINT ret = std::distance(samples.begin(), last_fade_in_sound(samples.begin(), samples.end()));
    //    if (ret == samples.size()) {
    //        ret = 0;
    //    }
    return ret;
}

// static
bool AnalyzerSilence::verifyFirstSound(
        std::span<const CSAMPLE> samples,
        mixxx::audio::FramePos firstSoundFrame,
        mixxx::audio::ChannelCount channelCount) {
    const SINT firstSoundSample = findFirstSoundInChunk(samples);
    if (firstSoundSample < static_cast<SINT>(samples.size())) {
        return mixxx::audio::FramePos::fromSamplePos(firstSoundSample, channelCount)
                       .toLowerFrameBoundary() == firstSoundFrame.toLowerFrameBoundary();
    }
    return false;
}

bool AnalyzerSilence::processSamples(const CSAMPLE* pIn, SINT count) {
    SINT numFrames = count / m_channelCount;

    std::span<const CSAMPLE> samples = mixxx::spanutil::spanFromPtrLen(pIn, count);
    if (m_signalStart < 0) {
        const SINT firstSoundSample = findFirstSoundInChunk(samples);
        if (firstSoundSample < count) {
            m_signalStart = m_framesProcessed + firstSoundSample / m_channelCount;
        }
    }

    if (m_fadeThresholdFadeInEnd < 0) {
        const SINT lastSampleOfFadeIn = findLastFadeInChunk(samples);
        if (lastSampleOfFadeIn < count) {
            m_fadeThresholdFadeInEnd = m_framesProcessed + (lastSampleOfFadeIn / m_channelCount);
        }
    }
    if (m_fadeThresholdFadeInEnd >= 0) {
        const SINT lasttSampleBeforeFadeOut = findFirstFadeOutChunk(samples);
        if (lasttSampleBeforeFadeOut < (count - 1)) {
            m_fadeThresholdFadeOutStart = m_framesProcessed + (lasttSampleBeforeFadeOut / m_channelCount) + 1;
        }
    }
    if (m_fadeThresholdFadeOutStart >= 0) {
        const SINT lastSoundSample = findLastSoundInChunk(samples);
        if (lastSoundSample < (count - 1)) { // not only sound or silence
            m_signalEnd = m_framesProcessed + (lastSoundSample / m_channelCount) + 1;
        }
    }

    m_framesProcessed += numFrames;
    return true;
}

void AnalyzerSilence::cleanup() {
}

void AnalyzerSilence::storeResults(TrackPointer pTrack) {
    if (m_signalStart < 0) {
        m_signalStart = 0;
    }
    if (m_signalEnd < 0) {
        m_signalEnd = m_framesProcessed;
    }

    const auto firstSoundPosition = mixxx::audio::FramePos(m_signalStart);
    const auto lastSoundPosition = mixxx::audio::FramePos(m_signalEnd);

    CuePointer pN60dBSound = pTrack->findCueByType(mixxx::CueType::N60dBSound);
    if (pN60dBSound == nullptr) {
        pN60dBSound = pTrack->createAndAddCue(
                mixxx::CueType::N60dBSound,
                Cue::kNoHotCue,
                firstSoundPosition,
                lastSoundPosition);
    } else {
        // The user has no way to directly edit the N60dBSound cue. If the user
        // has deleted the Intro or Outro Cue, this analysis will be rerun when
        // the track is loaded again. In this case, adjust the N60dBSound Cue's
        // positions. This could be helpful, for example, when the track length
        // is changed in a different program.
        pN60dBSound->setStartAndEndPosition(firstSoundPosition, lastSoundPosition);
    }

    setupMainAndIntroCue(pTrack.get(), firstSoundPosition, m_pConfig.data());
    setupOutroCue(pTrack.get(), lastSoundPosition);

    if (m_fadeThresholdFadeInEnd < 0) {
        m_fadeThresholdFadeInEnd = 0;
    }
    if (m_fadeThresholdFadeOutStart < 0) {
        m_fadeThresholdFadeOutStart = m_framesProcessed;
    }
    const auto fadeInEndPosition = mixxx::audio::FramePos(m_fadeThresholdFadeInEnd);
    CuePointer pFadeIn = pTrack->findCueByType(mixxx::CueType::FadeIn);
    if (pFadeIn == nullptr) {
        pFadeIn = pTrack->createAndAddCue(
                mixxx::CueType::FadeIn,
                Cue::kNoHotCue,
                firstSoundPosition,
                fadeInEndPosition);
    } else {
        pFadeIn->setStartAndEndPosition(firstSoundPosition, fadeInEndPosition);
    }

    const auto fadeOutStartPosition = mixxx::audio::FramePos(m_fadeThresholdFadeOutStart);
    CuePointer pFadeOut = pTrack->findCueByType(mixxx::CueType::FadeOut);
    if (pFadeOut == nullptr) {
        pFadeOut = pTrack->createAndAddCue(
                mixxx::CueType::FadeOut,
                Cue::kNoHotCue,
                fadeOutStartPosition,
                lastSoundPosition);
    } else {
        pFadeOut->setStartAndEndPosition(fadeOutStartPosition, lastSoundPosition);
    }
}

// static
void AnalyzerSilence::setupMainAndIntroCue(
        Track* pTrack, mixxx::audio::FramePos firstSoundPosition, UserSettings* pConfig) {
    CuePointer pIntroCue = pTrack->findCueByType(mixxx::CueType::Intro);

    mixxx::audio::FramePos mainCuePosition = pTrack->getMainCuePosition();
    mixxx::audio::FramePos introStartPosition = firstSoundPosition;
    // Before Mixxx 2.3, the default position for the main cue was 0.0. In this
    // case, move the main cue point to the first sound. This case can be
    // distinguished from a user intentionally setting the main cue position
    // to 0.0 at a later time after analysis because in that case the intro cue
    // would have already been created by this analyzer.
    bool upgradingWithMainCueAtDefault =
            (mainCuePosition == mixxx::audio::kStartFramePos &&
                    pIntroCue == nullptr);
    if (!mainCuePosition.isValid() || upgradingWithMainCueAtDefault) {
        pTrack->setMainCuePosition(firstSoundPosition);
        // NOTE: the actual default for this ConfigValue is set in DlgPrefDeck.
    } else if (pConfig->getValue(ConfigKey("[Controls]", "SetIntroStartAtMainCue"), false) &&
            pIntroCue == nullptr) {
        introStartPosition = mainCuePosition;
    }

    if (pIntroCue == nullptr) {
        pIntroCue = pTrack->createAndAddCue(
                mixxx::CueType::Intro,
                Cue::kNoHotCue,
                introStartPosition,
                mixxx::audio::kInvalidFramePos);
    }
}

// static
void AnalyzerSilence::setupOutroCue(Track* pTrack, mixxx::audio::FramePos lastSoundPosition) {
    CuePointer pOutroCue = pTrack->findCueByType(mixxx::CueType::Outro);
    if (pOutroCue == nullptr) {
        pOutroCue = pTrack->createAndAddCue(
                mixxx::CueType::Outro,
                Cue::kNoHotCue,
                mixxx::audio::kInvalidFramePos,
                lastSoundPosition);
    }
}
