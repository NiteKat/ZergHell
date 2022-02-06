#include <BWAPI.h>

#include "ZergHell.h"

enum ClientInfoKeys {
  morphingType,
  buildingType,
  buildX,
  buildY,
  firstGas,
  secondGas,
  thirdGas,
  attackTime,
  cooldown
};


void ZergHell::onFrame() {
  assignIdleWorkers();
  checkArmy();
  checkBuildDrone();
  checkBuildings();
  checkScout();
  checkEggs();
  morphLarva();
  checkEnemyBuildings();
  debugDraws();
}

ZergHell::ZergHell() {
  // Set the self pointer, which is used in place of always calling
  // the BWAPI::Broodwar->self() function.
  self = BWAPI::Broodwar->self();

  // Add the start locations to the map tracking if we've scouted them or not.
  for (auto& startLocation : BWAPI::Broodwar->getStartLocations()) {
    if (startLocation == self->getStartLocation()) {
      startLocations[startLocation] = true;
    }
    else {
      startLocations[startLocation] = false;
    }
  }

  defensePoint = (BWAPI::Position)self->getStartLocation();

  // Group resources by their Resource ID for determining base locations.
  std::map<int, std::vector<BWAPI::Unit>> resources;
  // Loop through all mineral patches.
  for (auto resource : BWAPI::Broodwar->getStaticMinerals()) {
    // Ignore blocking minerals, we are assuming these are less than 40.
    if (40 < resource->getResources()) {
      resources[resource->getResourceGroup()].push_back(resource);
    }
  }
  // Loop through all geysers.
  for (auto resource : BWAPI::Broodwar->getStaticGeysers()) {
    resources[resource->getResourceGroup()].push_back(resource);
  }
  // Figure out the Resource ID group for our start location by grabbing the closest
  // mineral patch to our start location and using it's resource group ID.
  auto startResource = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)self->getStartLocation(), BWAPI::Filter::IsMineralField);
  baseLocations[startResource->getResourceGroup()] = self->getStartLocation();

  // Loop through the organized resources to calculate base locations.
  for (auto& grouping : resources) {
    if (armyResourceIDMax < grouping.first) {
      armyResourceIDMax = grouping.first;
    }
    if (grouping.first != startResource->getResourceGroup()) {
      BWAPI::TilePosition baseTile = { 0, 0 };
      for (auto resource : grouping.second) {
        baseTile += resource->getTilePosition();
      }

      baseTile.x /= grouping.second.size();
      baseTile.y /= grouping.second.size();
      baseLocations[grouping.first] = baseTile;
    }
  }
}

void ZergHell::assignIdleWorkers() {
  // loop for idle workers and tell them to mine.
  // also check if a worker needs to defend itself from attack.
  for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
    // ignore non-workers for this loop
    if (!unit->getType().isWorker()) {
      continue;
    }

    // ignore the scout
    if (unit == scout) {
      continue;
    }

    auto attackTime = unit->getClientInfo<int>(ClientInfoKeys::attackTime);
    if (attackTime) {
      attackTime--;
      unit->setClientInfo<int>(attackTime, ClientInfoKeys::attackTime);
      if (!attackTime) {
        unit->stop();
      }
    }
    
    if (unit->isUnderAttack()) {
      auto enemy = unit->getClosestUnit(BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying);
      if (enemy) {
        unit->attack(enemy);
        unit->setClientInfo<int>(420, ClientInfoKeys::attackTime);
      }
    }
    else if (unit->isIdle()) {
      auto resource = unit->getClosestUnit(BWAPI::Filter::IsMineralField);
      if (!resource) {
        continue;
      }
      unit->gather(resource);
    }
  }
}

void ZergHell::build(BWAPI::UnitType type) {
  auto buildLocation = BWAPI::Broodwar->getBuildLocation(type, self->getStartLocation(), 64, true);
  buildDrone = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)buildLocation, (BWAPI::Filter::GetPlayer == self && BWAPI::Filter::IsWorker && BWAPI::Filter::GetRace == BWAPI::Races::Zerg && (BWAPI::Filter::IsGatheringGas || BWAPI::Filter::IsGatheringMinerals)));
  if (buildDrone == scout) {
    buildDrone = nullptr;
  }
  if (buildDrone) {
    if (!buildDrone->build(type, buildLocation)) {
      BWAPI::Broodwar->drawCircleMap(buildDrone->getPosition(), 10, BWAPI::Colors::Red, true);
      std::cout << BWAPI::Broodwar->getLastError() << "\n";
      buildDrone = nullptr;
    }
    else {
      buildDrone->setClientInfo<int>(type, buildingType);
      buildDrone->setClientInfo<int>(buildLocation.x, buildX);
      buildDrone->setClientInfo<int>(buildLocation.y, buildY);
    }
  }
}

