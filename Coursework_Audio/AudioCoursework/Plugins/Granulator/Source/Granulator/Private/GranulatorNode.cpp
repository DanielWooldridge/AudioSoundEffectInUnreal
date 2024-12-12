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
            METASOUND_PARAM(InParamArpRate, "Rate", "Rate of the arpeggiator in beats per second.")
            METASOUND_PARAM(InParamScaleType, "Scale Type", "Choose between Major and Minor scale.")
            METASOUND_PARAM(InParamRootNote, "Root Note", "Select the root note of the scale.")
            METASOUND_PARAM(InParamAddLowOctave, "Add Low Octave", "Include the low octave in the arpeggio.");
            METASOUND_PARAM(InParamRandomize, "Randomize", "Randomize the order of the notes in the arpeggio.")
            METASOUND_PARAM(InParamGlideTime, "Glide Time", "Time for glide/portamento in milliseconds.")
            METASOUND_PARAM(OutParamAudio, "Out", "Audio output.")
    }

    enum class EScaleType : uint8
    {
        Major,
        Minor
    };

    class FGranulator : public TExecutableOperator<FGranulator>
    {
    public:
        static const FNodeClassMetadata& GetNodeInfo();
        static const FVertexInterface& GetVertexInterface();
        static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults);

        FGranulator(const FBuildOperatorParams& InParams,
            const FAudioBufferReadRef& InAudioInput,
            const FFloatReadRef& InArpRate,
            const FBoolReadRef& InRandomize,
            const TDataReadReference<int32>& InScaleType,
            const TDataReadReference<FString>& InRootNote,
            const FFloatReadRef& InGlideTime,
            const FBoolReadRef& InLowOctave // Toggle for including the low octave
        );

        virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
        virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;
        void Execute();

    private:
        FAudioBufferReadRef AudioInput;
        FAudioBufferWriteRef AudioOutput;
        FFloatReadRef ArpRate;          // Rate of the arpeggiator in beats per second
        FBoolReadRef Randomize;         // Whether to randomize the arpeggio
        TDataReadReference<int32> ScaleType; // Major or Minor
        TDataReadReference<FString> RootNote; // Selected root note
        FFloatReadRef GlideTime;       // Time for glide between notes in milliseconds

        TArray<int32> CurrentArpeggio;  // Current interval pattern (Major or Minor)
        TArray<int32> FullArpeggio;     // Full arpeggio sequence including descending notes
        float BaseFrequency;            // Base frequency for the root note

        int32 CurrentIndex;             // Current index in the pitch sequence
        float Timer;                    // Timer to manage arpeggiator rate
        float Interval;                 // Time interval between triggers
        float SampleRate;               // Sample rate of the audio
        float Phase;                    // Phase of the sine wave
        float CurrentFrequency;         // Current frequency for smooth glide
        FBoolReadRef AddLowOctave; // Toggle for including the low octave


        void InitializeArpeggiator();
        void BuildFullArpeggio();
        void UpdateScale();
        void RandomizeArpeggio();
    };

    TMap<FString, float> RootFrequencies = {
        {"A", 220.0f},
        {"B", 246.94f},
        {"C", 261.63f},
        {"D", 293.66f},
        {"E", 329.63f},
        {"F", 349.23f},
        {"G", 392.00f}
    };

    FGranulator::FGranulator(const FBuildOperatorParams& InParams,
        const FAudioBufferReadRef& InAudioInput,
        const FFloatReadRef& InArpRate,
        const FBoolReadRef& InRandomize,
        const TDataReadReference<int32>& InScaleType,
        const TDataReadReference<FString>& InRootNote,
        const FFloatReadRef& InGlideTime,
        const FBoolReadRef& InAddLowOctave)
        : AudioInput(InAudioInput),
        ArpRate(InArpRate),
        Randomize(InRandomize),
        ScaleType(InScaleType),
        RootNote(InRootNote),
        GlideTime(InGlideTime),
        AddLowOctave(InAddLowOctave),
        AudioOutput(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings)),
        CurrentIndex(0),
        Timer(0.0f),
        Phase(0.0f),
        CurrentFrequency(0.0f),
        SampleRate(InParams.OperatorSettings.GetSampleRate())
    {
        UpdateScale();
        BuildFullArpeggio();
        if (*Randomize)
        {
            RandomizeArpeggio();
        }
        InitializeArpeggiator();
    }


    void FGranulator::UpdateScale()
    {
        if (*ScaleType == static_cast<int32>(EScaleType::Major))
        {
            CurrentArpeggio = { 0, 4, 7, 12 }; // Major intervals
        }
        else
        {
            CurrentArpeggio = { 0, 3, 7, 12 }; // Minor intervals
        }

        if (RootFrequencies.Contains(*RootNote))
        {
            BaseFrequency = RootFrequencies[*RootNote];
        }
        else
        {
            BaseFrequency = 220.0f; // Default to A
        }
    }

    void FGranulator::BuildFullArpeggio()
    {
        FullArpeggio = CurrentArpeggio;

        // Add the low octave if enabled
        if (*AddLowOctave)
        {
            FullArpeggio.Insert(-12, 0); // Insert -12 (one octave below root) at the start
        }

        // Add descending notes to the arpeggio
        for (int32 i = CurrentArpeggio.Num() - 2; i > 0; --i)
        {
            FullArpeggio.Add(CurrentArpeggio[i]);
        }
    }


    void FGranulator::RandomizeArpeggio()
    {
        FRandomStream RandomStream(FPlatformTime::Cycles());
        for (int32 i = FullArpeggio.Num() - 1; i > 0; --i)
        {
            int32 SwapIndex = RandomStream.RandRange(0, i);
            FullArpeggio.Swap(i, SwapIndex);
        }
    }

    void FGranulator::InitializeArpeggiator()
    {
        Interval = 1.0f / *ArpRate; // Calculate interval based on rate
    }

    void FGranulator::BindInputs(FInputVertexInterfaceData& InOutVertexData)
    {
        using namespace Granulator;
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAudioInput), AudioInput);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamArpRate), ArpRate);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamRandomize), Randomize);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamScaleType), ScaleType);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamRootNote), RootNote);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamGlideTime), GlideTime);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAddLowOctave), AddLowOctave);
    }


    void FGranulator::BindOutputs(FOutputVertexInterfaceData& InOutVertexData)
    {
        using namespace Granulator;
        InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutParamAudio), AudioOutput);
    }

    void FGranulator::Execute()
    {
        const float SemitoneRatio = FMath::Pow(2.0f, 1.0f / 12.0f);

        float* OutputAudio = AudioOutput->GetData();
        int32 NumFrames = AudioOutput->Num();

        FMemory::Memset(OutputAudio, 0, NumFrames * sizeof(float));

        float DeltaTime = 1.0f / SampleRate;
        float GlideIncrement = 0.0f;

        // Update the interval dynamically based on ArpRate
        Interval = 1.0f / *ArpRate;

        for (int32 Frame = 0; Frame < NumFrames; ++Frame)
        {
            Timer += DeltaTime;

            float TargetFrequency = BaseFrequency * FMath::Pow(SemitoneRatio, FullArpeggio[CurrentIndex]);
            if (CurrentFrequency != TargetFrequency)
            {
                GlideIncrement = (TargetFrequency - CurrentFrequency) / ((*GlideTime / 1000.0f) * SampleRate);
                CurrentFrequency += GlideIncrement;
                if (FMath::Abs(CurrentFrequency - TargetFrequency) < FMath::Abs(GlideIncrement))
                {
                    CurrentFrequency = TargetFrequency;
                }
            }

            float SineWave = FMath::Sin(2.0f * PI * Phase);
            Phase += CurrentFrequency * DeltaTime;

            if (Phase > 1.0f)
            {
                Phase -= 1.0f;
            }

            OutputAudio[Frame] = SineWave;

            if (Timer >= Interval)
            {
                Timer -= Interval;
                CurrentIndex = (CurrentIndex + 1) % FullArpeggio.Num();
            }
        }
    }

    const FVertexInterface& FGranulator::GetVertexInterface()
    {
        using namespace Granulator;

        static const FVertexInterface Interface(
            FInputVertexInterface(
                TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamArpRate), 2.0f), // Default rate = 2 beats per second
                TInputDataVertex<bool>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamRandomize), false), // Default randomize = false
                TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamScaleType), static_cast<int32>(EScaleType::Major)), // Default Major scale
                TInputDataVertex<FString>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamRootNote), FString("A")), // Default root note = A
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamGlideTime), 100.0f), // Default glide time = 100ms
                TInputDataVertex<bool>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAddLowOctave), false) // Default AddLowOctave = false
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
                Info.DisplayName = METASOUND_LOCTEXT("ArpeggiatorNode_DisplayName", "Arpeggiator");
                Info.Description = METASOUND_LOCTEXT("ArpeggiatorNode_Description", "Plays input audio in an arpeggiated sequence with customizable scale, root note, randomization, and glide.");
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
        FFloatReadRef ArpRate = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamArpRate), InParams.OperatorSettings);
        FBoolReadRef Randomize = InputData.GetOrCreateDefaultDataReadReference<bool>(METASOUND_GET_PARAM_NAME(InParamRandomize), InParams.OperatorSettings);
        TDataReadReference<int32> ScaleTypeRef = InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InParamScaleType), InParams.OperatorSettings);
        TDataReadReference<FString> RootNoteRef = InputData.GetOrCreateDefaultDataReadReference<FString>(METASOUND_GET_PARAM_NAME(InParamRootNote), InParams.OperatorSettings);
        FFloatReadRef GlideTime = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamGlideTime), InParams.OperatorSettings);
        FBoolReadRef AddLowOctave = InputData.GetOrCreateDefaultDataReadReference<bool>(METASOUND_GET_PARAM_NAME(InParamAddLowOctave), InParams.OperatorSettings);

        return MakeUnique<FGranulator>(InParams, AudioIn, ArpRate, Randomize, ScaleTypeRef, RootNoteRef, GlideTime, AddLowOctave);
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

