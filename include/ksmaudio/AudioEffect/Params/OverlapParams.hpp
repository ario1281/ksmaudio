#pragma once
#include "ksmaudio/AudioEffect/AudioEffectParam.hpp"

namespace ksmaudio::AudioEffect
{
    struct OverlapDSPParams
    {
        float timeScale = 1.0f;              // タイムスケール（0.5 ~ 2.0）
        float quality = 1.0f;                // 品質（0.0=低, 1.0=中, 2.0=高）
        float mix = 1.0f;                    // ミックス量（0.0 ~ 1.0）
    };

    struct OverlapParams
    {
        // パラメータ定義
        Param timeScale = DefineParam(Type::kRate, "50%>200%");     // タイムスケール
        Param quality = DefineParam(Type::kSample, "0-2");          // 品質設定
        Param mix = DefineParam(Type::kRate, "0%>100%");            // ミックス量

        /// @brief パラメータ辞書（ParamIDとParamのマッピング）
        const std::unordered_map<ParamID, Param*> dict = {
            { ParamID::kTimeScale, &timeScale },
            { ParamID::kQuality, &quality },
            { ParamID::kMix, &mix },
        };

        /// @brief DSPパラメータのレンダリング
        /// @param status 現在のステータス
        /// @param isOn エフェクトON/OFF
        /// @return DSPパラメータ
        WSOLADSPParams render(const Status& status, bool isOn)
        {
            return {
                .timeScale = GetValue(timeScale, status, isOn),
                .quality = GetValue(quality, status, isOn),
                .mix = GetValue(mix, status, isOn),
            };
        }

        /// @brief FXレーン用のレンダリング
        /// @param status 現在のステータス
        /// @param laneIdx レーンインデックス
        /// @return DSPパラメータ
        WSOLADSPParams renderByFX(const Status& status, std::optional<std::size_t> laneIdx)
        {
            const bool isOn = laneIdx.has_value();
            return render(status, isOn);
        }

        /// @brief レーザー用のレンダリング
        /// @param status 現在のステータス
        /// @param isOn エフェクトON/OFF
        /// @return DSPパラメータ
        WSOLADSPParams renderByLaser(const Status& status, bool isOn)
        {
            return render(status, isOn);
        }
    };
}
