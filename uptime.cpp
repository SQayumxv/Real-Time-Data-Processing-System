#include "uptime.hpp"

void displayTime() {
  ifstream file("/proc/uptime");
  string line;
  int Cores = num_core();

  if (!file.good()) {
    cerr << "Upime file could not be opened.... exiting" << endl;
    exit(EXIT_FAILURE);
  }


for (int i = 0; i < 1; i++) {
    getline(file, line);
    stringstream linestream(line);
    string token;
    float upTime = 0;
 
    float idleTime = 0;

    linestream >> upTime >> idleTime;
    idleTime = idleTime / Cores; // dividing through total amount of cores to get the 
   
    displayData(upTime, idleTime);
  }
 
  file.close();

  file.open("/proc/uptime");
  if (!file.good()) {
    cerr << "Uptime file could not be opened file....exiting" << endl;
    exit(EXIT_FAILURE);
  }
  file.close();
}

void displayData(float uptime, float idletime) { 
  // this will convert the amount seconds spent in up time and idle time into the format of hours, minutes and seconds
  int hour_UP, minute_UP, second_UP, total_UP = 0;
  int hour_IDLE, minute_IDLE, second_IDLE = 0, total_IDLE=0;
  stringstream s_UP, s_IDLE;
  s_UP << uptime, s_UP >> total_UP;
  hour_UP = total_UP / 3600;
  minute_UP = (total_UP % 3600) / 60;
  second_UP = (total_UP % 3600) % 60;
  s_IDLE << idletime, s_IDLE >> total_IDLE;
  hour_IDLE = total_IDLE / 3600;
  minute_IDLE = (total_IDLE % 3600) / 60;
  second_IDLE = (total_IDLE % 3600) % 60;

  printf("SYSTEM");
  printf("\t");
  printf("UP for %d hours %d minutes and %d seconds\n", hour_UP, minute_UP, second_UP); // print the amount of active time spent since booting the system up
  printf("      \t");
  printf("IDLE for %d hours %d minutes and %d seconds\n", hour_IDLE, minute_IDLE, second_IDLE); // print the idle time since booting the system up
  printf("--------------------------------------------------------------------");
  printf("----\n");
}
void displayEnergy() {
  ifstream time("/proc/uptime");
  string line;
  int Cores = num_core();

  if (!time.good()) {
    cerr << "Uptime file could not be opened....exiting" << endl;
    exit(EXIT_FAILURE);
  }

  for (unsigned int i = 0; i < Cores; i++) {
    while (getline(time, line)) {
        stringstream linestream(line);
        string token;
        float upTime = 0;

        float idleTime = 0;

        float ActivePower = 0;

        float IdlePower = 0;

    linestream >> upTime >> idleTime;

    idleTime = idleTime / Cores; // getting the average idle time per core by diving by amount of cores in the system

    IdlePower = idleTime * 22 / 1000000; // idle time unit conversion, divide by 1000000 to get the it from joules to megajoules
    ActivePower = (upTime - idleTime) * 40 / 1000000; // up time unit conversion, divide by 1000000 to get it from joules to megajoules
 printf("ENERGY\t");
    printf("In Active State: %.2f MJoules\n", ActivePower); // printing the amount of energy used currently in its active state
    printf("      \t");
    printf("In idle State: %.2f MJoules\n", IdlePower); // printing amount of energy used currently in its idle state
  }
}
time.close();

  time.open("/proc/uptime");
  if (!time.good()) {
    cerr << "Uptime file could not be opened....exiting" << endl;
    exit(EXIT_FAILURE); 
  }
  time.close();
}
