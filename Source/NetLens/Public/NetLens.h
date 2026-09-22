// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * The whole plugin is one runtime module. It owns no singletons and starts nothing: the measuring lives in
 * a world subsystem, which comes and goes with the world it is measuring.
 */
class FNetLensModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
