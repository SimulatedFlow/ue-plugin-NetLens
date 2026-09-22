// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Everything NetLens says.
 *
 * A diagnostic tool that fails quietly is worse than one that is not installed, because you go on believing
 * the empty panel. Every reason NetLens has for showing you nothing - no net driver, no connections, an
 * unreachable CSV folder, an RPC hook it could not take - is logged here at Warning, never swallowed.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogNetLens, Log, All);
