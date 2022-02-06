typedef struct IUnknown IUnknown;

#include <BWAPI.h>
#include <BWAPI/Client.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "ZergHell.h"

void reconnect() {
  while (!BWAPI::BWAPIClient.connect()) {
    std::this_thread::sleep_for(std::chrono::milliseconds{ 1000 });
  }
}

int main() {
  std::cout << "Connecting..." << std::endl;
  reconnect();
  while (true) {
    std::unique_ptr<ZergHell> bot;
    std::cout << "waiting to enter match" << std::endl;
    while (!BWAPI::Broodwar->isInGame()) {
      BWAPI::BWAPIClient.update();
      if (!BWAPI::BWAPIClient.isConnected()) {
        std::cout << "Reconnecting..." << std::endl;
        reconnect();
      }
    }
    std::cout << "starting match!" << std::endl;
    std::cout << "Map: " << BWAPI::Broodwar->mapName() << std::endl;
    while (BWAPI::Broodwar->isInGame()) {
      for (auto& e : BWAPI::Broodwar->getEvents()) {
        switch (e.getType()) {
        case BWAPI::EventType::MatchStart:
          bot = std::make_unique<ZergHell>();
          break;
        default:
          break;
        }
      }

    bot->onFrame();
    BWAPI::BWAPIClient.update();
    }
  }
}