// Fill out your copyright notice in the Description page of Project Settings.

#include "CustomProjectileActor.h"
#include "Components/SphereComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "CustomParticleSystemComponent.h"
#include "CustomSkeletalMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Particles/ParticleSystem.h"
#include "CollisionQueryParams.h"

ACustomProjectileActor::ACustomProjectileActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* InSceneComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = InSceneComp;

	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComponent"));
	ProjectileMovementComponent->bAutoActivate = false;
	ProjectileMovementComponent->bRotationFollowsVelocity = true;
	ProjectileMovementComponent->bShouldBounce = true;
	ProjectileMovementComponent->InitialSpeed = 3000.0f;
	ProjectileMovementComponent->MaxSpeed = 3000.0f;
	ProjectileMovementComponent->Bounciness = 0.3f;

	InitialLifeSpan = 5.0f;
}

void ACustomProjectileActor::BeginPlay()
{
	Super::BeginPlay();

}

void ACustomProjectileActor::Destroyed()
{
	DeActiveParticleComponent();
	DeActiveAudioComponents();
	Super::Destroyed();
}

void ACustomProjectileActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bActive == true)
	{
		if (m_ProjectileInfo.FireDelay <= ElapsedTime)
		{
			if (PreElemTM.Num() == 0)
			{
				ACustomCharacter* InCaster = Cast<ACustomCharacter>(m_ProjectileInfo.Caster);
				if (m_ProjectileInfo.SkillCID != NAME_None && (IsValid(InCaster) == false || InCaster->IsPlayingSkill(m_ProjectileInfo.SkillCID) == false))
				{
					// 발사 시점에 해당 스킬이 캔슬된 경우 발사체를 발사하지 않음.
					bActive = false;
					return;
				}

				if (SkeletalMeshComponent) SkeletalMeshComponent->SetVisibility(true);
				if (PointLightComponent) PointLightComponent->SetVisibility(true);
				if (ParticleSystemComponent) ParticleSystemComponent->Activate();
				if (AudioComponents.Num() > 0) ActiveAudioComponents();
				if (ProjectileMovementComponent) ProjectileMovementComponent->Activate(); // Projectile

				PreElemTM.AddDefaulted(1);
				PreElemTM[0] = StartElemTM;
			}

			if (HasAuthority() == true)
			{
				CheckSweep();
			}
		}
	}
	else
	{
		OnDestroy();
	}

	ElapsedTime += DeltaTime;
}

void ACustomProjectileActor::Fire(FSkillProjectileInfo InProjectileInfo)
{
	ACustomCharacter* InCaster = Cast<ACustomCharacter>(InProjectileInfo.Caster);
	if (IsValid(InCaster) == false || IsValid(ProjectileMovementComponent) == false || FinalProjectileLifetime <= 0.f)
	{
		bActive = false;
		return;
	}

	// LifeTime
	const float InMaxDistance = InProjectileInfo.ProjectileMaxMoveDistance - InProjectileInfo.CollisionExtent.X;
	const float BaseLifeSpan = InProjectileInfo.UseLifeTime ? InProjectileInfo.InitialLifeSpan + InProjectileInfo.FireDelay: 0.f;
	const float LifeTimeByDist = (InMaxDistance / InProjectileInfo.ProjectileSpeed) + InProjectileInfo.FireDelay;
	const float FinalProjectileLifetime = LifeTimeByDist <= 0.f ? BaseLifeSpan : LifeTimeByDist; // MaxDistance가 BaseLifeSpan보다 우선순위가 높다.

	SetLifeSpan(FinalProjectileLifetime);

	// Projectile
	{
		ProjectileMovementComponent->InitialSpeed = InProjectileInfo.ProjectileSpeed;
		ProjectileMovementComponent->MaxSpeed = InProjectileInfo.ProjectileSpeed;
		ProjectileMovementComponent->Velocity = CalcMoveDir(InProjectileInfo) * InProjectileInfo.ProjectileSpeed;
		ProjectileMovementComponent->ProjectileGravityScale = InProjectileInfo.ProjectileGravityScale;
		ProjectileMovementComponent->SetUpdatedComponent(RootComponent);
	}

	CollisionComponent = CreateCollision(InProjectileInfo);		// Collision
	SkeletalMeshComponent = CreateMesh(InProjectileInfo);		// Mesh
	PointLightComponent = CreateLight(InProjectileInfo);		// Light
	ParticleSystemComponent = CreateParticle(InProjectileInfo);	// Particle
	AudioComponents = CreateSound(InProjectileInfo);			// Audio

	// 초기 위치 설정
	const FVector InDir = ProjectileMovementComponent->Velocity.GetSafeNormal();
	const FVector InStartLoc = InProjectileInfo.Caster->GetActorLocation();
	StartElemTM = FTransform(GetActorLocation());
	EndElemTM = FTransform(StartElemTM.GetLocation() + (InDir * InMaxDistance));

	m_ProjectileInfo = InProjectileInfo;

	m_ProjectileInfo.ProjectileMaxMoveDistance = InMaxDistance;

	// 관통 가능여부 재설정
	const bool IsPierceableSection = InCaster->GetSkillSectionInfo().CheckFlags(ESkillSectionInfoBitflags::ProjectilePierceable);
	const bool bPierceable = InProjectileInfo.bForcePierceableChar || (IsPierceableSection == true && MyUtility::HasEnoughActionGauge(InCaster, InProjectileInfo.SkillCID, true));
	m_ProjectileInfo.bForcePierceableChar = bPierceable;

	InCaster->GetValidProjectileCountBySkill().FindOrAdd(m_ProjectileInfo.SkillCID)++;

	SetOwner(InCaster);
}

