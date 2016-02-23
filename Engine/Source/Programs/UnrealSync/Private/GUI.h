// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Initializes and starts GUI.
 *
 * @param CommandLine Command line passed to the program.
 * @param bP4EnvTabOnly Tells if initialize GUI with P4 env settings tab only.
 */
void InitGUI(const TCHAR* CommandLine, bool bP4EnvTabOnly = false);

/**
 * Saves GUI settings to settings cache.
 */
void SaveGUISettings();
