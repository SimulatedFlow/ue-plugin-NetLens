// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NetLensHUD.generated.h"

/**
 * A HUD that draws the NetLens panel, for projects that would rather be explicit than rely on a delegate.
 *
 * Most projects should not need this. NetLens draws itself through AHUD::OnHUDPostRender on whatever HUD
 * class the project already has, which is the setting you want in a shipping game where the HUD class is
 * already spoken for. This class is here for two cases: a project that prefers its diagnostics wired up
 * where it can see them, and a demo map, where the whole point is that the panel appears with no
 * configuration at all.
 *
 * Using both at once is safe. The panel is drawn by one function that refuses to draw twice in a frame, so
 * whichever route arrives first wins and the other does nothing.
 *
 * Nothing here is UMG. The panel is UCanvas from end to end, which is what lets it survive into a packaged
 * build with no widget tree - and a packaged build is precisely where you cannot open a profiler.
 */
UCLASS(meta = (DisplayName = "NetLens HUD"))
class NETLENS_API ANetLensHUD : public AHUD
{
	GENERATED_BODY()

public:
	ANetLensHUD();

	//~ AHUD interface
	virtual void DrawHUD() override;

	/**
	 * Draw the panel from this HUD.
	 *
	 * Off does not hide the panel - the any-HUD route may still be drawing it, and the console command still
	 * governs whether anything is drawn at all. It only stops *this* class from being one of the routes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetLens")
	bool bDrawNetLensPanel = true;

	/**
	 * Turn the panel on when this HUD starts, whatever the project setting says.
	 *
	 * The reason to place this HUD at all is usually that you want the panel; having to type a console
	 * command afterwards would make that a strange way to want it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetLens")
	bool bShowPanelOnBeginPlay = true;

	/** Override the panel's position for this HUD. Negative on either axis uses the project setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetLens")
	FVector2D PanelOrigin = FVector2D(-1.0f, -1.0f);

	/** Override the panel's width for this HUD. Zero or less uses the project setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetLens")
	float PanelWidth = 0.0f;

protected:
	virtual void BeginPlay() override;
};
