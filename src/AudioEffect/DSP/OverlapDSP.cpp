#include "ksmaudio/AudioEffect/DSP/WSOLADSP.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace ksmaudio::AudioEffect
{
    OverlapDSP::OverlapDSP(const DSPCommonInfo& info)
        : m_info(info)
    {
        // チャンネル数分のバッファを確保
        const std::size_t numChannels = m_info.numChannels;
        
        m_inputBuffer.resize(numChannels);
        m_overlapBuffer.resize(numChannels);

        // バッファの初期化
        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            std::fill(m_inputBuffer[ch].begin(), m_inputBuffer[ch].end(), 0.0f);
            std::fill(m_overlapBuffer[ch].begin(), m_overlapBuffer[ch].end(), 0.0f);
        }

        // ウィンドウ関数の初期化
        initializeWindowFunction();

        // 内部パラメータの計算
        updateInternalParameters();
    }

    void OverlapDSP::initializeWindowFunction()
    {
        m_windowFunction.resize(kMaxFrameSize);
        
        // ハニング窓の生成
        for (std::size_t i = 0; i < kMaxFrameSize; ++i)
        {
            m_windowFunction[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (kMaxFrameSize - 1)));
        }
    }

    std::size_t OverlapDSP::getFrameSizeFromQuality() const
    {
        // 品質に応じたフレームサイズ
        if (m_quality < 0.5f)
        {
            return 512;   // 低品質（低レイテンシ）
        }
        else if (m_quality < 1.5f)
        {
            return 1024;  // 中品質（バランス）
        }
        else
        {
            return 2048;  // 高品質
        }
    }

    float OverlapDSP::getOverlapRatioFromQuality() const
    {
        // 品質に応じたオーバーラップ率
        if (m_quality < 0.5f)
        {
            return 0.4f;   // 40%
        }
        else if (m_quality < 1.5f)
        {
            return 0.5f;   // 50%
        }
        else
        {
            return 0.75f;  // 75%
        }
    }

    void OverlapDSP::updateInternalParameters()
    {
        // 品質に応じたパラメータ設定
        m_frameSize = getFrameSizeFromQuality();
        const float overlapRatio = getOverlapRatioFromQuality();
        m_overlapSize = static_cast<std::size_t>(m_frameSize * overlapRatio);

        // ホップサイズの計算
        m_analysisHop = m_frameSize - m_overlapSize;
        
        // タイムスケールに応じた合成ホップサイズ
        if (m_timeScale > 0.001f)
        {
            m_synthesisHop = static_cast<std::size_t>(m_analysisHop / m_timeScale);
        }
        else
        {
            m_synthesisHop = m_analysisHop;
        }
        
        // 探索範囲の設定
        m_searchRange = std::min(m_frameSize / 2, kMaxSearchRange);
    }

    void OverlapDSP::resetBuffers()
    {
        for (auto& buffer : m_inputBuffer)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        
        for (auto& buffer : m_overlapBuffer)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        
        m_inputCursor = 0;
        m_outputCursor = 0;
        m_processedSamples = 0;
    }

    void WSOLADSP::reset()
    {
        resetBuffers();
        m_timeScalePrev = m_timeScale;
    }

    void OverlapDSP::updateParams(const WSOLADSPParams& params)
    {
        // タイムスケール更新（0.5 ~ 2.0にクランプ）
        m_timeScale = std::clamp(params.timeScale, 0.5f, 2.0f);
        
        // 品質設定更新
        m_quality = std::clamp(params.quality, 0.0f, 2.0f);
        
        // ミックス量更新
        m_mix = std::clamp(params.mix, 0.0f, 1.0f);

        // パラメータが変更された場合のみ更新
        const bool qualityChanged = std::abs(m_quality - m_quality) > 0.1f;
        const bool timeScaleChanged = std::abs(m_timeScale - m_timeScalePrev) > 0.001f;

        if (qualityChanged || timeScaleChanged)
        {
            updateInternalParameters();
            
            // 大きな変更の場合はバッファをリセット
            if (std::abs(m_timeScale - m_timeScalePrev) > 0.1f)
            {
                resetBuffers();
            }
            
            m_timeScalePrev = m_timeScale;
        }
    }

    float OverlapDSP::calculateSimilarity(const float* sig1, const float* sig2, std::size_t size)
    {
        float correlation = 0.0f;
        float energy1 = 0.0f;
        float energy2 = 0.0f;

        // 正規化相互相関の計算
        for (std::size_t i = 0; i < size; ++i)
        {
            const float s1 = sig1[i];
            const float s2 = sig2[i];
            
            correlation += s1 * s2;
            energy1 += s1 * s1;
            energy2 += s2 * s2;
        }

        // ゼロ除算回避
        const float epsilon = 1e-10f;
        if (energy1 < epsilon || energy2 < epsilon)
        {
            return 0.0f;
        }

        // 正規化
        return correlation / (std::sqrt(energy1) * std::sqrt(energy2));
    }

    std::size_t OverlapDSP::findBestMatch(std::size_t channel, std::size_t targetPos)
    {
        const auto& inputBuf = m_inputBuffer[channel];
        const auto& overlapBuf = m_overlapBuffer[channel];

        // 探索範囲の設定
        const std::size_t searchStart = (targetPos > m_searchRange) 
                                       ? (targetPos - m_searchRange) 
                                       : 0;
        const std::size_t searchEnd = std::min(targetPos + m_searchRange, 
                                              kInputBufferMax - m_frameSize);

        std::size_t bestPos = targetPos;
        float maxSimilarity = -std::numeric_limits<float>::max();

        // 探索ループ
        for (std::size_t pos = searchStart; pos < searchEnd; pos += 4)  // ステップを4にして高速化
        {
            // オーバーラップ領域での類似度計算
            const float similarity = calculateSimilarity(
                overlapBuf.data(),
                &inputBuf[pos % kInputBufferMax],
                m_overlapSize
            );

            if (similarity > maxSimilarity)
            {
                maxSimilarity = similarity;
                bestPos = pos;
            }
        }

        return bestPos;
    }

    void OverlapDSP::overlapAdd(std::size_t channel, std::size_t startPos, 
                              float* output, std::size_t outputPos, std::size_t outputSize)
    {
        auto& inputBuf = m_inputBuffer[channel];
        auto& overlapBuf = m_overlapBuffer[channel];

        // 出力範囲チェック
        if (outputPos + m_frameSize > outputSize)
        {
            return;
        }

        // オーバーラップ部分の処理（クロスフェード）
        for (std::size_t i = 0; i < m_overlapSize; ++i)
        {
            // 線形クロスフェード係数
            const float fadeOut = 1.0f - static_cast<float>(i) / m_overlapSize;
            const float fadeIn = static_cast<float>(i) / m_overlapSize;
            
            // ウィンドウ関数の適用
            const std::size_t windowIdx = (i * kMaxFrameSize) / m_frameSize;
            const float window = m_windowFunction[windowIdx];
            
            // ブレンド
            const std::size_t inputIdx = (startPos + i) % kInputBufferMax;
            const float blended = overlapBuf[i] * fadeOut + 
                                 inputBuf[inputIdx] * fadeIn;
            
            output[outputPos + i] = blended * window;
        }

        // 非オーバーラップ部分のコピー
        const std::size_t nonOverlapSize = m_frameSize - m_overlapSize;
        for (std::size_t i = 0; i < nonOverlapSize; ++i)
        {
            const std::size_t idx = m_overlapSize + i;
            const std::size_t windowIdx = (idx * kMaxFrameSize) / m_frameSize;
            const float window = m_windowFunction[windowIdx];
            const std::size_t inputIdx = (startPos + idx) % kInputBufferMax;
            
            output[outputPos + idx] = inputBuf[inputIdx] * window;
        }

        // 次回用のオーバーラップバッファを更新
        for (std::size_t i = 0; i < m_overlapSize; ++i)
        {
            const std::size_t inputIdx = (startPos + m_frameSize - m_overlapSize + i) % kInputBufferMax;
            overlapBuf[i] = inputBuf[inputIdx];
        }
    }

    void OverlapDSP::process(float* pData, std::size_t dataSize, bool bypass, const OverlapDSPParams& params)
    {
        // バイパス処理
        if (bypass || std::abs(params.timeScale - 1.0f) < 0.001f)
        {
            return; // 変更なし
        }

        // パラメータから品質設定を変換
        OverlapDSPParams adjustedParams = params;
        
        // 品質パラメータに基づいてフレームサイズとオーバーラップを調整
        const int qualityLevel = static_cast<int>(std::round(params.quality));
        std::size_t frameSize = kDefaultFrameSize;
        std::size_t overlapRatio = 50;
        
        switch (qualityLevel)
        {
        case 0: // 低品質（低レイテンシ）
            frameSize = 512;
            overlapRatio = 40;
            m_searchRange = frameSize / 4;
            break;
        case 2: // 高品質
            frameSize = 2048;
            overlapRatio = 75;
            m_searchRange = frameSize / 2;
            break;
        default: // 中品質（バランス）
            frameSize = 1024;
            overlapRatio = 50;
            m_searchRange = frameSize / 2;
            break;
        }

        // 内部パラメータ構造体を作成
        OverlapDSPParams internalParams;
        internalParams.timeScale = params.timeScale;
        internalParams.quality = params.quality;
        internalParams.mix = params.mix;

        // フレームサイズとオーバーラップの更新
        if (frameSize != m_frameSize)
        {
            m_frameSize = frameSize;
            m_overlapSize = (frameSize * overlapRatio) / 100;
            updateInternalParameters();
        }

        updateParams(internalParams);

        const std::size_t numChannels = m_info.numChannels;
        const std::size_t numFrames = dataSize / numChannels;

        // 出力バッファの準備（必要に応じて）
        std::vector<float> dryBuffer;
        if (params.mix < 0.999f)
        {
            dryBuffer.assign(pData, pData + dataSize);
        }

        // 一時的な処理用バッファ
        std::vector<std::vector<float>> tempOutput(numChannels);
        for (auto& ch : tempOutput)
        {
            ch.resize(numFrames, 0.0f);
        }

        // インターリーブ解除
        for (std::size_t frame = 0; frame < numFrames; ++frame)
        {
            for (std::size_t ch = 0; ch < numChannels; ++ch)
            {
                const std::size_t inputPos = (m_inputPosition + frame) % m_inputBuffer[ch].size();
                m_inputBuffer[ch][inputPos] = pData[frame * numChannels + ch];
            }
        }

        // WSOLA処理
        std::size_t inputFrameOffset = 0;
        std::size_t outputFrameCount = 0;

        while (inputFrameOffset + m_frameSize < numFrames)
        {
            // 各チャンネルで処理
            for (std::size_t ch = 0; ch < numChannels; ++ch)
            {
                // 目標位置の計算
                const std::size_t targetPos = (m_inputPosition + inputFrameOffset + m_analysisHop) 
                                             % m_inputBuffer[ch].size();

                // 最適位置の探索
                const std::size_t bestPos = findBestMatch(ch, targetPos);

                // オーバーラップ加算
                if (outputFrameCount + m_frameSize <= numFrames)
                {
                    overlapAdd(ch, bestPos, tempOutput[ch].data(), outputFrameCount);
                }
            }

            inputFrameOffset += m_analysisHop;
            outputFrameCount += m_synthesisHop;

            if (outputFrameCount >= numFrames)
            {
                break;
            }
        }

        // インターリーブして出力
        const std::size_t outputSamples = std::min(outputFrameCount, numFrames);
        for (std::size_t frame = 0; frame < outputSamples; ++frame)
        {
            for (std::size_t ch = 0; ch < numChannels; ++ch)
            {
                const float wet = tempOutput[ch][frame];
                const float dry = (params.mix < 0.999f) 
                                ? dryBuffer[frame * numChannels + ch] 
                                : 0.0f;
                
                // ミックス
                pData[frame * numChannels + ch] = dry * (1.0f - params.mix) + wet * params.mix;
            }
        }

        // 位置の更新
        m_inputPosition = (m_inputPosition + numFrames) % m_inputBuffer[0].size();
    }
}
