// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLens.h"
#include "NetLensLog.h"

DEFINE_LOG_CATEGORY(LogNetLens);

#define LOCTEXT_NAMESPACE "FNetLensModule"

void FNetLensModule::StartupModule()
{
	UE_LOG(LogNetLens, Log, TEXT("NetLens started."));
}

void FNetLensModule::ShutdownModule()
{
	UE_LOG(LogNetLens, Log, TEXT("NetLens shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNetLensModule, NetLens)
