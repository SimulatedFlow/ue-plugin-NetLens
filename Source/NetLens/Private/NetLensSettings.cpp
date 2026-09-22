// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensSettings.h"

UNetLensSettings::UNetLensSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("NetLens");
}

FName UNetLensSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UNetLensSettings::GetSectionName() const
{
	return TEXT("NetLens");
}

const UNetLensSettings& UNetLensSettings::Get()
{
	// GetDefault on a UDeveloperSettings is the class default object, which is created with the module and
	// lives as long as it. There is no window in which this can be null and no reason to guard it.
	return *GetDefault<UNetLensSettings>();
}
