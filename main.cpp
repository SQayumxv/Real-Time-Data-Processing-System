#include "stat.hpp"
#include "meminfo.hpp"
#include "uptime.hpp"
#include <iostream>
using namespace std;

void clearScreen() {
  cout << "\033[2J\033[0;0H";
  }

// allows us to clear and overwrite the console with new data

int main() {
  //using loop to constantly display an output and repeat the code
  while (true) {
     
    displayStat(); // will output the total cpu cores and data activity on each cpu core onto the console
    displayMemInfo(); // will output the RAM information onto the console
    displayTime(); // will output how long the up time and idle time have been onto the console
    displayEnergy(); // will output the amount of energy used in its active and idle state
    usleep(500000); // this will refresh the console with new data every 500ms
    clearScreen(); // this will clear the console 
    }
  }
  