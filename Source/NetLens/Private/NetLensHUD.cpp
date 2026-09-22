// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensHUD.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "NetLensSettings.h"
#include "NetLensSubsystem.h"

ANetLensHUD::ANetLensHUD()
{
	// The HUD ticks for its own reasons; nothing here needs a tick of its own. DrawHUD is called by the
	// renderer when there is a canvas to draw on, which is the only moment this class has anything to do.
	PrimaryActorTick.bCanEverTick = false;
}

void ANetLensHUD::BeginPlay()
{
	Super::BeginPlay();

	if (!bShowPanelOnBeginPlay)
	{
		return;
	}

	if (UNetLensSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UNetLensSubsystem>() : nullptr)
	{
		Subsystem->SetShowOverlay(true);
	}
}

void ANetLensHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bDrawNetLensPanel || !Canvas)
	{
		return;
	}

	UWorld* World = GetWorld();
	UNetLensSubsystem* Subsystem = World ? World->GetSubsystem<UNetLensSubsystem>() : nullptr;
	if (!Subsystem || !Subsystem->IsShowingOverlay())
	{
		return;
	}

	const UNetLensSettings& Settings = UNetLensSettings::Get();

	// Negative means "not overridden here". Zero is a real coordinate - the top left corner - so it cannot
	// be the sentinel, and a panel silently jumping to the project default because somebody typed 0 would be
	// an annoying afternoon.
	const FVector2D Origin = (PanelOrigin.X >= 0.0 && PanelOrigin.Y >= 0.0)
		? PanelOrigin
		: Settings.OverlayOrigin;

	const float Width = PanelWidth > 0.0f ? PanelWidth : Settings.OverlayWidth;

	Subsystem->DrawOverlay(Canvas, Origin, Width);
}
