#pragma once
#include <map>

struct ZergHell {
public:
  void onFrame();
  ZergHell();
private:
  int armyResourceID = 1;
  int armyResourceIDMax = 1;
  void assignIdleWorkers();
  bool attack = false;
  std::map<int, BWAPI::TilePosition> baseLocations;
  void build(BWAPI::UnitType type);
  BWAPI::Unit buildDrone = nullptr;
  bool canAfford(BWAPI::UnitType type);
  bool canAfford(BWAPI::UpgradeType type);
  void checkArmy();
  void checkBuildDrone();
  void checkBuildings();
  void checkEggs();
  void checkEnemyBuildings();
  void checkScout();
  void debugDraws();
  BWAPI::Position defensePoint;
  BWAPI::Unit detector = nullptr;
  std::map<BWAPI::TilePosition, BWAPI::UnitType> fogOfWarBuildings;
  void morphLarva();
  bool needSupply();
  BWAPI::Unit scout = nullptr;
  BWAPI::Player self;
  std::map<BWAPI::TilePosition, bool> startLocations;
  int clearBuildDroneCounter = 0;
};