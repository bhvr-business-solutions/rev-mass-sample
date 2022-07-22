// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassObserverProcessor.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "MassObserverProcessor.h"
#include "MSPhysicsTrait.generated.h"


USTRUCT()
struct MASSSAMPLE_API FMSPhysicsFragment : public FMassFragment
{
	GENERATED_BODY()

	FSingleParticlePhysicsProxy* Proxy = nullptr;
	Chaos::FPhysicsSolver* Solver = nullptr;

	~FMSPhysicsFragment();
};

/**
 * 
 */
UCLASS()
class MASSSAMPLE_API UMSPhysicsTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;

};


UCLASS()
class MASSSAMPLE_API UMSNewPhysicsEntitiesObserver : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UMSNewPhysicsEntitiesObserver();

protected:

	virtual void ConfigureQueries() override;

	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery physicsQuery;
	TSharedPtr<class Chaos::FImplicitObject, ESPMode::ThreadSafe> CubeGeometry;
};

UCLASS()
class MASSSAMPLE_API UMSPhysicsSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

	UMSPhysicsSyncProcessor();

	virtual void ConfigureQueries() override;

	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery physicsEntitiesQuery;
};


