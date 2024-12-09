#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/Delay.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodesGranulator"

namespace Metasound
{
    namespace Granulator
    {
        METASOUND_PARAM(InParamAudioInput, "In", "Audio input.")
            METASOUND_PARAM(InParamGrainSize, "Grain Size", "Size of each grain in milliseconds.")
            METASOUND_PARAM(OutParamAudio, "Out", "Audio output.")
    }

    class FGranulator : public TExecutableOperator<FGranulator>
    {
    public:
        static const FNodeClassMetadata& GetNodeInfo();
        static const FVertexInterface& GetVertexInterface();
        static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults);

        FGranulator(const FBuildOperatorParams& InParams,
            const FAudioBufferReadRef& InAudioInput,
            const FFloatReadRef& InGrainSize);

        virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
        virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;
        void Execute();

    private:
        FAudioBufferReadRef AudioInput;
        FAudioBufferWriteRef AudioOutput;
        FFloatReadRef GrainSize;   // Grain size in milliseconds

        TArray<float> Envelope;    // Envelope to smooth grains
        int32 GrainSizeInFrames;  // Grain size in frames

        void InitializeEnvelope(int32 NumFrames);
    };

    FGranulator::FGranulator(const FBuildOperatorParams& InParams,
        const FAudioBufferReadRef& InAudioInput,
        const FFloatReadRef& InGrainSize)
        : AudioInput(InAudioInput),
        GrainSize(InGrainSize),
        AudioOutput(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
    {
        // Calculate grain size in frames
        const float SampleRate = InParams.OperatorSettings.GetSampleRate();
        GrainSizeInFrames = FMath::RoundToInt((*GrainSize / 1000.0f) * SampleRate);

        // Initialize the envelope
        InitializeEnvelope(GrainSizeInFrames);
    }

    void FGranulator::InitializeEnvelope(int32 NumFrames)
    {
        Envelope.SetNum(NumFrames);
        for (int32 i = 0; i < NumFrames; ++i)
        {
            // Hanning window envelope
            Envelope[i] = 0.5f * (1.0f - FMath::Cos(2.0f * PI * i / (NumFrames - 1)));
        }
    }

    void FGranulator::BindInputs(FInputVertexInterfaceData& InOutVertexData)
    {
        using namespace Granulator;
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAudioInput), AudioInput);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamGrainSize), GrainSize);
    }

    void FGranulator::BindOutputs(FOutputVertexInterfaceData& InOutVertexData)
    {
        using namespace Granulator;
        InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutParamAudio), AudioOutput);
    }

    void FGranulator::Execute()
    {
        const float* InputAudio = AudioInput->GetData();
    float* OutputAudio = AudioOutput->GetData();
    int32 NumFrames = AudioInput->Num();

    // Clear the output buffer
    FMemory::Memset(OutputAudio, 0, NumFrames * sizeof(float));

    const int32 HalfGrainSizeInFrames = GrainSizeInFrames / 2; // For 50% overlap
    const int32 RandomOffsetRange = FMath::RoundToInt(HalfGrainSizeInFrames * 0.5f); // ±25% of the grain size
    const float RandomPitchRange = 0.5f; // Up to ±50% pitch variation

    int32 GrainOffset = 0;

    while (GrainOffset < NumFrames)
    {
        // Calculate random offset within a limited range
        int32 RandomOffset = FMath::RandRange(-RandomOffsetRange, RandomOffsetRange);
        int32 AdjustedGrainStart = FMath::Clamp(GrainOffset + RandomOffset, 0, NumFrames - GrainSizeInFrames);

        // Apply random pitch shift factor for this grain
        float RandomPitchFactor = 1.0f + FMath::RandRange(-RandomPitchRange, RandomPitchRange);

        int32 GrainEnd = FMath::Min(AdjustedGrainStart + GrainSizeInFrames, NumFrames);
        int32 GrainLength = GrainEnd - AdjustedGrainStart;

        for (int32 i = 0; i < GrainLength; ++i)
        {
            int32 GlobalIndex = GrainOffset + i;
            float EnvelopeValue = (i < Envelope.Num()) ? Envelope[i] : 1.0f;

            // Calculate resampled input index for pitch shifting
            float ResampledIndex = AdjustedGrainStart + i / RandomPitchFactor;
            int32 InputIndex = FMath::Clamp(FMath::FloorToInt(ResampledIndex), 0, NumFrames - 2);
            float InterpolationFactor = ResampledIndex - InputIndex;

            // Interpolate between samples
            float Sample0 = InputAudio[InputIndex];
            float Sample1 = InputAudio[InputIndex + 1];
            float ResampledValue = FMath::Lerp(Sample0, Sample1, InterpolationFactor);

            // Apply envelope
            float GrainSample = ResampledValue * EnvelopeValue;

            // Add stereo panning (alternate between left and right)
            float Pan = (FMath::RandBool()) ? 1.0f : -1.0f; // Randomly pan left (-1) or right (+1)
            GrainSample *= (Pan > 0 ? 1.0f : 0.8f); // Slight bias to the louder channel

            // Add to output using overlap-add
            if (GlobalIndex < NumFrames)
            {
                OutputAudio[GlobalIndex] += GrainSample;
            }
        }

        // Move to the next grain
        GrainOffset += HalfGrainSizeInFrames;
    }
    }

    const FVertexInterface& FGranulator::GetVertexInterface()
    {
        using namespace Granulator;

        static const FVertexInterface Interface(
            FInputVertexInterface(
                TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamGrainSize), 50.0f) // Default grain size = 50ms
            ),
            FOutputVertexInterface(
                TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutParamAudio)) // Audio output
            )
        );

        return Interface;
    }

    const FNodeClassMetadata& FGranulator::GetNodeInfo()
    {
        auto InitNodeInfo = []() -> FNodeClassMetadata
            {
                FNodeClassMetadata Info;
                Info.ClassName = { StandardNodes::Namespace, "Granulator", StandardNodes::AudioVariant };
                Info.MajorVersion = 1;
                Info.MinorVersion = 0;
                Info.DisplayName = METASOUND_LOCTEXT("GranulatorNode_DisplayName", "Granulator");
                Info.Description = METASOUND_LOCTEXT("GranulatorNode_Description", "A simple granular processor with randomized timing.");
                Info.Author = PluginAuthor;
                Info.PromptIfMissing = PluginNodeMissingPrompt;
                Info.DefaultInterface = GetVertexInterface();
                Info.CategoryHierarchy.Emplace(NodeCategories::Delays);
                return Info;
            };

        static const FNodeClassMetadata Info = InitNodeInfo();
        return Info;
    }

    TUniquePtr<IOperator> FGranulator::CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
    {
        using namespace Granulator;

        const FInputVertexInterfaceData& InputData = InParams.InputData;

        FAudioBufferReadRef AudioIn = InputData.GetOrConstructDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InParamAudioInput), InParams.OperatorSettings);
        FFloatReadRef GrainSize = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamGrainSize), InParams.OperatorSettings);

        return MakeUnique<FGranulator>(InParams, AudioIn, GrainSize);
    }

    class FCustomGranulatorNode : public FNodeFacade
    {
    public:
        FCustomGranulatorNode(const FNodeInitData& InitData)
            : FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FGranulator>())
        {
        }
    };

    METASOUND_REGISTER_NODE(FCustomGranulatorNode)
}

#undef LOCTEXT_NAMESPACE
