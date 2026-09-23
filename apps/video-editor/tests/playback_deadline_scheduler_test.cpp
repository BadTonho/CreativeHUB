#include "playback/playback_audio_pacing.h"
#include "playback/playback_deadline_scheduler.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Scheduler = playback::detail::PlaybackDeadlineScheduler;
using AudioPacingPolicy = playback::detail::AudioPacingPolicy;
using Clock = Scheduler::Clock;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void validateFractionalDeadlines() {
    const auto origin = Clock::time_point{};

    Scheduler scheduler;
    scheduler.start(origin, 0, 23.976);
    for (std::int64_t frame = 1; frame <= 500; ++frame) {
        scheduler.advanceAfterTarget(frame);
    }

    const auto expected = std::chrono::duration<double>(501.0 / 23.976);
    const auto expected_duration = std::chrono::duration_cast<Clock::duration>(expected);
    const auto actual = scheduler.nextDeadline() - origin;
    require(
        std::chrono::duration<double>(actual) >= expected,
        "The fractional scheduler deadline was early.");
    require(
        std::chrono::duration_cast<std::chrono::nanoseconds>(actual - expected_duration).count() < 2,
        "The fractional scheduler accumulated deadline drift.");

    scheduler.start(origin, 100, 24.0);
    for (std::int64_t frame = 1; frame <= 500; ++frame) {
        scheduler.advanceAfterTarget(100 + frame);
    }
    const auto expected_24 = std::chrono::duration<double>(501.0 / 24.0);
    const auto actual_24 = scheduler.nextDeadline() - origin;
    require(
        std::chrono::duration<double>(actual_24) >= expected_24,
        "The 24 fps scheduler deadline was early.");
    require(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            actual_24 - std::chrono::duration_cast<Clock::duration>(expected_24)).count() < 2,
        "The 24 fps scheduler accumulated deadline drift.");
}

void validateEarlyAndLateCallbacks() {
    const auto origin = Clock::time_point{};
    Scheduler scheduler;
    scheduler.start(origin, 10, 24.0);

    const auto early = origin + std::chrono::milliseconds(40);
    require(
        scheduler.targetFrame(early) == 10,
        "An early callback advanced the target frame.");
    require(
        scheduler.delayUntil(early).count() == 2,
        "An early callback did not use a millisecond ceiling.");

    const auto delayed = origin + std::chrono::milliseconds(300);
    require(
        scheduler.targetFrame(delayed) == 17,
        "A delayed callback did not catch up to all due frames.");
    scheduler.advanceAfterTarget(17);
    require(
        scheduler.delayUntil(delayed).count() == 34,
        "A delayed callback did not advance directly to the next deadline.");
    require(
        scheduler.nextDeadline() > delayed,
        "A delayed callback left an overdue deadline queued.");
}

void validateAudioTargetDoesNotChangeCadence() {
    const auto origin = Clock::time_point{};
    Scheduler scheduler;
    scheduler.start(origin, 0, 24.0);

    const auto callback_time = origin + std::chrono::milliseconds(42);
    const auto deadline_target = scheduler.targetFrame(callback_time);
    const auto audio_target = std::int64_t{4};
    const auto current_frame_index = audio_target;
    require(
        deadline_target == 1 && current_frame_index == 4 &&
            audio_target == current_frame_index &&
            audio_target > deadline_target,
        "The audio cadence test did not create an ahead-of-deadline target.");

    // The audio-selected frame may already be displayed, but it must not move
    // the scheduler past the next absolute video deadline.
    scheduler.advanceAfterTarget(deadline_target);
    const auto expected = std::chrono::duration<double>(2.0 / 24.0);
    const auto actual = scheduler.nextDeadline() - origin;
    require(
        std::chrono::duration<double>(actual) >= expected,
        "An audio target advanced the scheduler to an early deadline.");
    require(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            actual - std::chrono::duration_cast<Clock::duration>(expected)).count() < 2,
        "An audio target advanced the scheduler by multiple frames.");
    require(
        scheduler.delayUntil(callback_time).count() == 42,
        "An audio target changed the next video timer delay.");
}

void validateResetAndRestart() {
    const auto origin = Clock::time_point{};
    Scheduler scheduler;
    scheduler.start(origin, 4, 24.0);
    scheduler.advanceAfterTarget(100);
    scheduler.reset();

    require(!scheduler.active(), "Reset left the scheduler active.");
    require(
        scheduler.targetFrame(origin + std::chrono::seconds(1)) == 0,
        "Reset retained the previous frame origin.");
    require(
        scheduler.delayUntil(origin + std::chrono::seconds(1)).count() == 0,
        "Reset retained a pending deadline.");

    const auto restarted = origin + std::chrono::seconds(5);
    scheduler.start(restarted, 80, 24.0);
    require(
        scheduler.targetFrame(restarted) == 80,
        "Restart did not use the new frame origin.");
    require(
        scheduler.nextDeadline() > restarted,
        "Restart did not create a fresh deadline.");
}

void validateAudioPacingToleranceAndPersistence() {
    AudioPacingPolicy policy;

    const auto within_tolerance = policy.selectTarget(10, 11, 12);
    require(
        within_tolerance.target_frame == 11 &&
            within_tolerance.audio_catchup_frames == 0 &&
            within_tolerance.deadline_catchup_frames == 0,
        "A one-frame audio drift was not tolerated.");

    const auto first_ahead_tick = policy.selectTarget(10, 11, 13);
    const auto second_ahead_tick = policy.selectTarget(10, 11, 13);
    require(
        first_ahead_tick.target_frame == 11 &&
            second_ahead_tick.target_frame == 11,
        "Transient audio drift triggered visual catch-up.");

    const auto persistent_ahead = policy.selectTarget(10, 11, 13);
    require(
        persistent_ahead.target_frame == 12 &&
            persistent_ahead.audio_catchup_frames == 1 &&
            persistent_ahead.deadline_catchup_frames == 0 &&
            persistent_ahead.audio_correction_applied,
        "Persistent audio drift was not limited to one extra frame.");

    const auto continued_ahead = policy.selectTarget(11, 12, 20);
    require(
        continued_ahead.target_frame == 13 &&
            continued_ahead.audio_catchup_frames == 1,
        "Large audio drift was not bounded per tick.");

    const auto deadline_catchup = policy.selectTarget(10, 14, 14);
    require(
        deadline_catchup.target_frame == 14 &&
            deadline_catchup.deadline_catchup_frames == 3 &&
            deadline_catchup.audio_catchup_frames == 0,
        "Deadline catch-up was classified as audio catch-up.");

    const auto normalized = policy.selectTarget(12, 13, 14);
    require(
        normalized.target_frame == 13 &&
            normalized.audio_catchup_frames == 0,
        "Audio drift persistence was not reset after normalization.");

    policy.reset();
    const auto after_reset = policy.selectTarget(10, 11, 13);
    require(
        after_reset.target_frame == 11,
        "Reset did not clear audio drift persistence.");
}

} // namespace

int main() {
    try {
        validateFractionalDeadlines();
        validateEarlyAndLateCallbacks();
        validateAudioTargetDoesNotChangeCadence();
        validateResetAndRestart();
        validateAudioPacingToleranceAndPersistence();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
