// COPYRIGHT(C)ACTION SQUARE CO., LTD. ALL RIGHTS RESERVED.


#include "Component/AreaComponent.h"
#include "CustomParticleSystemComponent.h"
#include "Components/AudioComponent.h"
#include "Math/Vector.h"

UAreaComponent::UAreaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UAreaComponent::Init(const FSkillAreaInfo& InAreaInfo)
{
	if (IsValid(InAreaInfo.Caster) == false || InAreaInfo.AreaClass == nullptr)
	{
		return;
	}

	CalculatedAreaInfo = InAreaInfo;

	CasterState = InAreaInfo.Caster->GetPlayerState<ACustomPlayerState>();

	FVector SpawnLocation = InAreaInfo.OriginSpawnTransform.GetLocation();

	if (InAreaInfo.AreaCount > 1 || InAreaInfo.bForceRandomArea)
	{
		// Rand SpawnLocation, DecalDelay
		FRandomStream InRandStream;
		InRandStream.Initialize(FMath::FloorToInt(InAreaInfo.Timestamp));

		FVector2D InFindRandPoint;
		{
			float L;

			do
			{
				// Check random vectors in the unit circle so result is statistically uniform.
				InFindRandPoint.X = InRandStream.FRand() * 2.f - 1.f;
				InFindRandPoint.Y = InRandStream.FRand() * 2.f - 1.f;
				L = InFindRandPoint.SizeSquared();
			} while (L > 1.0f);
		}
		
		const FVector2D InRandPointInRadius = InFindRandPoint * InAreaInfo.MaxSpawnRadius;

		SpawnLocation.X += InRandPointInRadius.X;
		SpawnLocation.Y += InRandPointInRadius.Y;

		CalculatedAreaInfo.DecalDelay = MyUtility::RandRange(0.f, InAreaInfo.DecalDelay);
	}

	const float HalfHeight = InAreaInfo.Caster->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SpawnLocation.Z = InAreaInfo.Caster->GetActorLocation().Z - HalfHeight;

	CalculatedAreaInfo.OriginSpawnTransform.SetLocation(SpawnLocation);
	CalculatedAreaInfo.OriginSpawnTransform.SetScale3D(FVector::OneVector);

	CalculatedAreaInfo.DecalDelay = FMath::Max(0.f, CalculatedAreaInfo.DecalDelay);
	CalculatedAreaInfo.DecalLifeTime += CalculatedAreaInfo.DecalLifeTime > 0.f ? CalculatedAreaInfo.DecalDelay : 0.f;
	CalculatedAreaInfo.CollisionCheckDelay += CalculatedAreaInfo.DecalLifeTime;
	CalculatedAreaInfo.AreaLifeTime += CalculatedAreaInfo.CollisionCheckDelay;

	// Create Decal
	CreateDecal(CalculatedAreaInfo);

	// Create Collision
	CreateOverlapInfo(CalculatedAreaInfo);

	// Create Particle
	CreateParticle(CalculatedAreaInfo);

	// Create Audio
	CreateSound(CalculatedAreaInfo);
}

void UAreaComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bActiveArea == false || IsValid(CalculatedAreaInfo.Caster) == false || CalculatedAreaInfo.Caster->IsDie())
	{
		return;
	}

	ElapsedTime += DeltaTime;

	if (CalculatedAreaInfo.DecalLifeTime <= ElapsedTime)
	{
		if (IsValid(DecalComponent) == true)
		{
			DecalComponent->UnregisterComponent();
			DecalComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			DecalComponent = nullptr;
		}
	}
	else
	{
		if (CalculatedAreaInfo.DecalDelay <= ElapsedTime)
		{
			if (IsValid(DecalComponent) == true && DecalComponent->IsVisible() == false)
			{
				DecalComponent->ToggleVisibility();
			}
		}

		return;
	}

	// Particle
	for (int ParticleIndex = 0; ParticleIndex < ParticleComponents.Num(); ParticleIndex++)
	{
		// 루프가 아닐경우, 파티클 이미터의 LifeTime이 완료된경우 불필요한 파티클을 제거
		if (IsValid(ParticleComponents[ParticleIndex]) && ParticleComponents[ParticleIndex]->bWasCompleted)
		{
			ParticleComponents[ParticleIndex]->UnregisterComponent();
			ParticleComponents[ParticleIndex]->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			ParticleComponents[ParticleIndex] = nullptr;
			ParticleComponents.RemoveAt(ParticleIndex--);
		}
	}

	// Sound
	for (int SoundIndex = 0; SoundIndex < AreaSoundInfos.Num(); SoundIndex++)
	{
		if (AreaSoundInfos[SoundIndex].AudioComponent.IsValid() == false ||
			(AreaSoundInfos[SoundIndex].bWasPlayed == true && AreaSoundInfos[SoundIndex].AudioComponent->IsPlaying() == false))
		{
			if (AreaSoundInfos[SoundIndex].AudioComponent.IsValid())
			{
				AreaSoundInfos[SoundIndex].AudioComponent->Deactivate();
				AreaSoundInfos[SoundIndex].AudioComponent->DestroyComponent();
			}
			AreaSoundInfos[SoundIndex].AudioComponent.Reset();
			AreaSoundInfos.RemoveAt(SoundIndex--);
			continue;
		}
	}

	// Collision
	if (CalculatedAreaInfo.Caster->HasAuthority() && CalculatedAreaInfo.CollisionCheckDelay <= ElapsedTime && ElapsedTime <= CalculatedAreaInfo.AreaLifeTime)
	{
		CheckOverlap(DeltaTime);
	}
}

void UAreaComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

	for (int SoundIndex = 0; SoundIndex < AreaSoundInfos.Num(); SoundIndex++)
	{
		if (AreaSoundInfos[SoundIndex].AudioComponent.IsValid())
		{
			AreaSoundInfos[SoundIndex].AudioComponent->Deactivate();
			AreaSoundInfos[SoundIndex].AudioComponent->DestroyComponent();
		}
	}
}

const bool UAreaComponent::IsEnd() const
{
	if (ParticleComponents.Num() > 0)
	{
		// 재생중인 파티클이 있을 경우
		return false;
	}

	if (IsValid(CalculatedAreaInfo.Caster) == false || CalculatedAreaInfo.Caster->IsDie() || CalculatedAreaInfo.AreaLifeTime < ElapsedTime)
	{
		// 캐스터가 죽었을 경우, 유지 시간이 끝났을 경우 종료
		return true;
	}

	return false;
}

void UAreaComponent::OnEnd()
{
	// Sound
	ACustomCharacter* Caster = CasterState.IsValid() ? Cast<ACustomCharacter>(CasterState->GetPawn()) : nullptr;

	if (IsValid(Caster) && Caster->IsPlayingAction(CalculatedAreaInfo.ActionName) == false)
	{
		for (int SoundIndex = 0; SoundIndex < AreaSoundInfos.Num(); SoundIndex++)
		{
			if (AreaSoundInfos[SoundIndex].AudioComponent.IsValid())
			{
				AreaSoundInfos[SoundIndex].AudioComponent->Deactivate();
				AreaSoundInfos[SoundIndex].AudioComponent->DestroyComponent();
				AreaSoundInfos[SoundIndex].AudioComponent.Reset();
				AreaSoundInfos.RemoveAt(SoundIndex--);
			}
		}
	}
}

