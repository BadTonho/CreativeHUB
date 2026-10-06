#include "audio/audio_envelope_analyzer.h"
#include "model/composition_document.h"
#include "model/motion_project_data.h"
#include "persistence/motion_document_store.h"
#include "ui/audio_keyframe_generation.h"
#include "ui/dialogs/audio_keyframe_dialog.h"
#include "application/history/composition_history.h"
#include "ui/main_window.h"

#include <creative_suite/diagnostics/logger.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void writeU16(std::ostream& output, std::uint16_t value)
{
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void writeU32(std::ostream& output, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
        output.put(static_cast<char>((value >> shift) & 0xffU));
}

void writeWave(const std::filesystem::path& path,
               int sample_rate,
               int channels,
               const std::vector<float>& interleaved_samples)
{
    const auto data_bytes = static_cast<std::uint32_t>(
        interleaved_samples.size() * sizeof(std::int16_t));
    std::ofstream output(path, std::ios::binary);
    output.write("RIFF", 4);
    writeU32(output, 36U + data_bytes);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16U);
    writeU16(output, 1U);
    writeU16(output, static_cast<std::uint16_t>(channels));
    writeU32(output, static_cast<std::uint32_t>(sample_rate));
    writeU32(output, static_cast<std::uint32_t>(sample_rate * channels * 2));
    writeU16(output, static_cast<std::uint16_t>(channels * 2));
    writeU16(output, 16U);
    output.write("data", 4);
    writeU32(output, data_bytes);
    for (const auto sample : interleaved_samples) {
        const auto bounded = std::clamp(sample, -1.0F, 1.0F);
        const auto pcm = static_cast<std::int16_t>(std::lrint(bounded * 32767.0F));
        writeU16(output, static_cast<std::uint16_t>(pcm));
    }
    require(static_cast<bool>(output), "the temporary PCM audio fixture is written");
}

std::filesystem::path pathFromQString(const QString& path)
{
    const auto bytes = path.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(first, first + bytes.size()));
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

bool near(double actual, double expected, double tolerance = 0.002)
{
    return std::abs(actual - expected) <= tolerance;
}

