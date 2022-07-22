// Fill out your copyright notice in the Description page of Project Settings.

#include "Tests/MSPhysicsTrait.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "Common/Fragments/MSFragments.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/ImplicitObject.h"

#ifndef WITH_CHAOS
#error
#endif

FMSPhysicsFragment::~FMSPhysicsFragment()
{
	// TODO: should actually do this on fragment removal. Do fragments get their destructors called?
	if (Proxy && Solver)
	{
		if(!Proxy->GetSyncTimestamp()->bDeleted)
			Solver->UnregisterObject(Proxy);
		Proxy = nullptr;
		Solver = nullptr;
	}
	ensure(!Proxy && !Solver);
}

void UMSPhysicsTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragment<FTransformFragment>();
	auto& physFragment = BuildContext.AddFragment_GetRef<FMSPhysicsFragment>();

}


UMSNewPhysicsEntitiesObserver::UMSNewPhysicsEntitiesObserver()
{
	ObservedType = FMSPhysicsFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);

	bAutoRegisterWithProcessingPhases = false;
	CubeGeometry = TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>(new Chaos::TBox<Chaos::FReal, 3>(Chaos::FVec3(-50, -50, -50), Chaos::FVec3(50, 50, 50)));

}

void UMSNewPhysicsEntitiesObserver::ConfigureQueries()
{
	physicsQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	physicsQuery.AddRequirement<FMSPhysicsFragment>(EMassFragmentAccess::ReadWrite);
}

void UMSNewPhysicsEntitiesObserver::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	SCOPED_NAMED_EVENT(STAT_MASS_UMSNewPhysicsEntitiesObserver_Execute, FColor::Red);
	auto& World = *EntitySubsystem.GetWorld();
	if (FPhysScene* WorldPhysScene = World.GetPhysicsScene())
	{
		if (Chaos::FPhysicsSolver* Solver = WorldPhysScene->GetSolver())
		{
			physicsQuery.ForEachEntityChunk(EntitySubsystem, Context, [&, this](FMassExecutionContext& Context)
				{
					auto Transforms = Context.GetFragmentView<FTransformFragment>();
					auto PhysFragments = Context.GetMutableFragmentView<FMSPhysicsFragment>();

					for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
					{
						FTransform transform = Transforms[EntityIndex].GetTransform();
						transform.AddToTranslation(FVector(0, 0, 200));
						auto& physFragment = PhysFragments[EntityIndex];

						// Inspired by FInitBodiesHelperBase::CreateShapesAndActors

						FPhysicsCommand::ExecuteWrite(WorldPhysScene, [&]() {
							// Should probably use FChaosEngineInterface::CreateActor ?
#if 1
							// static Cube
							FActorCreationParams params;
							params.bEnableGravity = false;
							params.bStartAwake = true;
							params.bSimulatePhysics = false;
							params.bStatic = false;
							params.InitialTM = transform;
							FPhysicsActorHandle CubeProxy;
							FChaosEngineInterface::CreateActor(params, CubeProxy);
							auto& CubeParticle = CubeProxy->GetGameThreadAPI();
#else
							FSingleParticlePhysicsProxy* CubeProxy = FSingleParticlePhysicsProxy::Create(Chaos::FGeometryParticle::CreateParticle());
							
							auto& CubeParticle = CubeProxy->GetGameThreadAPI();
							CubeParticle.SetX(transform.GetLocation(), /*bInvalidate=*/false); //do not generate wake event since this is part of initialization
							CubeParticle.SetR(transform.GetRotation(), /*bInvalidate=*/false);
#endif


							CubeParticle.SetObjectState(Chaos::EObjectStateType::Dynamic);

							CubeParticle.SetGeometry(CubeGeometry);
							for (const TUniquePtr<Chaos::FPerShapeData>& Shape : CubeProxy->GetParticle_LowLevel()->ShapesArray())
							{
								Shape->SetQueryEnabled(true);
								Chaos::FCollisionData collData = Shape->GetCollisionData();
								// Collide with all channels.
								collData.QueryData.Word1 = 0xFFFFF;
								collData.QueryData.Word3 = 0xFFFFF;
#if 0
								collData.SimData.Word1 = 0xFFFFF;
								collData.SimData.Word3 = 0xFFFFF;
#else
								collData.SimData.Word1 = 0;
								collData.SimData.Word3 = 0;
#endif
								Shape->SetCollisionData(collData);
							}
							//Solver->RegisterObject(CubeProxy);
							FChaosEngineInterface::AddActorToSolver(CubeProxy, Solver);

							physFragment.Proxy = CubeProxy;
							physFragment.Solver = Solver;
							});
					}
				});
		}
	}
}


UMSPhysicsSyncProcessor::UMSPhysicsSyncProcessor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);
}

void UMSPhysicsSyncProcessor::ConfigureQueries()
{
	physicsEntitiesQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	physicsEntitiesQuery.AddRequirement<FMSPhysicsFragment>(EMassFragmentAccess::ReadWrite);
}


#pragma optimize("",off)
static bool setSpeedOrPosition = true;
static int rollingUpdate = 0;
static int rollingUpdateRate = 0;
#pragma optimize("",on)

void UMSPhysicsSyncProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_MASS_UMSPhysicsSyncProcessor_ExecuteCycleCounter);
	auto& World = *EntitySubsystem.GetWorld();
	if (FPhysScene* WorldPhysScene = World.GetPhysicsScene())
	{
		if (Chaos::FPhysicsSolver* Solver = WorldPhysScene->GetSolver())
		{
			FPhysicsCommand::ExecuteWrite(WorldPhysScene, [&]() {
				physicsEntitiesQuery.ForEachEntityChunk(EntitySubsystem, Context, [&, this](FMassExecutionContext& Context) {
					auto Transforms = Context.GetFragmentView<FTransformFragment>();
					auto PhysFragments = Context.GetMutableFragmentView<FMSPhysicsFragment>();

					for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
					{
						const FTransform& transform = Transforms[EntityIndex].GetTransform();
						auto& physFragment = PhysFragments[EntityIndex];

						auto& CubeParticle = physFragment.Proxy->GetGameThreadAPI();
						const auto destination = transform.GetLocation() + Chaos::FVec3(0, 0, 300);
						auto currentPhysPos = CubeParticle.X();
						if (!rollingUpdateRate || ((rollingUpdate + EntityIndex) % rollingUpdateRate) == 0)
						{
							if (setSpeedOrPosition)
								CubeParticle.SetV(destination - currentPhysPos);
							else
								CubeParticle.SetX(destination);
						}
						//CubeParticle.SetCenterOfMass(destination);

					}

					});
				});
		}
	}
	rollingUpdate++;
}