void UAreaComponent::CreateOverlapInfo(const FSkillAreaInfo& InAreaInfo)
{
	if (IsValid(CalculatedAreaInfo.Caster) == false)
	{
		return;
	}

	const int OverlapAreaCount = FMath::Max(InAreaInfo.PatternCount, 1);
	for (int Index = 0; Index < OverlapAreaCount; Index++)
	{
		FAreaOverlapInfo NewOverlapInfo;

		NewOverlapInfo.ShapeType = InAreaInfo.CollisionShapeType;
		NewOverlapInfo.OverlapCollisionTM = CalculatedAreaInfo.CollisionRelativeTM * CalculatedAreaInfo.OriginSpawnTransform;
		NewOverlapInfo.Params.AddIgnoredActor(CalculatedAreaInfo.Caster);

		FVector ForwardVector = NewOverlapInfo.OverlapCollisionTM.GetRotation().GetForwardVector();
		FVector BaseExtent = FVector(CalculatedAreaInfo.BaseUnit, CalculatedAreaInfo.BaseUnit, CalculatedAreaInfo.BaseUnit) * NewOverlapInfo.OverlapCollisionTM.GetScale3D();

		int PatternOffset = InAreaInfo.PatternOffset;

		bool bValidOverlapExtent = true;

		switch (InAreaInfo.CollisionShapeType)
		{
		case ECollisionSweepShapeType::Shpere:
		case ECollisionSweepShapeType::Box:
		{
			PatternOffset *= InAreaInfo.bReversePattern == false ? Index : -Index;

			const FVector OverlapExtent = (BaseExtent + FVector(PatternOffset, PatternOffset, PatternOffset)).ComponentMax(FVector(0.f, 0.f, 0.f));

			NewOverlapInfo.Dir = ForwardVector;
			NewOverlapInfo.Extent = OverlapExtent;
			break;
		}
		case ECollisionSweepShapeType::Sector:
		{
			PatternOffset += InAreaInfo.SectorAngle;
			PatternOffset *= InAreaInfo.bReversePattern == false ? Index : -Index;

			NewOverlapInfo.Dir = ForwardVector.RotateAngleAxis(PatternOffset, FVector::ZAxisVector);
			NewOverlapInfo.Extent = BaseExtent;
			break;
		}
		case ECollisionSweepShapeType::Ring:
		{
			PatternOffset += InAreaInfo.RingWidth;
			PatternOffset *= InAreaInfo.bReversePattern == false ? Index : -Index;

			const FVector RingOverlapExtent = (BaseExtent + FVector(PatternOffset, PatternOffset, PatternOffset)).ComponentMax(FVector(0.f, 0.f, 0.f));
			if (RingOverlapExtent.Size2D() <= 0 || RingOverlapExtent.X - InAreaInfo.RingWidth <= 0)
			{
				// Reverse 패턴일 경우 체크할 범위가 0보다 작아지는 경우 생성하지 않는다.
				bValidOverlapExtent = false;
			}

			NewOverlapInfo.Dir = ForwardVector;
			NewOverlapInfo.Extent = RingOverlapExtent;
			break;
		}
		default:
		{
			NewOverlapInfo.Dir = ForwardVector;
			NewOverlapInfo.Extent = BaseExtent;
			break;
		}
		}

		if (bValidOverlapExtent == true)
		{
			NewOverlapInfo.RingWidth = InAreaInfo.RingWidth;
			NewOverlapInfo.SectorAngle = InAreaInfo.SectorAngle;

			NewOverlapInfo.PatternDelay = Index == 0 ? 0.f : InAreaInfo.PatternDelayOffset;

			OverlapInfoList.Emplace(NewOverlapInfo);
		}
		else
		{
			break;
		}
	}
}

