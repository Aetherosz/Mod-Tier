#pragma once

#include "CoreMinimal.h"

class APlayerController;
class SWidget;

/** The in-game window for replacing names and icons. Opened and closed with Ctrl+Shift+M. */
class FModTierEditor
{
public:
	static void Open(APlayerController* Player);
	static void Close();
	static bool IsOpen();

private:
	static TSharedPtr<SWidget> ViewportContent;
	static TWeakObjectPtr<APlayerController> OwningPlayer;
};
