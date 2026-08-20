#include "services/settings_service.h"

#include "hardware/hardware_manager.h"
#include "hardware/hmi/system_control.h"
#include "hardware/imaging/thermal_camera.h"
#include "hardware/storage/storage_manager.h"
#include "processing/thermal_palette.h"
#include <QDebug>
#include <QStringList>

namespace {
QString keyToDebugName(SettingKey key) {
    const SettingDescriptor* descriptor = settingDescriptorForKey(key);
    return descriptor ? QString::fromLatin1(descriptor->jsonName) : QStringLiteral("unknown");
}

QString valueToDebugString(const QVariant& value) {
    if (value.type() == QVariant::Double) {
        return QString::number(value.toDouble(), 'f', 6);
    }
    return value.toString();
}

QVariant fallbackValueForKey(SettingKey key) {
    const SettingDescriptor* descriptor = settingDescriptorForKey(key);
    return descriptor ? descriptor->defaultValue : QVariant();
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

SettingsService::ApplyResult SettingsService::restoreDefaults() {
    SettingsPatch patch;
    for (const SettingDescriptor& descriptor : kSettingRegistry) {
        patch.values.insert(descriptor.key, descriptor.defaultValue);
    }
    return apply(patch);
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
        const SettingDescriptor* descriptor = settingDescriptorForKey(key);
        if (!descriptor) {
            if (outError) *outError = "Unsupported setting key";
            return false;
        }

        if (key == SettingKey::Palette) {
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

        if (descriptor->valueType == SettingValueType::Boolean) {
            if (!value.canConvert<bool>()) {
                if (outError) *outError = "Invalid boolean payload";
                return false;
            }
            outPatch->values.insert(key, value.toBool());
            continue;
        }

        bool ok = false;
        if (descriptor->valueType == SettingValueType::Integer) {
            const int parsed = value.toInt(&ok);
            if (!ok) {
                if (outError) *outError = "Invalid integer payload";
                return false;
            }
            if (descriptor->integerRangePolicy == SettingIntegerRangePolicy::Reject &&
                (parsed < descriptor->minimum || parsed > descriptor->maximum)) {
                if (outError) *outError = "Unsupported integer value";
                return false;
            }
            outPatch->values.insert(
                key, qBound(qRound(descriptor->minimum), parsed, qRound(descriptor->maximum)));
            continue;
        }

        const float parsed = value.toFloat(&ok);
        if (!ok) {
            if (outError) *outError = "Invalid floating-point payload";
            return false;
        }
        outPatch->values.insert(key, qBound(float(descriptor->minimum), parsed,
                                             float(descriptor->maximum)));
    }

    return true;
}

bool SettingsService::applyRuntimeEffects(const SettingsChangeEvent& change, QString* outError) {
    bool allSuccess = true;
    QStringList errors;

    if (change.changedKeys.contains(SettingKey::Palette)) {
        emit paletteChanged(change.snapshot.values.value(SettingKey::Palette).toInt());
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

            const float emissivity =
                change.snapshot.values.value(SettingKey::Emissivity).toFloat();
            const bool seekVisionEnabled =
                change.snapshot.values.value(SettingKey::SeekVisionEnabled).toBool();
            const bool shutterAutoEnabled =
                change.snapshot.values.value(SettingKey::ShutterAutoEnabled).toBool();
            const float thermographyOffset = change.snapshot.values
                                                  .value(SettingKey::ThermographyOffsetCelsius)
                                                  .toFloat();
            const bool legacySharpenEnabled =
                change.snapshot.values.value(SettingKey::LegacySharpenEnabled).toBool();
            const int agcModeValue = change.snapshot.values.value(SettingKey::AgcMode).toInt();
            const float linearMinCelsius = change.snapshot.values
                                                .value(SettingKey::LinearAgcMinCelsius)
                                                .toFloat();
            const float linearMaxCelsius = change.snapshot.values
                                                .value(SettingKey::LinearAgcMaxCelsius)
                                                .toFloat();

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
        emit unitChanged(change.snapshot.values.value(SettingKey::TemperatureUnit).toInt() ==
                         static_cast<int>(TemperatureUnit::Fahrenheit));
    }

    if (change.changedKeys.contains(SettingKey::StoragePriority)) {
        const int priorityValue = change.snapshot.values.value(SettingKey::StoragePriority).toInt();
        if (auto* storage = HardwareManager::instance().storage()) {
            StorageRoutingPolicy policy = storage->routingPolicy();
            const RemovableStoragePriority resolved =
                priorityValue == static_cast<int>(StoragePriority::UsbFirst)
                    ? RemovableStoragePriority::UsbFirst
                    : RemovableStoragePriority::SdFirst;
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

    if (change.changedKeys.contains(SettingKey::SaveMarkerInMedia)) {
        emit saveMarkerChanged(change.snapshot.values.value(SettingKey::SaveMarkerInMedia)
                                   .toBool());
    }

    if (change.changedKeys.contains(SettingKey::HideMarkerWhenHudHidden)) {
        emit hudHideMarkerChanged(change.snapshot.values
                                      .value(SettingKey::HideMarkerWhenHudHidden)
                                      .toBool());
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
                    change.snapshot.values.value(SettingKey::ScreenBrightnessPercent).toInt();

                QString brightnessError;
                applyOrCollect("Screen brightness",
                               systemControl->setScreenBrightnessPercent(brightnessPercent,
                                                                        &brightnessError),
                               brightnessError);
            }

            if (change.changedKeys.contains(SettingKey::AudioVolumePercent)) {
                const int volumePercent =
                    change.snapshot.values.value(SettingKey::AudioVolumePercent).toInt();

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