void ACustomProjectileActor::CheckSweep()
{
	ACustomCharacter* InCaster = Cast<ACustomCharacter>(m_ProjectileInfo.Caster);
	if (IsValid(InCaster) == false || CollisionComponent == nullptr)
	{
		bActive = false;
		return;
	}

	FTransform InCurElemTM = CollisionComponent->GetComponentTransform();
	const float InMoveDist = FVector::Dist(InCurElemTM.GetLocation(),StartElemTM.GetLocation());
	if (InMoveDist > m_ProjectileInfo.ProjectileMaxMoveDistance) InCurElemTM = EndElemTM;

	// SweepCheck
	TArray<struct FHitResult> OutHits;
	{
		FCollisionShape InCollisionShape;

		FQuat InSweepQuat = ProjectileMovementComponent->Velocity.GetSafeNormal2D().ToOrientationQuat();

		if (m_ProjectileInfo.CollisionShape == ECollisionSweepShapeType::Shpere)
		{
			InCollisionShape = FCollisionShape::MakeSphere(m_ProjectileInfo.CollisionExtent.X);
		}
		else if (m_ProjectileInfo.CollisionShape == ECollisionSweepShapeType::Box)
		{
			InCollisionShape = FCollisionShape::MakeBox(m_ProjectileInfo.CollisionExtent);
		}
		else if (m_ProjectileInfo.CollisionShape == ECollisionSweepShapeType::Capsule)
		{
			InCollisionShape = FCollisionShape::MakeCapsule(m_ProjectileInfo.CollisionExtent.Y, m_ProjectileInfo.CollisionExtent.X);
			InSweepQuat *= FQuat(FRotator(90.f, 0.f, 0.f));
		}

		FCollisionQueryParams InCollParams = FCollisionQueryParams();
		InCollParams.AddIgnoredActor(InCaster);
		InCollParams.AddIgnoredActor(this);
		InCollParams.AddIgnoredActors(HittedActor.Array());

		GetWorld()->SweepMultiByChannel(OutHits, PreElemTM[0].GetLocation(), InCurElemTM.GetLocation(), InSweepQuat, ECollisionChannel::ECC_GameTraceChannel12, InCollisionShape, InCollParams);
	}

	// Operate
	bool bShowDebugOnHit = false;
	for (int InCollIndex = 0; InCollIndex < OutHits.Num(); InCollIndex++)
	{
		const FHitResult& InHitResult = OutHits[InCollIndex];
		if (InHitResult.bBlockingHit == false)
		{
			continue;
		}

		AActor* TargetActor = InHitResult.GetActor();
		if (IsValid(TargetActor) == false || InCaster == TargetActor)
		{
			continue;
		}

		if (MyUtility::CanAttack(InCaster, TargetActor))
		{
			OnHit(InHitResult);
		}
		else
		{
			// 공격 불가능한 대상..
		}

		bShowDebugOnHit = true;

		const bool bIsCharacterTarget = TargetActor->IsA<ACustomCharacter>();
		if ((bIsCharacterTarget == true && m_ProjectileInfo.bForcePierceableChar == false) ||
			(bIsCharacterTarget == false && m_ProjectileInfo.bForcePierceableObject == false))
		{
			bActive = false;
			break;
		}
	}

	PreElemTM[0] = InCurElemTM;
}

