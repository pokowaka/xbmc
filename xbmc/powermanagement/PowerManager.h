/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "IPowerSyscall.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class CFileItem;
class CSetting;
class CSettings;

<<<<<<< HEAD
enum PowerState
{
  POWERSTATE_QUIT       = 0,
  POWERSTATE_SHUTDOWN,
  POWERSTATE_HIBERNATE,
  POWERSTATE_SUSPEND,
  POWERSTATE_REBOOT,
  POWERSTATE_MINIMIZE,
  POWERSTATE_NONE,
  POWERSTATE_ASK
};

// For systems without PowerSyscalls we have a NullObject
class CNullPowerSyscall : public CAbstractPowerSyscall
{
public:
  bool Powerdown() override { return false; }
  bool Suspend() override { return false; }
  bool Hibernate() override { return false; }
  bool Reboot() override { return false; }

  bool CanPowerdown() override { return true; }
  bool CanSuspend() override { return true; }
  bool CanHibernate() override { return true; }
  bool CanReboot() override { return true; }

  int  BatteryLevel() override { return 0; }


  bool PumpPowerEvents(IPowerEventsCallback *callback) override { return false; }

  bool ProcessAction(const CAction& action) override { return false; }
};
=======
struct IntegerSettingOption;
>>>>>>> xbmc/master

// This class will wrap and handle PowerSyscalls.
// It will handle and decide if syscalls are needed.
class CPowerManager : public IPowerEventsCallback
{
public:
  CPowerManager();
  ~CPowerManager() override;

  void Initialize();
  void SetDefaults();

  bool Powerdown();
  bool Suspend();
  bool Hibernate();
  bool Reboot();

  bool CanPowerdown();
  bool CanSuspend();
  bool CanHibernate();
  bool CanReboot();
<<<<<<< HEAD
  bool IsSuspending() { return m_suspended; }
  
=======

>>>>>>> xbmc/master
  int  BatteryLevel();

  void ProcessEvents();

  static void SettingOptionsShutdownStatesFiller(std::shared_ptr<const CSetting> setting, std::vector<IntegerSettingOption> &list, int &current, void *data);

  IPowerSyscall* GetPowerSyscall() const { return m_instance.get(); };

  bool ProcessAction(const CAction& action);
private:
  void OnSleep() override;
  void OnWake() override;
  void OnLowBattery() override;
  void RestorePlayerState();
  void StorePlayerState();

<<<<<<< HEAD
  IPowerSyscall *m_instance;
  bool m_suspended;
};

extern CPowerManager g_powerManager;
=======
  // Construction parameters
  std::shared_ptr<CSettings> m_settings;
>>>>>>> xbmc/master

  std::unique_ptr<IPowerSyscall> m_instance;
  std::unique_ptr<CFileItem> m_lastPlayedFileItem;
  std::string m_lastUsedPlayer;
};