void UAreaComponent::CreateDecal(const FSkillAreaInfo& InAreaInfo)
{
	if (MyUtility::IsInDedicatedServer(GetWorld()) == true || IsValid(InAreaInfo.Caster) == false || FMath::IsNearlyZero(InAreaInfo.DecalLifeTime))
	{
		return;
	}

	UMaterialInstance* MaterialInst = InAreaInfo.DecalMaterialInst.LoadSynchronous();
	if (IsValid(MaterialInst) == false)
	{
		return;
	}

	const FTransform FinalSpawnTM = InAreaInfo.DecalRelativeTM * InAreaInfo.OriginSpawnTransform;

	DecalComponent = NewObject<UDecalComponent>(this);
	if (IsValid(DecalComponent))
	{
		DecalComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		DecalComponent->ToggleVisibility();
		DecalComponent->RegisterComponent();

		const FTransform DecalTransform = FTransform(FRotator(90.f, 180.f - InAreaInfo.DecalAngle, 0.f), FinalSpawnTM.GetLocation(), FinalSpawnTM.GetScale3D());
		DecalComponent->SetWorldTransform(DecalTransform);

		UMaterialInstanceDynamic* MaterialInstance = DecalComponent->CreateDynamicMaterialInstance();
		if (!IsValid(MaterialInstance)) return;

		DecalComponent->SetDecalMaterial(MaterialInst);

		MaterialInstance->SetScalarParameterValue(TEXT("Angle"), InAreaInfo.DecalAngle * 2.f);
	}
}

void UAreaComponent::CreateParticle(const FSkillAreaInfo& InAreaInfo)
{
	if (MyUtility::IsInDedicatedServer(GetWorld()) == true || IsValid(CalculatedAreaInfo.Caster) == false)
	{
		return;
	}

	for (const FSkillAreaParticleInfo& ParticleInfo : InAreaInfo.ParticleData)
	{
		UParticleSystem* ParticleSystem = ParticleInfo.ParticleTemplate.LoadSynchronous();
		if (IsValid(ParticleSystem) == true)
		{
			UCustomParticleSystemComponent* ParticleComponent = NewObject<UCustomParticleSystemComponent>(this);
			if (IsValid(ParticleComponent) == true)
			{
				const FTransform FinalParticleSpawnTM = ParticleInfo.RelativeTransform * InAreaInfo.OriginSpawnTransform;

				ParticleComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				ParticleComponent->bAutoDestroy = false;

				ParticleComponent->SetCustomParticleDelay(ParticleInfo.ParticleDelay);
				ParticleComponent->SetTemplate(ParticleSystem);
				ParticleComponent->SetWorldTransform(FinalParticleSpawnTM);
				ParticleComponent->SetTranslucentSortPriority(ParticleInfo.TranslucencySortPriority);
				ParticleComponent->RegisterComponent();

				ParticleComponents.Emplace(ParticleComponent);
			}
		}

		// 컬링하지 않는 파티클
		UParticleSystem* NotCullParticleSystem = ParticleInfo.NotCullParticleTemplate.LoadSynchronous();
		if (IsValid(NotCullParticleSystem) == true)
		{
			UCustomParticleSystemComponent* ParticleComponent = NewObject<UCustomParticleSystemComponent>(this);
			if (IsValid(ParticleComponent) == true)
			{
				const FTransform FinalParticleSpawnTM = ParticleInfo.RelativeTransform * InAreaInfo.OriginSpawnTransform;

				ParticleComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				ParticleComponent->bAutoDestroy = false;
				ParticleComponent->bNeverDistanceCull = true;

				ParticleComponent->SetCustomParticleDelay(ParticleInfo.ParticleDelay);
				ParticleComponent->SetTemplate(NotCullParticleSystem);
				ParticleComponent->SetWorldTransform(FinalParticleSpawnTM);
				ParticleComponent->SetTranslucentSortPriority(ParticleInfo.NotCullTranslucencySortPriority);
				ParticleComponent->RegisterComponent();

				ParticleComponents.Emplace(ParticleComponent);
			}
		}
	}
}