void ACustomProjectileActor::OnHit(const FHitResult& InHitResult)
{
	ACustomCharacter* InCaster = Cast<ACustomCharacter>(m_ProjectileInfo.Caster);
	if (IsValid(InCaster) == false || CollisionComponent == nullptr)
	{
		return;
	}

	ACustomPlayerState* InCasterState = MyUtility::GetCustomPlayerState(InCaster);
	if (IsValid(InCasterState) == false)
	{
		return;
	}

	AActor* HitActor = InHitResult.GetActor();
	ACustomCharacter* HitCharacter = Cast<ACustomCharacter>(HitActor);
	UPrimitiveComponent* HitComp = InHitResult.GetComponent();

	HittedActor.Emplace(HitActor);

	USkeletalMeshComponent* HitAttachParentComp = nullptr;

	if (IsValid(HitCharacter))
	{
		const EHitForceType HitForceType = MyUtility::GetHitForceType(InCaster, m_ProjectileInfo.SkillCID, m_ProjectileInfo.AttackDamageIndex);
		const EHitDirType HitDirType = MyUtility::GetHitDirType(StartElemTM.GetLocation(), HitCharacter);

		InCasterState->OnSend_Hit_Skill(
			HitCharacter,
			m_ProjectileInfo.SkillCID.ToString(),
			ESkillType::SkillType_Exec_0,
			HitDirType,
			InHitResult.BoneName.ToString(),
			InHitResult.ImpactPoint,
			InHitResult.ImpactNormal,
			ESkillDamageType::ESkillDamage_Normal,
			m_ProjectileInfo.AttackDamageIndex
		);

		HitAttachParentComp = HitCharacter->GetBodyMesh();
	}

	UParticleSystem* HitParticle = m_ProjectileInfo.bForcePierceableChar ? nullptr : m_ProjectileInfo.AttachParticleOnHit;

	if (IsValid(HitAttachParentComp) && HitParticle != nullptr)
	{
		if (MyUtility::IsInDedicatedServer(GetWorld()) == true)
		{
			return;
		}
		else
		{
			if (IsValid(ParticleSystemComponent)) DeActiveParticleComponent();
			if (IsValid(PointLightComponent)) PointLightComponent->Deactivate();
			if (AudioComponents.Num() > 0) DeActiveAudioComponents();

			// 가장 가까운 소켓 찾기.
			FName TargetSocketName = NAME_None;
			float MinDistance = 0.f;

			for (const FName& AttachSocketName : m_ProjectileInfo.TargetBoneNames)
			{
				const float DistToSocket = (HitAttachParentComp->GetSocketLocation(AttachSocketName) - InHitResult.ImpactPoint).Size();
				if (TargetSocketName == NAME_None || DistToSocket < MinDistance)
				{
					TargetSocketName = AttachSocketName;
					MinDistance = DistToSocket;
				}
			}

			if (TargetSocketName != NAME_None)
			{
				FTransform Transform = HitAttachParentComp->GetSocketTransform(TargetSocketName);
				UParticleSystemComponent* Particle = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), HitParticle, Transform, true, EPSCPoolMethod::None, false);
				
				if (Particle != nullptr)
				{
					if (HitCharacter != nullptr && HitCharacter == MyUtility::GetCustomPlayerCharacter(GetWorld())) Particle->SetCustomPrimitiveDataFloat(0, 1);
					else Particle->SetCustomPrimitiveDataFloat(0, 0);
				}

				Particle->SetUsingAbsoluteLocation(false);
				Particle->SetUsingAbsoluteScale(false);
				Particle->bAutoManageAttachment = true;

				FQuat Rot = ProjectileMovementComponent->Velocity.GetSafeNormal2D().ToOrientationQuat() * FQuat(FRotator(0.f, 90.f, 0.f));
				Particle->SetWorldRotation(Rot);
				Particle->SetAutoAttachmentParameters(HitAttachParentComp, TargetSocketName, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, EAttachmentRule::SnapToTarget);
				Particle->ActivateSystem(true);

				if (IsValid(HitCharacter))
				{
					HitCharacter->AddHitParticle(Particle);
				}
			}
		}
	}
}