bool ZergHell::canAfford(BWAPI::UnitType type) {
  auto race = type.getRace();
  return self->supplyUsed(race) <= self->supplyTotal(race) - type.supplyRequired()
    && BWAPI::Broodwar->canMake(type)
    && type.mineralPrice() <= self->minerals()
    && type.gasPrice() <= self->gas();
}

bool ZergHell::canAfford(BWAPI::UpgradeType type) {
  return type.mineralPrice() <= self->minerals()
    && type.gasPrice() <= self->gas();
}

void ZergHell::checkArmy() {
  // Validate that detector is still valid.
  if (detector
    && !detector->exists()) {
    detector = nullptr;
  }

  if (attack) {
    if (self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk) <= 15) {
      attack = false;
      return;
    }


    BWAPI::TilePosition target = BWAPI::TilePositions::None;
    // Get closest visible enemy building.
    auto enemy = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)self->getStartLocation(), BWAPI::Filter::IsEnemy && BWAPI::Filter::IsBuilding);
    if (!enemy) {
      double closest = DBL_MAX;
      // No enemy buildings are visible, check fog of war buildings.
      for (auto& potentialTarget : fogOfWarBuildings) {
        if (self->getStartLocation().getDistance(potentialTarget.first) < closest) {
          target = potentialTarget.first;
        }
      }
    }
    else {
      target = enemy->getTilePosition();
    }
    if (target != BWAPI::TilePositions::None) {
      // Assign a detector if we do not have one.
      if (!detector) {
        detector = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)target, BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Overlord && BWAPI::Filter::GetPlayer == self);
      }
      for (auto& unit : self->getUnits()) {
        if (unit->getType() != BWAPI::UnitTypes::Zerg_Hydralisk
          && unit != detector) {
          continue;
        }

        if (unit == detector) {
          auto enemyCloaked = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)target, (BWAPI::Filter::IsCloaked || BWAPI::Filter::IsBurrowed) && BWAPI::Filter::IsEnemy);
          BWAPI::Unit followUnit = nullptr;
          if (enemyCloaked) {
            followUnit = BWAPI::Broodwar->getClosestUnit(enemyCloaked->getPosition(), BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Hydralisk && BWAPI::Filter::GetPlayer == self);
          }
          if (!followUnit) {
            followUnit = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)target, BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Hydralisk && BWAPI::Filter::GetPlayer == self);
          }
          if (followUnit) {
            unit->move(followUnit->getPosition());
          }
          else {
            unit->move((BWAPI::Position)defensePoint);
          }
        }
        else {
          if (unit->isIdle()) {
            unit->attack((BWAPI::Position)target);
          }
        }
      }
    }
    else {
      auto looped = false;
      while (BWAPI::Broodwar->isVisible(baseLocations[armyResourceID])) {
        armyResourceID++;
        if (armyResourceIDMax < armyResourceID) {
          if (looped) {
            break;
          }
          looped = true;
          armyResourceID = 1;
        }
      }
      target = baseLocations[armyResourceID];
      // Assign a detector if we do not have one.
      if (!detector) {
        detector = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)target, BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Overlord && BWAPI::Filter::GetPlayer == self);
      }
      for (auto& unit : self->getUnits()) {
        if (unit->getType() != BWAPI::UnitTypes::Zerg_Hydralisk
          && unit != detector) {
          continue;
        }

        if (unit == detector) {
          unit->move((BWAPI::Position)target);
        }
        else if (unit->isIdle()) {
          unit->attack((BWAPI::Position)target);
        }
      }
    }
  }
  else if (!attack && self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk) >= 30) {
    attack = true;
  }
  else {
    // Assign a detector if we do not have one.
    if (!detector) {
      detector = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)defensePoint, BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Overlord && BWAPI::Filter::GetPlayer == self);
    }
    // Loop units to have them attack to the defense point.
    for (auto& unit : self->getUnits()) {
      if (unit->getType() != BWAPI::UnitTypes::Zerg_Hydralisk
        && unit != detector) {
        continue;
      }

      if (unit == detector) {
        unit->move(defensePoint);
      }
      else if (unit->isIdle()) {
        unit->attack(defensePoint);
      }
    }
  }
}

