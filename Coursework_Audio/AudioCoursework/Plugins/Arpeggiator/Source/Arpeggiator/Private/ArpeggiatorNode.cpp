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

#define LOCTEXT_NAMESPACE "MetasoundStandardNodesArpeggiator"

namespace Metasound
{
    namespace Arpeggiator
    {
        METASOUND_PARAM(InParamAudioInput, "In", "Audio input.");
        METASOUND_PARAM(InParamArpRate, "Rate", "Rate of the arpeggiator in beats per second.");
        METASOUND_PARAM(InParamScaleType, "Scale Type", "Choose between Major and Minor scale.");
        METASOUND_PARAM(InParamRootNotesSequence, "Root Notes", "Sequence of root notes for the chord progression.");
        METASOUND_PARAM(InParamChordRepeatCount, "Chord Repeat Count", "Number of times to repeat each chord before switching.");
        METASOUND_PARAM(InParamArpeggioStyle, "Arpeggio Style", "Select the arpeggio style.");
        METASOUND_PARAM(InParamGlideTime, "Glide Time", "Time for glide/portamento in milliseconds.");
        METASOUND_PARAM(OutParamAudio, "Out", "Audio output.");
    }

    // enum for the different types of scale
    enum class EScaleType : uint8
    {
        Major,
        Minor
    };

    // enum for the diffferent types of style
    enum class EArpeggioStyle : uint8
    {
        Up,
        Down,
        UpDown,
        Style_201310,
        Style_203130,
        Style_210301,
        Style_213031
    };

    class FArpeggiator : public TExecutableOperator<FArpeggiator>
    {
    public:
        static const FNodeClassMetadata& GetNodeInfo();
        static const FVertexInterface& GetVertexInterface();
        static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults);

        FArpeggiator(const FBuildOperatorParams& InParams,
            const FAudioBufferReadRef& InAudioInput,
            const FFloatReadRef& InArpRate,
            const TDataReadReference<int32>& InScaleType,
            const TDataReadReference<TArray<FString>>& InRootNotesSequence,
            const TDataReadReference<int32>& InChordRepeatCount,
            const FFloatReadRef& InGlideTime,
            const TDataReadReference<int32>& InArpeggioStyle);

        virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
        virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;
        void Execute();

    private:
        FAudioBufferReadRef AudioInput;
        FAudioBufferWriteRef AudioOutput;
        FFloatReadRef ArpRate;
        TDataReadReference<int32> ScaleType;
        TDataReadReference<TArray<FString>> RootNotesSequence;
        TDataReadReference<int32> ChordRepeatCount;
        FFloatReadRef GlideTime;
        TDataReadReference<int32> ArpeggioStyle;

        TArray<int32> CurrentArpeggio;
        TArray<int32> FullArpeggio;
        float BaseFrequency;

        int32 CurrentChordIndex;
        int32 CurrentRepeatCount;
        int32 CurrentIndex;
        float Timer;
        float Interval;
        float SampleRate;
        float Phase;
        float CurrentFrequency;