void ACustomProjectileActor::OnDestroy()
{
	DeActiveParticleComponent(); 
	DeActiveAudioComponents();
}

void ACustomProjectileActor::DeActiveParticleComponent()
{
	if (IsValid(ParticleSystemComponent))
	{
		if (m_ProjectileInfo.bDestroyParticleComponentOnHit == false)
		{
			for (const FName EmitterName : m_ProjectileInfo.EmitterNameToDisableOnHit)
			{
				for (int Index = 0; Index < ParticleSystemComponent->EmitterInstances.Num(); Index++)
				{
					if (ParticleSystemComponent->EmitterInstances[Index] != nullptr)
					{
						UParticleEmitter* ParticleEmitter = ParticleSystemComponent->EmitterInstances[Index]->SpriteTemplate;
						if (IsValid(ParticleEmitter) && ParticleEmitter->EmitterName == EmitterName)
						{
							ParticleSystemComponent->EmitterInstances[Index]->bEnabled = false;
						}
					}
				}
			}

			ParticleSystemComponent->Deactivate();
			ParticleSystemComponent->bAutoDestroy = true;
			ParticleSystemComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}
		else
		{
			ParticleSystemComponent->DestroyComponent();
		}
	}
}

void ACustomProjectileActor::ActiveAudioComponents()
{
	for (int Index = 0; Index < AudioComponents.Num(); Index++)
	{
		if (AudioComponents[Index].IsValid() && SoundInfos.IsValidIndex(Index))
		{
			AudioComponents[Index].Get()->Activate();
			if (SoundInfos[Index].bUseFadeIn)
			{
				FSoundFadeInfo FadeInInfo = SoundInfos[Index].FadeInInfo;
				AudioComponents[Index].Get()->FadeIn(FadeInInfo.FadeDuration, FadeInInfo.FadeVolumeLevel, FadeInInfo.StartTime, FadeInInfo.FadeCurve);
			}
		}
	}
}

void ACustomProjectileActor::DeActiveAudioComponents()
{
	for (int Index = 0; Index < AudioComponents.Num(); Index++)
	{
		bool bDestroyImmediately = true;

		if (AudioComponents[Index].IsValid() == false)
		{
			continue;
		}

		if (SoundInfos.IsValidIndex(Index))
		{
			bDestroyImmediately = !SoundInfos[Index].bUseFadeOut;
		}

		if (bDestroyImmediately)
		{
			AudioComponents[Index].Get()->DestroyComponent();
		}
		else
		{
			FSoundFadeInfo FadeInInfo = SoundInfos[Index].FadeOutInfo;
			AudioComponents[Index].Get()->bAutoDestroy = true;
			AudioComponents[Index].Get()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			AudioComponents[Index].Get()->FadeOut(SoundInfos[Index].FadeOutInfo.FadeDuration, SoundInfos[Index].FadeOutInfo.FadeVolumeLevel, SoundInfos[Index].FadeOutInfo.FadeCurve);
		}
	}
}

