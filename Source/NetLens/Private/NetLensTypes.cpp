// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensTypes.h"

void FNetLensWindow::Configure(double InWindowSeconds, int32 InBucketCount)
{
	WindowSeconds = FMath::Max(0.1, InWindowSeconds);

	// Two is the floor. One bucket is not a ring, it is a counter that empties all at once, and a table
	// driven by it would blink every WindowSeconds instead of decaying.
	const int32 BucketCount = FMath::Clamp(InBucketCount, 2, 512);

	BucketSeconds = WindowSeconds / static_cast<double>(BucketCount);

	BucketBytes.SetNumZeroed(BucketCount, EAllowShrinking::Yes);
	BucketEvents.SetNumZeroed(BucketCount, EAllowShrinking::Yes);

	Reset();
}

void FNetLensWindow::Reset()
{
	for (double& Value : BucketBytes)
	{
		Value = 0.0;
	}
	for (double& Value : BucketEvents)
	{
		Value = 0.0;
	}

	TotalBytes = 0.0;
	TotalEvents = 0.0;
	NewestIndex = MIN_int64;
}

void FNetLensWindow::RollTo(int64 TargetIndex)
{
	const int32 BucketCount = BucketBytes.Num();
	if (BucketCount == 0)
	{
		return;
	}

	if (NewestIndex == MIN_int64)
	{
		NewestIndex = TargetIndex;
		return;
	}

	if (TargetIndex <= NewestIndex)
	{
		return;
	}

	const int64 Steps = TargetIndex - NewestIndex;

	// A gap longer than the whole ring means nothing in it can still be inside the window. Clearing it in
	// one go is both correct and the only way this stays cheap when a game has been paused on a breakpoint
	// for two minutes.
	if (Steps >= BucketCount)
	{
		for (int32 Index = 0; Index < BucketCount; ++Index)
		{
			BucketBytes[Index] = 0.0;
			BucketEvents[Index] = 0.0;
		}
		TotalBytes = 0.0;
		TotalEvents = 0.0;
		NewestIndex = TargetIndex;
		return;
	}

	for (int64 Step = 1; Step <= Steps; ++Step)
	{
		const int32 Slot = static_cast<int32>((NewestIndex + Step) % BucketCount);

		TotalBytes -= BucketBytes[Slot];
		TotalEvents -= BucketEvents[Slot];

		BucketBytes[Slot] = 0.0;
		BucketEvents[Slot] = 0.0;
	}

	// Subtraction of doubles cannot be trusted to land exactly on zero after thousands of additions and
	// removals. Clamping here is cheaper than a rate that reads -0.0001 KB/s on an idle actor.
	TotalBytes = FMath::Max(0.0, TotalBytes);
	TotalEvents = FMath::Max(0.0, TotalEvents);

	NewestIndex = TargetIndex;
}

void FNetLensWindow::Add(double Now, double Bytes, double Events)
{
	const int32 BucketCount = BucketBytes.Num();
	if (BucketCount == 0)
	{
		Configure(WindowSeconds, 20);
	}

	const int64 TargetIndex = static_cast<int64>(FMath::FloorToDouble(Now / BucketSeconds));

	RollTo(TargetIndex);

	// A sample from before the newest bucket lands in the newest one. See the header: losing a little
	// accuracy in a case that should not happen beats writing into a bucket that has already been counted.
	const int64 WriteIndex = FMath::Max(TargetIndex, NewestIndex);
	const int32 Slot = static_cast<int32>(((WriteIndex % BucketBytes.Num()) + BucketBytes.Num()) % BucketBytes.Num());

	BucketBytes[Slot] += Bytes;
	BucketEvents[Slot] += Events;

	TotalBytes += Bytes;
	TotalEvents += Events;
}

void FNetLensWindow::Advance(double Now)
{
	if (BucketBytes.Num() == 0)
	{
		return;
	}

	RollTo(static_cast<int64>(FMath::FloorToDouble(Now / BucketSeconds)));
}

double FNetLensWindow::GetBytesPerSecond() const
{
	return WindowSeconds > 0.0 ? TotalBytes / WindowSeconds : 0.0;
}

double FNetLensWindow::GetEventsPerSecond() const
{
	return WindowSeconds > 0.0 ? TotalEvents / WindowSeconds : 0.0;
}

double FNetLensWindow::GetPeakBytesPerSecond() const
{
	if (BucketSeconds <= 0.0)
	{
		return 0.0;
	}

	double Peak = 0.0;
	for (const double Value : BucketBytes)
	{
		Peak = FMath::Max(Peak, Value);
	}

	return Peak / BucketSeconds;
}
