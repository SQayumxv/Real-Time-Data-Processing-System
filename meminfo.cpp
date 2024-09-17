#include "meminfo.hpp"
void displayMemInfo()
{
    ifstream mem("/proc/meminfo");
    string line;
    if (!mem.good())
    {
        cerr << "Memory file could not be opened....exiting" << endl;
        exit(EXIT_FAILURE); // error message if mem files could not be opened
    }

    float Total, Free, Buffers, Cached = 0;
    for (int i = 0; i < 5; i++)
    {
        getline(mem, line);
        stringstream linestream(line);
        string token;
        getline(linestream, token, ':');

         // calculating the amount of memory in MB by dividing each categories value by 1024
         
        if (token == "MemTotal")
        {
            linestream >> Total;
            Total = Total / 1024;
        }
        if (token == "MemFree")
        {
            linestream >> Free;
            Free = Free / 1024;
        }
        if (token == "Buffers")
        {
            linestream >> Buffers;
            Buffers = Buffers / 1024;
        }
        if (token == "Cached")
        {
            linestream >> Cached;
            Cached = Cached / 1024;
        }
        else
        {
            continue;
    }
    }

      // printing the calculated meminfo output
    
printf("-----------------------------------------------------------------------\n");
printf("\t\tTotal: %0.0f MB\n", Total); // printing total Memory
printf("MEMORY");
printf("\tFree: %0.0f MB\n", Free); // printing the amount of free memory availabe
printf("\t\tCached: %0.0f MB\n", Cached); // printing the amount of memory being cached
printf("\t\tBuffers: %0.0f MB\n", Buffers); // printing the amount of buffered memory
printf("-----------------------------------------------------------------------\n");
            mem.close();
            mem.open("/proc/meminfo");
            if (!mem.good())
            {
                cerr << "Memory file could not be opened....exiting" << endl;
                exit(EXIT_FAILURE);
            }
    
     mem.close();
}