void ZergHell::checkBuildDrone() {
  // Check to see if a drone is already assigned to build something.
  // We are limiting our building production to 1 at a time. Note that
  // we could have more than one building actively morphing, but we are
  // only sending one drone out at a time to build.

  // If we don't have a build drone, lets see if we need to build anything.
  if (!buildDrone) {
    if (!self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool)
      && canAfford(BWAPI::UnitTypes::Zerg_Spawning_Pool)) {
      build(BWAPI::UnitTypes::Zerg_Spawning_Pool);
    }
    else if (self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool)
      && self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) + self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Sunken_Colony) < 4
      && canAfford(BWAPI::UnitTypes::Zerg_Creep_Colony)) {
      build(BWAPI::UnitTypes::Zerg_Creep_Colony);
    }
    else if (self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool)
      && self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) + self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Sunken_Colony) >= 4
      && self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Extractor) < 1
      && canAfford(BWAPI::UnitTypes::Zerg_Extractor)) {
      build(BWAPI::UnitTypes::Zerg_Extractor);
    }
    else if (self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Extractor)
      && self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) < 1
      && canAfford(BWAPI::UnitTypes::Zerg_Hydralisk_Den)) {
      build(BWAPI::UnitTypes::Zerg_Hydralisk_Den);
    }
    else if (self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Hatchery) + self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Lair) < 5
      && canAfford(BWAPI::UnitTypes::Zerg_Hatchery)) {
      build(BWAPI::UnitTypes::Zerg_Hatchery);
    }
  }
  // We have a build drone, lets see if we need to do something with it or unassign it.
  else {
    if (buildDrone->getType() != BWAPI::UnitTypes::Zerg_Drone
      || buildDrone->isIdle()
      || buildDrone->getOrder() == BWAPI::Orders::IncompleteBuilding
      || !buildDrone->exists()) {
      buildDrone = nullptr;
    }
    //else if (buildDrone->isGatheringMinerals() || buildDrone->isGatheringGas()) {
      //if (!buildDrone->build(buildDrone->getClientInfo<int>(buildingType), BWAPI::TilePosition{ buildDrone->getClientInfo<int>(buildX), buildDrone->getClientInfo<int>(buildY) })) {
      //  buildDrone = nullptr;
      //}
    //}
  }
}

