#ifndef SETTINGS_SERVICE_H
#define SETTINGS_SERVICE_H

#include <QObject>
#include "core/settings_store.h"

/**
 * @brief Unified write router for settings mutation and runtime apply side-effects.
 *
 * Architectural Role:
 * - Single write entry for upper modules.
 * - Owns input normalization and key-level business validation.
 *
 * Owner Scope:
 * - Does not own persisted truth snapshot.
 * - Delegates commit to SettingsStore and applies post-commit runtime hooks.
 *
 * Lifecycle:
 * - Process-wide singleton.
 * - Initialized once after hardware manager is ready.
 */
class SettingsService : public QObject {
    Q_OBJECT
public:
    /* --- Result Types --- */

    enum class ApplyCode : quint8 {
        Ok = 0,
        NoChange,
        InvalidInput,
        PersistFailed,
        RuntimeApplyFailed
    };

    struct ApplyResult {
        ApplyCode code = ApplyCode::Ok;
        QString message;
        SettingsChangeEvent change;
        bool persisted = false;
        bool runtimeApplied = false;
    };

    enum class PreviewCode : quint8 {
        Ok = 0,
        NoChange,
        InvalidInput,
        RuntimeApplyFailed,
        NotInitialized
    };

    struct PreviewResult {
        PreviewCode code = PreviewCode::Ok;
        QString message;
        SettingsPatch normalizedPatch;
        SettingsChangeEvent previewChange;
        bool runtimeApplied = false;
    };

    /* --- Lifecycle --- */

    static SettingsService& instance();

    bool init();

    /* --- Mutation API --- */

    /**
     * @brief Validates and applies one patch transaction.
     *
     * Contract:
     * - Flat validation lives here, no dynamic schema engine.
     * - Persistence success is not rolled back when runtime apply fails.
     */
    ApplyResult apply(const SettingsPatch& patch);

    /**
     * @brief Validates and applies runtime-only preview without persistence.
     *
     * Contract:
     * - Uses the same normalization path as apply().
     * - Never mutates SettingsStore committed snapshot.
     */
    PreviewResult preview(const SettingsPatch& patch);

signals:
    /**
     * @brief Transaction callback for cross-module diagnostics or deferred UI feedback.
     */
    void applyCompleted(const SettingsService::ApplyResult& result);

private:
    explicit SettingsService(QObject* parent = nullptr);

    /* --- Validation & Normalization --- */

    bool normalizePatch(const SettingsPatch& input,
                        SettingsPatch* outPatch,
                        QString* outError) const;

    /* --- Runtime Routing --- */

    bool applyRuntimeEffects(const SettingsChangeEvent& change, QString* outError);

    /* --- Runtime State --- */

    bool m_isInitialized = false;
};

Q_DECLARE_METATYPE(SettingsService::ApplyCode)
Q_DECLARE_METATYPE(SettingsService::ApplyResult)

#endif // SETTINGS_SERVICE_H
