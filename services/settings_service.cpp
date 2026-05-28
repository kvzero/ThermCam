#include "services/settings_service.h"

#include "hardware/hardware_manager.h"
#include "hardware/hmi/system_control.h"
#include "hardware/imaging/thermal_camera.h"
#include "hardware/storage/storage_manager.h"
#include "processing/thermal_palette.h"
#include <QDebug>
#include <QStringList>

namespace {
constexpr float kEmissivityMin = 0.01f;
constexpr float kEmissivityMax = 1.0f;
constexpr float kThermographyOffsetMin = -10.0f;
constexpr float kThermographyOffsetMax = 10.0f;
constexpr float kLinearAgcMinCelsiusMin = -40.0f;
constexpr float kLinearAgcMinCelsiusMax = 600.0f;
constexpr float kLinearAgcMaxCelsiusMin = -40.0f;
constexpr float kLinearAgcMaxCelsiusMax = 600.0f;
constexpr int kScreenBrightnessPercentMin = 1;
constexpr int kScreenBrightnessPercentMax = 100;
constexpr int kAudioVolumePercentMin = 0;
constexpr int kAudioVolumePercentMax = 100;

QString keyToDebugName(SettingKey key) {
    for (const auto& descriptor : kSettingRegistry) {
        if (descriptor.key == key) {
            return QString::fromLatin1(descriptor.jsonName);
        }
    }
    return "unknown";
}

QString valueToDebugString(const QVariant& value) {
    if (value.type() == QVariant::Double) {
        return QString::number(value.toDouble(), 'f', 6);
    }
    return value.toString();
}

QVariant fallbackValueForKey(SettingKey key) {
    for (const auto& descriptor : kSettingRegistry) {
        if (descriptor.key == key) {
            return descriptor.defaultValue;
        }
    }
    return QVariant();
}

int normalizeStoragePriorityInt(int value) {
    const int sdFirst = static_cast<int>(StoragePriority::SdFirst);
    const int usbFirst = static_cast<int>(StoragePriority::UsbFirst);
    return (value == usbFirst) ? usbFirst : sdFirst;
}

RemovableStoragePriority toRemovablePriority(int value) {
    return (normalizeStoragePriorityInt(value) == static_cast<int>(StoragePriority::UsbFirst))
               ? RemovableStoragePriority::UsbFirst
               : RemovableStoragePriority::SdFirst;
}

int normalizeAgcModeInt(int value) {
    const int linear = static_cast<int>(AgcMode::LinearManual);
    return (value == linear) ? linear : static_cast<int>(AgcMode::HistEqAuto);
}

bool boolSettingFromSnapshot(const SettingsSnapshot& snapshot, SettingKey key, bool fallback) {
    const QVariant payload = snapshot.values.value(key, QVariant(fallback));
    if (!payload.canConvert<bool>()) return fallback;
    return payload.toBool();
}

float floatSettingFromSnapshot(const SettingsSnapshot& snapshot, SettingKey key, float fallback) {
    bool ok = false;
    const float value = snapshot.values.value(key, QVariant(fallback)).toFloat(&ok);
    return ok ? value : fallback;
}

int intSettingFromSnapshot(const SettingsSnapshot& snapshot, SettingKey key, int fallback) {
    bool ok = false;
    const int value = snapshot.values.value(key, QVariant(fallback)).toInt(&ok);
    return ok ? value : fallback;
}
}

SettingsService& SettingsService::instance() {
    static SettingsService inst;
    return inst;
}

SettingsService::SettingsService(QObject* parent) : QObject(parent) {}

