#include "stat.hpp"

using namespace std;


 int num_core() {
  ifstream file("/proc/stat");
  string line;                 
  int num = 0;                 
  if(!file.is_open()) {
        cerr << "Stat file could not be opened....exiting." << endl; // error message if the stat file cannot be opened
        exit(EXIT_FAILURE);
    }
    while (getline(file, line)) {
    if (line[0] == 'c' && line[1] == 'p') {
      num++;
    }
  }

  file.close();
  --num; 

  return num;
}

void displayStat() {
  ifstream stat("/proc/stat");
  string line;
  int sum = 0;
  double percentage[200][200];                
  memset(percentage, 0, sizeof(percentage));
  int Cores = num_core();

  if (!stat.good()) {
    cerr << "Stat file could not be opened....exiting" << endl;
    exit(EXIT_FAILURE);
  }

  int CPU_percentage = 0; // initialising the variable that will be used in calculations

  printf("-----------------------------------------------------------------------");
    printf("\n");
    printf("Total CPU Cores: %d", Cores); // this will print the current total amount of CPU  in system
    printf("------------------------------------------------------------------");
    printf("\n");
  printf("CPU \t");
       printf("busy \t"); 
       printf("idle \t");
       printf("system \t");
       printf("nice\n") ;

     // obtain raw core data and convert them into percentages 
  
  while (getline(stat, line)) {
    stringstream linestream(line);
    string token;
    getline(linestream, token, ' ');
    double user = 0;
    double nice = 0;
    double system = 0;
    double idle = 0;

    // calculating the data %'s for each CPU core 
    
    if (token[0] == 'c' && token[1] == 'p' && !(token == "cpu")) {
      CPU_percentage++;
      linestream >> user >> nice >> system >> idle;
      sum = user + nice + system + idle;
      percentage[CPU_percentage][0] = (double)user * 100 / sum; // calculation of user %'s
      percentage[CPU_percentage][1] = (double)idle * 100 / sum; // calculation of idle %'s
      percentage[CPU_percentage][2] = (double)system * 100 / sum; // calculation of system %'s
      percentage[CPU_percentage][3] = (double)nice * 100 / sum; // calculation of nice %'s

        // this will print each data type and their percentage for each cpu core

        printf("cpu%d\t%.1f%\t%.1f%\t%.1f%\t%.1f%\n", CPU_percentage-1,
               percentage[CPU_percentage][0],
               percentage[CPU_percentage][1],
               percentage[CPU_percentage][2],
               percentage[CPU_percentage][3]);
      }
    }

   stat.close();
   stat.open("/proc/stat");
  
  if (!stat.good())
  {
    cerr << "Stat file could not be opened....exiting" << endl;
    exit(EXIT_FAILURE);
  }
    

   stat.close();
  }
  