UCustomSkeletalMeshComponent* ACustomProjectileActor::CreateMesh(const FSkillProjectileInfo& InProjectileInfo)
{
	if (MyUtility::IsInDedicatedServer(GetWorld()) == true || IsValid(InProjectileInfo.Caster) == false || InProjectileInfo.ProjectileSkeletalMesh == nullptr)
	{
		return nullptr;
	}

	UCustomSkeletalMeshComponent* pComponent = NewObject<UCustomSkeletalMeshComponent>(this);
	if (pComponent)
	{
		pComponent->SetVisibility(false);
		pComponent->SetSkeletalMesh(InProjectileInfo.ProjectileSkeletalMesh);
		pComponent->SetRelativeTransform(InProjectileInfo.ProjectileSkeletalMeshTM);
		pComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		pComponent->RegisterComponent();
	}

	return pComponent;
}

UCustomParticleSystemComponent* ACustomProjectileActor::CreateParticle(const FSkillProjectileInfo& InProjectileInfo)
{
	if (MyUtility::IsInDedicatedServer(GetWorld()) == true || IsValid(InProjectileInfo.Caster) == false || InProjectileInfo.ProjectileParticle == nullptr)
	{
		return nullptr;
	}
	
	//Projectile이 Destroy되도 Trail은 남아야하기 때문에 월드를 오너로 세팅
	UCustomParticleSystemComponent* OutParticle = NewObject<UCustomParticleSystemComponent>(GetWorld());
	if (OutParticle)
	{
		OutParticle->RegisterComponentWithWorld(GetWorld());
		OutParticle->bAutoActivate = false;
		OutParticle->bNeverDistanceCull = true;
		OutParticle->SetTemplate(InProjectileInfo.ProjectileParticle);
		OutParticle->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		OutParticle->SetRelativeTransform(InProjectileInfo.ProjectileParticleTM);
	}

	return OutParticle;
}

UShapeComponent* ACustomProjectileActor::CreateCollision(const FSkillProjectileInfo& InProjectileInfo)
{
	UShapeComponent* InNewCollision = nullptr;

	FRotator InCollisionRotate;

	switch (InProjectileInfo.CollisionShape)
	{
	case ECollisionSweepShapeType::Box:
	{
		UBoxComponent* InNewBoxComp = NewObject<UBoxComponent>(this, UBoxComponent::StaticClass(), FName("CollisionComponent"));
		if (IsValid(InNewBoxComp)) InNewBoxComp->SetBoxExtent(InProjectileInfo.CollisionExtent);
		InNewCollision = InNewBoxComp;
		break;
	}
	case ECollisionSweepShapeType::Capsule:
	{
		UCapsuleComponent* InNewCapsuleComp = NewObject<UCapsuleComponent>(this, UCapsuleComponent::StaticClass(), FName("CollisionComponent"));
		if (IsValid(InNewCapsuleComp)) InNewCapsuleComp->SetCapsuleSize(InProjectileInfo.CollisionExtent.Z, FMath::Max(InProjectileInfo.CollisionExtent.X, InProjectileInfo.CollisionExtent.Y));
		InNewCollision = InNewCapsuleComp;
		break;
	}
	case ECollisionSweepShapeType::Shpere:
	{
		USphereComponent* InNewSphereComp = NewObject<USphereComponent>(this, USphereComponent::StaticClass(), FName("CollisionComponent"));
		if (IsValid(InNewSphereComp)) InNewSphereComp->SetSphereRadius(FMath::Max(InProjectileInfo.CollisionExtent.X, InProjectileInfo.CollisionExtent.Y));
		InNewCollision = InNewSphereComp;
		break;
	}
	}

	if (InNewCollision != nullptr)
	{
		InNewCollision->RegisterComponent();
		InNewCollision->BodyInstance.SetCollisionProfileName(TEXT("Projectile"));
		InNewCollision->bShouldCollideWhenPlacing = true;
		InNewCollision->IgnoreActorWhenMoving(GetOwner(), true);
		InNewCollision->IgnoreActorWhenMoving(this, true);
		InNewCollision->SetRelativeTransform(InProjectileInfo.CollisionTM);
		InNewCollision->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	}

	return InNewCollision;
}