void ZergHell::checkBuildings() {
  // Loop for buildings.
  double closestDistance = DBL_MAX;
  for (auto& unit : self->getUnits()) {
    if (!unit->getType().isBuilding()) {
      continue;
    }

    // Calculate closest of my buildings to enemy buildings, so I can set the defense point
    // later.
    for (auto& enemyBuilding : fogOfWarBuildings) {
      auto thisDistance = unit->getDistance((BWAPI::Position)enemyBuilding.first);
      if ( thisDistance < closestDistance) {
        closestDistance = thisDistance;
        defensePoint = unit->getPosition();
      }
    }

    for (auto& enemyUnit : BWAPI::Broodwar->getAllUnits()) {
      // Ignore non-enemy units.
      if (!self->isEnemy(enemyUnit->getPlayer())) {
        continue;
      }

      auto thisDistance = unit->getDistance(enemyUnit->getPosition());
      if (thisDistance < closestDistance) {
        closestDistance = thisDistance;
        defensePoint = unit->getPosition();
      }
    }

    // lower cooldown if we need to
    if (unit->getClientInfo<int>(ClientInfoKeys::cooldown)) {
      auto cooldown = unit->getClientInfo<int>(ClientInfoKeys::cooldown);
      cooldown--;
      unit->setClientInfo<int>(cooldown, ClientInfoKeys::cooldown);
    }

    if (unit->getType() == BWAPI::UnitTypes::Zerg_Extractor) {
      auto gasWorker = unit->getClientInfo<BWAPI::Unit>(firstGas);
      if (!gasWorker) {
        // Check first worker
        auto worker = unit->getClosestUnit(BWAPI::Filter::IsWorker && BWAPI::Filter::GetPlayer == self && BWAPI::Filter::IsGatheringMinerals);
        if (worker
          && worker != unit->getClientInfo<BWAPI::Unit>(secondGas)
          && worker != unit->getClientInfo<BWAPI::Unit>(thirdGas)) {
          unit->setClientInfo<BWAPI::Unit>(worker, firstGas);
        }
      }
      else if (!gasWorker->exists()
        || gasWorker == buildDrone) {
        unit->setClientInfo<BWAPI::Unit>(nullptr, firstGas);
      }
      else if (gasWorker->isIdle() || gasWorker->isGatheringMinerals()) {
        gasWorker->gather(unit);
      }
      gasWorker = unit->getClientInfo<BWAPI::Unit>(secondGas);
      if (!gasWorker) {
        // Check second worker
        auto worker = unit->getClosestUnit(BWAPI::Filter::IsWorker && BWAPI::Filter::GetPlayer == self && BWAPI::Filter::IsGatheringMinerals);
        if (worker
          && worker != unit->getClientInfo<BWAPI::Unit>(firstGas)
          && worker != unit->getClientInfo<BWAPI::Unit>(thirdGas)) {
          unit->setClientInfo<BWAPI::Unit>(worker, secondGas);
        }
      }
      else if (!gasWorker->exists()
        || gasWorker == buildDrone) {
        unit->setClientInfo<BWAPI::Unit>(nullptr, secondGas);
      }
      else if (gasWorker->isIdle() || gasWorker->isGatheringMinerals()) {
        gasWorker->gather(unit);
      }
      gasWorker = unit->getClientInfo<BWAPI::Unit>(thirdGas);
      if (!gasWorker) {
        // Check third worker
        auto worker = unit->getClosestUnit(BWAPI::Filter::IsWorker && BWAPI::Filter::GetPlayer == self && BWAPI::Filter::IsGatheringMinerals);
        if (worker
          && worker != unit->getClientInfo<BWAPI::Unit>(secondGas)
          && worker != unit->getClientInfo<BWAPI::Unit>(firstGas)) {
          unit->setClientInfo<BWAPI::Unit>(worker, thirdGas);
        }
      }
      else if (!gasWorker->exists()
        || gasWorker == buildDrone) {
        unit->setClientInfo<BWAPI::Unit>(nullptr, thirdGas);
      }
      else if (gasWorker->isIdle() || gasWorker->isGatheringMinerals()) {
        gasWorker->gather(unit);
      }
    }
    else if (unit->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony) {
      if (canAfford(BWAPI::UnitTypes::Zerg_Sunken_Colony)) {
        unit->morph(BWAPI::UnitTypes::Zerg_Sunken_Colony);
      }
    }
    else if (unit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk_Den) {
      if (!self->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments)
        && canAfford(BWAPI::UpgradeTypes::Muscular_Augments)) {
        unit->upgrade(BWAPI::UpgradeTypes::Muscular_Augments);
      }
      else if (!self->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines)
        && canAfford(BWAPI::UpgradeTypes::Grooved_Spines)) {
        unit->upgrade(BWAPI::UpgradeTypes::Grooved_Spines);
      }
    }
    else if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery) {
      if (self->completedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool)
        && !self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Lair)
        && 2 < self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
        && canAfford(BWAPI::UnitTypes::Zerg_Lair)) {
        unit->morph(BWAPI::UnitTypes::Zerg_Lair);
      }
      // find a worker for defense if our hatchery is under attack and we have no Hydralisks, trying to keep starting buildings from dying or taking 
      // heavy damage from enemy scouts/workers.
      if (unit->isUnderAttack()
        && !self->completedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk)
        && !unit->getClientInfo<int>(ClientInfoKeys::cooldown)) {
        unit->setClientInfo<int>(420, ClientInfoKeys::cooldown);
        auto closestWorker = unit->getClosestUnit(BWAPI::Filter::IsWorker && BWAPI::Filter::GetPlayer == self && BWAPI::Filter::IsGatheringMinerals);
        if (closestWorker) {
          auto enemy = unit->getClosestUnit(BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying);
          if (enemy) {
            closestWorker->attack(enemy);
            closestWorker->setClientInfo<int>(420, ClientInfoKeys::attackTime);
          }
        }
      }
    }
    else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lair) {
      if (canAfford(BWAPI::UpgradeTypes::Pneumatized_Carapace)) {
        unit->upgrade(BWAPI::UpgradeTypes::Pneumatized_Carapace);
      }
    }
  }
}

