#include "services/settings_service.h"

#include "hardware/hardware_manager.h"
#include "hardware/imaging/seekcam/seekcam.h"
#include <QDebug>
#include <QStringList>

namespace {
constexpr float kEmissivityMin = 0.01f;
constexpr float kEmissivityMax = 1.0f;

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
        }

        if (outError) *outError = "Unsupported setting key";
        return false;
    }

    return true;
}

bool SettingsService::applyRuntimeEffects(const SettingsChangeEvent& change, QString* outError) {
    bool allSuccess = true;
    QStringList errors;

    if (change.changedKeys.contains(SettingKey::Emissivity)) {
        const QVariant payload = change.snapshot.values.value(SettingKey::Emissivity);
        bool ok = false;
        const float emissivity = payload.toFloat(&ok);
        if (!ok) {
            errors << "Committed emissivity value is invalid";
            allSuccess = false;
        }

        if (auto* camera = HardwareManager::instance().camera()) {
            if (ok) {
                camera->setEmissivity(emissivity);
            }
        } else {
            errors << "ThermalCamera is unavailable";
            allSuccess = false;
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
        }
    }

    if (outError) *outError = errors.join("; ");
    return allSuccess;
}