bool SettingsService::init() {
    if (m_isInitialized) return true;

    SettingsStore& store = SettingsStore::instance();
    const SettingsSnapshot before = store.current();

    SettingsPatch bootPatch;
    bootPatch.values = before.values;

    SettingsPatch cleanPatch;
    QString normalizeError;
    if (!normalizePatch(bootPatch, &cleanPatch, &normalizeError)) {
        qWarning() << "[SettingsService] Boot-time full normalize failed:" << normalizeError
                   << "- fallback to per-key reconciliation.";

        cleanPatch.values.clear();
        for (auto it = bootPatch.values.constBegin(); it != bootPatch.values.constEnd(); ++it) {
            const SettingKey key = it.key();

            SettingsPatch singleInput;
            singleInput.values.insert(key, it.value());

            SettingsPatch singleOutput;
            QString singleError;
            if (normalizePatch(singleInput, &singleOutput, &singleError)) {
                cleanPatch.values.insert(key, singleOutput.values.value(key));
                continue;
            }

            const QVariant fallback = fallbackValueForKey(key);
            if (fallback.isValid()) {
                cleanPatch.values.insert(key, fallback);
                qWarning() << "[SettingsService] Boot-time key fallback:"
                           << keyToDebugName(key)
                           << "reason:" << singleError;
            } else {
                qWarning() << "[SettingsService] Boot-time key dropped:"
                           << keyToDebugName(key)
                           << "reason:" << singleError;
            }
        }
    }

    SettingsChangeEvent bootChange;
    QString persistError;
    if (!store.commitPatch(cleanPatch, &bootChange, &persistError)) {
        qWarning() << "[SettingsService] Boot-time reconciliation persist failed:" << persistError;
    } else if (!bootChange.changedKeys.isEmpty()) {
        for (auto it = bootChange.changedKeys.constBegin(); it != bootChange.changedKeys.constEnd(); ++it) {
            const SettingKey key = *it;
            const QVariant oldValue = before.values.value(key);
            const QVariant newValue = bootChange.snapshot.values.value(key);
            qWarning() << "[SettingsService] Boot-time reconciled"
                       << keyToDebugName(key)
                       << valueToDebugString(oldValue)
                       << "->"
                       << valueToDebugString(newValue);
        }
    }

    SettingsChangeEvent runtimeSync;
    runtimeSync.snapshot = store.current();
    for (const auto& descriptor : kSettingRegistry) {
        runtimeSync.changedKeys.insert(descriptor.key);
    }

    QString runtimeError;
    if (!applyRuntimeEffects(runtimeSync, &runtimeError)) {
        qWarning() << "[SettingsService] Boot-time runtime sync failed:" << runtimeError;
    }

    m_isInitialized = true;
    qInfo() << "[SettingsService] initialized";
    return true;
}

SettingsService::ApplyResult SettingsService::apply(const SettingsPatch& patch) {
    ApplyResult result;

    if (!m_isInitialized) {
        result.code = ApplyCode::PersistFailed;
        result.message = "SettingsService is not initialized";
        emit applyCompleted(result);
        return result;
    }

    SettingsPatch normalized;
    QString normalizeError;
    if (!normalizePatch(patch, &normalized, &normalizeError)) {
        result.code = ApplyCode::InvalidInput;
        result.message = normalizeError;
        emit applyCompleted(result);
        return result;
    }

    SettingsChangeEvent change;
    QString persistError;
    if (!SettingsStore::instance().commitPatch(normalized, &change, &persistError)) {
        result.code = ApplyCode::PersistFailed;
        result.message = persistError;
        emit applyCompleted(result);
        return result;
    }

    result.persisted = true;
    result.change = change;

    if (change.changedKeys.isEmpty()) {
        result.code = ApplyCode::NoChange;
        result.runtimeApplied = true;
        emit applyCompleted(result);
        return result;
    }

    QString runtimeError;
    if (!applyRuntimeEffects(change, &runtimeError)) {
        result.code = ApplyCode::RuntimeApplyFailed;
        result.message = runtimeError;
        emit applyCompleted(result);
        return result;
    }

    result.code = ApplyCode::Ok;
    result.runtimeApplied = true;
    emit applyCompleted(result);
    return result;
}

SettingsService::PreviewResult SettingsService::preview(const SettingsPatch& patch) {
    PreviewResult result;

    if (!m_isInitialized) {
        result.code = PreviewCode::NotInitialized;
        result.message = "SettingsService is not initialized";
        return result;
    }

    SettingsPatch normalized;
    QString normalizeError;
    if (!normalizePatch(patch, &normalized, &normalizeError)) {
        result.code = PreviewCode::InvalidInput;
        result.message = normalizeError;
        return result;
    }

    result.normalizedPatch = normalized;

    if (normalized.values.isEmpty()) {
        result.code = PreviewCode::NoChange;
        result.runtimeApplied = true;
        result.previewChange.snapshot = SettingsStore::instance().current();
        return result;
    }

    SettingsChangeEvent previewChange;
    previewChange.snapshot = SettingsStore::instance().current();
    for (auto it = normalized.values.constBegin(); it != normalized.values.constEnd(); ++it) {
        previewChange.snapshot.values.insert(it.key(), it.value());
        previewChange.changedKeys.insert(it.key());
    }
    result.previewChange = previewChange;

    QString runtimeError;
    if (!applyRuntimeEffects(previewChange, &runtimeError)) {
        result.code = PreviewCode::RuntimeApplyFailed;
        result.message = runtimeError;
        return result;
    }

    result.code = PreviewCode::Ok;
    result.runtimeApplied = true;
    return result;
}

