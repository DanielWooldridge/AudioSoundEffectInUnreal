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

#define LOCTEXT_NAMESPACE "MetasoundStandardNodesTapDelay"

namespace Metasound
{
	namespace TapDelay
	{
		METASOUND_PARAM(InParamAudioInput, "In", "Audio input.")
			METASOUND_PARAM(InParamTapCount, "Tap Count", "Number of delay taps.")
			METASOUND_PARAM(InParamDryLevel, "Dry Level", "The dry level of the delay.")
			METASOUND_PARAM(InParamWetLevel, "Wet Level", "The wet level of the delay.")
			METASOUND_PARAM(InParamFeedbackAmount, "Feedback", "Feedback amount.")
			METASOUND_PARAM(InParamLFOFrequency, "LFO Frequency", "Oscillation frequency for all taps.")
			METASOUND_PARAM(InParamLFODepth, "LFO Depth", "Oscillation depth for all taps.")
			METASOUND_PARAM(InParamDifferentiator, "Differentiator", "Identifier to distinguish this node.")
			METASOUND_PARAM(OutParamAudio, "Out", "Audio output.")
	}

	class FMultiTapDelay : public TExecutableOperator<FMultiTapDelay>
	{
	public:
		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults);

		FMultiTapDelay(const FBuildOperatorParams& InParams,
			const FAudioBufferReadRef& InAudioInput,
			int32 InTapCount,
			const FFloatReadRef& InDryLevel,
			const FFloatReadRef& InWetLevel,
			const FFloatReadRef& InFeedback,
			const FFloatReadRef& InLFOFrequency,
			const FFloatReadRef& InLFODepth,
			const FString& InDifferentiator);

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;
		void Execute();

	private:
		FAudioBufferReadRef AudioInput;
		FAudioBufferWriteRef AudioOutput;

		int32 TapCount;
		FFloatReadRef DryLevel;
		FFloatReadRef WetLevel;
		FFloatReadRef Feedback;
		FFloatReadRef LFOFrequency;
		FFloatReadRef LFODepth;

		TArray<Audio::FDelay> DelayBuffers;
		TArray<float> BaseDelayTimes;
		TArray<float> LFOPhases;

