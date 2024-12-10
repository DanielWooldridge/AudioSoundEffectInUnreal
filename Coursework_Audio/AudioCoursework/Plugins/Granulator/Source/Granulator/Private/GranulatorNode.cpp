#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
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
            METASOUND_PARAM(InParamGrainOffsetPercent, "Grain Offset", "Offset between grains as a percentage of the grain size.")
            METASOUND_PARAM(InParamPitchShift, "Pitch Shift", "Amount of pitch shift in semitones.")
            METASOUND_PARAM(InParamRandomness, "Randomness", "Amount of randomness in grain selection (0-100%).")
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
            const FFloatReadRef& InGrainSize,
            const FFloatReadRef& InGrainOffsetPercent,
            const FFloatReadRef& InPitchShift,
            const FFloatReadRef& InRandomnessAmount);

        virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
        virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;
        void Execute();

    private:
        FAudioBufferReadRef AudioInput;
        FAudioBufferWriteRef AudioOutput;
        FFloatReadRef GrainSize;           // Grain size in milliseconds
        FFloatReadRef GrainOffsetPercent; // Grain offset as a percentage of grain size
        FFloatReadRef PitchShift;         // Pitch shift in semitones
        FFloatReadRef RandomnessAmount;   // Randomness control (0-100%)

        TArray<float> Envelope;            // Envelope to smooth grains
        int32 GrainSizeInFrames;           // Grain size in frames

        void InitializeEnvelope(int32 NumFrames);
    };

    FGranulator::FGranulator(const FBuildOperatorParams& InParams,
        const FAudioBufferReadRef& InAudioInput,
        const FFloatReadRef& InGrainSize,
        const FFloatReadRef& InGrainOffsetPercent,
        const FFloatReadRef& InPitchShift,
        const FFloatReadRef& InRandomnessAmount)
        : AudioInput(InAudioInput),
        GrainSize(InGrainSize),
        GrainOffsetPercent(InGrainOffsetPercent),
        PitchShift(InPitchShift),
        RandomnessAmount(InRandomnessAmount),
        AudioOutput(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
    {
        const float SampleRate = InParams.OperatorSettings.GetSampleRate();
        GrainSizeInFrames = FMath::RoundToInt((*GrainSize / 1000.0f) * SampleRate);

        // Initialize the envelope for smoothing
        InitializeEnvelope(GrainSizeInFrames);
    }

    void FGranulator::InitializeEnvelope(int32 NumFrames)
    {
        Envelope.SetNum(NumFrames);
        for (int32 i = 0; i < NumFrames; ++i)
        {
            // Hanning window for smoothing
            Envelope[i] = 0.5f * (1.0f - FMath::Cos(2.0f * PI * i / (NumFrames - 1)));
        }
    }

    void FGranulator::BindInputs(FInputVertexInterfaceData& InOutVertexData)
    {
        using namespace Granulator;
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAudioInput), AudioInput);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamGrainSize), GrainSize);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamGrainOffsetPercent), GrainOffsetPercent);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamPitchShift), PitchShift);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamRandomness), RandomnessAmount);
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

        // Calculate grain offset in frames
        int32 GrainOffsetFrames = FMath::RoundToInt((GrainSizeInFrames * (*GrainOffsetPercent)) / 1000.0f);

        // Determine the randomness range
        int32 MaxRandomOffset = FMath::RoundToInt((*RandomnessAmount / 1000.0f) * (NumFrames - GrainSizeInFrames));

        int32 CurrentFrame = 0;

        while (CurrentFrame < NumFrames)
        {
            // Add randomness to the grain start position
            int32 RandomOffset = FMath::RandRange(0, MaxRandomOffset);
            int32 GrainStart = FMath::Clamp(CurrentFrame + RandomOffset, 0, NumFrames - GrainSizeInFrames);

            int32 GrainEnd = FMath::Min(GrainStart + GrainSizeInFrames, NumFrames);
            int32 GrainLength = GrainEnd - GrainStart;

            for (int32 i = 0; i < GrainLength; ++i)
            {
                int32 GlobalIndex = CurrentFrame + i;

                // Ensure we don't exceed the input buffer
                if (GlobalIndex >= NumFrames) break;

                // Calculate resampled input index
                float ResampledIndex = GrainStart + i / FMath::Pow(2.0f, *PitchShift / 12.0f);
                int32 InputIndex = FMath::Clamp(FMath::FloorToInt(ResampledIndex), 0, NumFrames - 2);
                float InterpolationFactor = ResampledIndex - InputIndex;

                // Perform linear interpolation
                float Sample0 = InputAudio[InputIndex];
                float Sample1 = InputAudio[InputIndex + 1];
                float ResampledValue = FMath::Lerp(Sample0, Sample1, InterpolationFactor);

                // Apply envelope
                float EnvelopeValue = (i < Envelope.Num()) ? Envelope[i] : 1.0f;
                OutputAudio[GlobalIndex] += ResampledValue * EnvelopeValue;
            }

            // Advance to the next grain
            CurrentFrame += GrainOffsetFrames;
        }
    }

    const FVertexInterface& FGranulator::GetVertexInterface()
    {
        using namespace Granulator;

        static const FVertexInterface Interface(
            FInputVertexInterface(
                TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamGrainSize), 50.0f), // Default grain size = 50ms
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamGrainOffsetPercent), 50.0f), // Default offset = 50%
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamPitchShift), 0.0f), // Default pitch shift = 0 semitones
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamRandomness), 0.0f) // Default randomness = 0%
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
                Info.Description = METASOUND_LOCTEXT("GranulatorNode_Description", "Splits audio into overlapping grains with randomness.");
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
        FFloatReadRef GrainOffsetPercent = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamGrainOffsetPercent), InParams.OperatorSettings);
        FFloatReadRef PitchShift = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamPitchShift), InParams.OperatorSettings);
        FFloatReadRef RandomnessAmount = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamRandomness), InParams.OperatorSettings);

        return MakeUnique<FGranulator>(InParams, AudioIn, GrainSize, GrainOffsetPercent, PitchShift, RandomnessAmount);
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