bool SettingsService::triggerFlatSceneCorrection(QString* outError) {
    if (!m_isInitialized) {
        if (outError) *outError = "SettingsService is not initialized";
        return false;
    }

    auto* camera = HardwareManager::instance().camera();
    if (!camera) {
        if (outError) *outError = "ThermalCamera is unavailable";
        return false;
    }

    return camera->triggerFlatSceneCorrection(outError);
}

bool SettingsService::normalizePatch(const SettingsPatch& input,
                                     SettingsPatch* outPatch,
                                     QString* outError) const {
    if (!outPatch) {
        if (outError) *outError = "Null output patch";
        return false;
    }

    outPatch->values.clear();

    if (input.values.isEmpty()) {
        return true;
    }

    for (auto it = input.values.constBegin(); it != input.values.constEnd(); ++it) {
        const SettingKey key = it.key();
        const QVariant value = it.value();

        switch (key) {
        case SettingKey::Palette: {
            if (!value.canConvert<int>()) {
                if (outError) *outError = "Invalid palette payload";
                return false;
            }

            bool ok = false;
            const int paletteId = value.toInt(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid palette payload";
                return false;
            }

            if (paletteId < 0 || paletteId >= static_cast<int>(ThermalPalette::Id::Count)) {
                if (outError) *outError = "Unsupported palette id";
                return false;
            }

            outPatch->values.insert(key, QVariant(paletteId));
            continue;
        }
        case SettingKey::Emissivity: {
            bool ok = false;
            float v = value.toFloat(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid emissivity payload";
                return false;
            }
            v = qBound(kEmissivityMin, v, kEmissivityMax);
            outPatch->values.insert(key, QVariant(v));
            continue;
        }
        case SettingKey::TemperatureUnit: {
            int unitValue = static_cast<int>(TemperatureUnit::Celsius);

            if (value.canConvert<int>()) {
                bool ok = false;
                const int parsed = value.toInt(&ok);
                if (!ok) {
                    if (outError) *outError = "Invalid temperature unit payload";
                    return false;
                }
                unitValue = parsed;
            } else {
                if (outError) *outError = "Invalid temperature unit payload";
                return false;
            }

            if (unitValue != static_cast<int>(TemperatureUnit::Celsius) &&
                unitValue != static_cast<int>(TemperatureUnit::Fahrenheit)) {
                if (outError) *outError = "Unsupported temperature unit";
                return false;
            }

            outPatch->values.insert(key, QVariant(unitValue));
            continue;
        }
        case SettingKey::StoragePriority: {
            if (!value.canConvert<int>()) {
                if (outError) *outError = "Invalid storage priority payload";
                return false;
            }

            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid storage priority payload";
                return false;
            }

            if (parsed != static_cast<int>(StoragePriority::SdFirst) &&
                parsed != static_cast<int>(StoragePriority::UsbFirst)) {
                if (outError) *outError = "Unsupported storage priority";
                return false;
            }

            outPatch->values.insert(key, QVariant(parsed));
            continue;
        }
        case SettingKey::SaveMarkerInMedia: {
            if (!value.canConvert<bool>()) {
                if (outError) *outError = "Invalid save-marker payload";
                return false;
            }

            outPatch->values.insert(key, QVariant(value.toBool()));
            continue;
        }
        case SettingKey::HideMarkerWhenHudHidden: {
            if (!value.canConvert<bool>()) {
                if (outError) *outError = "Invalid HUD-marker payload";
                return false;
            }

            outPatch->values.insert(key, QVariant(value.toBool()));
            continue;
        }
        case SettingKey::ShutterAutoEnabled: {
            if (!value.canConvert<bool>()) {
                if (outError) *outError = "Invalid shutter-auto payload";
                return false;
            }

            outPatch->values.insert(key, QVariant(value.toBool()));
            continue;
        }
        case SettingKey::ThermographyOffsetCelsius: {
            bool ok = false;
            float offset = value.toFloat(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid thermography-offset payload";
                return false;
            }

            offset = qBound(kThermographyOffsetMin, offset, kThermographyOffsetMax);
            outPatch->values.insert(key, QVariant(offset));
            continue;
        }
        case SettingKey::SeekVisionEnabled: {
            if (!value.canConvert<bool>()) {
                if (outError) *outError = "Invalid seekvision payload";
                return false;
            }

            outPatch->values.insert(key, QVariant(value.toBool()));
            continue;
        }
        case SettingKey::LegacySharpenEnabled: {
            if (!value.canConvert<bool>()) {
                if (outError) *outError = "Invalid sharpen payload";
                return false;
            }

            outPatch->values.insert(key, QVariant(value.toBool()));
            continue;
        }
        case SettingKey::AgcMode: {
            if (!value.canConvert<int>()) {
                if (outError) *outError = "Invalid AGC mode payload";
                return false;
            }

            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid AGC mode payload";
                return false;
            }

            const int normalized = normalizeAgcModeInt(parsed);
            if (parsed != normalized) {
                if (outError) *outError = "Unsupported AGC mode";
                return false;
            }

            outPatch->values.insert(key, QVariant(normalized));
            continue;
        }
        case SettingKey::LinearAgcMinCelsius:
        case SettingKey::LinearAgcMaxCelsius: {
            bool ok = false;
            float valueC = value.toFloat(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid Linear AGC payload";
                return false;
            }

            if (key == SettingKey::LinearAgcMinCelsius) {
                valueC = qBound(kLinearAgcMinCelsiusMin, valueC, kLinearAgcMinCelsiusMax);
            } else {
                valueC = qBound(kLinearAgcMaxCelsiusMin, valueC, kLinearAgcMaxCelsiusMax);
            }

            outPatch->values.insert(key, QVariant(valueC));
            continue;
        }
        case SettingKey::ScreenBrightnessPercent: {
            if (!value.canConvert<int>()) {
                if (outError) *outError = "Invalid screen-brightness payload";
                return false;
            }

            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid screen-brightness payload";
                return false;
            }

            const int clamped =
                qBound(kScreenBrightnessPercentMin, parsed, kScreenBrightnessPercentMax);
            outPatch->values.insert(key, QVariant(clamped));
            continue;
        }
        case SettingKey::AudioVolumePercent: {
            if (!value.canConvert<int>()) {
                if (outError) *outError = "Invalid audio-volume payload";
                return false;
            }

            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid audio-volume payload";
                return false;
            }

            const int clamped = qBound(kAudioVolumePercentMin, parsed, kAudioVolumePercentMax);
            outPatch->values.insert(key, QVariant(clamped));
            continue;
        }
        }

        if (outError) *outError = "Unsupported setting key";
        return false;
    }

    return true;
}

