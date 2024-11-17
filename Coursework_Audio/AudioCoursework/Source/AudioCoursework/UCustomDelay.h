#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundSoundSource.h"
#include "MetasoundNodeFactory.h"
#include "Sound/CustomDelayNode.generated.h"

UCLASS(Blueprintable)
class YOURPROJECT_API UCustomDelayNode : public UMetasoundNode
{
    GENERATED_BODY()

public:
    // Constructor
    UCustomDelayNode();

    // Delay settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delay Settings")
    float DelayTime;  // Delay in seconds

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delay Settings")
    float FeedbackAmount;  // Feedback for the delay effect

    // Buffer for storing the audio samples
    TArray<float> DelayBuffer;

    // Sample rate (typically 44.1kHz or 48kHz)
    float SampleRate;

    // Current write position in the buffer
    int32 WriteIndex;

    // Process incoming audio
    void ProcessAudio(float* InOutAudioData, int32 NumSamples);
};