template <typename Predicate>
bool waitForWorker(motion::ui::AudioKeyframeGenerationWorker& worker,
                   Predicate completed,
                   int timeout_ms = 10'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!completed() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!completed()) {
        worker.cancelAndWait();
        worker.wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        return false;
    }
    worker.wait();
    return true;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    using creative_suite::animation::TransformProperty;
    using motion::model::CompositionDocument;
    using motion::model::FrameRate;
    using motion::model::LayerId;
    using motion::model::LayerKind;

    QTemporaryDir temporary_directory;
    require(temporary_directory.isValid(), "a temporary test directory is available");
    const auto fixture_directory = pathFromQString(temporary_directory.path());
    const auto audio_path = fixture_directory / "two-level-stereo.wav";
    std::vector<float> samples;
    samples.reserve(96'000U * 2U);
    for (int frame = 0; frame < 96'000; ++frame) {
        const float amplitude = frame < 48'000 ? 0.25F : 0.5F;
        samples.push_back(amplitude);
        samples.push_back(amplitude);
    }
    writeWave(audio_path, 48'000, 2, samples);

    std::atomic_bool cancel_requested{false};
    const auto envelope = motion::audio::AudioEnvelopeAnalyzer::analyze(
        audio_path, FrameRate{24, 1}, 48, cancel_requested);
    require(envelope.rms_by_frame.size() == 48,
            "audio is analyzed at the exact composition frame rate");
    require(envelope.rms_by_frame.front() > 0.0 &&
                near(envelope.rms_by_frame[24] / envelope.rms_by_frame.front(), 2.0),
            "the second section has twice the converted mono RMS");
    require(near(envelope.peak_rms, envelope.rms_by_frame[24]),
            "the peak RMS is recorded for normalization");

    const auto mono_path = fixture_directory / "mono-level.wav";
    writeWave(mono_path, 48'000, 1, std::vector<float>(48'000U, 0.25F));
    const auto mono_envelope = motion::audio::AudioEnvelopeAnalyzer::analyze(
        mono_path, FrameRate{24, 1}, 24, cancel_requested);
    require(mono_envelope.rms_by_frame.size() == 24 &&
                near(mono_envelope.rms_by_frame.front(), 0.25) &&
                near(mono_envelope.peak_rms, 0.25),
            "mono audio produces its expected per-frame RMS at 48 kHz");

    const auto shorter_analysis = motion::audio::AudioEnvelopeAnalyzer::analyze(
        audio_path, FrameRate{24, 1}, 12, cancel_requested);
    require(shorter_analysis.rms_by_frame.size() == 12 &&
                near(shorter_analysis.peak_rms, envelope.peak_rms),
            "normalization scans the complete source when the layer ends earlier");
    const auto normalized_short_track = motion::audio::generateAudioKeyframes(
        shorter_analysis, 12, TransformProperty::Scale, 1.0, 2.0, 12);
    require(near(normalized_short_track.front().value, 1.5),
            "the selected range uses the peak from audio beyond the layer boundary");

    auto scale_keys = motion::audio::generateAudioKeyframes(
        envelope, 72, TransformProperty::Scale, 1.0, 2.0, 100);
    require(scale_keys.size() == 49,
            "a shorter source gains one return-to-minimum keyframe");
    require(scale_keys.front().frame == 0 && near(scale_keys.front().value, 1.5),
            "peak-normalized low audio maps to the middle of the selected range");
    require(scale_keys[24].frame == 24 && near(scale_keys[24].value, 2.0),
            "the per-file peak maps to the selected maximum");
    require(scale_keys.back().frame == 48 && near(scale_keys.back().value, 1.0),
            "the property returns to its minimum when the source ends");
    for (const auto& keyframe : scale_keys) {
        require(keyframe.interpolation == creative_suite::animation::InterpolationMode::Linear,
                "generated keyframes use linear interpolation");
    }

    const auto truncated_keys = motion::audio::generateAudioKeyframes(
        envelope, 12, TransformProperty::Opacity, 0.0, 1.0, 12);
    require(truncated_keys.size() == 12 && truncated_keys.back().frame == 11,
            "audio is truncated at the selected layer duration without an extra key");
    require(near(truncated_keys.front().value, 0.5) &&
                near(truncated_keys[0].value, 0.5),
            "the selected output range is applied to opacity");

    const auto silent_path = fixture_directory / "silence.wav";
    writeWave(silent_path, 48'000, 1, std::vector<float>(48'000U, 0.0F));
    const auto silence = motion::audio::AudioEnvelopeAnalyzer::analyze(
        silent_path, FrameRate{24, 1}, 48, cancel_requested);
    require(motion::audio::generateAudioKeyframes(
                silence, 72, TransformProperty::Scale, 1.0, 2.0, 100).empty(),
            "digital silence does not generate keyframes");

    const auto invalid_path = fixture_directory / "not-audio.txt";
    {
        std::ofstream invalid(invalid_path, std::ios::binary);
        invalid << "not an audio file";
    }
    bool invalid_rejected = false;
    try {
        static_cast<void>(motion::audio::AudioEnvelopeAnalyzer::analyze(
            invalid_path, FrameRate{24, 1}, 48, cancel_requested));
    } catch (const motion::audio::AudioAnalysisError&) {
        invalid_rejected = true;
    }
    require(invalid_rejected, "an invalid audio source is rejected");

    auto& logger = creative_suite::diagnostics::Logger::instance();
    require(logger.initialize(fixture_directory / "diagnostics"),
            "a temporary diagnostics log is initialized for worker checks");
    const auto make_worker_request = [&](const std::filesystem::path& path,
                                         LayerId id) {
        motion::ui::AudioKeyframeGenerationRequest request;
        request.audio_path = path;
        request.layer_id = id;
        request.frame_rate = FrameRate{24, 1};
        request.layer_duration_frames = 72;
        request.analysis_frame_limit = 48;
        request.property = TransformProperty::Scale;
        request.minimum_value = 1.0;
        request.maximum_value = 2.0;
        request.maximum_keyframe_count = 100;
        return request;
    };

    QObject worker_receiver;
    std::optional<motion::ui::AudioKeyframeGenerationResult> successful_result;
    std::vector<int> worker_progress;
    bool success_callbacks_on_receiver_thread = true;
    motion::ui::AudioKeyframeGenerationWorker successful_worker(
        &worker_receiver, make_worker_request(audio_path, 41),
        [&](int progress) {
            success_callbacks_on_receiver_thread = success_callbacks_on_receiver_thread &&
                QThread::currentThread() == worker_receiver.thread();
            worker_progress.push_back(progress);
        },
        [&](motion::ui::AudioKeyframeGenerationResult result) {
            success_callbacks_on_receiver_thread = success_callbacks_on_receiver_thread &&
                QThread::currentThread() == worker_receiver.thread();
            successful_result = std::move(result);
        });
    successful_worker.start();
    require(waitForWorker(successful_worker, [&] {
                return successful_result.has_value() && !worker_progress.empty();
            }),
            "the successful audio keyframe worker completes before timeout");
    require(successful_result->succeeded && !successful_result->cancelled &&
                !successful_result->silent && successful_result->audio_path == audio_path &&
                successful_result->layer_id == 41 &&
                successful_result->property == TransformProperty::Scale &&
                successful_result->keyframes.size() == 49 &&
                successful_result->keyframes.back().frame == 48,
            "the worker returns keyframes and preserves request identity on success");
    require(!worker_progress.empty() &&
                std::all_of(worker_progress.begin(), worker_progress.end(),
                    [](int progress) { return progress >= 0 && progress <= 100; }),
            "the worker reports bounded audio analysis progress");
    require(success_callbacks_on_receiver_thread,
            "worker progress and completion callbacks run on the receiver thread");

    const auto log_before_cancel = readTextFile(logger.log_path());
    std::optional<motion::ui::AudioKeyframeGenerationResult> cancelled_result;
    bool cancellation_callback_on_receiver_thread = true;
    motion::ui::AudioKeyframeGenerationWorker cancelled_worker(
        &worker_receiver, make_worker_request(audio_path, 42), {},
        [&](motion::ui::AudioKeyframeGenerationResult result) {
            cancellation_callback_on_receiver_thread =
                QThread::currentThread() == worker_receiver.thread();
            cancelled_result = std::move(result);
        });
    cancelled_worker.cancel();
    cancelled_worker.start();
    require(waitForWorker(cancelled_worker, [&] { return cancelled_result.has_value(); }),
            "the pre-cancelled audio keyframe worker completes before timeout");
    require(cancelled_result->cancelled && !cancelled_result->succeeded &&
                cancelled_result->error_message.empty() &&
                cancelled_result->keyframes.empty() &&
                cancellation_callback_on_receiver_thread,
            "cancellation before worker start returns a cancelled result without an error");
    require(readTextFile(logger.log_path()) == log_before_cancel,
            "expected cancellation does not add an error log entry");

    std::optional<motion::ui::AudioKeyframeGenerationResult> failed_result;
    bool failure_callback_on_receiver_thread = true;
    constexpr LayerId failed_layer_id = 73;
    motion::ui::AudioKeyframeGenerationWorker failed_worker(
        &worker_receiver, make_worker_request(invalid_path, failed_layer_id), {},
        [&](motion::ui::AudioKeyframeGenerationResult result) {
            failure_callback_on_receiver_thread =
                QThread::currentThread() == worker_receiver.thread();
            failed_result = std::move(result);
        });
    failed_worker.start();
    require(waitForWorker(failed_worker, [&] { return failed_result.has_value(); }),
            "the failing audio keyframe worker completes before timeout");
    require(!failed_result->succeeded && !failed_result->cancelled &&
                !failed_result->error_message.empty() &&
                failed_result->audio_path == invalid_path &&
                failed_result->layer_id == failed_layer_id &&
                failure_callback_on_receiver_thread,
            "worker failure returns the technical error and preserves request identity");
    const auto failure_log = readTextFile(logger.log_path());
    require(failure_log.find("subsystem=\"motion_audio\"") != std::string::npos &&
                failure_log.find("operation=\"generate_keyframes\"") != std::string::npos &&
                failure_log.find("path=\"" + pathToUtf8(invalid_path) + "\"") !=
                    std::string::npos &&
                failure_log.find("layer_id=\"73\"") != std::string::npos,
            "worker failure logs its operation, audio path, and layer ID");

    std::atomic_bool already_cancelled{true};
    bool cancellation_observed = false;
    try {
        static_cast<void>(motion::audio::AudioEnvelopeAnalyzer::analyze(
            audio_path, FrameRate{24, 1}, 48, already_cancelled));
    } catch (const motion::audio::AudioAnalysisCancelled&) {
        cancellation_observed = true;
    }
    require(cancellation_observed, "audio analysis honors cancellation");
    bool keyframe_cancellation_observed = false;
    try {
        static_cast<void>(motion::audio::generateAudioKeyframes(
            envelope, 72, TransformProperty::Scale, 1.0, 2.0, 100,
            &already_cancelled));
    } catch (const motion::audio::AudioAnalysisCancelled&) {
        keyframe_cancellation_observed = true;
    }
    require(keyframe_cancellation_observed,
            "cancellation also prevents a pending keyframe result from being applied");

    const auto empty_path = fixture_directory / "empty.wav";
    writeWave(empty_path, 48'000, 1, {});
    const auto empty_audio = motion::audio::AudioEnvelopeAnalyzer::analyze(
        empty_path, FrameRate{24, 1}, 48, cancel_requested);
    require(empty_audio.rms_by_frame.empty() && near(empty_audio.peak_rms, 0.0) &&
                motion::audio::generateAudioKeyframes(
                    empty_audio, 72, TransformProperty::Scale, 1.0, 2.0, 100).empty(),
            "an audio stream with no samples leaves the destination track untouched");

    CompositionDocument document(640, 360, FrameRate{24, 1});
    LayerId layer_id = 0;
    require(document.addContentLayer(LayerKind::Shape, "Audio Shape", 0, &layer_id),
            "a composition layer is created for the generated animation");
    require(document.setLayerKeyframe(layer_id, TransformProperty::PositionX, 0, 0.25) &&
                document.setLayerKeyframe(layer_id, TransformProperty::PositionX, 5, 0.75),
            "an independent transform property receives existing keyframes");
    require(document.setLayerKeyframe(layer_id, TransformProperty::PositionY, 0, 0.2) &&
                document.setLayerKeyframe(layer_id, TransformProperty::Rotation, 0, 45.0) &&
                document.setLayerKeyframe(layer_id, TransformProperty::Opacity, 0, 0.8),
            "every other transform property also has existing keyframes");
    require(document.setLayerKeyframe(layer_id, TransformProperty::Scale, 0, 1.25),
            "the destination property receives an existing keyframe");
    const auto before_audio_generation = document;
    require(document.replaceLayerKeyframes(layer_id, TransformProperty::Scale, scale_keys),
            "generated keys replace the selected property track");
    const auto& changed_layer = document.layers().front();
    require(changed_layer.keyframes.position_x ==
                before_audio_generation.layers().front().keyframes.position_x,
            "replacing scale preserves Position X keyframes");
    require(changed_layer.keyframes.position_y ==
                before_audio_generation.layers().front().keyframes.position_y &&
                changed_layer.keyframes.rotation ==
                    before_audio_generation.layers().front().keyframes.rotation &&
                changed_layer.keyframes.opacity ==
                    before_audio_generation.layers().front().keyframes.opacity,
            "replacing scale preserves Position Y, rotation, and opacity keyframes");
    require(changed_layer.keyframes.scale == scale_keys,
            "the selected property contains only the generated keyframes");

    auto invalid_keys = scale_keys;
    invalid_keys[1].frame = invalid_keys[0].frame;
    const auto before_invalid_replace = document;
    require(!document.replaceLayerKeyframes(layer_id, TransformProperty::Scale, invalid_keys) &&
                document.layers() == before_invalid_replace.layers(),
            "an invalid replacement is rejected atomically");

    motion::ui::CompositionHistory history;
    history.recordBeforeEdit({before_audio_generation, layer_id});
    auto undone = history.undo({document, layer_id});
    require(undone.has_value() &&
                undone->document.layers() == before_audio_generation.layers(),
            "one Undo restores the prior transform track");
    auto redone = history.redo(*undone);
    require(redone.has_value() && redone->document.layers() == document.layers(),
            "Redo restores the generated transform track");

    motion::model::MotionProjectData project;
    project.composition = {document.canvasSize(), document.frameRate()};
    project.layers = document.layers();
    const auto document_path = fixture_directory / "generated.motion";
    motion::persistence::MotionDocumentStore::save(document_path, project);
    require(motion::persistence::MotionDocumentStore::load(document_path) == project,
            "generated keys round-trip through the existing Motion document format");

    motion::ui::AudioKeyframeDialog dialog(QStringLiteral("Audio Shape"));
    auto* property = dialog.findChild<QComboBox*>(
        QStringLiteral("motion-audio-keyframe-property"));
    auto* minimum = dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("motion-audio-keyframe-minimum"));
    auto* maximum = dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("motion-audio-keyframe-maximum"));
    require(property != nullptr && minimum != nullptr && maximum != nullptr,
            "the audio keyframe dialog exposes its transform controls");
    require(property->currentData().toInt() == static_cast<int>(TransformProperty::Scale) &&
                near(minimum->value(), 1.0) && near(maximum->value(), 2.0),
            "scale is the default property with its documented value range");
    property->setCurrentIndex(static_cast<int>(TransformProperty::PositionX));
    require(near(minimum->value(), 0.0) && near(maximum->value(), 1.0),
            "Position X uses normalized canvas coordinates");
    property->setCurrentIndex(static_cast<int>(TransformProperty::PositionY));
    require(near(minimum->value(), 0.0) && near(maximum->value(), 1.0),
            "Position Y uses normalized canvas coordinates");
    property->setCurrentIndex(static_cast<int>(TransformProperty::Rotation));
    require(near(minimum->value(), 0.0) && near(maximum->value(), 360.0),
            "rotation defaults to a 0 to 360 degree range");
    property->setCurrentIndex(static_cast<int>(TransformProperty::Opacity));
    require(near(minimum->value(), 0.0) && near(maximum->value(), 1.0),
            "opacity defaults to a 0 to 1 range");

    motion::ui::MainWindow window(
        nullptr, fixture_directory / "recovery", "audio-keyframe-generation-test");
    auto* action = window.findChild<QAction*>(
        QStringLiteral("motion-generate-audio-keyframes-action"));
    require(action != nullptr && !action->isEnabled(),
            "the optional command exists and is disabled without a selected layer");
    auto* new_composition = window.findChild<QAction*>(
        QStringLiteral("motion-new-composition-action"));
    require(new_composition != nullptr, "the New Composition command is available");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "New Composition opens its settings dialog");
        auto* width = dialog->findChild<QLineEdit*>(QStringLiteral("motion-canvas-width"));
        auto* height = dialog->findChild<QLineEdit*>(QStringLiteral("motion-canvas-height"));
        auto* frame_rate = dialog->findChild<QComboBox*>(QStringLiteral("motion-frame-rate"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(
            QStringLiteral("motion-new-composition-buttons"));
        require(width != nullptr && height != nullptr && frame_rate != nullptr &&
                    buttons != nullptr && buttons->button(QDialogButtonBox::Ok) != nullptr,
                "New Composition exposes its settings controls");
        width->setText(QStringLiteral("640"));
        height->setText(QStringLiteral("360"));
        frame_rate->setCurrentIndex(3);
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    new_composition->trigger();
    auto* new_rectangle = window.findChild<QAction*>(
        QStringLiteral("motion-new-rectangle-layer-action"));
    require(new_rectangle != nullptr, "the rectangle layer command is available");
    new_rectangle->trigger();
    require(action->isEnabled() && window.compositionDocument() != nullptr &&
                !window.compositionDocument()->layers().empty(),
            "selecting a layer enables the optional audio keyframe command");
    const auto before_dialog_cancel = window.compositionDocument()->layers();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr && dialog->objectName() ==
                    QStringLiteral("motion-audio-keyframe-dialog"),
                "the layer command opens the audio generation dialog");
        auto* selected_layer = dialog->findChild<QLabel*>(
            QStringLiteral("motion-audio-keyframe-layer"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(
            QStringLiteral("motion-audio-keyframe-buttons"));
        require(selected_layer != nullptr &&
                    selected_layer->text().contains(QStringLiteral("Rectangle 1")),
                "audio keyframes target the selected layer");
        require(buttons != nullptr && buttons->button(QDialogButtonBox::Cancel) != nullptr,
                "the audio keyframe dialog exposes Cancel");
        buttons->button(QDialogButtonBox::Cancel)->click();
    });
    action->trigger();
    require(window.compositionDocument()->layers() == before_dialog_cancel,
            "canceling audio keyframe setup leaves the composition unchanged");

    return EXIT_SUCCESS;
}
