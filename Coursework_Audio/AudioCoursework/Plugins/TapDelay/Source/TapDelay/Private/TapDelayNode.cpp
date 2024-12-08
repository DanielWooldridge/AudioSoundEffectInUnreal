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

#define LOCTEXT_NAMESPACE "MetasoundStandardNodesMultiTapDelay"

namespace Metasound
{
	namespace TapDelay
	{
		METASOUND_PARAM(InParamAudioInput, "In", "Audio input.")
			METASOUND_PARAM(InParamTapCount, "Tap Count", "Number of delay taps.")
			METASOUND_PARAM(InParamDryLevel, "Dry Level", "The dry level of the delay.")
			METASOUND_PARAM(InParamWetLevel, "Wet Level", "The wet level of the delay.")
			METASOUND_PARAM(InParamFeedbackAmount, "Feedback", "Feedback amount.")
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
			const FString& InDifferentiator);

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override;
		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override;
		void Execute();

	private:
		FAudioBufferReadRef AudioInput;
		FAudioBufferWriteRef AudioOutput;

		int32 TapCount;                 // Number of taps
	
		FFloatReadRef DryLevel;
		FFloatReadRef WetLevel;
		FFloatReadRef Feedback;

		TArray<Audio::FDelay> DelayBuffers; // A delay buffer for each tap
		FString Differentiator; // Visual differentiator, not used in processing
	};

	FMultiTapDelay::FMultiTapDelay(const FBuildOperatorParams& InParams,
		const FAudioBufferReadRef& InAudioInput,
		int32 InTapCount,
		const FFloatReadRef& InDryLevel,
		const FFloatReadRef& InWetLevel,
		const FFloatReadRef& InFeedback,
		const FString& InDifferentiator)
		: AudioInput(InAudioInput),
		TapCount(InTapCount),
		DryLevel(InDryLevel),
		WetLevel(InWetLevel),
		Feedback(InFeedback),
		AudioOutput(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings)),
		Differentiator(InDifferentiator)
	{
		// Initialize delay buffers
		const float SampleRate = InParams.OperatorSettings.GetSampleRate();
		for (int32 TapIndex = 0; TapIndex < TapCount; ++TapIndex)
		{
			float TapDelayTime = 5.0f * ((TapIndex + 1) / static_cast<float>(TapCount)); // Evenly spaced taps
			Audio::FDelay NewDelay;
			NewDelay.Init(SampleRate, 5.0f); // Max delay time of 5 seconds
			NewDelay.SetDelayMsec(TapDelayTime * 1000.0f); // Convert seconds to milliseconds
			DelayBuffers.Add(NewDelay);
		}
	}

	void FMultiTapDelay::BindInputs(FInputVertexInterfaceData& InOutVertexData)
	{
		using namespace TapDelay;

		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamAudioInput), AudioInput);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamDryLevel), DryLevel);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamWetLevel), WetLevel);
		InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InParamFeedbackAmount), Feedback);
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

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			float DrySignal = DryLevelValue * InputAudio[FrameIndex];
			float WetSignal = 0.0f;

			// Process each tap
			for (int32 TapIndex = 0; TapIndex < DelayBuffers.Num(); ++TapIndex)
			{
				Audio::FDelay& DelayBuffer = DelayBuffers[TapIndex];

				// Process delay for this tap
				float DelayedSample = DelayBuffer.ProcessAudioSample(InputAudio[FrameIndex] + FeedbackValue * WetSignal);

				// Accumulate the wet signal
				WetSignal += DelayedSample;
			}

			// Output the mix of dry and wet signals
			OutputAudio[FrameIndex] = DrySignal + WetLevelValue * WetSignal;
		}
	}

	const FVertexInterface& FMultiTapDelay::GetVertexInterface()
	{
		using namespace TapDelay;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
				TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamTapCount), 4), // Default to 4 taps
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamDryLevel)),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamWetLevel)),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamFeedbackAmount)),
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
				Info.MinorVersion = 1;
				Info.DisplayName = METASOUND_LOCTEXT("DelayNode_DisplayName", "TapDelay");
				Info.Description = METASOUND_LOCTEXT("DelayNode_Description", "Delays an audio buffer by the specified amount.");
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
		FString Differentiator = *InputData.GetOrCreateDefaultDataReadReference<FString>(METASOUND_GET_PARAM_NAME(InParamDifferentiator), InParams.OperatorSettings);

		return MakeUnique<FMultiTapDelay>(InParams, AudioIn, TapCount, DryLevel, WetLevel, Feedback, Differentiator);
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
