// COPYRIGHT(C)ACTION SQUARE CO., LTD. ALL RIGHTS RESERVED.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "AreaComponent.generated.h"

USTRUCT()
struct FAreaOverlapInfo
{
	GENERATED_BODY()

public:
	ECollisionSweepShapeType ShapeType;

	FTransform OverlapCollisionTM;

	FCollisionQueryParams Params;

	FVector Extent = FVector::OneVector;
	FVector Dir = FVector::ForwardVector;

	float RingWidth = 0.f;
	float SectorAngle = 0.f;

	float PatternDelay = 0.f;

	TSet<TWeakObjectPtr<AActor>> OverlappedActorList;

#if !UE_BUILD_SHIPPING
	bool bDebugOnceFlag = true;
#endif
};

USTRUCT()
struct FAreaSoundInfo
{
	GENERATED_BODY()

public:
	float RemainDelayTime = 0.f;

	UPROPERTY()
	TWeakObjectPtr<class UAudioComponent> AudioComponent;

	bool bWasPlayed = false;

	FSoundInfo SoundInfo;
};

UCLASS(Abstract)
class UAreaComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	UAreaComponent();
	virtual ~UAreaComponent() {};

	virtual void Init(const FSkillAreaInfo& InAreaInfo);

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;


public:
	// 자식에서 상속해서 사용
	virtual void OnAreaIn(const float InDeltaTime, AActor* OtherActor, UPrimitiveComponent* OtherComp) {};
	virtual void OnAreaOut(const float InDeltaTime, AActor* OtherActor, UPrimitiveComponent* OtherComp) {};


public:
	inline const FSkillAreaInfo& GetAreaInfo() const { return CalculatedAreaInfo; }
	inline const TWeakObjectPtr<ACustomPlayerState> GetCasterState() const { return CasterState; }

	inline void SetActiveArea(const bool InValue) { bActiveArea = InValue; }
	inline const bool IsActiveArea() const { return bActiveArea; }
	const bool IsEnd() const;
	void OnEnd();

private:
	void CreateOverlapInfo(const FSkillAreaInfo& InAreaInfo);
	void CreateDecal(const FSkillAreaInfo& InAreaInfo);
	void CreateParticle(const FSkillAreaInfo& InAreaInfo);
	void CreateSound(const FSkillAreaInfo& InAreaInfo);

	void CheckOverlap(const float InDeltaTime);

#if WITH_EDITOR
	void DrawOverlapDebug(const FAreaOverlapInfo& InOverlapInfo);
#endif

private:
	UPROPERTY()
	UDecalComponent* DecalComponent;

	UPROPERTY()
	TArray<UParticleSystemComponent*> ParticleComponents;

	UPROPERTY()
	TArray<FAreaSoundInfo> AreaSoundInfos;

	UPROPERTY()
	TArray<FAreaOverlapInfo> OverlapInfoList;

	TMap<TWeakObjectPtr<AActor>, float> OverlappedTimeList;

	TWeakObjectPtr<ACustomPlayerState> CasterState;

	UPROPERTY()
	FSkillAreaInfo CalculatedAreaInfo;

	float ElapsedTime = 0.f;

	bool bActiveArea = false;

};