UPointLightComponent* ACustomProjectileActor::CreateLight(const FSkillProjectileInfo& InProjectileInfo)
{
	if (MyUtility::IsInDedicatedServer(GetWorld()) == true || IsValid(InProjectileInfo.Caster) == false || InProjectileInfo.UsePointLight == false)
	{
		return nullptr;
	}

	UPointLightComponent* OutLightComponent = NewObject<UPointLightComponent>(this);
	if (OutLightComponent)
	{
		OutLightComponent->SetVisibility(false);
		OutLightComponent->SetMobility(EComponentMobility::Movable);
		OutLightComponent->SetLightColor(InProjectileInfo.LightColor);
		OutLightComponent->SetAttenuationRadius(InProjectileInfo.AttenuationRadius);
		OutLightComponent->SetIntensity(InProjectileInfo.Insensity);
		OutLightComponent->SetCastShadows(InProjectileInfo.CastShadow);
		OutLightComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		OutLightComponent->RegisterComponent();
	}

	return OutLightComponent;
}

TArray<TWeakObjectPtr<UAudioComponent>> ACustomProjectileActor::CreateSound(const FSkillProjectileInfo& InProjectileInfo)
{
	TArray<TWeakObjectPtr<UAudioComponent>> Result;

	if (MyUtility::IsInDedicatedServer(GetWorld()) == true || IsValid(InProjectileInfo.Caster) == false || InProjectileInfo.SoundInfo.Num() == 0)
	{
		return Result;
	}

	for (FSoundInfo SoundInfo : InProjectileInfo.SoundInfo)
	{
		UAudioComponent* OutAudio = NewObject<UAudioComponent>(GetWorld());
		if (OutAudio)
		{
			OutAudio->bAutoActivate = false;
			OutAudio->SetSound(SoundInfo.SoundBase);
			OutAudio->SetRelativeTransform(SoundInfo.SoundTM);
			OutAudio->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
			OutAudio->RegisterComponentWithWorld(GetWorld());

			Result.Add(OutAudio);
			SoundInfos.Add(SoundInfo);
		}
	}

	return Result;
}

FVector ACustomProjectileActor::CalcMoveDir(const FSkillProjectileInfo& InProjectileInfo)
{
	if (IsValid(InProjectileInfo.Caster) == false)
	{
		return FVector::ZeroVector;
	}

	FVector ProjectileMoveDir = InProjectileInfo.Caster->GetActorForwardVector();

	if (InProjectileInfo.bCalcDirFromTargetBone == true && IsValid(InProjectileInfo.Target))
	{
		// 랜덤 소켓위치 구하기
		const int InRandIndex = FMath::RandRange(0, InProjectileInfo.TargetBoneNames.Num() - 1);
		if (InProjectileInfo.TargetBoneNames.IsValidIndex(InRandIndex) && InProjectileInfo.TargetBoneNames[InRandIndex] != NAME_None)
		{
			float HalfHeight = 0.f;
			USkeletalMeshComponent* InAttachParentComp = nullptr;
			ACustomPropActor* InPropTarget = Cast<ACustomPropActor>(InProjectileInfo.Target);
			ACustomCharacter* InCharacterTarget = Cast<ACustomCharacter>(InProjectileInfo.Target);

			if (IsValid(InPropTarget))
			{
				InAttachParentComp = InPropTarget->GetSkeletalMeshComponent();

				FVector InExtent = FVector(0.f);
				FVector InOrigin = FVector(0.f);
				InPropTarget->GetActorBounds(true, InOrigin, InExtent);
				HalfHeight = InExtent.Z / 2.f;
			}
			else if (IsValid(InCharacterTarget))
			{
				InAttachParentComp = InCharacterTarget->GetBodyMesh();
				HalfHeight = InCharacterTarget->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			}

			if (InAttachParentComp != nullptr)
			{
				FVector InTargetSocketLoc = InAttachParentComp->GetSocketLocation(InProjectileInfo.TargetBoneNames[InRandIndex]);
				ProjectileMoveDir = InTargetSocketLoc - GetActorLocation();
			}
		}
	}

	if (InProjectileInfo.Angle != 0.f)
	{
		ProjectileMoveDir = ProjectileMoveDir.RotateAngleAxis(InProjectileInfo.Angle, FVector::UpVector);
	}

	return ProjectileMoveDir.GetSafeNormal();
}