bool SettingsService::applyRuntimeEffects(const SettingsChangeEvent& change, QString* outError) {
    bool allSuccess = true;
    QStringList errors;

    if (change.changedKeys.contains(SettingKey::Palette)) {
        const QVariant payload = change.snapshot.values.value(SettingKey::Palette);
        bool ok = false;
        const int paletteId = payload.toInt(&ok);
        if (!ok) {
            errors << "Committed palette value is invalid";
            allSuccess = false;
        } else if (paletteId < 0 || paletteId >= static_cast<int>(ThermalPalette::Id::Count)) {
            errors << "Committed palette id is unsupported";
            allSuccess = false;
        } else {
            emit paletteChanged(paletteId);
        }
    }

    const bool hasCameraSettingDelta =
        change.changedKeys.contains(SettingKey::Emissivity) ||
        change.changedKeys.contains(SettingKey::ShutterAutoEnabled) ||
        change.changedKeys.contains(SettingKey::ThermographyOffsetCelsius) ||
        change.changedKeys.contains(SettingKey::SeekVisionEnabled) ||
        change.changedKeys.contains(SettingKey::LegacySharpenEnabled) ||
        change.changedKeys.contains(SettingKey::AgcMode) ||
        change.changedKeys.contains(SettingKey::LinearAgcMinCelsius) ||
        change.changedKeys.contains(SettingKey::LinearAgcMaxCelsius);

    if (hasCameraSettingDelta) {
        auto* camera = HardwareManager::instance().camera();
        if (!camera) {
            errors << "ThermalCamera is unavailable";
            allSuccess = false;
        } else {
            auto applyOrCollect = [&errors, &allSuccess](const char* label,
                                                         bool ok,
                                                         const QString& detail) {
                if (ok) return;
                errors << QString("%1: %2").arg(QString::fromLatin1(label), detail);
                allSuccess = false;
            };

            const float emissivity = qBound(
                kEmissivityMin,
                floatSettingFromSnapshot(change.snapshot, SettingKey::Emissivity, 0.95f),
                kEmissivityMax);
            const bool seekVisionEnabled = boolSettingFromSnapshot(
                change.snapshot, SettingKey::SeekVisionEnabled, true);
            const bool shutterAutoEnabled = boolSettingFromSnapshot(
                change.snapshot, SettingKey::ShutterAutoEnabled, true);
            const float thermographyOffset = qBound(
                kThermographyOffsetMin,
                floatSettingFromSnapshot(change.snapshot,
                                         SettingKey::ThermographyOffsetCelsius,
                                         0.0f),
                kThermographyOffsetMax);
            const bool legacySharpenEnabled = boolSettingFromSnapshot(
                change.snapshot, SettingKey::LegacySharpenEnabled, false);
            const int agcModeValue = normalizeAgcModeInt(intSettingFromSnapshot(
                change.snapshot,
                SettingKey::AgcMode,
                static_cast<int>(AgcMode::HistEqAuto)));
            const float linearMinCelsius = qBound(
                kLinearAgcMinCelsiusMin,
                floatSettingFromSnapshot(change.snapshot,
                                         SettingKey::LinearAgcMinCelsius,
                                         20.0f),
                kLinearAgcMinCelsiusMax);
            const float linearMaxCelsius = qBound(
                kLinearAgcMaxCelsiusMin,
                floatSettingFromSnapshot(change.snapshot,
                                         SettingKey::LinearAgcMaxCelsius,
                                         80.0f),
                kLinearAgcMaxCelsiusMax);

            if (change.changedKeys.contains(SettingKey::Emissivity)) {
                camera->setEmissivity(emissivity);
            }

            const bool pipelineChanged = change.changedKeys.contains(SettingKey::SeekVisionEnabled);
            const bool shutterChanged = change.changedKeys.contains(SettingKey::ShutterAutoEnabled);
            const bool offsetChanged =
                change.changedKeys.contains(SettingKey::ThermographyOffsetCelsius);
            const bool sharpenChanged =
                change.changedKeys.contains(SettingKey::LegacySharpenEnabled);
            const bool agcModeChanged = change.changedKeys.contains(SettingKey::AgcMode);
            const bool linearRangeChanged =
                change.changedKeys.contains(SettingKey::LinearAgcMinCelsius) ||
                change.changedKeys.contains(SettingKey::LinearAgcMaxCelsius);

            QString cameraError;
            if (pipelineChanged) {
                applyOrCollect("Pipeline", camera->setPipelineMode(
                                               seekVisionEnabled
                                                   ? ThermalCamera::PipelineMode::SeekVision
                                                   : ThermalCamera::PipelineMode::Legacy,
                                               &cameraError),
                               cameraError);
            }

            if (shutterChanged) {
                cameraError.clear();
                applyOrCollect("Shutter mode", camera->setShutterMode(
                                                   shutterAutoEnabled
                                                       ? ThermalCamera::ShutterMode::Auto
                                                       : ThermalCamera::ShutterMode::Manual,
                                                   &cameraError),
                               cameraError);
            }

            if (offsetChanged) {
                cameraError.clear();
                applyOrCollect("Thermography offset",
                               camera->setThermographyOffsetCelsius(thermographyOffset,
                                                                    &cameraError),
                               cameraError);
            }

            if (sharpenChanged || pipelineChanged) {
                cameraError.clear();
                applyOrCollect("Sharpen filter", camera->setSharpenFilterEnabled(
                                                      legacySharpenEnabled, &cameraError),
                               cameraError);
            }

            const ThermalCamera::AgcMode agcMode =
                (agcModeValue == static_cast<int>(AgcMode::LinearManual))
                    ? ThermalCamera::AgcMode::Linear
                    : ThermalCamera::AgcMode::HistEq;

            if (agcModeChanged || pipelineChanged) {
                cameraError.clear();
                applyOrCollect("AGC mode", camera->setAgcMode(agcMode, &cameraError),
                               cameraError);
            }

            if (agcMode == ThermalCamera::AgcMode::Linear &&
                (linearRangeChanged || agcModeChanged || pipelineChanged)) {
                cameraError.clear();
                applyOrCollect("Linear AGC range",
                               camera->setLinearAgcManualRangeCelsius(
                                   linearMinCelsius, linearMaxCelsius, &cameraError),
                               cameraError);
            }
        }
    }

    if (change.changedKeys.contains(SettingKey::TemperatureUnit)) {
        const QVariant payload = change.snapshot.values.value(SettingKey::TemperatureUnit);
        bool ok = false;
        const int unitValue = payload.toInt(&ok);
        if (!ok) {
            errors << "Committed temperature unit value is invalid";
            allSuccess = false;
        } else if (unitValue != static_cast<int>(TemperatureUnit::Celsius) &&
                   unitValue != static_cast<int>(TemperatureUnit::Fahrenheit)) {
            errors << "Committed temperature unit is unsupported";
            allSuccess = false;
        } else {
            const bool isFahrenheit =
                (unitValue == static_cast<int>(TemperatureUnit::Fahrenheit));
            emit unitChanged(isFahrenheit);
        }
    }

    if (change.changedKeys.contains(SettingKey::StoragePriority)) {
        const QVariant payload = change.snapshot.values.value(SettingKey::StoragePriority);
        bool ok = false;
        const int priorityValue = payload.toInt(&ok);
        if (!ok) {
            errors << "Committed storage priority value is invalid";
            allSuccess = false;
        } else if (priorityValue != static_cast<int>(StoragePriority::SdFirst) &&
                   priorityValue != static_cast<int>(StoragePriority::UsbFirst)) {
            errors << "Committed storage priority is unsupported";
            allSuccess = false;
        } else {
            if (auto* storage = HardwareManager::instance().storage()) {
                StorageRoutingPolicy policy = storage->routingPolicy();
                const RemovableStoragePriority resolved = toRemovablePriority(priorityValue);
                policy.photoPriority = resolved;
                policy.videoPriority = resolved;

                QString policyError;
                if (!storage->setRoutingPolicy(policy, &policyError)) {
                    errors << QString("Failed to apply storage policy: %1").arg(policyError);
                    allSuccess = false;
                }
            } else {
                errors << "StorageManager is unavailable";
                allSuccess = false;
            }
        }
    }

    if (change.changedKeys.contains(SettingKey::SaveMarkerInMedia)) {
        const QVariant payload = change.snapshot.values.value(SettingKey::SaveMarkerInMedia);
        if (!payload.canConvert<bool>()) {
            errors << "Committed save-marker value is invalid";
            allSuccess = false;
        } else {
            emit saveMarkerChanged(payload.toBool());
        }
    }

    if (change.changedKeys.contains(SettingKey::HideMarkerWhenHudHidden)) {
        const QVariant payload = change.snapshot.values.value(SettingKey::HideMarkerWhenHudHidden);
        if (!payload.canConvert<bool>()) {
            errors << "Committed HUD-marker value is invalid";
            allSuccess = false;
        } else {
            emit hudHideMarkerChanged(payload.toBool());
        }
    }

    const bool hasSystemSettingDelta =
        change.changedKeys.contains(SettingKey::ScreenBrightnessPercent) ||
        change.changedKeys.contains(SettingKey::AudioVolumePercent);

    if (hasSystemSettingDelta) {
        auto* systemControl = HardwareManager::instance().systemControl();
        if (!systemControl) {
            errors << "SystemControl is unavailable";
            allSuccess = false;
        } else {
            auto applyOrCollect = [&errors, &allSuccess](const char* label,
                                                         bool ok,
                                                         const QString& detail) {
                if (ok) return;
                errors << QString("%1: %2").arg(QString::fromLatin1(label), detail);
                allSuccess = false;
            };

            if (change.changedKeys.contains(SettingKey::ScreenBrightnessPercent)) {
                const int brightnessPercent =
                    qBound(kScreenBrightnessPercentMin,
                           intSettingFromSnapshot(change.snapshot,
                                                  SettingKey::ScreenBrightnessPercent,
                                                  80),
                           kScreenBrightnessPercentMax);

                QString brightnessError;
                applyOrCollect("Screen brightness",
                               systemControl->setScreenBrightnessPercent(brightnessPercent,
                                                                        &brightnessError),
                               brightnessError);
            }

            if (change.changedKeys.contains(SettingKey::AudioVolumePercent)) {
                const int volumePercent =
                    qBound(kAudioVolumePercentMin,
                           intSettingFromSnapshot(change.snapshot,
                                                  SettingKey::AudioVolumePercent,
                                                  50),
                           kAudioVolumePercentMax);

                QString volumeError;
                applyOrCollect("Audio volume",
                               systemControl->setAudioVolumePercent(volumePercent, &volumeError),
                               volumeError);
            }
        }
    }

    if (outError) *outError = errors.join("; ");
    return allSuccess;
}