void ZergHell::checkEggs() {
  // Loop for eggs and set some client data regarding them, as some
  // information is lost on the final frame they morph.
  for (auto& unit : self->getUnits()) {
    // Ignore non-eggs
    if (unit->getType() != BWAPI::UnitTypes::Zerg_Egg) {
      continue;
    }

    if (unit->getBuildType() != BWAPI::UnitTypes::None) {
      unit->setClientInfo<int>(unit->getBuildType(), morphingType);
    }
  }
}

void ZergHell::checkEnemyBuildings() {
  // Loop through our fog of war tracker to see if tiles are visible.
  // If they are visible, we will clear them from the tracker, as
  // they will get added again down below.
  auto itr = fogOfWarBuildings.begin();
  while (itr != fogOfWarBuildings.end()) {
    if (BWAPI::Broodwar->isVisible(itr->first)) {
      itr = fogOfWarBuildings.erase(itr);
    }
    else {
      itr++;
    }
  }

  // Loop through enemy buildings and store their position in fogOfWarBuildings
  for (auto& unit : BWAPI::Broodwar->getAllUnits()) {
    // Ignore non-enemies and non-buildings
    if (!unit->getPlayer()->isEnemy(self) || !unit->getType().isBuilding())
      continue;

    fogOfWarBuildings[unit->getTilePosition()] = unit->getType();
  }
}

void ZergHell::checkScout() {
  // Check if we have a scout already.
  if (!scout) {
    // We don't have a scout, do we need a scout? Let's scout at 11 drones.
    if (self->visibleUnitCount(BWAPI::UnitTypes::Zerg_Drone) >= 11) {
      // Assign a scout.
      for (auto& location : startLocations) {
        if (!location.second) {
          scout = BWAPI::Broodwar->getClosestUnit((BWAPI::Position)location.first, BWAPI::Filter::GetPlayer == self && BWAPI::Filter::IsWorker);
          if (scout == buildDrone) {
            scout = nullptr;
          }
          break;
        }
      }
    }
  }
  else if (scout->exists()) {
    for (auto& location : startLocations) {
      if (!location.second) {
        if (scout->getDistance((BWAPI::Position)location.first) <= 400) {
          location.second = true;
          return;
        }
        else {
          scout->move((BWAPI::Position)location.first);
          return;
        }
      }
    }
    scout->move((BWAPI::Position)self->getStartLocation());
    scout = nullptr;
  }
}

void ZergHell::debugDraws() {
  if (buildDrone) {
    BWAPI::Broodwar->drawCircleMap(buildDrone->getPosition(), 10, BWAPI::Colors::Green, false);
  }
}

void ZergHell::morphLarva() {
  // Loop for larva and check conditions for morphing.
  for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
    // Ignore non-larva.
    if (unit->getType() != BWAPI::UnitTypes::Zerg_Larva) {
      continue;
    }

    // If we need supply and can afford the overlord, make an Overlord.
    if (needSupply()
      && canAfford(BWAPI::UnitTypes::Zerg_Overlord)) {
      unit->morph(BWAPI::UnitTypes::Zerg_Overlord);
    }
    // If less than 20 drones, morph a drone. Will need to revise this later
    // to be more sophisticated.
    else if (self->completedUnitCount(BWAPI::UnitTypes::Zerg_Drone) < 20
      && canAfford(BWAPI::UnitTypes::Zerg_Drone)) {
      unit->morph(BWAPI::UnitTypes::Zerg_Drone);
    }
    // If we can make and affor a Hydralisk, make it.
    else if (canAfford(BWAPI::UnitTypes::Zerg_Hydralisk)) {
      unit->morph(BWAPI::UnitTypes::Zerg_Hydralisk);
    }
  }
}

bool ZergHell::needSupply() {
  // Verify if we need a supply provider.
  // Loop for eggs and overlords, and manually count the expected
  // supply total. Dealing with Overlords as they hatch is annoying.
  int supplyTotal = 0;
  for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
    // Ignore non-eggs and non-Overlords.
    if (unit->getType() != BWAPI::UnitTypes::Zerg_Egg
      && unit->getType() != BWAPI::UnitTypes::Zerg_Overlord) {
      continue;
    }

    if (unit->getClientInfo<int>(morphingType) == BWAPI::UnitTypes::Zerg_Overlord
      || unit->getType() == BWAPI::UnitTypes::Zerg_Overlord) {
      supplyTotal += BWAPI::UnitTypes::Zerg_Overlord.supplyProvided();
    }
  }

  return self->supplyUsed() >= supplyTotal - 4;
}