void UAreaComponent::CreateSound(const FSkillAreaInfo& InAreaInfo)
{
	float OverlapTotalDelay = InAreaInfo.bIsSyncWithParticle ? 0.f : CalculatedAreaInfo.CollisionCheckDelay;

	FVector SpawnLocation;

	for (int Index = 0; Index < InAreaInfo.SoundData.Num(); Index++)
	{
		if (OverlapInfoList.IsValidIndex(Index))
		{
			SpawnLocation = OverlapInfoList[Index].OverlapCollisionTM.GetLocation();
		}

		if (InAreaInfo.bIsSyncWithParticle && InAreaInfo.ParticleData.IsValidIndex(Index))
		{
			OverlapTotalDelay = InAreaInfo.ParticleData[Index].ParticleDelay;
		}
		else if (Index != 0) 
		{
			OverlapTotalDelay += InAreaInfo.PatternDelayOffset;
		}

		UAudioComponent* InAudioComponent = UGameplayStatics::SpawnSoundAtLocation(GetWorld(),InAreaInfo.SoundData[Index].SoundBase, SpawnLocation, FRotator::ZeroRotator, 1.f, 1.f, 0.f, nullptr, nullptr, false);
		if (IsValid(InAudioComponent))
		{
			InAudioComponent->Stop();

			FAreaSoundInfo AreaSoundInfo;
			AreaSoundInfo.SoundInfo = InAreaInfo.SoundData[Index];
			AreaSoundInfo.AudioComponent = InAudioComponent;
			AreaSoundInfo.RemainDelayTime = OverlapTotalDelay;

			AreaSoundInfos.Emplace(AreaSoundInfo);
		}
	}
}

