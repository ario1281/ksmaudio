#pragma once
#include "ksmaudio/AudioEffect/AudioEffect.hpp"
#include "ksmaudio/AudioEffect/Params/OverlapParams.hpp"
#include <array>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace ksmaudio::AudioEffect
{
  class OverlapDSP
  {
    private:
        // 定数定義
        static constexpr std::size_t kMaxFrameSize = 4096;        // 最大フレームサイズ
        static constexpr std::size_t kMaxSearchRange = 2048;      // 最大探索範囲
        static constexpr std::size_t kDefaultFrameSize = 1024;    // デフォルトフレームサイズ
        static constexpr std::size_t kDefaultOverlapSize = 512;   // デフォルトオーバーラップ
        static constexpr float kMinTimeScale = 0.5f;              // 最小タイムスケール
        static constexpr float kMaxTimeScale = 2.0f;              // 最大タイムスケール

        // メンバ変数
        const DSPCommonInfo m_info;                               // DSP共通情報

        float m_timeScale = 1.0f;                                 // タイムスケール（速度倍率）
        float m_timeScalePrev = 1.0f;                             // 前回のタイムスケール
        std::size_t m_frameSize = kDefaultFrameSize;              // フレームサイズ
        std::size_t m_overlapSize = kDefaultOverlapSize;          // オーバーラップサイズ
        std::size_t m_searchRange = kDefaultFrameSize / 2;        // 探索範囲

        // バッファ
        std::vector<std::vector<float>> m_overlapBuffer;          // チャンネルごとのオーバーラップバッファ
        std::vector<std::vector<float>> m_inputBuffer;            // 入力バッファ（チャンネル別）
        std::vector<float> m_windowFunction;                      // ウィンドウ関数

        // 処理状態
        std::size_t m_inputPosition = 0;                          // 入力位置
        std::size_t m_outputPosition = 0;                         // 出力位置
        std::size_t m_synthesisHop = 0;                           // 合成ホップサイズ
        std::size_t m_analysisHop = 0;                            // 解析ホップサイズ

        // 内部処理関数
        
        /// @brief 最適なマッチング位置を探索
        /// @param channel チャンネルインデックス
        /// @param targetPos 目標位置
        /// @return 最適な開始位置
        std::size_t findBestMatch(std::size_t channel, std::size_t targetPos);

        /// @brief 2つの波形の類似度を計算（正規化相互相関）
        /// @param sig1 信号1
        /// @param sig2 信号2
        /// @param size サンプル数
        /// @return 類似度（-1.0 ~ 1.0）
        float calculateSimilarity(const float* sig1, const float* sig2, std::size_t size);

        /// @brief オーバーラップ加算処理
        /// @param channel チャンネルインデックス
        /// @param startPos 開始位置
        /// @param output 出力バッファ
        /// @param outPos 出力位置
        void overlapAdd(std::size_t channel, std::size_t startPos, 
                       float* output, std::size_t outPos);

        /// @brief ウィンドウ関数の初期化（ハニング窓）
        void initializeWindowFunction();

        /// @brief パラメータ変更時の内部状態更新
        void updateInternalParameters();

        /// @brief バッファのリセット
        void resetBuffers();

    public:
        /// @brief コンストラクタ
        /// @param info DSP共通情報
        explicit WSOLADSP(const DSPCommonInfo& info);

        /// @brief メイン処理
        /// @param pData インターリーブされた音声データ
        /// @param dataSize データサイズ（全チャンネル分）
        /// @param bypass バイパスフラグ
        /// @param params WSOLAパラメータ
        void process(float* pData, std::size_t dataSize, bool bypass, 
                    const WSOLADSPParams& params);

        /// @brief パラメータ更新
        /// @param params 新しいパラメータ
        void updateParams(const WSOLADSPParams& params);

        /// @brief リセット（状態初期化）
        void reset();

        /// @brief 現在のタイムスケールを取得
        /// @return タイムスケール値
        float getTimeScale() const { return m_timeScale; }

        /// @brief 現在のレイテンシを取得（サンプル数）
        /// @return レイテンシ
        std::size_t getLatency() const { return m_frameSize; }

  };
}
