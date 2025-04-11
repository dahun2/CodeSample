// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFramework/Actor.h"
#include "CustomProjectileActor.generated.h"

class USphereComponent;
class UCustomParticleSystemComponent;
class UPointLightComponent;
class UAudioComponent;
class UProjectileMovementComponent;

UCLASS()
class ACustomProjectileActor : public AActor
{
	GENERATED_UCLASS_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

public:
	virtual void Tick(float DeltaSeconds) override;
		
	void Fire(FSkillProjectileInfo InProjectileInfo);

private:
	void CheckSweep();
	void OnHit(const FHitResult& InHitResult);
	void OnDestroy();

	void DeActiveParticleComponent();

	void ActiveAudioComponents();
	void DeActiveAudioComponents();

	UCustomSkeletalMeshComponent* CreateMesh(const FSkillProjectileInfo& InProjectileInfo);
	UCustomParticleSystemComponent* CreateParticle(const FSkillProjectileInfo& InProjectileInfo);
	UShapeComponent* CreateCollision(const FSkillProjectileInfo& InProjectileInfo);
	UPointLightComponent* CreateLight(const FSkillProjectileInfo& InProjectileInfo);
	TArray<TWeakObjectPtr<UAudioComponent>> CreateSound(const FSkillProjectileInfo& InProjectileInfo);

	FVector CalcMoveDir(const FSkillProjectileInfo& InProjectileInfo);


private:
	UPROPERTY(VisibleDefaultsOnly, Category = Projectile)
	UShapeComponent* CollisionComponent = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = Projectile)
	UCustomSkeletalMeshComponent* SkeletalMeshComponent = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = Projectile)
	UCustomParticleSystemComponent* ParticleSystemComponent = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = Projectile)
	UPointLightComponent* PointLightComponent = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = Projectile)
	TArray<TWeakObjectPtr<UAudioComponent>> AudioComponents;

	UPROPERTY(VisibleDefaultsOnly, Category = Projectile)
	TArray<FSoundInfo> SoundInfos;

	UPROPERTY(VisibleAnywhere, Category = Projectile)
	UProjectileMovementComponent* ProjectileMovementComponent = nullptr;


private:
	TArray<FTransform> PreElemTM;
	FTransform StartElemTM;
	FTransform EndElemTM;

	TSet<TWeakObjectPtr<const AActor>> HittedActor;
	FSkillProjectileInfo m_ProjectileInfo;

	bool bActive = true;

	float ElapsedTime = 0.f;

	FDelegateHandle OnHitReactionHandle;
};
