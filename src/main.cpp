#include "data.h"
#include "server.h"
#include "gui.h"

#include <thread>

int main()
{
    Location locationInfo;
    CellSignalStrength cellInfo;
    
    locationInfo.imei = "None";

    std::thread server_thread(run_server, &locationInfo, &cellInfo);
    run_gui(&locationInfo, &cellInfo);
    server_thread.join();

    return 0;
}