		FString Differentiator;
	};

	FMultiTapDelay::FMultiTapDelay(const FBuildOperatorParams& InParams,
		const FAudioBufferReadRef& InAudioInput,
		int32 InTapCount,
		const FFloatReadRef& InDryLevel,
		const FFloatReadRef& InWetLevel,
		const FFloatReadRef& InFeedback,
		const FFloatReadRef& InLFOFrequency,
		const FFloatReadRef& InLFODepth,
		const FString& InDifferentiator)
		: AudioInput(InAudioInput),
		TapCount(InTapCount),
		DryLevel(InDryLevel),
		WetLevel(InWetLevel),
		Feedback(InFeedback),
		LFOFrequency(InLFOFrequency),
		LFODepth(InLFODepth),
		AudioOutput(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings)),
		Differentiator(InDifferentiator)
	{
		const float SampleRate = InParams.OperatorSettings.GetSampleRate();
		for (int32 TapIndex = 0; TapIndex < TapCount; ++TapIndex)
		{
			float TapDelayTime = 5.0f * ((TapIndex + 1) / static_cast<float>(TapCount));
			Audio::FDelay NewDelay;
			NewDelay.Init(SampleRate, 5.0f);
			NewDelay.SetDelayMsec(TapDelayTime * 1000.0f);
			DelayBuffers.Add(NewDelay);

			BaseDelayTimes.Add(TapDelayTime * 1000.0f);
			LFOPhases.Add(0.0f);
		}
	}

	void FMultiTapDelay::BindInputs(FInputVertexInterfaceData& InOutVertexData)
	{
		using namespace TapDelay;

		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAudioInput), AudioInput);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamDryLevel), DryLevel);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamWetLevel), WetLevel);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamFeedbackAmount), Feedback);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamLFOFrequency), LFOFrequency);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamLFODepth), LFODepth);
	}

	void FMultiTapDelay::BindOutputs(FOutputVertexInterfaceData& InOutVertexData)
	{
		using namespace TapDelay;
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutParamAudio), AudioOutput);
	}

	void FMultiTapDelay::Execute()
	{
		const float* InputAudio = AudioInput->GetData();
		float* OutputAudio = AudioOutput->GetData();
		int32 NumFrames = AudioInput->Num();

		const float DryLevelValue = FMath::Clamp(*DryLevel, 0.0f, 1.0f);
		const float WetLevelValue = FMath::Clamp(*WetLevel, 0.0f, 1.0f);
		const float FeedbackValue = FMath::Clamp(*Feedback, 0.0f, 1.0f);
		const float LFOFrequencyValue = FMath::Clamp(*LFOFrequency, 0.0f, 20.0f);
		const float LFODepthValue = FMath::Clamp(*LFODepth, 0.0f, 100.0f);

		const float SampleRate = 48000.0f;

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			float DrySignal = DryLevelValue * InputAudio[FrameIndex];
			float WetSignal = 0.0f;

			for (int32 TapIndex = 0; TapIndex < DelayBuffers.Num(); ++TapIndex)
			{
				Audio::FDelay& DelayBuffer = DelayBuffers[TapIndex];

				float LFOValue = LFODepthValue * FMath::Sin(2.0f * PI * LFOPhases[TapIndex]);

				LFOPhases[TapIndex] += LFOFrequencyValue / SampleRate;
				if (LFOPhases[TapIndex] >= 1.0f)
				{
					LFOPhases[TapIndex] -= 1.0f;
				}

				float ModulatedDelayTime = BaseDelayTimes[TapIndex] + LFOValue;
				DelayBuffer.SetDelayMsec(FMath::Clamp(ModulatedDelayTime, 0.0f, 5000.0f));

				float DelayedSample = DelayBuffer.ProcessAudioSample(InputAudio[FrameIndex] + FeedbackValue * WetSignal);
				WetSignal += DelayedSample;

				//UE_LOG(LogTemp, Warning, TEXT("TapIndex: %d, LFOValue: %f, ModulatedDelayTime: %f"), TapIndex, LFOValue, ModulatedDelayTime);

			}

			OutputAudio[FrameIndex] = DrySignal + WetLevelValue * WetSignal;
		}
	}

	const FVertexInterface& FMultiTapDelay::GetVertexInterface()
	{
		using namespace TapDelay;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
				TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamTapCount), 4),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamDryLevel)),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamWetLevel)),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamFeedbackAmount)),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamLFOFrequency)),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamLFODepth)),
				TInputDataVertex<FString>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamDifferentiator))
			),
			FOutputVertexInterface(
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutParamAudio))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FMultiTapDelay::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { StandardNodes::Namespace, "TapDelay", StandardNodes::AudioVariant };
				Info.MajorVersion = 1;
				Info.MinorVersion = 2;
				Info.DisplayName = METASOUND_LOCTEXT("DelayNode_DisplayName", "TapDelay");
				Info.Description = METASOUND_LOCTEXT("DelayNode_Description", "Delays an audio buffer with global LFO modulation.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy.Emplace(NodeCategories::Delays);
				return Info;
			};

		static const FNodeClassMetadata Info = InitNodeInfo();
		return Info;
	}

	TUniquePtr<IOperator> FMultiTapDelay::CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
	{
		using namespace TapDelay;

		const FInputVertexInterfaceData& InputData = InParams.InputData;

		FAudioBufferReadRef AudioIn = InputData.GetOrConstructDataReadReference<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InParamAudioInput), InParams.OperatorSettings);
		int32 TapCount = *InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(InParamTapCount), InParams.OperatorSettings);
		FFloatReadRef DryLevel = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamDryLevel), InParams.OperatorSettings);
		FFloatReadRef WetLevel = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamWetLevel), InParams.OperatorSettings);
		FFloatReadRef Feedback = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamFeedbackAmount), InParams.OperatorSettings);
		FFloatReadRef LFOFrequency = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamLFOFrequency), InParams.OperatorSettings);
		FFloatReadRef LFODepth = InputData.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InParamLFODepth), InParams.OperatorSettings);
		FString Differentiator = *InputData.GetOrCreateDefaultDataReadReference<FString>(METASOUND_GET_PARAM_NAME(InParamDifferentiator), InParams.OperatorSettings);

		return MakeUnique<FMultiTapDelay>(InParams, AudioIn, TapCount, DryLevel, WetLevel, Feedback, LFOFrequency, LFODepth, Differentiator);
	}

	class FMultiTapDelayNode : public FNodeFacade
	{
	public:
		FMultiTapDelayNode(const FNodeInitData& InitData)
			: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FMultiTapDelay>())
		{
		}
	};

	METASOUND_REGISTER_NODE(FMultiTapDelayNode)
}

#undef LOCTEXT_NAMESPACE