void UAreaComponent::CheckOverlap(const float InDeltaTime)
{
	for (int Index = 0; Index < OverlapInfoList.Num(); Index++)
	{
		// 패턴 오버랩 구간별 딜레이 체크
		if (OverlapInfoList[Index].PatternDelay > 0.f)
		{
			OverlapInfoList[Index].PatternDelay -= InDeltaTime;
			break;
		}

#if WITH_EDITOR
		if (ShowDebugCollisionFlag.GetValueOnAnyThread() == true && OverlapInfoList[Index].bDebugOnceFlag == true)
		{
			DrawOverlapDebug(OverlapInfoList[Index]);
			OverlapInfoList[Index].bDebugOnceFlag = false;
		}
#endif

		FCollisionShape Shape;
		switch (OverlapInfoList[Index].ShapeType)
		{
		case ECollisionSweepShapeType::Box:
		{
			Shape = FCollisionShape::MakeBox(OverlapInfoList[Index].Extent);
			break;
		}
		case ECollisionSweepShapeType::Capsule:
		{
			Shape = FCollisionShape::MakeCapsule(FMath::Max(OverlapInfoList[Index].Extent.X, OverlapInfoList[Index].Extent.Y), OverlapInfoList[Index].Extent.Z);
			break;
		}
		case ECollisionSweepShapeType::Shpere:
		case ECollisionSweepShapeType::Sector:
		case ECollisionSweepShapeType::Ring:
		{
			Shape = FCollisionShape::MakeSphere(OverlapInfoList[Index].Extent.X);
			break;
		}
		}

		// Overlap
		TArray<FOverlapResult> OutResult;

		GetWorld()->OverlapMultiByChannel(OutResult,
			OverlapInfoList[Index].OverlapCollisionTM.GetLocation(),
			OverlapInfoList[Index].Dir.ToOrientationQuat(),
			ECollisionChannel::ECC_GameTraceChannel1,
			Shape,
			OverlapInfoList[Index].Params
		);

		TSet<AActor*> TempOverlappedActorListForDot;
		const bool bIsDotEffect = GetAreaInfo().AreaSectionTime > 0.f;

		// 모양에 따른 처리
		for (FOverlapResult& Result : OutResult)
		{
			AActor* TargetActor = Result.GetActor();
			if (IsValid(TargetActor) == false)
			{
				continue;
			}

			bool bIsOverlap = true;

			ACustomCharacter* InTargetPawn = Cast<ACustomCharacter>(TargetActor);
			const FTransform& OverlapOriginTM = OverlapInfoList[Index].OverlapCollisionTM;
			const FVector AreaLookTargetVector = TargetActor->GetActorLocation() - OverlapOriginTM.GetLocation();
			const float TargetCapsuleRadius = IsValid(InTargetPawn) ? InTargetPawn->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f;

			switch (OverlapInfoList[Index].ShapeType)
			{
			case ECollisionSweepShapeType::Sector:
			{
				const FVector RightVector = AreaLookTargetVector.ToOrientationQuat().GetRightVector();
				const FVector CrossProduct = FVector::CrossProduct(OverlapInfoList[Index].Dir, AreaLookTargetVector);
				const FVector RadiusVector = CrossProduct.Z < 0.f ? RightVector * TargetCapsuleRadius : RightVector * TargetCapsuleRadius  * -1.f;
				const FVector FinalAreaLookTargetDir = AreaLookTargetVector + RadiusVector;

				const float InDiffAngleNoCapsule = MyUtility::GetTargetAngle(OverlapInfoList[Index].Dir, AreaLookTargetVector.GetSafeNormal2D());
				const float InDiffAngleWithCapsule = MyUtility::GetTargetAngle(OverlapInfoList[Index].Dir, FinalAreaLookTargetDir.GetSafeNormal2D());
				const float InCheckAngle = OverlapInfoList[Index].SectorAngle / 2;
				bIsOverlap = InDiffAngleNoCapsule <= InCheckAngle || InDiffAngleWithCapsule <= InCheckAngle;
				break;
			}
			case ECollisionSweepShapeType::Ring:
			{
				const float ExcludeRingRadius = OverlapInfoList[Index].Extent.X - OverlapInfoList[Index].RingWidth;
				const float AreaDistFromTarget = AreaLookTargetVector.Size2D() + TargetCapsuleRadius;
				bIsOverlap = AreaDistFromTarget >= ExcludeRingRadius;
				break;
			}
			}

			if (bIsOverlap)
			{
				if (bIsDotEffect)
				{
					if (TempOverlappedActorListForDot.Contains(Result.GetActor()))
					{
						/*
						* 도트대미지의 경우 모든 모든 오버랩 패턴(구간)을 하나의 장판으로 판정.
						* 하나라도 오버랩된것을 체크했다면 종료
						*/
						return;
					}

					if (OverlappedTimeList.Contains(Result.GetActor()) == false)
					{
						OverlappedTimeList.Emplace(Result.GetActor(), GetAreaInfo().AreaSectionTime);
					}

					float& OverlappedTime = OverlappedTimeList.FindOrAdd(Result.GetActor());
					OverlappedTime += InDeltaTime;

					if (GetAreaInfo().AreaSectionTime <= OverlappedTime)
					{
						OnAreaIn(InDeltaTime, Result.GetActor(), Result.GetComponent());

						OverlappedTime = 0.f;
					}
				}
				else
				{
					// 도트효과가 아닌 경우 오버랩 패턴(구간)을 별개로 처리하여 여러번 맞을 수 있음.
					OnAreaIn(InDeltaTime, Result.GetActor(), Result.GetComponent());
				}

				OverlapInfoList[Index].OverlappedActorList.Emplace(Result.GetActor());
				TempOverlappedActorListForDot.Emplace(Result.GetActor());
			}
			else
			{
				if(OverlapInfoList[Index].OverlappedActorList.Contains(Result.GetActor()))
				{ 
					OnAreaOut(InDeltaTime, Result.GetActor(), Result.GetComponent());

					OverlapInfoList[Index].OverlappedActorList.Remove(Result.GetActor());
				}
			}
		}

		if (bIsDotEffect == false)
		{
			/*
			* 도트 형태의 처리가 아닌 경우 다음 틱에서 처리 제외
			* 도트 형태의 처리가 아닌 경우 OnAreaOut의 호출은 발생하지 않는다.
			*/
			OverlapInfoList.RemoveAt(Index--);
			continue;
		}
	}
}