        void InitializeArpeggiator();
        void BuildFullArpeggio();
        void UpdateScale(const FString& RootNote);
        void ApplyStyle(EArpeggioStyle Style);
    };

    // map of all the frequencies of notes
    TMap<FString, float> RootFrequencies = {
        {"A", 220.0f},
        {"B", 246.94f},
        {"C", 261.63f},
        {"D", 293.66f},
        {"E", 329.63f},
        {"F", 349.23f},
        {"G", 392.00f}
    };

    FArpeggiator::FArpeggiator(const FBuildOperatorParams& InParams,
        const FAudioBufferReadRef& InAudioInput,
        const FFloatReadRef& InArpRate,
        const TDataReadReference<int32>& InScaleType,
        const TDataReadReference<TArray<FString>>& InRootNotesSequence,
        const TDataReadReference<int32>& InChordRepeatCount,
        const FFloatReadRef& InGlideTime,
        const TDataReadReference<int32>& InArpeggioStyle)
        : AudioInput(InAudioInput),
        ArpRate(InArpRate),
        ScaleType(InScaleType),
        RootNotesSequence(InRootNotesSequence),
        ChordRepeatCount(InChordRepeatCount),
        GlideTime(InGlideTime),
        ArpeggioStyle(InArpeggioStyle),
        AudioOutput(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings)),
        CurrentChordIndex(0),
        CurrentRepeatCount(0),
        CurrentIndex(0),
        Timer(0.0f),
        Phase(0.0f),
        CurrentFrequency(0.0f),
        SampleRate(InParams.OperatorSettings.GetSampleRate())
    {
        if (!RootNotesSequence->IsEmpty())
        {
            UpdateScale((*RootNotesSequence)[0]);
        }
        BuildFullArpeggio();
        InitializeArpeggiator();
    }

    // Creates scales for minor and major, and defaults the scale to the A scale
    void FArpeggiator::UpdateScale(const FString& RootNote)
    {
        if (*ScaleType == static_cast<int32>(EScaleType::Major))
        {
            CurrentArpeggio = { 0, 4, 7, 12 }; // Major intervals
        }
        else
        {
            CurrentArpeggio = { 0, 3, 7, 12 }; // Minor intervals
        }

        if (RootFrequencies.Contains(RootNote))
        {
            BaseFrequency = RootFrequencies[RootNote];
        }
        else
        {
            BaseFrequency = 220.0f; // Default to A
        }
    }

    void FArpeggiator::BuildFullArpeggio()
    {
        FullArpeggio.Empty();
        ApplyStyle(static_cast<EArpeggioStyle>(*ArpeggioStyle));
    }

    // Predefined values that the user can choose on how the notes are played in the chord
    void FArpeggiator::ApplyStyle(EArpeggioStyle Style)
    {
        TArray<int32> StyledArpeggio;

        switch (Style)
        {
        case EArpeggioStyle::Up:
            StyledArpeggio = { 0, 1, 2, 3 };
            break;
        case EArpeggioStyle::Down:
            StyledArpeggio = { 3, 2, 1, 0 };
            break;
        case EArpeggioStyle::UpDown:
            StyledArpeggio = { 0, 1, 2, 3, 2, 1 };
            break;
        case EArpeggioStyle::Style_201310:
            StyledArpeggio = { 2, 0, 1, 3, 1, 0 };
            break;
        case EArpeggioStyle::Style_203130:
            StyledArpeggio = { 2, 0, 3, 1, 3, 0 };
            break;
        case EArpeggioStyle::Style_210301:
            StyledArpeggio = { 2, 1, 0, 3, 0, 1 };
            break;
        case EArpeggioStyle::Style_213031:
            StyledArpeggio = { 2, 1, 3, 0, 3, 1 };
            break;
        default:
            StyledArpeggio = { 0, 1, 2, 3 }; // default to the up style
            break;
        }

        for (int32 Note : StyledArpeggio)
        {
            FullArpeggio.Add(CurrentArpeggio[Note % CurrentArpeggio.Num()]);
        }
    }

    // Initalises how fast the intervals are between the notes
    void FArpeggiator::InitializeArpeggiator()
    {
        Interval = 1.0f / *ArpRate; 
    }

    void FArpeggiator::BindInputs(FInputVertexInterfaceData& InOutVertexData)
    {
        using namespace Arpeggiator;
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAudioInput), AudioInput);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamArpRate), ArpRate);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamScaleType), ScaleType);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamRootNotesSequence), RootNotesSequence);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamChordRepeatCount), ChordRepeatCount);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamGlideTime), GlideTime);
        InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamArpeggioStyle), ArpeggioStyle);
    }

    void FArpeggiator::BindOutputs(FOutputVertexInterfaceData& InOutVertexData)
    {
        using namespace Arpeggiator;
        InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutParamAudio), AudioOutput);
    }

    void FArpeggiator::Execute()
    {

        // pitch calculation formula
        const float SemitoneRatio = FMath::Pow(2.0f, 1.0f / 12.0f);

        float* OutputAudio = AudioOutput->GetData();
        int32 NumFrames = AudioOutput->Num();

        FMemory::Memset(OutputAudio, 0, NumFrames * sizeof(float));

        // set up deltatime to be per audio frame and the glide increment variable
        float DeltaTime = 1.0f / SampleRate;
        float GlideIncrement = 0.0f;

        // set interval here as well so it can be changed in realtime through node
        Interval = 1.0f / *ArpRate;

        // for loop for each audio frame
        for (int32 Frame = 0; Frame < NumFrames; ++Frame)
        {

            Timer += DeltaTime;

            // target for the next frequency calculation, e.g root note to the 3rd
            float TargetFrequency = BaseFrequency * FMath::Pow(SemitoneRatio, FullArpeggio[CurrentIndex]);

            // portamento implementation, if it is not the target freq
            if (CurrentFrequency != TargetFrequency)
            {
                // increase the current freq until it hits the target freq
                GlideIncrement = (TargetFrequency - CurrentFrequency) / ((*GlideTime / 1000.0f) * SampleRate);
                CurrentFrequency += GlideIncrement;
                if (FMath::Abs(CurrentFrequency - TargetFrequency) < FMath::Abs(GlideIncrement))
                {
                    CurrentFrequency = TargetFrequency;
                }
            }

            // new sine wave at the target freq (the current freq now)
            float SineWave = FMath::Sin(2.0f * PI * Phase);
            Phase += CurrentFrequency * DeltaTime;

            // reset the phasse if its out of bounds
            if (Phase > 1.0f)
            {
                Phase -= 1.0f;
            }

            // output new sine wave
            OutputAudio[Frame] += SineWave;

            // arpeggio note switching and each time on the note
            if (Timer >= Interval)
            {
                // set timer and move note
                Timer -= Interval;
                CurrentIndex = (CurrentIndex + 1) % FullArpeggio.Num();

                // check if chord in sequence is complete
                if (CurrentIndex == 0)
                {
                    CurrentRepeatCount++;
                    if (CurrentRepeatCount >= *ChordRepeatCount)
                    {
                        // switch to the next root note if needed, including updating scale and rebuilding the arpeggio pattern
                        CurrentRepeatCount = 0;
                        CurrentChordIndex = (CurrentChordIndex + 1) % RootNotesSequence->Num();
                        UpdateScale((*RootNotesSequence)[CurrentChordIndex]); 
                        BuildFullArpeggio();
                    }
                }
            }
        }
    }

    const FVertexInterface& FArpeggiator::GetVertexInterface()
    {
        using namespace Arpeggiator;

        static const FVertexInterface Interface(
            FInputVertexInterface(
                TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamArpRate), 2.0f),
                TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamScaleType), static_cast<int32>(EScaleType::Major)),
                TInputDataVertex<TArray<FString>>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamRootNotesSequence)),
                TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamChordRepeatCount), 1),
                TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamGlideTime), 100.0f),
                TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamArpeggioStyle), static_cast<int32>(EArpeggioStyle::Up))
            ),
            FOutputVertexInterface(
                TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutParamAudio))
            )
        );

        return Interface;
    }

    const FNodeClassMetadata& FArpeggiator::GetNodeInfo()
    {
        auto InitNodeInfo = []() -> FNodeClassMetadata
            {
                FNodeClassMetadata Info;
                Info.ClassName = { StandardNodes::Namespace, "Arpeggiator", StandardNodes::AudioVariant };
                Info.MajorVersion = 1;
                Info.MinorVersion = 0;
                Info.DisplayName = METASOUND_LOCTEXT("ArpeggiatorNode_DisplayName", "Arpeggiator");
                Info.Description = METASOUND_LOCTEXT("ArpeggiatorNode_Description", "Plays input audio in an arpeggiated sequence with customizable scale, root note, glide, and direction.");
                Info.Author = PluginAuthor;
                Info.PromptIfMissing = PluginNodeMissingPrompt;
                Info.DefaultInterface = GetVertexInterface();
                Info.CategoryHierarchy.Emplace(NodeCategories::Delays);
                return Info;
            };

        static const FNodeClassMetadata Info = InitNodeInfo();
        return Info;
    }

    TUniquePtr<IOperator> FArpeggiator::CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
    {
        using namespace Arpeggiator;

        const FInputVertexInterfaceData& InputData = InParams.InputData;

        FAudioBufferReadRef AudioIn = InputData.GetOrConstructDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InParamAudioInput), InParams.OperatorSettings);
        FFloatReadRef ArpRate = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamArpRate), InParams.OperatorSettings);
        TDataReadReference<int32> ScaleTypeRef = InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InParamScaleType), InParams.OperatorSettings);
        TDataReadReference<TArray<FString>> RootNotesSequenceRef = InputData.GetOrCreateDefaultDataReadReference<TArray<FString>>(METASOUND_GET_PARAM_NAME(InParamRootNotesSequence), InParams.OperatorSettings);
        TDataReadReference<int32> ChordRepeatCountRef = InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InParamChordRepeatCount), InParams.OperatorSettings);
        FFloatReadRef GlideTime = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamGlideTime), InParams.OperatorSettings);
        TDataReadReference<int32> ArpeggioStyle = InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InParamArpeggioStyle), InParams.OperatorSettings);

        return MakeUnique<FArpeggiator>(InParams, AudioIn, ArpRate, ScaleTypeRef, RootNotesSequenceRef, ChordRepeatCountRef, GlideTime, ArpeggioStyle);
    }

    class FCustomArpeggiatorNode : public FNodeFacade
    {
    public:
        FCustomArpeggiatorNode(const FNodeInitData& InitData)
            : FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FArpeggiator>())
        {
        }
    };

    METASOUND_REGISTER_NODE(FCustomArpeggiatorNode)
}

#undef LOCTEXT_NAMESPACE
